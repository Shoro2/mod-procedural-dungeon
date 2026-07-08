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
#include "generator/PDWallPlan.h"
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

    // Verifies the one-gate-per-doorway contract and the room-clear gating: that
    // starting from the (auto-cleared) entrance, clearing reachable rooms in some
    // order opens a path to every room including the boss - i.e. the gating can
    // never softlock. Mirrors PDInstanceScript's adjacency + boss-gate rules on
    // the engine-free layout, so this is the same logic the live run uses.
    bool CheckGating(PDLayout const& layout, std::string& error)
    {
        size_t const doorCount = layout.doorways.size();
        std::vector<std::vector<int>> const incident = layout.DoorwayIncidentRooms();
        if (incident.size() != doorCount)
        {
            error = "doorway incidence size mismatch";
            return false;
        }

        // One gate per doorway: exactly one representative center per doorway,
        // in-bounds, and the doorway's own room is incident to its corridor blob.
        for (size_t i = 0; i < doorCount; ++i)
        {
            Doorway const& doorway = layout.doorways[i];
            if (doorway.tiles.empty())
            {
                error = "doorway without tiles";
                return false;
            }
            float cx = 0.0f;
            float cy = 0.0f;
            doorway.Center(cx, cy);
            if (!layout.InBounds(static_cast<int>(cx), static_cast<int>(cy)))
            {
                error = "gate center out of bounds";
                return false;
            }
            bool ownRoomIncident = false;
            for (int roomId : incident[i])
            {
                if (roomId == doorway.roomId)
                {
                    ownRoomIncident = true;
                }
            }
            if (!ownRoomIncident)
            {
                error = "doorway room not incident to its own corridor blob";
                return false;
            }
        }

        int const roomCount = static_cast<int>(layout.rooms.size());
        if (layout.entranceRoomId < 0 || layout.entranceRoomId >= roomCount ||
            layout.bossRoomId < 0 || layout.bossRoomId >= roomCount)
        {
            error = "invalid entrance/boss room id for gating";
            return false;
        }

        // Mob-less rooms are auto-cleared at setup (entrance + no-spawn rooms).
        std::vector<bool> hasMobs(roomCount, false);
        for (SpawnPoint const& spawn : layout.spawnPoints)
        {
            if (spawn.roomId >= 0 && spawn.roomId < roomCount)
            {
                hasMobs[spawn.roomId] = true;
            }
        }

        // A doorway is a boss door if any tile touches boss room floor.
        int const offsetsX[4] = { -1, 1, 0, 0 };
        int const offsetsY[4] = { 0, 0, -1, 1 };
        std::vector<bool> isBossDoor(doorCount, false);
        for (size_t i = 0; i < doorCount; ++i)
        {
            for (TilePos const& tile : layout.doorways[i].tiles)
            {
                for (int n = 0; n < 4; ++n)
                {
                    int const nx = tile.x + offsetsX[n];
                    int const ny = tile.y + offsetsY[n];
                    if (layout.At(nx, ny) == TileType::RoomFloor && layout.RoomIdAt(nx, ny) == layout.bossRoomId)
                    {
                        isBossDoor[i] = true;
                    }
                }
            }
        }

        // Fixpoint: a reachable room is immediately clearable; clearing a room
        // opens every gate whose blob touches it, letting the far room be reached.
        // The boss door additionally needs every mob-bearing elite room cleared.
        std::vector<bool> cleared(roomCount, false);
        cleared[layout.entranceRoomId] = true;
        for (int r = 0; r < roomCount; ++r)
        {
            if (!hasMobs[r])
            {
                cleared[r] = true;
            }
        }

        bool changed = true;
        while (changed)
        {
            changed = false;
            bool allElitesCleared = true;
            for (Room const& room : layout.rooms)
            {
                if (room.kind == RoomKind::Elite && hasMobs[room.id] && !cleared[room.id])
                {
                    allElitesCleared = false;
                }
            }
            for (size_t i = 0; i < doorCount; ++i)
            {
                bool gateOpen = false;
                for (int roomId : incident[i])
                {
                    if (cleared[roomId])
                    {
                        gateOpen = true;
                        break;
                    }
                }
                if (!gateOpen)
                {
                    continue;
                }
                if (isBossDoor[i] && !allElitesCleared)
                {
                    continue; // boss gate still shut
                }
                for (int roomId : incident[i])
                {
                    if (!cleared[roomId])
                    {
                        cleared[roomId] = true;
                        changed = true;
                    }
                }
            }
        }

        for (int r = 0; r < roomCount; ++r)
        {
            if (!cleared[r])
            {
                error = (r == layout.bossRoomId) ? "boss room unreachable under gating"
                                                 : "room unreachable under gating";
                return false;
            }
        }
        return true;
    }

    // Verifies the wall/gate geometry plan (PDWallPlan) that the world-builder
    // feeds to the palette. This is the engine-free half of the placement
    // rework, so the harness exercises the exact deterministic logic the live
    // run uses. Four properties:
    //   (a) no wall piece is oriented perpendicular to its own wall line;
    //   (b) every wall run end that abuts a perpendicular wall buries into a
    //       real wall tile (junctions/corners are covered, no taper gap);
    //   (c) gates are emitted only at genuine wall-capped passages;
    //   (d) the whole plan is deterministic (recomputed identical).
    bool CheckWallGatePlan(PDLayout const& layout, std::string& error)
    {
        int const W = layout.width;
        int const H = layout.height;
        auto isWall = [&](int x, int y) { return layout.At(x, y) == TileType::Wall; };

        std::vector<WallRun> const runs = BuildWallRuns(layout);

        // (d) determinism: an identical recompute must yield an identical plan.
        std::vector<WallRun> const again = BuildWallRuns(layout);
        if (again.size() != runs.size())
        {
            error = "wall run plan not deterministic (count)";
            return false;
        }
        for (size_t i = 0; i < runs.size(); ++i)
        {
            if (again[i].startX != runs[i].startX || again[i].startY != runs[i].startY ||
                again[i].length != runs[i].length || again[i].horizontal != runs[i].horizontal ||
                again[i].abutLow != runs[i].abutLow || again[i].abutHigh != runs[i].abutHigh)
            {
                error = "wall run plan not deterministic (run)";
                return false;
            }
        }

        // Body coverage: every Wall tile is the body tile of exactly one run.
        std::vector<int> bodyRun(static_cast<size_t>(W) * H, -1);
        std::vector<bool> bodyHoriz(static_cast<size_t>(W) * H, false);
        for (size_t r = 0; r < runs.size(); ++r)
        {
            WallRun const& run = runs[r];
            for (int i = 0; i < run.length; ++i)
            {
                int const x = run.horizontal ? run.startX + i : run.startX;
                int const y = run.horizontal ? run.startY : run.startY + i;
                if (!layout.InBounds(x, y) || !isWall(x, y))
                {
                    error = "wall run body tile is not a Wall tile";
                    return false;
                }
                size_t const idx = static_cast<size_t>(y) * W + x;
                if (bodyRun[idx] != -1)
                {
                    error = "wall tile covered by two run bodies";
                    return false;
                }
                bodyRun[idx] = static_cast<int>(r);
                bodyHoriz[idx] = run.horizontal;
            }
        }

        // (a) every Wall tile is covered, and by a run along its OWN wall line:
        // a straight-through vertical tile is never placed by a horizontal run
        // (and vice versa). This is the anti-"verdrehte Waende" invariant.
        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                if (!isWall(x, y))
                {
                    continue;
                }
                size_t const idx = static_cast<size_t>(y) * W + x;
                if (bodyRun[idx] == -1)
                {
                    error = "wall tile not covered by any run";
                    return false;
                }
                WallAxis const a = WallAxisOf(layout, x, y);
                bool const expectHoriz = (a == WallAxis::Horizontal || a == WallAxis::Isolated);
                if (bodyHoriz[idx] != expectHoriz)
                {
                    error = "wall piece perpendicular to its own wall line";
                    return false;
                }
            }
        }

        // (b) junction coverage: abut flags are exactly "wall beyond the end",
        // and every applied extension buries into a REAL wall tile that some run
        // owns (so the tapered bases actually meet - no walk-through gap).
        for (WallRun const& run : runs)
        {
            int const bLoX = run.horizontal ? run.startX - 1 : run.startX;
            int const bLoY = run.horizontal ? run.startY : run.startY - 1;
            int const bHiX = run.horizontal ? run.startX + run.length : run.startX;
            int const bHiY = run.horizontal ? run.startY : run.startY + run.length;
            if (run.abutLow != isWall(bLoX, bLoY) || run.abutHigh != isWall(bHiX, bHiY))
            {
                error = "wall run abut flag disagrees with the grid";
                return false;
            }
            if (run.abutLow && bodyRun[static_cast<size_t>(bLoY) * W + bLoX] == -1)
            {
                error = "junction extension does not bury into a covered wall tile";
                return false;
            }
            if (run.abutHigh && bodyRun[static_cast<size_t>(bHiY) * W + bHiX] == -1)
            {
                error = "junction extension does not bury into a covered wall tile";
                return false;
            }
        }

        // (c) gates only at genuine passages. Re-derive the predicate the finder
        // must satisfy for every opening it accepts, and verify determinism.
        auto isRoom = [&](int x, int y) { return layout.At(x, y) == TileType::RoomFloor; };
        auto isPath = [&](int x, int y) { TileType t = layout.At(x, y); return t == TileType::Corridor || t == TileType::Doorway; };
        auto isBlocked = [&](int x, int y) { TileType t = layout.At(x, y); return t == TileType::Wall || t == TileType::Void; };
        auto isDoor = [&](int x, int y) { return layout.At(x, y) == TileType::Doorway; };
        for (Doorway const& doorway : layout.doorways)
        {
            GateOpening const op = FindGateOpening(layout, doorway);
            GateOpening const op2 = FindGateOpening(layout, doorway);
            if (op.valid != op2.valid || op.spanAlongX != op2.spanAlongX || op.spanTiles != op2.spanTiles ||
                op.anchorX != op2.anchorX || op.anchorY != op2.anchorY)
            {
                error = "gate opening not deterministic";
                return false;
            }
            if (!op.valid)
            {
                continue; // stray/edge-hugging doorway correctly suppressed
            }

            int const cx = static_cast<int>(op.centerTileX);
            int const cy = static_cast<int>(op.centerTileY);
            if (!layout.InBounds(cx, cy) || op.spanTiles < 1)
            {
                error = "gate opening out of bounds / empty span";
                return false;
            }

            int const spanDX = op.spanAlongX ? 1 : 0;
            int const spanDY = op.spanAlongX ? 0 : 1;
            int const loX = op.anchorX;
            int const loY = op.anchorY;
            int const hiX = loX + (op.spanTiles - 1) * spanDX;
            int const hiY = loY + (op.spanTiles - 1) * spanDY;

            // Wall-capped at both span ends (rejects a corridor hugging an edge).
            if (!isBlocked(loX - spanDX, loY - spanDY) || !isBlocked(hiX + spanDX, hiY + spanDY))
            {
                error = "gate opening not capped by wall at both ends";
                return false;
            }
            // Every opening tile is a Doorway that pierces the room wall ring:
            // RoomFloor on one through-side, a corridor on the exact opposite.
            int const thruDX = op.spanAlongX ? 0 : 1;
            int const thruDY = op.spanAlongX ? 1 : 0;
            for (int i = 0; i < op.spanTiles; ++i)
            {
                int const tx = loX + i * spanDX;
                int const ty = loY + i * spanDY;
                if (!isDoor(tx, ty))
                {
                    error = "gate opening tile is not a Doorway";
                    return false;
                }
                bool const through =
                    (isRoom(tx + thruDX, ty + thruDY) && isPath(tx - thruDX, ty - thruDY)) ||
                    (isRoom(tx - thruDX, ty - thruDY) && isPath(tx + thruDX, ty + thruDY));
                if (!through)
                {
                    error = "gate opening tile does not connect a corridor to a room";
                    return false;
                }
            }
        }

        return true;
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

        if (!CheckGating(layout, error))
        {
            return false;
        }
        if (!CheckWallGatePlan(layout, error))
        {
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
