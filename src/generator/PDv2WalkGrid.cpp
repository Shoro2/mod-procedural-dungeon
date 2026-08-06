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
                       WalkGrid* out, std::string* error)
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
        out->cells.assign(static_cast<size_t>(out->width) * out->height, 0);

        for (PlacedBlock const& b : plan.blocks)
        {
            uint8_t const* mask = maskFor(b.chunkId);
            if (!mask)
            {
                return fail("no walk mask for chunk " + std::to_string(b.chunkId) +
                            " - the kit metadata is missing or out of date");
            }

            int const baseX = (b.bx - minX) * PD_CELLS_PER_BLOCK;
            int const baseY = (b.by - minY) * PD_CELLS_PER_BLOCK;
            for (int row = 0; row < PD_CELLS_PER_BLOCK; ++row)
            {
                for (int col = 0; col < PD_CELLS_PER_BLOCK; ++col)
                {
                    if (!mask[row * PD_CELLS_PER_BLOCK + col])
                    {
                        continue;
                    }
                    // Blocks never overlap - the planner rejects duplicate
                    // coordinates - so a plain write cannot lose anything.
                    size_t const at = static_cast<size_t>(baseY + row) * out->width +
                                      static_cast<size_t>(baseX + col);
                    out->cells[at] = 1;
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

    namespace
    {
        // Every cell a straight line between two cell centres passes through must
        // be walkable, or the shortcut would cut a corner.
        bool LineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b)
        {
            int const steps = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
            if (steps == 0)
            {
                return grid.At(a.x, a.y);
            }
            for (int i = 0; i <= steps; ++i)
            {
                // Rounded sampling: the midpoint of a step must be solid too,
                // otherwise a diagonal run could clip a corner between cells.
                double const t = static_cast<double>(i) / steps;
                int const x = static_cast<int>(std::lround(a.x + (b.x - a.x) * t));
                int const y = static_cast<int>(std::lround(a.y + (b.y - a.y) * t));
                if (!grid.At(x, y))
                {
                    return false;
                }
            }
            return true;
        }
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
                if (LineWalkable(grid, path[anchor], path[probe]))
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
}
