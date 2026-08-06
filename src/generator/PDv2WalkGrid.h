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
#include "PDv2WorldMath.h"

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
    // PD_CELLS_PER_BLOCK - the resolution of everything here - lives in
    // PDv2WorldMath.h next to the yard sizes it divides.

    // Hands out a block variant's 8x8 walk mask, row-major, row 0 = north edge,
    // col 0 = west edge. Returns nullptr for a chunk it does not know, which
    // BuildWalkGrid treats as an error rather than as empty space - a silently
    // unwalkable room is worse than a refusal.
    using WalkMaskProvider = std::function<uint8_t const*(int chunkId)>;

    struct GridPoint
    {
        int x = 0;
        int y = 0;

        bool operator==(GridPoint const& o) const { return x == o.x && y == o.y; }
    };

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

        // GLOBAL cell coordinate (PDv2WorldMath's WorldToCell frame) <-> this
        // grid's local cells. Local coordinates may fall outside the grid;
        // At() treats those as unwalkable, which is what every caller wants.
        GridPoint LocalFromGlobalCell(int gcx, int gcy) const
        {
            return { gcx - originBX * PD_CELLS_PER_BLOCK,
                     gcy - originBY * PD_CELLS_PER_BLOCK };
        }

        void GlobalFromLocalCell(GridPoint p, int& gcx, int& gcy) const
        {
            gcx = p.x + originBX * PD_CELLS_PER_BLOCK;
            gcy = p.y + originBY * PD_CELLS_PER_BLOCK;
        }

        size_t WalkableCount() const;
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

    // True when every cell under the straight line between two cell centres is
    // walkable. This is the chase gate: engine line-of-sight cannot serve on
    // map 760, because the server has no VMAP and no terrain there - the
    // engine sees a clear line straight across the void between two platforms,
    // and a creature sent down that line walks off the world.
    bool GridLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b);

    // What a creature at `from` should do about a target at `to`.
    enum class ApproachKind
    {
        Direct,      // the straight line is walkable - core chase movement is safe
        Path,        // follow `waypoints` with MovePoint(generatePath = false)
        Unreachable  // no walkable route within reach; hold position
    };

    // Both endpoints are snapped to the nearest walkable cell within
    // `snapRadius` first, because a live position rarely sits dead on a
    // walkable cell centre. `waypoints` is filled for Path only: the
    // simplified cell chain from the snapped start (index 0) to the snapped
    // goal, every consecutive pair joined by a walkable straight line.
    ApproachKind PlanApproach(WalkGrid const& grid, GridPoint from, GridPoint to,
                              int snapRadius, std::vector<GridPoint>& waypoints);

    // 'RLE1:<value>*<count>[,...]' -> bytes; the wire form of a walk mask in
    // `pdungeon_chunk_meta`. Shared by the harness and the server-side loader
    // so there is exactly one parser to get wrong.
    bool DecodeWalkMaskRle(std::string const& text, std::vector<uint8_t>& out);
}

#endif
