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

#ifndef MOD_PDUNGEON_INSTANCE_SCRIPT_H
#define MOD_PDUNGEON_INSTANCE_SCRIPT_H

#include "InstanceScript.h"
#include "PDWorldBuilder.h"
#include "Position.h"
#include <set>
#include <unordered_map>
#include <vector>

class Creature;
class InstanceMap;

namespace PDungeon
{
    // One procedural run: owns the generated layout, materializes it as
    // batched GameObject/creature summons, tracks room clears, gates the boss
    // room and handles completion. Attached by PDMapScript via the
    // OnBeforeCreateInstanceScript hook - no instance_template.ScriptId needed.
    class PDInstanceScript : public InstanceScript
    {
    public:
        explicit PDInstanceScript(InstanceMap* map);

        void Initialize() override { }
        void Load(char const* data) override;
        std::string GetSaveData() override;
        void Update(uint32 diff) override;
        void OnPlayerEnter(Player* player) override;

        // Called by the module creature AI.
        void OnMobDied(Creature* creature);
        void OnBossDied();

        bool IsGenerated() const { return _generated; }
        bool IsCompleted() const { return _completed; }
        uint32 GetSeed() const { return _seed; }
        uint32 GetGenerationMicros() const { return _generationMicros; }
        uint32 GetSpawnedCount() const { return _spawnedCount; }
        uint32 GetQueuedCount() const { return static_cast<uint32>(_initialSpawns.size() - _spawnIndex); }
        uint32 GetEliteRoomsRemaining() const { return _eliteRoomsRemaining; }
        PDLayout const& GetLayout() const { return _layout; }

        Position TileToWorldPosition(float tileX, float tileY) const;
        bool WorldToTile(float worldX, float worldY, int& tileX, int& tileY) const;
        float GroundZ(float worldX, float worldY) const;

        // Tiles the module AI must treat as blocked (closed gates).
        std::vector<TilePos> const& GetClosedDoorTiles() const { return _closedDoorTiles; }

    private:
        void SetupRun(uint32 seed);
        void SpawnBatch(uint32 budget);
        void SpawnOne(PlannedSpawn const& plan);
        void RegisterMob(Creature* creature, int roomId);
        void OnRoomCleared(int roomId);
        void CheckRoomCleared(int roomId);
        void OpenDoorGroup(uint32 group);
        void RebuildClosedDoorTiles();
        void OpenBossDoors();
        void HandleCompletion(bool silent);
        void ProcessPendingIntroTeleports();

        bool _generated = false;
        bool _completed = false;
        bool _bossDoorsOpen = false;
        uint32 _seed = 0;
        uint32 _generationMicros = 0;
        uint32 _spawnedCount = 0;
        size_t _spawnIndex = 0;
        PDLayout _layout;
        std::vector<PlannedSpawn> _initialSpawns;
        std::vector<PlannedSpawn> _completionSpawns;
        Position _entrancePosition;

        std::unordered_map<uint32, std::vector<ObjectGuid>> _gateGuids; // doorGroupId -> gate pieces
        std::unordered_map<uint32, std::vector<TilePos>> _groupTiles;   // doorGroupId -> its doorway tiles
        std::unordered_map<int, std::vector<uint32>> _roomDoorGroups;   // roomId -> groups opened when it clears
        std::set<uint32> _bossDoorGroups;
        std::set<uint32> _openDoorGroups;    // groups currently open (or scheduled to open)
        std::set<uint32> _collisionWarnings; // entries already reported without model
        std::vector<TilePos> _closedDoorTiles;

        std::unordered_map<ObjectGuid, int> _mobRooms;
        std::unordered_map<int, uint32> _roomMobsAlive;
        std::unordered_map<int, uint32> _roomMobsPlanned; // mobs that will actually spawn (post-truncation)
        std::unordered_map<int, uint32> _roomMobsSpawned; // mobs spawned so far
        std::set<int> _clearedRooms;                      // idempotency for OnRoomCleared
        uint32 _eliteRoomsRemaining = 0;

        std::vector<ObjectGuid> _pendingIntroTeleports;
        ObjectGuid _leaderGuid;
        bool _runRowInserted = false;
    };
}

#endif
