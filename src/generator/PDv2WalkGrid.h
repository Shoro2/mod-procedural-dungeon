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

    // ---- the patrol planner (Round D / D1) ----
    //
    // Why a SECOND planner beside FindGridPath: after Round C's supercover fix
    // the patroller stopped walking through walls, and the operator's next
    // report was that it walked "in einer geraden linie durch ecken von
    // haeusern und objekte hindurch" - straight legs that clip a house corner
    // and cross the module's own props. Neither of those is a wall on this
    // grid. A kit facade intrudes up to 6.1 yd into a corridor mouth while the
    // cell under it stays walkable, and a prop is a GameObject the walk mask
    // has never heard of. A guard would not hug the geometry anyway - it walks
    // down the middle of the lane and turns at the junction - so the answer is
    // not a finer collision test but a different COST:
    //
    //   step         charged for every cell entered
    //   turn         charged when the direction changes, so one long leg beats
    //                a staircase of the same Manhattan length; the first step
    //                is free, a patroller starts with no heading
    //   wallAdjacent charged when the entered cell has a blocked or
    //                out-of-bounds 4-neighbour, so the lane centre is cheaper
    //                than the wall band a facade leans over
    //   propCell     charged when a prop stands on the entered cell - a COST,
    //                not a wall, so a corridor a prop fills still has a route
    //
    // The chase keeps FindGridPath / SimplifyGridPath / PlanApproach: cutting
    // a corner to reach a player is fine, that is what the diagonal legs exist
    // for. This is for the beat a patrol walks when nothing is chasing anyone.
    struct PatrolCost
    {
        int step = 10;
        int turn = 30;
        int wallAdjacent = 20;
        int propCell = 60;
    };

    // A* over (cell, incoming direction), not over cells alone: a turn charge
    // is a property of HOW a cell was entered, so the direction has to be part
    // of the state - otherwise the cheapest arrival at a cell would be settled
    // once and then charged wrongly for everything that leaves it. Five
    // directions per cell (none, N, E, S, W); `none` belongs to the start and
    // is what makes its first step free.
    //
    // `propCells` may be null. When given it must be indexed exactly like
    // `grid.cells` (y * width + x) and be the same size, and a non-zero byte
    // means a prop stands on that cell; a vector of any other size is IGNORED
    // rather than read past its end.
    //
    // `outPath` comes back as the cell chain including both ends, the way
    // FindGridPath returns it: single 4-neighbour steps, so every consecutive
    // pair is axis-aligned by construction. False when either end is
    // unwalkable or nothing connects them - the caller must then hold, never
    // walk the straight line.
    //
    // Deterministic on every compiler, like everything else under
    // src/generator/: integer costs only, no floating point, fixed neighbour
    // order N/E/S/W and the open queue's ties broken by state index. A patrol
    // that took a different route each pull would look like a bug even though
    // every route was valid.
    bool FindPatrolPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                        std::vector<uint8_t> const* propCells,
                        std::vector<GridPoint>& outPath,
                        PatrolCost const& cost = PatrolCost{});

    // Drops every waypoint whose neighbours continue in the same direction:
    // the two endpoints and every turn survive, nothing else does. The patrol
    // counterpart of SimplifyGridPath, which merges as far as a walkable
    // straight line reaches and therefore produces diagonals. This one never
    // moves the route - it only says the same route in fewer points - so an
    // axis-aligned cell chain stays axis-aligned, which is the whole promise
    // D1 makes to the creature AI.
    void MergeCollinear(std::vector<GridPoint>& path);

    // Nearest walkable cell to (cx, cy) within `radius`, for snapping a position
    // that landed just off the grid. Returns false when nothing is near.
    bool NearestWalkable(WalkGrid const& grid, int cx, int cy, int radius,
                         GridPoint& out);

    // A supercover line test: true when EVERY cell the straight segment between
    // the two cell centres enters is walkable, and - where the segment passes
    // exactly through a cell corner - both cells it straddles are walkable too
    // (no corner cutting; a creature has a body). This is the chase gate:
    // engine line-of-sight cannot serve on map 760, because the server has no
    // VMAP and no terrain there - the engine sees a clear line straight across
    // the void between two platforms, and a creature sent down that line walks
    // off the world. Until Round C this was a Bresenham sampler that tested one
    // rounded cell per major-axis step and silently skipped a cell at every
    // minor-axis transition, which let patrols walk through walls.
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
