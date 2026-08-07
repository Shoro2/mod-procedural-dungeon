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
    };

    // What a player is doing right now, in the form the UI wants to read it.
    //
    // diffX100 and lootMultX100 are FROZEN into this at spawn time and every
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
        uint16 diffX100 = 100;
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
