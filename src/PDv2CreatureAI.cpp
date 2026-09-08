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

#include "PDv2CreatureAI.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <list>

namespace PDungeon
{
    namespace
    {
        // How often the chase decision is re-taken. 500 ms was v1's number
        // and survived live testing; the tick is cheap, because an A* runs
        // only when the straight line is blocked and no run is active.
        uint32 const REPATH_INTERVAL_MS = 500;

        // Cells searched around a live position for walkable ground. MEASURED,
        // not estimated: a cell is PD_BLOCK_SIZE_YD / PD_CELLS_PER_BLOCK =
        // 8.33 yd, so 2 cells is up to 16.7 yd per axis (23.6 yd at a ring
        // corner, since NearestWalkable searches square rings). Anything
        // further off the surface is not a position to walk to but one to
        // refuse - a target mid-jump over the void. The snap can therefore
        // MOVE a start cell by that much, which is why StartWaypointRun walks
        // to waypoint 0 rather than assuming the creature stands on it.
        int const SNAP_RADIUS_CELLS = 2;

        PDv2InstanceScript* GetV2Instance(Creature* creature)
        {
            return dynamic_cast<PDv2InstanceScript*>(creature->GetInstanceScript());
        }

        GridPoint CellOf(WalkGrid const& grid, float x, float y)
        {
            int gcx = 0, gcy = 0;
            WorldToCell(x, y, gcx, gcy);
            // May land outside the grid; PlanApproach's snap (or its refusal)
            // is the policy for that, not this conversion.
            return grid.LocalFromGlobalCell(gcx, gcy);
        }
    }

    PDv2MobAI::PDv2MobAI(Creature* creature) : ScriptedAI(creature)
    {
        _instance = GetV2Instance(creature);

        // The tag may not be there YET. This AI is built inside SummonCreature
        // (AddToWorld -> AIM_Initialize, Creature.cpp:319), which runs before
        // the instance script has the pointer back and can tag anything - so
        // the constructor takes the tag if it exists and UpdateAI keeps asking
        // until it does. Get, never GetDefault: the tag's PRESENCE is what says
        // "the dungeon spawned this", and creating one here would hand that
        // status to every creature the AI has ever been attached to.
        _mob = creature->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
    }

    void PDv2MobAI::JustEngagedWith(Unit* /*who*/)
    {
        // A previous fight may have left a stale run behind (evade clears the
        // motion, not these flags), and the automatic chase that AttackStart
        // starts walks a straight line - which this map cannot promise is
        // floor. Decide on the very next UpdateAI tick.
        StopWaypointRun(false);
        _repathTimer = 0;

        // Same for the caster's plant. AttackStart has just put this creature
        // back into a chase, so a _holding left over from the last fight would
        // skip the stop and leave it running at the target while casting.
        _holding = false;
        _lineOk = false;
        _lineTimer = 0;

        // A fresh fight opens STAGGERED, not with everything ready: each
        // cooldown spell draws its own 1-2 s opening delay. Per FIGHT, not per
        // creature lifetime, so a pack that was pulled, evaded and pulled again
        // opens the same way - a fresh draw - both times.
        ArmKitForNewFight();

        // Call for Help (affix 1) is ARMED here and fired on the next tick,
        // not shouted from inside this hook. Two reasons, and the second is the
        // load-bearing one. It matches where that module actually does the work
        // - its own update loop, not an engage hook
        // (DungeonChallengeScripts.cpp:674-705) - and it keeps the pull FLAT:
        // calling AttackStart on an ally runs that ally's JustEngagedWith
        // synchronously, so shouting from in here would recurse one stack frame
        // per mob in the chain, and a mass pull is exactly the case where that
        // chain is long. Armed and deferred, each mob calls from its own tick.
        //
        // Re-arming on every engage is deliberate: a mob that evaded and was
        // pulled again calls again, which is what that module's reset of its
        // hasCalled flag after an evade amounts to (:671).
        _hasCalled = false;

        // B4: a patroller walks its beat and RUNS its fights. The chase - the
        // core's or this AI's waypoints - takes over from here; the beat is
        // picked back up by EnterEvadeMode when the fight is over. The
        // StopWaypointRun above already threw the current leg away, which is
        // the other half of the handover.
        if (_mob && _mob->isPatrol)
        {
            me->SetWalk(false);
        }
    }

    void PDv2MobAI::JustDied(Unit* killer)
    {
        // The instance owns the counters, and it is the only thing that knows
        // how many mobs a room had. The AI just tells it who died - including
        // for the placeholder fallback spawns, which are tagged too.
        if (_instance)
        {
            _instance->OnMobDied(me, killer);
        }
    }

    void PDv2MobAI::JustReachedHome()
    {
        ScriptedAI::JustReachedHome();

        // An evade stripped every aura on the way home (core behaviour), and
        // for a carrier that included the affix VISUALS - the mechanics ride
        // the tag and never noticed, but the mob walked back looking clean
        // (operator report, first affix test 2026-08-09). Dress it again.
        if (_instance && _mob && _mob->affixMask)
        {
            _instance->ReapplyAffixAuras(me, _mob->affixMask);
        }

        // B4. EnterEvadeMode below normally cancels the home walk before it can
        // arrive, so this hook is not the usual way back - but it is the core's
        // contract for "this creature is home and out of evade", and a
        // HomeMovementGenerator that finishes by any route fires it
        // (HomeMovementGenerator.cpp:31-42). Whichever route it was, the beat
        // has to be re-planned from where the creature now stands.
        if (_mob && _mob->isPatrol)
        {
            ResumePatrol();
        }
    }

    void PDv2MobAI::EnterEvadeMode(EvadeReason why)
    {
        // The base call FIRST, always and for every creature: it is what stops
        // the combat, drops the threat list, restores the health and adds
        // UNIT_STATE_EVADE (CreatureAI::_EnterEvadeMode). Nothing below is a
        // substitute for any of that - it only redirects the walk home.
        ScriptedAI::EnterEvadeMode(why);

        // Alive, because _EnterEvadeMode refuses a dead creature outright and
        // clearing a corpse's motion during its own death would be a second
        // opinion about a transition setDeathState already owns.
        if (!_mob || !_mob->isPatrol || !me->IsAlive())
        {
            return;
        }

        // HOME IS WHEREVER THE PATROL STANDS. For an ownerless creature the
        // base call queues a MoveTargetedHome (CreatureAI.cpp:255-259), and
        // that generator walks a straight line to the home position with
        // pathfinding disabled - which on map 760 goes through the void,
        // because there are no mmaps and no terrain for the engine to refuse
        // over.
        me->SetHomePosition(me->GetPositionX(), me->GetPositionY(),
                            me->GetPositionZ(), me->GetOrientation());

        // Throw that walk away. Clear() pops the home generator, whose
        // Finalize clears UNIT_STATE_EVADE without calling JustReachedHome
        // (HomeMovementGenerator.cpp:31-42) - so the creature leaves evade
        // state here rather than at the end of a walk it will never take, and
        // UpdateProximityAggro is free to look again on the next tick. MoveIdle
        // is the no-op that follows a Clear down to a static idle generator; it
        // is there so the motion stack is never left half-described.
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveIdle();
        ResumePatrol();
    }

    void PDv2MobAI::MoveToWaypoint(size_t index, WalkGrid const& grid)
    {
        int gcx = 0, gcy = 0;
        grid.GlobalFromLocalCell(_waypoints[index], gcx, gcy);
        double wx = 0.0, wy = 0.0;
        CellCentreToWorld(gcx, gcy, wx, wy);
        me->GetMotionMaster()->MovePoint(WAYPOINT_MOVE_ID_BASE + static_cast<uint32>(index),
                                         static_cast<float>(wx), static_cast<float>(wy),
                                         sPDv2Mgr->GetConfig().floorZ,
                                         FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                                         /*generatePath=*/false);
    }

    void PDv2MobAI::StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid)
    {
        _waypoints = std::move(waypoints);
        if (_waypoints.size() < 2)
        {
            _followingPath = false;
            return;
        }
        // Index 0 is the planner's SNAPPED start cell, not necessarily the one
        // under our feet: both producers snap before they plan (the patrol
        // through NearestWalkable, the chase through PlanApproach), and the
        // snap may move the start by up to SNAP_RADIUS_CELLS. When it did, the
        // first leg IS the walk into that cell and nobody else ever line-checks
        // it; only when we already stand in the cell is index 0 skipped.
        GridPoint const standing = CellOf(grid, me->GetPositionX(), me->GetPositionY());
        _waypointIndex = (standing == _waypoints[0]) ? 1 : 0;
        _followingPath = true;
        MoveToWaypoint(_waypointIndex, grid);
    }

    void PDv2MobAI::StopWaypointRun(bool resumeChase)
    {
        _followingPath = false;
        _waypoints.clear();
        _waypointIndex = 0;
        if (resumeChase)
        {
            if (Unit* victim = me->GetVictim())
            {
                me->GetMotionMaster()->MoveChase(victim);
            }
        }
    }

    void PDv2MobAI::MovementInform(uint32 type, uint32 id)
    {
        if (type != POINT_MOTION_TYPE || !_followingPath || id < WAYPOINT_MOVE_ID_BASE)
        {
            return;
        }

        ++_waypointIndex;

        // B4: which RUN this is, not which creature. A patroller in combat is
        // walking a chase path, and that path has to end the way every other
        // chase path ends - resuming the chase. Only an out-of-combat run
        // belongs to the beat.
        bool const onBeat = _mob && _mob->isPatrol && !me->GetVictim();
        if (onBeat)
        {
            // The home position follows the patrol along its lane, so an evade
            // in the middle of the beat is a few yards of walking rather than a
            // straight line back to the spawn block across the void. Done at
            // every waypoint, not only at the ends: the pull can come anywhere.
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(),
                                me->GetPositionZ(), me->GetOrientation());
        }

        if (_waypointIndex >= _waypoints.size())
        {
            if (onBeat)
            {
                // The end of the beat: turn around. Reversing the ROUTE (not
                // the leg, which StopWaypointRun is about to clear) is what
                // makes the next UpdatePatrol walk back the way it came
                // without paying for a second A*. _patrolActive stays true, so
                // the reversed list is used as it is.
                std::reverse(_patrolRoute.begin(), _patrolRoute.end());
                StopWaypointRun(false);
                _patrolTimer = 0;
                return;
            }

            // Arrived where the target WAS when the path was planned. Resume
            // the chase and force the next tick to re-decide, so a target
            // that moved on is followed by plan rather than by beeline.
            StopWaypointRun(true);
            _repathTimer = 0;
            return;
        }

        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!grid)
        {
            StopWaypointRun(false);
            return;
        }
        MoveToWaypoint(_waypointIndex, *grid);
    }

    void PDv2MobAI::UpdatePatrol(uint32 diff)
    {
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!grid || !_mob || !_mob->isPatrol || !me->IsAlive())
        {
            return;
        }
        if (_patrolTimer > diff)
        {
            _patrolTimer -= diff;
            return;
        }
        _patrolTimer = REPATH_INTERVAL_MS;

        // A stale run, the same shape UpdateGridChase carries and for the same
        // reason: an evade, a knockback or a crowd-control effect can end the
        // motion without a MovementInform, and the flag alone would then hold
        // the beat still for ever. The BEAT survives it: a creature thrown off
        // its route rejoins the route below, exactly as it does after an evade.
        if (_followingPath &&
            me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        {
            StopWaypointRun(false);
            _patrolRejoin = true;
        }
        if (_followingPath)
        {
            return;     // a leg is live; MovementInform owns the next decision
        }

        // Where the creature actually stands, snapped to floor. The first plan
        // and every rejoin both start from it, so the snap is taken once - and
        // refused once: walking from a cell the grid does not call floor is the
        // same mistake whichever of the two is about to happen.
        GridPoint const cell = CellOf(*grid, me->GetPositionX(), me->GetPositionY());
        GridPoint here;
        if (!NearestWalkable(*grid, cell.x, cell.y, SNAP_RADIUS_CELLS, here))
        {
            // Hold and ask again in 500 ms - the same refusal the chase makes.
            return;
        }

        if (!_patrolActive)
        {
            // ONE A* FOR THE WHOLE RUN, and this is the only place a path is
            // ever written into _patrolRoute. The beat a patroller is handed on
            // its first idle tick is the beat it keeps: every later
            // interruption rejoins that route rather than replacing it with a
            // shorter one (design 2026-09-03 §B4.4).
            GridPoint const goalCell = grid->LocalFromGlobalCell(_mob->patrolGoalCellX,
                                                                 _mob->patrolGoalCellY);
            GridPoint goal;
            if (!NearestWalkable(*grid, goalCell.x, goalCell.y, SNAP_RADIUS_CELLS, goal))
            {
                return;
            }
            std::vector<GridPoint> path;
            if (!FindGridPath(*grid, here, goal, path))
            {
                return;
            }
            SimplifyGridPath(*grid, path);
            if (path.size() < 2)
            {
                // Standing on the goal already. Nothing to walk this tick; a
                // barrier that opens later can still turn this into a route.
                return;
            }
            _patrolRoute = path;
            _patrolActive = true;
            // Planned FROM here, so index 0 is already under our feet and
            // there is nothing to rejoin.
            _patrolRejoin = false;
        }

        std::vector<GridPoint> leg;
        if (_patrolRejoin)
        {
            _patrolRejoin = false;

            // REJOIN, NEVER RE-PLAN. An evade is the ordinary end of a
            // patroller's fight - there is no distance leash - so re-planning
            // from the evade point to the goal would make the stretch between
            // those two the whole beat for the rest of the run, and a patroller
            // pulled near the goal would end up shuffling on the spot. Design
            // §B4.4 asks for the other thing: get back onto the route at the
            // NEAREST waypoint by grid distance and carry on from there.
            //
            // A rejoin is WALKED, not flown: a waypoint further than this is not
            // "nearby" whatever the line test says (research A4: the old rejoin
            // was an uncapped beeline of up to ~530 yd).
            int const REJOIN_MAX_CELLS = 4;

            size_t best = _patrolRoute.size();
            int bestDist = 0;
            for (size_t i = 0; i < _patrolRoute.size(); ++i)
            {
                // Manhattan on cells: the A* is 4-neighbour, so this IS its
                // metric, and ranking needs no square root.
                int const dist = std::abs(_patrolRoute[i].x - here.x) +
                                 std::abs(_patrolRoute[i].y - here.y);
                if (dist > REJOIN_MAX_CELLS)
                {
                    continue;   // too far to be one straight leg, cap first
                }
                if (best < _patrolRoute.size() && dist >= bestDist)
                {
                    continue;   // cannot beat what we have; skip the line test
                }
                // The way back on has to be floor cell by cell: the leg is
                // walked with MovePoint(generatePath = false), so an unchecked
                // straight line is a walk across the void.
                if (!GridLineWalkable(*grid, here, _patrolRoute[i]))
                {
                    continue;
                }
                best = i;
                bestDist = dist;
            }

            if (best >= _patrolRoute.size())
            {
                // Nothing on the beat is within a straight, SHORT walk. Walk
                // the GRID back onto it instead of giving the route up: the old
                // fallback dropped the beat here, and the next tick's fresh
                // plan then collapsed it to "here -> goal" for the rest of the
                // run - the very thing rejoining exists to prevent. The route
                // itself is still never re-planned; only the way back onto it
                // is, and that A* is paid at most once per fight.
                size_t nearest = 0;
                int nearestDist = std::numeric_limits<int>::max();
                for (size_t i = 0; i < _patrolRoute.size(); ++i)
                {
                    int const d = std::abs(_patrolRoute[i].x - here.x) +
                                  std::abs(_patrolRoute[i].y - here.y);
                    if (d < nearestDist)
                    {
                        nearestDist = d;
                        nearest = i;
                    }
                }
                std::vector<GridPoint> back;
                if (_patrolRoute.empty() ||
                    !FindGridPath(*grid, here, _patrolRoute[nearest], back))
                {
                    // No beat at all, or off its component entirely (a barrier
                    // closed between us and it). The old fallback: give the
                    // route up and let the next tick plan a fresh one.
                    _patrolActive = false;
                    return;
                }
                SimplifyGridPath(*grid, back);
                leg = back;                 // here ... _patrolRoute[nearest]
                for (size_t i = nearest + 1; i < _patrolRoute.size(); ++i)
                {
                    leg.push_back(_patrolRoute[i]);
                }
                if (leg.size() < 2)
                {
                    // KEPT although it cannot fire today: it mirrors the `else`
                    // branch's guard below, and one of the two really is
                    // reachable. Why this one is not - a waypoint under our own
                    // feet has Manhattan distance 0, so it is inside the cap and
                    // its line test is the single walkable cell we stand on;
                    // that candidate is therefore always accepted and routes us
                    // to the `else`. Reaching HERE needs every waypoint refused,
                    // hence nearestDist >= 1, hence `back` has at least two
                    // points. Loosen the candidate loop's cap or its line gate
                    // and that stops being true, so the guard stays rather than
                    // leaving a one-point leg for StartWaypointRun to drop.
                    // What it does when it fires: the nearest waypoint is the
                    // far END of the beat and we stand on it, which IS the end
                    // of a lap - turn around the way MovementInform would have.
                    std::reverse(_patrolRoute.begin(), _patrolRoute.end());
                    leg = _patrolRoute;
                }
            }
            else
            {
                leg.push_back(here);
                for (size_t i = best; i < _patrolRoute.size(); ++i)
                {
                    if (i == best && _patrolRoute[i] == here)
                    {
                        continue;   // already standing on the waypoint we rejoin at
                    }
                    leg.push_back(_patrolRoute[i]);
                }
                if (leg.size() < 2)
                {
                    // Standing on the far END of the beat, which IS the end of a
                    // lap - so turn around the way MovementInform would have. The
                    // whole route is walked back, never a stub of it.
                    std::reverse(_patrolRoute.begin(), _patrolRoute.end());
                    leg = _patrolRoute;
                }
            }
        }
        else
        {
            // A COPY. StartWaypointRun takes ownership of what it is handed and
            // StopWaypointRun clears it, but _patrolRoute has to survive both -
            // it is the beat, and _waypoints is only the leg being walked.
            leg = _patrolRoute;
        }

        // Out of combat the patrol WALKS: it is meant to be seen coming down a
        // corridor, not to sprint. JustEngagedWith puts it back on run speed
        // the moment it pulls.
        me->SetWalk(true);
        StartWaypointRun(std::move(leg), *grid);
        if (!_followingPath)
        {
            // The runner refused the leg (fewer than two waypoints). Nothing
            // else on that path clears _patrolActive, so without this the same
            // doomed leg would be retried every 500 ms for the rest of the run.
            _patrolActive = false;
        }
    }

    void PDv2MobAI::ResumePatrol()
    {
        // THE BEAT SURVIVES THE FIGHT. This used to drop the route and let the
        // next tick re-plan from here to the goal, and because an evade is how
        // every patroller's fight ends (there is no leash), the beat then
        // collapsed to "evade point -> goal" permanently. So the route is left
        // exactly as it is and the next tick walks back onto it at the nearest
        // waypoint it has a walkable line to (design 2026-09-03 §B4.4); the
        // end-of-run reversal then restores the full lap.
        //
        // _patrolActive is deliberately NOT cleared. With no beat yet it is
        // already false, and UpdatePatrol plans the first one exactly as before.
        _patrolRejoin = true;
        _patrolTimer = 0;
        StopWaypointRun(false);
    }

    bool PDv2MobAI::UpdateGridChase(uint32 diff)
    {
        if (_repathTimer > diff)
        {
            _repathTimer -= diff;
            return _followingPath;
        }
        _repathTimer = REPATH_INTERVAL_MS;

        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        Unit* victim = me->GetVictim();
        if (!grid || !victim)
        {
            return _followingPath;
        }

        if (_followingPath)
        {
            // A knockback or crowd-control effect can displace the point run
            // without a MovementInform. Once the motion stack is back to
            // something ordinary with our flag still set, the run is dead -
            // drop it so the decision below starts a fresh one.
            MovementGeneratorType const current =
                me->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (current == IDLE_MOTION_TYPE || current == CHASE_MOTION_TYPE)
            {
                StopWaypointRun(false);
            }
        }

        std::vector<GridPoint> waypoints;
        switch (PlanApproach(*grid,
                             CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                             CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                             SNAP_RADIUS_CELLS, waypoints))
        {
            case ApproachKind::Direct:
                // The straight line is floor the whole way, so the core chase
                // is safe: with no mmaps its generated path degenerates to
                // exactly that straight line.
                if (_followingPath)
                {
                    StopWaypointRun(true);
                }
                else if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                {
                    me->GetMotionMaster()->MoveChase(victim);
                }
                return false;

            case ApproachKind::Path:
                // An active run is left to finish; MovementInform re-decides
                // the moment it ends. Replanning every tick would make the
                // mob stutter each time the target strafes a cell.
                if (!_followingPath)
                {
                    StartWaypointRun(std::move(waypoints), *grid);
                }
                return _followingPath;

            case ApproachKind::Unreachable:
            default:
                // The target is not on walkable ground - mid-air over the
                // void, or off the layout entirely. Walking toward them would
                // walk off the world; hold until they land somewhere real or
                // the fall catcher returns them to the entrance.
                if (_followingPath)
                {
                    StopWaypointRun(false);
                }
                return false;
        }
    }

    bool PDv2MobAI::GridLineOkTo(Unit* victim) const
    {
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!victim)
        {
            return false;
        }
        if (!grid)
        {
            // No grid means the server knows nothing about floor at all, which
            // EnsureWalkGrid already reported as an error. In that state the
            // melee mobs stand still but still swing at whatever reaches them;
            // a caster that refused to cast on top of that would add nothing
            // but a second symptom. Distance alone decides.
            return true;
        }

        // The same policy the chase runs on, not a second line test:
        // PlanApproach snaps both ends onto the walkable surface first (a live
        // position rarely sits on a cell centre) and answers Direct exactly
        // when the straight line between them is floor the whole way. That
        // equivalence is what the harness pins in CheckApproachPolicy.
        //
        // NEVER IsWithinLOSInMap here. Map 760 has no VMAP and no terrain, so
        // engine line of sight is true between any two points including
        // straight across the void (pd/02 §7) - a caster gated on it would
        // plant itself and nuke a player it has no floor to reach.
        std::vector<GridPoint> waypoints;
        return PlanApproach(*grid,
                            CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                            CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                            SNAP_RADIUS_CELLS, waypoints) == ApproachKind::Direct;
    }

    void PDv2MobAI::BuildKit()
    {
        _kitBuilt = true;
        _kit.clear();
        _fillerSpellId = 0;
        _fillerCooldownMs = 0;
        _fillerRemainingMs = 0;

        // The run's difficulty, frozen at spawn like every other number these
        // mobs fight with. No instance means no run, and a creature outside a
        // run has no kit - which is the honest answer, not a missing one.
        uint32 const difficulty = _instance ? _instance->GetRunState().difficulty : 0u;

        for (MemberSpell const& spell : sPDv2PackMgr->MemberSpells(me->GetEntry()))
        {
            if (static_cast<uint32>(spell.minDiff) > difficulty)
            {
                continue;
            }

            if (spell.slot == MEMBER_SPELL_SLOT_FILLER)
            {
                _fillerSpellId = spell.spellId;
                _fillerCooldownMs = spell.cooldownMs;
                continue;
            }

            KitSpell entry;
            entry.spellId = spell.spellId;
            entry.cooldownMs = spell.cooldownMs;
            _kit.push_back(entry);
        }

        // The pack column is the fallback the loader warns about: a half
        // applied SQL set leaves a range mob spamming its old nuke instead of
        // standing at range doing nothing. member_spells is the truth when it
        // has an answer.
        if (!_fillerSpellId && _mob)
        {
            _fillerSpellId = _mob->casterSpellId;
        }
    }

    // A fight no longer opens with the whole kit ready.
    //
    // Zeroing everything meant every unlocked spell of every pulled mob was
    // castable on the first tick, so a pack opened with one synchronised
    // volley - and the higher the difficulty the bigger that volley, because
    // difficulty is exactly what unlocks the extra spells (`minDiff` in
    // `pdungeon_member_spells`). Operator verdict from the first live run on
    // the host, 2026-08-10: stagger the opening.
    //
    // Each cooldown spell draws its own delay in [PD_KIT_OPENING_MIN_MS,
    // PD_KIT_OPENING_MAX_MS], per spell and per fight, so a pack opens
    // differently on every pull and the casts arrive spread out instead of
    // together. The FILLER deliberately keeps its zero: it is what the mob
    // does BETWEEN cooldowns, and delaying it as well would only leave the
    // creature standing there for the first second doing nothing.
    //
    // Still per FIGHT and not per lifetime: a pack that was pulled, evaded and
    // pulled again re-draws and opens staggered both times.
    void PDv2MobAI::ArmKitForNewFight()
    {
        for (KitSpell& spell : _kit)
        {
            spell.remainingMs = urand(PD_KIT_OPENING_MIN_MS, PD_KIT_OPENING_MAX_MS);
        }
        _fillerRemainingMs = 0;
    }

    void PDv2MobAI::TickKit(uint32 diff)
    {
        for (KitSpell& spell : _kit)
        {
            spell.remainingMs = spell.remainingMs > diff ? spell.remainingMs - diff : 0;
        }
        _fillerRemainingMs = _fillerRemainingMs > diff ? _fillerRemainingMs - diff : 0;
    }

    bool PDv2MobAI::CastReadyKitSpell()
    {
        // FIRST READY WINS, in table order - and there is deliberately no
        // PDRandom anywhere in this file. The seeded stream is the SPAWN
        // contract (PDv2PackMgr::SelectSpawns lists every draw it contains, in
        // order); which spell a mob picks in a fight is combat behaviour, and
        // one extra draw here would re-roll the creatures of every room after
        // this one for the same seed.
        for (KitSpell& spell : _kit)
        {
            if (spell.remainingMs)
            {
                continue;
            }

            // The cooldown is spent on the ATTEMPT. A spell that refuses -
            // out of range, out of power, target immune - would otherwise be
            // retried on every single tick, and for a range mob the filler
            // below would never get a turn again.
            spell.remainingMs = spell.cooldownMs;
            return DoCastVictim(spell.spellId) == SPELL_CAST_OK;
        }
        return false;
    }

    bool PDv2MobAI::CastFiller()
    {
        if (!_fillerSpellId || _fillerRemainingMs)
        {
            return false;
        }

        // Normally 0, i.e. back to back for as long as the mob holds, which is
        // the operator's design ("durchspammen", 2026-08-09). The column is
        // honoured all the same, so a filler that turns out to be too much in
        // game - Wail of Souls carries a 30 yd knockback - can be paced from
        // the table without a build.
        _fillerRemainingMs = _fillerCooldownMs;
        return DoCastVictim(_fillerSpellId) == SPELL_CAST_OK;
    }

    void PDv2MobAI::UpdateCasterCombat(uint32 diff)
    {
        Unit* victim = me->GetVictim();
        float const castRange = sPDv2Mgr->GetConfig().castRangeYd;

        // Distance first, because it is a subtraction and it decides whether
        // the expensive half is worth running at all. The line test is a grid
        // search, so it rides the chase decision's own 500 ms cadence; between
        // refreshes the last verdict stands.
        bool hold = victim && me->IsWithinCombatRange(victim, castRange);
        if (hold)
        {
            if (_lineTimer > diff)
            {
                _lineTimer -= diff;
            }
            else
            {
                _lineTimer = REPATH_INTERVAL_MS;
                _lineOk = GridLineOkTo(victim);
            }
            hold = _lineOk;
        }

        if (hold)
        {
            if (!_holding)
            {
                // Once per plant, not once per tick: re-clearing the motion
                // master every 500 ms would restart the spline of a creature
                // that is already standing still.
                StopWaypointRun(false);
                me->GetMotionMaster()->Clear(false);
                me->GetMotionMaster()->MoveIdle();
                _holding = true;
            }
            me->SetFacingToObject(victim);

            // A cooldown spell whenever one is ready, the filler otherwise -
            // and the filler has no gap of its own, so between cooldowns the
            // mob casts back to back. UNIT_STATE_CASTING is the ONLY thing
            // that paces it, which is what turns a 3 s Frostbolt or a 5 s
            // Drain Life channel into a cast rhythm rather than a stutter.
            //
            // DoCastVictim throughout rather than DoSpellAttackIfReady: the
            // latter ties the cast to the MELEE attack timer, and this branch
            // wants the two rhythms independent. A spell still refuses when
            // its own range cannot reach, which is why every filler in
            // mod_pdungeon_member_spells.sql reaches at least V2.CastRangeYd.
            if (!me->HasUnitState(UNIT_STATE_CASTING) && !CastReadyKitSpell())
            {
                CastFiller();
            }
        }
        else
        {
            if (_holding)
            {
                _holding = false;
                _repathTimer = 0;       // decide the approach on this very tick
            }
            UpdateGridChase(diff);
        }

        // Point blank: a range mob a melee player has walked into swings too.
        // No out-of-power fallback beyond that: four of the five range mobs
        // are unit_class 8 with a level-80 mana pool, and the fifth (84287,
        // unit_class 1) was given a kit whose every spell costs it nothing -
        // mod_pdungeon_member_spells.sql carries that measurement.
        DoMeleeAttackIfReady();
    }

    void PDv2MobAI::UpdateProximityAggro(uint32 diff)
    {
        if (_aggroTimer > diff)
        {
            _aggroTimer -= diff;
            return;
        }
        _aggroTimer = REPATH_INTERVAL_MS;

        // WHY the module aggroes at all instead of letting the core do it: the
        // dungeon spawns creature_template rows it SHARES with
        // fl-underground-dungeon, where they balance map 741, so this module
        // must not edit them - and six of them are near-blind by design
        // (detection_range 1 on 84284-84287, 2 on the bosses; measured live
        // 2026-08-07). Left to their templates they would stand there while a
        // player walked through the room. So the AI supplies its own eyes and
        // the row stays untouched.
        if (!me->IsAlive() || me->IsInCombat() || me->IsInEvadeMode())
        {
            return;
        }

        float const range = sPDv2Mgr->GetConfig().aggroRangeYd;
        if (range <= 0.0f)
        {
            return;
        }

        Player* target = me->SelectNearestPlayer(range);
        if (!target || !me->IsValidAttackTarget(target))
        {
            return;
        }

        // Never across the void. A mob that pulls something it cannot walk to
        // either stands in combat for ever or walks off the world, so the aggro
        // test is the SAME reachability test the chase uses - "it noticed me"
        // and "it can get to me" must never disagree.
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (grid)
        {
            std::vector<GridPoint> waypoints;
            if (PlanApproach(*grid,
                             CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                             CellOf(*grid, target->GetPositionX(), target->GetPositionY()),
                             SNAP_RADIUS_CELLS, waypoints) == ApproachKind::Unreachable)
            {
                return;
            }
        }

        AttackStart(target);
    }

    void PDv2MobAI::UpdateImmolation(uint32 diff)
    {
        // The bit, never the aura: a player who dispels the fire visual off a
        // mob must not also switch its burn off (PDv2Affixes.h).
        if (!_mob || !HasAffix(_mob->affixMask, PD_AFFIX_IMMOLATION))
        {
            return;
        }

        _immolationTimer += diff;
        if (_immolationTimer < AFFIX_IMMOLATION_INTERVAL_MS)
        {
            return;
        }
        // Subtract rather than reset, so a long map tick does not swallow the
        // remainder and stretch the interval.
        _immolationTimer -= AFFIX_IMMOLATION_INTERVAL_MS;

        // OUT OF COMBAT TOO, which is deliberate and mirrored: that module's
        // tick is gated on the creature being alive and nothing else
        // (DungeonChallengeScripts.cpp:622 and :708-726). It is an aura, so
        // walking past one hurts and pulls the mob - which is the mechanic.
        uint32 const difficulty = _instance ? _instance->GetRunState().difficulty : 0u;
        if (!difficulty)
        {
            return;
        }
        uint32 const damage = difficulty * AFFIX_IMMOLATION_DMG_PER_DIFF;

        // No grid-line gate here, unlike every ranged decision in this file:
        // the radius is 8 yd and a walk-grid cell is 8.3 yd, so the grid cannot
        // resolve anything at this scale and a line test would answer about the
        // cells the two stand in rather than about the 8 yd between them.
        Map::PlayerList const& players = me->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* player = it->GetSource();
            if (!player || !player->IsAlive())
            {
                continue;
            }
            if (me->GetDistance(player) > AFFIX_IMMOLATION_RANGE_YD)
            {
                continue;
            }

            // ENVIRONMENTAL damage, exactly as that module deals it
            // (DungeonChallengeScripts.cpp:722): it has no caster, so it is not
            // resisted, not reflected, and not run through the difficulty
            // damage lever a second time - the difficulty is already the 80x
            // factor above.
            player->EnvironmentalDamage(DAMAGE_FIRE, damage);
        }
    }

    void PDv2MobAI::CallAlliesForHelp(Unit* victim)
    {
        if (!victim)
        {
            return;
        }

        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;

        // A grid search, not a walk of the map's spawn store: that module can
        // iterate GetCreatureBySpawnIdStore because its dungeons are populated
        // from creature tables, while every mob PDv2 owns is a SUMMON and
        // appears in no such store.
        std::list<Creature*> nearby;
        me->GetDeadCreatureListInGrid(nearby, AFFIX_CARRIER_SEARCH_YD, false);

        uint32 called = 0;
        for (Creature* ally : nearby)
        {
            if (!ally || ally == me || !ally->IsAlive() || ally->IsInCombat())
            {
                continue;
            }

            // The tag replaces that module's pet/summon/totem/faction filters
            // wholesale. It has to: its IsSummon() line would reject every
            // creature in this dungeon, and the tag says the stronger thing
            // anyway - the dungeon spawned this, so it is dungeon content.
            if (!ally->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY))
            {
                continue;
            }
            if (ally->GetDistance(me) > AFFIX_CALL_FOR_HELP_RANGE_YD)
            {
                continue;
            }
            if (!ally->IsValidAttackTarget(victim))
            {
                continue;
            }

            // AND IT MUST BE ABLE TO GET THERE. The same reachability test the
            // proximity aggro and the chase run on, for the same reason: a mob
            // that answers a call it cannot walk to either stands in combat for
            // ever or walks off the world. "It was called" and "it can reach
            // the fight" must never disagree, and on this map only the walk
            // grid can say - engine line of sight is true across the void
            // (pd/02 §7).
            if (grid)
            {
                std::vector<GridPoint> waypoints;
                if (PlanApproach(*grid,
                                 CellOf(*grid, ally->GetPositionX(), ally->GetPositionY()),
                                 CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                                 SNAP_RADIUS_CELLS, waypoints) == ApproachKind::Unreachable)
                {
                    continue;
                }
            }

            if (CreatureAI* ai = ally->AI())
            {
                ai->AttackStart(victim);
                ++called;
            }
        }

        if (called)
        {
            LOG_DEBUG(PD_LOG, "PDv2: {} called {} reachable ally(s) into the fight",
                      me->GetName(), called);
        }
    }

    void PDv2MobAI::UpdateAI(uint32 diff)
    {
        if (!_mob)
        {
            _mob = me->CustomData.Get<PDv2MobData>(PD_MOB_DATA_KEY);
        }

        // Before the victim check: an aura burns whoever stands next to it,
        // fight or no fight.
        UpdateImmolation(diff);

        // Deliberately no distance leash: dungeon mobs chase for as long as
        // the target exists on the map, like any stock instance (operator
        // decision 2026-08-06, replacing a working 150 yd leash). Reset still
        // happens the normal way - UpdateVictim() fails when the target dies
        // or leaves the map - and a target the grid cannot reach is held at
        // bay by UpdateGridChase's Unreachable case, not by walking after it.
        if (!UpdateVictim())
        {
            // B4 first, aggro second. The beat is what a patroller does when
            // nothing is happening, and the proximity check below can turn this
            // very tick into a fight - JustEngagedWith then throws the leg away
            // again, which is the handover in the right order. Every other mob
            // pays one branch for this and nothing else.
            if (_mob && _mob->isPatrol)
            {
                UpdatePatrol(diff);
            }
            UpdateProximityAggro(diff);
            return;
        }

        // Built on the first tick of the first fight, not in the constructor:
        // the spawn tag may still be missing there (the constructor's own
        // comment says why), and the run's difficulty - which decides what the
        // kit contains - is only knowable once the instance script has one.
        if (!_kitBuilt && _mob)
        {
            BuildKit();
        }
        TickKit(diff);

        // In combat with a live victim: the tick JustEngagedWith armed. Once
        // per fight, and only for a carrier.
        if (!_hasCalled && _mob && HasAffix(_mob->affixMask, PD_AFFIX_CALL_FOR_HELP))
        {
            _hasCalled = true;
            CallAlliesForHelp(me->GetVictim());
        }

        // A range mob with nothing to spam is not a range mob. The loader
        // already said so once per template at startup, so this is silent:
        // it just fights like everything else.
        if (_mob && _mob->role == PACK_ROLE_CASTER && _fillerSpellId)
        {
            UpdateCasterCombat(diff);
            return;
        }

        // Melee, and bosses with it: in v1 a boss's menace is its stats, and a
        // boss that kited would be unfightable in a 66 yd room.
        UpdateGridChase(diff);

        // Kit spells are cast FROM MELEE, between swings. In melee because
        // that is where the mob is going anyway and because several of these
        // spells are 5 yd weapon abilities that would refuse anywhere else;
        // between swings because DoMeleeAttackIfReady below skips only the
        // moment the mob is actually mid-cast, so an instant ability costs no
        // auto-attack at all.
        if (!me->HasUnitState(UNIT_STATE_CASTING) && me->IsWithinMeleeRange(me->GetVictim()))
        {
            CastReadyKitSpell();
        }

        DoMeleeAttackIfReady();
    }
}

// Binds the AI to every hostile creature on the v2 map, whatever its template.
// The dungeon deliberately spawns stock Blizzard creatures (native loot tables
// are the farming design, 01 §8), so v1's route - a ScriptName on the module's
// own creature_template rows - cannot work here: it would mean editing every
// stock template the packs might ever use. The AI-selection hook binds without
// touching a row; it runs before template scripts and after the selector's pet
// special case (CreatureAISelector.cpp:78-88), so player pets never reach it.
class PDv2CreatureAIBinder : public AllCreatureScript
{
public:
    PDv2CreatureAIBinder() : AllCreatureScript("PDv2CreatureAIBinder") { }

    CreatureAI* GetCreatureAI(Creature* creature) const override
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

        // Guardians and totems a player brings along keep their normal AI
        // (pets were already filtered by the selector's special case).
        if (creature->GetCharmerOrOwnerPlayerOrPlayerItself())
        {
            return nullptr;
        }

        // Ambient life keeps its own AI. This binder hands PDv2MobAI to every
        // ownerless creature on the map, and PDv2MobAI::UpdateProximityAggro
        // calls AttackStart - which UnitAI::AttackStart (UnitAI.cpp:29-33)
        // does NOT gate on REACT_PASSIVE. So the passive react state every
        // critter gets from Creature::InitializeReactState would not protect
        // it: without this, a rat would hunt the player.
        if (creature->IsCritter())
        {
            return nullptr;
        }

        return new PDungeon::PDv2MobAI(creature);
    }
};

void AddPDv2CreatureScripts()
{
    new PDv2CreatureAIBinder();
}
