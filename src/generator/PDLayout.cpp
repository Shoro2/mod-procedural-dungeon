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
#include <algorithm>

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

    std::vector<std::vector<int>> PDLayout::DoorwayIncidentRooms() const
    {
        int const offsetsX[4] = { -1, 1, 0, 0 };
        int const offsetsY[4] = { 0, 0, -1, 1 };

        auto isCorridor = [&](int x, int y)
        {
            TileType const type = At(x, y);
            return type == TileType::Corridor || type == TileType::Doorway;
        };

        // Flood-fill 4-connected Corridor+Doorway tiles into blobs. Room floor
        // and walls are barriers. Fixed y-then-x scan keeps blob ids stable.
        std::vector<int> blobOf(static_cast<size_t>(width) * height, -1);
        int blobCount = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (!isCorridor(x, y) || blobOf[static_cast<size_t>(y) * width + x] != -1)
                {
                    continue;
                }
                std::vector<TilePos> frontier;
                frontier.push_back({ x, y });
                blobOf[static_cast<size_t>(y) * width + x] = blobCount;
                for (size_t head = 0; head < frontier.size(); ++head)
                {
                    TilePos const tile = frontier[head];
                    for (int n = 0; n < 4; ++n)
                    {
                        int const nx = tile.x + offsetsX[n];
                        int const ny = tile.y + offsetsY[n];
                        if (!InBounds(nx, ny) || !isCorridor(nx, ny) || blobOf[static_cast<size_t>(ny) * width + nx] != -1)
                        {
                            continue;
                        }
                        blobOf[static_cast<size_t>(ny) * width + nx] = blobCount;
                        frontier.push_back({ nx, ny });
                    }
                }
                ++blobCount;
            }
        }

        // blob -> incident rooms: every room whose floor is 4-adjacent to any
        // tile of the blob. This is what "the corridor touches room R" really
        // means - it must be derived from floor adjacency, not doorway.roomId: a
        // doorway tile between two rooms records only one of them as its
        // leads-into room, and some rooms are only ever reached across a doorway
        // assigned to the neighbor (so they have no doorway of their own).
        std::vector<std::vector<int>> blobRooms(static_cast<size_t>(blobCount));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int const blob = blobOf[static_cast<size_t>(y) * width + x];
                if (blob < 0)
                {
                    continue;
                }
                for (int n = 0; n < 4; ++n)
                {
                    int const nx = x + offsetsX[n];
                    int const ny = y + offsetsY[n];
                    if (At(nx, ny) != TileType::RoomFloor)
                    {
                        continue;
                    }
                    int const roomId = RoomIdAt(nx, ny);
                    if (roomId < 0)
                    {
                        continue;
                    }
                    std::vector<int>& incident = blobRooms[static_cast<size_t>(blob)];
                    if (std::find(incident.begin(), incident.end(), roomId) == incident.end())
                    {
                        incident.push_back(roomId);
                    }
                }
            }
        }

        std::vector<int> doorwayBlob(doorways.size(), -1);
        for (size_t i = 0; i < doorways.size(); ++i)
        {
            Doorway const& doorway = doorways[i];
            if (!doorway.tiles.empty())
            {
                doorwayBlob[i] = blobOf[static_cast<size_t>(doorway.tiles[0].y) * width + doorway.tiles[0].x];
            }
        }

        std::vector<std::vector<int>> result(doorways.size());
        for (size_t i = 0; i < doorways.size(); ++i)
        {
            int const blob = doorwayBlob[i];
            if (blob >= 0)
            {
                result[i] = blobRooms[static_cast<size_t>(blob)];
                std::sort(result[i].begin(), result[i].end());
            }
        }
        return result;
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
