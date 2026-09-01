/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Creature.h"
#include "Log.h"
#include "LootMgr.h"
#include "Map.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Unit.h"

// PDv2 scaling and loot hooks.
//
// 01 §8 gives difficulty exactly two levers - HP and damage - and gives the
// loot multiplier exactly one - loot and gold. Everything that implements
// those two sentences lives here, so the list of things difficulty touches can
// be read in one file instead of inferred from five.
//
// Since the 2026-08-08 operator directive difficulty is an INTEGER 1..100 and
// each lever is a straight line over it, mirroring mod-dungeon-challenge:
//   HP     x100 = 100 + difficulty * V2.Diff.HealthPctPerLevel   (default 5)
//   damage x100 = 100 + difficulty * V2.Diff.DamagePctPerLevel   (default 2)
// The two percentages are config rather than constants for the reason that
// module's are: they are the numbers an operator retunes after watching a
// night of play, and a rebuild is a bad price for that.
//
// The value both levers use is FROZEN into the instance's run state at spawn
// time (PDv2InstanceScript::SpawnFromPlan). Nothing here reads the live
// account row: a player who raises difficulty mid-run must not watch the mobs
// around them get tougher, and a run that paid out at difficulty 1 must not be
// auditable as a difficulty 100 run.
namespace
{
    using namespace PDungeon;

    // 01 §8: every spawn fights at 80, whatever its template says. That is what
    // makes an arbitrary Blizzard creature usable as dungeon content.
    uint8 const PD_MOB_LEVEL = 80;

    // The gate for the creature-level hooks. Deliberately NOT the PDv2MobData
    // tag: SelectLevel runs inside SummonCreature, before the instance script
    // gets the pointer back and can tag anything (Creature.cpp:319 -
    // AIM_Initialize and the whole creation path run under AddToMap). So the
    // map is the gate here, and everything hostile standing on the dungeon map
    // is dungeon content by definition.
    bool IsDungeonCreature(Creature* creature)
    {
        if (!creature || !sPDv2Mgr->IsEnabled())
        {
            return false;
        }

        Map* map = creature->FindMap();
        if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return false;
        }

        // A critter is decoration, not content: it must not be forced to level
        // 80 and must not carry the difficulty health multiplier. This gate is
        // on the TEMPLATE type, which - unlike the spawn tag - is already true
        // inside SummonCreature, where OnBeforeCreatureSelectLevel runs.
        if (creature->IsCritter())
        {
            return false;
        }

        // A guardian, totem or minion a player brought with them keeps its own
        // level and its own numbers - the same owner check the AI binder uses.
        return !creature->GetCharmerOrOwnerPlayerOrPlayerItself();
    }

    // The run this creature belongs to, or nullptr. Cheap enough for the damage
    // path: a map-id compare, then a dynamic_cast that only happens on the
    // dungeon map.
    PDv2InstanceScript* RunOf(Creature* creature)
    {
        if (!creature || !sPDv2Mgr->IsEnabled())
        {
            return nullptr;
        }

        Map* map = creature->FindMap();
        if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return nullptr;
        }
        return dynamic_cast<PDv2InstanceScript*>(creature->GetInstanceScript());
    }

    // The DAMAGE multiplier, x100, for a creature the DUNGEON spawned. The tag
    // is part of the gate here (unlike IsDungeonCreature above) because by the
    // time anything deals damage it exists, and it is what keeps a GM's test
    // spawn or a summoned add out of the multiplier.
    uint32 DungeonDamageMultX100(Unit* attacker)
    {
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        if (!creature || !creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY))
        {
            return 100;
        }

        PDv2InstanceScript* run = RunOf(creature);
        if (!run)
        {
            return 100;
        }

        uint32 const difficulty = run->GetRunState().difficulty;
        return 100 + difficulty *
                         static_cast<uint32>(sPDv2Mgr->GetConfig().diffDamagePctPerLevel);
    }

    void ScaleOutgoing(Unit* attacker, uint32& damage)
    {
        if (!damage)
        {
            return;
        }

        uint32 const multX100 = DungeonDamageMultX100(attacker);
        if (multX100 == 100)
        {
            return;
        }
        damage = static_cast<uint32>(static_cast<uint64>(damage) * multX100 / 100);
    }

    // Hell Touched (affix 10), the attacker side: a carrier's landed hit sears
    // its target for a flat 666 on top and stacks the stat debuff. Separate
    // from ScaleOutgoing on purpose - that one also serves the periodic tick
    // hook, and a damage-over-time tick must NOT sear (that module excludes it
    // in as many words, DungeonChallengeScripts.cpp:1052).
    void HellTouchedOnHit(Unit* attacker, Unit* target)
    {
        Creature* creature = attacker ? attacker->ToCreature() : nullptr;
        PDv2MobData const* tag = creature
                                     ? creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY)
                                     : nullptr;
        if (!tag || !HasAffix(tag->affixMask, PD_AFFIX_HELL_TOUCHED))
        {
            return;
        }
        ApplyHellTouched(target);
    }

    // Damage Reduce (affix 8), the target side: a dungeon mob standing within
    // 30 yd of a carrier takes a quarter less. Every gate here is cheaper than
    // the one after it, in that order - most damage on this server never gets
    // past the first.
    void ReduceIncoming(Unit* target, uint32& damage)
    {
        if (!damage)
        {
            return;
        }

        // Only a mob the DUNGEON spawned can be shielded. A player, a pet or a
        // GM's test spawn has no tag and stops here.
        Creature* creature = target ? target->ToCreature() : nullptr;
        PDv2MobData* tag = creature
                               ? creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY)
                               : nullptr;
        if (!tag)
        {
            return;
        }

        // Then the run: below the affix's own minDiff no mob in this dungeon
        // carries it, so there is nobody to look for and the search never runs.
        // That is what keeps the cost off every run that is not at the top of
        // the dial.
        PDv2InstanceScript* run = RunOf(creature);
        if (!run || !HasAffix(run->GetRunAffixMask(), PD_AFFIX_DAMAGE_REDUCE))
        {
            return;
        }

        // And only then the grid search, itself cached for half a second.
        if (!DamageReduceActive(creature, tag))
        {
            return;
        }

        damage = static_cast<uint32>(static_cast<uint64>(damage)
                                     * (100 - AFFIX_DAMAGE_REDUCE_PCT) / 100);
    }
}

// 01 §8's two difficulty levers, half one: normalise the level, then scale HP.
class PDv2CreatureScaling : public AllCreatureScript
{
public:
    PDv2CreatureScaling() : AllCreatureScript("PDv2CreatureScaling") { }

    void OnBeforeCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature,
                                     uint8& level) override
    {
        if (!IsDungeonCreature(creature))
        {
            return;
        }

        // Setting the level here and NOWHERE else is the whole point: the core
        // regenerates health, mana, base weapon damage and attack power from
        // CreatureBaseStats(80) immediately afterwards (Creature.cpp:1495-1556),
        // so a level-20 template becomes a level-80 creature by the same path
        // Blizzard content uses. Any manual stat arithmetic here would be a
        // second, worse copy of those lines - do not reintroduce one.
        level = PD_MOB_LEVEL;
    }

    void OnCreatureSelectLevel(CreatureTemplate const* /*cinfo*/, Creature* creature) override
    {
        if (!IsDungeonCreature(creature))
        {
            return;
        }

        // The run's difficulty, not the account's: SpawnFromPlan froze it
        // before the first SummonCreature, so it is already there.
        PDv2InstanceScript* run = RunOf(creature);
        uint32 const difficulty = run ? run->GetRunState().difficulty : 0u;
        uint32 const multX100 = 100 + difficulty *
            static_cast<uint32>(sPDv2Mgr->GetConfig().diffHealthPctPerLevel);
        if (multX100 == 100)
        {
            return;
        }

        // HP ONLY. 01 §8 gives difficulty two levers - HP and damage - and
        // damage is the UnitScript below. Nothing here may touch XP, loot,
        // mana or attack power, or difficulty stops being the one honest knob
        // it was designed to be.
        //
        // SetDungeonHealth writes the four lines SelectLevel itself uses for
        // health, the UNIT_MOD_HEALTH base value included - without that last
        // one the creature's max health is recomputed back to the unscaled
        // number the first time anything touches its stats. The affixes' own
        // health effects go through the same helper, so there is one place on
        // this map that knows how to move a creature's health.
        uint64 const scaled = static_cast<uint64>(creature->GetMaxHealth()) * multX100 / 100;
        SetDungeonHealth(creature, static_cast<uint32>(scaled));
    }
};

// 01 §8's two difficulty levers, half two: outgoing damage.
class PDv2DamageScaling : public UnitScript
{
public:
    PDv2DamageScaling() : UnitScript("PDv2DamageScaling", true,
        { UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
          UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        // Attacker's multiplier first, then the attacker's affix, then the
        // target's reduction - the order mod-dungeon-challenge applies them in
        // (DungeonChallengeScripts.cpp:939-965), so the quarter comes off the
        // scaled number rather than the raw one and the 666 is never scaled or
        // reduced by either.
        ScaleOutgoing(attacker, damage);
        HellTouchedOnHit(attacker, target);
        ReduceIncoming(target, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage,
                                SpellInfo const* /*spellInfo*/) override
    {
        // Signed, and the sign is load-bearing: absorbs and other reductions
        // arrive here as a negative number, and multiplying those by the
        // difficulty would make a HARDER dungeon absorb MORE. Only real damage
        // is scaled.
        //
        // No level-normalisation factor is applied to spell damage, and none is
        // needed in v1: the imported stock (84263-84290) is native level 80/82,
        // so a spell's own coefficients already land where they should. A future
        // pack with sub-80 members WILL need one - a level-20 template
        // normalised to 80 still casts a level-20 spell.
        if (damage <= 0)
        {
            return;
        }

        uint32 scaled = static_cast<uint32>(damage);
        ScaleOutgoing(attacker, scaled);
        HellTouchedOnHit(attacker, target);
        ReduceIncoming(target, scaled);
        damage = static_cast<int32>(scaled);
    }

    void ModifyPeriodicDamageAurasTick(Unit* /*target*/, Unit* attacker, uint32& damage,
                                       SpellInfo const* /*spellInfo*/) override
    {
        // attacker can be nullptr here when the caster despawned while its DoT
        // is still ticking (UnitScript.h says so); ScaleOutgoing handles it.
        //
        // No Damage Reduce on this path, deliberately: that module spends the
        // reduction in the melee and direct-spell hooks only and leaves ticks
        // alone (DungeonChallengeScripts.cpp:1052-1054 applies the attacker's
        // multiplier here and nothing else). A shield that stopped DoTs too
        // would be a different affix.
        ScaleOutgoing(attacker, damage);
    }

    // ModifyHealReceived is deliberately NOT overridden. 01 §8 gives difficulty
    // HP and damage, full stop: scaling a mob's self-heal too would compound
    // with the HP lever and make d = 3.0 far more than three times the fight
    // it advertises.
};

// 01 §8's loot lever. The other half of it - the FL mat bonus roll - lives in
// PDv2InstanceScript::OnMobDied, because it belongs to a KILL in a run rather
// than to a global hook that fires everywhere on the server.
class PDv2LootScript : public PlayerScript
{
public:
    PDv2LootScript() : PlayerScript("PDv2LootScript", { PLAYERHOOK_ON_BEFORE_LOOT_MONEY }) { }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!player || !loot || !loot->gold || !sPDv2Mgr->IsEnabled())
        {
            return;
        }

        Map* map = player->FindMap();
        if (!map || map->GetId() != sPDv2Mgr->GetConfig().mapId)
        {
            return;
        }

        PDv2InstanceScript* run = dynamic_cast<PDv2InstanceScript*>(player->GetInstanceScript());
        if (!run)
        {
            return;
        }

        // lootMult = base(difficulty) x (0.8 + 0.5 x r) - the difficulty is
        // ALREADY inside it (PDv2GameMath.h, GameLootMultX100). Multiplying by
        // it again here would pay a difficulty-100 run 300 times over, not
        // three.
        //
        // The creature's native loot table is untouched, here and everywhere:
        // farming stock creatures for stock drops is the whole design of 01 §8,
        // and a module that rewrote those tables would be a different feature.
        uint16 const lootMultX100 = run->GetRunState().lootMultX100;
        if (lootMultX100 == 100)
        {
            return;
        }
        loot->gold = static_cast<uint32>(static_cast<uint64>(loot->gold) * lootMultX100 / 100);
    }
};

void AddPDv2ScalingScripts()
{
    new PDv2CreatureScaling();
    new PDv2DamageScaling();
    new PDv2LootScript();
}
