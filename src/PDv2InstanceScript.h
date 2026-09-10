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
#include "generator/PDv2AmbushPlan.h"
#include "generator/PDv2WalkGrid.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
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

        // Round E / D7. This creature was never in the LAYOUT: a WP5 event
        // wave, or a WP6 respawn copy of a pack the player already cleared.
        // Separate from `countsForRun`, which answers a different question - a
        // patrol IS in the layout and moves no counter, an event mob is not in
        // the layout at all - so the two are set independently.
        //
        // What it changes is the kill funnel, and only half of it: materials
        // drop from an extra mob ALWAYS, because mats are a crafting input and
        // farming one in place is the point of it, while currency needs
        // V2.Loot.ExtraMobs.DropCurrency, because currency is progression and a
        // mob that comes back for ever would make it a faucet.
        bool   isExtra = false;

        // B4: this creature walks a beat out of combat. Since Round D / D2 the
        // beat is ONE CORRIDOR - both of its ends are doorway lane cells of
        // that corridor run, in GLOBAL grid cells, and the patrol never enters
        // a room. The AI plans the route on its first idle tick and reverses it
        // at either end.
        //
        // A patrol is a FILE: pick 0 of the corridor's draw is the LEADER and
        // carries the two cells; picks 1..n are FOLLOWERS and carry the leader
        // instead. The two halves are mutually exclusive by construction - an
        // empty `patrolLeader` IS "this one leads" - because a follower walks
        // no beat of its own: it follows the creature in front of it, so one
        // patrol costs one path however many creatures it has (design
        // 2026-09-08 §D2.3).
        bool   isPatrol = false;
        int    patrolStartCellX = 0;
        int    patrolStartCellY = 0;
        int    patrolGoalCellX = 0;
        int    patrolGoalCellY = 0;
        ObjectGuid patrolLeader;        // empty on the leader itself
        uint8  patrolRank = 0;          // 0 = leader, k >= 1 = the k-th follower

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
    // difficulty, lootMultX100, roomFactorX100 and dlvl are FROZEN into this at
    // spawn time and every gameplay hook reads them from here, never from the
    // live account row: a settings change in the middle of a run must not
    // retune the mobs already standing in the dungeon.
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
        // Round E / D8, x100 like every other multiplier in this module. The
        // run's ORDINARY room count against V2.Loot.Currency.RoomsBaseline, as
        // GameRoomFactorX100 works it out, frozen at spawn beside the loot
        // multiplier and for the same reason: every currency roll of every
        // kill is scaled by it, so a `.pdungeon v2 set` between two pulls must
        // not move the price of the second one.
        //
        // 100 - "full price" - is the no-run-bound-yet value rather than 0,
        // because a run whose spawn never happened should pay the ordinary
        // rate and not silently pay nothing.
        uint16 roomFactorX100 = 100;
        // Round E / L3. The ACCOUNT's dungeon level at spawn, which is the top
        // of the per-mob material band (GameMatsMaxCount). Frozen like the
        // dial: a dlvl gained by finishing THIS run must not retune the mobs
        // that are still standing in it.
        uint8  dlvl = 0;
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

        // Round D / D1-D2. Where this instance's props stand, indexed exactly
        // like the walk grid's `cells` (1 = at least one prop occupies that
        // cell), or nullptr while there is no grid or nothing was placed.
        //
        // NOT a walkability flag and deliberately not folded into the grid: a
        // prop is a COST to the patrol planner (FindPatrolPath adds
        // PatrolCost::propCell for entering one) and nothing at all to the
        // chase, because a mob squeezing past a brazier to reach a player is
        // fine and a patrol strolling through one is what the operator
        // reported. Barriers are not in here - a closed portcullis flips its
        // lane cells out of the grid itself, which is a wall and not a cost.
        std::vector<uint8_t> const* PropCells() const
        {
            return _propCells.empty() ? nullptr : &_propCells;
        }

        // Round C, for `.pdungeon v2 patrol` and nothing else. One snapshot
        // line per tagged patrol CREATURE this instance summoned, in spawn
        // order, each formatted by the AI itself (PDv2MobAI::PatrolStateLine).
        // Since Round D that is every member of every file, leaders and
        // followers alike, and the line's own role field is what tells them
        // apart. The walk lives here rather than in the command because
        // _spawnedGuids is this class's business and `instance` resolves a GUID
        // on the map that owns it. A member that has despawned contributes no
        // line.
        std::vector<std::string> PatrolSnapshot() const;

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

        // True while a death is waiting for its tick; the release veto in
        // PDClientLink reads it so a quick 'release spirit' cannot beat the
        // tick to the graveyard.
        bool HasPendingRespawn(ObjectGuid const& playerGuid) const
        {
            return _pendingRespawn.find(playerGuid) != _pendingRespawn.end();
        }

        // Round C / C7. The gate the HUD reports on: the LOWEST segment whose
        // portcullis is still sealed, with that segment's own numbers. False -
        // and three zeros - means no barrier of this run is closed any more,
        // which includes a run that never built one.
        //
        // `pct` is the RAW progress killed/planned, NOT progress towards the
        // threshold: the player is meant to watch it climb past the configured
        // barrierPct and see the wall fall there, and a bar that reads 100 %
        // while the portcullis still stands is a bug report. A segment that
        // plans nothing reports 100 because that is what EvaluateBarrier does
        // with it - it opens on sight (the single-boss-segment case).
        bool NextClosedBarrier(uint32& planned, uint32& killed, uint32& pct) const;

        // Round C / C7. The block coordinates of every room whose pack is
        // dead, in the PLAN's own frame - the UI link shifts them into the map
        // payload's frame with the same origin it shifts the M payload's
        // blocks by. A room that spawned nothing is cleared from the first
        // tick, which is also what it looks like to a player standing in it.
        void ClearedRoomBlocks(std::vector<std::pair<int, int>>& out) const;

        // The run's cleared-room counter, as the wire's change detector: the
        // K message is a complete set, so "resend it when this moved" is all
        // the link needs to keep every client's map honest without a delta
        // protocol it could silently fall out of step with.
        uint32 RoomsClearedCount() const { return _run.roomsCleared; }

        // ...and the OTHER half of that change detector. The counter above
        // only counts rooms emptied by kills, so it cannot tell a rebuild
        // apart from the run it replaced when both stand at 0 - and the K
        // payload does differ there, because it also carries rooms that
        // spawned nothing. This number is bumped by the rebuild itself, so a
        // link that records the pair always resends for a new run
        // (C7 Task 1 review, minor 2). Starts at 1, so a record written with
        // no script at all - {0, 0} - can never look like a real one.
        uint32 RunGeneration() const { return _runGeneration; }

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

        // Round D / D1-D2. Fills _propCells from _decorGuids, so it must run
        // AFTER SpawnDecor and SpawnKitProps and BEFORE SpawnPatrols (the
        // spawn-time beat is planned with it) - and it is deliberately built
        // from the SUMMONED objects rather than from the two plans, because
        // that is the only list that knows which prop actually made it onto
        // the map. Barriers are excluded by construction: SpawnBarriers runs
        // later, and its cells leave the walk grid rather than joining this.
        void BuildPropCells();

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

        // Round B / B3. One sealed portcullis per boss segment, standing in
        // the boss room's own doorway - the cell inside the entry edge that
        // segment's corridor run arrives through, found with the SAME walk the
        // validator proved the spine with. Called after EnsureWalkGrid built
        // the walk grid, so there is a grid to cut: the GameObject
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

        // Round B / B4, REWRITTEN by Round D / D2. ONE patrol per CORRIDOR:
        // for every chain room i = 1..chainLen-1 the corridor run that leads
        // into it (SpineRunInto, the same walk the barrier and the validator
        // make) gets a single file of 1, 2 or 3 creatures by run difficulty.
        //
        // The beat is CORRIDOR ONLY - doorway to doorway, never into a room -
        // so a patrol is risk on the road and never a second pack in a room
        // the player already cleared (design 2026-09-08 §D2.1). Both ends are
        // read off the run itself: the entry socket SpineRunInto answers is
        // room i's OWN edge, so the corridor's end of it is OppositeSocket(bit)
        // on run.back(), exactly as SpawnBarriers seals both halves of that
        // doorway; the far end is the socket of run.front() that faces room
        // i-1, which is the step between those two blocks.
        //
        // Its own RNG stream (PD_PATROL_SEED_MIX, mixed again per corridor), so
        // adding or retuning a patrol cannot move one pick of the room draw.
        // Called after SpawnBarriers because a sealed portcullis takes its four
        // lane cells out of the walk grid, and the goal cell of a boss
        // corridor is one of them - see the snap in the body.
        void SpawnPatrols(BlockPlan const& plan);

        // Round B / B5. One corridor per boss segment may be armed with an
        // ambush: the player who walks into it is stunned for two seconds and
        // a handful of mobs appear around them.
        //
        // WHICH corridor is a pure function of the layout and one chance, so
        // it is decided in the engine-free planner (BuildAmbushPlan) that the
        // harness pins; nothing here rolls anything about the geometry. What
        // this half owns is the arming: the block centre in world coordinates,
        // the corridor's own socket mask (which IS its axis, and therefore the
        // direction the mobs are placed along) and the creatures the spot will
        // spawn - drawn ONCE at build time on the ambush's own stream, so the
        // same seed springs the same ambush and a trap that fires costs no
        // draw at the moment the player is already busy being stunned.
        struct Ambush
        {
            AmbushSpot spot;            // bx/by ARE the trigger since C2
            float x = 0.0f;             // the corridor block's centre, world
            float y = 0.0f;             // - the arm log's coordinate, not a test
            float z = 0.0f;             // the corridor's floor plane, where mobs are seated
            unsigned socketMask = 0;    // that block's sockets = the corridor axis
            std::vector<SpawnPick> picks;
            bool armed = true;          // fires once, then stays spent until a rebuild
        };
        void SpawnAmbushPlan(BlockPlan const& plan);

        // AMBUSH_SCAN_MS. Every armed spot against every player in the
        // instance. The trigger is the BLOCK, not a disc (Round C / C2):
        // WorldToCell on the player's position, then gcx / PD_CELLS_PER_BLOCK
        // and gcy / PD_CELLS_PER_BLOCK against the spot's own bx/by, refined
        // by the walk grid so a player on the wall band is not "in" the
        // corridor. Kind-independent and without a tuning constant.
        //
        // Round B measured a 9 yd disc around the block centre, and half the
        // corridor kinds could be walked past: the four centre cells form a
        // junction square of half-width 8.33 yd, so a straight transit passes
        // its inscribed disc at 8.08 yd and fires, while a turn's geodesic
        // hugs the square's CORNER at 8.33 * sqrt(2) = 11.79 yd and never
        // does - 11.20 to 11.79 yd on every corner, T and cross, and 11.43 yd
        // on the alt-1 straight whose centre pillar offers a dogleg (research
        // c-research-ambush-trigger.md, "Per-kind lane geometry"). Widening
        // the radius past 11.79 would have closed the hole and left a magic
        // number that breaks the day the kit's corridor width changes; the
        // block test has nothing to break.
        //
        // It keeps its own cadence rather than the 1 Hz branch's, but no
        // longer because it has to: the disc could be crossed in under two
        // seconds, while a whole block is 66.67 yd and takes a running player
        // some nine, so 1 Hz would now catch them too. What 250 ms buys is how
        // fast the trap SPRINGS once they are in - a one-second scan would let
        // them walk several yards into the corridor before the stun lands.
        void TickAmbushes();

        // Disarms the spot, stuns the player, says so, and puts the stored
        // picks on the floor around them - grid-vetoed, because a corridor
        // block is 66.67 yd across and only its lane is floor: two cells of
        // 8.333 yd (LaneCellsForSocket puts the doorway on columns 3-4 / rows
        // 3-4), so 16.67 yd wide, 8.33 yd of it either side of the lane
        // centre. The eight offsets are measured from the PLAYER in x/y and
        // seated on Ambush::z, which is the corridor's own floor plane -
        // Ambush::x/y are the block centre and, since C2 dropped the disc,
        // only what the arm log prints.
        void FireAmbush(Ambush& ambush, Player* player);

        // The other half of OnUnitDeath, on the 1 Hz tick where a resurrect
        // is safe: everyone recorded there who is still on this map and still
        // dead comes back alive at full health and WITHOUT resurrection
        // sickness (ResurrectPlayer's applySickness is false - operator, T2
        // 2026-09-08), at the entrance or the furthest cleared boss hall -
        // computed, never chosen (Round C / C5; B1's clickable altars are
        // gone). Called after CatchFallers, so a death below the floor is
        // pulled onto the map before it is sent on.
        void RespawnPending();

        // Round C / C5. The checkpoint's world position: the arena centre of
        // the boss room with the highest chainIndex whose boss is dead.
        // False when no boss has fallen yet - RespawnPending then uses the
        // entrance, which is the normal case for the first half of a run.
        bool CheckpointSpot(float& x, float& y, float& z) const;

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

        // Round C / C8. The run's closing beat, and the module's only piece of
        // staged theatre: Chromie and the reward cache appear the moment the
        // last boss falls, she speaks three lines, and the portal home opens
        // behind the third.
        //
        // A state machine and not a chain of timed callbacks because the
        // instance script already owns a 1 Hz tick and nothing here needs to
        // be finer than that - `step` is which beat comes next and `nextAtMs`
        // is when it is due, so the whole thing survives a save/load of
        // nothing at all and costs one comparison a second while it runs.
        //
        // `x, y, z` is the arena centre the finale is staged around, copied
        // once at the start: `_roomSpot[_checkpointRoom]` can be re-derived at
        // any later tick, but a rebuild would have emptied it, and the three
        // objects must stand in a fixed relation to each other rather than to
        // whatever the run state says four seconds later.
        struct Finale
        {
            bool active = false;        // false once the portal is up, and before it starts
            uint32 step = 0;            // 0-2 = the lines, 3 = the portal
            uint32 nextAtMs = 0;        // getMSTime() deadline of `step`
            ObjectGuid chromie;         // the speaker; she is in _spawnedGuids
            float x = 0.0f;             // the arena centre the three objects ring
            float y = 0.0f;
            float z = 0.0f;
        };

        // Called at the end of FinishRun, so it runs after OnMobDied moved the
        // checkpoint onto the boss room that just fell. Summons Chromie and
        // the cache and arms the first line; does nothing that can fail the
        // run if either summon fails.
        void StartFinale();

        // 1 Hz, after HintBarriers. One beat per tick at most: the deadline is
        // a schedule, not a budget, so a step that comes due between ticks
        // simply fires on the next one.
        void TickFinale();

        // The grid veto every other placement in this file takes, applied to
        // one finale spot. Moves (x, y) onto the nearest walkable cell centre
        // when the cell it names is not floor; leaves it where it was when
        // there is no grid or nothing walkable within SPAWN_FALLBACK_SNAP_CELLS.
        // `what` names the object in the log line ("Chromie", "cache", "portal").
        void VetoFinaleSpot(float& x, float& y, char const* what) const;

        void RollBonusLoot(Unit* killer);

        // Round E / L2-L4, the kill funnel. Three sources with three different
        // shapes, and the shape is the design (D6): currency and materials are
        // PERSONAL - every player on the map rolls their own dice, so a group
        // of five is five independent chances and nothing to argue over - while
        // gear goes into the CORPSE, where the group's own loot rules already
        // decide who gets it.
        //
        // None of them touches PDRandom. The determinism boundary
        // (RollBonusLoot's comment in the .cpp states it in full) puts layout
        // and spawn selection on the seeded stream and every loot roll on the
        // core's urand, because a seed that also decided the drops would turn
        // farming into a lookup table.
        //
        // `tag` is the dead creature's own facts. Neither roll reads it today -
        // the isExtra gate is applied at the call site, where the whole funnel
        // is visible in one place - but a per-mob rule (a rarer mob paying
        // more) has nowhere else to come from, so it is carried rather than
        // added back later at three call sites.
        void RollCurrency(Creature* creature, PDv2MobData const& tag);
        void RollMaterials(Creature* creature, PDv2MobData const& tag);

        // The run boss's gear, added to the corpse loot the core has ALREADY
        // filled: Unit::Kill generates it (Unit.cpp:14082-14092) and sets the
        // lootable flag (:14226) well before it calls JustDied (:14238-14241),
        // which is what reaches this. So the items land in the normal loot
        // window, under the normal rules, and no second window is invented.
        // `killer` only picks the class filter's looter; who may loot is the
        // core's business, not ours.
        void InjectBossGear(Creature* creature, Unit* killer);

        // One stack into a bag, and a letter from Chromie when there is no room
        // for it. Player::AddItem already prints the client's "You receive
        // item" line, so a grant that lands says nothing extra; only the mail
        // announces itself, because a drop that silently went to the mailbox
        // reads as a drop that never happened.
        void GrantItem(Player* player, uint32 item, uint32 count) const;

        // Every player on `map` that still has a session, which is who a
        // personal roll is made for. Static and taking the map explicitly:
        // it is a plain walk of GetPlayers() and the callers name the map they
        // mean (the dead creature's), rather than reaching for `instance`.
        static void ForEachRunPlayer(Map* map,
                                     std::function<void(Player*)> const& fn);

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

        // Round C / C5. Four more per-room facts, taken in the SAME pass and
        // the same order as the three above, so `roomIndex` still means one
        // thing in every vector keyed by it. `roomBlocks` is discarded at the
        // end of SpawnFromPlan - these are what survives it, and they are what
        // replaced B1's altars: the respawn point is COMPUTED from the run's
        // own state rather than clicked.
        struct RoomSpot
        {
            float x = 0.0f;                     // the room's arena centre, world
            float y = 0.0f;
            float z = 0.0f;
        };
        std::vector<RoomSpot> _roomSpot;        // per room, the grid-vetoed arena centre
        std::vector<int> _roomBX;               // per room, its block coordinates
        std::vector<int> _roomBY;
        std::vector<int> _roomChain;            // per room, PlacedBlock::chainIndex (-1 off the spine)
        // The furthest cleared boss room, moved only forward by OnMobDied.
        // -1/-1 means no boss has fallen yet, which is what sends a corpse
        // back to the entrance. Reset with the run, like every vector above.
        int _checkpointChain = -1;              // its chainIndex, -1 = none
        int _checkpointRoom = -1;               // its dense roomIndex, -1 = none
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
        // Round B / B5. One entry per armed corridor, in segment order. A
        // sprung ambush STAYS in here with `armed` cleared - that is the record
        // that this corridor is spent - and the rebuild, not the firing, is
        // what forgets it. Same shape and same reasoning as _barriers.
        std::vector<Ambush> _ambushes;
        // Round C / C8. Inert until the last boss dies and inert again once
        // the portal is up, so the 1 Hz branch pays one bool for it on every
        // other tick of every other run. The rebuild resets it whole, the same
        // way it resets _barriers and _ambushes - the three objects themselves
        // went with _spawnedGuids and _decorGuids in DespawnAll, and this is
        // the run's memory that the beat already played.
        Finale _finale;
        // Round C / C7 review fold. Which run of THIS instance is standing:
        // bumped once by the rebuild branch, never reset, and read only by the
        // UI link's K record (RunGeneration says why). 1, not 0, so "no script"
        // is a value it cannot take. It wraps after 4 billion rebuilds of one
        // instance, which no instance survives.
        uint32   _runGeneration = 1;
        std::unordered_map<ObjectGuid, uint32> _pendingRespawn;  // player -> getMSTime() at death
        std::vector<ObjectGuid> _voidZones;     // friendly ground-hazard carriers; pruned each tick
        uint32   _fallCheckTimer = 0;
        uint32   _ambushTimer = 0;      // accumulates toward AMBUSH_SCAN_MS
        float    _entranceX = 0.0f;
        float    _entranceY = 0.0f;
        float    _entranceZ = 0.0f;
        bool     _haveEntrance = false;
        WalkGrid _grid;
        bool     _gridReady = false;
        bool     _gridTried = false;
        // Round D / D1-D2, PropCells() says what it means. Sized like
        // _grid.cells when it is filled and empty otherwise; cleared by the
        // rebuild together with the objects it describes.
        std::vector<uint8_t> _propCells;
    };
}

#endif
