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
    struct PDv2MobAI : public ScriptedAI
    {
        explicit PDv2MobAI(Creature* creature);

        void JustEngagedWith(Unit* who) override;
        void JustDied(Unit* killer) override;
        void JustReachedHome() override;
        void UpdateAI(uint32 diff) override;
        void MovementInform(uint32 type, uint32 id) override;

    protected:
        bool UpdateGridChase(uint32 diff);
        void UpdateCasterCombat(uint32 diff);
        void UpdateProximityAggro(uint32 diff);
        void UpdateImmolation(uint32 diff);
        void CallAlliesForHelp(Unit* victim);
        bool GridLineOkTo(Unit* victim) const;
        void StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid);
        void MoveToWaypoint(size_t index, WalkGrid const& grid);
        void StopWaypointRun(bool resumeChase);

        PDv2InstanceScript* _instance = nullptr;
        // The spawn tag. Not owned, and not resolvable in the constructor for
        // every creature - see the comment there.
        PDv2MobData const* _mob = nullptr;
        uint32 _repathTimer = 0;
        uint32 _aggroTimer = 0;
        uint32 _lineTimer = 0;
        uint32 _immolationTimer = 0;    // affix 4, accumulates toward 2 s
        uint32 _nukeCooldown = 0;       // caster nuke gap; 0 = ready to cast
        std::vector<GridPoint> _waypoints;
        size_t _waypointIndex = 0;
        bool _followingPath = false;
        bool _lineOk = false;       // last grid-line verdict, refreshed on the tick
        bool _holding = false;      // a caster that has planted itself at range
        bool _hasCalled = false;    // affix 1 already shouted for THIS fight
    };
}

#endif
