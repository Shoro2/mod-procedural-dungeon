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

#ifndef MOD_PDUNGEON_GEN_TYPES_H
#define MOD_PDUNGEON_GEN_TYPES_H

#include <cstdint>
#include <vector>

// Pure data types for the procedural dungeon generator.
// This header (and everything under src/generator/) must stay free of any
// engine include so it can be compiled standalone by tests/ascii_harness.cpp.
namespace PDungeon
{
    enum class TileType : uint8_t
    {
        Void = 0,
        RoomFloor,
        Corridor,
        Doorway,
        Wall
    };

    enum class RoomKind : uint8_t
    {
        Entrance = 0,
        Combat,
        Elite,
        Treasure,
        Shrine,
        Boss
    };

    enum class DecorKind : uint8_t
    {
        Torch = 0,
        Brazier,
        Chest,
        Shrine,
        ExitPortal,
        EntrancePad
    };

    enum class MobKind : uint8_t
    {
        Melee = 0,
        Caster,
        Elite,
        Boss
    };

    struct GenConfig
    {
        uint32_t seed = 0;
        int gridWidth = 64;
        int gridHeight = 64;
        int roomsMin = 10;
        int roomsMax = 16;
        int roomSizeMin = 5;        // tiles, forced odd
        int roomSizeMax = 11;       // tiles, forced odd
        int loopChancePct = 15;     // chance to re-add a non-MST edge
        int torchEvery = 4;         // every Nth eligible wall tile gets a torch
        int packSizeMin = 3;
        int packSizeMax = 5;
        int casterChancePct = 30;   // chance that a trash mob is a caster
        int maxGenerateTries = 10;  // seed+n retries before giving up
    };

    struct Room
    {
        int id = 0;
        int x = 0;                  // top-left tile
        int y = 0;
        int w = 0;
        int h = 0;
        RoomKind kind = RoomKind::Combat;
        int depth = 0;              // BFS depth from the entrance room

        int CenterX() const { return x + w / 2; }
        int CenterY() const { return y + h / 2; }
    };

    struct Edge
    {
        int a = 0;
        int b = 0;
        int weightSq = 0;           // squared distance between room centers
    };

    struct TilePos
    {
        int x = 0;
        int y = 0;
    };

    // A contiguous group of Doorway tiles where a corridor meets a room.
    // The whole group is one logical door: gate pieces spawned for it open
    // and close together. Corridors running along a room edge can produce
    // spans longer than the classic 2-tile doorway.
    struct Doorway
    {
        std::vector<TilePos> tiles; // sorted by (y, x); never empty
        bool spanAlongX = false;    // true: the opening extends along X
        int roomId = -1;            // room this doorway leads into

        int AnchorX() const { return tiles.empty() ? 0 : tiles[0].x; }
        int AnchorY() const { return tiles.empty() ? 0 : tiles[0].y; }
    };

    struct Decoration
    {
        DecorKind kind = DecorKind::Torch;
        int x = 0;
        int y = 0;
        int roomId = -1;            // -1 for corridor/wall decorations
        int facing = 0;             // 0..3, multiples of half pi (0 = +X)
    };

    struct SpawnPoint
    {
        MobKind kind = MobKind::Melee;
        int x = 0;
        int y = 0;
        int roomId = -1;
    };
}

#endif
