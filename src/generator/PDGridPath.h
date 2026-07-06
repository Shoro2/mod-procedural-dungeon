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

#ifndef MOD_PDUNGEON_GRID_PATH_H
#define MOD_PDUNGEON_GRID_PATH_H

#include "PDLayout.h"

namespace PDungeon
{
    // A* on the generated tile grid. The static mmaps cannot see runtime
    // spawned geometry, so creature movement across rooms is driven by paths
    // computed here and executed as MovePoint chains with generatePath=false.
    class PDGridPath
    {
    public:
        struct Point
        {
            int x = 0;
            int y = 0;

            bool operator==(Point const& other) const
            {
                return x == other.x && y == other.y;
            }
        };

        // 4-neighbor A* over walkable tiles. blockedTiles (optional) marks
        // additional solid tiles, e.g. closed gates. Returns the tile path
        // including both endpoints.
        static bool FindPath(PDLayout const& layout, Point from, Point to, std::vector<Point>& outPath, std::vector<Point> const* blockedTiles = nullptr);

        // Greedy string-pulling: collapses the tile path to few waypoints,
        // keeping every remaining segment fully on walkable tiles.
        static void SimplifyPath(PDLayout const& layout, std::vector<Point>& path);

    private:
        static bool LineWalkable(PDLayout const& layout, Point from, Point to);
    };
}

#endif
