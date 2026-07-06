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

#include "PDGridPath.h"
#include <algorithm>
#include <cstdlib>
#include <queue>

namespace PDungeon
{
    namespace
    {
        struct OpenNode
        {
            int fScore = 0;
            int hScore = 0;
            int index = 0;

            // Inverted for min-heap; ties broken by heuristic, then index for
            // fully deterministic expansion order.
            bool operator<(OpenNode const& other) const
            {
                if (fScore != other.fScore)
                {
                    return fScore > other.fScore;
                }
                if (hScore != other.hScore)
                {
                    return hScore > other.hScore;
                }
                return index > other.index;
            }
        };

        int Manhattan(PDGridPath::Point a, PDGridPath::Point b)
        {
            return std::abs(a.x - b.x) + std::abs(a.y - b.y);
        }
    }

    bool PDGridPath::FindPath(PDLayout const& layout, Point from, Point to, std::vector<Point>& outPath, std::vector<Point> const* blockedTiles)
    {
        outPath.clear();
        if (!layout.IsWalkable(from.x, from.y) || !layout.IsWalkable(to.x, to.y))
        {
            return false;
        }

        size_t const cells = static_cast<size_t>(layout.width) * layout.height;
        std::vector<bool> blocked(cells, false);
        if (blockedTiles)
        {
            for (Point const& tile : *blockedTiles)
            {
                if (layout.InBounds(tile.x, tile.y))
                {
                    blocked[static_cast<size_t>(tile.y) * layout.width + tile.x] = true;
                }
            }
        }

        auto toIndex = [&](Point p)
        {
            return static_cast<size_t>(p.y) * layout.width + p.x;
        };

        std::vector<int> gScore(cells, -1);
        std::vector<int> cameFrom(cells, -1);
        std::priority_queue<OpenNode> open;

        gScore[toIndex(from)] = 0;
        open.push({ Manhattan(from, to), Manhattan(from, to), static_cast<int>(toIndex(from)) });

        int const offsetsX[4] = { 1, -1, 0, 0 };
        int const offsetsY[4] = { 0, 0, 1, -1 };
        bool found = false;
        while (!open.empty())
        {
            OpenNode const current = open.top();
            open.pop();

            Point const currentPoint = { current.index % layout.width, current.index / layout.width };
            int const currentG = gScore[static_cast<size_t>(current.index)];
            if (current.fScore > currentG + current.hScore)
            {
                continue; // stale queue entry
            }
            if (currentPoint == to)
            {
                found = true;
                break;
            }

            for (int n = 0; n < 4; ++n)
            {
                Point const next = { currentPoint.x + offsetsX[n], currentPoint.y + offsetsY[n] };
                if (!layout.IsWalkable(next.x, next.y))
                {
                    continue;
                }
                size_t const nextIndex = toIndex(next);
                if (blocked[nextIndex])
                {
                    continue;
                }
                int const tentative = currentG + 1;
                if (gScore[nextIndex] == -1 || tentative < gScore[nextIndex])
                {
                    gScore[nextIndex] = tentative;
                    cameFrom[nextIndex] = current.index;
                    open.push({ tentative + Manhattan(next, to), Manhattan(next, to), static_cast<int>(nextIndex) });
                }
            }
        }

        if (!found)
        {
            return false;
        }

        for (int walk = static_cast<int>(toIndex(to)); walk != -1; walk = cameFrom[static_cast<size_t>(walk)])
        {
            outPath.push_back({ walk % layout.width, walk / layout.width });
        }
        std::reverse(outPath.begin(), outPath.end());
        return true;
    }

    bool PDGridPath::LineWalkable(PDLayout const& layout, Point from, Point to)
    {
        // Conservative supercover traversal: every cell the segment passes
        // through (including corner-adjacent pairs) must be walkable.
        int x = from.x;
        int y = from.y;
        int const dx = std::abs(to.x - from.x);
        int const dy = std::abs(to.y - from.y);
        int const stepX = to.x > from.x ? 1 : -1;
        int const stepY = to.y > from.y ? 1 : -1;
        int error = dx - dy;

        while (true)
        {
            if (!layout.IsWalkable(x, y))
            {
                return false;
            }
            if (x == to.x && y == to.y)
            {
                return true;
            }

            int const doubledError = 2 * error;
            if (doubledError > -dy && doubledError < dx)
            {
                // Diagonal step: require both orthogonal companions walkable
                // so the segment cannot squeeze through a wall corner.
                if (!layout.IsWalkable(x + stepX, y) || !layout.IsWalkable(x, y + stepY))
                {
                    return false;
                }
                error += dx - dy;
                x += stepX;
                y += stepY;
            }
            else if (doubledError > -dy)
            {
                error -= dy;
                x += stepX;
            }
            else
            {
                error += dx;
                y += stepY;
            }
        }
    }

    void PDGridPath::SimplifyPath(PDLayout const& layout, std::vector<Point>& path)
    {
        if (path.size() <= 2)
        {
            return;
        }

        std::vector<Point> simplified;
        simplified.push_back(path.front());
        size_t anchor = 0;
        while (anchor < path.size() - 1)
        {
            size_t best = anchor + 1;
            for (size_t candidate = path.size() - 1; candidate > anchor; --candidate)
            {
                if (LineWalkable(layout, path[anchor], path[candidate]))
                {
                    best = candidate;
                    break;
                }
            }
            simplified.push_back(path[best]);
            anchor = best;
        }
        path = std::move(simplified);
    }
}
