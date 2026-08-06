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

#ifndef MOD_PDUNGEON_V2_WALK_GRID_H
#define MOD_PDUNGEON_V2_WALK_GRID_H

#include "PDBlockPlan.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The walkable surface of a PDv2 layout, and A* over it.
//
// Why this exists at all: map 760 has no mmaps, and it never will - the terrain
// is composed by the client at run time, so there is nothing offline for the
// navmesh generator to chew on. A creature that wants to chase a player has
// nothing to path over. This grid is the substitute, and it is the SAME surface
// the client renders because both come from the kit: the client from the block
// ADTs, this from the walk masks the kit generator emits alongside them.
//
// Engine-free, like everything else under src/generator/, so a layout's
// pathing can be checked without a worldserver.
namespace PDungeon
{
    // Cells per block side, and therefore the resolution of everything here.
    // Fixed by the kit (48_gen_t1_blockkit.py, WALK_CELLS_PER_BLOCK).
    constexpr int PD_CELLS_PER_BLOCK = 8;

    // Hands out a block variant's 8x8 walk mask, row-major, row 0 = north edge,
    // col 0 = west edge. Returns nullptr for a chunk it does not know, which
    // BuildWalkGrid treats as an error rather than as empty space - a silently
    // unwalkable room is worse than a refusal.
    using WalkMaskProvider = std::function<uint8_t const*(int chunkId)>;

    struct WalkGrid
    {
        int originBX = 0;              // block coordinate of cell column 0
        int originBY = 0;              // block coordinate of cell row 0
        int width = 0;                 // in cells
        int height = 0;
        std::vector<uint8_t> cells;    // 1 = walkable, row-major

        bool InBounds(int cx, int cy) const
        {
            return cx >= 0 && cy >= 0 && cx < width && cy < height;
        }

        bool At(int cx, int cy) const
        {
            return InBounds(cx, cy) &&
                   cells[static_cast<size_t>(cy) * width + cx] != 0;
        }

        size_t WalkableCount() const;
    };

    struct GridPoint
    {
        int x = 0;
        int y = 0;

        bool operator==(GridPoint const& o) const { return x == o.x && y == o.y; }
    };

    // Lays every placed block's mask into one grid spanning the plan's bounding
    // box. Fails when a chunk's mask is unavailable.
    bool BuildWalkGrid(BlockPlan const& plan, WalkMaskProvider const& maskFor,
                       WalkGrid* out, std::string* error);

    // 4-neighbour A* over walkable cells; the returned path includes both ends.
    // 4 rather than 8 on purpose: a diagonal step between two blocked cells
    // would cut a corner the terrain does not actually have.
    bool FindGridPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                      std::vector<GridPoint>& outPath);

    // Collapses a cell path to as few waypoints as possible while keeping every
    // remaining segment on walkable cells, so a creature walks in straight lines
    // instead of stepping around a staircase of 8 yd cells.
    void SimplifyGridPath(WalkGrid const& grid, std::vector<GridPoint>& path);

    // Nearest walkable cell to (cx, cy) within `radius`, for snapping a position
    // that landed just off the grid. Returns false when nothing is near.
    bool NearestWalkable(WalkGrid const& grid, int cx, int cy, int radius,
                         GridPoint& out);
}

#endif
