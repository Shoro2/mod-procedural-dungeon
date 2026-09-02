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
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>

namespace PDungeon
{
    namespace
    {
        int const CHUNK_ID_BASE = 2000;         // theme 1 (mine)
        int const CHUNK_ID_BASE_CITY = 12000;   // theme 2 (city) - kit scheme
                                                // themeBase + alt*1000 + role*100 + mask
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
                case 1:
                    // The stub role the kit ships since Phase 2. A 0-socket
                    // corridor still has no variant and ValidateBlockPlan
                    // rejects it rather than letting a bogus chunkId reach
                    // the client.
                    return BlockRole::CorridorDeadEnd;
                default:
                    return BlockRole::CorridorStraight;
            }
        }

        int const ALT_STRIDE = 1000;

        // 0 = unknown theme; ValidateBlockPlan turns that into a refusal
        // rather than letting a bogus chunkId reach the client.
        int ThemeChunkIdBase(int theme)
        {
            switch (theme)
            {
                case 1:  return CHUNK_ID_BASE;
                case 2:  return CHUNK_ID_BASE_CITY;
                default: return 0;
            }
        }

        int ChunkIdFor(int theme, BlockRole role, unsigned mask, int alt)
        {
            return ThemeChunkIdBase(theme) + alt * ALT_STRIDE
                 + static_cast<int>(role) * 100 + static_cast<int>(mask);
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

        // --- Round B chain helpers (spec 2026-09-02 §3, §4) ------------------

        int Manhattan(Cell const& a, Cell const& b)
        {
            return std::abs(a.x - b.x) + std::abs(a.y - b.y);
        }

        // The planning field while a chain is being laid: occupancy, the
        // socket bits as they accumulate, every room cell placed so far (for
        // the gap rule) and the spine in chain order. Small enough (<= 64
        // cells) that the depth-first search copies it per level.
        struct Field
        {
            int size = 0;
            std::vector<uint8_t> occ;       // 0 free, 1 room, 2 corridor
            std::vector<unsigned> masks;    // socket bits per cell
            std::vector<Cell> rooms;
            std::vector<Cell> chain;

            explicit Field(int n)
                : size(n), occ(static_cast<size_t>(n) * static_cast<size_t>(n), 0),
                  masks(static_cast<size_t>(n) * static_cast<size_t>(n), 0u) { }

            size_t Index(Cell const& c) const
            {
                return static_cast<size_t>(c.y) * static_cast<size_t>(size) + static_cast<size_t>(c.x);
            }

            bool Inside(Cell const& c) const
            {
                return c.x >= 0 && c.y >= 0 && c.x < size && c.y < size;
            }

            bool Free(Cell const& c) const
            {
                return Inside(c) && occ[Index(c)] == 0;
            }
        };

        // Interior cells of the L-route from a to b (endpoints excluded), in
        // walking order. xFirst walks the x axis to b.x, then the y axis - the
        // same two walks the v2 planner routed its MST edges with.
        std::vector<Cell> LRouteInterior(Cell const& a, Cell const& b, bool xFirst)
        {
            std::vector<Cell> out;
            Cell cur = a;
            auto walk = [&](int targetX, int targetY)
            {
                while (cur.x != targetX || cur.y != targetY)
                {
                    if (cur.x != targetX)
                    {
                        cur.x += (targetX > cur.x) ? 1 : -1;
                    }
                    else
                    {
                        cur.y += (targetY > cur.y) ? 1 : -1;
                    }
                    if (!(cur == b))
                    {
                        out.push_back(cur);
                    }
                }
            };
            if (xFirst)
            {
                walk(b.x, a.y);
                walk(b.x, b.y);
            }
            else
            {
                walk(a.x, b.y);
                walk(b.x, b.y);
            }
            return out;
        }

        enum : int
        {
            ORDER_NONE = 0,
            ORDER_X_FIRST = 1,
            ORDER_Y_FIRST = 2
        };

        bool InteriorFree(Field const& f, std::vector<Cell> const& interior)
        {
            for (Cell const& c : interior)
            {
                if (!f.Free(c))
                {
                    return false;
                }
            }
            return true;
        }

        // Which L-orders between a and b run over free cells only. This is the
        // rule that makes the layout one path by construction: a corridor is
        // never laid through a room or across another corridor. A straight
        // route (same row or column) is ONE route and reports x-first only,
        // so it never costs an axis draw.
        int FeasibleOrders(Field const& f, Cell const& a, Cell const& b)
        {
            if (a.x == b.x || a.y == b.y)
            {
                return InteriorFree(f, LRouteInterior(a, b, true)) ? ORDER_X_FIRST : ORDER_NONE;
            }
            int orders = ORDER_NONE;
            if (InteriorFree(f, LRouteInterior(a, b, true)))
            {
                orders |= ORDER_X_FIRST;
            }
            if (InteriorFree(f, LRouteInterior(a, b, false)))
            {
                orders |= ORDER_Y_FIRST;
            }
            return orders;
        }

        // The axis coin, drawn ONLY when both orders are open.
        bool ChooseXFirst(PDRandom& rng, int orders)
        {
            if (orders == (ORDER_X_FIRST | ORDER_Y_FIRST))
            {
                return rng.Chance(50);
            }
            return orders == ORDER_X_FIRST;
        }

        // Claims the corridor cells between two room cells and opens the
        // sockets along the way.
        void CommitRoute(Field& f, Cell const& a, Cell const& b, bool xFirst)
        {
            std::vector<Cell> const interior = LRouteInterior(a, b, xFirst);
            std::vector<Cell> path = interior;
            path.push_back(b);
            Cell cur = a;
            for (Cell const& next : path)
            {
                unsigned const outBit = BitForStep(next.x - cur.x, next.y - cur.y);
                f.masks[f.Index(cur)] |= outBit;
                f.masks[f.Index(next)] |= OppositeBit(outBit);
                cur = next;
            }
            for (Cell const& c : interior)
            {
                f.occ[f.Index(c)] = 2;
            }
        }

        struct StepCandidate
        {
            Cell cell;
            int orders = ORDER_NONE;
        };

        int const STEP_MIN = 2;     // Manhattan: one corridor block
        int const STEP_MAX = 3;     // two corridor blocks

        // Cells the next room may take, seen from `from`: the step rule, the
        // room gap against every room placed so far, at least one free
        // L-route. Enumerated in the field's (y, x) order so a draw index
        // means the same on every compiler. `prev` is the room before `from`
        // (nullptr at the entrance and for pockets): candidates heading back
        // toward it are dropped unless they are all there is.
        std::vector<StepCandidate> StepCandidates(Field const& f, Cell const& from, Cell const* prev)
        {
            std::vector<StepCandidate> forward;
            std::vector<StepCandidate> backward;
            for (int y = 0; y < f.size; ++y)
            {
                for (int x = 0; x < f.size; ++x)
                {
                    Cell c;
                    c.x = x;
                    c.y = y;
                    int const d = Manhattan(from, c);
                    if (d < STEP_MIN || d > STEP_MAX || !f.Free(c))
                    {
                        continue;
                    }
                    bool gapOk = true;
                    for (Cell const& r : f.rooms)
                    {
                        if (Manhattan(r, c) < MIN_ROOM_GAP)
                        {
                            gapOk = false;
                            break;
                        }
                    }
                    if (!gapOk)
                    {
                        continue;
                    }
                    StepCandidate cand;
                    cand.cell = c;
                    cand.orders = FeasibleOrders(f, from, c);
                    if (cand.orders == ORDER_NONE)
                    {
                        continue;
                    }
                    bool reversal = false;
                    if (prev)
                    {
                        int const px = from.x - prev->x;
                        int const py = from.y - prev->y;
                        int const sx = c.x - from.x;
                        int const sy = c.y - from.y;
                        reversal = (px * sx + py * sy) < 0;
                    }
                    (reversal ? backward : forward).push_back(cand);
                }
            }
            return forward.empty() ? backward : forward;
        }

        // Commits per attempt before the search gives up on this seed.
        int const CHAIN_BUDGET = 4000;

        struct Pocket
        {
            Cell cell;
            int host = -1;          // chain index
            int shortcutTo = -1;    // chain index, -1 = dead end
        };

        bool IsBossIndex(std::vector<int> const& bosses, int idx)
        {
            return std::find(bosses.begin(), bosses.end(), idx) != bosses.end();
        }

        int NextBossAfter(std::vector<int> const& bosses, int idx)
        {
            for (int b : bosses)        // ascending by construction
            {
                if (b > idx)
                {
                    return b;
                }
            }
            return -1;
        }

        // Pockets hang off ordinary spine rooms, one each, placed like a chain
        // step without the direction bias. A pocket may carry a shortcut to a
        // later spine room of its own segment (never onto or past a boss -
        // the boss room must stay a cut of the graph for B4's barrier).
        bool PlacePockets(BlockCfg const& cfg, PDRandom& rng, std::vector<int> const& bosses,
                          int pockets, Field& f, std::vector<Pocket>& out)
        {
            int const chainLen = static_cast<int>(f.chain.size());
            std::vector<bool> hosted(static_cast<size_t>(chainLen), false);
            for (int p = 0; p < pockets; ++p)
            {
                std::vector<int> hosts;
                for (int i = 1; i < chainLen - 1; ++i)
                {
                    if (IsBossIndex(bosses, i) || hosted[static_cast<size_t>(i)])
                    {
                        continue;
                    }
                    if (StepCandidates(f, f.chain[static_cast<size_t>(i)], nullptr).empty())
                    {
                        continue;
                    }
                    hosts.push_back(i);
                }
                if (hosts.empty())
                {
                    return false;
                }
                int const host = hosts[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(hosts.size()) - 1))];
                Cell const from = f.chain[static_cast<size_t>(host)];
                std::vector<StepCandidate> const cands = StepCandidates(f, from, nullptr);
                StepCandidate const cand = cands[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(cands.size()) - 1))];
                CommitRoute(f, from, cand.cell, ChooseXFirst(rng, cand.orders));
                f.occ[f.Index(cand.cell)] = 1;
                f.rooms.push_back(cand.cell);
                hosted[static_cast<size_t>(host)] = true;

                Pocket pocket;
                pocket.cell = cand.cell;
                pocket.host = host;

                if (rng.Chance(cfg.loopChancePct))
                {
                    int const segmentBoss = NextBossAfter(bosses, host);
                    std::vector<std::pair<int, int>> targets;      // (chain index, orders)
                    for (int j = host + 1; j < segmentBoss; ++j)
                    {
                        int const orders = FeasibleOrders(f, cand.cell, f.chain[static_cast<size_t>(j)]);
                        if (orders != ORDER_NONE)
                        {
                            targets.push_back(std::make_pair(j, orders));
                        }
                    }
                    if (!targets.empty())
                    {
                        std::pair<int, int> const target = targets[static_cast<size_t>(
                            rng.UniformInt(0, static_cast<int>(targets.size()) - 1))];
                        CommitRoute(f, cand.cell, f.chain[static_cast<size_t>(target.first)],
                                    ChooseXFirst(rng, target.second));
                        pocket.shortcutTo = target.first;
                    }
                }
                out.push_back(pocket);
            }
            return true;
        }

        // What the search is for: the spine length, the pockets that must
        // fit around it, and the boss positions the pocket hosts avoid.
        struct ChainGoal
        {
            BlockCfg const* cfg = nullptr;
            std::vector<int> const* bosses = nullptr;
            int chainLen = 0;
            int pockets = 0;
        };

        // Depth-first over chain positions. Every commit draws; a failed
        // subtree removes the drawn candidate and draws again from what is
        // left, so the draw sequence is a pure function of the seed whatever
        // path the search takes. Every level checks the budget, so an
        // exhausted attempt unwinds at once.
        //
        // The pockets are seated HERE, once the spine is complete, and a
        // spine they do not fit around is treated like any other dead end of
        // the search - the level above draws its next candidate. Measured
        // 2026-09-02: seating them after the search instead failed ~55 % of
        // single attempts at the live default (5 rooms on a 5x5 field).
        bool ExtendChain(PDRandom& rng, ChainGoal const& goal, Field& f,
                         std::vector<Pocket>& pocketsOut, int& budget)
        {
            if (static_cast<int>(f.chain.size()) == goal.chainLen)
            {
                Field seated = f;
                std::vector<Pocket> placed;
                if (!PlacePockets(*goal.cfg, rng, *goal.bosses, goal.pockets, seated, placed))
                {
                    return false;
                }
                f = seated;
                pocketsOut = placed;
                return true;
            }
            Cell const from = f.chain.back();
            Cell const* prev = f.chain.size() >= 2 ? &f.chain[f.chain.size() - 2] : nullptr;
            std::vector<StepCandidate> cands = StepCandidates(f, from, prev);
            while (!cands.empty())
            {
                if (budget <= 0)
                {
                    return false;
                }
                --budget;
                size_t const pick = static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(cands.size()) - 1));
                StepCandidate const cand = cands[pick];

                Field next = f;
                CommitRoute(next, from, cand.cell, ChooseXFirst(rng, cand.orders));
                next.occ[next.Index(cand.cell)] = 1;
                next.rooms.push_back(cand.cell);
                next.chain.push_back(cand.cell);
                if (ExtendChain(rng, goal, next, pocketsOut, budget))
                {
                    f = next;
                    return true;
                }
                cands.erase(cands.begin() + static_cast<std::ptrdiff_t>(pick));
            }
            return false;
        }
    }

    int AltCountFor(BlockRole role)
    {
        // Mirrors ALT_COUNT in 48_gen_t1_blockkit.py: rooms and straight
        // corridors ship a second look (blob outline / S-curve), everything
        // else has exactly one. The harness proves every combination against
        // the shipped chunk-meta SQL, which is what keeps this table honest.
        switch (role)
        {
            case BlockRole::Room:
            case BlockRole::RoomEntrance:
            case BlockRole::RoomBoss:
            case BlockRole::CorridorStraight:
                return 2;
            default:
                return 1;
        }
    }

    int PocketCountFor(int rooms, int bossRooms, int branches)
    {
        int const total = std::max(2, rooms + bossRooms);
        int const bosses = bossRooms > 0 ? bossRooms : 1;
        int pockets = std::max(0, branches);
        pockets = std::min(pockets, total / 3);
        // Host clamp: one pocket per spine room that is neither the entrance
        // nor a boss, so pockets <= (total - pockets) - 1 - bosses.
        pockets = std::min(pockets, std::max(0, (total - 1 - bosses) / 2));
        return pockets;
    }

    int BossChainIndex(int chainLen, int bossRooms, int k)
    {
        int const bosses = bossRooms > 0 ? bossRooms : 1;
        // round(k * (L - 1) / N) in integers, half up.
        return (2 * k * (chainLen - 1) + bosses) / (2 * bosses);
    }

    int ChainLength(BlockPlan const& plan)
    {
        int len = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            len = std::max(len, b.chainIndex + 1);
        }
        return len;
    }

    int SegmentOf(BlockPlan const& plan, PlacedBlock const& block)
    {
        int const idx = block.chainIndex >= 0 ? block.chainIndex : block.branchOf;
        if (idx < 0)
        {
            return -1;
        }
        if (idx == 0)
        {
            return 0;
        }
        int const len = ChainLength(plan);
        int const bosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;
        for (int k = 1; k <= bosses; ++k)
        {
            if (idx <= BossChainIndex(len, plan.config.bossRooms, k))
            {
                return k;
            }
        }
        return bosses;
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
            if (b.role == BlockRole::CorridorDeadEnd)
            {
                // The one corridor allowed a single socket - that is its
                // whole definition. Anything else stays under the old rule.
                if (PopCount(b.socketMask) != 1)
                {
                    return fail("dead-end block without exactly one socket");
                }
            }
            else if (!isRoom && PopCount(b.socketMask) < 2)
            {
                return fail("corridor block with fewer than two sockets");
            }
            if (b.alt < 0 || b.alt >= AltCountFor(b.role))
            {
                return fail("alt outside the role's alternate count");
            }
            if (ThemeChunkIdBase(plan.config.theme) == 0)
            {
                return fail("unknown theme - no kit namespace for it");
            }
            if (b.chunkId != ChunkIdFor(plan.config.theme, b.role, b.socketMask, b.alt))
            {
                return fail("chunkId does not match theme, role, mask and alt");
            }
        }

        // Round B: the spine (spec 2026-09-02 §5). Chain indices are exactly
        // 0..L-1 once each, the entrance is chain 0, the last chain room is a
        // boss, bosses sit at their formula positions, pockets hang off
        // ordinary spine rooms (one each) and a shortcut lands forward on a
        // non-boss room of the same segment.
        std::vector<int> chainBlock;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            PlacedBlock const& b = plan.blocks[i];
            if (b.chainIndex < 0)
            {
                continue;
            }
            if (b.roomId < 0)
            {
                return fail("a corridor block carries a chain index");
            }
            // Bounded BEFORE the resize: the chain can never be longer than
            // the block list, so a garbage index must not be allowed to
            // allocate a vector sized by whatever happened to be in memory.
            if (static_cast<size_t>(b.chainIndex) >= plan.blocks.size())
            {
                return fail("a chain index is outside the block list");
            }
            if (static_cast<size_t>(b.chainIndex) >= chainBlock.size())
            {
                chainBlock.resize(static_cast<size_t>(b.chainIndex) + 1, -1);
            }
            if (chainBlock[static_cast<size_t>(b.chainIndex)] != -1)
            {
                return fail("two blocks share a chain index");
            }
            chainBlock[static_cast<size_t>(b.chainIndex)] = static_cast<int>(i);
        }
        if (chainBlock.size() < 2)
        {
            return fail("the chain has fewer than two rooms");
        }
        for (int idx : chainBlock)
        {
            if (idx < 0)
            {
                return fail("a chain index is missing");
            }
        }
        int const chainLen = static_cast<int>(chainBlock.size());
        int const wantBosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;
        if (plan.entranceIndex != chainBlock[0] ||
            plan.blocks[static_cast<size_t>(chainBlock[0])].role != BlockRole::RoomEntrance)
        {
            return fail("chain 0 is not the entrance");
        }
        if (plan.bossIndex != chainBlock[static_cast<size_t>(chainLen - 1)] ||
            plan.blocks[static_cast<size_t>(plan.bossIndex)].role != BlockRole::RoomBoss)
        {
            return fail("bossIndex is not the last chain room, or it is not a boss");
        }
        int bossCount = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.role == BlockRole::RoomBoss)
            {
                ++bossCount;
                if (b.chainIndex < 1)
                {
                    return fail("a boss room is off the spine");
                }
            }
        }
        if (bossCount != wantBosses)
        {
            return fail("boss room count does not match the config");
        }
        std::vector<bool> bossPos(static_cast<size_t>(chainLen), false);
        for (int k = 1; k <= wantBosses; ++k)
        {
            int const at = BossChainIndex(chainLen, plan.config.bossRooms, k);
            if (at < 1 || at >= chainLen ||
                plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(at)])].role != BlockRole::RoomBoss)
            {
                return fail("a boss room is off its formula position");
            }
            bossPos[static_cast<size_t>(at)] = true;
        }
        // Every other spine position is an ordinary room. Chain 0 is the
        // entrance (checked above) and the last chain room is a boss, so the
        // interior is the only place a wrong role could hide - a second
        // RoomEntrance at chain 3 used to validate, and B1's altar cadence and
        // B4's barrier both read the role, not just the index.
        for (int idx = 1; idx < chainLen - 1; ++idx)
        {
            if (bossPos[static_cast<size_t>(idx)])
            {
                continue;
            }
            if (plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(idx)])].role != BlockRole::Room)
            {
                return fail("a spine room carries the wrong role");
            }
        }
        std::vector<bool> hosted(static_cast<size_t>(chainLen), false);
        int pocketCount = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId < 0 || b.chainIndex >= 0)
            {
                continue;
            }
            if (b.branchOf < 0)
            {
                return fail("a room is neither on the chain nor a pocket");
            }
            ++pocketCount;
            if (b.role != BlockRole::Room)
            {
                return fail("a pocket room carries the wrong role");
            }
            if (b.branchOf < 1 || b.branchOf >= chainLen - 1 ||
                plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.branchOf)])].role != BlockRole::Room)
            {
                return fail("a pocket hangs off the entrance, a boss or nothing");
            }
            if (hosted[static_cast<size_t>(b.branchOf)])
            {
                return fail("two pockets on one host");
            }
            hosted[static_cast<size_t>(b.branchOf)] = true;
            if (b.shortcutTo >= 0)
            {
                if (b.shortcutTo <= b.branchOf || b.shortcutTo >= chainLen)
                {
                    return fail("a shortcut does not lead forward");
                }
                for (int j = b.branchOf + 1; j <= b.shortcutTo; ++j)
                {
                    if (plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(j)])].role == BlockRole::RoomBoss)
                    {
                        return fail("a shortcut crosses or lands on a boss room");
                    }
                }
            }
        }
        if (pocketCount != PocketCountFor(plan.config.rooms, plan.config.bossRooms, plan.config.branches))
        {
            return fail("pocket count does not match the config");
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

        // The coordinate index the corridor walk and the floods below share.
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }

        // Which room a socket of `from` leads to, walking the corridor run
        // behind it: -1 for a run that ends in a chest stub (or nowhere),
        // otherwise the block index of the first room reached. Corridor
        // blocks on the way must have exactly two non-stub sockets - the
        // junction rule - or the walk reports the junction.
        auto roomAtEndOf = [&](size_t from, unsigned bit, bool& junction) -> int
        {
            junction = false;
            size_t prev = from;
            unsigned entryBit = bit;
            // A run cannot be longer than the block list; the bound turns a
            // corridor cycle (which the junction rule forbids, but which a
            // future generator bug could still hand us) into a rejection
            // rather than a hang inside the engine.
            for (size_t steps = 0; steps <= plan.blocks.size(); ++steps)
            {
                int dx = 0, dy = 0;
                StepFor(entryBit, dx, dy);
                auto it = index.find(std::make_pair(plan.blocks[prev].bx + dx,
                                                    plan.blocks[prev].by + dy));
                if (it == index.end())
                {
                    return -1;
                }
                size_t const at = it->second;
                PlacedBlock const& b = plan.blocks[at];
                if (b.roomId >= 0)
                {
                    return static_cast<int>(at);
                }
                if (b.role == BlockRole::CorridorDeadEnd)
                {
                    return -1;      // the run ends in a chest stub
                }
                // Leave through the one socket that is neither the way in nor
                // a stub hanging off this corridor block.
                unsigned const cameFrom = OppositeBit(entryBit);
                unsigned next = 0;
                int outs = 0;
                for (unsigned side = 1; side <= SOCKET_W; side <<= 1)
                {
                    if (!(b.socketMask & side) || side == cameFrom)
                    {
                        continue;
                    }
                    int nx = 0, ny = 0;
                    StepFor(side, nx, ny);
                    auto n = index.find(std::make_pair(b.bx + nx, b.by + ny));
                    if (n != index.end() &&
                        plan.blocks[n->second].role == BlockRole::CorridorDeadEnd)
                    {
                        continue;
                    }
                    ++outs;
                    next = side;
                }
                if (outs != 1)
                {
                    junction = true;
                    return -1;
                }
                prev = at;
                entryBit = next;
            }
            junction = true;
            return -1;
        };

        // Round B, the physical half of the spine rules (final review of B0,
        // item I2). Everything above reads the DECLARED chainIndex / branchOf /
        // shortcutTo; these three rules prove them against the sockets. A
        // pocket labelled into segment k but physically hanging off a room
        // behind boss k passes both cut floods and would put its spawns into
        // the wrong barrier denominator - a softlock B3 could ship.
        //
        // 1. Junction rule: no corridor block forks. Exactly two of its
        //    sockets lead to non-stub blocks, so a corridor run is a path.
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0 || b.role == BlockRole::CorridorDeadEnd)
            {
                continue;
            }
            int through = 0;
            for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
            {
                if (!(b.socketMask & bit))
                {
                    continue;
                }
                int dx = 0, dy = 0;
                StepFor(bit, dx, dy);
                auto it = index.find(std::make_pair(b.bx + dx, b.by + dy));
                if (it != index.end() &&
                    plan.blocks[it->second].role != BlockRole::CorridorDeadEnd)
                {
                    ++through;
                }
            }
            if (through != 2)
            {
                return fail("a corridor block is a junction");
            }
        }

        // 2. Spine adjacency: consecutive chain rooms are joined by exactly
        //    one corridor run, which is what makes the chain order physical
        //    rather than a label (B1's altar cadence, B5's patrol).
        for (int i = 1; i < chainLen; ++i)
        {
            size_t const from = static_cast<size_t>(chainBlock[static_cast<size_t>(i - 1)]);
            int hits = 0;
            for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
            {
                if (!(plan.blocks[from].socketMask & bit))
                {
                    continue;
                }
                bool junction = false;
                int const to = roomAtEndOf(from, bit, junction);
                if (junction)
                {
                    return fail("a corridor block is a junction");
                }
                if (to == chainBlock[static_cast<size_t>(i)])
                {
                    ++hits;
                }
            }
            if (hits != 1)
            {
                return fail("consecutive chain rooms are not joined by one corridor run");
            }
        }

        // 3. Pocket physics: the rooms a pocket's corridors actually reach are
        //    exactly its declared host (once) and, when it declares one, its
        //    shortcut target (once). Stub runs are ignored; anything else is a
        //    corridor the plan does not admit to.
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            PlacedBlock const& b = plan.blocks[i];
            if (b.roomId < 0 || b.chainIndex >= 0)
            {
                continue;
            }
            int hostHits = 0;
            int targetHits = 0;
            int others = 0;
            for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
            {
                if (!(b.socketMask & bit))
                {
                    continue;
                }
                bool junction = false;
                int const to = roomAtEndOf(i, bit, junction);
                if (junction)
                {
                    return fail("a corridor block is a junction");
                }
                if (to < 0)
                {
                    continue;
                }
                if (to == chainBlock[static_cast<size_t>(b.branchOf)])
                {
                    ++hostHits;
                }
                else if (b.shortcutTo >= 0 &&
                         to == chainBlock[static_cast<size_t>(b.shortcutTo)])
                {
                    ++targetHits;
                }
                else
                {
                    ++others;
                }
            }
            if (hostHits != 1 || others != 0 ||
                targetHits != (b.shortcutTo >= 0 ? 1 : 0))
            {
                return fail("a pocket's corridors do not match its declared host and shortcut");
            }
        }

        // 4. Chain length: the spine holds the whole room budget minus the
        //    pockets. Without this a 3-room chain with the bosses at the
        //    formula positions for L = 3 validates against a 4-room config.
        if (chainLen != std::max(2, plan.config.rooms + plan.config.bossRooms) - pocketCount)
        {
            return fail("chain length does not match the room budget");
        }

        // Connectivity: every block must be reachable from the entrance through
        // open sockets, or part of the dungeon is unplayable. The same flood,
        // run once more per boss room with that room removed, proves the boss
        // cut property: nothing behind a boss is reachable around it, so B4's
        // barrier on its doorway is a real gate.
        auto flood = [&](int skipBlock, std::vector<bool>& visited)
        {
            visited.assign(plan.blocks.size(), false);
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
                    if (static_cast<int>(it->second) == skipBlock) continue;
                    if (visited[it->second]) continue;
                    visited[it->second] = true;
                    ++reached;
                    q.push(it->second);
                }
            }
            return reached;
        };

        std::vector<bool> visited;
        if (flood(-1, visited) != plan.blocks.size())
        {
            return fail("some blocks are unreachable from the entrance");
        }
        for (int idx = 1; idx < chainLen; ++idx)
        {
            int const bossBlock = chainBlock[static_cast<size_t>(idx)];
            if (plan.blocks[static_cast<size_t>(bossBlock)].role != BlockRole::RoomBoss)
            {
                continue;
            }
            flood(bossBlock, visited);
            for (size_t i = 0; i < plan.blocks.size(); ++i)
            {
                PlacedBlock const& b = plan.blocks[i];
                if (visited[i] && (b.chainIndex > idx || b.branchOf > idx))
                {
                    return fail("a boss room can be bypassed");
                }
            }
        }
        return true;
    }

    bool GenerateBlockPlan(BlockCfg const& cfg, BlockPlan* out)
    {
        if (!out || cfg.fieldBlocks < 2)
        {
            return false;
        }

        // Round B (spec 2026-09-02): the budget is arithmetic, not a draw.
        int const total = std::max(2, cfg.rooms + cfg.bossRooms);
        int const bosses = cfg.bossRooms > 0 ? cfg.bossRooms : 1;
        int const pocketsWanted = PocketCountFor(cfg.rooms, cfg.bossRooms, cfg.branches);
        int const chainLen = total - pocketsWanted;
        if (chainLen - 1 < bosses)
        {
            return false;       // cannot seat N distinct bosses on the spine
        }
        std::vector<int> bossIdx;
        for (int k = 1; k <= bosses; ++k)
        {
            bossIdx.push_back(BossChainIndex(chainLen, cfg.bossRooms, k));
        }

        // DRAW ORDER (the contract every stored seed depends on; the layout
        // version is bumped when it changes):
        //   1. start cell: x then y                     (2 draws)
        //   2. each chain step: a candidate index, then the axis coin only if
        //      both L-orders are open; backtracking re-draws from the
        //      shrunken list (ExtendChain)
        //   3. once the spine is complete, the pockets, still inside the
        //      search: per pocket a host index, a candidate index, the axis
        //      coin (if both), the shortcut Chance(loopChancePct), then a
        //      target index and axis coin only when the Chance hit AND a
        //      target exists. Pockets that do not fit unwind the search, and
        //      the draws simply continue from wherever it lands
        //   4. dead-end stubs: count, then one index per stub (a placed stub
        //      is never a host, since Round B)
        //   5. visual alternates, one per multi-alt block, last (unchanged)
        // Nothing else draws. Theme moves no draw.
        //
        // Three properties of PDRandom the items above lean on (PDRandom.h:41-68):
        //   - a single candidate costs NO draw. UniformInt(lo, hi) returns lo
        //     without touching the stream when lo >= hi, so a one-element
        //     candidate list, a single eligible host and a single feasible
        //     L-route are all free. That is why "a candidate index" is not the
        //     same as "a draw" in items 2 and 3.
        //   - Chance(pct) draws nothing at pct <= 0 and pct >= 100, so
        //     V2.LoopChance 0 and 100 sit on a DIFFERENT stream from 1..99,
        //     not merely on a different outcome.
        //   - a failed base case does not unwind exactly one step. It keeps
        //     unwinding for as long as the level above has no candidate left,
        //     so a failure deep in the chain can return the search several
        //     positions - and the stream simply continues from there, since
        //     every abandoned level's draws have already been consumed.
        for (int attempt = 0; attempt < cfg.maxTries; ++attempt)
        {
            uint32_t const seed = cfg.seed + static_cast<uint32_t>(attempt);
            PDRandom rng(seed);

            Field field(cfg.fieldBlocks);
            Cell start;
            start.x = rng.UniformInt(0, cfg.fieldBlocks - 1);
            start.y = rng.UniformInt(0, cfg.fieldBlocks - 1);
            field.occ[field.Index(start)] = 1;
            field.rooms.push_back(start);
            field.chain.push_back(start);

            ChainGoal goal;
            goal.cfg = &cfg;
            goal.bosses = &bossIdx;
            goal.chainLen = chainLen;
            goal.pockets = pocketsWanted;

            int budget = CHAIN_BUDGET;
            std::vector<Pocket> pockets;
            if (!ExtendChain(rng, goal, field, pockets, budget))
            {
                continue;       // no spine with seated pockets within the budget - next seed
            }

            // Hand over to the ordered (y, x) map the rest of the pipeline has
            // always worked on: the stub pass, the depth BFS and the
            // materialisation all iterate it, which is what keeps the block
            // order and the alt draws reproducible.
            std::map<Cell, unsigned> masks;
            std::map<Cell, int> roomOf;         // cell -> room id, rooms only
            std::map<Cell, int> chainOf;        // cell -> chain index, spine only
            std::map<Cell, size_t> pocketOf;    // cell -> index into `pockets`
            for (int y = 0; y < field.size; ++y)
            {
                for (int x = 0; x < field.size; ++x)
                {
                    Cell c;
                    c.x = x;
                    c.y = y;
                    if (field.occ[field.Index(c)] != 0)
                    {
                        masks[c] = field.masks[field.Index(c)];
                    }
                }
            }
            for (size_t i = 0; i < field.chain.size(); ++i)
            {
                roomOf[field.chain[i]] = static_cast<int>(i);
                chainOf[field.chain[i]] = static_cast<int>(i);
            }
            for (size_t p = 0; p < pockets.size(); ++p)
            {
                roomOf[pockets[p].cell] = chainLen + static_cast<int>(p);
                pocketOf[pockets[p].cell] = p;
            }

            // Dead-end stubs, AFTER every routing draw: the whole layout up to
            // here consumes exactly the draws it consumed before, so the stub
            // pass is additive to the stream, never a reshuffle.
            // A stub is one extra block hanging off an existing cell through a
            // socket the host did not have - a side passage worth peeking into
            // (the kit puts a chest there and no spawns).
            if (cfg.maxDeadEnds > 0)
            {
                int const wantStubs = rng.UniformInt(0, cfg.maxDeadEnds);
                // A stub is ONE block. A placed stub claims its cell but never
                // becomes a host itself: a chain of two stubs would give the
                // block they hang off a third socket leading to a non-stub
                // block, and would leave the middle stub with two sockets - the
                // corridor junction Round B's spine rules out (spec 2026-09-02
                // §7.1). Before Round B the layout was full of junctions
                // anyway, so the pass could host stubs on stubs.
                std::set<Cell> stubs;
                for (int placedStubs = 0; placedStubs < wantStubs; ++placedStubs)
                {
                    // Candidates recomputed per stub over the ordered map, so
                    // a placed stub claims its cell for the next round; the
                    // order is the map's own (y, x) order.
                    std::vector<std::pair<Cell, unsigned>> candidates;
                    for (auto const& kv : masks)
                    {
                        if (stubs.find(kv.first) != stubs.end())
                        {
                            continue;   // one block per stub, never a chain
                        }
                        for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                        {
                            if (kv.second & bit)
                            {
                                continue;   // that side already leads somewhere
                            }
                            int dx = 0, dy = 0;
                            StepFor(bit, dx, dy);
                            Cell n;
                            n.x = kv.first.x + dx;
                            n.y = kv.first.y + dy;
                            if (n.x < 0 || n.y < 0 || n.x >= cfg.fieldBlocks ||
                                n.y >= cfg.fieldBlocks)
                            {
                                continue;
                            }
                            if (masks.find(n) != masks.end())
                            {
                                continue;   // occupied - that would be a loop, not a stub
                            }
                            candidates.push_back(std::make_pair(kv.first, bit));
                        }
                    }
                    if (candidates.empty())
                    {
                        break;
                    }
                    auto const& pick = candidates[static_cast<size_t>(
                        rng.UniformInt(0, static_cast<int>(candidates.size()) - 1))];
                    int dx = 0, dy = 0;
                    StepFor(pick.second, dx, dy);
                    Cell stub;
                    stub.x = pick.first.x + dx;
                    stub.y = pick.first.y + dy;
                    masks[pick.first] |= pick.second;
                    masks[stub] = OppositeBit(pick.second);
                    stubs.insert(stub);
                }
            }

            // Depth over the block graph from the entrance (chain 0). Nothing
            // downstream reads it today; it stays the BFS depth it always was.
            std::map<Cell, int> depth;
            std::queue<Cell> q;
            Cell const entranceCell = field.chain[0];
            depth[entranceCell] = 0;
            q.push(entranceCell);
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
                    auto cit = chainOf.find(c);
                    if (cit != chainOf.end())
                    {
                        b.chainIndex = cit->second;
                        b.role = (cit->second == 0)             ? BlockRole::RoomEntrance
                               : IsBossIndex(bossIdx, cit->second) ? BlockRole::RoomBoss
                                                                   : BlockRole::Room;
                    }
                    else
                    {
                        Pocket const& pocket = pockets[pocketOf[c]];
                        b.role = BlockRole::Room;
                        b.branchOf = pocket.host;
                        b.shortcutTo = pocket.shortcutTo;
                    }
                }
                else
                {
                    b.role = CorridorRoleFor(mask);
                }

                auto dit = depth.find(c);
                b.depth = (dit == depth.end()) ? -1 : dit->second;

                // Visual alternate, drawn LAST of all draws (stubs included)
                // and only where the kit ships more than one look - a
                // single-variant family must not consume a draw, or adding an
                // alt to one role would reshuffle every other block's choice.
                int const altCount = AltCountFor(b.role);
                b.alt = altCount > 1 ? rng.UniformInt(0, altCount - 1) : 0;
                // The theme moves only the id BASE, never a draw: the same
                // seed lays out the same dungeon in every theme, and stored
                // layouts stay draw-stable across a theme config change.
                b.chunkId = ChunkIdFor(cfg.theme, b.role, b.socketMask, b.alt);

                if (b.role == BlockRole::RoomEntrance)
                {
                    plan.entranceIndex = static_cast<int>(plan.blocks.size());
                }
                else if (b.chainIndex == chainLen - 1)
                {
                    // The END of the dungeon: the last chain room, always a
                    // boss. The manifest, the harness and the HUD read
                    // bossIndex as the landmark the run finishes at.
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
                    case BlockRole::Room:         out += (b->branchOf >= 0) ? 'r' : 'R'; break;
                    case BlockRole::CorridorDeadEnd: out += 'D'; break;
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
