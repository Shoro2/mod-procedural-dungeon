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

// Standalone verification for the PDv2 block planner. No AzerothCore, no
// worldserver: the planner is engine-free on purpose so a layout can be checked
// long before anything is deployed.
//
//   pdblock <seed> [rooms]               one layout: ASCII map + FLPD2 manifest
//   pdblock --batch <n> [rooms]          invariants + determinism over n seeds
//   pdblock --roomcap [n]                the 01 §8 room-cap measurement table
//                                        that decides PD_GAME_ROOMS_CAP_MEASURED
//   pdblock --manifest <seed> <file> [rooms]
//                                        writes the manifest as raw bytes, for
//                                        feeding to 49_pd_compose_blocks.py
//
// Build:
//   cl /std:c++17 /EHsc /W4 /O2 /I src tests\blockplan_harness.cpp
//      src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp
//      /Fe:pdblock.exe

// MSVC deprecates std::fopen in favour of fopen_s, which is a Microsoft
// extension. This harness is also expected to build with g++ (see CLAUDE.md),
// so the portable call stays and the warning is turned off for this file only.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "generator/PDBlockPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2LinkState.h"
#include "generator/PDv2WalkGrid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

using namespace PDungeon;

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool ok, char const* what, uint32_t seed)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_failures;
            std::printf("  FAIL seed %u: %s\n", seed, what);
        }
    }

    BlockCfg MakeCfg(uint32_t seed, int rooms, int originBX = 32 * 8,
                     int originBY = 32 * 8)
    {
        BlockCfg cfg;
        cfg.seed = seed;
        cfg.rooms = rooms;
        cfg.bossRooms = 1;
        // 8 blocks square is exactly one ADT tile, which is the case that
        // matters: a dungeon of this size composes into a single tile.
        cfg.fieldBlocks = 8;
        // The origin decides which tile the layout lands on: tx = bx / 8. It is
        // settable so a layout can be aimed at a tile a real map already has,
        // which is what makes it viewable before map 760 exists.
        cfg.originBX = originBX;
        cfg.originBY = originBY;
        return cfg;
    }

    // Re-derives the socket agreement from scratch rather than trusting the
    // planner's own validator, so a bug in that validator cannot hide here.
    bool EdgesAgree(BlockPlan const& plan, std::string* why)
    {
        std::map<std::pair<int, int>, unsigned> mask;
        for (PlacedBlock const& b : plan.blocks)
        {
            mask[std::make_pair(b.bx, b.by)] = b.socketMask;
        }
        struct Dir { unsigned bit; int dx; int dy; unsigned opp; };
        Dir const dirs[4] = {
            { SOCKET_N,  0, -1, SOCKET_S },
            { SOCKET_S,  0,  1, SOCKET_N },
            { SOCKET_W, -1,  0, SOCKET_E },
            { SOCKET_E,  1,  0, SOCKET_W },
        };
        for (auto const& kv : mask)
        {
            for (Dir const& d : dirs)
            {
                if (!(kv.second & d.bit)) continue;
                auto it = mask.find(std::make_pair(kv.first.first + d.dx, kv.first.second + d.dy));
                if (it == mask.end())
                {
                    *why = "socket opens onto nothing";
                    return false;
                }
                if (!(it->second & d.opp))
                {
                    *why = "neighbour does not answer the socket";
                    return false;
                }
            }
        }
        return true;
    }

    // --- walk masks, read from the kit's own metadata -----------------------
    //
    // The masks come from the kit rather than being recomputed here on purpose:
    // the kit generator is the authority on what its blocks look like, and a
    // second implementation of that geometry would drift from it silently.
    // On the server the same data arrives from `pdungeon_chunk_meta`; here it is
    // parsed out of the SQL the kit generator emits alongside the ADTs.
    std::map<int, std::vector<uint8_t>> g_masks;

    bool LoadMasks(char const* sqlPath)
    {
        FILE* fh = std::fopen(sqlPath, "rb");
        if (!fh)
        {
            std::printf("  cannot open %s\n", sqlPath);
            return false;
        }
        std::string blob;
        char buf[8192];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof(buf), fh)) > 0)
        {
            blob.append(buf, got);
        }
        std::fclose(fh);

        // Rows look like:  (2005, @KIT, 1, 'room', 5, 'RLE1:â€¦', '{â€¦}')
        size_t at = 0;
        while (true)
        {
            size_t const open = blob.find("\n    (", at);
            if (open == std::string::npos) break;
            size_t const idStart = open + 6;
            int const chunkId = std::atoi(blob.substr(idStart, 12).c_str());

            size_t const rle = blob.find("'RLE1:", idStart);
            if (rle == std::string::npos) break;
            size_t const rleEnd = blob.find('\'', rle + 1);
            if (rleEnd == std::string::npos) break;

            std::vector<uint8_t> mask;
            if (DecodeWalkMaskRle(blob.substr(rle + 1, rleEnd - rle - 1), mask) &&
                mask.size() == PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK)
            {
                g_masks[chunkId] = mask;
            }
            at = rleEnd;
        }
        std::printf("  %u walk mask(s) from %s\n",
                    static_cast<unsigned>(g_masks.size()), sqlPath);
        return !g_masks.empty();
    }

    uint8_t const* MaskFor(int chunkId)
    {
        auto it = g_masks.find(chunkId);
        return it == g_masks.end() ? nullptr : it->second.data();
    }

    void PrintOne(uint32_t seed, int rooms)
    {
        BlockCfg const cfg = MakeCfg(seed, rooms);
        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            std::printf("generation FAILED for seed %u\n", seed);
            return;
        }

        int roomCount = 0;
        int corridorCount = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0) ++roomCount; else ++corridorCount;
        }

        std::printf("seed %u (effective %u): %d blocks = %d rooms + %d corridors\n",
                    seed, plan.effectiveSeed, static_cast<int>(plan.blocks.size()),
                    roomCount, corridorCount);
        std::printf("E = entrance, B = boss, R = room, | - + = corridor\n\n");
        std::printf("%s\n", AsciiBlockDump(plan).c_str());

        std::string const manifest = EmitManifest(plan, 1);
        std::printf("manifest, %d bytes of the 2048 budget:\n\n%s\n",
                    static_cast<int>(manifest.size()), manifest.c_str());
    }

    // Writes to a FILE, in binary, deliberately. A manifest is LF separated and
    // the parsers on both sides reject CR, but stdout on Windows is a text
    // stream that rewrites every \n into \r\n -- so piping this through a shell
    // would corrupt it in a way that only shows up as a parse error much later.
    void WriteManifest(uint32_t seed, int rooms, char const* path, int obx, int oby)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(seed, rooms, obx, oby), &plan))
        {
            std::fprintf(stderr, "generation failed\n");
            std::exit(2);
        }
        std::string const m = EmitManifest(plan, 1);

        FILE* fh = std::fopen(path, "wb");
        if (!fh)
        {
            std::fprintf(stderr, "cannot write %s\n", path);
            std::exit(2);
        }
        std::fwrite(m.data(), 1, m.size(), fh);
        std::fclose(fh);
        std::printf("wrote %s, %d bytes\n", path, static_cast<int>(m.size()));
    }

    // Cell coordinates of a block's centre within the grid.
    GridPoint BlockCentreCell(WalkGrid const& grid, PlacedBlock const& b)
    {
        return { (b.bx - grid.originBX) * PD_CELLS_PER_BLOCK + PD_CELLS_PER_BLOCK / 2,
                 (b.by - grid.originBY) * PD_CELLS_PER_BLOCK + PD_CELLS_PER_BLOCK / 2 };
    }

    // Can a creature standing in any room reach any other? That is the property
    // the dungeon actually needs; a grid that merely exists proves nothing.
    bool CheckAllRoomsConnected(BlockPlan const& plan, WalkGrid const& grid,
                                std::string& why, int& longest)
    {
        std::vector<PlacedBlock const*> rooms;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0)
            {
                rooms.push_back(&b);
            }
        }
        longest = 0;
        for (size_t i = 0; i < rooms.size(); ++i)
        {
            for (size_t j = i + 1; j < rooms.size(); ++j)
            {
                GridPoint a = BlockCentreCell(grid, *rooms[i]);
                GridPoint b = BlockCentreCell(grid, *rooms[j]);
                GridPoint sa, sb;
                if (!NearestWalkable(grid, a.x, a.y, 4, sa) ||
                    !NearestWalkable(grid, b.x, b.y, 4, sb))
                {
                    why = "a room centre has no walkable cell near it";
                    return false;
                }
                std::vector<GridPoint> path;
                if (!FindGridPath(grid, sa, sb, path))
                {
                    why = "no path between two rooms";
                    return false;
                }
                longest = (static_cast<int>(path.size()) > longest)
                              ? static_cast<int>(path.size()) : longest;

                std::vector<GridPoint> simple = path;
                SimplifyGridPath(grid, simple);
                if (simple.size() > path.size())
                {
                    why = "simplification made the path longer";
                    return false;
                }
                for (GridPoint const& p : simple)
                {
                    if (!grid.At(p.x, p.y))
                    {
                        why = "a simplified waypoint is not walkable";
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // The room-connectivity check above walks room centres. This one is
    // stronger: EVERY walkable cell must be one component, because a creature
    // can stand anywhere walkable and PlanApproach promises it a route.
    bool CheckGridOneComponent(WalkGrid const& grid, std::string& why)
    {
        size_t const total = grid.WalkableCount();
        if (total == 0)
        {
            why = "grid has no walkable cells";
            return false;
        }

        GridPoint start{ -1, -1 };
        for (int y = 0; start.x < 0 && y < grid.height; ++y)
        {
            for (int x = 0; x < grid.width; ++x)
            {
                if (grid.At(x, y))
                {
                    start = { x, y };
                    break;
                }
            }
        }

        std::vector<uint8_t> seen(grid.cells.size(), 0);
        std::vector<GridPoint> frontier{ start };
        seen[static_cast<size_t>(start.y) * grid.width + start.x] = 1;
        size_t reached = 1;
        int const dx[4] = { 0, 1, 0, -1 };
        int const dy[4] = { -1, 0, 1, 0 };
        while (!frontier.empty())
        {
            GridPoint const cur = frontier.back();
            frontier.pop_back();
            for (int d = 0; d < 4; ++d)
            {
                int const nx = cur.x + dx[d];
                int const ny = cur.y + dy[d];
                if (!grid.At(nx, ny))
                {
                    continue;
                }
                size_t const idx = static_cast<size_t>(ny) * grid.width + nx;
                if (seen[idx])
                {
                    continue;
                }
                seen[idx] = 1;
                ++reached;
                frontier.push_back({ nx, ny });
            }
        }

        if (reached != total)
        {
            why = "walkable surface is not one component";
            return false;
        }
        return true;
    }

    // Deterministic LCG so sampled cell pairs are identical on every compiler;
    // <random> distributions are not, and the batch must reproduce.
    struct Lcg
    {
        uint32_t s;
        uint32_t Next() { s = s * 1664525u + 1013904223u; return s; }
    };

    // The contract the creature AI stands on: on a connected grid nothing
    // walkable is unreachable, Direct really is a walkable straight line, and
    // every Path segment can be walked with MovePoint(generatePath = false).
    bool CheckApproachPolicy(WalkGrid const& grid, uint32_t seed, std::string& why)
    {
        std::vector<GridPoint> walkable;
        for (int y = 0; y < grid.height; ++y)
        {
            for (int x = 0; x < grid.width; ++x)
            {
                if (grid.At(x, y))
                {
                    walkable.push_back({ x, y });
                }
            }
        }
        if (walkable.empty())
        {
            why = "no walkable cells to sample";
            return false;
        }

        Lcg rng{ seed ^ 0x9E3779B9u };
        std::vector<GridPoint> wp;
        for (int k = 0; k < 8; ++k)
        {
            GridPoint const a = walkable[rng.Next() % walkable.size()];
            GridPoint const b = walkable[rng.Next() % walkable.size()];
            switch (PlanApproach(grid, a, b, 2, wp))
            {
                case ApproachKind::Unreachable:
                    why = "unreachable between two walkable cells of a connected grid";
                    return false;
                case ApproachKind::Direct:
                    if (!GridLineWalkable(grid, a, b))
                    {
                        why = "Direct but the straight line is not walkable";
                        return false;
                    }
                    break;
                case ApproachKind::Path:
                    if (GridLineWalkable(grid, a, b))
                    {
                        why = "Path where Direct was possible";
                        return false;
                    }
                    if (wp.size() < 2 || !(wp.front() == a) || !(wp.back() == b))
                    {
                        why = "path endpoints do not match the (walkable) query";
                        return false;
                    }
                    for (size_t i = 0; i + 1 < wp.size(); ++i)
                    {
                        if (!GridLineWalkable(grid, wp[i], wp[i + 1]))
                        {
                            why = "a path segment cuts across unwalkable cells";
                            return false;
                        }
                    }
                    break;
            }
        }

        // Snapping: querying from an unwalkable cell right next to the surface
        // must still resolve - that is a creature standing a hair off-centre.
        for (GridPoint const& cell : walkable)
        {
            GridPoint const off{ cell.x + 1, cell.y };
            if (grid.At(off.x, off.y))
            {
                continue;
            }
            if (PlanApproach(grid, off, walkable.front(), 2, wp) == ApproachKind::Unreachable)
            {
                why = "snap failed one cell off the walkable surface";
                return false;
            }
            break;
        }

        // And a position nowhere near the layout must refuse instead of
        // inventing a route.
        if (PlanApproach(grid, { -10, -10 }, walkable.front(), 2, wp) != ApproachKind::Unreachable)
        {
            why = "a far off-grid start did not come back Unreachable";
            return false;
        }
        return true;
    }

    // Forward (block-local, the spawn path) and inverse (world -> cell, the AI
    // path) must agree, or creatures would chase mirrored positions. The u/v
    // to row/col pairing below IS the axis mapping - if someone swaps it, this
    // is the check that goes red, because no seam or render test ever would.
    bool CheckWorldMathRoundTrip(BlockPlan const& plan, WalkGrid const& grid,
                                 std::string& why)
    {
        struct Probe { int col; int row; };
        Probe const probes[3] = { { 0, 0 }, { 7, 7 }, { 3, 4 } };

        for (PlacedBlock const& b : plan.blocks)
        {
            for (Probe const& p : probes)
            {
                int const gcx = b.bx * PD_CELLS_PER_BLOCK + p.col;
                int const gcy = b.by * PD_CELLS_PER_BLOCK + p.row;

                double wx = 0.0, wy = 0.0;
                CellCentreToWorld(gcx, gcy, wx, wy);

                int rx = 0, ry = 0;
                WorldToCell(wx, wy, rx, ry);
                if (rx != gcx || ry != gcy)
                {
                    why = "cell -> world -> cell did not round-trip";
                    return false;
                }

                double bx2 = 0.0, by2 = 0.0;
                BlockLocalToWorld(b.bx, b.by,
                                  (p.row + 0.5) * PD_CELL_SIZE_YD,
                                  (p.col + 0.5) * PD_CELL_SIZE_YD, bx2, by2);
                if (std::abs(bx2 - wx) > 1e-6 || std::abs(by2 - wy) > 1e-6)
                {
                    why = "block-local and cell-centre forms disagree (axis swap?)";
                    return false;
                }

                GridPoint const local = grid.LocalFromGlobalCell(gcx, gcy);
                int ggx = 0, ggy = 0;
                grid.GlobalFromLocalCell(local, ggx, ggy);
                if (ggx != gcx || ggy != gcy)
                {
                    why = "grid local<->global cell mapping did not round-trip";
                    return false;
                }
            }
        }
        return true;
    }

    void PrintPath(uint32_t seed, int rooms)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(seed, rooms), &plan))
        {
            std::printf("generation FAILED\n");
            return;
        }

        WalkGrid grid;
        std::string err;
        if (!BuildWalkGrid(plan, MaskFor, &grid, &err))
        {
            std::printf("walk grid FAILED: %s\n", err.c_str());
            return;
        }
        std::printf("grid %dx%d cells, %u walkable (%.1f%%)\n", grid.width, grid.height,
                    static_cast<unsigned>(grid.WalkableCount()),
                    100.0 * grid.WalkableCount() / (grid.width * grid.height));

        PlacedBlock const& entrance = plan.blocks[static_cast<size_t>(plan.entranceIndex)];
        PlacedBlock const& boss = plan.blocks[static_cast<size_t>(plan.bossIndex)];
        GridPoint a, b;
        NearestWalkable(grid, BlockCentreCell(grid, entrance).x,
                        BlockCentreCell(grid, entrance).y, 4, a);
        NearestWalkable(grid, BlockCentreCell(grid, boss).x,
                        BlockCentreCell(grid, boss).y, 4, b);

        std::vector<GridPoint> path;
        if (!FindGridPath(grid, a, b, path))
        {
            std::printf("no path from the entrance to the boss\n");
            return;
        }
        std::vector<GridPoint> simple = path;
        SimplifyGridPath(grid, simple);
        std::printf("entrance -> boss: %u cells, %u waypoints after simplification\n\n",
                    static_cast<unsigned>(path.size()), static_cast<unsigned>(simple.size()));

        std::vector<bool> onPath(grid.cells.size(), false);
        for (GridPoint const& p : path)
        {
            onPath[static_cast<size_t>(p.y) * grid.width + p.x] = true;
        }
        for (int y = 0; y < grid.height; ++y)
        {
            std::string row;
            for (int x = 0; x < grid.width; ++x)
            {
                bool const walk = grid.At(x, y);
                bool const on = onPath[static_cast<size_t>(y) * grid.width + x];
                row += on ? '*' : (walk ? '.' : ' ');
            }
            std::printf("  %s\n", row.c_str());
        }
        std::printf("\n  '.' walkable   '*' the path the creatures would take\n");
    }

    // The client-link handshake gate. Entering without a composed layout
    // crashes the client, so the verdict matrix below is safety logic and
    // every row of it is pinned here.
    void RunLinkStateChecks()
    {
        LinkState link;
        uint32_t const acc = 1;
        uint32_t const other = 2;
        auto Is = [&](LinkVerdict want, char const* what) {
            Check(link.Verdict(acc, 1) == want, what, 0);
        };

        Is(LinkVerdict::NoAddon, "fresh account should be NoAddon");
        link.ReportVersion(acc, 0);
        Is(LinkVerdict::NoDll, "version 0 should be NoDll");
        link.ReportVersion(acc, 1);
        Check(link.Verdict(acc, 2) == LinkVerdict::DllTooOld,
              "version below requirement should be DllTooOld", 0);
        Is(LinkVerdict::NothingPushed, "no push yet should be NothingPushed");

        uint32_t const seqA = link.BeginPush(acc, 1000);
        Check(seqA != 0, "seq 0 must never be issued", 0);
        Is(LinkVerdict::AwaitingAck, "freshly pushed should be AwaitingAck");
        link.ReportAck(acc, "RECV:" + std::to_string(seqA));
        Is(LinkVerdict::AwaitingAck, "RECV is receipt, not readiness");
        link.ReportAck(acc, "READY:garbage");
        Is(LinkVerdict::AwaitingAck, "malformed READY must not satisfy the gate");
        link.ReportAck(acc, "READY:" + std::to_string(seqA));
        Is(LinkVerdict::Ready, "READY for the pending seq should be Ready");

        uint32_t const seqB = link.BeginPush(acc, 2000);
        Check(seqB != seqA, "seqs must be unique", 0);
        Is(LinkVerdict::AwaitingAck, "a stale READY must not satisfy a new push");
        Check(!link.ShouldRepush(acc, 2000 + 4999, 5000),
              "no repush before the timeout", 0);
        Check(link.ShouldRepush(acc, 2000 + 5000, 5000),
              "timeout should grant exactly one repush", 0);
        Check(!link.ShouldRepush(acc, 2000 + 60000, 5000),
              "the one repush must not repeat", 0);
        link.ReportAck(acc, "READY:" + std::to_string(seqB));
        Is(LinkVerdict::Ready, "READY for the new seq should recover to Ready");

        uint32_t const seqC = link.BeginPush(acc, 9000);
        link.ReportAck(acc, "NAK:crc mismatch");
        Is(LinkVerdict::Nak, "a NAK should be reported as Nak");
        Check(link.ShouldRepush(acc, 9001, 5000),
              "a NAK should grant the repush immediately", 0);
        Check(!link.ShouldRepush(acc, 9002, 5000),
              "the NAK repush must not repeat either", 0);
        link.ReportAck(acc, "READY:" + std::to_string(seqC));
        Is(LinkVerdict::Ready, "READY after a NAK should still recover");

        Check(link.Verdict(other, 1) == LinkVerdict::NoAddon,
              "accounts must not share state", 0);
        Check(!link.ShouldRepush(other, 99999, 1),
              "no repush for an account that was never pushed", 0);

        // A fresh version report must drop earlier readiness: after a client
        // restart the DLL's composed slots are gone, and a READY that
        // survived the report would wave a crash straight through the gate.
        Is(LinkVerdict::Ready, "precondition: still Ready before the re-report");
        link.ReportVersion(acc, 1);
        Is(LinkVerdict::NothingPushed, "a VER report must invalidate readiness");
        Check(!link.ShouldRepush(acc, 999999, 1),
              "no repush credit left over after invalidation", 0);
    }

    // --- 01 §8 game math ---------------------------------------------------
    //
    // The formulas are cheap, so the sweeps below are exhaustive over the whole
    // input range rather than sampled. Each sweep reports ONE check per dlvl so
    // the failure counter stays readable; the message carries the exact input
    // that broke, which is the part a reader needs.

    void RunGameMathChecks()
    {
        char msg[160];

        // The anchor values the design states outright. If one of these moves,
        // the doc and the code have diverged and one of the two is wrong.
        Check(GameLootMultX100(PD_GAME_DIFF_DEFAULT, PD_GAME_CASTER_PCT_DEFAULT) == 100,
              "lootMult at the defaults must be exactly 1.00", 0);
        Check(GameLootMultX100(PD_GAME_DIFF_MAX, PD_GAME_CASTER_PCT_DEFAULT) == 300,
              "lootMult at difficulty 100 must be exactly 3.00", 0);
        Check(GameLootMultX100(PD_GAME_DIFF_MAX, PD_GAME_CASTER_PCT_MAX) == 360,
              "lootMult at difficulty 100 with all casters must be 3.60", 0);
        Check(PD_GAME_DIFF_MIN == 1 && PD_GAME_DIFF_MAX == 100 && PD_GAME_DIFF_DEFAULT == 1,
              "the difficulty dial must be the integers 1..100, default 1", 0);
        Check(GameBossRooms(0) == 1 && GameBossRooms(9) == 1 && GameBossRooms(10) == 2,
              "boss rooms must follow 1 + dlvl/10", 0);
        Check(GameClampCasterPct(PD_GAME_CASTER_PCT_DEFAULT) == PD_GAME_CASTER_PCT_DEFAULT,
              "the default caster ratio must survive its own clamp", 0);

        // Rooms: the cap is min(measured, design, 3 + dlvl) at every dlvl, and
        // it never goes down as dlvl goes up.
        for (int dlvl = 0; dlvl <= 40; ++dlvl)
        {
            int want = 3 + dlvl;
            if (want > PD_GAME_ROOMS_CAP_DESIGN) want = PD_GAME_ROOMS_CAP_DESIGN;
            if (want > PD_GAME_ROOMS_CAP_MEASURED) want = PD_GAME_ROOMS_CAP_MEASURED;
            std::snprintf(msg, sizeof(msg),
                          "GameRoomsCap(%d) = %d, expected min(measured %d, 15, 3+dlvl)",
                          dlvl, GameRoomsCap(dlvl), PD_GAME_ROOMS_CAP_MEASURED);
            Check(GameRoomsCap(dlvl) == want, msg, 0);
            if (dlvl > 0)
            {
                std::snprintf(msg, sizeof(msg), "GameRoomsCap went DOWN between dlvl %d and %d",
                              dlvl - 1, dlvl);
                Check(GameRoomsCap(dlvl) >= GameRoomsCap(dlvl - 1), msg, 0);
            }
        }

        // The difficulty clamp takes NO dlvl: the dial is open from the first
        // run (operator directive 2026-08-08), so the sweep is one pass over
        // every input a command, a panel or a corrupt DB row can hand it -
        // including the negative and the absurd ones.
        {
            bool band = true, stable = true, kept = true;
            int badAt = 0;
            for (int wanted = -50; wanted <= 150; ++wanted)
            {
                int const d = GameClampDiff(wanted);
                if (d < PD_GAME_DIFF_MIN || d > PD_GAME_DIFF_MAX)
                {
                    band = false;
                    badAt = wanted;
                }
                if (GameClampDiff(d) != d)
                {
                    stable = false;
                    badAt = wanted;
                }
                // A value already inside the dial must come back untouched -
                // there is no grid to snap onto any more, so a clamp that
                // "corrected" a legal 37 would be silently eating player input.
                if (wanted >= PD_GAME_DIFF_MIN && wanted <= PD_GAME_DIFF_MAX && d != wanted)
                {
                    kept = false;
                    badAt = wanted;
                }
            }
            std::snprintf(msg, sizeof(msg), "difficulty clamp left [1, 100] (wanted %d)", badAt);
            Check(band, msg, 0);
            std::snprintf(msg, sizeof(msg), "difficulty clamp is not idempotent (wanted %d)", badAt);
            Check(stable, msg, 0);
            std::snprintf(msg, sizeof(msg), "difficulty clamp moved a legal value (wanted %d)", badAt);
            Check(kept, msg, 0);
        }

        // The room clamp still depends on dlvl and still has to hold for every
        // input, so it keeps its own per-dlvl sweep.
        for (int dlvl = 0; dlvl <= 40; ++dlvl)
        {
            bool roomsOk = true;
            int badAt = 0;
            for (int wanted = -100; wanted <= 400; ++wanted)
            {
                int const r = GameClampRooms(wanted, dlvl);
                if (r < PD_GAME_ROOMS_MIN || r > GameRoomsCap(dlvl) ||
                    GameClampRooms(r, dlvl) != r)
                {
                    roomsOk = false;
                    badAt = wanted;
                }
            }
            std::snprintf(msg, sizeof(msg), "room clamp left [3, cap] at dlvl %d (wanted %d)",
                          dlvl, badAt);
            Check(roomsOk, msg, 0);
        }

        // Caster ratio: band, idempotence, and the fact that the clamp is the
        // only thing standing between a config typo and a pack full of casters.
        {
            bool ok = true;
            int badAt = 0;
            for (int pct = -200; pct <= 400; ++pct)
            {
                int const c = GameClampCasterPct(pct);
                if (c < PD_GAME_CASTER_PCT_MIN || c > PD_GAME_CASTER_PCT_MAX ||
                    GameClampCasterPct(c) != c)
                {
                    ok = false;
                    badAt = pct;
                }
            }
            std::snprintf(msg, sizeof(msg), "caster ratio clamp broke at %d", badAt);
            Check(ok, msg, 0);
        }

        // Level band: every level snaps onto the 1, 6, ... 76 grid, and the
        // band it opens ([min, min+4]) never runs past 80.
        {
            bool ok = true;
            int badAt = 0;
            for (int lvl = -10; lvl <= 120; ++lvl)
            {
                int const b = GameClampBandMin(lvl);
                if (b < PD_GAME_BAND_MIN || b > PD_GAME_BAND_MAX ||
                    (b - PD_GAME_BAND_MIN) % PD_GAME_BAND_STEP != 0 ||
                    b + 4 > 80 || GameClampBandMin(b) != b)
                {
                    ok = false;
                    badAt = lvl;
                }
            }
            std::snprintf(msg, sizeof(msg), "level band snap broke at level %d", badAt);
            Check(ok, msg, 0);
            Check(GameClampBandMin(80) == 76 && GameClampBandMin(76) == 76,
                  "the top band must be 76..80", 0);
            Check(GameClampBandMin(5) == 1 && GameClampBandMin(6) == 6,
                  "the level band must snap DOWN, not to nearest", 0);
        }

        // Loot multiplier: monotone in BOTH inputs over the whole dial (a player
        // who raises difficulty or caster share must never see loot go down),
        // positive everywhere, and exact at the three anchors above.
        {
            bool mono = true;
            int prevD = -1;
            int badAt = 0;
            for (int d = PD_GAME_DIFF_MIN; d <= PD_GAME_DIFF_MAX; ++d)
            {
                int prevC = -1;
                for (int c = PD_GAME_CASTER_PCT_MIN; c <= PD_GAME_CASTER_PCT_MAX; ++c)
                {
                    int const m = GameLootMultX100(d, c);
                    if (m < prevC || m <= 0)
                    {
                        mono = false;
                        badAt = d * 1000 + c;
                    }
                    prevC = m;
                }
                int const atDefault = GameLootMultX100(d, PD_GAME_CASTER_PCT_DEFAULT);
                if (atDefault < prevD)
                {
                    mono = false;
                    badAt = d;
                }
                prevD = atDefault;
            }
            std::snprintf(msg, sizeof(msg), "loot multiplier is not monotone (at %d)", badAt);
            Check(mono, msg, 0);

            // Out-of-dial inputs are clamped rather than extrapolated: a corrupt
            // row must not be able to buy loot nobody could have earned.
            Check(GameLootMultX100(0, PD_GAME_CASTER_PCT_DEFAULT) == 100 &&
                  GameLootMultX100(1000, PD_GAME_CASTER_PCT_DEFAULT) == 300,
                  "lootMult must clamp its difficulty, not extrapolate it", 0);
        }

        // dxp -> dlvl, and the run reward that feeds it.
        {
            Check(GameDlvlFromDxp(0, 100, 30) == 0, "no dxp means dlvl 0", 0);
            Check(GameDlvlFromDxp(99, 100, 30) == 0, "a partial level is not a level", 0);
            Check(GameDlvlFromDxp(100, 100, 30) == 1, "one level's dxp is one dlvl", 0);
            Check(GameDlvlFromDxp(0xFFFFFFFFu, 1, 30) == 30,
                  "the cap must hold before the narrowing cast, not after", 0);
            Check(GameDlvlFromDxp(1000, 0, 30) == 0, "a zero curve must not divide by zero", 0);

            bool mono = true;
            uint32_t badAt = 0;
            for (uint32_t dxp = 0; dxp <= 5000; dxp += 7)
            {
                if (GameDlvlFromDxp(dxp + 7, 100, 30) < GameDlvlFromDxp(dxp, 100, 30))
                {
                    mono = false;
                    badAt = dxp;
                }
            }
            std::snprintf(msg, sizeof(msg), "dlvl went DOWN as dxp went up (at %u)", badAt);
            Check(mono, msg, 0);

            Check(GameRunDxp(5, 10) == 50u, "run dxp must be rooms x XP.PerRoom", 0);
            Check(GameRunDxp(0, 10) == 0u && GameRunDxp(-3, 10) == 0u,
                  "a run that cleared nothing pays nothing", 0);

            // 01 §8: difficulty must NOT be an XP lever. The signature has no
            // difficulty argument, so this holds by construction - the loop is
            // here to make the intent break loudly if someone ever adds one.
            // It sweeps the WHOLE new dial, because the 1..100 rework is exactly
            // the kind of change that invites "surely 100 should pay more".
            bool flat = true;
            for (int diff = PD_GAME_DIFF_MIN; diff <= PD_GAME_DIFF_MAX; ++diff)
            {
                (void)diff;
                if (GameRunDxp(7, 10) != 70u)
                {
                    flat = false;
                }
            }
            Check(flat, "run dxp must be difficulty-independent (01 §8)", 0);
        }
    }

    // --- boss rooms (01 §8 "1 + dlvl/10", flagged as the N deepest) ---------

    // The layout with bossRooms = 1 is FROZEN, and this is what freezes it.
    //
    // Accounts persist a layout as its seed plus the generation inputs it was
    // made with, so any change to the planner silently reshapes every dungeon
    // that already exists unless the old case comes out byte for byte the same.
    // The numbers below were captured from the build BEFORE boss flagging
    // became "the N deepest rooms" (2026-08-07); the manifest's own trailer is
    // a CRC32 over its whole body, so length + trailer is a byte-identity pin
    // rather than a spot check.
    //
    // If this check fails, the question is not "update the constants" - it is
    // whether PD_LAYOUT_VERSION has to be bumped, because every stored seed now
    // regenerates a different dungeon.
    void RunLayoutFreezeCheck()
    {
        uint32_t const PINNED_SEED = 12345u;
        int const PINNED_ROOMS = 5;
        size_t const PINNED_BYTES = 551;
        char const* const PINNED_TRAILER = "E;13df5510\n";

        BlockCfg cfg = MakeCfg(PINNED_SEED, PINNED_ROOMS);
        cfg.bossRooms = 1;

        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            Check(false, "the pinned layout no longer generates at all", PINNED_SEED);
            return;
        }

        std::string const m = EmitManifest(plan, 1);
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      "pinned manifest is %d bytes, was %d - the bossRooms=1 layout MOVED",
                      static_cast<int>(m.size()), static_cast<int>(PINNED_BYTES));
        Check(m.size() == PINNED_BYTES, msg, PINNED_SEED);

        size_t const trailer = std::strlen(PINNED_TRAILER);
        bool const same = m.size() >= trailer &&
                          m.compare(m.size() - trailer, trailer, PINNED_TRAILER) == 0;
        Check(same, "pinned manifest CRC changed - the bossRooms=1 layout MOVED", PINNED_SEED);
    }

    // Exactly `bossRooms` rooms carry the boss role, the entrance never does,
    // and bossIndex still points at the deepest of them. Run over its own seeds
    // rather than the batch's, because the batch only ever asks for one boss.
    void RunBossRoomChecks(int seeds)
    {
        char msg[160];
        for (int bossRooms = 1; bossRooms <= 3; ++bossRooms)
        {
            // Rooms enough that the deepest few are never forced to be the
            // entrance's neighbours - 8 is what dlvl 5 already unlocks.
            int const rooms = 8;
            for (int i = 0; i < seeds; ++i)
            {
                uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 7u;
                BlockCfg cfg = MakeCfg(seed, rooms);
                cfg.bossRooms = bossRooms;

                BlockPlan plan;
                if (!GenerateBlockPlan(cfg, &plan))
                {
                    std::snprintf(msg, sizeof(msg), "generation failed with %d boss room(s)",
                                  bossRooms);
                    Check(false, msg, seed);
                    continue;
                }

                int found = 0;
                int shallowestBoss = 1 << 30;
                int deepestBoss = -1;
                int deepestOther = -1;
                for (PlacedBlock const& b : plan.blocks)
                {
                    if (b.role == BlockRole::RoomBoss)
                    {
                        ++found;
                        shallowestBoss = (b.depth < shallowestBoss) ? b.depth : shallowestBoss;
                        deepestBoss = (b.depth > deepestBoss) ? b.depth : deepestBoss;
                    }
                    else if (b.roomId >= 0 && b.role != BlockRole::RoomEntrance)
                    {
                        deepestOther = (b.depth > deepestOther) ? b.depth : deepestOther;
                    }
                }

                std::snprintf(msg, sizeof(msg), "%d block(s) carry the boss role, asked for %d",
                              found, bossRooms);
                Check(found == bossRooms, msg, seed);

                PlacedBlock const& entrance = plan.blocks[static_cast<size_t>(plan.entranceIndex)];
                Check(entrance.role == BlockRole::RoomEntrance &&
                      plan.entranceIndex != plan.bossIndex,
                      "the entrance was flagged as a boss room", seed);

                PlacedBlock const& boss = plan.blocks[static_cast<size_t>(plan.bossIndex)];
                Check(boss.role == BlockRole::RoomBoss,
                      "bossIndex does not point at a boss room", seed);

                // "The N DEEPEST": every boss room is at least as deep as every
                // other room, and bossIndex is the deepest of the boss rooms.
                Check(shallowestBoss >= deepestOther,
                      "a non-boss room is deeper than a boss room", seed);
                Check(boss.depth == deepestBoss,
                      "bossIndex is not the deepest boss room", seed);
            }
        }
    }

    // --- the room-cap measurement (01 §8 cap 15 vs. what the kit allows) ----
    //
    // Two physical constraints bound the room count and NEITHER is negotiable
    // here: the manifest is one addon packet, and the field stays 8x8 (one ADT
    // tile) because multi-tile plans are untested client-side. So the cap is
    // measured against the shipped generator rather than designed.

    struct RoomCapRow
    {
        int    rooms = 0;
        int    bossRooms = 0;
        int    failures = 0;
        size_t maxManifest = 0;
    };

    RoomCapRow MeasureRoomCapRow(int rooms, int bossRooms, int seeds)
    {
        RoomCapRow row;
        row.rooms = rooms;
        row.bossRooms = bossRooms;

        for (int i = 0; i < seeds; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 1u;
            BlockCfg cfg = MakeCfg(seed, rooms);
            cfg.bossRooms = bossRooms;

            BlockPlan plan;
            if (!GenerateBlockPlan(cfg, &plan))
            {
                ++row.failures;
                continue;
            }
            // seq 99 rather than 1: a two-digit sequence number is the widest
            // the header realistically carries, so the length measured here is
            // the worst case rather than the prettiest one.
            std::string const m = EmitManifest(plan, 99);
            row.maxManifest = (m.size() > row.maxManifest) ? m.size() : row.maxManifest;
        }
        return row;
    }

    // Largest room count that generates on EVERY seed and still fits the
    // manifest budget, with the boss-room count a player at that dlvl would
    // actually run (rooms R unlocks at dlvl R - 3, per 01 §8 "3 + dlvl").
    int MeasureRoomCap(int seeds, bool verbose)
    {
        if (verbose)
        {
            std::printf("room-cap measurement: %d seeds per row, field 8x8, "
                        "manifest budget %d B\n\n", seeds, PD_GAME_MANIFEST_BUDGET_B);
            std::printf("  rooms  boss  cells   genfail  maxManifest  verdict\n");
        }

        int cap = PD_GAME_ROOMS_MIN;
        for (int rooms = PD_GAME_ROOMS_MIN; rooms <= PD_GAME_ROOMS_CAP_DESIGN; ++rooms)
        {
            // bossRooms as a player at that room count would run them: the cap
            // formula is 3 + dlvl, so room count R unlocks at dlvl R - 3. The
            // floor is lower than 3 (a 1-room boss rush is legal), which is
            // why the dlvl here is clamped at 0 rather than derived from the
            // floor constant.
            int const unlockDlvl = rooms > 3 ? rooms - 3 : 0;
            int const boss = GameBossRooms(unlockDlvl);
            RoomCapRow const row = MeasureRoomCapRow(rooms, boss, seeds);
            bool const ok = row.failures == 0 &&
                            row.maxManifest <= static_cast<size_t>(PD_GAME_MANIFEST_BUDGET_B);
            if (ok)
            {
                cap = rooms;
            }
            if (verbose)
            {
                std::printf("  %5d  %4d  %5d   %7d  %11d  %s\n", row.rooms, row.bossRooms,
                            row.rooms + row.bossRooms, row.failures,
                            static_cast<int>(row.maxManifest), ok ? "ok" : "FAILS");
            }
        }
        return cap;
    }

    int RunRoomCap(int seeds)
    {
        int const measured = MeasureRoomCap(seeds, true);
        std::printf("\nlargest room count clean on every seed: %d\n", measured);
        std::printf("PD_GAME_ROOMS_CAP_MEASURED currently encodes: %d\n",
                    PD_GAME_ROOMS_CAP_MEASURED);
        std::printf("%s\n", measured >= PD_GAME_ROOMS_CAP_MEASURED
                                ? "the encoded cap holds"
                                : "THE ENCODED CAP IS TOO HIGH - update PDv2GameMath.h");
        return measured >= PD_GAME_ROOMS_CAP_MEASURED ? 0 : 1;
    }

    int RunBatch(int count, int rooms)
    {
        std::printf("batch of %d seeds, %d rooms + 1 boss each\n\n", count, rooms);

        RunLinkStateChecks();
        RunGameMathChecks();
        RunLayoutFreezeCheck();
        // A tenth of the batch is plenty for three boss-room counts: the
        // property is structural, not statistical.
        RunBossRoomChecks(count / 10 + 1);

        // The encoded room cap is a MEASUREMENT, so it has to be re-measured or
        // it rots: a generator change that makes packing harder would otherwise
        // only surface as accounts whose dungeon stopped generating.
        int const measuredCap = MeasureRoomCap(count, false);
        {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "PD_GAME_ROOMS_CAP_MEASURED is %d but only %d is clean over %d seeds "
                          "- re-run `pdblock --roomcap`", PD_GAME_ROOMS_CAP_MEASURED,
                          measuredCap, count);
            Check(measuredCap >= PD_GAME_ROOMS_CAP_MEASURED, msg, 0);
        }

        size_t maxManifest = 0;
        int longestPath = 0;
        int minBlocks = 1 << 30;
        int maxBlocks = 0;

        for (int i = 0; i < count; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 1u;
            BlockCfg const cfg = MakeCfg(seed, rooms);

            BlockPlan plan;
            if (!GenerateBlockPlan(cfg, &plan))
            {
                Check(false, "generation failed", seed);
                continue;
            }

            std::string err;
            Check(ValidateBlockPlan(plan, &err), err.empty() ? "validation failed" : err.c_str(), seed);

            std::string why;
            Check(EdgesAgree(plan, &why), why.empty() ? "edges disagree" : why.c_str(), seed);

            // Determinism: the same config must produce the identical plan.
            BlockPlan again;
            Check(GenerateBlockPlan(cfg, &again), "second generation failed", seed);
            bool same = again.blocks.size() == plan.blocks.size() &&
                        again.effectiveSeed == plan.effectiveSeed;
            for (size_t k = 0; same && k < plan.blocks.size(); ++k)
            {
                same = plan.blocks[k].bx == again.blocks[k].bx &&
                       plan.blocks[k].by == again.blocks[k].by &&
                       plan.blocks[k].chunkId == again.blocks[k].chunkId &&
                       plan.blocks[k].socketMask == again.blocks[k].socketMask;
            }
            Check(same, "two runs of the same seed differ", seed);

            // The room count must be what was asked for; a planner that quietly
            // drops rooms would make dlvl meaningless.
            int rooms_found = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.roomId >= 0) ++rooms_found;
            }
            Check(rooms_found == rooms + 1, "room count does not match the config", seed);

            // Entrance and boss must be distinct blocks.
            Check(plan.entranceIndex != plan.bossIndex, "entrance and boss are the same block", seed);

            // Exactly as many boss rooms as were asked for. The batch asks for
            // one, which is the case a stored layout can already be in.
            int bossFound = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.role == BlockRole::RoomBoss) ++bossFound;
            }
            Check(bossFound == cfg.bossRooms, "boss room count does not match the config", seed);

            // The manifest must fit the wire budget and round-trip its own CRC.
            std::string const manifest = EmitManifest(plan, 1);
            maxManifest = (manifest.size() > maxManifest) ? manifest.size() : maxManifest;
            Check(manifest.size() <= 2048, "manifest over the 2 KB budget", seed);

            size_t const e = manifest.rfind("\nE;");
            Check(e != std::string::npos, "manifest has no E; trailer", seed);
            if (e != std::string::npos)
            {
                uint32_t const want = Crc32(manifest.data(), e + 1);
                char expect[24];
                std::snprintf(expect, sizeof(expect), "E;%08x\n", want);
                Check(manifest.compare(e + 1, std::string::npos, expect) == 0,
                      "manifest CRC does not match its body", seed);
            }

            // The walk grid, if the kit metadata was found. A layout whose rooms
            // cannot reach each other is worse than one that fails to generate:
            // it looks fine and strands the player.
            if (!g_masks.empty())
            {
                WalkGrid grid;
                std::string gridErr;
                if (!BuildWalkGrid(plan, MaskFor, &grid, &gridErr))
                {
                    Check(false, gridErr.c_str(), seed);
                }
                else
                {
                    std::string gridWhy;
                    int longest = 0;
                    Check(CheckAllRoomsConnected(plan, grid, gridWhy, longest),
                          gridWhy.empty() ? "rooms not all connected" : gridWhy.c_str(), seed);
                    longestPath = (longest > longestPath) ? longest : longestPath;

                    std::string compWhy;
                    Check(CheckGridOneComponent(grid, compWhy),
                          compWhy.empty() ? "grid not one component" : compWhy.c_str(), seed);

                    std::string approachWhy;
                    Check(CheckApproachPolicy(grid, seed, approachWhy),
                          approachWhy.empty() ? "approach policy broken" : approachWhy.c_str(), seed);

                    std::string mathWhy;
                    Check(CheckWorldMathRoundTrip(plan, grid, mathWhy),
                          mathWhy.empty() ? "world math broken" : mathWhy.c_str(), seed);
                }
            }

            int const blocks = static_cast<int>(plan.blocks.size());
            minBlocks = (blocks < minBlocks) ? blocks : minBlocks;
            maxBlocks = (blocks > maxBlocks) ? blocks : maxBlocks;
        }

        if (longestPath) std::printf("longest room-to-room path: %d cells\n", longestPath);
        std::printf("blocks per layout: %d..%d\n", minBlocks, maxBlocks);
        std::printf("largest manifest  : %d bytes (budget 2048)\n", static_cast<int>(maxManifest));
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        std::printf("%s\n", g_failures == 0 ? "ALL CHECKS PASS" : "FAILURES");
        return g_failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // Optional: without the kit metadata the planner checks still run, the
    // walk-grid ones are simply skipped rather than faked.
    LoadMasks("C:\\\\wowstuff\\\\ForgottenLand2.0\\\\output\\\\pd_block_kit\\\\FLStream\\\\chunks\\\\t1b\\\\pdungeon_chunk_meta.sql");

    if (argc >= 2 && std::strcmp(argv[1], "--batch") == 0)
    {
        int const n = (argc >= 3) ? std::atoi(argv[2]) : 100;
        int const rooms = (argc >= 4) ? std::atoi(argv[3]) : 5;
        return RunBatch(n, rooms);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--roomcap") == 0)
    {
        return RunRoomCap((argc >= 3) ? std::atoi(argv[2]) : 300);
    }
    if (argc >= 4 && std::strcmp(argv[1], "--manifest") == 0)
    {
        int const rooms = (argc >= 5) ? std::atoi(argv[4]) : 5;
        int const obx = (argc >= 7) ? std::atoi(argv[5]) : 32 * 8;
        int const oby = (argc >= 7) ? std::atoi(argv[6]) : 32 * 8;
        WriteManifest(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)),
                      rooms, argv[3], obx, oby);
        return 0;
    }

    if (argc >= 3 && std::strcmp(argv[1], "--path") == 0)
    {
        int const r = (argc >= 4) ? std::atoi(argv[3]) : 5;
        PrintPath(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)), r);
        return 0;
    }

    uint32_t const seed = (argc >= 2) ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 12345u;
    int const rooms = (argc >= 3) ? std::atoi(argv[2]) : 5;
    PrintOne(seed, rooms);
    return 0;
}
