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

#include "PDv2WalkGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace PDungeon
{
    size_t WalkGrid::WalkableCount() const
    {
        size_t n = 0;
        for (uint8_t c : cells)
        {
            if (c)
            {
                ++n;
            }
        }
        return n;
    }

    bool BuildWalkGrid(BlockPlan const& plan, WalkMaskProvider const& maskFor,
                       WalkGrid* out, std::string* error,
                       PatrolLayerProvider const& patrolFor)
    {
        auto fail = [error](std::string const& why) {
            if (error) *error = why;
            return false;
        };

        if (!out) return fail("no output");
        if (plan.blocks.empty()) return fail("plan has no blocks");
        if (!maskFor) return fail("no walk-mask provider");

        int minX = plan.blocks[0].bx, maxX = minX;
        int minY = plan.blocks[0].by, maxY = minY;
        for (PlacedBlock const& b : plan.blocks)
        {
            minX = std::min(minX, b.bx);
            maxX = std::max(maxX, b.bx);
            minY = std::min(minY, b.by);
            maxY = std::max(maxY, b.by);
        }

        out->originBX = minX;
        out->originBY = minY;
        out->width = (maxX - minX + 1) * PD_CELLS_PER_BLOCK;
        out->height = (maxY - minY + 1) * PD_CELLS_PER_BLOCK;
        size_t const total = static_cast<size_t>(out->width) * out->height;
        out->cells.assign(total, 0);
        // Round D / D2. Sized with `cells` and zeroed, so a cell no block ever
        // writes - and every non-walkable cell - reads 0/0/0, which is what the
        // kit publishes for one and what nothing ever enters anyway.
        out->patrolClear.assign(total, 0);
        out->patrolDu.assign(total, 0);
        out->patrolDv.assign(total, 0);

        for (PlacedBlock const& b : plan.blocks)
        {
            uint8_t const* mask = maskFor(b.chunkId);
            if (!mask)
            {
                return fail("no walk mask for chunk " + std::to_string(b.chunkId) +
                            " - the kit metadata is missing or out of date");
            }
            // ONE lookup per block, in the same loop as the mask: the clearance
            // of a cell and the fact that it is walkable have to come from the
            // same chunk record or they describe different terrain.
            PatrolLayers const layers = patrolFor ? patrolFor(b.chunkId) : PatrolLayers{};

            int const baseX = (b.bx - minX) * PD_CELLS_PER_BLOCK;
            int const baseY = (b.by - minY) * PD_CELLS_PER_BLOCK;
            for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
            {
                for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
                {
                    int const cell = row * PD_CELLS_PER_BLOCK + col;
                    if (!mask[cell])
                    {
                        continue;
                    }
                    // Blocks never overlap - the planner rejects duplicate
                    // coordinates - so a plain write cannot lose anything.
                    size_t const at = static_cast<size_t>(baseY + row) * out->width +
                                      static_cast<size_t>(baseX + col);
                    out->cells[at] = 1;
                    // Clamped rather than trusted. The bytes come off a
                    // database column a kit regeneration rewrites, and the two
                    // failure modes a wrong one would cause here are a patrol
                    // that refuses every cell (clear > 15 wrapping negative in
                    // the cost) and an offset that walks a waypoint out of its
                    // own cell - both silent, both hard to see in game.
                    out->patrolClear[at] =
                        layers.clear ? std::min<uint8_t>(layers.clear[cell], PD_PATROL_CLEAR_FREE)
                                     : PD_PATROL_CLEAR_FREE;
                    if (layers.du)
                    {
                        int const du = static_cast<int>(layers.du[cell]) - 32;
                        out->patrolDu[at] = static_cast<int8_t>(std::max(-32, std::min(32, du)));
                    }
                    if (layers.dv)
                    {
                        int const dv = static_cast<int>(layers.dv[cell]) - 32;
                        out->patrolDv[at] = static_cast<int8_t>(std::max(-32, std::min(32, dv)));
                    }
                }
            }
        }

        if (out->WalkableCount() == 0)
        {
            return fail("every cell came out unwalkable");
        }
        return true;
    }

    namespace
    {
        struct OpenNode
        {
            int  f = 0;
            int  idx = 0;

            // std::priority_queue is a MAX heap, so the comparison is inverted
            // to pop the cheapest node.
            bool operator<(OpenNode const& o) const { return f > o.f; }
        };

        int Manhattan(GridPoint a, GridPoint b)
        {
            return std::abs(a.x - b.x) + std::abs(a.y - b.y);
        }
    }

    bool FindGridPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                      std::vector<GridPoint>& outPath)
    {
        outPath.clear();
        if (!grid.At(from.x, from.y) || !grid.At(to.x, to.y))
        {
            return false;
        }
        if (from == to)
        {
            outPath.push_back(from);
            return true;
        }

        size_t const total = grid.cells.size();
        std::vector<int> gScore(total, -1);
        std::vector<int> cameFrom(total, -1);
        std::priority_queue<OpenNode> open;

        auto index = [&grid](int x, int y) {
            return static_cast<size_t>(y) * grid.width + static_cast<size_t>(x);
        };

        size_t const startIdx = index(from.x, from.y);
        size_t const goalIdx = index(to.x, to.y);
        gScore[startIdx] = 0;
        open.push({ Manhattan(from, to), static_cast<int>(startIdx) });

        // Fixed neighbour order, so the same two endpoints always yield the same
        // path. A creature that took a different route each pull would look like
        // a bug even though every route was valid.
        int const dx[4] = { 0, 1, 0, -1 };
        int const dy[4] = { -1, 0, 1, 0 };

        while (!open.empty())
        {
            OpenNode const cur = open.top();
            open.pop();
            size_t const curIdx = static_cast<size_t>(cur.idx);
            if (curIdx == goalIdx)
            {
                break;
            }

            int const cx = static_cast<int>(curIdx % grid.width);
            int const cy = static_cast<int>(curIdx / grid.width);

            for (int d = 0; d < 4; ++d)
            {
                int const nx = cx + dx[d];
                int const ny = cy + dy[d];
                if (!grid.At(nx, ny))
                {
                    continue;
                }
                size_t const nIdx = index(nx, ny);
                int const tentative = gScore[curIdx] + 1;
                if (gScore[nIdx] >= 0 && gScore[nIdx] <= tentative)
                {
                    continue;
                }
                gScore[nIdx] = tentative;
                cameFrom[nIdx] = static_cast<int>(curIdx);
                open.push({ tentative + Manhattan({ nx, ny }, to), static_cast<int>(nIdx) });
            }
        }

        if (gScore[goalIdx] < 0)
        {
            return false;
        }

        for (int at = static_cast<int>(goalIdx); at >= 0; at = cameFrom[static_cast<size_t>(at)])
        {
            outPath.push_back({ static_cast<int>(at % grid.width),
                                static_cast<int>(at / grid.width) });
            if (static_cast<size_t>(at) == startIdx)
            {
                break;
            }
        }
        std::reverse(outPath.begin(), outPath.end());
        return true;
    }

    // SUPERCOVER: every cell the segment between the two cell CENTRES enters
    // must be walkable. Exact integer DDA - the next x boundary is crossed at
    // t = (2 nx + 1) / (2 ax), the next y boundary at (2 ny + 1) / (2 ay), and
    // the two are compared cross-multiplied so a tie is exact. A tie is the
    // segment passing through a cell CORNER: then BOTH straddling cells must
    // be walkable (no corner cutting - a creature has a body). Round B's
    // version sampled one rounded cell per major-axis step and skipped a cell
    // at every minor-axis transition, which is how the patroller walked
    // through walls (Round C research A1). Public because it is also the
    // creature AI's chase gate (see the header).
    //
    // `sx` / `sy` are -1 on a ZERO delta (the ternaries below ask `>`, not
    // `>=`), which is harmless only because that axis never steps: every read
    // of `sx` sits behind `nx < ax` and every read of `sy` behind `ny < ay`.
    // Move a `grid.At(x + sx, ...)` out of those branches and the -1 becomes a
    // read one cell off the line.
    bool GridLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b)
    {
        if (!grid.At(a.x, a.y))
        {
            return false;
        }
        int const ax = std::abs(b.x - a.x);
        int const ay = std::abs(b.y - a.y);
        int const sx = b.x > a.x ? 1 : -1;
        int const sy = b.y > a.y ? 1 : -1;
        int x = a.x, y = a.y, nx = 0, ny = 0;
        while (nx < ax || ny < ay)
        {
            bool const stepX = nx < ax && (ny >= ay || (2LL * nx + 1) * ay < (2LL * ny + 1) * ax);
            bool const stepY = ny < ay && (nx >= ax || (2LL * ny + 1) * ax < (2LL * nx + 1) * ay);
            if (!stepX && !stepY)
            {
                // Exact corner crossing.
                if (!grid.At(x + sx, y) || !grid.At(x, y + sy))
                {
                    return false;
                }
                x += sx;
                y += sy;
                ++nx;
                ++ny;
            }
            else if (stepX)
            {
                x += sx;
                ++nx;
            }
            else
            {
                y += sy;
                ++ny;
            }
            if (!grid.At(x, y))
            {
                return false;
            }
        }
        return true;
    }

    void SimplifyGridPath(WalkGrid const& grid, std::vector<GridPoint>& path)
    {
        if (path.size() < 3)
        {
            return;
        }

        std::vector<GridPoint> out;
        out.push_back(path.front());
        size_t anchor = 0;
        while (anchor + 1 < path.size())
        {
            size_t best = anchor + 1;
            for (size_t probe = path.size() - 1; probe > anchor + 1; --probe)
            {
                if (GridLineWalkable(grid, path[anchor], path[probe]))
                {
                    best = probe;
                    break;
                }
            }
            out.push_back(path[best]);
            anchor = best;
        }
        path.swap(out);
    }

    namespace
    {
        // The incoming direction half of a patrol state. NONE belongs to the
        // start cell alone - it is what makes the first step free of a turn
        // charge - and the other four are ordered exactly like the neighbour
        // loop below, so `d + 1` names the direction a step d arrives from.
        enum PatrolDir
        {
            PATROL_DIR_NONE = 0,
            PATROL_DIR_N,
            PATROL_DIR_E,
            PATROL_DIR_S,
            PATROL_DIR_W
        };

        size_t const PATROL_DIRS = 5;

        struct PatrolNode
        {
            int f = 0;
            int g = 0;
            int state = 0;

            // std::priority_queue is a MAX heap, so both comparisons are
            // inverted: the cheapest f pops first and, among equal f, the
            // lowest state index. That second half is not decoration - it is a
            // TOTAL order over the queue, and without it the route would be
            // decided by the heap's internal shuffling of equal elements,
            // which is exactly the kind of thing that differs between MSVC and
            // libstdc++ and would break the determinism contract in CLAUDE.md.
            bool operator<(PatrolNode const& o) const
            {
                if (f != o.f)
                {
                    return f > o.f;
                }
                return state > o.state;
            }
        };

        // At() answers false for out of bounds as well, which is the intent
        // here: the edge of the grid IS a wall - there is no terrain past it
        // on map 760, only the void.
        bool PatrolWallAdjacent(WalkGrid const& grid, int x, int y)
        {
            return !grid.At(x, y - 1) || !grid.At(x + 1, y) ||
                   !grid.At(x, y + 1) || !grid.At(x - 1, y);
        }
    }

    bool FindPatrolPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                        std::vector<uint8_t> const* propCells,
                        std::vector<GridPoint>& outPath, PatrolCost const& cost)
    {
        outPath.clear();
        if (!grid.At(from.x, from.y) || !grid.At(to.x, to.y))
        {
            return false;
        }
        if (from == to)
        {
            outPath.push_back(from);
            return true;
        }

        // A prop mask of the wrong size is dropped rather than indexed: the
        // alternative is reading past the end of the caller's vector, and a
        // patrol that ignores props is a cosmetic fault while that is a crash.
        if (propCells && propCells->size() != grid.cells.size())
        {
            propCells = nullptr;
        }

        // Round D / D2, read under the same guard and for the same reason. Null
        // here means "this grid has no clearance layer", which is a grid built
        // by hand or from a kit that predates D2: no cell is tight, no cell is
        // blocked, and the search is exactly D1's.
        std::vector<uint8_t> const* clearCells =
            grid.patrolClear.size() == grid.cells.size() ? &grid.patrolClear : nullptr;

        std::vector<int> gScore(grid.cells.size() * PATROL_DIRS, -1);
        std::vector<int> cameFrom(grid.cells.size() * PATROL_DIRS, -1);
        std::priority_queue<PatrolNode> open;

        auto index = [&grid](int x, int y) {
            return static_cast<size_t>(y) * grid.width + static_cast<size_t>(x);
        };

        size_t const goalCell = index(to.x, to.y);
        size_t const startState = index(from.x, from.y) * PATROL_DIRS + PATROL_DIR_NONE;
        gScore[startState] = 0;
        open.push({ Manhattan(from, to) * cost.step, 0, static_cast<int>(startState) });

        // Fixed neighbour order, the same one FindGridPath uses, for the same
        // reason: two endpoints must always yield the same beat.
        int const dx[4] = { 0, 1, 0, -1 };
        int const dy[4] = { -1, 0, 1, 0 };

        int goalState = -1;
        while (!open.empty())
        {
            PatrolNode const cur = open.top();
            open.pop();
            size_t const curState = static_cast<size_t>(cur.state);
            if (cur.g > gScore[curState])
            {
                continue;   // a cheaper way to this state turned up after it was queued
            }

            size_t const curCell = curState / PATROL_DIRS;
            if (curCell == goalCell)
            {
                // The heuristic is Manhattan * step and every step costs at
                // least step, so it never overestimates and never drops by
                // more than one step's charge: the first state popped at the
                // goal cell is the cheapest one there, whichever direction it
                // arrived from.
                goalState = cur.state;
                break;
            }

            int const cx = static_cast<int>(curCell % grid.width);
            int const cy = static_cast<int>(curCell / grid.width);
            int const curDir = static_cast<int>(curState % PATROL_DIRS);

            for (int d = 0; d < 4; ++d)
            {
                int const nx = cx + dx[d];
                int const ny = cy + dy[d];
                if (!grid.At(nx, ny))
                {
                    continue;
                }

                size_t const nCell = index(nx, ny);
                int const dir = d + 1;   // PATROL_DIR_N..PATROL_DIR_W, in step order

                // THE HARD HALF, tested before anything is charged: a cell with
                // less than minClearQ quarter-yards of room is not a passage.
                // Only the cell being ENTERED is judged - the start is where
                // the creature already stands - which is also why a tight GOAL
                // makes the whole search fail and the caller re-runs it with
                // minClearQ 0.
                int const nClear = clearCells ? static_cast<int>((*clearCells)[nCell])
                                              : static_cast<int>(PD_PATROL_CLEAR_FREE);
                if (clearCells && nClear < cost.minClearQ)
                {
                    continue;
                }

                int add = cost.step;
                if (curDir != PATROL_DIR_NONE && dir != curDir)
                {
                    add += cost.turn;
                }
                if (PatrolWallAdjacent(grid, nx, ny))
                {
                    add += cost.wallAdjacent;
                }
                if (propCells && (*propCells)[nCell] != 0)
                {
                    add += cost.propCell;
                }
                // The soft half. A free cell (15) adds nothing, so a theme
                // without facades - and every grid without the layer - keeps
                // D1's numbers exactly.
                if (nClear < static_cast<int>(PD_PATROL_CLEAR_FREE))
                {
                    add += cost.tightPerQuarter *
                           (static_cast<int>(PD_PATROL_CLEAR_FREE) - nClear);
                }

                size_t const nState = nCell * PATROL_DIRS + static_cast<size_t>(dir);
                int const tentative = cur.g + add;
                if (gScore[nState] >= 0 && gScore[nState] <= tentative)
                {
                    continue;
                }
                gScore[nState] = tentative;
                cameFrom[nState] = cur.state;
                open.push({ tentative + Manhattan({ nx, ny }, to) * cost.step,
                            tentative, static_cast<int>(nState) });
            }
        }

        if (goalState < 0)
        {
            return false;
        }

        for (int at = goalState; at >= 0; at = cameFrom[static_cast<size_t>(at)])
        {
            size_t const cell = static_cast<size_t>(at) / PATROL_DIRS;
            outPath.push_back({ static_cast<int>(cell % grid.width),
                                static_cast<int>(cell / grid.width) });
            if (static_cast<size_t>(at) == startState)
            {
                break;
            }
        }
        std::reverse(outPath.begin(), outPath.end());
        return true;
    }

    void MergeCollinear(std::vector<GridPoint>& path)
    {
        if (path.size() < 3)
        {
            return;
        }

        std::vector<GridPoint> out;
        out.push_back(path.front());
        for (size_t i = 1; i + 1 < path.size(); ++i)
        {
            int const inX = path[i].x - path[i - 1].x;
            int const inY = path[i].y - path[i - 1].y;
            int const outX = path[i + 1].x - path[i].x;
            int const outY = path[i + 1].y - path[i].y;
            // Same heading: the cross product vanishes AND the dot product is
            // positive. Written out rather than compared step for step so the
            // function is idempotent - running it over an already merged list
            // is a no-op - and so a beat that DOUBLES BACK on itself keeps its
            // turning point, which a bare "same axis" test would swallow.
            bool const straightOn =
                inX * outY - inY * outX == 0 && inX * outX + inY * outY > 0;
            if (!straightOn)
            {
                out.push_back(path[i]);
            }
        }
        out.push_back(path.back());
        path.swap(out);
    }

    namespace
    {
        // The unit the merge below measures in: 1/12 of a yard, chosen because
        // it makes BOTH lengths that matter exact integers - a cell is 25/3 yd
        // = 100 units and the clearance layer's quarter-yard is 3. The double
        // PD_CELL_SIZE_YD is deliberately NOT used: this test decides how many
        // waypoints a beat has, and an integer answer is the same answer on
        // every compiler (CLAUDE.md's determinism rule).
        long long const PATROL_UNITS_PER_CELL = 100;
        long long const PATROL_UNITS_PER_QUARTER = 3;

        struct PatrolClearPoint
        {
            long long x = 0;
            long long y = 0;
        };

        // A cell's clear point in the GRID's own frame, which is all this
        // needs: the world mapping (BlockLocalToWorld, u against -X and v
        // against -Y) is a 180 degree rotation of it, and a rotation moves no
        // distance - a perpendicular distance measured here is the one the
        // creature walks. grid.x is the v axis and grid.y the u axis, the same
        // pairing BuildWalkGrid lays the mask down with.
        PatrolClearPoint PatrolClearPointOf(WalkGrid const& grid, GridPoint cell)
        {
            PatrolCellInfo const info = PatrolInfoAt(grid, cell);
            PatrolClearPoint p;
            p.x = static_cast<long long>(cell.x) * PATROL_UNITS_PER_CELL +
                  PATROL_UNITS_PER_CELL / 2 +
                  static_cast<long long>(info.dv) * PATROL_UNITS_PER_QUARTER;
            p.y = static_cast<long long>(cell.y) * PATROL_UNITS_PER_CELL +
                  PATROL_UNITS_PER_CELL / 2 +
                  static_cast<long long>(info.du) * PATROL_UNITS_PER_QUARTER;
            return p;
        }
    }

    void MergeClearPoints(WalkGrid const& grid, std::vector<GridPoint>& path,
                          int toleranceQ)
    {
        if (path.size() < 3)
        {
            return;
        }
        if (toleranceQ < 0)
        {
            // A negative tolerance is "keep every cell", not a merge that runs
            // backwards: nothing can be within a negative distance of a line.
            toleranceQ = 0;
        }

        long long const tol = static_cast<long long>(toleranceQ) * PATROL_UNITS_PER_QUARTER;

        // Would ONE leg from path[a] to path[b] still describe every clear
        // point between them? |cross| / |ab| is a point's perpendicular
        // distance from the line, so the question is |cross| <= tol * |ab| -
        // asked squared, which keeps it in integers and needs no square root.
        // Magnitudes are small (a run is at most a few hundred cells and a
        // clear point sits inside its own cell), so the squares stay far below
        // what a 64-bit integer holds.
        auto describesRun = [&grid, tol](std::vector<GridPoint> const& p, size_t a, size_t b)
        {
            PatrolClearPoint const from = PatrolClearPointOf(grid, p[a]);
            PatrolClearPoint const to = PatrolClearPointOf(grid, p[b]);
            long long const abx = to.x - from.x;
            long long const aby = to.y - from.y;
            long long const len2 = abx * abx + aby * aby;
            if (len2 == 0)
            {
                // Two clear points on top of each other - there is no line to
                // measure against, so keep both rather than merge blind.
                return false;
            }
            for (size_t i = a + 1; i < b; ++i)
            {
                PatrolClearPoint const mid = PatrolClearPointOf(grid, p[i]);
                long long const cross = abx * (mid.y - from.y) - aby * (mid.x - from.x);
                if (cross * cross > tol * tol * len2)
                {
                    return false;
                }
            }
            return true;
        };

        std::vector<GridPoint> out;
        out.push_back(path.front());
        // The index in `path` of the last waypoint KEPT, which is where the leg
        // under test starts. `path` is never written before the swap, so this
        // stays valid while `out` grows.
        size_t anchor = 0;
        for (size_t i = 1; i + 1 < path.size(); ++i)
        {
            int const inX = path[i].x - path[i - 1].x;
            int const inY = path[i].y - path[i - 1].y;
            int const outX = path[i + 1].x - path[i].x;
            int const outY = path[i + 1].y - path[i].y;
            // MergeCollinear's rule, unchanged and asked first: a turn is a
            // waypoint whatever the clearance layer says, which is what keeps
            // every leg axis-aligned.
            bool const straightOn =
                inX * outY - inY * outX == 0 && inX * outX + inY * outY > 0;
            // The second question, and the whole of this function: the leg the
            // drop would produce runs anchor -> i + 1, so it has to describe
            // every clear point in between - path[i]'s included, and every one
            // dropped before it. Checking the WHOLE run rather than the one
            // step is what stops a lane that drifts a quarter-yard per cell
            // from merging into a leg that ends a house-width off the passage.
            if (!straightOn || !describesRun(path, anchor, i + 1))
            {
                out.push_back(path[i]);
                anchor = i;
            }
        }
        out.push_back(path.back());
        path.swap(out);
    }

    PatrolCellInfo PatrolInfoAt(WalkGrid const& grid, GridPoint cell)
    {
        PatrolCellInfo info;
        // All three sizes, not only the one being read: the loader stores the
        // layer as ONE measurement or not at all, so a grid carrying two of the
        // three would answer an offset for a clearance nobody measured.
        // !InBounds covers the cell a caller derived from a route planned on a
        // grid that has since been rebuilt.
        if (grid.patrolClear.size() != grid.cells.size() ||
            grid.patrolDu.size() != grid.cells.size() ||
            grid.patrolDv.size() != grid.cells.size() ||
            !grid.InBounds(cell.x, cell.y))
        {
            return info;    // 15/0/0 - free, and standing on the cell centre
        }

        size_t const at = static_cast<size_t>(cell.y) * grid.width +
                          static_cast<size_t>(cell.x);
        info.clear = grid.patrolClear[at];
        info.du = grid.patrolDu[at];
        info.dv = grid.patrolDv[at];
        return info;
    }

    void PatrolPointToWorld(WalkGrid const& grid, GridPoint cell, double& x, double& y)
    {
        int gcx = 0, gcy = 0;
        grid.GlobalFromLocalCell(cell, gcx, gcy);
        CellCentreToWorld(gcx, gcy, x, y);

        PatrolCellInfo const info = PatrolInfoAt(grid, cell);
        // MINUS, and this is the whole of the frame mapping: BlockLocalToWorld
        // is x = MAX - (by * BLOCK + u) and y = MAX - (bx * BLOCK + v), so u
        // runs against world X and v against world Y. CellCentreToWorld is the
        // same identity at cell resolution (its gcy is the u axis, its gcx the
        // v axis), which is why the offsets attach to x and y in that order.
        x -= static_cast<double>(info.du) * PD_PATROL_QUARTER_YD;
        y -= static_cast<double>(info.dv) * PD_PATROL_QUARTER_YD;
    }

    bool NearestWalkable(WalkGrid const& grid, int cx, int cy, int radius, GridPoint& out)
    {
        if (grid.At(cx, cy))
        {
            out = { cx, cy };
            return true;
        }
        for (int r = 1; r <= radius; ++r)
        {
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dx = -r; dx <= r; ++dx)
                {
                    // Ring only: the interior was covered by a smaller r.
                    if (std::abs(dx) != r && std::abs(dy) != r)
                    {
                        continue;
                    }
                    if (grid.At(cx + dx, cy + dy))
                    {
                        out = { cx + dx, cy + dy };
                        return true;
                    }
                }
            }
        }
        return false;
    }

    ApproachKind PlanApproach(WalkGrid const& grid, GridPoint from, GridPoint to,
                              int snapRadius, std::vector<GridPoint>& waypoints)
    {
        waypoints.clear();

        GridPoint start, goal;
        if (!NearestWalkable(grid, from.x, from.y, snapRadius, start) ||
            !NearestWalkable(grid, to.x, to.y, snapRadius, goal))
        {
            // One end is nowhere near the walkable surface - a target mid-jump
            // over the void, or a position that is not on this layout at all.
            // Walking toward it would walk off the world, so: hold.
            return ApproachKind::Unreachable;
        }

        if (GridLineWalkable(grid, start, goal))
        {
            return ApproachKind::Direct;
        }

        std::vector<GridPoint> path;
        if (!FindGridPath(grid, start, goal, path))
        {
            return ApproachKind::Unreachable;
        }
        SimplifyGridPath(grid, path);
        waypoints.swap(path);
        return ApproachKind::Path;
    }

    bool DecodeWalkMaskRle(std::string const& text, std::vector<uint8_t>& out)
    {
        out.clear();
        if (text.compare(0, 5, "RLE1:") != 0)
        {
            return false;
        }
        size_t at = 5;
        while (at < text.size())
        {
            size_t const star = text.find('*', at);
            if (star == std::string::npos)
            {
                return false;
            }
            size_t comma = text.find(',', star);
            if (comma == std::string::npos)
            {
                comma = text.size();
            }
            int const value = std::atoi(text.substr(at, star - at).c_str());
            int const count = std::atoi(text.substr(star + 1, comma - star - 1).c_str());
            if (count <= 0)
            {
                return false;
            }
            out.insert(out.end(), static_cast<size_t>(count), static_cast<uint8_t>(value));
            at = comma + 1;
        }
        return true;
    }
}
