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

#ifndef MOD_PDUNGEON_CREATURE_AI_H
#define MOD_PDUNGEON_CREATURE_AI_H

#include "ScriptedCreature.h"
#include "generator/PDGridPath.h"

namespace PDungeon
{
    class PDInstanceScript;

    // Mob AI for the runtime-assembled dungeon. Static mmaps cannot see the
    // spawned walls, so cross-room chases run on module A* waypoints executed
    // with MovePoint(generatePath = false); inside the convex rooms the
    // default chase is safe. Casters gate their casts on dynamic-tree LoS.
    struct PDMobAI : public ScriptedAI
    {
        explicit PDMobAI(Creature* creature);

        void InitializeAI() override;
        void JustDied(Unit* killer) override;
        void JustEngagedWith(Unit* who) override;
        void UpdateAI(uint32 diff) override;
        void MovementInform(uint32 type, uint32 id) override;

    protected:
        bool UpdateCrossRoomMovement(uint32 diff);
        void StartWaypointRun(std::vector<PDGridPath::Point> const& path);
        void StopWaypointRun(bool resumeChase);
        bool IsLeashed() const;

        PDInstanceScript* _instance = nullptr;
        bool _isCaster = false;
        float _homeX = 0.0f;
        float _homeY = 0.0f;
        uint32 _pathCheckTimer = 0;
        uint32 _castTimer = 0;
        std::vector<PDGridPath::Point> _waypoints;
        size_t _waypointIndex = 0;
        bool _followingPath = false;
    };

    struct PDBossAI : public PDMobAI
    {
        explicit PDBossAI(Creature* creature);

        void JustDied(Unit* killer) override;
        void UpdateAI(uint32 diff) override;

    private:
        uint32 _stompTimer = 0;
    };
}

#endif
