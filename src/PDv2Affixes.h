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

#ifndef MOD_PDUNGEON_V2_AFFIXES_H
#define MOD_PDUNGEON_V2_AFFIXES_H

#include "Define.h"

class Creature;
class Unit;

namespace PDungeon
{
    struct PDv2MobData;
}

// PDv2's half of the affixes: the BEHAVIOUR that mod-dungeon-challenge keeps in
// its own script hooks, re-gated on PDv2's own state.
//
// The spells stay that module's (mod_pdungeon_affixes.sql says so at length):
// nothing here creates, edits or reads one of its rows, and nothing here
// includes one of its headers. What is shared is the DEFINITION of an affix -
// the ids below and the numbers each mechanic uses - so that a Big Boy is the
// same creature in both dungeons. Every one of those numbers carries the
// file:line it was read from on 2026-08-08.
//
// MEMBERSHIP IS THE TAG, NEVER THE AURA. Each hook asks the spawn tag's
// `affixMask` whether this creature carries the affix, and never
// `HasAura(spellId)`. The affix auras are player-visible and can be dispelled,
// purged or stripped by an evade, and a mechanic that quietly disarms itself
// when its marker aura goes missing is the in-house "HasAura marker gating"
// trap. The aura is the look; the mask is the fact.
namespace PDungeon
{
    // The affix ids as they appear in `pdungeon_affixes`.`id`. They ARE
    // mod-dungeon-challenge's `DungeonChallengeAffix` values
    // (DungeonChallenge.h:34-49) - mod_pdungeon_affixes.sql was extracted from
    // that enum - so the two must never drift.
    //
    // Only the six with a behaviour half are named. Affixes 2 (Speedy), 5 (CC
    // Immunity) and 6 (Heavy Hits), and the damage half of 9, are pure DBC
    // auras: they arrive whole with the spell and nothing in this module may
    // implement them a second time.
    enum PDv2Affix : uint8
    {
        PD_AFFIX_CALL_FOR_HELP = 1,
        PD_AFFIX_BIG_BOY       = 3,
        PD_AFFIX_IMMOLATION    = 4,
        PD_AFFIX_LIL_BRO       = 7,
        PD_AFFIX_DAMAGE_REDUCE = 8,
        PD_AFFIX_BIGGER_BOY    = 9,
        PD_AFFIX_HELL_TOUCHED  = 10
    };

    // Bit i-1 carries affix id i, so the table's whole 1..10 range fits one
    // uint16 with room for an operator's own rows up to 16. An id outside that
    // window maps to no bit at all, which reads as "this creature does not have
    // it" everywhere rather than shifting past the end of the word.
    constexpr uint16 AffixBit(uint8 id)
    {
        return (id >= 1 && id <= 16) ? static_cast<uint16>(1u << (id - 1)) : static_cast<uint16>(0);
    }

    constexpr bool HasAffix(uint16 affixMask, uint8 id)
    {
        return (affixMask & AffixBit(id)) != 0;
    }

    // Call for Help (1): on entering combat, every ally within 30 yd joins the
    // fight on the same victim (DungeonChallengeScripts.cpp:692-695).
    float const AFFIX_CALL_FOR_HELP_RANGE_YD = 30.0f;

    // Big Boy (3) and Bigger Boy (9): x1.5 max health EACH
    // (DungeonChallenge.cpp:684 and :699 - the same literal in both cases).
    float const AFFIX_BIG_BOY_HEALTH_MULT = 1.5f;

    // Immolation Aura (4): every 2 s, difficulty x 80 fire damage to every
    // player within 8 yd (DungeonChallengeScripts.cpp:711-722). The damage is
    // ENVIRONMENTAL there, not a spell - it has no caster, so nothing resists,
    // reflects or scales it, and it does not run through the difficulty damage
    // lever a second time.
    uint32 const AFFIX_IMMOLATION_INTERVAL_MS = 2000;
    uint32 const AFFIX_IMMOLATION_DMG_PER_DIFF = 80;
    float const AFFIX_IMMOLATION_RANGE_YD = 8.0f;

    // Lil' Bro (7): on death, two copies at 10 % of the dying creature's max
    // health, twice over - 1 -> 2 -> 4 and then no more
    // (DungeonChallengeScripts.cpp:849 for the depth, :868 for the tenth, :875
    // for the pair). The children inherit the parent's WHOLE affix set there
    // (:890), so a Lil' Bro child is a Lil' Bro too until the depth stops it.
    float const AFFIX_LIL_BRO_HEALTH_PCT = 0.1f;
    uint8 const AFFIX_LIL_BRO_MAX_DEPTH = 2;
    uint8 const AFFIX_LIL_BRO_CHILDREN = 2;

    // Damage Reduce (8): allies within 30 yd of a carrier take 25 % less
    // (DungeonChallenge.cpp:742-745 for the range and the quarter,
    // DungeonChallengeScripts.cpp:963-964 for where it is spent). The carrier
    // shields itself too - that module's loop has no "not me" case and this
    // one keeps that.
    uint32 const AFFIX_DAMAGE_REDUCE_PCT = 25;
    float const AFFIX_DAMAGE_REDUCE_RANGE_YD = 30.0f;

    // The candidate radius for the search, padded far beyond the 30 yd cut so
    // the candidates are a strict superset of anything the distance test could
    // accept: GetDistance subtracts BOTH object sizes, and Big Boy plus Bigger
    // Boy scale a model up to 2.25x (DungeonChallenge.cpp:18 and :709-732).
    float const AFFIX_CARRIER_SEARCH_YD = 50.0f;

    // How long a carrier verdict stands before it is asked again. The module's
    // own decision cadence (PDv2CreatureAI's REPATH_INTERVAL_MS): long enough
    // that an area spell landing on a room does not run one grid search per mob
    // per hit, short enough that a carrier who dies stops shielding within half
    // a second of dying.
    uint32 const AFFIX_CARRIER_RECHECK_MS = 500;

    // Hell Touched (10): every landed hit adds 666 environmental fire on top
    // and stacks a debuff on the player - -2 % stats, 10 s, ten stacks
    // (DungeonChallengeScripts.cpp:947-955, melee, and :1001-1009, direct
    // spells; ticks are excluded there and here).
    uint32 const AFFIX_HELL_TOUCHED_DAMAGE = 666;

    // The debuff is that module's spell, cast on the PLAYER, and it is the one
    // affix spell `pdungeon_affixes` deliberately does NOT carry: no creature
    // ever wears it, so it is not assignment metadata (mod_pdungeon_affixes.sql
    // says so). The id is that module's SPELL_AFFIX_HELL_TOUCHED_DEBUFF
    // (DungeonChallenge.h:58). On a server without those spell rows the cast
    // no-ops, exactly like the affix auras do.
    uint32 const SPELL_AFFIX_HELL_TOUCHED_DEBUFF = 900053;

    // Writes a creature's max health through ALL FOUR of the lines the core's
    // own SelectLevel uses (Creature.cpp:1495-1556), the UNIT_MOD_HEALTH base
    // value included. Miss that last one and the next stat recompute quietly
    // puts the creature back to its unscaled health (pd/02 §7).
    //
    // mod-dungeon-challenge writes two of them (SetMaxHealth + SetFullHealth,
    // DungeonChallenge.cpp:684-685), which is why that module needs a loop that
    // notices lost affixes and re-applies everything. PDv2 pays the two extra
    // lines instead and needs no such loop.
    void SetDungeonHealth(Creature* creature, uint32 health);

    // The spawn-time half of Big Boy and Bigger Boy, applied AFTER the
    // difficulty HP scale - the same order mod-dungeon-challenge uses
    // (AssignAffixesToCreatures scales first, then applies the affixes,
    // DungeonChallenge.cpp:655-661 with the note at :770-771). Both bits on one
    // creature is 1.5 x 1.5 = x2.25, the multiplier that module's own mass-pull
    // note cites (DungeonChallenge.cpp:721-723).
    void ApplyAffixSpawnHealth(Creature* creature, uint16 affixMask);

    // Damage Reduce (8), asked from the TARGET's side: is a carrier standing
    // within 30 yd of this creature right now?
    //
    // The carrier is a DIFFERENT creature, so this is the one affix that cannot
    // be answered from a bit test - it takes a grid search. That module runs
    // the search ONCE, when it hands the affix out, and writes a fixed
    // reduction onto whoever stood nearby at the time (DungeonChallenge.cpp:
    // 704-748). Here it is asked at damage time and cached for
    // AFFIX_CARRIER_RECHECK_MS, because PDv2's mobs are not standing where they
    // spawned by the time a fight is on: an answer frozen at spawn would shield
    // a mob that walked away from its carrier and leave one that walked to it
    // unprotected. `tag` carries the cache and is written through.
    bool DamageReduceActive(Creature* victim, PDv2MobData* tag);

    // Hell Touched (10), spent on whatever a carrier just hit. Does nothing to
    // a target that is not a player - that module only ever reaches for
    // ToPlayer() here, and a mob searing another mob is not the affix.
    void ApplyHellTouched(Unit* target);
}

#endif
