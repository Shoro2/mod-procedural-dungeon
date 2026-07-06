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

#include "PDDungeonGenerator.h"
#include "PDRandom.h"
#include <algorithm>
#include <cstdlib>

namespace PDungeon
{
    namespace
    {
        int const ROOM_GAP = 2;                  // minimum empty tiles between rooms
        int const BORDER_MARGIN = 3;             // keep-out border around the grid
        int const SEPARATION_MAX_ITERATIONS = 200;
        int const SCATTER_BUFFER_ROOMS = 2;      // extra rooms scattered as drop reserve
        int const KNN_NEIGHBORS = 3;
        int const ELITE_DEPTH_NUM = 3;           // elite rooms: depth >= 3/5 of boss depth
        int const ELITE_DEPTH_DEN = 5;

        struct Dsu
        {
            std::vector<int> parent;

            explicit Dsu(int n) : parent(n)
            {
                for (int i = 0; i < n; ++i)
                {
                    parent[i] = i;
                }
            }

            int Find(int a)
            {
                while (parent[a] != a)
                {
                    parent[a] = parent[parent[a]];
                    a = parent[a];
                }
                return a;
            }

            bool Union(int a, int b)
            {
                int const ra = Find(a);
                int const rb = Find(b);
                if (ra == rb)
                {
                    return false;
                }
                parent[rb] = ra;
                return true;
            }
        };

        int DistSq(Room const& a, Room const& b)
        {
            int const dx = a.CenterX() - b.CenterX();
            int const dy = a.CenterY() - b.CenterY();
            return dx * dx + dy * dy;
        }

        // Interval-overlap length on one axis, inflated by ROOM_GAP so that
        // "touching" rooms still count as colliding.
        bool RoomsCollide(Room const& a, Room const& b, int& penX, int& penY)
        {
            penX = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x) + ROOM_GAP;
            penY = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y) + ROOM_GAP;
            return penX > 0 && penY > 0;
        }

        void ClampRoom(GenConfig const& config, Room& room)
        {
            room.x = std::max(BORDER_MARGIN, std::min(room.x, config.gridWidth - BORDER_MARGIN - room.w));
            room.y = std::max(BORDER_MARGIN, std::min(room.y, config.gridHeight - BORDER_MARGIN - room.h));
        }

        int OddSize(PDRandom& rng, int sizeMin, int sizeMax)
        {
            int const steps = (sizeMax - sizeMin) / 2;
            return sizeMin + 2 * rng.UniformInt(0, steps);
        }

        void ScatterRooms(GenConfig const& config, PDRandom& rng, int sizeMin, int sizeMax, std::vector<Room>& rooms)
        {
            int const targetCount = rng.UniformInt(config.roomsMin, config.roomsMax);
            int const scatterCount = targetCount + SCATTER_BUFFER_ROOMS;
            int const radiusX = config.gridWidth / 2 - sizeMax / 2 - BORDER_MARGIN - 1;
            int const radiusY = config.gridHeight / 2 - sizeMax / 2 - BORDER_MARGIN - 1;
            int const radius = std::max(1, std::min(radiusX, radiusY));

            rooms.clear();
            rooms.reserve(static_cast<size_t>(scatterCount));
            for (int i = 0; i < scatterCount; ++i)
            {
                Room room;
                room.id = i;
                room.w = OddSize(rng, sizeMin, sizeMax);
                room.h = OddSize(rng, sizeMin, sizeMax);

                // Integer-only point in a disc (rejection sampled).
                int dx = 0;
                int dy = 0;
                for (int attempt = 0; attempt < 100; ++attempt)
                {
                    dx = rng.UniformInt(-radius, radius);
                    dy = rng.UniformInt(-radius, radius);
                    if (dx * dx + dy * dy <= radius * radius)
                    {
                        break;
                    }
                }

                room.x = config.gridWidth / 2 + dx - room.w / 2;
                room.y = config.gridHeight / 2 + dy - room.h / 2;
                ClampRoom(config, room);
                rooms.push_back(room);
            }
        }

        void SeparateRooms(GenConfig const& config, std::vector<Room>& rooms)
        {
            for (int iteration = 0; iteration < SEPARATION_MAX_ITERATIONS; ++iteration)
            {
                bool moved = false;
                for (size_t i = 0; i < rooms.size(); ++i)
                {
                    for (size_t j = i + 1; j < rooms.size(); ++j)
                    {
                        Room& a = rooms[i];
                        Room& b = rooms[j];
                        int penX = 0;
                        int penY = 0;
                        if (!RoomsCollide(a, b, penX, penY))
                        {
                            continue;
                        }

                        if (penX <= penY)
                        {
                            int sign = 1;
                            if (a.CenterX() > b.CenterX())
                            {
                                sign = -1;
                            }
                            int const moveA = (penX + 1) / 2;
                            a.x -= sign * moveA;
                            b.x += sign * (penX - moveA);
                        }
                        else
                        {
                            int sign = 1;
                            if (a.CenterY() > b.CenterY())
                            {
                                sign = -1;
                            }
                            int const moveA = (penY + 1) / 2;
                            a.y -= sign * moveA;
                            b.y += sign * (penY - moveA);
                        }

                        ClampRoom(config, a);
                        ClampRoom(config, b);
                        moved = true;
                    }
                }

                if (!moved)
                {
                    return;
                }
            }
        }

        // Drops rooms that still collide after separation (earlier index wins)
        // and trims the scatter buffer. Room ids are reassigned to 0..n-1.
        bool FinalizeRooms(GenConfig const& config, std::vector<Room>& rooms)
        {
            std::vector<Room> kept;
            kept.reserve(rooms.size());
            for (Room const& candidate : rooms)
            {
                bool collides = false;
                for (Room const& existing : kept)
                {
                    int penX = 0;
                    int penY = 0;
                    if (RoomsCollide(existing, candidate, penX, penY))
                    {
                        collides = true;
                        break;
                    }
                }
                if (!collides)
                {
                    kept.push_back(candidate);
                }
            }

            if (static_cast<int>(kept.size()) > config.roomsMax)
            {
                kept.resize(static_cast<size_t>(config.roomsMax));
            }

            if (static_cast<int>(kept.size()) < std::max(config.roomsMin, 2))
            {
                return false;
            }

            for (size_t i = 0; i < kept.size(); ++i)
            {
                kept[i].id = static_cast<int>(i);
            }
            rooms = std::move(kept);
            return true;
        }

        void BuildNearestNeighborEdges(std::vector<Room> const& rooms, std::vector<Edge>& edges)
        {
            edges.clear();
            int const count = static_cast<int>(rooms.size());
            std::vector<std::pair<int, int>> byDistance; // (distSq, other)
            for (int i = 0; i < count; ++i)
            {
                byDistance.clear();
                for (int j = 0; j < count; ++j)
                {
                    if (j != i)
                    {
                        byDistance.emplace_back(DistSq(rooms[i], rooms[j]), j);
                    }
                }
                std::sort(byDistance.begin(), byDistance.end());

                int const take = std::min(KNN_NEIGHBORS, static_cast<int>(byDistance.size()));
                for (int n = 0; n < take; ++n)
                {
                    int const j = byDistance[n].second;
                    Edge edge;
                    edge.a = std::min(i, j);
                    edge.b = std::max(i, j);
                    edge.weightSq = byDistance[n].first;
                    edges.push_back(edge);
                }
            }

            std::sort(edges.begin(), edges.end(), [](Edge const& lhs, Edge const& rhs)
            {
                if (lhs.a != rhs.a)
                {
                    return lhs.a < rhs.a;
                }
                return lhs.b < rhs.b;
            });
            edges.erase(std::unique(edges.begin(), edges.end(), [](Edge const& lhs, Edge const& rhs)
            {
                return lhs.a == rhs.a && lhs.b == rhs.b;
            }), edges.end());
        }

        // Kruskal MST plus optional loop edges. False if the graph is not connected.
        bool SelectCorridors(GenConfig const& config, PDRandom& rng, int roomCount, std::vector<Edge> const& edges, std::vector<Edge>& chosen)
        {
            std::vector<Edge> sorted = edges;
            std::sort(sorted.begin(), sorted.end(), [](Edge const& lhs, Edge const& rhs)
            {
                if (lhs.weightSq != rhs.weightSq)
                {
                    return lhs.weightSq < rhs.weightSq;
                }
                if (lhs.a != rhs.a)
                {
                    return lhs.a < rhs.a;
                }
                return lhs.b < rhs.b;
            });

            chosen.clear();
            Dsu dsu(roomCount);
            int unions = 0;
            for (Edge const& edge : sorted)
            {
                if (dsu.Union(edge.a, edge.b))
                {
                    chosen.push_back(edge);
                    ++unions;
                }
                else if (rng.Chance(config.loopChancePct))
                {
                    chosen.push_back(edge);
                }
            }

            if (unions != roomCount - 1)
            {
                return false;
            }

            std::sort(chosen.begin(), chosen.end(), [](Edge const& lhs, Edge const& rhs)
            {
                if (lhs.a != rhs.a)
                {
                    return lhs.a < rhs.a;
                }
                return lhs.b < rhs.b;
            });
            return true;
        }

        // BFS from the entrance assigns depths and picks room roles.
        bool AssignSemantics(PDLayout& layout)
        {
            int const count = static_cast<int>(layout.rooms.size());
            std::vector<std::vector<int>> adjacency(static_cast<size_t>(count));
            for (Edge const& edge : layout.corridors)
            {
                adjacency[static_cast<size_t>(edge.a)].push_back(edge.b);
                adjacency[static_cast<size_t>(edge.b)].push_back(edge.a);
            }

            int entrance = 0;
            for (int i = 1; i < count; ++i)
            {
                Room const& best = layout.rooms[static_cast<size_t>(entrance)];
                Room const& candidate = layout.rooms[static_cast<size_t>(i)];
                if (candidate.CenterX() < best.CenterX() ||
                    (candidate.CenterX() == best.CenterX() && candidate.CenterY() < best.CenterY()))
                {
                    entrance = i;
                }
            }

            std::vector<int> depth(static_cast<size_t>(count), -1);
            std::vector<int> parent(static_cast<size_t>(count), -1);
            std::vector<int> queue;
            queue.reserve(static_cast<size_t>(count));
            queue.push_back(entrance);
            depth[static_cast<size_t>(entrance)] = 0;
            for (size_t head = 0; head < queue.size(); ++head)
            {
                int const current = queue[head];
                for (int next : adjacency[static_cast<size_t>(current)])
                {
                    if (depth[static_cast<size_t>(next)] == -1)
                    {
                        depth[static_cast<size_t>(next)] = depth[static_cast<size_t>(current)] + 1;
                        parent[static_cast<size_t>(next)] = current;
                        queue.push_back(next);
                    }
                }
            }

            int boss = -1;
            for (int i = 0; i < count; ++i)
            {
                if (i == entrance || depth[static_cast<size_t>(i)] < 0)
                {
                    continue;
                }
                if (boss == -1 || depth[static_cast<size_t>(i)] > depth[static_cast<size_t>(boss)])
                {
                    boss = i;
                }
            }
            if (boss == -1)
            {
                return false;
            }

            std::vector<bool> onPath(static_cast<size_t>(count), false);
            for (int walk = boss; walk != -1; walk = parent[static_cast<size_t>(walk)])
            {
                onPath[static_cast<size_t>(walk)] = true;
            }

            int const bossDepth = depth[static_cast<size_t>(boss)];
            int eliteCount = 0;
            for (int i = 0; i < count; ++i)
            {
                Room& room = layout.rooms[static_cast<size_t>(i)];
                room.depth = depth[static_cast<size_t>(i)];
                if (i == entrance)
                {
                    room.kind = RoomKind::Entrance;
                }
                else if (i == boss)
                {
                    room.kind = RoomKind::Boss;
                }
                else if (adjacency[static_cast<size_t>(i)].size() == 1)
                {
                    room.kind = RoomKind::Treasure;
                }
                else if (onPath[static_cast<size_t>(i)] && room.depth * ELITE_DEPTH_DEN >= bossDepth * ELITE_DEPTH_NUM)
                {
                    room.kind = RoomKind::Elite;
                    ++eliteCount;
                }
                else
                {
                    room.kind = RoomKind::Combat;
                }
            }

            // Promote the deepest treasure room to the shrine.
            int shrine = -1;
            for (int i = 0; i < count; ++i)
            {
                if (layout.rooms[static_cast<size_t>(i)].kind != RoomKind::Treasure)
                {
                    continue;
                }
                if (shrine == -1 || layout.rooms[static_cast<size_t>(i)].depth > layout.rooms[static_cast<size_t>(shrine)].depth)
                {
                    shrine = i;
                }
            }
            if (shrine != -1)
            {
                layout.rooms[static_cast<size_t>(shrine)].kind = RoomKind::Shrine;
            }

            // Guarantee at least one elite room when the path allows it.
            if (eliteCount == 0)
            {
                int const beforeBoss = parent[static_cast<size_t>(boss)];
                if (beforeBoss != -1 && beforeBoss != entrance)
                {
                    layout.rooms[static_cast<size_t>(beforeBoss)].kind = RoomKind::Elite;
                }
            }

            layout.entranceRoomId = entrance;
            layout.bossRoomId = boss;
            return true;
        }

        void CarveCorridorTile(PDLayout& layout, int x, int y)
        {
            if (layout.At(x, y) == TileType::Void)
            {
                layout.Set(x, y, TileType::Corridor);
            }
        }

        void CarveHorizontal(PDLayout& layout, int x1, int x2, int y)
        {
            int const from = std::min(x1, x2);
            int const to = std::max(x1, x2);
            for (int x = from; x <= to; ++x)
            {
                CarveCorridorTile(layout, x, y);
                CarveCorridorTile(layout, x, y + 1);
            }
        }

        void CarveVertical(PDLayout& layout, int y1, int y2, int x)
        {
            int const from = std::min(y1, y2);
            int const to = std::max(y1, y2);
            for (int y = from; y <= to; ++y)
            {
                CarveCorridorTile(layout, x, y);
                CarveCorridorTile(layout, x + 1, y);
            }
        }

        void Rasterize(PDLayout& layout, PDRandom& rng)
        {
            layout.tiles.assign(static_cast<size_t>(layout.width) * layout.height, TileType::Void);
            for (Room const& room : layout.rooms)
            {
                for (int y = room.y; y < room.y + room.h; ++y)
                {
                    for (int x = room.x; x < room.x + room.w; ++x)
                    {
                        layout.Set(x, y, TileType::RoomFloor);
                    }
                }
            }

            for (Edge const& edge : layout.corridors)
            {
                Room const& a = layout.rooms[static_cast<size_t>(edge.a)];
                Room const& b = layout.rooms[static_cast<size_t>(edge.b)];
                if (rng.Chance(50))
                {
                    CarveHorizontal(layout, a.CenterX(), b.CenterX(), a.CenterY());
                    CarveVertical(layout, a.CenterY(), b.CenterY(), b.CenterX());
                }
                else
                {
                    CarveVertical(layout, a.CenterY(), b.CenterY(), a.CenterX());
                    CarveHorizontal(layout, a.CenterX(), b.CenterX(), b.CenterY());
                }
            }
        }

        void PlaceDoorways(PDLayout& layout)
        {
            int const offsetsX[4] = { -1, 1, 0, 0 };
            int const offsetsY[4] = { 0, 0, -1, 1 };

            // First pass: flag corridor tiles that touch room floor.
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (layout.At(x, y) != TileType::Corridor)
                    {
                        continue;
                    }
                    for (int n = 0; n < 4; ++n)
                    {
                        if (layout.At(x + offsetsX[n], y + offsetsY[n]) == TileType::RoomFloor)
                        {
                            layout.Set(x, y, TileType::Doorway);
                            break;
                        }
                    }
                }
            }

            // Second pass: flood-fill contiguous doorway tiles into logical
            // door groups (corridors hugging a room edge can create long spans).
            std::vector<bool> grouped(static_cast<size_t>(layout.width) * layout.height, false);
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (layout.At(x, y) != TileType::Doorway || grouped[static_cast<size_t>(y) * layout.width + x])
                    {
                        continue;
                    }

                    Doorway doorway;
                    std::vector<TilePos> frontier;
                    frontier.push_back({ x, y });
                    grouped[static_cast<size_t>(y) * layout.width + x] = true;
                    for (size_t head = 0; head < frontier.size(); ++head)
                    {
                        TilePos const tile = frontier[head];
                        doorway.tiles.push_back(tile);
                        for (int n = 0; n < 4; ++n)
                        {
                            int const nx = tile.x + offsetsX[n];
                            int const ny = tile.y + offsetsY[n];
                            if (layout.At(nx, ny) != TileType::Doorway || grouped[static_cast<size_t>(ny) * layout.width + nx])
                            {
                                continue;
                            }
                            grouped[static_cast<size_t>(ny) * layout.width + nx] = true;
                            frontier.push_back({ nx, ny });
                        }
                    }

                    std::sort(doorway.tiles.begin(), doorway.tiles.end(), [](TilePos const& lhs, TilePos const& rhs)
                    {
                        if (lhs.y != rhs.y)
                        {
                            return lhs.y < rhs.y;
                        }
                        return lhs.x < rhs.x;
                    });

                    int minX = doorway.tiles[0].x;
                    int maxX = doorway.tiles[0].x;
                    int minY = doorway.tiles[0].y;
                    int maxY = doorway.tiles[0].y;
                    for (TilePos const& tile : doorway.tiles)
                    {
                        minX = std::min(minX, tile.x);
                        maxX = std::max(maxX, tile.x);
                        minY = std::min(minY, tile.y);
                        maxY = std::max(maxY, tile.y);
                    }
                    doorway.spanAlongX = (maxX - minX) >= (maxY - minY);

                    for (TilePos const& tile : doorway.tiles)
                    {
                        bool found = false;
                        for (int n = 0; n < 4; ++n)
                        {
                            if (layout.At(tile.x + offsetsX[n], tile.y + offsetsY[n]) == TileType::RoomFloor)
                            {
                                doorway.roomId = layout.RoomIdAt(tile.x + offsetsX[n], tile.y + offsetsY[n]);
                                found = true;
                                break;
                            }
                        }
                        if (found)
                        {
                            break;
                        }
                    }

                    layout.doorways.push_back(doorway);
                }
            }
        }

        void PlaceWalls(PDLayout& layout)
        {
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (layout.At(x, y) != TileType::Void)
                    {
                        continue;
                    }
                    bool nearWalkable = false;
                    for (int dy = -1; dy <= 1 && !nearWalkable; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if ((dx != 0 || dy != 0) && layout.IsWalkable(x + dx, y + dy))
                            {
                                nearWalkable = true;
                                break;
                            }
                        }
                    }
                    if (nearWalkable)
                    {
                        layout.Set(x, y, TileType::Wall);
                    }
                }
            }
        }

        int FacingTowardsFloor(PDLayout const& layout, int x, int y)
        {
            if (layout.IsWalkable(x + 1, y))
            {
                return 0;
            }
            if (layout.IsWalkable(x, y + 1))
            {
                return 1;
            }
            if (layout.IsWalkable(x - 1, y))
            {
                return 2;
            }
            return 3;
        }

        void AddRoomMobs(PDLayout& layout, PDRandom& rng, Room const& room, int packSize, bool includeElite)
        {
            std::vector<std::pair<int, int>> used;
            int placed = 0;
            if (includeElite)
            {
                SpawnPoint elite;
                elite.kind = MobKind::Elite;
                elite.x = room.CenterX();
                elite.y = room.CenterY();
                elite.roomId = room.id;
                layout.spawnPoints.push_back(elite);
                used.emplace_back(elite.x, elite.y);
                ++placed;
            }

            for (int attempt = 0; attempt < packSize * 10 && placed < packSize; ++attempt)
            {
                int const x = rng.UniformInt(room.x + 1, room.x + room.w - 2);
                int const y = rng.UniformInt(room.y + 1, room.y + room.h - 2);
                if (std::find(used.begin(), used.end(), std::make_pair(x, y)) != used.end())
                {
                    continue;
                }
                SpawnPoint spawn;
                spawn.kind = rng.Chance(layout.config.casterChancePct) ? MobKind::Caster : MobKind::Melee;
                spawn.x = x;
                spawn.y = y;
                spawn.roomId = room.id;
                layout.spawnPoints.push_back(spawn);
                used.emplace_back(x, y);
                ++placed;
            }
        }

        void AddDecoration(PDLayout& layout, DecorKind kind, int x, int y, int roomId, int facing = 0)
        {
            Decoration decor;
            decor.kind = kind;
            decor.x = x;
            decor.y = y;
            decor.roomId = roomId;
            decor.facing = facing;
            layout.decorations.push_back(decor);
        }

        void Decorate(PDLayout& layout, PDRandom& rng)
        {
            for (Room const& room : layout.rooms)
            {
                int const cx = room.CenterX();
                int const cy = room.CenterY();
                switch (room.kind)
                {
                    case RoomKind::Entrance:
                        AddDecoration(layout, DecorKind::EntrancePad, cx, cy, room.id);
                        break;
                    case RoomKind::Boss:
                    {
                        SpawnPoint boss;
                        boss.kind = MobKind::Boss;
                        boss.x = cx;
                        boss.y = cy;
                        boss.roomId = room.id;
                        layout.spawnPoints.push_back(boss);
                        AddDecoration(layout, DecorKind::Chest, cx, std::min(cy + 2, room.y + room.h - 2), room.id);
                        AddDecoration(layout, DecorKind::ExitPortal, cx, std::max(cy - 2, room.y + 1), room.id);
                        AddDecoration(layout, DecorKind::Brazier, room.x + 1, room.y + 1, room.id);
                        AddDecoration(layout, DecorKind::Brazier, room.x + room.w - 2, room.y + room.h - 2, room.id);
                        break;
                    }
                    case RoomKind::Treasure:
                    {
                        AddDecoration(layout, DecorKind::Chest, cx, cy, room.id);
                        AddRoomMobs(layout, rng, room, 2, false);
                        break;
                    }
                    case RoomKind::Shrine:
                        AddDecoration(layout, DecorKind::Shrine, cx, cy, room.id);
                        break;
                    case RoomKind::Elite:
                        AddDecoration(layout, DecorKind::Brazier, room.x + 1, room.y + 1, room.id);
                        AddDecoration(layout, DecorKind::Brazier, room.x + room.w - 2, room.y + room.h - 2, room.id);
                        AddRoomMobs(layout, rng, room, rng.UniformInt(layout.config.packSizeMin, layout.config.packSizeMax), true);
                        break;
                    case RoomKind::Combat:
                        AddRoomMobs(layout, rng, room, rng.UniformInt(layout.config.packSizeMin, layout.config.packSizeMax), false);
                        break;
                }
            }

            // Torches on every Nth wall tile that borders walkable space.
            int const torchEvery = std::max(1, layout.config.torchEvery);
            int eligible = 0;
            for (int y = 0; y < layout.height; ++y)
            {
                for (int x = 0; x < layout.width; ++x)
                {
                    if (layout.At(x, y) != TileType::Wall)
                    {
                        continue;
                    }
                    bool borders = false;
                    if (layout.IsWalkable(x + 1, y) || layout.IsWalkable(x - 1, y) ||
                        layout.IsWalkable(x, y + 1) || layout.IsWalkable(x, y - 1))
                    {
                        borders = true;
                    }
                    if (!borders)
                    {
                        continue;
                    }
                    if (eligible % torchEvery == 0)
                    {
                        AddDecoration(layout, DecorKind::Torch, x, y, layout.RoomIdAt(x, y), FacingTowardsFloor(layout, x, y));
                    }
                    ++eligible;
                }
            }
        }

        // Flood fill over walkable tiles; blockBossRoom additionally treats the
        // boss room rectangle and its doorway tiles as solid.
        bool CheckReachability(PDLayout const& layout, bool blockBossRoom)
        {
            Room const* entrance = layout.GetRoom(layout.entranceRoomId);
            Room const* boss = layout.GetRoom(layout.bossRoomId);
            if (!entrance || !boss)
            {
                return false;
            }

            auto isBlocked = [&](int x, int y)
            {
                if (!blockBossRoom)
                {
                    return false;
                }
                if (x >= boss->x - 1 && x < boss->x + boss->w + 1 && y >= boss->y - 1 && y < boss->y + boss->h + 1)
                {
                    return true;
                }
                return false;
            };

            std::vector<bool> visited(static_cast<size_t>(layout.width) * layout.height, false);
            std::vector<std::pair<int, int>> queue;
            queue.emplace_back(entrance->CenterX(), entrance->CenterY());
            visited[static_cast<size_t>(entrance->CenterY()) * layout.width + entrance->CenterX()] = true;
            int const offsetsX[4] = { -1, 1, 0, 0 };
            int const offsetsY[4] = { 0, 0, -1, 1 };
            for (size_t head = 0; head < queue.size(); ++head)
            {
                auto const [x, y] = queue[head];
                for (int n = 0; n < 4; ++n)
                {
                    int const nx = x + offsetsX[n];
                    int const ny = y + offsetsY[n];
                    if (!layout.IsWalkable(nx, ny) || isBlocked(nx, ny))
                    {
                        continue;
                    }
                    size_t const index = static_cast<size_t>(ny) * layout.width + nx;
                    if (!visited[index])
                    {
                        visited[index] = true;
                        queue.emplace_back(nx, ny);
                    }
                }
            }

            for (Room const& room : layout.rooms)
            {
                if (blockBossRoom && room.id == layout.bossRoomId)
                {
                    continue;
                }
                size_t const index = static_cast<size_t>(room.CenterY()) * layout.width + room.CenterX();
                if (!visited[index])
                {
                    return false;
                }
            }
            return true;
        }
    }

    bool PDDungeonGenerator::TryGenerate(GenConfig const& config, uint32_t seed, PDLayout& out)
    {
        out = PDLayout();
        out.config = config;
        out.effectiveSeed = seed;
        out.width = config.gridWidth;
        out.height = config.gridHeight;

        int const sizeMin = config.roomSizeMin | 1;
        int const sizeMax = std::max(sizeMin, config.roomSizeMax | 1);
        if (config.gridWidth < sizeMax + 2 * (BORDER_MARGIN + 2) || config.gridHeight < sizeMax + 2 * (BORDER_MARGIN + 2))
        {
            return false;
        }
        if (config.roomsMin < 2 || config.roomsMax < config.roomsMin)
        {
            return false;
        }

        PDRandom rng(seed);
        ScatterRooms(config, rng, sizeMin, sizeMax, out.rooms);
        SeparateRooms(config, out.rooms);
        if (!FinalizeRooms(config, out.rooms))
        {
            return false;
        }

        std::vector<Edge> edges;
        BuildNearestNeighborEdges(out.rooms, edges);
        if (!SelectCorridors(config, rng, static_cast<int>(out.rooms.size()), edges, out.corridors))
        {
            return false;
        }

        if (!AssignSemantics(out))
        {
            return false;
        }

        Rasterize(out, rng);
        PlaceDoorways(out);
        PlaceWalls(out);
        Decorate(out, rng);

        if (!CheckReachability(out, false))
        {
            return false;
        }
        if (!CheckReachability(out, true))
        {
            return false;
        }
        return true;
    }

    bool PDDungeonGenerator::Generate(GenConfig const& config, PDLayout& out)
    {
        int const tries = std::max(1, config.maxGenerateTries);
        for (int attempt = 0; attempt < tries; ++attempt)
        {
            uint32_t const seed = config.seed + static_cast<uint32_t>(attempt);
            if (TryGenerate(config, seed, out))
            {
                return true;
            }
        }
        return false;
    }
}
