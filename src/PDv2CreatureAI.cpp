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
#include "PDv2InstanceScript.h"
#include "PDv2Mgr.h"
#include "ScriptMgr.h"

namespace PDungeon
{
    namespace
    {
        // How often the chase decision is re-taken. 500 ms was v1's number
        // and survived live testing; the tick is cheap, because an A* runs
        // only when the straight line is blocked and no run is active.
        uint32 const REPATH_INTERVAL_MS = 500;

        // Cells searched around a live position for walkable ground, ~2 cells
        // = up to ~16 yd. Anything further off the surface is not a position
        // to walk to but one to refuse - a target mid-jump over the void.
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
    }

    void PDv2MobAI::JustEngagedWith(Unit* /*who*/)
    {
        // A previous fight may have left a stale run behind (evade clears the
        // motion, not these flags), and the automatic chase that AttackStart
        // starts walks a straight line - which this map cannot promise is
        // floor. Decide on the very next UpdateAI tick.
        StopWaypointRun(false);
        _repathTimer = 0;
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
        _waypointIndex = 1; // index 0 is the cell the creature stands on
        if (_waypoints.size() < 2)
        {
            _followingPath = false;
            return;
        }
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
        if (_waypointIndex >= _waypoints.size())
        {
            // Arrived where the target WAS when the path was planned. Resume
            // the chase and force the next tick to re-decide, so a target
            // that moved on is followed by plan rather than by shortcut.
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

    void PDv2MobAI::UpdateAI(uint32 diff)
    {
        // Deliberately no distance leash: dungeon mobs chase for as long as
        // the target exists on the map, like any stock instance (operator
        // decision 2026-08-06, replacing a working 150 yd leash). Reset still
        // happens the normal way - UpdateVictim() fails when the target dies
        // or leaves the map - and a target the grid cannot reach is held at
        // bay by UpdateGridChase's Unreachable case, not by walking after it.
        if (!UpdateVictim())
        {
            return;
        }

        UpdateGridChase(diff);
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

        return new PDungeon::PDv2MobAI(creature);
    }
};

void AddPDv2CreatureScripts()
{
    new PDv2CreatureAIBinder();
}
