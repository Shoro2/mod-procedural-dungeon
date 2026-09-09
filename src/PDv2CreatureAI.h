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

#ifndef MOD_PDUNGEON_V2_CREATURE_AI_H
#define MOD_PDUNGEON_V2_CREATURE_AI_H

#include "ScriptedCreature.h"
#include "generator/PDv2WalkGrid.h"

namespace PDungeon
{
    class PDv2InstanceScript;
    struct PDv2MobData;

    // Mob AI for the file-less v2 map.
    //
    // Map 760 has no mmaps and never will, so core pathfinding degenerates to
    // straight lines - and a straight line between two platforms crosses the
    // void. Engine line-of-sight cannot gate the chase either: with no VMAP
    // and no server terrain, everything on the map "sees" everything else.
    // The walk grid is the only thing on the server that knows where floor
    // is, so every movement decision here goes through it:
    //
    //   * straight line walkable  -> core chase (its straight line IS safe)
    //   * otherwise               -> module A* waypoints, executed with
    //                                MovePoint(generatePath = false)
    //   * target not on the grid  -> hold; the fall catcher will deal with a
    //                                player who is over the void
    //
    // v1's PDMobAI (PDCreatureAI.h) is the model; the differences are the
    // grid (8.3 yd cells instead of v1's room tiles) and the gate (grid line
    // walkability instead of engine LoS, which v1 could use because its walls
    // were dynamic-tree GameObjects).
    //
    // Two things ride on the same grid for the same reason. A caster holds at
    // range only where the LINE to its target is floor, so it can never plant
    // itself and nuke across a gap it has no fight on. And the AI supplies its
    // own proximity aggro, because the templates it spawns are SHARED with
    // another dungeon and cannot be edited - several of them are deliberately
    // near-blind there - so a mob that would otherwise ignore a player next to
    // it engages, but only one it could walk to.
    //
    // WHAT A MOB CASTS comes from pdungeon_member_spells and nowhere else in
    // code (mod_pdungeon_member_spells.sql carries the whole argument for each
    // pick). Two shapes, one scheduler:
    //
    //   RANGE  holds, then spams its slot-0 FILLER back to back for as long as
    //          it holds, and drops a slot-1 spell in whenever one comes off
    //          cooldown. No gap between fillers beyond the cast itself.
    //   MELEE  chases, swings, and casts a slot-1 spell from melee whenever
    //          one is ready. Bosses use the same shape.
    //
    // The per-fight cooldowns live on the AI rather than in the creature's
    // spell history so that a pack pulled twice opens the same way twice.
    struct PDv2MobAI : public ScriptedAI
    {
        explicit PDv2MobAI(Creature* creature);

        void JustEngagedWith(Unit* who) override;
        void JustDied(Unit* killer) override;
        void JustReachedHome() override;
        void UpdateAI(uint32 diff) override;
        // Round C. RECORDS an arrival; it never launches the next leg itself.
        // See _legPending, and the .cpp, where the whole argument lives.
        void MovementInform(uint32 type, uint32 id) override;

        // Round B / B4, REORDERED by Round C's patrol fix. The core's evade
        // sends a creature home in a STRAIGHT LINE (HomeMovementGenerator on a
        // map with no mmaps), and on this map a straight line crosses the void
        // - so a patroller pulled halfway down its beat FLEW back to its spawn
        // block through nothing. The override therefore moves the home position
        // to wherever the creature stands BEFORE the base call (the base reads
        // that position two frames later), lets the base do the whole combat
        // stop, and only then throws the home walk away - motion master AND
        // spline, because Clear() cannot stop a spline - before putting the
        // patrol back on the grid. Every other mob keeps the core's behaviour
        // untouched. The .cpp carries the full trace.
        void EnterEvadeMode(EvadeReason why) override;

        // One line of patrol state for `.pdungeon v2 patrol`, formatted here
        // rather than in the command so these members stay protected and this
        // header stays free of Chat.h. A snapshot: it reads, it decides
        // nothing (the .cpp says what each field proves).
        std::string PatrolStateLine() const;

    protected:
        // One slot-1 spell with its own live timer. A plain vector rather than
        // an EventMap: the whole schedule is "cast the first one that is
        // ready", and a vector says that in one loop.
        struct KitSpell
        {
            uint32 spellId = 0;
            uint32 cooldownMs = 0;
            uint32 remainingMs = 0;     // 0 = ready
        };

        bool UpdateGridChase(uint32 diff);
        void UpdateCasterCombat(uint32 diff);
        void UpdateProximityAggro(uint32 diff);
        void UpdateImmolation(uint32 diff);
        void CallAlliesForHelp(Unit* victim);
        bool GridLineOkTo(Unit* victim) const;
        // Takes a planned route and walks its FIRST leg. Round C added a
        // refusal: a route whose waypoint 0 is not the cell the creature stands
        // in (give or take the planners' own snap radius) is not walked at all,
        // because leg 0 would then be an unchecked straight line from here to
        // wherever that route begins.
        //
        // Round D / D2: `clearPoints` says WHERE inside a cell this route's
        // waypoints are - the kit's clear point (a patrol beat) or the cell
        // centre (a chase). It is a property of the ROUTE and not of the
        // creature, because a patroller in a fight chases like anything else,
        // so it is handed over with the route and kept until the next one.
        void StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid,
                              bool clearPoints);
        // Launches ONE leg. Round C: reachable from UpdateAI only - directly
        // for the first leg of a run, through _legPending for every later one.
        void MoveToWaypoint(size_t index, WalkGrid const& grid);
        void StopWaypointRun(bool resumeChase);

        // Round B / B4, the out-of-combat half of a patrol LEADER's life. The
        // route is planned ONCE, on the first idle tick - from where the
        // creature stands to the goal cell its spawn tag carries - and then
        // walked back and forth for the rest of the run, reversed by
        // MovementInform at either end. Everything the chase already owns is
        // reused: the same grid, the same 500 ms decision interval, the same
        // waypoint runner and the same snap radius, because a patrol that
        // agreed with the chase about walkable ground only approximately would
        // eventually step off the world.
        //
        // Since Round D / D1 the plan comes from FindPatrolPath + a merge
        // rather than FindGridPath + SimplifyGridPath: axis-aligned legs down
        // the lane centre that pay for turns, for hugging a wall and for
        // walking over a prop, instead of the diagonals that cut through house
        // corners and furniture (the operator's "durch ecken von häusern",
        // 2026-09-08). Nothing else about the beat changed.
        //
        // The merge is MergeClearPoints since the D2 follow-up, not
        // MergeCollinear: the legs are walked on the cells' CLEAR POINTS, so a
        // run may only collapse while the straight line between the surviving
        // ends still passes every dropped cell's clear point within half a
        // yard. A beat therefore has more waypoints than D1's had, and each
        // extra one is a place where the visible passage bends.
        //
        // ONCE is load-bearing. _patrolRoute is never re-planned once it holds
        // a beat: a fight that ends anywhere but on the route is rejoined (see
        // ResumePatrol), not answered with a shorter beat.
        void UpdatePatrol(uint32 diff);
        // Round D / D2, the other half of a patrol: what a FOLLOWER does when
        // nothing is chasing anyone. It plans nothing and walks no grid - it
        // hands the whole movement problem to the core's follow generator,
        // aimed at the creature in front of it, which is what makes a file of
        // three cost exactly one planned route (design 2026-09-08 §D2.3).
        //
        // Three states and no fourth: leader alive and out of combat ->
        // MoveFollow at V2.Patrol.FollowDistYd x rank, directly behind it;
        // leader in a fight this follower is not in -> attack its victim;
        // leader gone or dead -> stop and hold, the patrol has dissolved.
        void UpdateFollower(uint32 diff);
        // The creature this follower walks behind, or nullptr when the tag
        // names nobody (this one leads) or the map no longer holds it.
        Creature* PatrolLeader() const;
        // Ends the current LEG and asks the next tick to rejoin the beat.
        // Called after an evade (and from JustReachedHome): the creature is
        // rarely standing on one of its own waypoints by then, so the leg it
        // was walking is no longer a leg it can walk from here - but the BEAT
        // still is, which is why the route itself survives untouched.
        //
        // Round D: a FOLLOWER takes the same call and answers it differently.
        // It has no route to rejoin, only a leader to find again, so the two
        // follow latches are cleared instead and the next tick re-issues the
        // follow - which is the whole of "after an evade a follower re-follows"
        // (design §D2.3).
        void ResumePatrol();

        // Reads the creature's rows once and keeps only what this run's
        // difficulty unlocks, so no tick ever looks at minDiff again.
        void BuildKit();
        // Draws a fresh opening delay for every cooldown spell. Named for what
        // it does: it does NOT zero the kit, it staggers it (see the .cpp).
        void ArmKitForNewFight();
        void TickKit(uint32 diff);
        bool CastReadyKitSpell();
        bool CastFiller();

        PDv2InstanceScript* _instance = nullptr;
        // The spawn tag. Not owned, and not resolvable in the constructor for
        // every creature - see the comment there.
        PDv2MobData const* _mob = nullptr;
        uint32 _repathTimer = 0;
        uint32 _aggroTimer = 0;
        uint32 _lineTimer = 0;
        uint32 _immolationTimer = 0;    // affix 4, accumulates toward 2 s
        std::vector<GridPoint> _waypoints;
        size_t _waypointIndex = 0;
        // B4. The whole beat, kept across runs and reversed at either end -
        // _waypoints is only ever the leg currently being walked, and
        // StopWaypointRun clears it. _patrolTimer shares REPATH_INTERVAL_MS
        // with the chase for the same reason the snap radius is shared.
        std::vector<GridPoint> _patrolRoute;
        uint32 _patrolTimer = 0;
        // Round D / D2, the follower's own cadence. It shares
        // REPATH_INTERVAL_MS with the beat and the chase for the same reason
        // they share it: three decisions taken at three different rates about
        // one creature would disagree about where it is.
        uint32 _followTimer = 0;
        std::vector<KitSpell> _kit;     // slot-1 spells this run unlocked
        uint32 _fillerSpellId = 0;      // slot-0 spell, or the pack fallback
        uint32 _fillerCooldownMs = 0;   // normally 0 - the filler is spammed
        uint32 _fillerRemainingMs = 0;
        bool _kitBuilt = false;
        bool _followingPath = false;
        // Round C, THE patrol fix. "A leg ended and the next one is owed."
        // Set by MovementInform, acted on and cleared by the next UpdateAI,
        // cleared by StopWaypointRun with the run it belonged to.
        //
        // Why the launch cannot happen where the arrival is learned: the inform
        // reaches the AI from inside MotionMaster::DirectExpire
        // (MotionMaster.cpp:177-195), which finalises the finished generator
        // FIRST and only then runs `top()->Reset(_owner)` on whatever is now on
        // top. A MovePoint issued from the inform installs and starts the new
        // generator during that finalize, so the Reset lands on it -
        // PointMovementGenerator::DoReset (PointMovementGenerator.cpp:295-297)
        // calls StopMoving() and kills the spline one line after it was
        // launched. Deferring costs nothing: Creature::Update runs the motion
        // (Creature.cpp:771 -> Unit.cpp:635-636) and then the AI
        // (Creature.cpp:884) in the SAME tick, so the leg starts microseconds
        // later, merely outside the motion master's own call stack.
        bool _legPending = false;
        // Round D / D2. "The route in _waypoints is a patrol beat, so its
        // waypoints are the cells' CLEAR POINTS." Set with the route by
        // StartWaypointRun and read by MoveToWaypoint, including the deferred
        // launch _legPending owes - which is why it is a member and not an
        // argument: the leg that pays that debt is launched a tick later, from
        // a call site that no longer knows which planner produced the route.
        bool _legOnClearPoint = false;
        bool _lineOk = false;       // last grid-line verdict, refreshed on the tick
        bool _holding = false;      // a caster that has planted itself at range
        bool _hasCalled = false;    // affix 1 already shouted for THIS fight
        bool _patrolActive = false; // _patrolRoute holds a beat worth walking
        // B4. The next patrol tick has to REJOIN the beat rather than start a
        // leg from _patrolRoute[0]: something moved the creature off its route
        // (an evade, which is how every patroller's fight ends, or a
        // knockback). Set by ResumePatrol, cleared by the tick that acts on it.
        bool _patrolRejoin = false;
        // Round D / D2, the follower's hold latch - the exact shape _chaseHeld
        // has, and for the same reason: a leader that is gone or dead STOPS
        // this creature (motion master, spline and all), and this says it has
        // already been stopped so the next 500 ms verdict does not stop it
        // again. The verdict itself is still re-taken every tick.
        //
        // There is no matching "already following" flag: the top movement
        // generator answers that question directly, and unlike a flag it also
        // notices when something else - a knockback, a crowd-control effect -
        // took the follow away. MoveFollow itself never de-duplicates; it
        // Mutates a fresh generator on every call (MotionMaster.cpp:448-469),
        // which is why the question has to be asked at all.
        bool _followHeld = false;
        // Round D / D2, the fix for Task 3 review I1. The distance the live
        // MoveFollow was ISSUED with, or a negative number when this creature
        // is not following anything. FollowMovementGenerator freezes its
        // _range at construction, so re-reading the conf key without comparing
        // it to this would keep promising a live key that only ever takes
        // effect after the next fight - which is what the conf.dist says it
        // does NOT do. A difference here re-issues the follow, and that is the
        // whole of "`.reload config` re-spaces a file that is already walking".
        float _followDistIssued = -1.0f;
        // Round C. The chase's own hold latch, the exact shape _holding has for
        // the caster's plant: an Unreachable verdict STOPS the creature (motion
        // master, spline and all), and this says it has already been stopped so
        // the next 500 ms verdict does not stop it a second time. The verdict
        // itself is still re-taken on every tick - only the clearing is
        // latched. Cleared wherever the creature is given something to walk to
        // again (both other verdicts, and JustEngagedWith).
        bool _chaseHeld = false;
        // Round C, diagnostics only, all three inert while
        // V2.Patrol.Debug is 0.
        //
        // D4 is once per creature ("the idle slot holds X"), and this is the
        // "already said it" flag. D5 prints a chase verdict only when the
        // verdict or the top generator CHANGED, and those two ints are the last
        // pair it printed - ints rather than the enums so -1 can mean "nothing
        // yet", which is what makes the first verdict of a fight print.
        bool _dbgSlotLogged = false;
        int  _dbgChaseVerdict = -1;
        int  _dbgChaseTop = -1;
    };
}

#endif
