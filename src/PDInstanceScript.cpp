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

#include "PDInstanceScript.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PDDefines.h"
#include "PDMgr.h"
#include "PDPaletteMgr.h"
#include "Player.h"
#include "Random.h"
#include "TemporarySummon.h"
#include "generator/PDDungeonGenerator.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>

namespace PDungeon
{
    PDInstanceScript::PDInstanceScript(InstanceMap* map) : InstanceScript(map)
    {
    }

    void PDInstanceScript::Load(char const* data)
    {
        if (!data)
        {
            return;
        }

        uint32 seed = 0;
        uint32 flags = 0;
        if (std::sscanf(data, "PD %u %u", &seed, &flags) < 1 || !seed)
        {
            LOG_ERROR(PD_LOG, "instance {}: unparseable save data '{}'", instance->GetInstanceId(), data);
            return;
        }

        SetupRun(seed);
        if (_generated && (flags & 1))
        {
            HandleCompletion(true);
        }
    }

    std::string PDInstanceScript::GetSaveData()
    {
        if (!_generated)
        {
            return "";
        }
        return "PD " + std::to_string(_seed) + " " + (_completed ? "1" : "0");
    }

    Position PDInstanceScript::TileToWorldPosition(float tileX, float tileY) const
    {
        float x = 0.0f;
        float y = 0.0f;
        PDWorldBuilder::TileToWorld(sPDMgr->GetConfig(), _layout, tileX, tileY, x, y);
        return Position(x, y, GroundZ(x, y), sPDMgr->GetConfig().orientation);
    }

    bool PDInstanceScript::WorldToTile(float worldX, float worldY, int& tileX, int& tileY) const
    {
        return PDWorldBuilder::WorldToTile(sPDMgr->GetConfig(), _layout, worldX, worldY, tileX, tileY);
    }

    float PDInstanceScript::GroundZ(float worldX, float worldY) const
    {
        float const centerZ = sPDMgr->GetConfig().centerZ;
        float const height = instance->GetHeight(worldX, worldY, centerZ + 50.0f, true, 120.0f);
        if (height <= INVALID_HEIGHT + 1.0f)
        {
            return centerZ;
        }
        return height;
    }

    void PDInstanceScript::SetupRun(uint32 seed)
    {
        if (_generated)
        {
            return;
        }

        PDConfig const& config = sPDMgr->GetConfig();
        if (!sPDPaletteMgr->IsLoaded())
        {
            LOG_ERROR(PD_LOG, "instance {}: palette not loaded, cannot build dungeon", instance->GetInstanceId());
            return;
        }

        GenConfig genConfig = config.gen;
        genConfig.seed = seed;

        auto const start = std::chrono::steady_clock::now();
        if (!PDDungeonGenerator::Generate(genConfig, _layout))
        {
            LOG_ERROR(PD_LOG, "instance {}: generation failed for seed {}", instance->GetInstanceId(), seed);
            return;
        }
        _generationMicros = static_cast<uint32>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        _seed = seed;

        PDWorldBuilder::Build(_layout, config, _initialSpawns, _completionSpawns);
        if (_initialSpawns.size() > config.maxGameObjects)
        {
            // Truncate cosmetic decor FIRST so collision-critical pieces (walls,
            // gates, towers) and creatures (boss + required mobs) always survive
            // the cap - otherwise the highest-sortKey entries (creatures, incl. the
            // boss) would be dropped first and the run become unwinnable. Partition
            // keeps critical ahead of decor (build-out order preserved within each
            // group); resize then drops from the decor tail; then restore sortKey
            // build-out order.
            auto const isCritical = [](PlannedSpawn const& s)
            {
                return s.isCreature || s.requiresCollision;
            };
            auto const firstDecor = std::stable_partition(_initialSpawns.begin(), _initialSpawns.end(), isCritical);
            size_t const criticalCount = static_cast<size_t>(firstDecor - _initialSpawns.begin());
            if (criticalCount > config.maxGameObjects)
            {
                LOG_ERROR(PD_LOG, "instance {}: {} collision-critical/creature spawns exceed ProceduralDungeon.Spawn.MaxGameObjects ({}); {} critical dropped - raise the cap",
                          instance->GetInstanceId(), criticalCount, config.maxGameObjects, criticalCount - config.maxGameObjects);
            }
            else
            {
                LOG_WARN(PD_LOG, "instance {}: spawn plan ({}) exceeds ProceduralDungeon.Spawn.MaxGameObjects ({}); dropped {} cosmetic decor (walls/gates/boss/mobs kept)",
                         instance->GetInstanceId(), _initialSpawns.size(), config.maxGameObjects, _initialSpawns.size() - config.maxGameObjects);
            }
            _initialSpawns.resize(config.maxGameObjects);
            std::stable_sort(_initialSpawns.begin(), _initialSpawns.end(),
                             [](PlannedSpawn const& a, PlannedSpawn const& b) { return a.sortKey < b.sortKey; });
        }

        Room const* entrance = _layout.GetRoom(_layout.entranceRoomId);
        _entrancePosition = TileToWorldPosition(entrance->CenterX() + 0.5f, entrance->CenterY() + 0.5f);
        _entrancePosition.m_positionZ += 0.5f;

        // Every doorway is a closed blocking gate that opens on adjacent-room
        // clear. Record each group's tiles (for the fallback mob A* blocker) and
        // which rooms open it (its corridor-blob incident rooms).
        for (size_t group = 0; group < _layout.doorways.size(); ++group)
        {
            uint32 const groupId = static_cast<uint32>(group) + 1;
            _groupTiles[groupId] = _layout.doorways[group].tiles;
        }

        // A doorway is a boss door when any of its tiles touches boss room floor;
        // boss gates ignore the adjacency rule and open only via OpenBossDoors.
        int const offsetsX[4] = { -1, 1, 0, 0 };
        int const offsetsY[4] = { 0, 0, -1, 1 };
        for (size_t group = 0; group < _layout.doorways.size(); ++group)
        {
            bool isBossDoor = false;
            for (TilePos const& tile : _layout.doorways[group].tiles)
            {
                for (int n = 0; n < 4 && !isBossDoor; ++n)
                {
                    int const nx = tile.x + offsetsX[n];
                    int const ny = tile.y + offsetsY[n];
                    if (_layout.At(nx, ny) == TileType::RoomFloor && _layout.RoomIdAt(nx, ny) == _layout.bossRoomId)
                    {
                        isBossDoor = true;
                    }
                }
                if (isBossDoor)
                {
                    break;
                }
            }
            if (isBossDoor)
            {
                _bossDoorGroups.insert(static_cast<uint32>(group) + 1);
            }
        }

        // Planned mob counts from the post-truncation plan: a room clears when
        // its last actually-spawned mob dies, so count what will really spawn.
        for (PlannedSpawn const& plan : _initialSpawns)
        {
            if (plan.isCreature && plan.roomId >= 0)
            {
                ++_roomMobsPlanned[plan.roomId];
            }
        }

        // Doorway <-> rooms adjacency: clearing a room opens every gate whose
        // corridor blob touches that room (both the gate leaving it and the gate
        // letting the player into the neighbor).
        std::vector<std::vector<int>> const incidentRooms = _layout.DoorwayIncidentRooms();
        for (size_t i = 0; i < incidentRooms.size(); ++i)
        {
            uint32 const groupId = static_cast<uint32>(i) + 1;
            for (int roomId : incidentRooms[i])
            {
                _roomDoorGroups[roomId].push_back(groupId);
            }
        }

        // Boss gate is additionally gated on clearing every elite room.
        for (Room const& room : _layout.rooms)
        {
            if (room.kind == RoomKind::Elite && _roomMobsPlanned[room.id] > 0)
            {
                ++_eliteRoomsRemaining;
            }
        }
        if (!_eliteRoomsRemaining)
        {
            _bossDoorsOpen = true;
        }

        // Seed the fallback mob-path blocker from every currently-closed door.
        RebuildClosedDoorTiles();

        // Auto-clear rooms the player can never be trapped behind: the entrance
        // and any room with no mobs to fight (shrine / empty / truncated).
        OnRoomCleared(_layout.entranceRoomId);
        for (Room const& room : _layout.rooms)
        {
            if (_roomMobsPlanned[room.id] == 0)
            {
                OnRoomCleared(room.id);
            }
        }

        _generated = true;
        LOG_INFO(PD_LOG, "instance {}: seed {} -> {} rooms, {} doorways, {} spawns planned ({} on completion), generated in {}us",
                 instance->GetInstanceId(), _seed, _layout.rooms.size(), _layout.doorways.size(),
                 _initialSpawns.size(), _completionSpawns.size(), _generationMicros);

        if (config.debug)
        {
            std::istringstream dump(_layout.AsciiDump());
            std::string line;
            while (std::getline(dump, line))
            {
                LOG_INFO(PD_LOG, "{}", line);
            }
        }

        SaveToDB();
    }

    void PDInstanceScript::OnPlayerEnter(Player* player)
    {
        InstanceScript::OnPlayerEnter(player);

        if (!sPDMgr->IsEnabled() || !player)
        {
            return;
        }

        if (!_generated)
        {
            uint32 seed = 0;
            if (!sPDMgr->ConsumePendingSeed(player->GetGUID(), seed) || !seed)
            {
                seed = urand(1, 0x7FFFFFFE);
            }
            SetupRun(seed);
        }

        if (!_generated)
        {
            return;
        }

        if (_leaderGuid.IsEmpty())
        {
            _leaderGuid = player->GetGUID();
        }
        if (!_runRowInserted)
        {
            _runRowInserted = true;
            CharacterDatabase.Execute("INSERT INTO pdungeon_runs (seed, map_id, instance_id, leader_guid, started_at) VALUES ({}, {}, {}, {}, NOW())",
                                      _seed, instance->GetId(), instance->GetInstanceId(), _leaderGuid.GetRawValue());
            SaveToDB();
        }

        _pendingIntroTeleports.push_back(player->GetGUID());
    }

    void PDInstanceScript::ProcessPendingIntroTeleports()
    {
        if (_pendingIntroTeleports.empty())
        {
            return;
        }

        std::vector<ObjectGuid> remaining;
        for (ObjectGuid guid : _pendingIntroTeleports)
        {
            Player* player = ObjectAccessor::GetPlayer(instance, guid);
            if (!player)
            {
                continue; // left the map again
            }
            if (player->IsBeingTeleported())
            {
                remaining.push_back(guid);
                continue;
            }
            player->NearTeleportTo(_entrancePosition.GetPositionX(), _entrancePosition.GetPositionY(),
                                   _entrancePosition.GetPositionZ() + 1.0f, _entrancePosition.GetOrientation());
        }
        _pendingIntroTeleports = std::move(remaining);
    }

    void PDInstanceScript::Update(uint32 diff)
    {
        InstanceScript::Update(diff);

        if (!_generated)
        {
            return;
        }

        ProcessPendingIntroTeleports();

        if (_spawnIndex < _initialSpawns.size())
        {
            SpawnBatch(sPDMgr->GetConfig().spawnBatchSize);
        }
    }

    void PDInstanceScript::SpawnBatch(uint32 budget)
    {
        while (budget-- && _spawnIndex < _initialSpawns.size())
        {
            SpawnOne(_initialSpawns[_spawnIndex]);
            ++_spawnIndex;
        }
        if (_spawnIndex == _initialSpawns.size())
        {
            LOG_DEBUG(PD_LOG, "instance {}: build-out complete, {} objects spawned", instance->GetInstanceId(), _spawnedCount);
        }
    }

    void PDInstanceScript::SpawnOne(PlannedSpawn const& plan)
    {
        float const z = GroundZ(plan.x, plan.y) + plan.zOffset;

        if (plan.isCreature)
        {
            Position const pos(plan.x, plan.y, z + 0.5f, plan.o);
            TempSummon* summon = instance->SummonCreature(plan.entry, pos);
            if (!summon)
            {
                LOG_ERROR(PD_LOG, "instance {}: failed to summon creature {} (missing creature_template?)", instance->GetInstanceId(), plan.entry);
                // The plan counted this mob; a failed summon means it will never spawn
                // or die. Drop it from the room's plan and re-check the room so a summon
                // failure can never leave the room's gates permanently shut (softlock).
                if (plan.roomId >= 0 && _roomMobsPlanned[plan.roomId] > 0)
                {
                    --_roomMobsPlanned[plan.roomId];
                }
                CheckRoomCleared(plan.roomId);
                return;
            }
            int const level = sPDMgr->GetConfig().mobLevel;
            if (level > 0)
            {
                summon->SetLevel(static_cast<uint8>(level));
                summon->SetHealth(summon->GetMaxHealth());
            }
            RegisterMob(summon, plan.roomId);
            ++_spawnedCount;
            return;
        }

        GameObject* go = instance->SummonGameObject(plan.entry, plan.x, plan.y, z, plan.o, 0.0f, 0.0f, 0.0f, 0.0f, 0);
        if (!go)
        {
            LOG_ERROR(PD_LOG, "instance {}: failed to summon gameobject {} (missing gameobject_template?)", instance->GetInstanceId(), plan.entry);
            return;
        }
        ++_spawnedCount;

        // Gates span their opening via M2 scale (walls can't scale - WMO ignores
        // it). Rebuild the collision model so the door blocks at the new width;
        // the model is (re)created for the current GO_STATE_READY (closed) here,
        // before the doorGroupId open-state block, so an auto-opened group still
        // opens correctly.
        if (plan.scale > 0.0f)
        {
            go->SetObjectScale(plan.scale);
            // UpdateModel() is protected; SetDisplayId (public) re-runs it, rebuilding
            // m_model at the new scale so server collision matches the scaled width.
            go->SetDisplayId(go->GetDisplayId());
        }

        if (plan.requiresCollision)
        {
            go->SetGameObjectFlag(GameObjectFlags(GO_FLAG_NOT_SELECTABLE | GO_FLAG_LOCKED));
            if (!go->m_model && _collisionWarnings.insert(plan.entry).second)
            {
                LOG_ERROR(PD_LOG, "gameobject {} (display {}) has NO collision model - its displayId is missing from vmaps/GAMEOBJECT_MODELS",
                          plan.entry, go->GetDisplayId());
            }
        }

        if (plan.doorGroupId)
        {
            _gateGuids[plan.doorGroupId].push_back(go->GetGUID());
            // Spawn every gate closed (GO_STATE_READY) unless its group is
            // already scheduled open - gates spawn in later Update batches, so a
            // door auto-opened at setup must still open when its piece appears.
            bool const bossGate = _bossDoorGroups.find(plan.doorGroupId) != _bossDoorGroups.end();
            bool const open = bossGate ? _bossDoorsOpen
                                       : _openDoorGroups.find(plan.doorGroupId) != _openDoorGroups.end();
            if (open)
            {
                go->SetGoState(GO_STATE_ACTIVE);
            }
        }
    }

    void PDInstanceScript::RegisterMob(Creature* creature, int roomId)
    {
        _mobRooms[creature->GetGUID()] = roomId;
        ++_roomMobsAlive[roomId];
        ++_roomMobsSpawned[roomId];
    }

    void PDInstanceScript::OnRoomCleared(int roomId)
    {
        if (roomId < 0 || !_clearedRooms.insert(roomId).second)
        {
            return;
        }
        auto const itr = _roomDoorGroups.find(roomId);
        if (itr != _roomDoorGroups.end())
        {
            for (uint32 group : itr->second)
            {
                OpenDoorGroup(group);
            }
        }
    }

    void PDInstanceScript::OpenDoorGroup(uint32 group)
    {
        if (_bossDoorGroups.find(group) != _bossDoorGroups.end())
        {
            return; // boss gates are opened only by OpenBossDoors
        }
        if (!_openDoorGroups.insert(group).second)
        {
            return; // idempotent
        }
        auto const itr = _gateGuids.find(group);
        if (itr != _gateGuids.end())
        {
            for (ObjectGuid guid : itr->second)
            {
                if (GameObject* gate = instance->GetGameObject(guid))
                {
                    gate->SetGoState(GO_STATE_ACTIVE);
                }
            }
        }
        RebuildClosedDoorTiles();
    }

    void PDInstanceScript::RebuildClosedDoorTiles()
    {
        auto isOpen = [&](uint32 group)
        {
            if (_bossDoorGroups.find(group) != _bossDoorGroups.end())
            {
                return _bossDoorsOpen;
            }
            return _openDoorGroups.find(group) != _openDoorGroups.end();
        };

        _closedDoorTiles.clear();
        for (auto const& pair : _groupTiles)
        {
            if (!isOpen(pair.first))
            {
                _closedDoorTiles.insert(_closedDoorTiles.end(), pair.second.begin(), pair.second.end());
            }
        }
    }

    void PDInstanceScript::OnMobDied(Creature* creature)
    {
        auto const itr = _mobRooms.find(creature->GetGUID());
        if (itr == _mobRooms.end())
        {
            return;
        }
        int const roomId = itr->second;
        _mobRooms.erase(itr);

        uint32& alive = _roomMobsAlive[roomId];
        if (alive)
        {
            --alive;
        }
        if (alive)
        {
            return;
        }
        CheckRoomCleared(roomId);
    }

    // A room is cleared once all its planned mobs have spawned and died - or failed
    // to summon (SpawnOne drops the failed mob from the plan and re-checks here).
    // Fires exactly once per room: opens the room's adjacency gates and, for an elite
    // room, ticks the boss-gate counter. The summon-failure path routing through here
    // is what keeps a failed spawn from permanently sealing a room's gates (softlock).
    void PDInstanceScript::CheckRoomCleared(int roomId)
    {
        if (roomId < 0 || _clearedRooms.find(roomId) != _clearedRooms.end())
        {
            return;
        }
        if (_roomMobsAlive[roomId] != 0 || _roomMobsSpawned[roomId] < _roomMobsPlanned[roomId])
        {
            return;
        }

        OnRoomCleared(roomId); // opens this room's adjacency gates (marks _clearedRooms)

        Room const* room = _layout.GetRoom(roomId);
        if (room && room->kind == RoomKind::Elite && _eliteRoomsRemaining)
        {
            --_eliteRoomsRemaining;
            if (!_eliteRoomsRemaining)
            {
                OpenBossDoors();
            }
            else
            {
                DoSendNotifyToInstance("An elite chamber falls silent. %u remain before the boss gate opens.", _eliteRoomsRemaining);
            }
        }
    }

    void PDInstanceScript::OpenBossDoors()
    {
        if (_bossDoorsOpen)
        {
            return;
        }
        _bossDoorsOpen = true;

        for (uint32 group : _bossDoorGroups)
        {
            _openDoorGroups.insert(group);
            auto const itr = _gateGuids.find(group);
            if (itr == _gateGuids.end())
            {
                continue;
            }
            for (ObjectGuid guid : itr->second)
            {
                if (GameObject* gate = instance->GetGameObject(guid))
                {
                    gate->SetGoState(GO_STATE_ACTIVE);
                }
            }
        }
        RebuildClosedDoorTiles();
        DoSendNotifyToInstance("The boss gate grinds open!");
    }

    void PDInstanceScript::OnBossDied()
    {
        HandleCompletion(false);
    }

    void PDInstanceScript::HandleCompletion(bool silent)
    {
        if (_completed)
        {
            return;
        }
        _completed = true;

        OpenBossDoors();
        for (auto const& pair : _gateGuids)
        {
            _openDoorGroups.insert(pair.first);
            for (ObjectGuid guid : pair.second)
            {
                if (GameObject* gate = instance->GetGameObject(guid))
                {
                    gate->SetGoState(GO_STATE_ACTIVE);
                }
            }
        }
        // Everything is open now; stop blocking late-pathing mobs.
        _closedDoorTiles.clear();

        for (PlannedSpawn const& plan : _completionSpawns)
        {
            SpawnOne(plan);
        }

        if (!silent)
        {
            DoSendNotifyToInstance("The dungeon falls silent - claim your reward and step through the portal!");
            CharacterDatabase.Execute("UPDATE pdungeon_runs SET completed_at = NOW(), result = 1 WHERE instance_id = {} AND seed = {} AND result = 0",
                                      instance->GetInstanceId(), _seed);
        }
        SaveToDB();
    }
}
