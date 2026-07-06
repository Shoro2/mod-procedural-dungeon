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

#include "PDCreatureAI.h"
#include "MotionMaster.h"
#include "PDDefines.h"
#include "PDInstanceScript.h"
#include "PDMgr.h"
#include "ScriptMgr.h"

namespace PDungeon
{
    namespace
    {
        uint32 const PATH_CHECK_INTERVAL = 500;
        uint32 const CASTER_CAST_INTERVAL = 2500;
        uint32 const BOSS_STOMP_INTERVAL = 15000;
        float const CASTER_RANGE = 30.0f;

        PDInstanceScript* GetPDInstance(Creature* creature)
        {
            return dynamic_cast<PDInstanceScript*>(creature->GetInstanceScript());
        }
    }

    PDMobAI::PDMobAI(Creature* creature) : ScriptedAI(creature)
    {
        _instance = GetPDInstance(creature);
        _isCaster = creature->GetEntry() == NPC_TRASH_CASTER;
    }

    void PDMobAI::InitializeAI()
    {
        ScriptedAI::InitializeAI();
        _homeX = me->GetPositionX();
        _homeY = me->GetPositionY();
    }

    void PDMobAI::JustDied(Unit* /*killer*/)
    {
        if (_instance)
        {
            _instance->OnMobDied(me);
        }
    }

    void PDMobAI::JustEngagedWith(Unit* /*who*/)
    {
        _pathCheckTimer = 0;
        _castTimer = 1000;
    }

    bool PDMobAI::IsLeashed() const
    {
        PDConfig const& config = sPDMgr->GetConfig();
        float const leashRange = config.leashTiles * config.tileSize;
        float const dx = me->GetPositionX() - _homeX;
        float const dy = me->GetPositionY() - _homeY;
        return dx * dx + dy * dy > leashRange * leashRange;
    }

    void PDMobAI::StartWaypointRun(std::vector<PDGridPath::Point> const& path)
    {
        _waypoints = path;
        _waypointIndex = 1; // index 0 is the own tile
        if (_waypoints.size() < 2)
        {
            _followingPath = false;
            return;
        }
        _followingPath = true;

        Position const target = _instance->TileToWorldPosition(_waypoints[_waypointIndex].x + 0.5f, _waypoints[_waypointIndex].y + 0.5f);
        me->GetMotionMaster()->MovePoint(WAYPOINT_MOVE_ID_BASE + static_cast<uint32>(_waypointIndex),
                                         target.GetPositionX(), target.GetPositionY(), target.GetPositionZ(),
                                         FORCED_MOVEMENT_NONE, 0.0f, 0.0f, /*generatePath=*/false);
    }

    void PDMobAI::StopWaypointRun(bool resumeChase)
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

    void PDMobAI::MovementInform(uint32 type, uint32 id)
    {
        if (type != POINT_MOTION_TYPE || !_followingPath || id < WAYPOINT_MOVE_ID_BASE)
        {
            return;
        }

        ++_waypointIndex;
        if (_waypointIndex >= _waypoints.size())
        {
            StopWaypointRun(true);
            return;
        }

        Position const target = _instance->TileToWorldPosition(_waypoints[_waypointIndex].x + 0.5f, _waypoints[_waypointIndex].y + 0.5f);
        me->GetMotionMaster()->MovePoint(WAYPOINT_MOVE_ID_BASE + static_cast<uint32>(_waypointIndex),
                                         target.GetPositionX(), target.GetPositionY(), target.GetPositionZ(),
                                         FORCED_MOVEMENT_NONE, 0.0f, 0.0f, /*generatePath=*/false);
    }

    bool PDMobAI::UpdateCrossRoomMovement(uint32 diff)
    {
        if (!_instance || !_instance->IsGenerated())
        {
            return false;
        }

        if (_pathCheckTimer > diff)
        {
            _pathCheckTimer -= diff;
            return _followingPath;
        }
        _pathCheckTimer = PATH_CHECK_INTERVAL;

        Unit* victim = me->GetVictim();
        if (!victim)
        {
            return _followingPath;
        }

        PDLayout const& layout = _instance->GetLayout();
        int myTileX = 0;
        int myTileY = 0;
        int victimTileX = 0;
        int victimTileY = 0;
        if (!_instance->WorldToTile(me->GetPositionX(), me->GetPositionY(), myTileX, myTileY) ||
            !_instance->WorldToTile(victim->GetPositionX(), victim->GetPositionY(), victimTileX, victimTileY))
        {
            return _followingPath;
        }

        int const myRoom = layout.RoomIdAt(myTileX, myTileY);
        int const victimRoom = layout.RoomIdAt(victimTileX, victimTileY);
        bool const sameArea = myRoom == victimRoom && myRoom != -1;
        if (sameArea || me->IsWithinLOSInMap(victim))
        {
            if (_followingPath)
            {
                StopWaypointRun(true);
            }
            return false;
        }

        if (_followingPath)
        {
            return true;
        }

        std::vector<PDGridPath::Point> path;
        std::vector<TilePos> const& closedDoors = _instance->GetClosedDoorTiles();
        std::vector<PDGridPath::Point> blocked;
        blocked.reserve(closedDoors.size());
        for (TilePos const& tile : closedDoors)
        {
            blocked.push_back({ tile.x, tile.y });
        }

        if (!PDGridPath::FindPath(layout, { myTileX, myTileY }, { victimTileX, victimTileY }, path, blocked.empty() ? nullptr : &blocked))
        {
            return false;
        }
        PDGridPath::SimplifyPath(layout, path);
        StartWaypointRun(path);
        return _followingPath;
    }

    void PDMobAI::UpdateAI(uint32 diff)
    {
        if (!UpdateVictim())
        {
            return;
        }

        if (IsLeashed())
        {
            EnterEvadeMode(EVADE_REASON_BOUNDARY);
            return;
        }

        bool const walkingPath = UpdateCrossRoomMovement(diff);

        if (_isCaster && !walkingPath)
        {
            if (_castTimer > diff)
            {
                _castTimer -= diff;
            }
            else if (Unit* victim = me->GetVictim())
            {
                if (me->IsWithinDistInMap(victim, CASTER_RANGE) && me->IsWithinLOSInMap(victim))
                {
                    DoCastVictim(SPELL_SHADOW_BOLT);
                    _castTimer = CASTER_CAST_INTERVAL;
                }
            }
        }

        DoMeleeAttackIfReady();
    }

    PDBossAI::PDBossAI(Creature* creature) : PDMobAI(creature)
    {
    }

    void PDBossAI::JustDied(Unit* /*killer*/)
    {
        if (_instance)
        {
            _instance->OnBossDied();
            _instance->OnMobDied(me);
        }
    }

    void PDBossAI::UpdateAI(uint32 diff)
    {
        if (!UpdateVictim())
        {
            return;
        }

        if (_stompTimer > diff)
        {
            _stompTimer -= diff;
        }
        else
        {
            DoCastSelf(SPELL_WAR_STOMP);
            _stompTimer = BOSS_STOMP_INTERVAL;
        }

        UpdateCrossRoomMovement(diff);
        DoMeleeAttackIfReady();
    }
}

// Bound via creature_template.ScriptName for the trash/elite entries.
class npc_pdungeon_mob : public CreatureScript
{
public:
    npc_pdungeon_mob() : CreatureScript("npc_pdungeon_mob") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new PDungeon::PDMobAI(creature);
    }
};

class npc_pdungeon_boss : public CreatureScript
{
public:
    npc_pdungeon_boss() : CreatureScript("npc_pdungeon_boss") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new PDungeon::PDBossAI(creature);
    }
};

void AddPDCreatureScripts()
{
    new npc_pdungeon_mob();
    new npc_pdungeon_boss();
}
