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

// Standalone verification harness for the engine-free generator core.
// Not part of the module build (CMake only collects src/**), compile with:
//   g++ -std=c++17 -Wall -Wextra -Werror -O2 -Isrc tests/ascii_harness.cpp src/generator/*.cpp -o pdgen
// Usage:
//   pdgen <seed> [--path]      print one dungeon as ASCII (+ A* overlay)
//   pdgen --batch <count> [startSeed]   invariant checks over many seeds

#include "generator/PDDungeonGenerator.h"
#include "generator/PDGridPath.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace PDungeon;

namespace
{
    char const* RoomKindName(RoomKind kind)
    {
        switch (kind)
        {
            case RoomKind::Entrance:
                return "Entrance";
            case RoomKind::Combat:
                return "Combat";
            case RoomKind::Elite:
                return "Elite";
            case RoomKind::Treasure:
                return "Treasure";
            case RoomKind::Shrine:
                return "Shrine";
            case RoomKind::Boss:
                return "Boss";
        }
        return "?";
    }

    int CountTiles(PDLayout const& layout, TileType type)
    {
        int count = 0;
        for (TileType tile : layout.tiles)
        {
            if (tile == type)
            {
                ++count;
            }
        }
        return count;
    }

    // Rough GameObject estimate: greedy 3/2/1 wall tiling ~= tiles/2.2,
    // plus one gate per doorway plus all decorations.
    int EstimateGameObjects(PDLayout const& layout)
    {
        int const wallTiles = CountTiles(layout, TileType::Wall);
        return wallTiles * 10 / 22 + static_cast<int>(layout.doorways.size()) + static_cast<int>(layout.decorations.size());
    }

    void PrintStats(PDLayout const& layout, long long microseconds)
    {
        std::printf("seed=%u effectiveSeed=%u grid=%dx%d rooms=%zu corridors=%zu doorways=%zu decorations=%zu spawns=%zu\n",
                    layout.config.seed, layout.effectiveSeed, layout.width, layout.height,
                    layout.rooms.size(), layout.corridors.size(), layout.doorways.size(),
                    layout.decorations.size(), layout.spawnPoints.size());
        std::printf("wallTiles=%d floorTiles=%d estimatedGameObjects=%d genTime=%lldus\n",
                    CountTiles(layout, TileType::Wall),
                    CountTiles(layout, TileType::RoomFloor) + CountTiles(layout, TileType::Corridor) + CountTiles(layout, TileType::Doorway),
                    EstimateGameObjects(layout), microseconds);
        for (Room const& room : layout.rooms)
        {
            std::printf("  room %2d: %-8s at (%2d,%2d) size %2dx%-2d depth %d\n",
                        room.id, RoomKindName(room.kind), room.x, room.y, room.w, room.h, room.depth);
        }
    }

    bool CheckLayout(PDLayout const& layout, std::string& error)
    {
        if (static_cast<int>(layout.rooms.size()) < layout.config.roomsMin ||
            static_cast<int>(layout.rooms.size()) > layout.config.roomsMax)
        {
            error = "room count out of range";
            return false;
        }
        if (!layout.GetRoom(layout.entranceRoomId) || !layout.GetRoom(layout.bossRoomId) ||
            layout.entranceRoomId == layout.bossRoomId)
        {
            error = "invalid entrance/boss rooms";
            return false;
        }

        for (Room const& room : layout.rooms)
        {
            if (room.x < 1 || room.y < 1 || room.x + room.w >= layout.width - 1 || room.y + room.h >= layout.height - 1)
            {
                error = "room out of bounds";
                return false;
            }
            if (!layout.IsWalkable(room.CenterX(), room.CenterY()))
            {
                error = "room center not walkable";
                return false;
            }
        }

        for (Doorway const& doorway : layout.doorways)
        {
            if (doorway.tiles.empty())
            {
                error = "doorway without tiles";
                return false;
            }
            for (TilePos const& tile : doorway.tiles)
            {
                if (layout.At(tile.x, tile.y) != TileType::Doorway)
                {
                    error = "doorway tile has wrong tile type";
                    return false;
                }
            }
            if (!layout.GetRoom(doorway.roomId))
            {
                error = "doorway without room";
                return false;
            }
        }

        for (SpawnPoint const& spawn : layout.spawnPoints)
        {
            if (!layout.IsWalkable(spawn.x, spawn.y))
            {
                error = "spawn point not walkable";
                return false;
            }
        }

        Room const* entrance = layout.GetRoom(layout.entranceRoomId);
        Room const* boss = layout.GetRoom(layout.bossRoomId);
        std::vector<PDGridPath::Point> path;
        if (!PDGridPath::FindPath(layout, { entrance->CenterX(), entrance->CenterY() }, { boss->CenterX(), boss->CenterY() }, path))
        {
            error = "no A* path from entrance to boss";
            return false;
        }
        PDGridPath::SimplifyPath(layout, path);
        if (path.size() < 2)
        {
            error = "simplified path degenerated";
            return false;
        }
        return true;
    }

    int RunSingle(uint32_t seed, bool overlayPath)
    {
        GenConfig config;
        config.seed = seed;

        auto const start = std::chrono::steady_clock::now();
        PDLayout layout;
        if (!PDDungeonGenerator::Generate(config, layout))
        {
            std::printf("FAILED: no layout for seed %u\n", seed);
            return 1;
        }
        auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

        std::string dump = layout.AsciiDump();
        if (overlayPath)
        {
            Room const* entrance = layout.GetRoom(layout.entranceRoomId);
            Room const* boss = layout.GetRoom(layout.bossRoomId);
            std::vector<PDGridPath::Point> path;
            if (PDGridPath::FindPath(layout, { entrance->CenterX(), entrance->CenterY() }, { boss->CenterX(), boss->CenterY() }, path))
            {
                PDGridPath::SimplifyPath(layout, path);
                for (PDGridPath::Point const& point : path)
                {
                    size_t const index = static_cast<size_t>(point.y) * (layout.width + 1) + point.x;
                    if (index < dump.size() && dump[index] != '@')
                    {
                        dump[index] = '*';
                    }
                }
                std::printf("A* waypoints (simplified): %zu\n", path.size());
            }
        }

        std::fputs(dump.c_str(), stdout);
        PrintStats(layout, elapsed);

        std::string error;
        if (!CheckLayout(layout, error))
        {
            std::printf("INVARIANT FAILED: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    int RunBatch(int count, uint32_t startSeed)
    {
        int failures = 0;
        long long totalMicroseconds = 0;
        int minRooms = 1 << 30;
        int maxRooms = 0;
        int maxEstimatedGos = 0;
        long long totalEstimatedGos = 0;

        for (int i = 0; i < count; ++i)
        {
            GenConfig config;
            config.seed = startSeed + static_cast<uint32_t>(i);

            auto const start = std::chrono::steady_clock::now();
            PDLayout layout;
            bool const ok = PDDungeonGenerator::Generate(config, layout);
            totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();

            if (!ok)
            {
                std::printf("seed %u: generation FAILED\n", config.seed);
                ++failures;
                continue;
            }

            std::string error;
            if (!CheckLayout(layout, error))
            {
                std::printf("seed %u: invariant FAILED: %s\n", config.seed, error.c_str());
                ++failures;
                continue;
            }

            // Determinism: every 50th seed is generated twice and compared.
            if (i % 50 == 0)
            {
                PDLayout second;
                if (!PDDungeonGenerator::Generate(config, second) || second.AsciiDump() != layout.AsciiDump())
                {
                    std::printf("seed %u: determinism FAILED\n", config.seed);
                    ++failures;
                    continue;
                }
            }

            int const rooms = static_cast<int>(layout.rooms.size());
            minRooms = std::min(minRooms, rooms);
            maxRooms = std::max(maxRooms, rooms);
            int const estimated = EstimateGameObjects(layout);
            maxEstimatedGos = std::max(maxEstimatedGos, estimated);
            totalEstimatedGos += estimated;
        }

        std::printf("batch: %d seeds starting at %u, failures=%d\n", count, startSeed, failures);
        if (failures < count)
        {
            std::printf("rooms min/max=%d/%d avgEstimatedGameObjects=%lld maxEstimatedGameObjects=%d avgGenTime=%lldus\n",
                        minRooms, maxRooms, totalEstimatedGos / std::max(1, count - failures), maxEstimatedGos,
                        totalMicroseconds / std::max(1, count));
        }
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: %s <seed> [--path] | %s --batch <count> [startSeed]\n", argv[0], argv[0]);
        return 2;
    }

    if (std::strcmp(argv[1], "--batch") == 0)
    {
        if (argc < 3)
        {
            std::printf("--batch needs a count\n");
            return 2;
        }
        int const count = std::atoi(argv[2]);
        uint32_t const startSeed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 1u;
        return RunBatch(count, startSeed);
    }

    uint32_t const seed = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    bool const overlayPath = argc > 2 && std::strcmp(argv[2], "--path") == 0;
    return RunSingle(seed, overlayPath);
}
