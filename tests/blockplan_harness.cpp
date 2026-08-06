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
    }

    int RunBatch(int count, int rooms)
    {
        std::printf("batch of %d seeds, %d rooms + 1 boss each\n\n", count, rooms);

        RunLinkStateChecks();

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
