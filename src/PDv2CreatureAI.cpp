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
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PDDefines.h"
#include "PDv2Affixes.h"
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "PDv2PackMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"

#include <algorithm>
// M_PI, for the follow angle. Safe in this order and only in this order:
// PDv2CreatureAI.h reaches Define.h first, which defines _USE_MATH_DEFINES on
// Windows (Define.h:36-38) before anything has pulled <cmath> in.
#include <cmath>
#include <cstdlib>
#include <limits>
#include <list>
#include <string>

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

        // Round C. The patrol diagnostics gate, asked on the tick that would
        // log rather than cached anywhere: the key is read live (PDv2Mgr.cpp),
        // so an operator who types `.reload config` mid-run starts and stops
        // the evidence without a restart. Default 0, and the host never turns
        // it on - see conf/mod_procedural_dungeon.conf.dist.
        bool PatrolDebug()
        {
            return sPDv2Mgr->GetConfig().patrolDebug;
        }

        // The motion stack's top, in words. The number alone is unreadable in a
        // log and the enum is not printable, so this is the only place the two
        // are tied together (MotionMaster.h:37-59). Only the types this map can
        // actually produce are named; anything else prints as its number, which
        // is itself the finding.
        char const* MotionName(MovementGeneratorType type)
        {
            switch (type)
            {
                case IDLE_MOTION_TYPE:      return "IDLE";
                case RANDOM_MOTION_TYPE:    return "RANDOM";
                case CONFUSED_MOTION_TYPE:  return "CONFUSED";
                case CHASE_MOTION_TYPE:     return "CHASE";
                case HOME_MOTION_TYPE:      return "HOME";
                case POINT_MOTION_TYPE:     return "POINT";
                case FLEEING_MOTION_TYPE:   return "FLEEING";
                case DISTRACT_MOTION_TYPE:  return "DISTRACT";
                case FOLLOW_MOTION_TYPE:    return "FOLLOW";
                case EFFECT_MOTION_TYPE:    return "EFFECT";
                case NULL_MOTION_TYPE:      return "NULL";
                default:                    return "OTHER";
            }
        }

        char const* ApproachName(ApproachKind kind)
        {
            switch (kind)
            {
                case ApproachKind::Direct: return "Direct";
                case ApproachKind::Path:   return "Path";
                default:                   return "Unreachable";
            }
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

        // Round C, and for exactly the same reason one line up: AttackStart
        // (UnitAI.cpp:29-33) has just installed a fresh MoveChase, so a
        // _chaseHeld carried over from the LAST fight's Unreachable hold would
        // tell UpdateGridChase the creature is already stopped when it is in
        // fact beelining at the new target.
        _chaseHeld = false;
        // The D5 gate is per fight too, so the opening verdict of every pull
        // prints even when it matches the one the last fight ended on.
        _dbgChaseVerdict = -1;
        _dbgChaseTop = -1;

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
            // D3's tail. Since the evade fix this hook is reachable in ONE way
            // - the zero-length home walk to the cell the creature already
            // stands on arriving before Clear() pops it - so a line here says
            // which of the two routes home the run actually took.
            if (PatrolDebug())
            {
                LOG_INFO(PD_LOG, "PDv2 patrol: {} JustReachedHome at ({:.1f},{:.1f},{:.1f}) top {}",
                         me->GetName(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
                         MotionName(me->GetMotionMaster()->GetCurrentMovementGeneratorType()));
            }
            ResumePatrol();
        }
    }

    void PDv2MobAI::EnterEvadeMode(EvadeReason why)
    {
        // WHY THE ORDER OF THE NEXT TWENTY LINES IS THE WHOLE FIX (Round C,
        // after the Round B version flew patrollers across the void on every
        // evade - measured 2026-09-08).
        //
        // The base call is still FIRST for everything it owns: it stops the
        // combat, drops the threat list, restores the health and sets
        // UNIT_STATE_EVADE (CreatureAI::_EnterEvadeMode). Nothing here is a
        // substitute for any of that. But for an ownerless creature it also
        // LAUNCHES a walk home (CreatureAI.cpp:259 -> MotionMaster::MoveTargetedHome,
        // MotionMaster.cpp:262-270), and HomeMovementGenerator::_setTargetLocation
        // reads the home position and starts the spline inside that same call
        // (HomeMovementGenerator.cpp:62-69). Round B moved the home AFTERWARDS,
        // two frames too late: the spline was already flying to the SPAWN
        // block, in a straight unpathed line, across whatever void lay between.
        //
        // So home is set BEFORE the base call - then the walk the base queues
        // is a zero-length walk to the cell the creature already stands on
        // instead of a flight home - and the spline is stopped EXPLICITLY
        // afterwards, because Clear() provably cannot: DirectClean's reset
        // branch (MotionMaster.cpp:163-164) only calls Reset() on the new top,
        // that top is the static idle singleton, and IdleMovementGenerator's
        // Reset (IdleMovementGenerator.cpp:31-32) skips StopMoving() whenever
        // IsStopped() is true - which _setTargetLocation made true one line
        // after launching the spline, by clearing UNIT_STATE_MOVING
        // (HomeMovementGenerator.cpp:73; UNIT_STATE_MOVING is UnitDefines.h:217,
        // IsStopped is Unit.h:1760). Unit::Update drives splines from
        // UpdateSplineMovement, before and independently of the motion master
        // (Unit.cpp:635-636), so an unowned spline runs to completion.
        //
        // Decide the patrol case BEFORE the base call, because the home write
        // has to happen there. Alive, because _EnterEvadeMode refuses a dead
        // creature outright and moving a corpse's home during its own death
        // would be a second opinion about a transition setDeathState owns; the
        // base call cannot change that answer either way.
        bool const patrol = _mob && _mob->isPatrol && me->IsAlive();

        if (patrol)
        {
            // HOME IS WHEREVER THE PATROL STANDS.
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(),
                                me->GetPositionZ(), me->GetOrientation());

            if (PatrolDebug())
            {
                float hx = 0.0f, hy = 0.0f, hz = 0.0f, ho = 0.0f;
                me->GetHomePosition(hx, hy, hz, ho);
                LOG_INFO(PD_LOG, "PDv2 patrol: {} EVADE reason {} at ({:.1f},{:.1f},{:.1f}) "
                                 "home ({:.1f},{:.1f},{:.1f}) dist {:.1f}",
                         me->GetName(), uint32(why), me->GetPositionX(), me->GetPositionY(),
                         me->GetPositionZ(), hx, hy, hz, me->GetExactDist2d(hx, hy));
            }
        }

        ScriptedAI::EnterEvadeMode(why);

        if (patrol && PatrolDebug())
        {
            // The middle line of D3. With the home already moved this reads
            // "splineFinalized 1" on a creature that had not moved since the
            // pull, and a short flight otherwise - never the walk back to spawn
            // Round B produced.
            LOG_INFO(PD_LOG, "PDv2 patrol: {} after base: top {} splineFinalized {} "
                             "stopped {} unitState 0x{:X}",
                     me->GetName(),
                     MotionName(me->GetMotionMaster()->GetCurrentMovementGeneratorType()),
                     me->movespline->Finalized() ? 1 : 0, me->IsStopped() ? 1 : 0,
                     me->GetUnitState());
        }

        if (!patrol)
        {
            return;
        }

        // Throw the home walk away, spline included. Clear() pops the home
        // generator, whose Finalize clears UNIT_STATE_EVADE without calling
        // JustReachedHome (HomeMovementGenerator.cpp:31-42) - so the creature
        // leaves evade state here rather than at the end of a walk it will
        // never take, and UpdateProximityAggro is free to look again on the
        // next tick. StopMoving is what Clear() cannot do: Unit::StopMoving
        // (Unit.cpp:13056-13073) is gated on movespline->Finalized(), NOT on
        // UNIT_STATE_MOVING, so it kills the spline the idle generator's Reset
        // declines to touch. MoveIdle then follows the Clear down to the static
        // idle generator, so the motion stack is never left half-described.
        me->GetMotionMaster()->Clear();
        me->StopMoving();
        me->GetMotionMaster()->MoveIdle();

        if (PatrolDebug())
        {
            // THE decisive line. Round B read "splineFinalized 0 stopped 1" -
            // a live spline nothing owned and nothing would stop. It must now
            // read "splineFinalized 1".
            LOG_INFO(PD_LOG, "PDv2 patrol: {} after Clear+StopMoving+MoveIdle: top {} "
                             "splineFinalized {} stopped {}",
                     me->GetName(),
                     MotionName(me->GetMotionMaster()->GetCurrentMovementGeneratorType()),
                     me->movespline->Finalized() ? 1 : 0, me->IsStopped() ? 1 : 0);
        }

        ResumePatrol();
    }

    // ONLY EVER CALLED FROM UpdateAI (Round C). Directly for the first leg of a
    // run - StartWaypointRun's own callers are both UpdateAI branches - and
    // through _legPending for every leg after that. A leg launched from inside
    // MovementInform is killed by the Reset that follows the finalize; the
    // header's _legPending comment carries the call chain and the line numbers.
    void PDv2MobAI::MoveToWaypoint(size_t index, WalkGrid const& grid)
    {
        int gcx = 0, gcy = 0;
        grid.GlobalFromLocalCell(_waypoints[index], gcx, gcy);
        double wx = 0.0, wy = 0.0;
        // Round D / D2. A PATROL walks the cell's clear point, the chase walks
        // its centre - and the difference is up to 4 yd, which on a 6.6 yd city
        // passage is the whole of it. The chase keeps centres deliberately: it
        // is aimed at a player who is standing wherever they like, the leg is
        // re-decided every 500 ms, and moving its target off-centre would only
        // add a wobble to a route that already ends at a moving unit.
        if (_legOnClearPoint)
        {
            PatrolPointToWorld(grid, _waypoints[index], wx, wy);
        }
        else
        {
            CellCentreToWorld(gcx, gcy, wx, wy);
        }
        // WHAT THE `false` ACTUALLY BUYS, corrected in Round C. It lands on
        // MovePoint's `generatePath` parameter (MotionMaster.h:242) - the call
        // IS the one it means - but that flag does not suppress the
        // PathGenerator in this core: both branches of
        // PointMovementGenerator::DoInitialize end in `init.MoveTo(i_x, i_y,
        // i_z, true)` (PointMovementGenerator.cpp:75 and :87), and that `true`
        // is MoveSplineInit's OWN generatePath (MoveSplineInit.h:104 ->
        // MoveSplineInit.cpp:221-238), which runs a PathGenerator regardless.
        //
        // The leg is a straight line for a different reason, and that reason is
        // why this is safe rather than merely harmless: map 760 has no mmaps,
        // so PathGenerator::CalculatePath (PathGenerator.cpp:57-87) takes its
        // no-navmesh exit into BuildShortcut (:631-645) - a 2-point path whose
        // NormalizePath (:623-629) leaves Z untouched, because with no terrain
        // WorldObject::UpdateAllowedPositionZ (Object.cpp:1610) has no height
        // to clamp to. So every leg is exactly the straight cell-centre to
        // cell-centre line at floorZ that the grid already proved walkable -
        // which is the contract StartWaypointRun and the rejoin both rely on.
        // The `false` stays because it states the intent; if this core ever
        // gains a navmesh for 760, it is the flag that keeps the leg honest.
        me->GetMotionMaster()->MovePoint(WAYPOINT_MOVE_ID_BASE + static_cast<uint32>(index),
                                         static_cast<float>(wx), static_cast<float>(wy),
                                         sPDv2Mgr->GetConfig().floorZ,
                                         FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                                         /*generatePath=*/false);

        // D1. One line per leg: where the module thinks it is going and how far
        // that is. Since Round C this line is printed from the UpdateAI launch,
        // so `me` is where the creature really stands when the leg starts - and
        // `dist` is therefore the length the leg will really have. A leg longer
        // than the cell size times a waypoint span is the "flew off the beat"
        // signature - the grid is 8.33 yd per cell, so a simplified leg of a few
        // cells is tens of yards and anything in the hundreds is not a leg at
        // all. Consecutive legs whose `me` never changes were the bug itself.
        //
        // Round D / D2 adds `clear` and the offset the leg's target carries.
        // They are the operator-visible half of this whole round: a leg into a
        // city straight should read a clear of 12-15 and a non-zero offset,
        // and a leg that reads `clear 4` is the planner buying the last cell a
        // body fits through - which is the line to quote when the patrol still
        // brushes a house.
        if (PatrolDebug() && _mob && _mob->isPatrol)
        {
            PatrolCellInfo const info = PatrolInfoAt(grid, _waypoints[index]);
            LOG_INFO(PD_LOG, "PDv2 patrol: {} guid {} leg wp {}/{} cell ({},{}) global ({},{}) "
                             "clear {} offset ({},{}) -> world ({:.1f},{:.1f},{:.1f}) "
                             "| me ({:.1f},{:.1f},{:.1f}) | top {} | dist {:.1f}",
                     me->GetName(), me->GetGUID().GetCounter(), uint32(index),
                     uint32(_waypoints.size()), _waypoints[index].x, _waypoints[index].y,
                     gcx, gcy, uint32(info.clear), int32(info.du), int32(info.dv),
                     float(wx), float(wy), sPDv2Mgr->GetConfig().floorZ,
                     me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
                     MotionName(me->GetMotionMaster()->GetCurrentMovementGeneratorType()),
                     me->GetExactDist2d(float(wx), float(wy)));
        }
    }

    void PDv2MobAI::StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid,
                                     bool clearPoints)
    {
        _waypoints = std::move(waypoints);
        // Set BEFORE the two refusals below rather than after them: a refused
        // route clears _waypoints, and leaving the flag describing the route
        // before it would hand the next MoveToWaypoint the wrong frame if
        // anything ever launched a leg without going through here.
        _legOnClearPoint = clearPoints;
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

        // NO BEELINE START (Round C). "Up to SNAP_RADIUS_CELLS" is the whole
        // licence the paragraph above has, and it is now enforced instead of
        // assumed. Both snaps are square-ring searches of that radius
        // (PDv2WalkGrid.cpp:294-321, :329), so for any route planned from HERE
        // the offset is Chebyshev <= SNAP_RADIUS_CELLS by construction and this
        // never fires. It fires for a route planned somewhere ELSE - the beat
        // reversed while the creature was still at the far end of it, a
        // displacement no MovementInform reported - and there leg 0 would be an
        // unchecked straight line from here to the route's start: the 489 yd
        // flight across the void the 2026-09-08 retest recorded. Refusing costs
        // one 500 ms tick, after which the caller plans again from where the
        // creature actually is.
        int const offCells = std::max(std::abs(standing.x - _waypoints[0].x),
                                      std::abs(standing.y - _waypoints[0].y));
        if (offCells > SNAP_RADIUS_CELLS)
        {
            if (PatrolDebug())
            {
                LOG_INFO(PD_LOG, "PDv2 patrol: {} guid {} refused: start {} cells from waypoint 0 "
                                 "(standing ({},{}), wp0 ({},{}), {} waypoint(s))",
                         me->GetName(), me->GetGUID().GetCounter(), offCells,
                         standing.x, standing.y, _waypoints[0].x, _waypoints[0].y,
                         uint32(_waypoints.size()));
            }
            _followingPath = false;
            _waypoints.clear();
            _waypointIndex = 0;
            return;
        }

        _waypointIndex = (standing == _waypoints[0]) ? 1 : 0;
        _followingPath = true;
        _legPending = false;    // this leg is being launched right here
        MoveToWaypoint(_waypointIndex, grid);
    }

    void PDv2MobAI::StopWaypointRun(bool resumeChase)
    {
        _followingPath = false;
        _waypoints.clear();
        _waypointIndex = 0;
        // The owed leg dies with the run it was owed to. Without this, a run
        // stopped between the arrival and the next UpdateAI would leave a
        // pending launch pointing into a cleared route.
        _legPending = false;
        if (resumeChase)
        {
            // SAFE FROM INSIDE MovementInform, unlike a MovePoint - and this is
            // the one call the inform still makes. ChaseMovementGenerator's
            // DoReset is DoInitialize (TargetedMovementGenerator.cpp:401-405),
            // and the Creature specialisation (:382-390) launches no spline at
            // all: it only clears _lastTargetPosition and the recheck timer, so
            // the Reset that DirectExpire runs afterwards has nothing to kill
            // and in fact GUARANTEES the next DoUpdate re-aims and launches.
            // Deferring it would be pure loss - a tick of standing still.
            if (Unit* victim = me->GetVictim())
            {
                me->GetMotionMaster()->MoveChase(victim);
            }
        }
    }

    // AN ARRIVAL IS RECORDED HERE AND NOTHING IS LAUNCHED (Round C, the patrol
    // fix; measured 2026-09-08, log Server_2026-09-08_21_03_52 lines 988-1093).
    //
    // Round B walked the beat from this hook: arrive, ++index, MovePoint the
    // next leg. Every leg after the first then died on the frame it was born.
    // The core reaches this function through
    // MotionMaster::DirectExpire (MotionMaster.cpp:177-195), which pops and
    // DIRECTLY deletes the finished generator - that delete is the Finalize
    // that informs us (PointMovementGenerator.cpp:322-323) - and only
    // afterwards runs `top()->Reset(_owner)` on whatever is on top by then. A
    // MovePoint issued from here is not queued: MMCF_UPDATE is already cleared
    // when MovementExpired is called (MotionMaster.cpp:117-121), so Mutate
    // initialises the new generator at once (MotionMaster.cpp:891-917) and
    // PointMovementGenerator::DoInitialize launches its spline. Control returns
    // to DirectExpire, `top()` is now that brand-new generator, and its
    // Reset - PointMovementGenerator::DoReset,
    // PointMovementGenerator.cpp:295-297 - opens with
    // `if (!unit->IsStopped()) unit->StopMoving();`. The spline dies a line
    // after it was launched, the next tick's DoUpdate sees a finalised spline,
    // expires, informs, and the whole run burns through its waypoints in one
    // second without the creature moving a yard - "leg 2..11 and their informs
    // all at me (-54.2,-362.5)" in the log. The route then reversed, and the
    // first leg launched from OUTSIDE the motion update (by UpdatePatrol) DID
    // run: one 489 yd straight line across the dungeon and the void.
    //
    // So this hook validates the arrival, advances the index, and asks the next
    // UpdateAI for the leg. That tick is the SAME server tick - Creature::Update
    // runs Unit::Update (Creature.cpp:771 -> the motion master, Unit.cpp:636)
    // before AI()->UpdateAI (Creature.cpp:884) - so nothing is delayed; the leg
    // is merely started outside a call stack that would undo it.
    void PDv2MobAI::MovementInform(uint32 type, uint32 id)
    {
        // D2, BEFORE the guard on purpose: an inform the module drops - wrong
        // type, a stale _followingPath, a core id below the module's base - is
        // invisible everywhere else, and "the leg ended and nobody noticed" and
        // "no leg ever ended" look identical in the log without this line.
        if (PatrolDebug() && _mob && _mob->isPatrol)
        {
            LOG_INFO(PD_LOG, "PDv2 patrol: {} inform type {} ({}) id {} (base {}) following {} "
                             "idx {}/{} | me ({:.1f},{:.1f},{:.1f})",
                     me->GetName(), type, MotionName(MovementGeneratorType(type)), id,
                     uint32(WAYPOINT_MOVE_ID_BASE), _followingPath ? 1 : 0,
                     uint32(_waypointIndex), uint32(_waypoints.size()),
                     me->GetPositionX(), me->GetPositionY(), me->GetPositionZ());
        }

        if (type != POINT_MOTION_TYPE || !_followingPath || id < WAYPOINT_MOVE_ID_BASE)
        {
            return;
        }

        // WHICH leg ended, not just "a leg did". The id is the index the leg
        // was launched with (MoveToWaypoint), and the only leg this AI is
        // waiting for is _waypointIndex; anything else is an arrival from a leg
        // that was already thrown away and re-issued, and advancing on it would
        // skip a waypoint nobody walked. Cheap, and it makes the invariant
        // "index == the leg in flight" checkable rather than assumed.
        if (id - WAYPOINT_MOVE_ID_BASE != static_cast<uint32>(_waypointIndex))
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
            // that moved on is followed by plan rather than by beeline. The
            // MoveChase inside StopWaypointRun is the one motion call that is
            // safe from here - see the reason there.
            StopWaypointRun(true);
            _repathTimer = 0;
            return;
        }

        // OWE the next leg; do not launch it. UpdateAI pays the debt later in
        // this same tick, from a call stack that will not Reset it away.
        _legPending = true;
    }

    void PDv2MobAI::UpdatePatrol(uint32 diff)
    {
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        // Round D: rank 0 only. A follower has a leader, not a beat, and
        // UpdateAI routes it to UpdateFollower - but the guard is repeated here
        // because this function writes _patrolRoute, and a follower that ever
        // reached it would plan a second route for a patrol that is supposed to
        // cost exactly one. `grid` non-null implies `_instance` non-null (the
        // line above is where it came from), which is what lets the plan below
        // read PropCells() without a second null test.
        if (!grid || !_mob || !_mob->isPatrol || _mob->patrolRank || !me->IsAlive())
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
            // ONE PLAN FOR THE WHOLE RUN, and this is the only place a path is
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
            // THE PATROL PLANNER, NOT THE CHASE'S (Round D / D1). FindGridPath
            // is uniform-cost and SimplifyGridPath then merges as far as a
            // walkable straight line reaches, so the beat came out as diagonals
            // across rooms and through corridor mouths - and the supercover
            // test that approves them knows CELLS, not the facades that lean up
            // to 6.1 yd into a mouth nor the props this module stands on
            // walkable ground. That is the operator's "durch ecken von häusern
            // und objekte hindurch". FindPatrolPath pays for a turn, for a cell
            // with a non-walkable neighbour and for a cell with a prop on it,
            // so the cheapest route is the lane centre; MergeCollinear then
            // says that same route in fewer points WITHOUT introducing a
            // diagonal. The prop map comes from the instance (PropCells(), null
            // when there are none), which is why this needs _instance and not
            // just the grid.
            //
            // The goal snap above is what makes a SEALED boss corridor plan at
            // all: SpawnPatrols tagged the true doorway lane cell, and while
            // that segment's portcullis is closed the barrier has taken it out
            // of the grid - the snap answers the lane cell one step back, and
            // the beat grows to the doorway itself on the first plan after the
            // barrier falls.
            //
            // Round D / D2, the two-pass plan. The first pass runs at the
            // shipped PatrolCost, whose minClearQ BLOCKS every cell with less
            // than 1.0 yd of room to either side; the second drops that floor
            // and lets the tightPerQuarter charge alone decide. Both passes
            // pay the charge, so the fallback is still the widest route
            // available and not D1's route back - it is only allowed to squeeze
            // where nothing else exists. A corridor whose every lane row is
            // pinched (a city straight with two deep houses facing each other)
            // would otherwise leave its patrol standing still for the whole
            // run, which is a worse dungeon than one whose patrol brushes a
            // wall for two cells.
            std::vector<GridPoint> path;
            if (!FindPatrolPath(*grid, here, goal, _instance->PropCells(), path))
            {
                PatrolCost loose;
                loose.minClearQ = 0;
                if (!FindPatrolPath(*grid, here, goal, _instance->PropCells(), path, loose))
                {
                    return;
                }
                // ONE LINE PER BEAT, not per tick: this branch runs inside
                // `!_patrolActive`, and the plan it produces sets that flag.
                // A patroller that logs this twice has lost its beat twice,
                // which is itself the finding.
                LOG_WARN(PD_LOG, "PDv2: patrol planner fell back to the walk grid for {} "
                                 "guid {}: no beat from cell ({},{}) to ({},{}) keeps a "
                                 "yard of clearance",
                         me->GetName(), me->GetGUID().GetCounter(),
                         here.x, here.y, goal.x, goal.y);
            }
            MergeCollinear(path);
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
                // is, and that plan is paid at most once per fight.
                //
                // Round D: the same planner as the beat, for the same reason -
                // the walk back onto the route is walked in public down the
                // same corridor, and a diagonal here would cut the same house
                // corner the beat now avoids.
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
                //
                // Round D / D2: the same two passes the first plan makes, for
                // the same reason and in the same order. A fight that ended
                // inside a pinched stretch would otherwise be unable to walk
                // back onto its own beat and would drop it - and dropping the
                // beat is exactly what this branch exists to prevent.
                std::vector<GridPoint> back;
                bool haveBack = !_patrolRoute.empty() &&
                                FindPatrolPath(*grid, here, _patrolRoute[nearest],
                                               _instance->PropCells(), back);
                if (!haveBack && !_patrolRoute.empty())
                {
                    PatrolCost loose;
                    loose.minClearQ = 0;
                    haveBack = FindPatrolPath(*grid, here, _patrolRoute[nearest],
                                              _instance->PropCells(), back, loose);
                    if (haveBack)
                    {
                        LOG_WARN(PD_LOG, "PDv2: patrol rejoin fell back to the walk grid for "
                                         "{} guid {}: no way from cell ({},{}) back onto the "
                                         "beat keeps a yard of clearance",
                                 me->GetName(), me->GetGUID().GetCounter(), here.x, here.y);
                    }
                }
                if (!haveBack)
                {
                    // No beat at all, or off its component entirely (a barrier
                    // closed between us and it). The old fallback: give the
                    // route up and let the next tick plan a fresh one.
                    _patrolActive = false;
                    return;
                }
                MergeCollinear(back);
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
        // A BEAT, so its waypoints are the cells' clear points (Round D / D2).
        StartWaypointRun(std::move(leg), *grid, /*clearPoints=*/true);
        if (!_followingPath)
        {
            // The runner refused the leg: fewer than two waypoints, or - since
            // Round C - a route that does not start where the creature stands.
            // Nothing else on that path clears _patrolActive, so without this
            // the same doomed leg would be retried every 500 ms for the rest of
            // the run. Dropping the beat is exactly right for the second case:
            // the next tick takes a fresh A* FROM HERE, which is the only
            // honest answer once the creature and its route have parted.
            _patrolActive = false;
        }
    }

    Creature* PDv2MobAI::PatrolLeader() const
    {
        if (!_mob || !_mob->patrolRank || !_mob->patrolLeader)
        {
            // No rank means this creature IS the leader (or is not in a patrol
            // at all), and the two halves of the tag are mutually exclusive by
            // construction - PDv2InstanceScript.h says so at the fields.
            return nullptr;
        }
        // The instance's map, never ObjectAccessor's global lookup: a patrol
        // lives and dies inside one instance of map 760, and a GUID that has
        // left it is a GUID this AI must read as "gone".
        return me->GetMap()->GetCreature(_mob->patrolLeader);
    }

    void PDv2MobAI::UpdateFollower(uint32 diff)
    {
        if (!_mob || !_mob->isPatrol || !_mob->patrolRank || !me->IsAlive())
        {
            return;
        }
        if (_followTimer > diff)
        {
            _followTimer -= diff;
            return;
        }
        _followTimer = REPATH_INTERVAL_MS;

        Creature* leader = PatrolLeader();
        if (!leader || !leader->IsAlive() || !leader->IsInWorld())
        {
            // THE PATROL HAS DISSOLVED (design §D2.3). Hold where it stands
            // rather than walk anywhere: there is no beat on this creature's
            // tag to take over - it never carried one - and sending it home
            // would be the straight unpathed line across the void that Round C
            // spent itself removing. Latched like the chase's hold, so the stop
            // is issued once and not twice a second for the rest of the run.
            if (!_followHeld)
            {
                _followHeld = true;
                me->GetMotionMaster()->Clear();
                // Clear() cannot stop a spline on its own - the whole argument
                // is written out in EnterEvadeMode - so the stop is explicit
                // and MoveIdle follows it down, leaving the motion stack fully
                // described rather than half.
                me->StopMoving();
                me->GetMotionMaster()->MoveIdle();
                _followDistIssued = -1.0f;      // nothing is being followed now
                if (PatrolDebug())
                {
                    LOG_INFO(PD_LOG, "PDv2 patrol: {} guid {} follower {} holds - its leader "
                                     "is gone or dead",
                             me->GetName(), me->GetGUID().GetCounter(), uint32(_mob->patrolRank));
                }
            }
            return;
        }
        _followHeld = false;

        // THE LEADER'S FIGHT IS THE FILE'S FIGHT. Polled here rather than
        // shouted from the leader's JustEngagedWith on purpose, and for the
        // reason the affix's Call for Help is deferred too: AttackStart runs
        // the target's own JustEngagedWith synchronously, so a leader shouting
        // at its followers would recurse one stack frame per member, and a
        // poll costs one pointer read per follower per 500 ms.
        if (!me->GetVictim())
        {
            if (Unit* victim = leader->GetVictim())
            {
                if (victim->IsAlive() && me->IsValidAttackTarget(victim))
                {
                    AttackStart(victim);
                    return;
                }
            }
        }

        // SINGLE FILE. The top generator is the "already following" test - a
        // flag would not notice a knockback or a crowd-control effect taking
        // the follow away, and MoveFollow itself never de-duplicates
        // (MotionMaster.cpp:448-469), so re-issuing it every tick would restart
        // the spline every tick.
        //
        // dist x rank, angle pi: follower 1 walks FollowDistYd behind the
        // leader, follower 2 twice that, both directly behind it - so the file
        // is one line down the lane and the whole patrol still costs the one
        // route its leader planned. inheritWalkState and inheritSpeed are the
        // call's defaults (true): the followers walk while the leader walks its
        // beat and run when it runs, without this AI tracking either.
        //
        // Round D / D2, Task 3 review I1: the key is read on every DECISION,
        // which is what conf.dist promises, and not merely on every issue.
        // FollowMovementGenerator freezes its _range at construction
        // (TargetedMovementGenerator.h), so a file already walking would
        // otherwise keep the spacing it was born with until a fight took the
        // follow away - and `.reload config` would re-space nothing, which is
        // the one thing the operator will try during the round. Comparing the
        // live product against what was issued costs one float and re-issues
        // ONLY when the operator really moved the key: MoveFollow never
        // de-duplicates, so an unconditional call here would restart the spline
        // twice a second.
        float const dist = sPDv2Mgr->GetConfig().patrolFollowDistYd *
                           static_cast<float>(_mob->patrolRank);
        bool const following =
            me->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;
        if (!following || dist != _followDistIssued)
        {
            me->GetMotionMaster()->MoveFollow(leader, dist, float(M_PI));
            _followDistIssued = dist;
            if (PatrolDebug())
            {
                LOG_INFO(PD_LOG, "PDv2 patrol: {} guid {} follower {} follows {} at {:.1f} yd "
                                 "({}) | me ({:.1f},{:.1f}) leader ({:.1f},{:.1f}) dist {:.1f}",
                         me->GetName(), me->GetGUID().GetCounter(), uint32(_mob->patrolRank),
                         leader->GetName(), dist,
                         following ? "re-spaced from the live conf" : "new follow",
                         me->GetPositionX(), me->GetPositionY(),
                         leader->GetPositionX(), leader->GetPositionY(),
                         me->GetExactDist2d(leader));
            }
        }
    }

    void PDv2MobAI::ResumePatrol()
    {
        // Round D / D2. A FOLLOWER has no route to rejoin - it has a leader to
        // find again. Both latches are cleared and the timer zeroed, so the
        // very next tick re-issues the MoveFollow the evade's Clear threw away;
        // that is the whole of "after an evade a follower re-follows".
        if (_mob && _mob->patrolRank)
        {
            _followTimer = 0;
            _followHeld = false;
            // The follow the evade threw away was issued with SOME distance;
            // saying "none is issued" here keeps the field describing the
            // motion master rather than a generator that no longer exists.
            _followDistIssued = -1.0f;
            StopWaypointRun(false);
            return;
        }

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

    std::string PDv2MobAI::PatrolStateLine() const
    {
        // A SNAPSHOT, for the instant an operator sees a mob where it should
        // not be. The whole point is that the three explanations the Round C
        // debug report could not tell apart are one glance apart on this line:
        //
        //   walkable 0 + top IDLE  + spline running -> a spline nobody owns
        //                                              (the evade bug, fixed)
        //   walkable 0 + top CHASE                  -> the chase is beelining
        //                                              at an unreachable target
        //   walkable 1 + following 1                -> the module really did
        //                                              choose that cell, and
        //                                              the grid or the planner
        //                                              is what is wrong
        //
        // Round C adds `pending`: the owed leg is paid by the very next
        // UpdateAI, so this reads 0 to a human every time. A 1 that survives
        // being looked at twice means UpdateAI is not running for this creature
        // at all - the run is not stuck, the AI is.
        //
        // Round D opens the line with the ROLE, because a patrol is now a file
        // and the answer to "why is this one standing still" is different for
        // its two halves. The leader prints the beat its tag carries, in global
        // cells, so a `route 0` next to a beat that is two real cells apart says
        // the plan failed rather than that the tag is empty; a follower prints
        // its rank and its leader's guid, so `.pdungeon v2 patrol` can be read
        // top to bottom as the files the dungeon actually built.
        //
        // Nothing here decides anything, and nothing here is cheap to be
        // clever about: it is typed by hand, once.
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        GridPoint cell{ 0, 0 };
        int walkable = -1;
        int routeDist = -1;
        if (grid)
        {
            cell = CellOf(*grid, me->GetPositionX(), me->GetPositionY());
            walkable = grid->At(cell.x, cell.y) ? 1 : 0;
            // Manhattan on cells, the metric the 4-neighbour A* and the rejoin
            // both use - so this number is directly comparable with the
            // rejoin's REJOIN_MAX_CELLS cap.
            for (GridPoint const& wp : _patrolRoute)
            {
                int const d = std::abs(wp.x - cell.x) + std::abs(wp.y - cell.y);
                if (routeDist < 0 || d < routeDist)
                {
                    routeDist = d;
                }
            }
        }

        float hx = 0.0f, hy = 0.0f, hz = 0.0f, ho = 0.0f;
        me->GetHomePosition(hx, hy, hz, ho);
        Unit const* victim = me->GetVictim();

        // The role and, for a leader, the beat's two ends as the tag carries
        // them - GLOBAL cells, the same frame `.pdungeon v2 gen`'s block
        // coordinates divide into and the only frame in which two patrols of
        // one dungeon can be compared. A follower has no beat of its own, so it
        // names the creature whose beat it is walking behind instead.
        std::string role = "no-tag";
        if (_mob && _mob->isPatrol)
        {
            role = _mob->patrolRank
                 ? Acore::StringFormat("follower {} of guid {}", uint32(_mob->patrolRank),
                                       _mob->patrolLeader.GetCounter())
                 : Acore::StringFormat("leader beat ({},{})->({},{})",
                                       _mob->patrolStartCellX, _mob->patrolStartCellY,
                                       _mob->patrolGoalCellX, _mob->patrolGoalCellY);
        }

        return Acore::StringFormat(
            "{} entry {} guid {} | {} | pos ({:.0f},{:.0f},{:.0f}) cell ({},{}) walkable {} "
            "| top {} spline {} stopped {} "
            "| following {} idx {}/{} pending {} route {} nearest {} active {} rejoin {} "
            "| home ({:.0f},{:.0f}) dist {:.0f} | victim {}",
            me->GetName(), me->GetEntry(), me->GetGUID().GetCounter(), role,
            me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(),
            cell.x, cell.y, walkable,
            MotionName(me->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            me->movespline->Finalized() ? "finalized" : "RUNNING",
            me->IsStopped() ? 1 : 0,
            _followingPath ? 1 : 0, uint32(_waypointIndex), uint32(_waypoints.size()),
            _legPending ? 1 : 0,
            uint32(_patrolRoute.size()), routeDist, _patrolActive ? 1 : 0,
            _patrolRejoin ? 1 : 0,
            hx, hy, me->GetExactDist2d(hx, hy),
            victim ? victim->GetName() : std::string("none"));
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
        ApproachKind const verdict =
            PlanApproach(*grid,
                         CellOf(*grid, me->GetPositionX(), me->GetPositionY()),
                         CellOf(*grid, victim->GetPositionX(), victim->GetPositionY()),
                         SNAP_RADIUS_CELLS, waypoints);

        // D5. One line per DECISION, and only when the decision or the motion
        // stack under it changed - so a fight in which nothing moves costs one
        // line, and the signature that matters (verdict Unreachable with top
        // CHASE surviving the tick) is impossible to miss. Read BEFORE the
        // switch acts, because what the branch is about to do is only
        // interesting against what was there when it decided.
        if (PatrolDebug())
        {
            MovementGeneratorType const top =
                me->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (int(verdict) != _dbgChaseVerdict || int(top) != _dbgChaseTop)
            {
                _dbgChaseVerdict = int(verdict);
                _dbgChaseTop = int(top);
                GridPoint const meCell = CellOf(*grid, me->GetPositionX(), me->GetPositionY());
                GridPoint const vicCell =
                    CellOf(*grid, victim->GetPositionX(), victim->GetPositionY());
                LOG_INFO(PD_LOG, "PDv2 chase: {} verdict {} victim {} me-cell ({},{}) "
                                 "victim-cell ({},{}) top {}",
                         me->GetName(), ApproachName(verdict), victim->GetName(),
                         meCell.x, meCell.y, vicCell.x, vicCell.y, MotionName(top));
            }
        }

        switch (verdict)
        {
            case ApproachKind::Direct:
                // The straight line is floor the whole way, so the core chase
                // is safe: with no mmaps its generated path degenerates to
                // exactly that straight line.
                _chaseHeld = false;
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
                //
                // Round C: if the runner refuses the route it plans again on
                // the next 500 ms decision, from PlanApproach's fresh snap of
                // where the creature then stands - nothing here has to undo
                // anything, because a refused run started no motion. That
                // refusal cannot actually fire on this path: PlanApproach's
                // waypoint 0 IS NearestWalkable(our own cell, SNAP_RADIUS_CELLS)
                // (PDv2WalkGrid.cpp:329), i.e. within the radius by
                // construction. The guard is there for the producer that is not
                // this one.
                _chaseHeld = false;
                if (!_followingPath)
                {
                    // CELL CENTRES. The chase is aimed at a player standing
                    // wherever they like and re-decided every 500 ms; walking
                    // its waypoints off-centre would add a wobble to a route
                    // that already ends at a moving unit, and the clearance
                    // layer describes where a PATROL should be seen, not where
                    // a mob has to stand to reach someone.
                    StartWaypointRun(std::move(waypoints), *grid, /*clearPoints=*/false);
                }
                return _followingPath;

            case ApproachKind::Unreachable:
            default:
                // The target is not on walkable ground - mid-air over the void,
                // or off the layout entirely. Walking toward them would walk off
                // the world, so the creature HOLDS.
                //
                // Round C: it now actually holds. Round B only cleared the
                // module's own flags here, and StopWaypointRun never touches the
                // motion master - so whenever the ACTIVE slot held the core's
                // ChaseMovementGenerator (after any Direct verdict above, after
                // MovementInform's StopWaypointRun(true), or after an
                // AttackStart the module did not initiate, UnitAI.cpp:29-33) the
                // chase kept beelining at a target that had since become
                // unreachable. On map 760 nothing can refuse that beeline:
                // ChaseMovementGenerator::DispatchSplineToPosition
                // (TargetedMovementGenerator.cpp:99-149) gets PATHFIND_SHORTCUT
                // rather than a failure, so `pathFailed` is false and it launches
                // a straight 2-point spline across the void.
                //
                // Three calls, because each one is needed and none substitutes
                // for another: Clear(false) pops the chase generator (false
                // because the Reset() it would otherwise run lands on the idle
                // singleton, whose StopMoving is gated on UNIT_STATE_MOVING and
                // therefore unreliable - the same trap EnterEvadeMode documents),
                // StopMoving kills the spline that Clear cannot (Unit.cpp:13056),
                // and MoveIdle leaves the stack fully described.
                //
                // LATCHED, not re-run: the verdict is still re-taken on the
                // module's own 500 ms cadence, but a creature already standing
                // still is not cleared and stopped again every tick - that is
                // what _chaseHeld says, exactly as _holding does for the
                // caster's plant. Both other verdicts clear it, so the first
                // tick that has somewhere to walk to moves again.
                if (_followingPath)
                {
                    StopWaypointRun(false);
                }
                if (!_chaseHeld)
                {
                    me->GetMotionMaster()->Clear(false);
                    me->StopMoving();
                    me->GetMotionMaster()->MoveIdle();
                    _chaseHeld = true;
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
                //
                // Round C: STOP the spline as well. Clear(false) pops the
                // generator and, with reset == false, does not even reach
                // DirectClean's Reset() branch (MotionMaster.cpp:163) - and
                // that branch is unreliable anyway, because the idle singleton
                // it lands on skips StopMoving() whenever UNIT_STATE_MOVING is
                // already clear (IdleMovementGenerator.cpp:31-32). Without the
                // explicit stop a caster that planted mid-leg or mid-chase kept
                // GLIDING to its old destination while casting: the spline
                // survives its generator, because Unit::Update drives it
                // independently (Unit.cpp:635-636). Lower stakes than the
                // patrol evade - these destinations are on-grid - same bug.
                StopWaypointRun(false);
                me->GetMotionMaster()->Clear(false);
                me->StopMoving();
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

        // THE OWED LEG, FIRST - before the beat, before the chase, before the
        // aggro sweep, in and out of combat alike. MovementInform recorded an
        // arrival and refused to launch anything (the reason is written out
        // there); this is the one place in the module that starts a leg after
        // the first, and it is deliberately ahead of every decision that could
        // read _followingPath, so no branch below ever sees a run that is
        // between legs.
        //
        // The _mob fetch above is not an action, just the tag pointer this tick
        // needs - the leg's own debug line reads it.
        if (_legPending)
        {
            _legPending = false;
            WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
            if (!_followingPath || _waypointIndex >= _waypoints.size() || !grid)
            {
                // The run died between the arrival and now (an evade, a stop,
                // an instance without a grid). Nothing to walk, and the flags
                // must not survive it.
                StopWaypointRun(false);
            }
            else
            {
                MoveToWaypoint(_waypointIndex, *grid);
            }
        }

        // D4, once per patroller and here because this is the first tick on
        // which the tag is reliably present (the constructor's comment says
        // why). It pins two of the debug report's refuted hypotheses
        // empirically in the same run as the fix: the idle slot should read
        // IDLE with wander 0.0 - a summon has no `creature` row, so
        // RANDOM_MOTION_TYPE is downgraded at creation (Creature.cpp:569-571)
        // and nothing can wander this mob off the grid - and `levitating 0`,
        // which since Round D / D3 is simply the truth: no module summon sets
        // the gravity flag any more, because the core stripped it on the first
        // movement update and until then it was the operator's spawn hover
        // (the SpawnTaggedMob comment cites the core lines).
        if (_mob && _mob->isPatrol && !_dbgSlotLogged && PatrolDebug())
        {
            _dbgSlotLogged = true;
            LOG_INFO(PD_LOG, "PDv2 patrol: {} entry {} idle-slot {} defaultMove {} "
                             "wander {:.1f} canFly {} levitating {}",
                     me->GetName(), me->GetEntry(),
                     MotionName(me->GetMotionMaster()->GetMotionSlotType(MOTION_SLOT_IDLE)),
                     uint32(me->GetDefaultMovementType()), me->GetWanderDistance(),
                     me->CanFly() ? 1 : 0, me->IsLevitating() ? 1 : 0);
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
                // Round D / D2: which half of a patrol this is. The two are
                // exclusive by construction (rank 0 = the leader, the only one
                // carrying a beat), and a follower never walks a grid route -
                // one patrol, one planned path, however many creatures.
                if (_mob->patrolRank)
                {
                    UpdateFollower(diff);
                }
                else
                {
                    UpdatePatrol(diff);
                }
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
