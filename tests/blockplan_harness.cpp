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
//      src\generator\PDBlockPlan.cpp /Fe:pdblock.exe

// MSVC deprecates std::fopen in favour of fopen_s, which is a Microsoft
// extension. This harness is also expected to build with g++ (see CLAUDE.md),
// so the portable call stays and the warning is turned off for this file only.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "generator/PDBlockPlan.h"

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

    int RunBatch(int count, int rooms)
    {
        std::printf("batch of %d seeds, %d rooms + 1 boss each\n\n", count, rooms);

        size_t maxManifest = 0;
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

            int const blocks = static_cast<int>(plan.blocks.size());
            minBlocks = (blocks < minBlocks) ? blocks : minBlocks;
            maxBlocks = (blocks > maxBlocks) ? blocks : maxBlocks;
        }

        std::printf("blocks per layout: %d..%d\n", minBlocks, maxBlocks);
        std::printf("largest manifest  : %d bytes (budget 2048)\n", static_cast<int>(maxManifest));
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        std::printf("%s\n", g_failures == 0 ? "ALL CHECKS PASS" : "FAILURES");
        return g_failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
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

    uint32_t const seed = (argc >= 2) ? static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 12345u;
    int const rooms = (argc >= 3) ? std::atoi(argv[2]) : 5;
    PrintOne(seed, rooms);
    return 0;
}
