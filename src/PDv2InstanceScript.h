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

#ifndef MOD_PDUNGEON_V2_INSTANCE_SCRIPT_H
#define MOD_PDUNGEON_V2_INSTANCE_SCRIPT_H

#include "DataMap.h"
#include "InstanceScript.h"
#include "ObjectGuid.h"
#include "PDv2PackMgr.h"
#include "generator/PDBlockPlan.h"
#include "generator/PDv2WalkGrid.h"

#include <cstdint>
#include <vector>

class Creature;
class InstanceMap;
class Player;
class Unit;

namespace PDungeon
{
    // Every creature the instance script spawns carries this, and nothing else
    // on the map does. That is the whole gate: "is this a PDv2 dungeon mob" is
    // answered by CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY) != nullptr, so
    // the scaling and damage hooks can never reach a player's guardian, a GM's
    // test spawn or anything else that happens to stand on map 760.
    //
    // Get, never GetDefault, outside the spawner - GetDefault CREATES the entry
    // and would turn the gate into "anything the AI has ever looked at".
    char const* const PD_MOB_DATA_KEY = "mod-procedural-dungeon";

    // AzerothCore's DataMap (src/common/Utilities/DataMap.h); the precedent in
    // this fork is mod-dungeon-challenge's CreatureChallengeData.
    struct PDv2MobData : public DataMap::Base
    {
        uint8  role = 0;                // PDungeon::PackRole
        uint32 casterSpellId = 0;       // 0 for anything that is not a caster
        uint32 roomIndex = 0;           // index into the instance's room list
        bool   counted = false;         // this kill was already scored

        // Occupies a boss room's boss slot. Separate from `role` on purpose:
        // when the unlocked packs hold no role-2 member at all, PDv2PackMgr
        // fills the slot with a trash stand-in, and a run whose completion
        // waited on role 2 could then never finish. This flag says "the room
        // is done with you", which is what completion actually means, while
        // `role` keeps steering the AI.
        bool   isRunBoss = false;

        // Which affixes this creature carries: bit i-1 = `pdungeon_affixes`.id
        // i (PDv2Affixes.h). THE MASK IS THE MEMBERSHIP TEST for every affix
        // hook in the module - never HasAura(spellId), because the affix auras
        // are player-visible and dispellable and a purged marker must not
        // silently disarm a mechanic. The aura is the look; this is the fact.
        uint16 affixMask = 0;

        // Lil' Bro (affix 7) generation: 0 for a mob the dungeon spawned, 1 for
        // its children, 2 for theirs. Depth 2 does not split again, so one
        // carrier is worth 1 -> 2 -> 4 corpses and no more.
        uint8  splitDepth = 0;

        // Damage Reduce (affix 8) is the one affix a creature cannot answer
        // about itself: the carrier is somebody else, so the verdict costs a
        // grid search. It is taken lazily on the damage path and kept for
        // AFFIX_CARRIER_RECHECK_MS (PDv2Affixes.h says why both halves of that
        // matter). 0 means "never asked", which the first hit turns into an
        // answer - the arithmetic works out to a recheck either way.
        uint32 dmgReduceCheckedMs = 0;
        bool   dmgReduceActive = false;
    };

    // What a player is doing right now, in the form the UI wants to read it.
    //
    // difficulty and lootMultX100 are FROZEN into this at spawn time and every
    // gameplay hook reads them from here, never from the live account row: a
    // settings change in the middle of a run must not retune the mobs already
    // standing in the dungeon.
    struct PDv2RunState
    {
        uint32 startedMs = 0;
        uint32 elapsedSec = 0;
        uint16 killed = 0;
        uint16 total = 0;
        uint8  bossKilled = 0;
        uint8  bossTotal = 0;
        uint8  roomsCleared = 0;
        uint8  roomsTotal = 0;
        // The 1..100 dial, frozen at spawn. 0 rather than the dial's floor is
        // the deliberate "no run bound yet" value: the scaling hooks read it as
        // "multiply by nothing", and SpawnFromPlan overwrites it before the
        // first SummonCreature, so no creature can ever be built from it.
        uint8  difficulty = 0;
        uint16 lootMultX100 = 100;
        bool   complete = false;
        bool   started = false;
    };

    // One PDv2 run.
    //
    // The map has no terrain server-side, which changes what an instance script
    // has to do here. Two consequences drive this class:
    //
    //   * the server does not know where the floor is, so a player who steps off
    //     a platform falls for ever. Nothing in the core will stop them, because
    //     there is no ground to hit. The fall catcher below IS the floor, as far
    //     as the server is concerned.
    //   * creatures cannot be placed by asking the map for a height. They are
    //     placed at the plan's own block coordinates instead, at the kit's floor
    //     plane - the same number the client's terrain was generated at.
    //
    // Attached by PDv2MapScript through OnBeforeCreateInstanceScript, so no
    // instance_template.ScriptId binding is needed.
    class PDv2InstanceScript : public InstanceScript
    {
    public:
        explicit PDv2InstanceScript(InstanceMap* map);

        void Initialize() override { }
        void Update(uint32 diff) override;
        void OnPlayerEnter(Player* player) override;

        // The walkable surface of this instance's plan, or nullptr while no
        // plan is bound yet (or its masks are missing). The creature AI paths
        // over this; it is built once on first entry and read-only afterwards,
        // and AI updates run on this map's own update thread, so no lock.
        WalkGrid const* GetWalkGrid() const { return _gridReady ? &_grid : nullptr; }

        // The live run. Read-only for everyone outside this class: the counters
        // are only ever moved by OnMobDied, on this map's own update thread.
        PDv2RunState const& GetRunState() const { return _run; }

        // The affixes this run hands to every affixed mob, as a bit mask
        // (PDv2Affixes.h). Frozen at spawn beside the difficulty, for the same
        // reason: a `.pdungeon v2 set` mid-run must not change what the mobs
        // already standing in the dungeon do.
        //
        // A creature's own mask is either this or nothing, so the two never
        // disagree - but a hook whose carrier is a DIFFERENT creature (Damage
        // Reduce) needs the run-wide answer to decide whether looking for one
        // is worth anything at all.
        uint16 GetRunAffixMask() const { return _runAffixMask; }

        // Re-casts the affix auras a carrier's mask names. The AI calls this
        // from JustReachedHome: an evade strips every aura (core behaviour),
        // which disarmed nothing - the mechanics ride the tag - but left the
        // carrier LOOKING clean (operator report, first affix test). The
        // spawn-time health effects are deliberately NOT re-applied: max
        // health survives an aura wipe.
        void ReapplyAffixAuras(Creature* creature, uint16 affixMask) const;

        // The account this instance was BUILT for - the one whose stored plan
        // the terrain and the spawns came from, which is not necessarily the
        // account of whoever is reading. 0 until the first player enters.
        uint32_t GetAccountId() const { return _accountId; }

        // Returns and CLEARS "a counter moved since you last asked". The UI
        // polls the instance once a second and only sends a frame when this
        // says something happened, so a player standing still costs nothing on
        // the wire. elapsedSec deliberately does NOT set it: it is a clock, not
        // an event, and a dirty flag that is always true is not a flag.
        bool ConsumeRunDirty();

        // A tagged dungeon mob died. Called by PDv2MobAI::JustDied; `killer` is
        // whatever landed the blow, which may be a pet or nothing at all.
        void OnMobDied(Creature* creature, Unit* killer);

    private:
        void SpawnFromPlan(BlockPlan const& plan);

        // Summons ONE dungeon mob: the floor plane, the disabled gravity, the
        // tag copied off `proto`, the run's affix auras and their spawn-time
        // health effects. Every creature this module puts on the map is born
        // here, so "what a PDv2 mob is" has exactly one definition and a Lil'
        // Bro child cannot drift from the mobs it was cut out of.
        //
        // `baseHealthOverride` is written BEFORE the affix multipliers, which
        // is what makes a split child a small copy that a Big Boy bit then
        // grows again - the order that module's own split relies on.
        // Returns nullptr when the summon failed; the caller owns the counters.
        Creature* SpawnTaggedMob(uint32 entry, PDv2MobData const& proto,
                                 float x, float y, float z,
                                 uint32 baseHealthOverride = 0);

        // Lil' Bro (affix 7). Called from OnMobDied BEFORE the death moves any
        // counter, which is the only ordering that keeps them honest.
        void SplitOnDeath(Creature* parent, PDv2MobData const& parentTag, Unit* killer);
        void MarkRunDirty() { _runDirty = true; }
        void FinishRun();
        void RollBonusLoot(Unit* killer);
        void DespawnAll();
        void EnsureWalkGrid(BlockPlan const& plan);
        void CatchFallers();
        void EvictDisconnected();

        uint32_t _accountId = 0;
        bool     _spawned = false;
        uint32_t _spawnedSeed = 0;              // plan this instance is built for
        std::vector<ObjectGuid> _spawnedGuids;  // for a rebuild when the plan changes
        PDv2RunState _run;
        std::vector<AffixDef> _runAffixes;      // the rows, for re-casting on a split
        uint16   _runAffixMask = 0;             // the same rows as bits
        bool     _runDirty = false;
        uint64   _leaderGuid = 0;               // the character that opened this run
        std::vector<uint16> _roomAlive;         // per room, index-aligned with the spawn draw
        uint32   _fallCheckTimer = 0;
        float    _entranceX = 0.0f;
        float    _entranceY = 0.0f;
        float    _entranceZ = 0.0f;
        bool     _haveEntrance = false;
        WalkGrid _grid;
        bool     _gridReady = false;
        bool     _gridTried = false;
    };
}

#endif
