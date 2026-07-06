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

#ifndef MOD_PDUNGEON_LAYOUT_H
#define MOD_PDUNGEON_LAYOUT_H

#include "PDGenTypes.h"
#include <string>
#include <vector>

namespace PDungeon
{
    // Complete result of one generation run: tile grid plus semantic data.
    struct PDLayout
    {
        GenConfig config;
        uint32_t effectiveSeed = 0;  // seed that actually produced this layout
        int width = 0;
        int height = 0;
        std::vector<TileType> tiles;
        std::vector<Room> rooms;
        std::vector<Edge> corridors;
        std::vector<Doorway> doorways;
        std::vector<Decoration> decorations;
        std::vector<SpawnPoint> spawnPoints;
        int entranceRoomId = -1;
        int bossRoomId = -1;

        bool InBounds(int x, int y) const
        {
            return x >= 0 && y >= 0 && x < width && y < height;
        }

        TileType At(int x, int y) const
        {
            return InBounds(x, y) ? tiles[static_cast<size_t>(y) * width + x] : TileType::Void;
        }

        void Set(int x, int y, TileType type)
        {
            if (InBounds(x, y))
            {
                tiles[static_cast<size_t>(y) * width + x] = type;
            }
        }

        bool IsWalkable(int x, int y) const
        {
            TileType const type = At(x, y);
            return type == TileType::RoomFloor || type == TileType::Corridor || type == TileType::Doorway;
        }

        // Room whose rectangle contains (x, y), or -1.
        int RoomIdAt(int x, int y) const;

        Room const* GetRoom(int roomId) const
        {
            return roomId >= 0 && roomId < static_cast<int>(rooms.size()) ? &rooms[roomId] : nullptr;
        }

        // Human-readable map: '#' wall, '.' room, ',' corridor, 'D' doorway,
        // room-kind letter on room centers, '@' entrance pad.
        std::string AsciiDump() const;
    };
}

#endif
