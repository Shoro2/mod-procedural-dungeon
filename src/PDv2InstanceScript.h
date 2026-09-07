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
#include "Position.h"
#include "generator/PDBlockPlan.h"
#include "generator/PDv2WalkGrid.h"

#include <cstdint>
#include <unordered_map>
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

        // Round B. false for the patrol (B4) and the ambush mobs (B5): they
        // fight, scale, split and drop loot like any dungeon mob but move no
        // run counter and no barrier - risk on the road, not progress.
        bool   countsForRun = true;

        // B4: this creature walks the spine out of combat. The goal cell is
        // the far end of its beat in GLOBAL grid cells; the AI plans the
        // route on its first idle tick and reverses it at either end.
        bool   isPatrol = false;
        int    patrolGoalCellX = 0;
        int    patrolGoalCellY = 0;

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

        // Round B / B1. Forgets a player's pending respawn when they leave
        // this map - the .cpp says which core ordering makes that necessary.
        void OnPlayerLeave(Player* player) override;

        // Disarms unselectable summons (void zones) so they decorate instead of
        // damaging - see the .cpp for why this is scoped to this map.
        void OnCreatureCreate(Creature* creature) override;

        // The dungeon's own ground-damage tick: once per second, a player
        // standing inside ANY tracked void zone takes ONE application of the
        // zone's damage spell - however many zones overlap. Riding the same
        // 1 Hz branch as the fall catcher keeps the cadence identical to the
        // vanilla aura it replaces.
        void TickVoidZones();

        // The walkable surface of this instance's plan, or nullptr while no
        // plan is bound yet (or its masks are missing). The creature AI paths
        // over this.
        //
        // It is built once on first entry and written afterwards ONLY by the
        // instance itself, through SetCellsWalkable: since Round B (B3) a
        // closed barrier seals its lane cells and reopens them when it falls,
        // because creatures ignore GameObject collision and this grid is the
        // only thing they path over. Every reader and that one writer run on
        // this map's own update thread, so there is still no lock.
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

        // Round B / B1. A player died on this map: ZoneScript hook, reached
        // through GetInstanceScript() from Unit::setDeathState - before any
        // corpse or ghost exists. Only records the death; the resurrect runs
        // on the 1 Hz tick (RespawnPending), never inside the death itself.
        void OnUnitDeath(Unit* unit) override;

        // The player clicked an altar: binds the run's respawn point to it.
        // False for a GameObject that is not one of this instance's altars.
        bool BindAltar(Player* player, ObjectGuid const& altarGuid);

        // True while a death is waiting for its tick; the release veto in
        // PDClientLink reads it so a quick 'release spirit' cannot beat the
        // tick to the graveyard.
        bool HasPendingRespawn(ObjectGuid const& playerGuid) const
        {
            return _pendingRespawn.find(playerGuid) != _pendingRespawn.end();
        }

    private:
        void SpawnFromPlan(BlockPlan const& plan);

        // Places the plan's props (torches, braziers). Called from the SAME
        // guard as SpawnFromPlan, so decor and creatures are built and torn
        // down together and re-entering a dungeon can never double up on
        // either. Reads the plan and never writes it; the spots come from the
        // decor stream, which is seeded from the layout seed - so the torches
        // stand where they stood the last time this seed was entered.
        //
        // `outPositions` collects each summoned prop's world position, in the
        // same order it is placed. SpawnCritters reads it back: a scatter
        // decor rule and the critter rule draw from the SAME candidate cells
        // on two RNG streams that know nothing about each other, so this is
        // the only record either side has of where the other one landed.
        void SpawnDecor(BlockPlan const& plan, std::vector<Position>& outPositions);

        // The kit's structural props (fountains, cave-ins, columns ...):
        // GameObjects pinned per variant by the chunk-meta 'props' anchors,
        // spawned because MDDF doodads never collide with players (measured,
        // first Phase-4 T2 round). Same guard, same teardown as SpawnDecor.
        void SpawnKitProps(BlockPlan const& plan);

        // Ambient life: BuildCritterPlan's spots, summoned as ownerless,
        // tagless creatures. `decorPositions` is SpawnDecor's output for the
        // SAME layout - a critter within CRITTER_DECOR_CLEAR_YD of a prop is
        // dropped rather than summoned, because the two planners can and do
        // pick the same cell centre. Same guard, same teardown shape as
        // SpawnDecor, but its own GUID list: critters are creatures torn down
        // with DespawnOrUnsummon, not the GameObject Delete() the props get.
        void SpawnCritters(BlockPlan const& plan, std::vector<Position> const& decorPositions);

        // One Shifting Cache per dead-end stub, on its junction square. A
        // reward, not a look: deliberately NOT behind Decor.Enable. Shares
        // the decor GUID list so one teardown owns every summoned object.
        void SpawnDeadEndChests(BlockPlan const& plan);

        // Round B / B1. One altar per altar room (IsAltarRoom), in chain
        // order, on a walkable cell beside the room's entry anchor. The
        // respawn spot itself is the entry anchor. Same guard and teardown
        // as the decor.
        struct Altar
        {
            int chainIndex = 0;
            float x = 0.0f;         // the respawn spot (entry anchor), world
            float y = 0.0f;
            float z = 0.0f;
            ObjectGuid guid;        // the altar GameObject; empty when none could be seated
        };
        void SpawnAltars(BlockPlan const& plan);

        // Round B / B3. One sealed portcullis per boss segment, standing in
        // the boss room's own doorway - the cell inside the entry edge that
        // segment's corridor run arrives through, found with the SAME walk the
        // validator proved the spine with. Called after SpawnAltars so the
        // walk grid EnsureWalkGrid built is there to be cut: the GameObject
        // stops the PLAYER, and the four lane cells taken out of the grid stop
        // the CREATURES, which ignore GameObject collision entirely.
        struct Barrier
        {
            int segment = 0;                // k; the boss room is chain b_k
            ObjectGuid guid;                // the portcullis, empty once opened
            std::vector<GridPoint> cells;   // the four lane cells it seals
            float x = 0.0f;                 // where it stands, for the hint radius
            float y = 0.0f;
            bool open = false;
            bool hinted = false;
        };
        void SpawnBarriers(BlockPlan const& plan);

        // The other half of OnUnitDeath, on the 1 Hz tick where a resurrect
        // is safe: everyone recorded there who is still on this map and still
        // dead comes back alive at RespawnAltarFor's spot with resurrection
        // sickness. Called after CatchFallers, so a death below the floor is
        // pulled onto the map before it is sent to its altar.
        void RespawnPending();

        // The altar a player respawns at: the one they bound, else the
        // entrance room's (_altars[0]). nullptr only when the build seated
        // no altar at all - RespawnPending owns that fallback.
        Altar const* RespawnAltarFor(ObjectGuid const& playerGuid) const;

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

        // Round B / B3. A segment's kill counter moved: re-decide whether that
        // segment's barrier may fall. OnMobDied calls it from the counter
        // block - the numerator moves, then the barrier is asked, before
        // MarkRunDirty - and SpawnBarriers calls it once per barrier it
        // places, which is what opens a segment that plans no trash at all.
        void EvaluateBarrier(int segment);

        // Drops the portcullis: deletes the GameObject, hands the four lane
        // cells back to the walk grid and says so to everyone in the dungeon.
        // `why` goes to the log only. Idempotent - an open barrier is left
        // alone, so a second threshold hit costs nothing.
        void OpenBarrier(Barrier& barrier, char const* why);

        // 1 Hz. A closed barrier explains itself ONCE, to whoever walks up to
        // it: a wall with no stated reason reads as a broken dungeon, and the
        // number it names is the only place a player learns what is left.
        void HintBarriers();

        // Flips walkability on a handful of grid cells, in place - the grid's
        // OWN (local) coordinates, the shape LocalFromGlobalCell returns. B3's
        // barrier is the only caller: creatures ignore GameObject collision,
        // so a closed portcullis has to be a hole in this grid or mobs walk
        // straight through it. No-op while the grid is not ready; a cell
        // outside it is skipped, never clamped.
        void SetCellsWalkable(std::vector<GridPoint> const& cells, bool walkable);

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
        std::vector<ObjectGuid> _decorGuids;    // props, torn down by the same rebuild
        std::vector<ObjectGuid> _critterGuids;  // ambient life, torn down by DespawnOrUnsummon
        PDv2RunState _run;
        std::vector<AffixDef> _runAffixes;      // the rows, for re-casting on a split
        uint16   _runAffixMask = 0;             // the same rows as bits
        bool     _runDirty = false;
        uint64   _leaderGuid = 0;               // the character that opened this run
        std::vector<uint16> _roomAlive;         // per room, index-aligned with the spawn draw

        // Round B / B3, the barrier's arithmetic. All five are per dense room
        // index or per boss segment and are filled once, at the end of
        // SpawnFromPlan, beside _roomAlive.
        //
        // _roomPlanned is a COPY of _roomAlive taken there and never moved
        // again: _roomAlive is inflated mid-run by a Lil' Bro split, so it can
        // only ever be the numerator's live count, never the denominator a
        // threshold is measured against.
        std::vector<uint16> _roomPlanned;       // per room, what the draw actually spawned
        std::vector<int>    _roomSegment;       // per room, SegmentOf its block
        std::vector<bool>   _roomIsBoss;        // per room, its block is a RoomBoss
        // Index 1..N, [0] unused: segment 0 is the entrance, which has no
        // barrier. The boss room's own pack is deliberately NOT in `planned` -
        // a segment whose only room is its boss would otherwise never open.
        std::vector<uint32> _segmentPlanned;
        std::vector<uint32> _segmentKilled;
        // One entry per boss segment that got a portcullis, in segment order.
        // An opened barrier STAYS in here with `open` set: it is the record
        // that this segment's threshold was already met, and the rebuild - not
        // the opening - is what forgets it.
        std::vector<Barrier> _barriers;
        std::vector<Altar> _altars;                              // chain order; [0] = the entrance's
        std::unordered_map<ObjectGuid, size_t> _altarByGuid;     // altar GO -> index into _altars
        std::unordered_map<ObjectGuid, size_t> _boundAltar;      // player -> index into _altars
        std::unordered_map<ObjectGuid, uint32> _pendingRespawn;  // player -> getMSTime() at death
        std::vector<ObjectGuid> _voidZones;     // friendly ground-hazard carriers; pruned each tick
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
