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

#include "PDBlockPlan.h"
#include "PDRandom.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <queue>
#include <set>

namespace PDungeon
{
    namespace
    {
        int const CHUNK_ID_BASE = 2000;
        int const BLOCKS_PER_TILE = 8;
        int const MAX_BLOCK_COORD = 64 * BLOCKS_PER_TILE;   // 512
        int const MIN_ROOM_GAP = 2;                         // Manhattan, so a corridor always fits

        struct Cell
        {
            int x = 0;
            int y = 0;

            bool operator<(Cell const& o) const
            {
                return y != o.y ? y < o.y : x < o.x;
            }

            bool operator==(Cell const& o) const { return x == o.x && y == o.y; }
        };

        struct Node                 // a room, before roles are assigned
        {
            Cell cell;
            int id = 0;
            int depth = 0;
        };

        unsigned OppositeBit(unsigned bit)
        {
            switch (bit)
            {
                case SOCKET_N: return SOCKET_S;
                case SOCKET_S: return SOCKET_N;
                case SOCKET_E: return SOCKET_W;
                default:       return SOCKET_E;
            }
        }

        // Kit convention, mirrored from 48_gen_t1_blockkit.py: bx grows EAST and
        // by grows SOUTH, so N is -y, S is +y, W is -x, E is +x.
        void StepFor(unsigned bit, int& dx, int& dy)
        {
            dx = 0;
            dy = 0;
            switch (bit)
            {
                case SOCKET_N: dy = -1; break;
                case SOCKET_S: dy = 1;  break;
                case SOCKET_W: dx = -1; break;
                default:       dx = 1;  break;
            }
        }

        unsigned BitForStep(int dx, int dy)
        {
            if (dy < 0) return SOCKET_N;
            if (dy > 0) return SOCKET_S;
            if (dx < 0) return SOCKET_W;
            return SOCKET_E;
        }

        int PopCount(unsigned mask)
        {
            int n = 0;
            for (unsigned b = 1; b <= SOCKET_W; b <<= 1)
            {
                if (mask & b)
                {
                    ++n;
                }
            }
            return n;
        }

        // Corridor role follows purely from the socket count/shape, which is
        // what the kit provides variants for.
        BlockRole CorridorRoleFor(unsigned mask)
        {
            switch (PopCount(mask))
            {
                case 4:  return BlockRole::CorridorCross;
                case 3:  return BlockRole::CorridorT;
                case 2:
                    // Straight only when the two sockets face each other.
                    if (mask == (SOCKET_N | SOCKET_S) || mask == (SOCKET_E | SOCKET_W))
                    {
                        return BlockRole::CorridorStraight;
                    }
                    return BlockRole::CorridorCorner;
                default:
                    // A 0- or 1-socket corridor is a dead end the kit has no
                    // variant for; ValidateBlockPlan rejects it rather than
                    // letting a bogus chunkId reach the client.
                    return BlockRole::CorridorStraight;
            }
        }

        int ChunkIdFor(BlockRole role, unsigned mask)
        {
            return CHUNK_ID_BASE + static_cast<int>(role) * 100 + static_cast<int>(mask);
        }

        std::string MaskName(unsigned mask)
        {
            std::string s;
            if (mask & SOCKET_N) s += 'N';
            if (mask & SOCKET_E) s += 'E';
            if (mask & SOCKET_S) s += 'S';
            if (mask & SOCKET_W) s += 'W';
            return s.empty() ? std::string("-") : s;
        }

        // --- graph helpers ---------------------------------------------------

        struct GraphEdge
        {
            int a = 0;
            int b = 0;
            int weightSq = 0;
        };

        int DistSq(Cell const& p, Cell const& q)
        {
            int const dx = p.x - q.x;
            int const dy = p.y - q.y;
            return dx * dx + dy * dy;
        }

        // Union-find over room ids, for Kruskal.
        int FindRoot(std::vector<int>& parent, int i)
        {
            while (parent[static_cast<size_t>(i)] != i)
            {
                parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
                i = parent[static_cast<size_t>(i)];
            }
            return i;
        }

        bool ScatterRooms(BlockCfg const& cfg, PDRandom& rng, int wanted, std::vector<Node>& out)
        {
            out.clear();
            // Rooms are kept MIN_ROOM_GAP apart so an L-route always has at
            // least one free cell to become a corridor block. Without that the
            // planner would emit rooms sharing an edge, which is legal geometry
            // but produces dungeons with no corridors at all.
            int const attempts = wanted * 200;
            for (int i = 0; i < attempts && static_cast<int>(out.size()) < wanted; ++i)
            {
                Cell c;
                c.x = rng.UniformInt(0, cfg.fieldBlocks - 1);
                c.y = rng.UniformInt(0, cfg.fieldBlocks - 1);

                bool ok = true;
                for (Node const& n : out)
                {
                    int const md = std::abs(n.cell.x - c.x) + std::abs(n.cell.y - c.y);
                    if (md < MIN_ROOM_GAP)
                    {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                {
                    continue;
                }

                Node n;
                n.cell = c;
                n.id = static_cast<int>(out.size());
                out.push_back(n);
            }
            return static_cast<int>(out.size()) == wanted;
        }

        std::vector<GraphEdge> SelectCorridorEdges(std::vector<Node> const& nodes, PDRandom& rng,
                                                   int loopChancePct)
        {
            std::vector<GraphEdge> candidates;
            for (size_t i = 0; i < nodes.size(); ++i)
            {
                for (size_t j = i + 1; j < nodes.size(); ++j)
                {
                    GraphEdge e;
                    e.a = static_cast<int>(i);
                    e.b = static_cast<int>(j);
                    e.weightSq = DistSq(nodes[i].cell, nodes[j].cell);
                    candidates.push_back(e);
                }
            }
            // Stable order first, so the sort cannot depend on input order.
            std::sort(candidates.begin(), candidates.end(), [](GraphEdge const& l, GraphEdge const& r)
            {
                if (l.weightSq != r.weightSq) return l.weightSq < r.weightSq;
                if (l.a != r.a) return l.a < r.a;
                return l.b < r.b;
            });

            std::vector<int> parent(nodes.size());
            for (size_t i = 0; i < parent.size(); ++i)
            {
                parent[i] = static_cast<int>(i);
            }

            std::vector<GraphEdge> chosen;
            for (GraphEdge const& e : candidates)
            {
                int const ra = FindRoot(parent, e.a);
                int const rb = FindRoot(parent, e.b);
                if (ra != rb)
                {
                    parent[static_cast<size_t>(ra)] = rb;
                    chosen.push_back(e);
                }
                else if (rng.Chance(loopChancePct))
                {
                    chosen.push_back(e);        // a loop, for layouts that are not pure trees
                }
            }
            return chosen;
        }
    }

    uint32_t Crc32(void const* data, size_t len)
    {
        uint8_t const* p = static_cast<uint8_t const*>(data);
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b)
            {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return crc ^ 0xFFFFFFFFu;
    }

    PlacedBlock const* BlockPlan::At(int bx, int by) const
    {
        for (PlacedBlock const& b : blocks)
        {
            if (b.bx == bx && b.by == by)
            {
                return &b;
            }
        }
        return nullptr;
    }

    bool ValidateBlockPlan(BlockPlan const& plan, std::string* error)
    {
        auto fail = [error](char const* why) {
            if (error) *error = why;
            return false;
        };

        if (plan.blocks.empty()) return fail("no blocks");
        if (plan.entranceIndex < 0) return fail("no entrance block");
        if (plan.bossIndex < 0) return fail("no boss block");

        std::set<std::pair<int, int>> seen;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.bx < 0 || b.by < 0 || b.bx >= MAX_BLOCK_COORD || b.by >= MAX_BLOCK_COORD)
            {
                return fail("block coordinate outside the map");
            }
            if (!seen.insert(std::make_pair(b.bx, b.by)).second)
            {
                return fail("two blocks share a coordinate");
            }
            if (b.socketMask == 0)
            {
                return fail("a block has no sockets and is unreachable");
            }
            bool const isRoom = b.role == BlockRole::Room || b.role == BlockRole::RoomEntrance ||
                                b.role == BlockRole::RoomBoss;
            if (!isRoom && PopCount(b.socketMask) < 2)
            {
                return fail("corridor block with fewer than two sockets");
            }
            if (b.chunkId != ChunkIdFor(b.role, b.socketMask))
            {
                return fail("chunkId does not match role and mask");
            }
        }

        // Every open socket must be answered by the neighbour. A dangling
        // socket would render as a corridor ending in mid-air, and a mismatched
        // pair would leave a wall where the player expects a doorway.
        for (PlacedBlock const& b : plan.blocks)
        {
            for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
            {
                if (!(b.socketMask & bit))
                {
                    continue;
                }
                int dx = 0, dy = 0;
                StepFor(bit, dx, dy);
                PlacedBlock const* n = plan.At(b.bx + dx, b.by + dy);
                if (!n)
                {
                    return fail("socket opens onto an empty coordinate");
                }
                if (!(n->socketMask & OppositeBit(bit)))
                {
                    return fail("neighbouring blocks disagree on a shared edge");
                }
            }
        }

        // Connectivity: every block must be reachable from the entrance through
        // open sockets, or part of the dungeon is unplayable.
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }
        std::vector<bool> visited(plan.blocks.size(), false);
        std::queue<size_t> q;
        q.push(static_cast<size_t>(plan.entranceIndex));
        visited[static_cast<size_t>(plan.entranceIndex)] = true;
        size_t reached = 1;
        while (!q.empty())
        {
            PlacedBlock const& b = plan.blocks[q.front()];
            q.pop();
            for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
            {
                if (!(b.socketMask & bit)) continue;
                int dx = 0, dy = 0;
                StepFor(bit, dx, dy);
                auto it = index.find(std::make_pair(b.bx + dx, b.by + dy));
                if (it == index.end()) continue;
                if (visited[it->second]) continue;
                visited[it->second] = true;
                ++reached;
                q.push(it->second);
            }
        }
        if (reached != plan.blocks.size())
        {
            return fail("some blocks are unreachable from the entrance");
        }
        return true;
    }

    bool GenerateBlockPlan(BlockCfg const& cfg, BlockPlan* out)
    {
        if (!out)
        {
            return false;
        }

        for (int attempt = 0; attempt < cfg.maxTries; ++attempt)
        {
            uint32_t const seed = cfg.seed + static_cast<uint32_t>(attempt);
            PDRandom rng(seed);

            int const wanted = std::max(2, cfg.rooms + cfg.bossRooms);
            std::vector<Node> nodes;
            if (!ScatterRooms(cfg, rng, wanted, nodes))
            {
                continue;       // the field is too small for this many rooms
            }

            // Socket masks accumulate per cell as corridors are routed.
            std::map<Cell, unsigned> masks;
            std::map<Cell, int> roomOf;         // cell -> room id, rooms only
            for (Node const& n : nodes)
            {
                masks[n.cell] = 0u;
                roomOf[n.cell] = n.id;
            }

            std::vector<GraphEdge> const edges = SelectCorridorEdges(nodes, rng, cfg.loopChancePct);

            // Route every edge as an L: one axis first, then the other. Which
            // axis leads is a coin flip, which is what keeps layouts from all
            // looking like staircases in the same direction.
            bool routed = true;
            for (GraphEdge const& e : edges)
            {
                Cell const from = nodes[static_cast<size_t>(e.a)].cell;
                Cell const to = nodes[static_cast<size_t>(e.b)].cell;
                bool const xFirst = rng.Chance(50);

                Cell cur = from;
                std::vector<Cell> path;
                auto walk = [&](int targetX, int targetY)
                {
                    while (cur.x != targetX || cur.y != targetY)
                    {
                        Cell next = cur;
                        if (cur.x != targetX)
                        {
                            next.x += (targetX > cur.x) ? 1 : -1;
                        }
                        else
                        {
                            next.y += (targetY > cur.y) ? 1 : -1;
                        }

                        unsigned const outBit = BitForStep(next.x - cur.x, next.y - cur.y);
                        masks[cur] |= outBit;
                        masks[next] |= OppositeBit(outBit);
                        cur = next;
                        path.push_back(cur);
                    }
                };

                if (xFirst)
                {
                    walk(to.x, cur.y);
                    walk(to.x, to.y);
                }
                else
                {
                    walk(cur.x, to.y);
                    walk(to.x, to.y);
                }

                if (cur.x != to.x || cur.y != to.y)
                {
                    routed = false;
                    break;
                }
            }
            if (!routed)
            {
                continue;
            }

            // BFS from the room nearest the field's north-west corner, so the
            // entrance is stable for a given layout rather than a draw.
            int entranceRoom = 0;
            for (Node const& n : nodes)
            {
                Node const& best = nodes[static_cast<size_t>(entranceRoom)];
                int const a = n.cell.y * cfg.fieldBlocks + n.cell.x;
                int const b = best.cell.y * cfg.fieldBlocks + best.cell.x;
                if (a < b)
                {
                    entranceRoom = n.id;
                }
            }

            // Depth over the block graph, then rooms by depth: the deepest is
            // the boss. Corridors take no depth.
            std::map<Cell, int> depth;
            std::queue<Cell> q;
            Cell const start = nodes[static_cast<size_t>(entranceRoom)].cell;
            depth[start] = 0;
            q.push(start);
            while (!q.empty())
            {
                Cell const c = q.front();
                q.pop();
                unsigned const mask = masks[c];
                for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                {
                    if (!(mask & bit)) continue;
                    int dx = 0, dy = 0;
                    StepFor(bit, dx, dy);
                    Cell n;
                    n.x = c.x + dx;
                    n.y = c.y + dy;
                    if (masks.find(n) == masks.end()) continue;
                    if (depth.find(n) != depth.end()) continue;
                    depth[n] = depth[c] + 1;
                    q.push(n);
                }
            }

            int bossRoom = -1;
            int bossDepth = -1;
            for (Node const& n : nodes)
            {
                auto it = depth.find(n.cell);
                if (it == depth.end()) continue;        // unreachable; caught by validation
                if (it->second > bossDepth || (it->second == bossDepth && n.id < bossRoom))
                {
                    bossDepth = it->second;
                    bossRoom = n.id;
                }
            }
            if (bossRoom < 0 || bossRoom == entranceRoom)
            {
                continue;
            }

            // Materialise. Fixed iteration order (masks is an ordered map keyed
            // by (y, x)), so the block list is reproducible.
            BlockPlan plan;
            plan.config = cfg;
            plan.effectiveSeed = seed;
            for (auto const& kv : masks)
            {
                Cell const& c = kv.first;
                unsigned const mask = kv.second;

                PlacedBlock b;
                b.bx = cfg.originBX + c.x;
                b.by = cfg.originBY + c.y;
                b.socketMask = mask;

                auto rit = roomOf.find(c);
                if (rit != roomOf.end())
                {
                    b.roomId = rit->second;
                    b.role = (rit->second == entranceRoom) ? BlockRole::RoomEntrance
                           : (rit->second == bossRoom)     ? BlockRole::RoomBoss
                                                           : BlockRole::Room;
                }
                else
                {
                    b.role = CorridorRoleFor(mask);
                }

                auto dit = depth.find(c);
                b.depth = (dit == depth.end()) ? -1 : dit->second;
                b.chunkId = ChunkIdFor(b.role, b.socketMask);

                if (b.role == BlockRole::RoomEntrance)
                {
                    plan.entranceIndex = static_cast<int>(plan.blocks.size());
                }
                else if (b.role == BlockRole::RoomBoss)
                {
                    plan.bossIndex = static_cast<int>(plan.blocks.size());
                }
                plan.blocks.push_back(b);
            }

            std::string error;
            if (!ValidateBlockPlan(plan, &error))
            {
                continue;       // try the next seed rather than ship a broken layout
            }

            *out = plan;
            return true;
        }
        return false;
    }

    std::string EmitManifest(BlockPlan const& plan, int seq, int manifestVer)
    {
        int minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool first = true;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (first)
            {
                minX = maxX = b.bx;
                minY = maxY = b.by;
                first = false;
                continue;
            }
            minX = std::min(minX, b.bx);
            maxX = std::max(maxX, b.bx);
            minY = std::min(minY, b.by);
            maxY = std::max(maxY, b.by);
        }

        char head[160];
        std::snprintf(head, sizeof(head), "FLPD2;%d;%d;%u;%d;%d,%d,%d,%d\n",
                      manifestVer, seq, plan.effectiveSeed, plan.config.theme,
                      minX, minY, maxX, maxY);

        std::string body = head;

        // The oracle emits block lines sorted by (by, bx); matching that keeps
        // the two sides byte-comparable, which is the whole point of having an
        // oracle.
        std::vector<PlacedBlock> sorted = plan.blocks;
        std::sort(sorted.begin(), sorted.end(), [](PlacedBlock const& l, PlacedBlock const& r)
        {
            return l.by != r.by ? l.by < r.by : l.bx < r.bx;
        });

        for (PlacedBlock const& b : sorted)
        {
            char line[96];
            std::snprintf(line, sizeof(line), "B;%d;%d;%d;%d;%s\n",
                          b.bx, b.by, b.chunkId, 0, MaskName(b.socketMask).c_str());
            body += line;
        }

        char trailer[24];
        std::snprintf(trailer, sizeof(trailer), "E;%08x\n", Crc32(body.data(), body.size()));
        return body + trailer;
    }

    std::string AsciiBlockDump(BlockPlan const& plan)
    {
        if (plan.blocks.empty())
        {
            return "(empty plan)\n";
        }

        int minX = plan.blocks[0].bx, maxX = minX;
        int minY = plan.blocks[0].by, maxY = minY;
        for (PlacedBlock const& b : plan.blocks)
        {
            minX = std::min(minX, b.bx);
            maxX = std::max(maxX, b.bx);
            minY = std::min(minY, b.by);
            maxY = std::max(maxY, b.by);
        }

        std::string out;
        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                PlacedBlock const* b = plan.At(x, y);
                if (!b)
                {
                    out += ' ';
                    continue;
                }
                switch (b->role)
                {
                    case BlockRole::RoomEntrance: out += 'E'; break;
                    case BlockRole::RoomBoss:     out += 'B'; break;
                    case BlockRole::Room:         out += 'R'; break;
                    default:
                        // Corridors draw as the shape of their sockets, which
                        // makes a wrong mask visible at a glance.
                        switch (b->socketMask)
                        {
                            case (SOCKET_N | SOCKET_S): out += '|'; break;
                            case (SOCKET_E | SOCKET_W): out += '-'; break;
                            default:                    out += '+'; break;
                        }
                }
            }
            out += '\n';
        }
        return out;
    }
}
