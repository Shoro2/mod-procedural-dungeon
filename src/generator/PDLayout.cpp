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

#include "PDLayout.h"

namespace PDungeon
{
    int PDLayout::RoomIdAt(int x, int y) const
    {
        for (Room const& room : rooms)
        {
            if (x >= room.x && x < room.x + room.w && y >= room.y && y < room.y + room.h)
            {
                return room.id;
            }
        }
        return -1;
    }

    namespace
    {
        char RoomKindLetter(RoomKind kind)
        {
            switch (kind)
            {
                case RoomKind::Entrance:
                    return 'E';
                case RoomKind::Combat:
                    return 'C';
                case RoomKind::Elite:
                    return 'L';
                case RoomKind::Treasure:
                    return 'T';
                case RoomKind::Shrine:
                    return 'S';
                case RoomKind::Boss:
                    return 'B';
            }
            return '?';
        }
    }

    std::string PDLayout::AsciiDump() const
    {
        std::string out;
        out.reserve(static_cast<size_t>(height) * (width + 1));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                char symbol = ' ';
                switch (At(x, y))
                {
                    case TileType::Void:
                        symbol = ' ';
                        break;
                    case TileType::RoomFloor:
                        symbol = '.';
                        break;
                    case TileType::Corridor:
                        symbol = ',';
                        break;
                    case TileType::Doorway:
                        symbol = 'D';
                        break;
                    case TileType::Wall:
                        symbol = '#';
                        break;
                }
                out += symbol;
            }
            out += '\n';
        }

        for (Room const& room : rooms)
        {
            size_t const index = static_cast<size_t>(room.CenterY()) * (width + 1) + room.CenterX();
            if (index < out.size())
            {
                out[index] = RoomKindLetter(room.kind);
            }
        }

        for (Decoration const& decor : decorations)
        {
            if (decor.kind == DecorKind::EntrancePad)
            {
                size_t const index = static_cast<size_t>(decor.y) * (width + 1) + decor.x;
                if (index < out.size())
                {
                    out[index] = '@';
                }
            }
        }

        return out;
    }
}
