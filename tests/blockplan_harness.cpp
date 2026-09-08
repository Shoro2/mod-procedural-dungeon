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
//   pdblock --decor-batch <n>            decor placement + determinism over n
//                                        seeds x a room-count matrix, and the
//                                        surface classes against kit_meta.json
//   pdblock --manifest <seed> <file> [rooms]
//                                        writes the manifest as raw bytes, for
//                                        feeding to 49_pd_compose_blocks.py
//
// Build:
//   cl /std:c++17 /EHsc /W4 /O2 /I src tests\blockplan_harness.cpp
//      src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp
//      src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp
//      src\generator\PDv2PackDraw.cpp src\generator\PDv2AmbushPlan.cpp
//      /Fe:pdblock.exe

// MSVC deprecates std::fopen in favour of fopen_s, which is a Microsoft
// extension. This harness is also expected to build with g++ (see CLAUDE.md),
// so the portable call stays and the warning is turned off for this file only.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "generator/PDBlockPlan.h"
#include "generator/PDv2AmbushPlan.h"
#include "generator/PDv2DecorPlan.h"
#include "generator/PDv2GameMath.h"
#include "generator/PDv2LinkState.h"
#include "generator/PDv2PackDraw.h"
#include "generator/PDv2SpawnAnchors.h"
#include "generator/PDv2WalkGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

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

    // --- kit metadata, for the surface-class oracle -------------------------
    //
    // The SQL above carries the walk masks; kit_meta.json carries what the kit
    // DERIVED from them - the surface classes and the anchors. PDv2Classify
    // re-derives the classes on the server side, and re-derivation is only
    // safe while the two agree, so the harness compares them cell for cell.
    struct KitChunk
    {
        std::string classes;
        std::vector<DecorAnchor> anchors;
        std::vector<KitProp> props;
        int declaredProps = 0;      // kit_meta's own "goProps" count
        // Round B / B1: the same anchors span, decoded with its KINDS kept.
        // The flat list above stays what the decor clearance reads.
        std::string anchorsJson;
        RoomAnchors typed;
    };

    std::map<int, KitChunk> g_kit;

    bool LoadKitMeta(char const* path)
    {
        FILE* fh = std::fopen(path, "rb");
        if (!fh)
        {
            std::printf("  cannot open %s\n", path);
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

        // Field scanning rather than a JSON parser: two fields per chunk are
        // wanted and the file is machine-written by 48_gen_t1_blockkit.py, so
        // a dependency would cost more than it buys. Each chunk record runs
        // from its own "chunkId" to the next one.
        size_t at = blob.find("\"chunkId\"");
        while (at != std::string::npos)
        {
            size_t const next = blob.find("\"chunkId\"", at + 9);
            size_t const end = (next == std::string::npos) ? blob.size() : next;

            size_t const colon = blob.find(':', at);
            int const chunkId = (colon == std::string::npos || colon > end)
                                    ? 0 : std::atoi(blob.c_str() + colon + 1);

            KitChunk chunk;
            size_t const cls = blob.find("\"surfaceClasses\"", at);
            if (cls != std::string::npos && cls < end)
            {
                size_t const open = blob.find('"', blob.find(':', cls) + 1);
                size_t const close = (open == std::string::npos)
                                         ? std::string::npos : blob.find('"', open + 1);
                if (close != std::string::npos && close < end)
                {
                    chunk.classes = blob.substr(open + 1, close - open - 1);
                }
            }

            size_t const anc = blob.find("\"anchors\"", at);
            if (anc != std::string::npos && anc < end)
            {
                std::string const span = blob.substr(anc, end - anc);
                DecodeAnchorList(span, chunk.anchors);
                DecodePropList(span, chunk.props);
                chunk.anchorsJson = span;
                DecodeRoomAnchors(span, chunk.typed);
            }

            size_t const gp = blob.find("\"goProps\"", at);
            if (gp != std::string::npos && gp < end)
            {
                size_t const colon = blob.find(':', gp);
                if (colon != std::string::npos && colon < end)
                {
                    chunk.declaredProps = std::atoi(blob.c_str() + colon + 1);
                }
            }

            if (chunkId && !chunk.classes.empty())
            {
                g_kit[chunkId] = chunk;
            }
            at = next;
        }

        std::printf("  %u kit chunk(s) from %s\n",
                    static_cast<unsigned>(g_kit.size()), path);
        return !g_kit.empty();
    }

    std::vector<DecorAnchor> const* AnchorsForChunk(int chunkId)
    {
        auto it = g_kit.find(chunkId);
        return it == g_kit.end() ? nullptr : &it->second.anchors;
    }

    // Round B: the spine in one glance - the chain in order, every pocket
    // with its host, every loop room with the run it hangs off, the segments.
    // Printed by the single layout and by --path, and quoted by the operator
    // document.
    std::string ChainSummary(BlockPlan const& plan)
    {
        int const len = ChainLength(plan);
        std::vector<PlacedBlock const*> chain(static_cast<size_t>(len), nullptr);
        std::vector<PlacedBlock const*> pockets;
        std::vector<PlacedBlock const*> loops;
        int bosses = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.chainIndex >= 0) chain[static_cast<size_t>(b.chainIndex)] = &b;
            if (b.branchOf >= 0) pockets.push_back(&b);
            if (b.detourOf >= 0) loops.push_back(&b);
            if (b.role == BlockRole::RoomBoss) ++bosses;
        }

        std::string out;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "chain (%d rooms, %d boss): ", len, bosses);
        out += buf;
        for (int i = 0; i < len; ++i)
        {
            PlacedBlock const* b = chain[static_cast<size_t>(i)];
            if (!b)
            {
                out += (i ? " -> ?" : "?");
                continue;
            }
            char const tag = b->role == BlockRole::RoomEntrance ? 'E'
                           : b->role == BlockRole::RoomBoss     ? 'B' : 'R';
            std::snprintf(buf, sizeof(buf), "%s%c#%d (%d,%d)", i ? " -> " : "", tag, i, b->bx, b->by);
            out += buf;
        }
        out += '\n';

        if (!pockets.empty())
        {
            out += "pockets:";
            for (PlacedBlock const* p : pockets)
            {
                std::snprintf(buf, sizeof(buf), "  R#%d + pocket (%d,%d)",
                              p->branchOf, p->bx, p->by);
                out += buf;
            }
            out += '\n';
        }

        if (!loops.empty())
        {
            out += "loops:";
            for (PlacedBlock const* l : loops)
            {
                std::snprintf(buf, sizeof(buf), "  R#%d run + loop room (%d,%d) [segment %d]",
                              l->detourOf, l->bx, l->by, SegmentOf(plan, *l));
                out += buf;
            }
            out += '\n';
        }

        int segStart = 1;
        int k = 0;
        for (int i = 1; i < len; ++i)
        {
            PlacedBlock const* b = chain[static_cast<size_t>(i)];
            if (!b || b->role != BlockRole::RoomBoss) continue;
            ++k;
            int inSegment = 0;
            for (PlacedBlock const* p : pockets)
            {
                if (p->branchOf >= segStart && p->branchOf < i) ++inSegment;
            }
            // A loop room belongs to the segment of the spine room its run
            // leads INTO, and that room may be the boss itself - so the upper
            // bound is inclusive here where the pocket line's is not (a boss
            // never hosts a pocket).
            int loopsIn = 0;
            for (PlacedBlock const* l : loops)
            {
                if (l->detourOf >= segStart && l->detourOf <= i) ++loopsIn;
            }
            std::snprintf(buf, sizeof(buf),
                          "segment %d: chain %d..%d, pockets %d, loops %d, boss B#%d\n",
                          k, segStart, i, inSegment, loopsIn, i);
            out += buf;
            segStart = i + 1;
        }
        return out;
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
        std::printf("E = entrance, B = boss, R = spine room, r = pocket room, o = loop room, "
                    "D = dead end, | - + = corridor\n\n");
        std::printf("%s\n", ChainSummary(plan).c_str());
        std::printf("%s\n", AsciiBlockDump(plan).c_str());

        std::string const manifest = EmitManifest(plan, 1);
        std::printf("manifest, %d bytes of the 2048 budget:\n\n%s\n",
                    static_cast<int>(manifest.size()), manifest.c_str());
    }

    // Writes to a FILE, in binary, deliberately. A manifest is LF separated and
    // the parsers on both sides reject CR, but stdout on Windows is a text
    // stream that rewrites every \n into \r\n -- so piping this through a shell
    // would corrupt it in a way that only shows up as a parse error much later.
    void WriteManifest(uint32_t seed, int rooms, char const* path, int obx, int oby,
                       int theme, int bossRooms)
    {
        BlockCfg cfg = MakeCfg(seed, rooms, obx, oby);
        cfg.theme = theme;
        // The boss-room count is a generation INPUT, not a decoration: it
        // changes how many cells the scatter wants, so a stored layout with
        // gen_boss_rooms 2 cannot be reproduced with the fixture's 1. Added
        // 2026-09-02 to audit an operator's live layout from its
        // pdungeon_account row; 1 keeps every earlier call byte-identical.
        cfg.bossRooms = bossRooms;
        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
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

    // The Bresenham sampler Round B shipped, kept ONLY as the reference the
    // supercover test is measured against (research c-research-patrol-ambush.md
    // A1: it skips one cell at every minor-axis transition).
    bool LegacyLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b)
    {
        int const steps = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
        if (steps == 0)
        {
            return grid.At(a.x, a.y);
        }
        for (int i = 0; i <= steps; ++i)
        {
            double const t = static_cast<double>(i) / steps;
            int const x = static_cast<int>(std::lround(a.x + (b.x - a.x) * t));
            int const y = static_cast<int>(std::lround(a.y + (b.y - a.y) * t));
            if (!grid.At(x, y))
            {
                return false;
            }
        }
        return true;
    }

    // Independent reference: sample the segment between the two cell centres
    // at 1/64 of a cell and demand that every sampled cell is walkable. Slower
    // and cruder than the DDA, which is the point - it shares no code with it.
    bool SampledLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b)
    {
        int const steps = 64 * std::max(1, std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)));
        for (int i = 0; i <= steps; ++i)
        {
            double const t = static_cast<double>(i) / steps;
            double const fx = a.x + (b.x - a.x) * t;
            double const fy = a.y + (b.y - a.y) * t;
            int const x = static_cast<int>(std::floor(fx + 0.5));
            int const y = static_cast<int>(std::floor(fy + 0.5));
            if (!grid.At(x, y))
            {
                return false;
            }
        }
        return true;
    }

    // One 8x8 grid per kit chunk, built the way BuildWalkGrid copies a mask
    // (PDv2WalkGrid.cpp:79-90: mask[row * PD_CELLS_PER_BLOCK + col] with row
    // the cell's y and col its x, so At(x, y) reads mask[y * 8 + x]).
    WalkGrid GridFromMask(uint8_t const* mask)
    {
        WalkGrid g;
        g.originBX = 0;
        g.originBY = 0;
        g.width = PD_CELLS_PER_BLOCK;
        g.height = PD_CELLS_PER_BLOCK;
        g.cells.assign(mask, mask + PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK);
        return g;
    }

    // The supercover pair counts over the whole kit, `approved,rejected;`.
    // Captured by RUNNING `pdblock --batch` and reading the "supercover pair
    // counts moved" message, never by reasoning about the value. `rejected` is
    // the size of the defect the Round C fix removes: pairs the shipped
    // Bresenham sampler approved and the supercover test does not.
    char const* const PD_SUPERCOVER_PAIRS_PIN = "291480,9604;";

    // Over EVERY kit walk mask and every ordered pair of its walkable cells:
    //   (1) whatever the supercover test approves, the sampled reference approves too
    //       (no approved segment leaves the mask);
    //   (2) approved(supercover) is a subset of approved(legacy) - the fix only
    //       removes approvals;
    //   (3) the legacy sampler approves strictly more pairs (the defect exists).
    // The pair counts are pinned so a later change to the test is visible.
    void CheckSupercover(std::map<int, std::vector<uint8_t>> const& masks,
                         uint64_t& approved, uint64_t& rejectedByFix)
    {
        approved = 0;
        rejectedByFix = 0;
        for (auto const& kv : masks)
        {
            WalkGrid const g = GridFromMask(kv.second.data());
            for (int ay = 0; ay < g.height; ++ay)
            {
                for (int ax = 0; ax < g.width; ++ax)
                {
                    if (!g.At(ax, ay))
                    {
                        continue;
                    }
                    for (int by = 0; by < g.height; ++by)
                    {
                        for (int bx = 0; bx < g.width; ++bx)
                        {
                            if (!g.At(bx, by))
                            {
                                continue;
                            }
                            GridPoint const a{ ax, ay }, b{ bx, by };
                            bool const ok = GridLineWalkable(g, a, b);
                            bool const legacy = LegacyLineWalkable(g, a, b);
                            if (ok)
                            {
                                ++approved;
                                Check(SampledLineWalkable(g, a, b),
                                      "supercover approved a segment that leaves the walk mask",
                                      static_cast<uint32_t>(kv.first));
                                Check(legacy, "supercover approved a segment the legacy sampler refused",
                                      static_cast<uint32_t>(kv.first));
                            }
                            else if (legacy)
                            {
                                ++rejectedByFix;
                            }
                        }
                    }
                }
            }
        }
        Check(rejectedByFix > 0, "the legacy sampler approved nothing the supercover test rejects (vacuous)", 0);
    }

    // The corner half of the supercover rule, on geometry a kit rebuild cannot
    // move (Round C / C1 Task 1 review, Important 2).
    //
    // CheckSupercover above cannot see that half at all: its sampled reference
    // rounds with `floor(f + 0.5)`, which at an exact corner names the UPPER
    // cell only, so it is strictly LOOSER than the DDA there and can never
    // object to a corner rule that is dropped, widened or narrowed. Until this
    // table existed the only thing between such a change and a green batch was
    // PD_SUPERCOVER_PAIRS_PIN - and a pin's documented failure mode is to be
    // re-captured by whoever moved the kit.
    //
    // Each case is an 8x8 grid, walkable everywhere except the cells it names,
    // with the verdict written out by hand from the geometry rather than read
    // off a run: a corner tie needs equal 2-adic valuation of |dx| and |dy|, so
    // a 45 degree line ties at EVERY step and a 3:1 line (|dx| = 1, |dy| = 3)
    // ties exactly once, at t = 1/2. Every blocked cell below is one the
    // segment only ever STRADDLES - none of them lies on the supercover path -
    // so a case that flips can only have flipped because the corner rule did.
    struct CornerCase
    {
        char const* name;
        GridPoint a;
        GridPoint b;
        std::vector<GridPoint> blocked;
        bool expected;
    };

    void CheckCornerRule()
    {
        std::vector<CornerCase> const cases = {
            // 45 degrees: (0,0) -> (3,3) ties at (0.5,0.5), (1.5,1.5) and
            // (2.5,2.5). The first tie straddles (1,0) and (0,1); the cells
            // actually entered are the diagonal (0,0) (1,1) (2,2) (3,3).
            { "45 deg with both straddling cells open must be APPROVED",
              { 0, 0 }, { 3, 3 }, {}, true },
            { "45 deg with the x-side straddling cell (1,0) blocked must be REFUSED",
              { 0, 0 }, { 3, 3 }, { { 1, 0 } }, false },
            { "45 deg with the y-side straddling cell (0,1) blocked must be REFUSED",
              { 0, 0 }, { 3, 3 }, { { 0, 1 } }, false },
            // 3:1, one tie and it is at t = 1/2 exactly: (0,0) -> (1,3) enters
            // (0,0) (0,1), crosses the corner at (0.5,1.5) straddling (1,1) and
            // (0,2), then enters (1,2) (1,3). Neither straddling cell is on
            // that path.
            { "3:1 crossing a corner at t = 1/2 with both straddling cells open must be APPROVED",
              { 0, 0 }, { 1, 3 }, {}, true },
            { "3:1 at t = 1/2 with the x-side straddling cell (1,1) blocked must be REFUSED",
              { 0, 0 }, { 1, 3 }, { { 1, 1 } }, false },
            { "3:1 at t = 1/2 with the y-side straddling cell (0,2) blocked must be REFUSED",
              { 0, 0 }, { 1, 3 }, { { 0, 2 } }, false },
            // Axis-aligned: no tie is possible (a tie needs steps left on BOTH
            // axes), so a one-cell-wide corridor has to stay walkable. This is
            // the case a "test both neighbours on every step" reading of the
            // corner rule breaks, and no pin in this file would notice.
            { "a horizontal line along an open row walled on both sides must be APPROVED",
              { 0, 4 }, { 7, 4 },
              { { 0, 3 }, { 1, 3 }, { 2, 3 }, { 3, 3 }, { 4, 3 }, { 5, 3 }, { 6, 3 }, { 7, 3 },
                { 0, 5 }, { 1, 5 }, { 2, 5 }, { 3, 5 }, { 4, 5 }, { 5, 5 }, { 6, 5 }, { 7, 5 } },
              true },
            { "a vertical line along an open column walled on both sides must be APPROVED",
              { 4, 0 }, { 4, 7 },
              { { 3, 0 }, { 3, 1 }, { 3, 2 }, { 3, 3 }, { 3, 4 }, { 3, 5 }, { 3, 6 }, { 3, 7 },
                { 5, 0 }, { 5, 1 }, { 5, 2 }, { 5, 3 }, { 5, 4 }, { 5, 5 }, { 5, 6 }, { 5, 7 } },
              true },
        };

        for (CornerCase const& c : cases)
        {
            std::vector<uint8_t> mask(PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK, 1);
            for (GridPoint const& p : c.blocked)
            {
                mask[static_cast<size_t>(p.y) * PD_CELLS_PER_BLOCK + p.x] = 0;
            }
            WalkGrid const g = GridFromMask(mask.data());
            Check(GridLineWalkable(g, c.a, c.b) == c.expected, c.name, 0);
        }
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

    // The derivation Round C / C2 hangs the ambush trigger on: a world
    // position taken anywhere inside block (bx, by) must divide back to that
    // block. TickAmbushes fires on `gcx / PD_CELLS_PER_BLOCK == spot.bx &&
    // gcy / PD_CELLS_PER_BLOCK == spot.by` and on nothing else - there is no
    // radius left to absorb a mistake here, so a division that stopped naming
    // the block would either kill every ambush in the dungeon silently or fire
    // one from the corridor next door.
    //
    // BOTH paths, deliberately. The engine hands WorldToCell a Player's float
    // position, while the arm side computes the centre in double and
    // BlockToWorld narrows it to float (PDv2Mgr.cpp:465-475). The block centre
    // sits EXACTLY on the cell 3/4 boundary on both axes (mid = BLOCK/2 =
    // 4 * CELL), so the two paths may legitimately name different CELLS there
    // - which is why the patrol beat block below reproduces the narrowing
    // before it derives a start cell. What must never differ is the BLOCK the
    // two divide to, because the coarse question is the only one the ambush
    // trigger asks.
    void RunBlockDerivationChecks()
    {
        double const mid = PD_BLOCK_SIZE_YD / 2.0;

        // Both corners of the block field, the operator's own origin, and the
        // ambush pin's own segment-1 corridor block (259,259) - the block this
        // trigger is actually pinned on, so the check samples the layout it
        // guards rather than a neighbour of it. Block 511 sits at about
        // -17066 yd, so the sign of x/y is covered as well.
        struct Blk { int bx; int by; };
        Blk const blocks[4] = { { 0, 0 }, { 256, 256 }, { 259, 259 }, { 511, 511 } };

        // Block-local (u, v) in yards: the near corner cell, the centre (both
        // axes exactly on the cell 3/4 boundary), the far corner cell, and
        // 4 * CELL = 33.3333 on one axis only - ON that same boundary in u,
        // mid-cell in v, so the one-sided case is covered too. 33.3 would sit
        // a third of a yard BELOW the boundary and prove nothing.
        struct Pt { double u; double v; char const* what; };
        Pt const points[4] = {
            { 0.5, 0.5, "(0.5,0.5)" },
            { mid, mid, "(mid,mid)" },
            { 66.6, 66.6, "(66.6,66.6)" },
            { 4.0 * PD_CELL_SIZE_YD, 0.5, "(33.3333,0.5)" },
        };

        char msg[192];
        for (Blk const& b : blocks)
        {
            for (Pt const& p : points)
            {
                double x = 0.0, y = 0.0;
                BlockLocalToWorld(b.bx, b.by, p.u, p.v, x, y);

                int gcx = 0, gcy = 0;
                WorldToCell(x, y, gcx, gcy);
                std::snprintf(msg, sizeof(msg),
                              "block (%d,%d) local %s divides to block (%d,%d) - double path",
                              b.bx, b.by, p.what,
                              gcx / PD_CELLS_PER_BLOCK, gcy / PD_CELLS_PER_BLOCK);
                Check(gcx >= 0 && gcy >= 0 &&
                      gcx / PD_CELLS_PER_BLOCK == b.bx &&
                      gcy / PD_CELLS_PER_BLOCK == b.by, msg, 0);

                // The narrowing the engine cannot avoid: a Player's position
                // is a float, and so is everything BlockToWorld hands back.
                int fcx = 0, fcy = 0;
                WorldToCell(static_cast<float>(x), static_cast<float>(y), fcx, fcy);
                std::snprintf(msg, sizeof(msg),
                              "block (%d,%d) local %s divides to block (%d,%d) - float path",
                              b.bx, b.by, p.what,
                              fcx / PD_CELLS_PER_BLOCK, fcy / PD_CELLS_PER_BLOCK);
                Check(fcx >= 0 && fcy >= 0 &&
                      fcx / PD_CELLS_PER_BLOCK == b.bx &&
                      fcy / PD_CELLS_PER_BLOCK == b.by, msg, 0);
            }
        }
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

        std::printf("\n%s\n", ChainSummary(plan).c_str());

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

    // --- Round B chain arithmetic (spec 2026-09-02 §2) ----------------------
    //
    // Pure functions, so they are checked against a table rather than against
    // themselves: pockets = min(branches, total / 3, (total - 1 - N) / 2),
    // chainLen = total - pockets, boss k at round(k * (L - 1) / N).
    void RunChainMathChecks()
    {
        char msg[200];
        struct Row { int rooms; int boss; int branches; int pockets; int chainLen; int b1; int b2; };
        Row const rows[] = {
            {  1, 1, 2, 0,  2, 1, -1 },
            {  2, 1, 2, 0,  3, 2, -1 },
            {  3, 1, 2, 1,  3, 2, -1 },
            {  5, 1, 2, 2,  4, 3, -1 },
            {  8, 1, 2, 2,  7, 6, -1 },
            { 12, 2, 2, 2, 12, 6, 11 },
            { 15, 2, 2, 2, 15, 7, 14 },
            {  5, 1, 0, 0,  6, 5, -1 },
            {  1, 2, 2, 0,  3, 1,  2 },
            {  5, 1, 9, 2,  4, 3, -1 },
            { 15, 2, 9, 5, 12, 6, 11 },
            {  5, 0, 2, 1,  4, 3, -1 },     // bossRooms 0 still means one boss
        };
        for (Row const& r : rows)
        {
            int const pockets = PocketCountFor(r.rooms, r.boss, r.branches);
            std::snprintf(msg, sizeof(msg), "PocketCountFor(%d,%d,%d) = %d, want %d",
                          r.rooms, r.boss, r.branches, pockets, r.pockets);
            Check(pockets == r.pockets, msg, 0);

            int const total = std::max(2, r.rooms + r.boss);
            int const chainLen = total - pockets;
            std::snprintf(msg, sizeof(msg), "chainLen for (%d,%d,%d) = %d, want %d",
                          r.rooms, r.boss, r.branches, chainLen, r.chainLen);
            Check(chainLen == r.chainLen, msg, 0);

            int const b1 = BossChainIndex(chainLen, r.boss, 1);
            std::snprintf(msg, sizeof(msg), "boss 1 for (%d,%d,%d) at %d, want %d",
                          r.rooms, r.boss, r.branches, b1, r.b1);
            Check(b1 == r.b1, msg, 0);
            if (r.b2 >= 0)
            {
                int const b2 = BossChainIndex(chainLen, r.boss, 2);
                std::snprintf(msg, sizeof(msg), "boss 2 for (%d,%d,%d) at %d, want %d",
                              r.rooms, r.boss, r.branches, b2, r.b2);
                Check(b2 == r.b2, msg, 0);
            }
        }

        // Every legal engine request seats N distinct bosses at index >= 1:
        // chainLen - 1 >= N is what the host clamp guarantees. The boss loop
        // runs to 4 because GameBossRooms(30) is 4 and the conf's DlvlCap is
        // 30 - stopping at 3 left the top of the engine's band unmeasured.
        for (int rooms = 1; rooms <= 15; ++rooms)
        {
            for (int boss = 1; boss <= 4; ++boss)
            {
                int const total = std::max(2, rooms + boss);
                int const chainLen = total - PocketCountFor(rooms, boss, 2);
                std::snprintf(msg, sizeof(msg),
                              "the pocket clamp left no room for the bosses at (%d rooms, %d boss)",
                              rooms, boss);
                Check(chainLen - 1 >= boss, msg, 0);
                int last = 0;
                for (int k = 1; k <= boss; ++k)
                {
                    int const idx = BossChainIndex(chainLen, boss, k);
                    std::snprintf(msg, sizeof(msg),
                                  "boss positions not strictly increasing at (%d rooms, %d boss): "
                                  "boss %d at %d, previous at %d",
                                  rooms, boss, k, idx, last);
                    Check(idx > last && idx <= chainLen - 1, msg, 0);
                    last = idx;
                }
                std::snprintf(msg, sizeof(msg),
                              "the last boss is not the last chain room at (%d rooms, %d boss)",
                              rooms, boss);
                Check(last == chainLen - 1, msg, 0);
            }
        }

        // The struct defaults the later tasks rely on.
        PlacedBlock const fresh;
        Check(fresh.chainIndex == -1 && fresh.branchOf == -1 && fresh.detourOf == -1,
              "PlacedBlock chain fields must default to -1", 0);
        BlockCfg const cfg;
        Check(cfg.branches == 2, "BlockCfg::branches must default to 2", 0);
        Check(cfg.detourChancePct == 33, "BlockCfg::detourChancePct must default to 33", 0);
    }

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

        // The level-cost chain: each level costs 10 % more than the one before,
        // integer floor at every step. These ten numbers ARE the curve - if one
        // of them moves, every stored dxp means a different level than it did.
        {
            uint32_t const WANT[10] = { 100, 110, 121, 133, 146, 160, 176, 193, 212, 233 };
            bool chain = true;
            uint32_t total = 0;
            int badAt = 0;
            for (int n = 0; n < 10; ++n)
            {
                uint32_t const cost = GameDlvlCost(n, 100);
                if (cost != WANT[n])
                {
                    chain = false;
                    badAt = n;
                }
                total += cost;
            }
            std::snprintf(msg, sizeof(msg),
                          "level cost chain broke at level %d (got %u, want %u)",
                          badAt, GameDlvlCost(badAt, 100), WANT[badAt]);
            Check(chain, msg, 0);
            std::snprintf(msg, sizeof(msg),
                          "reaching dlvl 10 must cost 1584 dxp at PerDlvl 100, not %u", total);
            Check(total == 1584u, msg, 0);

            Check(GameDlvlCost(0, 100) == 100u,
                  "the first level must cost V2.XP.PerDlvl exactly", 0);
            Check(GameDlvlCost(5, 0) == 0u && GameDlvlCost(-3, 100) == 100u,
                  "a zero curve costs nothing and a negative level is level 0", 0);
        }

        // dxp -> dlvl, and the run reward that feeds it.
        {
            Check(GameDlvlFromDxp(0, 100, 30) == 0, "no dxp means dlvl 0", 0);
            Check(GameDlvlFromDxp(99, 100, 30) == 0, "a partial level is not a level", 0);
            Check(GameDlvlFromDxp(100, 100, 30) == 1, "one level's dxp is one dlvl", 0);
            Check(GameDlvlFromDxp(209, 100, 30) == 1 && GameDlvlFromDxp(210, 100, 30) == 2,
                  "the second level must cost 110 on top of the first", 0);
            Check(GameDlvlFromDxp(1584, 100, 30) == 10 &&
                  GameDlvlFromDxp(1583, 100, 30) == 9,
                  "1584 lifetime dxp must be exactly dlvl 10", 0);
            Check(GameDlvlFromDxp(0xFFFFFFFFu, 1, 30) == 30,
                  "the cap must hold however much dxp arrives", 0);
            Check(GameDlvlFromDxp(1000, 0, 30) == 0, "a zero curve must not level anyone", 0);
            Check(GameDxpIntoLevel(1584, 100, 30) == 0u &&
                  GameDxpIntoLevel(1583, 100, 30) == 232u,
                  "the bar must restart at 0 on a level and sit one short below it", 0);

            // The pair has to reconstruct the lifetime total exactly, or the
            // panel's bar and the level beside it are describing different
            // players. Swept rather than spot-checked: this is the invariant
            // that broke when the display was cumulative (operator report
            // 2026-08-08, "100 / 200 XP" after the first level-up).
            bool exact = true, fits = true, capped = true;
            uint32_t badDxp = 0;
            for (uint32_t dxp = 0; dxp <= 20000; dxp += 13)
            {
                int const dlvl = GameDlvlFromDxp(dxp, 100, 30);
                uint32_t const into = GameDxpIntoLevel(dxp, 100, 30);

                uint32_t spent = 0;
                for (int n = 0; n < dlvl; ++n)
                {
                    spent += GameDlvlCost(n, 100);
                }
                if (spent + into != dxp)
                {
                    exact = false;
                    badDxp = dxp;
                }
                // Below the cap the remainder must be short of the next level,
                // or a level-up was missed. AT the cap it may run past, because
                // there is no next level to spend it on.
                if (dlvl < 30 && into >= GameDlvlCost(dlvl, 100))
                {
                    fits = false;
                    badDxp = dxp;
                }
                if (dlvl > 30)
                {
                    capped = false;
                    badDxp = dxp;
                }
            }
            std::snprintf(msg, sizeof(msg),
                          "spent + into did not add up to the lifetime dxp (at %u)", badDxp);
            Check(exact, msg, 0);
            std::snprintf(msg, sizeof(msg),
                          "a level-up was left unpaid below the cap (at %u)", badDxp);
            Check(fits, msg, 0);
            std::snprintf(msg, sizeof(msg), "the dlvl cap was exceeded (at %u)", badDxp);
            Check(capped, msg, 0);

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

            // The planning field follows the room count (2026-08-10: three rooms
            // in a full 8x8 field made the corridors the whole run). Three
            // properties, and the last one is the load-bearing one.
            Check(GameFieldBlocksForRooms(1) == PD_GAME_FIELD_BLOCKS_MIN &&
                  GameFieldBlocksForRooms(0) == PD_GAME_FIELD_BLOCKS_MIN &&
                  GameFieldBlocksForRooms(-4) == PD_GAME_FIELD_BLOCKS_MIN,
                  "a degenerate room count must still yield the minimum field", 0);

            bool fieldMono = true;
            int fieldBadAt = 0;
            for (int r = 1; r < 64; ++r)
            {
                if (GameFieldBlocksForRooms(r + 1) < GameFieldBlocksForRooms(r))
                {
                    fieldMono = false;
                    fieldBadAt = r;
                    break;
                }
            }
            std::snprintf(msg, sizeof(msg),
                          "field shrank while rooms grew (at %d rooms)", fieldBadAt);
            Check(fieldMono, msg, 0);

            // NEVER wider than one ADT tile, whatever the room count: the client
            // composes a single tile and a multi-tile plan is untested there.
            bool fieldCapped = true;
            int fieldCapBadAt = 0;
            for (int r = 1; r < 512; ++r)
            {
                if (GameFieldBlocksForRooms(r) > PD_GAME_FIELD_BLOCKS_HARD_MAX)
                {
                    fieldCapped = false;
                    fieldCapBadAt = r;
                    break;
                }
            }
            std::snprintf(msg, sizeof(msg),
                          "field exceeded one ADT tile (at %d rooms)", fieldCapBadAt);
            Check(fieldCapped, msg, 0);

            // And it must actually be SMALLER for the small dungeons that
            // prompted the change - otherwise the whole thing is a no-op.
            Check(GameFieldBlocksForRooms(3) < PD_GAME_FIELD_BLOCKS_HARD_MAX,
                  "a three-room dungeon must plan in less than a full tile", 0);

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
        // Re-pinned for B2's third Room look: AltCountFor(Room) is 3, so the
        // room's alt draw maps one unchanged raw value onto 0..2 instead of
        // 0..1 and this layout's two spine rooms became alt 2 (chunk 4011).
        // The manifest keeps its LENGTH - a four-digit id either way - and
        // only the CRC moves, which is exactly why the trailer is pinned
        // beside the byte count. PD_LAYOUT_VERSION stays 3 (spec decision 10;
        // nothing is deployed). The pin before this one was 403 / E;c1478940
        // (B0b's loop rooms: one Chance per boss segment before any chain
        // step), before that 383 / E;0eeda3ad (B0b task 1, the shortcut draw
        // withdrawn), the B0 pin 363 / E;a5019024, the v2 pin
        // 571 / E;85fc0e4c, the v1 pin 551 / E;13df5510.
        uint32_t const PINNED_SEED = 12345u;
        int const PINNED_ROOMS = 5;
        size_t const PINNED_BYTES = 403;
        char const* const PINNED_TRAILER = "E;6576f540\n";
        // The failure message names the pin this one REPLACED, so whoever
        // reads it can tell a fresh move from the B0b re-roll. Kept as
        // constants beside the live pin: the message used to pair the current
        // byte count with the previous trailer, which read as a third value
        // that never existed.
        size_t const PREVIOUS_BYTES = 403;
        char const* const PREVIOUS_TRAILER = "E;c1478940";

        BlockCfg cfg = MakeCfg(PINNED_SEED, PINNED_ROOMS);
        cfg.bossRooms = 1;

        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            Check(false, "the pinned layout no longer generates at all", PINNED_SEED);
            return;
        }

        std::string const m = EmitManifest(plan, 1);
        size_t const trailer = std::strlen(PINNED_TRAILER);
        std::string const actualTrailer =
            m.size() >= trailer ? m.substr(m.size() - trailer) : m;
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "pinned manifest is %d bytes / %.*s, the pin says %d / %.*s "
                      "(the pin before B0b was %d / %s) - the bossRooms=1 "
                      "layout MOVED",
                      static_cast<int>(m.size()),
                      static_cast<int>(actualTrailer.size() ? actualTrailer.size() - 1 : 0),
                      actualTrailer.c_str(),
                      static_cast<int>(PINNED_BYTES),
                      static_cast<int>(trailer ? trailer - 1 : 0), PINNED_TRAILER,
                      static_cast<int>(PREVIOUS_BYTES), PREVIOUS_TRAILER);
        Check(m.size() == PINNED_BYTES, msg, PINNED_SEED);

        bool const same = m.size() >= trailer &&
                          m.compare(m.size() - trailer, trailer, PINNED_TRAILER) == 0;
        Check(same, msg, PINNED_SEED);
    }

    // Phase 2: dead-end stubs and visual alternates.
    //
    // Structure first - every (role, mask, alt) the planner can emit must have
    // a walk mask in the shipped SQL, or a dungeon would generate a chunkId the
    // server cannot path over (the "0 masks = mobs stand still" failure, but
    // per block). Then non-vacuity over real seeds: every alternate a family
    // ships (the Room's third one included) and at least one dead end must
    // actually OCCUR, or the draws are dead code the batch quietly stopped
    // exercising.
    void RunPhase2Checks(int seeds)
    {
        // Rooms ship all 15 masks; straight corridors the two facing pairs;
        // dead ends exactly the four single bits. Both theme namespaces must
        // be complete - the planner can aim at either, and a missing row is
        // the per-block "mobs stand still" failure.
        int const themeBases[2] = { 2000, 12000 };
        for (int base : themeBases)
        for (unsigned m = 1; m <= 15; ++m)
        {
            // Round B / B2: each ROLE is bounded by its OWN alt count, not by
            // the room's. The three no longer agree - the 33 yd platform is
            // Room-only - and sweeping the entrance and the boss up to the
            // room's count would demand ids the kit never ships.
            for (BlockRole role : { BlockRole::Room, BlockRole::RoomEntrance,
                                    BlockRole::RoomBoss })
            {
                for (int alt = 0; alt < AltCountFor(role); ++alt)
                {
                    int const id = base + alt * 1000 + static_cast<int>(role) * 100
                                 + static_cast<int>(m);
                    Check(MaskFor(id) != nullptr,
                          "a room (role,mask,alt) combination has no walk mask in the SQL",
                          static_cast<uint32_t>(id));
                }
            }
        }
        unsigned const straightMasks[2] = { SOCKET_N | SOCKET_S, SOCKET_E | SOCKET_W };
        for (int base : themeBases)
        for (unsigned m : straightMasks)
        {
            for (int alt = 0; alt < AltCountFor(BlockRole::CorridorStraight); ++alt)
            {
                int const id = base + alt * 1000 + 300 + static_cast<int>(m);
                Check(MaskFor(id) != nullptr,
                      "a straight-corridor alt has no walk mask in the SQL",
                      static_cast<uint32_t>(id));
            }
        }
        unsigned const stubMasks[4] = { SOCKET_N, SOCKET_E, SOCKET_S, SOCKET_W };
        for (int base : themeBases)
        for (unsigned m : stubMasks)
        {
            int const id = base + 700 + static_cast<int>(m);
            Check(MaskFor(id) != nullptr,
                  "a dead-end mask has no walk mask in the SQL",
                  static_cast<uint32_t>(id));
        }

        bool sawAltRoom = false;
        bool sawAlt2Room = false;
        bool sawAltStraight = false;
        bool sawDeadEnd = false;
        for (int i = 0; i < seeds; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2246822519u + 3u;
            BlockCfg const cfg = MakeCfg(seed, 5);
            BlockPlan plan;
            if (!GenerateBlockPlan(cfg, &plan))
            {
                continue;   // the batch already fails hard on generation
            }
            int stubs = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.role == BlockRole::CorridorDeadEnd)
                {
                    ++stubs;
                    sawDeadEnd = true;
                    Check(b.chunkId == 2700 + static_cast<int>(b.socketMask),
                          "dead-end chunkId is not 2700 + mask", seed);
                }
                else if (b.alt > 0)
                {
                    bool const isRoom = b.role == BlockRole::Room ||
                                        b.role == BlockRole::RoomEntrance ||
                                        b.role == BlockRole::RoomBoss;
                    if (isRoom) sawAltRoom = true;
                    // The third look is Room-only, so it needs its own
                    // witness: sawAltRoom is already true from an alt-1
                    // entrance or boss and would hide an alt-2 draw that
                    // never happens.
                    if (b.role == BlockRole::Room && b.alt == 2) sawAlt2Room = true;
                    if (b.role == BlockRole::CorridorStraight) sawAltStraight = true;
                }
            }
            Check(stubs <= cfg.maxDeadEnds,
                  "more dead ends than the config allows", seed);
        }
        Check(sawDeadEnd, "no seed in the sample produced a dead end - the "
                          "stub pass is dead code", 0);
        Check(sawAltRoom, "no seed produced an alt-1 room - the alternate draw "
                          "is dead code", 0);
        Check(sawAlt2Room, "no seed produced an alt-2 (33 yd) room", 0);
        Check(sawAltStraight, "no seed produced an S-curve corridor", 0);
    }

    // Theme 2 must be the SAME dungeon under different art: the theme moves
    // only the chunkId base, never a draw. Same seed -> block-identical
    // layout, ids offset by exactly the namespace distance, and the walk grid
    // builds from the theme-2 masks (which may differ per variant - city ring
    // rooms - without touching the layout structure).
    void RunThemeParityChecks(int seeds)
    {
        for (int i = 0; i < seeds; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2246822519u + 11u;
            BlockCfg cfgMine = MakeCfg(seed, 6);
            BlockCfg cfgCity = cfgMine;
            cfgCity.theme = 2;

            BlockPlan mine;
            BlockPlan city;
            if (!GenerateBlockPlan(cfgMine, &mine) ||
                !GenerateBlockPlan(cfgCity, &city))
            {
                Check(false, "a theme failed to generate where the other could", seed);
                continue;
            }
            Check(mine.blocks.size() == city.blocks.size(),
                  "theme 2 laid out a different block count", seed);
            if (mine.blocks.size() != city.blocks.size())
            {
                continue;
            }
            bool same = true;
            for (size_t k = 0; k < mine.blocks.size(); ++k)
            {
                PlacedBlock const& a = mine.blocks[k];
                PlacedBlock const& b = city.blocks[k];
                if (a.bx != b.bx || a.by != b.by || a.role != b.role ||
                    a.socketMask != b.socketMask || a.alt != b.alt ||
                    a.chainIndex != b.chainIndex || a.branchOf != b.branchOf ||
                    a.detourOf != b.detourOf ||
                    b.chunkId - a.chunkId != 10000)
                {
                    same = false;
                }
            }
            Check(same, "theme 2 is not the same layout with ids moved by the "
                        "namespace distance", seed);

            WalkGrid grid;
            std::string why;
            if (!BuildWalkGrid(city, MaskFor, &grid, &why))
            {
                Check(false, "theme-2 walk grid failed to build", seed);
                continue;
            }
            int longest = 0;
            Check(CheckAllRoomsConnected(city, grid, why, longest),
                  "a theme-2 dungeon has unreachable rooms", seed);
        }
    }

    // --- Round B: the spine (spec 2026-09-02 §7.1) --------------------------
    //
    // Re-derived from the plan's blocks and sockets, never from the planner's
    // own bookkeeping, so a bug in the validator cannot hide here (the same
    // stance EdgesAgree takes). Runs over its own seeds and its own room /
    // boss matrix, because the batch only ever asks for one boss room.
    // Socket flood from `startBlock`, never entering `skipBlock` (-1 = none).
    void FloodFrom(BlockPlan const& plan, int startBlock, int skipBlock, std::vector<bool>& seen)
    {
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }
        seen.assign(plan.blocks.size(), false);
        if (startBlock < 0 || startBlock == skipBlock)
        {
            return;
        }
        std::vector<size_t> stack;
        stack.push_back(static_cast<size_t>(startBlock));
        seen[static_cast<size_t>(startBlock)] = true;
        struct Dir { unsigned bit; int dx; int dy; };
        Dir const dirs[4] = { { SOCKET_N, 0, -1 }, { SOCKET_E, 1, 0 }, { SOCKET_S, 0, 1 }, { SOCKET_W, -1, 0 } };
        while (!stack.empty())
        {
            size_t const at = stack.back();
            stack.pop_back();
            PlacedBlock const& b = plan.blocks[at];
            for (Dir const& d : dirs)
            {
                if (!(b.socketMask & d.bit)) continue;
                auto it = index.find(std::make_pair(b.bx + d.dx, b.by + d.dy));
                if (it == index.end()) continue;
                if (static_cast<int>(it->second) == skipBlock) continue;
                if (seen[it->second]) continue;
                seen[it->second] = true;
                stack.push_back(it->second);
            }
        }
    }

    // Walk the corridor run behind one socket of block `from` and report the
    // first ROOM it reaches: -1 when the run ends in a chest stub or in
    // nothing. Deliberately the harness's own walk rather than a call into
    // ValidateBlockPlan - the validator grew the same rule in this wave and
    // the point of these checks is that a bug in it cannot hide here.
    //
    // B0b: `attachments` holds the two cells of a loop room's run that carry
    // the strip. They are the only three-socket corridors a layout admits, and
    // a run entering one along the run leaves it straight ahead; entered from
    // the strip side it is the end of the walk, not a through route.
    int RoomAtEndOf(BlockPlan const& plan, size_t from, unsigned bit,
                    std::set<size_t> const& attachments)
    {
        struct Dir { unsigned bit; int dx; int dy; unsigned opp; };
        Dir const dirs[4] = {
            { SOCKET_N,  0, -1, SOCKET_S },
            { SOCKET_E,  1,  0, SOCKET_W },
            { SOCKET_S,  0,  1, SOCKET_N },
            { SOCKET_W, -1,  0, SOCKET_E },
        };
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        }
        size_t prev = from;
        unsigned entry = bit;
        for (size_t steps = 0; steps <= plan.blocks.size(); ++steps)
        {
            Dir const* in = nullptr;
            for (Dir const& d : dirs) if (d.bit == entry) in = &d;
            if (!in) return -1;
            auto it = index.find(std::make_pair(plan.blocks[prev].bx + in->dx,
                                                plan.blocks[prev].by + in->dy));
            if (it == index.end()) return -1;
            size_t const at = it->second;
            PlacedBlock const& b = plan.blocks[at];
            if (b.roomId >= 0) return static_cast<int>(at);
            if (b.role == BlockRole::CorridorDeadEnd) return -1;
            unsigned next = 0;
            int outs = 0;
            for (Dir const& d : dirs)
            {
                if (!(b.socketMask & d.bit) || d.bit == in->opp) continue;
                auto n = index.find(std::make_pair(b.bx + d.dx, b.by + d.dy));
                if (n != index.end() &&
                    plan.blocks[n->second].role == BlockRole::CorridorDeadEnd) continue;
                ++outs;
                next = d.bit;
            }
            if (outs != 1)
            {
                bool const isAttachment = attachments.count(at) != 0;
                bool const alongRun = (b.socketMask & entry) != 0;
                // The one sanctioned fork: a loop attachment entered ALONG the
                // run continues straight through it.
                if (isAttachment && alongRun && outs == 2)
                {
                    next = entry;
                }
                else if (isAttachment && !alongRun)
                {
                    // Entered from the STRIP side (the entry socket is not one
                    // of the attachment's own): design 2026-09-03 §4 calls that
                    // the end of the strip, not a fork. The engine's WalkRun
                    // ends the run here WITHOUT reporting a junction; the walk
                    // ends either way, which is all this one returns.
                    return -1;
                }
                else
                {
                    return -1;      // a junction, and the junction rule catches it
                }
            }
            prev = at;
            entry = next;
        }
        return -1;
    }

    void RunChainChecks(int seeds, bool& sawPocket, bool& sawDetour)
    {
        char msg[224];
        // The engine's real configuration space, not a diagonal of it:
        // bossRooms reaches 4 at the conf's DlvlCap 30, branches and
        // detourChancePct are both operator keys whose extremes sit on their
        // own draw streams (PDRandom's no-draw contract at a single candidate
        // and at Chance 0/100), and bossRooms 0 is reachable from the server
        // config for an account with no row.
        struct Combo
        {
            int rooms;
            int bossRooms;
            int branches = 2;
            int detourPct = 15;
        };
        Combo const combos[] = {
            { 8, 1 }, { 8, 2 }, { 8, 3 }, { 15, 2 }, { 3, 1 }, { 1, 1 },
            { 4, 4 }, { 2, 4 }, { 1, 4 },                   // dlvl 30's boss count
            { 8, 1, 0, 15 },                                // V2.Branches 0: no pockets at all
            { 8, 1, 2, 0 },                                 // V2.DetourChance 0: Chance draws nothing
            { 8, 1, 2, 100 },                               // V2.DetourChance 100: same, other way
            { 5, 0, 2, 15 },                                // bossRooms 0 still means one boss
        };
        for (Combo const& combo : combos)
        {
            // Per COMBO, not per seed: the two DetourChance edge combos assert
            // over the whole sample (100 must yield at least one loop room,
            // 0 must yield none), which is a statement about the draw, not
            // about any single layout.
            bool sawDetourHere = false;
            for (int i = 0; i < seeds; ++i)
            {
                uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 7u;
                BlockCfg cfg = MakeCfg(seed, combo.rooms);
                cfg.bossRooms = combo.bossRooms;
                cfg.branches = combo.branches;
                cfg.detourChancePct = combo.detourPct;

                BlockPlan plan;
                if (!GenerateBlockPlan(cfg, &plan))
                {
                    std::snprintf(msg, sizeof(msg),
                                  "generation failed with %d rooms + %d boss, branches %d, loop %d%%",
                                  combo.rooms, combo.bossRooms, combo.branches, combo.detourPct);
                    Check(false, msg, seed);
                    continue;
                }

                // Own arithmetic, deliberately not PocketCountFor. N is the
                // boss count the PLANNER seats - one even when the config says
                // zero - and the pocket ceiling is cfg.branches, not the 2 this
                // used to hardcode.
                int const N = std::max(1, combo.bossRooms);
                int const total = std::max(2, combo.rooms + combo.bossRooms);
                int wantPockets = std::min(std::max(0, cfg.branches), total / 3);
                wantPockets = std::min(wantPockets, std::max(0, (total - 1 - N) / 2));
                int const wantChain = total - wantPockets;

                std::vector<int> chainBlock(static_cast<size_t>(wantChain), -1);
                int pockets = 0, rooms = 0, bosses = 0, strays = 0;
                for (size_t k = 0; k < plan.blocks.size(); ++k)
                {
                    PlacedBlock const& b = plan.blocks[k];
                    if (b.roomId < 0)
                    {
                        Check(b.chainIndex < 0 && b.branchOf < 0 && b.detourOf < 0,
                              "a corridor block carries chain fields", seed);
                        continue;
                    }
                    ++rooms;
                    if (b.role == BlockRole::RoomBoss) ++bosses;
                    if (b.chainIndex >= 0)
                    {
                        if (b.chainIndex < wantChain && chainBlock[static_cast<size_t>(b.chainIndex)] < 0)
                        {
                            chainBlock[static_cast<size_t>(b.chainIndex)] = static_cast<int>(k);
                        }
                        else
                        {
                            ++strays;
                        }
                    }
                    else if (b.branchOf >= 0)
                    {
                        ++pockets;
                    }
                    else if (b.detourOf >= 0)
                    {
                        // A loop room (B0b) is a legitimate third kind of room;
                        // it is counted and checked in its own pass below.
                    }
                    else
                    {
                        ++strays;
                    }
                }
                Check(strays == 0, "a room is neither on the chain, a pocket nor a loop room (or a chain index repeats)", seed);
                std::snprintf(msg, sizeof(msg), "%d pocket(s), want %d", pockets, wantPockets);
                Check(pockets == wantPockets, msg, seed);
                Check(bosses == N, "boss room count does not match the config", seed);
                bool chainComplete = true;
                for (int idx : chainBlock) if (idx < 0) chainComplete = false;
                Check(chainComplete, "a chain index is missing", seed);
                if (!chainComplete) continue;
                if (pockets > 0) sawPocket = true;

                // Entrance, last boss, boss positions by the formula.
                Check(plan.entranceIndex == chainBlock[0] &&
                      plan.blocks[static_cast<size_t>(chainBlock[0])].role == BlockRole::RoomEntrance,
                      "chain 0 is not the entrance", seed);
                Check(plan.bossIndex == chainBlock[static_cast<size_t>(wantChain - 1)] &&
                      plan.blocks[static_cast<size_t>(plan.bossIndex)].role == BlockRole::RoomBoss,
                      "bossIndex is not the last chain room, or it is not a boss", seed);
                std::vector<bool> isBossIdx(static_cast<size_t>(wantChain), false);
                for (int k = 1; k <= N; ++k)
                {
                    int const want = (2 * k * (wantChain - 1) + N) / (2 * N);
                    isBossIdx[static_cast<size_t>(want)] = true;
                    std::snprintf(msg, sizeof(msg), "boss %d is not at chain index %d", k, want);
                    Check(plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(want)])].role == BlockRole::RoomBoss,
                          msg, seed);
                }
                for (int idx = 1; idx < wantChain; ++idx)
                {
                    PlacedBlock const& b = plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(idx)])];
                    Check(isBossIdx[static_cast<size_t>(idx)] == (b.role == BlockRole::RoomBoss),
                          "a boss sits off its formula position", seed);
                    // Every spine room that is not a boss is a plain Room: the
                    // entrance role belongs to chain 0 alone (final review, M1).
                    if (!isBossIdx[static_cast<size_t>(idx)])
                    {
                        Check(b.role == BlockRole::Room, "a spine room carries the wrong role", seed);
                    }
                    // SegmentOf agrees with the formula.
                    int wantSeg = N;
                    for (int k = 1; k <= N; ++k)
                    {
                        if (idx <= (2 * k * (wantChain - 1) + N) / (2 * N)) { wantSeg = k; break; }
                    }
                    Check(SegmentOf(plan, b) == wantSeg, "SegmentOf disagrees with the boss positions", seed);
                }
                Check(SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[0])]) == 0,
                      "the entrance is not segment 0", seed);

                // Loop rooms (B0b): re-derived from the sockets. R has exactly
                // two opposite sockets; the corners, the two attachment cells
                // and the straight middle cell have exactly the masks the
                // geometry demands; the run's two ends are chain rooms
                // detourOf-1 and detourOf (either orientation).
                //
                // Placed BEFORE the spine-adjacency walk, not after the pocket
                // pass: the attachment cells are the only corridors the walk is
                // allowed to pass straight through, so it needs the set.
                //
                // The mask without sockets that lead to chest stubs - the same
                // normalisation the junction rule and the corridor walk use.
                auto const StubsOff = [&](PlacedBlock const* c) -> unsigned
                {
                    unsigned out = 0;
                    for (unsigned bit = 1; c && bit <= SOCKET_W; bit <<= 1)
                    {
                        if (!(c->socketMask & bit)) continue;
                        int ex = 0, ey = 0;
                        if (bit == SOCKET_N) ey = -1; else if (bit == SOCKET_S) ey = 1;
                        else if (bit == SOCKET_W) ex = -1; else ex = 1;
                        PlacedBlock const* n = plan.At(c->bx + ex, c->by + ey);
                        if (n && n->role == BlockRole::CorridorDeadEnd) continue;
                        out |= bit;
                    }
                    return out;
                };
                std::set<size_t> attachments;
                std::vector<bool> loopInSegment(static_cast<size_t>(N + 1), false);
                int loops = 0;
                for (size_t at = 0; at < plan.blocks.size(); ++at)
                {
                    PlacedBlock const& b = plan.blocks[at];
                    if (b.detourOf < 0) continue;
                    ++loops;
                    if (b.detourOf > 0 && b.detourOf < wantChain) sawDetourHere = true;
                    Check(b.role == BlockRole::Room && b.chainIndex < 0 && b.branchOf < 0,
                          "a loop room carries the wrong role or fields", seed);
                    bool const intoOk = b.detourOf >= 1 && b.detourOf < wantChain;
                    Check(intoOk, "a loop room's run leads into no chain room", seed);
                    if (!intoOk) continue;
                    int const seg = SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf)])]);
                    Check(seg >= 1 && seg <= N && SegmentOf(plan, b) == seg, "a loop room is not in its run's segment", seed);
                    if (seg >= 1 && seg <= N)
                    {
                        Check(!loopInSegment[static_cast<size_t>(seg)], "two loop rooms in one segment", seed);
                        loopInSegment[static_cast<size_t>(seg)] = true;
                    }
                    // Stub-normalised throughout: the stub pass is free to hang
                    // a chest alcove off any cell of the loop, the loop room
                    // included (design 2026-09-03 §2.5).
                    unsigned const m = StubsOff(&b);
                    bool const opposite = (m == (SOCKET_N | SOCKET_S)) || (m == (SOCKET_E | SOCKET_W));
                    Check(opposite, "a loop room does not have exactly two opposite sockets", seed);
                    if (!opposite) continue;
                    int dx = 0, dy = 0;
                    if (m == (SOCKET_N | SOCKET_S)) { dx = 0; dy = 1; } else { dx = 1; dy = 0; }
                    // The strip: S1 = R - d, S2 = R + d, both corners sharing a side t.
                    PlacedBlock const* s1 = plan.At(b.bx - dx, b.by - dy);
                    PlacedBlock const* s2 = plan.At(b.bx + dx, b.by + dy);
                    unsigned const dBit = (dy > 0) ? SOCKET_S : SOCKET_E;
                    unsigned const dOpp = (dy > 0) ? SOCKET_N : SOCKET_W;
                    unsigned const s1Mask = StubsOff(s1);
                    unsigned const s2Mask = StubsOff(s2);
                    bool cornersOk = s1 && s2 && s1->roomId < 0 && s2->roomId < 0 &&
                                     (s1Mask & dBit) && (s2Mask & dOpp);
                    unsigned const t1 = s1Mask & ~dBit;
                    unsigned const t2 = s2Mask & ~dOpp;
                    cornersOk = cornersOk && t1 == t2 && (t1 == SOCKET_N || t1 == SOCKET_E || t1 == SOCKET_S || t1 == SOCKET_W)
                                && t1 != dBit && t1 != dOpp;
                    Check(cornersOk, "a loop room's corners are not two matching corner corridors", seed);
                    if (!cornersOk) continue;
                    int tx = 0, ty = 0;
                    if (t1 == SOCKET_N) ty = -1; else if (t1 == SOCKET_S) ty = 1; else if (t1 == SOCKET_W) tx = -1; else tx = 1;
                    unsigned const tOpp = (t1 == SOCKET_N) ? SOCKET_S : (t1 == SOCKET_S) ? SOCKET_N : (t1 == SOCKET_W) ? SOCKET_E : SOCKET_W;
                    PlacedBlock const* m1 = plan.At(s1->bx + tx, s1->by + ty);
                    PlacedBlock const* m2 = plan.At(s2->bx + tx, s2->by + ty);
                    PlacedBlock const* mid = plan.At(b.bx + tx, b.by + ty);
                    unsigned const attachMask = dBit | dOpp | tOpp;
                    bool const runOk = m1 && m2 && mid && m1->roomId < 0 && m2->roomId < 0 && mid->roomId < 0 &&
                                       StubsOff(m1) == attachMask && StubsOff(m2) == attachMask &&
                                       StubsOff(mid) == (dBit | dOpp);
                    Check(runOk, "a loop room's run is not the straight three-cell run with two attachments", seed);
                    if (!runOk) continue;
                    for (size_t k = 0; k < plan.blocks.size(); ++k)
                    {
                        if (&plan.blocks[k] == m1 || &plan.blocks[k] == m2) attachments.insert(k);
                    }
                    PlacedBlock const* e1 = plan.At(m1->bx - dx, m1->by - dy);
                    PlacedBlock const* e2 = plan.At(m2->bx + dx, m2->by + dy);
                    PlacedBlock const* into = &plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf)])];
                    PlacedBlock const* before = &plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.detourOf - 1)])];
                    bool const endsOk = (e1 == before && e2 == into) || (e1 == into && e2 == before);
                    Check(endsOk, "a loop room's run does not join chain rooms detourOf-1 and detourOf", seed);

                    // B0b §4, through the ENGINE's public walk: every socket of
                    // a loop room leads into the strip (or, at most, into a
                    // chest stub hanging off it), and the walk there ends at
                    // the attachment cell it enters FROM THE STRIP SIDE. That
                    // is the end of the strip, not a fork - so the answer is
                    // "no room" WITHOUT a junction. This is the only place a
                    // strip-side entry is reachable at all, so without it the
                    // rule would be untested (B0b Task 2 review, item 1).
                    for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                    {
                        if (!(b.socketMask & bit)) continue;
                        bool junction = true;
                        int const to = RunFromSocket(plan, at, bit, nullptr, &junction);
                        Check(to < 0 && !junction,
                              "a walk out of a loop room does not end quietly at the strip", seed);
                    }
                }
                Check(loops <= N, "more loop rooms than segments", seed);
                Check(rooms == total + loops, "room count is not rooms + bossRooms + loop rooms", seed);
                // Non-vacuity over the WHOLE sample, exactly like sawPocket -
                // not a property of any single layout. Set here rather than
                // from the DetourChance-100 combo alone: that combo already
                // has its own per-combo assertion, and hanging the batch-wide
                // one off it would make it true by construction.
                if (loops > 0) sawDetour = true;

                // Spine adjacency (spec 7.1 invariant 3, final review M2):
                // consecutive chain rooms are joined by EXACTLY one corridor
                // run. Construction guarantees it today and one pinned seed
                // notices a move, but C5's respawn checkpoint and B5's patrol
                // key off the physical order, so it is asserted per seed here
                // - with the harness's own corridor walk, not the validator's.
                for (int idx = 1; idx < wantChain; ++idx)
                {
                    size_t const from = static_cast<size_t>(chainBlock[static_cast<size_t>(idx - 1)]);
                    int hits = 0;
                    for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                    {
                        if (!(plan.blocks[from].socketMask & bit)) continue;
                        if (RoomAtEndOf(plan, from, bit, attachments) == chainBlock[static_cast<size_t>(idx)]) ++hits;
                    }
                    std::snprintf(msg, sizeof(msg),
                                  "chain %d and %d are joined by %d corridor run(s), want 1",
                                  idx - 1, idx, hits);
                    Check(hits == 1, msg, seed);

                    // Round B / B3-B5: the ENGINE's own reader of that same
                    // run. SpineRunInto is what the barrier, the patrol and
                    // the ambush plan will call, and it walks the planner's
                    // shared WalkRun - so it is checked against the harness's
                    // independent RoomAtEndOf rather than against itself.
                    std::vector<size_t> run;
                    unsigned const entryBit = SpineRunInto(plan, idx, &run);
                    std::snprintf(msg, sizeof(msg),
                                  "SpineRunInto found no run into chain %d", idx);
                    Check(entryBit != 0, msg, seed);
                    if (entryBit == 0) continue;

                    std::snprintf(msg, sizeof(msg),
                                  "the run into chain %d is empty - a barrier would have no corridor to seal", idx);
                    Check(!run.empty(), msg, seed);
                    if (run.empty()) continue;

                    bool allCorridors = true;
                    for (size_t at : run)
                    {
                        PlacedBlock const& c = plan.blocks[at];
                        if (c.roomId >= 0 || c.role == BlockRole::CorridorDeadEnd) allCorridors = false;
                    }
                    std::snprintf(msg, sizeof(msg),
                                  "the run into chain %d holds a room or a chest stub", idx);
                    Check(allCorridors, msg, seed);

                    // WALKING ORDER, which is the half of the contract the
                    // block indices alone do not state: run[0] touches chain
                    // idx-1, run.back() touches chain idx, and every step in
                    // between is one block. B3 seats its portcullis on the
                    // last block, B4 starts its patrol there.
                    auto const Adjacent = [&](PlacedBlock const& p, PlacedBlock const& q)
                    {
                        int const d = std::abs(p.bx - q.bx) + std::abs(p.by - q.by);
                        return d == 1;
                    };
                    bool ordered = Adjacent(plan.blocks[run.front()],
                                            plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(idx - 1)])]) &&
                                   Adjacent(plan.blocks[run.back()],
                                            plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(idx)])]);
                    for (size_t k = 1; k < run.size(); ++k)
                    {
                        if (!Adjacent(plan.blocks[run[k - 1]], plan.blocks[run[k]])) ordered = false;
                    }
                    std::snprintf(msg, sizeof(msg),
                                  "the run into chain %d is not a contiguous walk from %d to %d",
                                  idx, idx - 1, idx);
                    Check(ordered, msg, seed);

                    // ...and it arrives through the socket SpineRunInto named:
                    // walking that socket backwards out of chain idx must land
                    // on chain idx-1, by the harness's own walk.
                    std::snprintf(msg, sizeof(msg),
                                  "SpineRunInto's socket for chain %d does not walk back to chain %d",
                                  idx, idx - 1);
                    Check(RoomAtEndOf(plan, static_cast<size_t>(chainBlock[static_cast<size_t>(idx)]),
                                      entryBit, attachments) == chainBlock[static_cast<size_t>(idx - 1)],
                          msg, seed);
                }

                // Round B / B5: the ambush plan over that same chain. At
                // chance 100 the coin is off and what is left is a statement
                // about WHERE a spot may sit - and the candidate set is
                // re-derived here from the harness's own RoomAtEndOf, never
                // from the SpineRunInto BuildAmbushPlan itself walks, so a bug
                // in that walk cannot hide behind its own output.
                {
                    // Which rooms each corridor block reaches through its own
                    // sockets, computed once for the layout. A corridor lies on
                    // the run between two chain rooms exactly when it reaches
                    // BOTH of them: consecutive chain rooms are joined by
                    // exactly one run (asserted above), so there is no second
                    // way to reach both. Loop-strip cells reach only their loop
                    // room (RoomAtEndOf ends the walk at an attachment entered
                    // from the strip side), chest stubs are skipped outright,
                    // and a pocket corridor reaches its host and its pocket -
                    // none of the three can qualify.
                    std::vector<std::set<int>> reach(plan.blocks.size());
                    for (size_t at = 0; at < plan.blocks.size(); ++at)
                    {
                        PlacedBlock const& c = plan.blocks[at];
                        if (c.roomId >= 0 || c.role == BlockRole::CorridorDeadEnd) continue;
                        for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                        {
                            if (!(c.socketMask & bit)) continue;
                            int const end = RoomAtEndOf(plan, at, bit, attachments);
                            if (end >= 0) reach[at].insert(end);
                        }
                    }

                    // The engine will hand BuildAmbushPlan the plan's own seed,
                    // exactly like the decor, critter and patrol draws.
                    std::vector<AmbushSpot> const hot = BuildAmbushPlan(plan, 100, plan.effectiveSeed);
                    std::vector<AmbushSpot> const twice = BuildAmbushPlan(plan, 100, plan.effectiveSeed);
                    bool determ = hot.size() == twice.size();
                    for (size_t s = 0; determ && s < hot.size(); ++s)
                    {
                        determ = hot[s].blockIndex == twice[s].blockIndex &&
                                 hot[s].bx == twice[s].bx && hot[s].by == twice[s].by &&
                                 hot[s].segment == twice[s].segment;
                    }
                    Check(determ, "two ambush plans from the same seed differ", seed);
                    Check(BuildAmbushPlan(plan, 0, plan.effectiveSeed).empty(),
                          "an ambush spot appeared at chance 0", seed);

                    int prevBoss = 0;
                    for (int k = 1; k <= N; ++k)
                    {
                        // The harness's own boss formula, the one the checks
                        // above use - not BossChainIndex.
                        int const bossAt = (2 * k * (wantChain - 1) + N) / (2 * N);
                        std::set<size_t> segRun;
                        for (int step = prevBoss + 1; step <= bossAt; ++step)
                        {
                            for (size_t at = 0; at < plan.blocks.size(); ++at)
                            {
                                if (reach[at].count(chainBlock[static_cast<size_t>(step - 1)]) != 0 &&
                                    reach[at].count(chainBlock[static_cast<size_t>(step)]) != 0)
                                {
                                    segRun.insert(at);
                                }
                            }
                        }

                        int spotsHere = 0;
                        for (AmbushSpot const& s : hot)
                        {
                            if (s.segment != k) continue;
                            ++spotsHere;
                            Check(s.blockIndex < plan.blocks.size(),
                                  "an ambush spot indexes past the plan", seed);
                            if (s.blockIndex >= plan.blocks.size()) continue;
                            Check(plan.blocks[s.blockIndex].bx == s.bx &&
                                  plan.blocks[s.blockIndex].by == s.by,
                                  "an ambush spot's coordinates are not its own block's", seed);
                            std::snprintf(msg, sizeof(msg),
                                          "segment %d's ambush spot is not a corridor on one of that segment's spine runs", k);
                            Check(segRun.count(s.blockIndex) != 0, msg, seed);
                        }
                        std::snprintf(msg, sizeof(msg),
                                      "segment %d has %d ambush spot(s) at chance 100, want %d",
                                      k, spotsHere, segRun.empty() ? 0 : 1);
                        Check(spotsHere == (segRun.empty() ? 0 : 1), msg, seed);
                        prevBoss = bossAt;
                    }

                    for (AmbushSpot const& s : hot)
                    {
                        std::snprintf(msg, sizeof(msg),
                                      "an ambush spot carries segment %d, outside 1..%d", s.segment, N);
                        Check(s.segment >= 1 && s.segment <= N, msg, seed);
                    }
                }

                // Pockets: host is an ordinary spine room, one pocket per host,
                // in the host's segment - and physically what the fields claim:
                // with the HOST removed, a flood from the pocket reaches no
                // spine room at all.
                std::vector<bool> hosted(static_cast<size_t>(wantChain), false);
                for (size_t at = 0; at < plan.blocks.size(); ++at)
                {
                    PlacedBlock const& b = plan.blocks[at];
                    if (b.branchOf < 0) continue;
                    Check(b.role == BlockRole::Room, "a pocket is not a plain room", seed);
                    bool const hostOk = b.branchOf >= 1 && b.branchOf < wantChain - 1 &&
                                        !isBossIdx[static_cast<size_t>(b.branchOf)];
                    Check(hostOk, "pocket hosted on the entrance, a boss or off the chain", seed);
                    if (!hostOk) continue;
                    Check(!hosted[static_cast<size_t>(b.branchOf)], "two pockets on one host", seed);
                    hosted[static_cast<size_t>(b.branchOf)] = true;
                    Check(SegmentOf(plan, b) ==
                          SegmentOf(plan, plan.blocks[static_cast<size_t>(chainBlock[static_cast<size_t>(b.branchOf)])]),
                          "a pocket is not in its host's segment", seed);

                    std::vector<bool> seen;
                    FloodFrom(plan, static_cast<int>(at), chainBlock[static_cast<size_t>(b.branchOf)], seen);
                    bool touchesSpine = false;
                    for (size_t k = 0; k < plan.blocks.size(); ++k)
                    {
                        if (seen[k] && plan.blocks[k].chainIndex >= 0) touchesSpine = true;
                    }
                    Check(!touchesSpine, "a dead-end pocket reaches the spine around its host", seed);
                }

                // No junction that is not a stub: a corridor block has exactly
                // two sockets leading to non-stub blocks - three for a loop
                // attachment, the only fork B0b sanctions.
                for (size_t k = 0; k < plan.blocks.size(); ++k)
                {
                    PlacedBlock const& b = plan.blocks[k];
                    if (b.roomId >= 0 || b.role == BlockRole::CorridorDeadEnd) continue;
                    int through = 0;
                    struct Dir { unsigned bit; int dx; int dy; };
                    Dir const dirs[4] = { { SOCKET_N, 0, -1 }, { SOCKET_E, 1, 0 }, { SOCKET_S, 0, 1 }, { SOCKET_W, -1, 0 } };
                    for (Dir const& d : dirs)
                    {
                        if (!(b.socketMask & d.bit)) continue;
                        PlacedBlock const* n = plan.At(b.bx + d.dx, b.by + d.dy);
                        if (n && n->role != BlockRole::CorridorDeadEnd) ++through;
                    }
                    Check(through == (attachments.count(k) ? 3 : 2),
                          "a corridor block is a junction (not a loop attachment)", seed);
                }

                // Boss cut property: without boss block k nothing behind it is
                // reachable from the entrance.
                for (int idx = 1; idx < wantChain; ++idx)
                {
                    if (!isBossIdx[static_cast<size_t>(idx)]) continue;
                    std::vector<bool> seen;
                    FloodFrom(plan, plan.entranceIndex, chainBlock[static_cast<size_t>(idx)], seen);
                    for (size_t k = 0; k < plan.blocks.size(); ++k)
                    {
                        PlacedBlock const& b = plan.blocks[k];
                        // detourOf is "behind" as well: a loop room whose run
                        // leads into chain room detourOf hangs off the corridor
                        // between detourOf-1 and detourOf, so a detourOf past
                        // the removed boss puts the whole strip behind it.
                        bool const behind = b.chainIndex > idx || b.branchOf > idx || b.detourOf > idx;
                        if (behind && seen[k])
                        {
                            std::snprintf(msg, sizeof(msg), "boss at chain %d can be bypassed", idx);
                            Check(false, msg, seed);
                            break;
                        }
                    }
                }
            }

            // The detour draw, asserted where PDRandom does NOT draw at all:
            // at 100 every segment wants a loop room, at 0 none does, so both
            // are statements about the whole sample rather than about a seed.
            if (combo.detourPct >= 100)
            {
                std::snprintf(msg, sizeof(msg), "no loop room at all over %d seeds with DetourChance 100 (%d rooms + %d boss)",
                              seeds, combo.rooms, combo.bossRooms);
                Check(sawDetourHere, msg, 0);
            }
            if (combo.detourPct <= 0)
            {
                Check(!sawDetourHere, "a loop room appeared with DetourChance 0", 0);
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

    RoomCapRow MeasureRoomCapRow(int rooms, int bossRooms, int seeds, int theme = 1)
    {
        RoomCapRow row;
        row.rooms = rooms;
        row.bossRooms = bossRooms;

        for (int i = 0; i < seeds; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 1u;
            BlockCfg cfg = MakeCfg(seed, rooms);
            cfg.bossRooms = bossRooms;
            cfg.theme = theme;

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
    int MeasureRoomCap(int seeds, bool verbose, int theme = 1)
    {
        if (verbose)
        {
            std::printf("room-cap measurement: %d seeds per row, field 8x8, "
                        "theme %d, manifest budget %d B\n\n", seeds, theme,
                        PD_GAME_MANIFEST_BUDGET_B);
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
            RoomCapRow const row = MeasureRoomCapRow(rooms, boss, seeds, theme);
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
        // Theme 2's five-digit chunk ids are the widest manifest lines, so
        // the cap has to hold there too - a city dungeon at the cap must not
        // blow the packet budget theme 1 measured its way under.
        std::printf("\n");
        int const measuredCity = MeasureRoomCap(seeds, true, 2);
        std::printf("\nlargest room count clean on every seed: %d (theme 1), "
                    "%d (theme 2)\n", measured, measuredCity);
        std::printf("PD_GAME_ROOMS_CAP_MEASURED currently encodes: %d\n",
                    PD_GAME_ROOMS_CAP_MEASURED);
        bool const ok = measured >= PD_GAME_ROOMS_CAP_MEASURED &&
                        measuredCity >= PD_GAME_ROOMS_CAP_MEASURED;
        std::printf("%s\n", ok ? "the encoded cap holds for both themes"
                                : "THE ENCODED CAP IS TOO HIGH - update PDv2GameMath.h");
        return ok ? 0 : 1;
    }

    // --- decor -------------------------------------------------------------
    //
    // The full 14 shipped rows of `pdungeon_decor_rules`, mirrored exactly
    // (id, entry, placement, min/max, weight, spacing): ids 1-3 from
    // mod_pdungeon_decor.sql, ids 4-14 from mod_pdungeon_decor_clutter.sql.
    // THE SQL IS THE RUNTIME SOURCE - both files under data/sql/db-world/ -
    // and this copy exists only so the placement can be checked without a
    // database. If one moves, move the other.
    //
    // Final whole-branch review, item 5: the fixture used to carry only the
    // first 5 rules (roughly half the shipped density) and the room-count
    // matrix topped out at 8, well under the measured room cap of 15 - so
    // PD_DECOR_MAX_SPOTS/PD_CRITTER_MAX_SPOTS were declared inert on data
    // thinner than what actually ships. This mirror plus the 15-room row
    // added to RunDecorBatch's ROOM_MATRIX below is what makes that
    // measurement real.
    std::vector<DecorRule> DecorFixture()
    {
        std::vector<DecorRule> rules;

        DecorRule torchRoom;
        torchRoom.id = 1;
        torchRoom.theme = 0;
        torchRoom.roleFilter = "room";
        torchRoom.goEntry = 910020;
        torchRoom.minPerBlock = 1;
        torchRoom.maxPerBlock = 3;
        torchRoom.weight = 100;
        torchRoom.minSpacingYd = 8.0;
        rules.push_back(torchRoom);

        DecorRule brazierBoss;
        brazierBoss.id = 2;
        brazierBoss.theme = 0;
        brazierBoss.roleFilter = "room_boss";
        brazierBoss.goEntry = 910021;
        brazierBoss.minPerBlock = 1;
        brazierBoss.maxPerBlock = 2;
        brazierBoss.weight = 40;
        brazierBoss.minSpacingYd = 8.0;
        rules.push_back(brazierBoss);

        DecorRule torchCorridor;
        torchCorridor.id = 3;
        torchCorridor.theme = 0;
        torchCorridor.roleFilter = "corridor";
        torchCorridor.goEntry = 910020;
        torchCorridor.minPerBlock = 0;
        torchCorridor.maxPerBlock = 1;
        torchCorridor.weight = 100;
        torchCorridor.minSpacingYd = 8.0;
        rules.push_back(torchCorridor);

        DecorRule barrelWallFoot;
        barrelWallFoot.id = 4;
        barrelWallFoot.theme = 0;
        barrelWallFoot.roleFilter = "room";
        barrelWallFoot.goEntry = 910050;
        barrelWallFoot.minPerBlock = 0;
        barrelWallFoot.maxPerBlock = 2;
        barrelWallFoot.weight = 30;
        barrelWallFoot.minSpacingYd = 8.0;
        rules.push_back(barrelWallFoot);

        DecorRule crateWallFoot;
        crateWallFoot.id = 5;
        crateWallFoot.theme = 0;
        crateWallFoot.roleFilter = "room";
        crateWallFoot.goEntry = 910051;
        crateWallFoot.minPerBlock = 0;
        crateWallFoot.maxPerBlock = 2;
        crateWallFoot.weight = 30;
        crateWallFoot.minSpacingYd = 8.0;
        rules.push_back(crateWallFoot);

        DecorRule bookshelfWallFoot;
        bookshelfWallFoot.id = 6;
        bookshelfWallFoot.theme = 0;
        bookshelfWallFoot.roleFilter = "room";
        bookshelfWallFoot.goEntry = 910054;
        bookshelfWallFoot.minPerBlock = 0;
        bookshelfWallFoot.maxPerBlock = 1;
        bookshelfWallFoot.weight = 20;
        bookshelfWallFoot.minSpacingYd = 8.0;
        rules.push_back(bookshelfWallFoot);

        DecorRule alchemyBenchWallFoot;
        alchemyBenchWallFoot.id = 7;
        alchemyBenchWallFoot.theme = 0;
        alchemyBenchWallFoot.roleFilter = "room";
        alchemyBenchWallFoot.goEntry = 910055;
        alchemyBenchWallFoot.minPerBlock = 0;
        alchemyBenchWallFoot.maxPerBlock = 1;
        alchemyBenchWallFoot.weight = 15;
        alchemyBenchWallFoot.minSpacingYd = 8.0;
        rules.push_back(alchemyBenchWallFoot);

        DecorRule longTableWallFoot;
        longTableWallFoot.id = 8;
        longTableWallFoot.theme = 0;
        longTableWallFoot.roleFilter = "room";
        longTableWallFoot.goEntry = 910056;
        longTableWallFoot.minPerBlock = 0;
        longTableWallFoot.maxPerBlock = 1;
        longTableWallFoot.weight = 15;
        longTableWallFoot.minSpacingYd = 8.0;
        rules.push_back(longTableWallFoot);

        DecorRule plagueBarrelCorridor;
        plagueBarrelCorridor.id = 9;
        plagueBarrelCorridor.theme = 0;
        plagueBarrelCorridor.roleFilter = "corridor";
        plagueBarrelCorridor.goEntry = 910052;
        plagueBarrelCorridor.minPerBlock = 0;
        plagueBarrelCorridor.maxPerBlock = 1;
        plagueBarrelCorridor.weight = 40;
        plagueBarrelCorridor.minSpacingYd = 8.0;
        rules.push_back(plagueBarrelCorridor);

        DecorRule crateStackCorner;
        crateStackCorner.id = 10;
        crateStackCorner.theme = 0;
        crateStackCorner.roleFilter = "room";
        crateStackCorner.goEntry = 910060;
        crateStackCorner.placement = DECOR_PLACEMENT_CORNER;
        crateStackCorner.minPerBlock = 0;
        crateStackCorner.maxPerBlock = 2;
        crateStackCorner.weight = 50;
        crateStackCorner.minSpacingYd = 8.0;
        rules.push_back(crateStackCorner);

        DecorRule brokenCrateStackCorner;
        brokenCrateStackCorner.id = 11;
        brokenCrateStackCorner.theme = 0;
        brokenCrateStackCorner.roleFilter = "room";
        brokenCrateStackCorner.goEntry = 910062;
        brokenCrateStackCorner.placement = DECOR_PLACEMENT_CORNER;
        brokenCrateStackCorner.minPerBlock = 0;
        brokenCrateStackCorner.maxPerBlock = 2;
        brokenCrateStackCorner.weight = 50;
        brokenCrateStackCorner.minSpacingYd = 8.0;
        rules.push_back(brokenCrateStackCorner);

        DecorRule tallBarrelCorridorCorner;
        tallBarrelCorridorCorner.id = 12;
        tallBarrelCorridorCorner.theme = 0;
        tallBarrelCorridorCorner.roleFilter = "corridor_corner";
        tallBarrelCorridorCorner.goEntry = 910063;
        tallBarrelCorridorCorner.placement = DECOR_PLACEMENT_CORNER;
        tallBarrelCorridorCorner.minPerBlock = 0;
        tallBarrelCorridorCorner.maxPerBlock = 1;
        tallBarrelCorridorCorner.weight = 60;
        tallBarrelCorridorCorner.minSpacingYd = 8.0;
        rules.push_back(tallBarrelCorridorCorner);

        DecorRule rubbleLowScatter;
        rubbleLowScatter.id = 13;
        rubbleLowScatter.theme = 0;
        rubbleLowScatter.roleFilter = "room";
        rubbleLowScatter.goEntry = 910070;
        rubbleLowScatter.placement = DECOR_PLACEMENT_SCATTER;
        rubbleLowScatter.minPerBlock = 0;
        rubbleLowScatter.maxPerBlock = 3;
        rubbleLowScatter.weight = 60;
        rubbleLowScatter.minSpacingYd = 12.0;
        rules.push_back(rubbleLowScatter);

        DecorRule skeletonBossScatter;
        skeletonBossScatter.id = 14;
        skeletonBossScatter.theme = 0;
        skeletonBossScatter.roleFilter = "room_boss";
        skeletonBossScatter.goEntry = 910073;
        skeletonBossScatter.placement = DECOR_PLACEMENT_SCATTER;
        skeletonBossScatter.minPerBlock = 1;
        skeletonBossScatter.maxPerBlock = 3;
        skeletonBossScatter.weight = 80;
        skeletonBossScatter.minSpacingYd = 12.0;
        rules.push_back(skeletonBossScatter);

        return rules;
    }

    // The full 5 shipped rows of `pdungeon_critter_rules`, mirrored exactly
    // (id, entry, min/max, weight): mod_pdungeon_critters.sql ids 1-4 and 6.
    // Id 5 does not exist any more - final whole-branch review, item 2
    // removed it as a geometric dead zone (a corridor_dead_end block can
    // never carry a critter, whichever rule matches it).
    std::vector<CritterRule> CritterFixture()
    {
        std::vector<CritterRule> rules;
        CritterRule r;
        r.id = 1; r.theme = 0; r.roleFilter = "room";
        r.creatureEntry = 32428; r.minPerBlock = 0; r.maxPerBlock = 2; r.weight = 100;
        rules.push_back(r);
        r.id = 2; r.theme = 0; r.roleFilter = "room";
        r.creatureEntry = 23086; r.minPerBlock = 0; r.maxPerBlock = 2; r.weight = 80;
        rules.push_back(r);
        r.id = 3; r.theme = 0; r.roleFilter = "room";
        r.creatureEntry = 2110; r.minPerBlock = 0; r.maxPerBlock = 1; r.weight = 60;
        rules.push_back(r);
        r.id = 4; r.theme = 0; r.roleFilter = "corridor";
        r.creatureEntry = 26525; r.minPerBlock = 0; r.maxPerBlock = 2; r.weight = 100;
        rules.push_back(r);
        r.id = 6; r.theme = 0; r.roleFilter = "room_boss";
        r.creatureEntry = 26525; r.minPerBlock = 0; r.maxPerBlock = 1; r.weight = 60;
        rules.push_back(r);
        return rules;
    }

    bool SameSpots(std::vector<DecorSpot> const& l, std::vector<DecorSpot> const& r)
    {
        if (l.size() != r.size())
        {
            return false;
        }
        for (size_t i = 0; i < l.size(); ++i)
        {
            if (l[i].bx != r[i].bx || l[i].by != r[i].by ||
                l[i].ruleId != r[i].ruleId || l[i].goEntry != r[i].goEntry ||
                l[i].u != r[i].u || l[i].v != r[i].v ||
                l[i].orientation != r[i].orientation)
            {
                return false;
            }
        }
        return true;
    }

    bool SameCritters(std::vector<CritterSpot> const& a,
                      std::vector<CritterSpot> const& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].bx != b[i].bx || a[i].by != b[i].by ||
                a[i].ruleId != b[i].ruleId ||
                a[i].creatureEntry != b[i].creatureEntry ||
                a[i].u != b[i].u || a[i].v != b[i].v)
            {
                return false;
            }
        }
        return true;
    }

    // BuildDecorPlan takes the plan by const reference, so this can only fail
    // through a const_cast somebody added - which is exactly the kind of thing
    // that is cheap to check and expensive to find later.
    bool SamePlan(BlockPlan const& l, BlockPlan const& r)
    {
        if (l.effectiveSeed != r.effectiveSeed ||
            l.entranceIndex != r.entranceIndex || l.bossIndex != r.bossIndex ||
            l.blocks.size() != r.blocks.size())
        {
            return false;
        }
        for (size_t i = 0; i < l.blocks.size(); ++i)
        {
            PlacedBlock const& a = l.blocks[i];
            PlacedBlock const& b = r.blocks[i];
            if (a.bx != b.bx || a.by != b.by || a.role != b.role ||
                a.socketMask != b.socketMask || a.chunkId != b.chunkId ||
                a.roomId != b.roomId || a.depth != b.depth)
            {
                return false;
            }
        }
        return true;
    }

    // A dead-end stub must report its own role name. Until the split it
    // reported "corridor_cross", which made both filters wrong at once.
    bool CheckRoleNamesDistinct(BlockPlan const& plan, std::string& why)
    {
        bool sawDeadEnd = false;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.role != BlockRole::CorridorDeadEnd) continue;
            sawDeadEnd = true;
            if (std::strcmp(BlockRoleName(b.role), "corridor_dead_end") != 0)
            {
                why = "a dead-end stub does not report role name corridor_dead_end";
                return false;
            }
        }
        if (sawDeadEnd && std::strcmp(BlockRoleName(BlockRole::CorridorCross),
                                      "corridor_cross") != 0)
        {
            why = "corridor_cross lost its own name";
            return false;
        }
        return true;
    }

    // Every property a spot has to have, checked against the KIT's own surface
    // classes rather than against the planner's - a placement bug that also
    // lives in PDv2Classify would otherwise agree with itself.
    bool CheckDecorSpots(BlockPlan const& plan, std::vector<DecorSpot> const& spots,
                         std::vector<DecorRule> const& rules, std::string& why)
    {
        std::map<std::pair<int, int>, int> chunkAt;
        for (PlacedBlock const& b : plan.blocks)
        {
            chunkAt[std::make_pair(b.bx, b.by)] = b.chunkId;
        }

        std::map<int, double> spacingOf;
        for (DecorRule const& rule : rules)
        {
            spacingOf[rule.id] = rule.minSpacingYd;
        }

        std::set<std::string> seen;
        std::map<std::pair<std::pair<int, int>, int>, std::vector<DecorSpot>> perRule;

        for (DecorSpot const& spot : spots)
        {
            auto const block = chunkAt.find(std::make_pair(spot.bx, spot.by));
            if (block == chunkAt.end())
            {
                why = "a spot sits on a block the plan does not contain";
                return false;
            }
            auto const kit = g_kit.find(block->second);
            if (kit == g_kit.end())
            {
                why = "a spot sits on a chunk the kit metadata does not describe";
                return false;
            }
            std::string const& classes = kit->second.classes;
            if (classes.size() != PD_CELLS_PER_BLOCK * PD_CELLS_PER_BLOCK)
            {
                why = "kit surfaceClasses is not 64 characters";
                return false;
            }

            int const row = static_cast<int>(std::floor(spot.u / PD_CELL_SIZE_YD));
            int const col = static_cast<int>(std::floor(spot.v / PD_CELL_SIZE_YD));
            if (row < 0 || col < 0 ||
                row >= PD_CELLS_PER_BLOCK || col >= PD_CELLS_PER_BLOCK)
            {
                why = "a spot is outside its own block";
                return false;
            }
            if (row == PD_CELLS_PER_BLOCK / 2 || col == PD_CELLS_PER_BLOCK / 2)
            {
                why = "a spot stands on the socket track";
                return false;
            }
            if (classes[static_cast<size_t>(row) * PD_CELLS_PER_BLOCK + col] != 'W')
            {
                why = "a spot is not on a walkable cell";
                return false;
            }

            // Which placement kind put this spot here. The rule vector is
            // already in hand for `spacingOf`, so this is the same lookup.
            std::string placement = PDungeon::DECOR_PLACEMENT_WALL_FOOT;
            for (DecorRule const& r : rules)
            {
                if (r.id == spot.ruleId) { placement = r.placement; break; }
            }

            int wallSides = 0;
            int const drow[4] = { -1, 0, 1, 0 };
            int const dcol[4] = { 0, 1, 0, -1 };
            bool wallAt[4] = { false, false, false, false };
            for (int d = 0; d < 4; ++d)
            {
                int const r = row + drow[d];
                int const c = col + dcol[d];
                if (r < 0 || c < 0 ||
                    r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                {
                    continue;
                }
                wallAt[d] =
                    classes[static_cast<size_t>(r) * PD_CELLS_PER_BLOCK + c] == 'L';
                if (wallAt[d]) ++wallSides;
            }

            if (placement == PDungeon::DECOR_PLACEMENT_WALL_FOOT)
            {
                if (!wallSides)
                {
                    why = "a wall_foot spot's cell touches no wall cell";
                    return false;
                }
            }
            else if (placement == PDungeon::DECOR_PLACEMENT_CORNER)
            {
                // Two ADJACENT walls, i.e. N+E, E+S, S+W or W+N. Two OPPOSITE
                // walls make a passage, not a corner, and a prop wedged there
                // would block it.
                bool adjacent = (wallAt[0] && wallAt[1]) || (wallAt[1] && wallAt[2]) ||
                                (wallAt[2] && wallAt[3]) || (wallAt[3] && wallAt[0]);
                if (!adjacent)
                {
                    why = "a corner spot's cell has no two adjacent wall sides";
                    return false;
                }
            }
            else if (placement == PDungeon::DECOR_PLACEMENT_SCATTER)
            {
                // The inverse of wall_foot: open floor. A scatter prop that
                // ends up against a wall duplicates the wall_foot pass and
                // leaves the middle of the room as empty as it was.
                if (wallSides)
                {
                    why = "a scatter spot's cell touches a wall cell";
                    return false;
                }
            }

            for (DecorAnchor const& anchor : kit->second.anchors)
            {
                double const du = spot.u - anchor.u;
                double const dv = spot.v - anchor.v;
                if (du * du + dv * dv <
                    DECOR_ANCHOR_CLEAR_YD * DECOR_ANCHOR_CLEAR_YD)
                {
                    why = "a spot stands inside an anchor's clearance";
                    return false;
                }
            }

            char key[96];
            std::snprintf(key, sizeof(key), "%d,%d,%.6f,%.6f",
                          spot.bx, spot.by, spot.u, spot.v);
            if (!seen.insert(key).second)
            {
                why = "two spots share a position";
                return false;
            }

            auto const spacing = spacingOf.find(spot.ruleId);
            if (spacing == spacingOf.end())
            {
                why = "a spot names a rule id the fixture does not have";
                return false;
            }
            auto& siblings =
                perRule[std::make_pair(std::make_pair(spot.bx, spot.by), spot.ruleId)];
            for (DecorSpot const& other : siblings)
            {
                double const du = spot.u - other.u;
                double const dv = spot.v - other.v;
                if (du * du + dv * dv < spacing->second * spacing->second)
                {
                    why = "two spots of one rule are closer than its spacing";
                    return false;
                }
            }
            siblings.push_back(spot);
        }
        return true;
    }

    // Critters live on open floor, never on the socket track, never on a
    // wall foot, and never off the walkable set - the same veto SplitOnDeath
    // applies to affix children, for the same reason: a gravity-less
    // creature past the platform edge hovers where nobody can reach it.
    bool CheckCritterSpots(BlockPlan const& plan,
                           std::vector<CritterSpot> const& spots,
                           std::string& why)
    {
        for (CritterSpot const& spot : spots)
        {
            PlacedBlock const* const block = plan.At(spot.bx, spot.by);
            if (!block)
            {
                why = "a critter stands on no block of the plan";
                return false;
            }
            uint8_t const* const mask = MaskFor(block->chunkId);
            if (!mask)
            {
                why = "a critter was planned on a chunk with no walk mask";
                return false;
            }
            std::string const classes = PDv2Classify(mask);

            int const row = static_cast<int>(std::floor(spot.u / PD_CELL_SIZE_YD));
            int const col = static_cast<int>(std::floor(spot.v / PD_CELL_SIZE_YD));
            if (row < 0 || col < 0 ||
                row >= PD_CELLS_PER_BLOCK || col >= PD_CELLS_PER_BLOCK)
            {
                why = "a critter is outside its own block";
                return false;
            }
            if (row == PD_CELLS_PER_BLOCK / 2 || col == PD_CELLS_PER_BLOCK / 2)
            {
                why = "a critter stands on the socket track";
                return false;
            }
            if (classes[static_cast<size_t>(row) * PD_CELLS_PER_BLOCK + col] != 'W')
            {
                why = "a critter is not on a walkable cell";
                return false;
            }

            // "never on a wall foot": the same four-side wall test
            // CheckDecorSpots applies to a scatter placement, asserted here
            // too rather than just trusted from the collector's name -
            // critters draw from CollectScatter (see BuildCritterPlan),
            // and this is what would turn this gate red if that collector
            // were ever swapped for CollectWallFeet in the planner (final
            // whole-branch review, item 5).
            int const drow[4] = { -1, 0, 1, 0 };
            int const dcol[4] = { 0, 1, 0, -1 };
            int wallSides = 0;
            for (int d = 0; d < 4; ++d)
            {
                int const r = row + drow[d];
                int const c = col + dcol[d];
                if (r < 0 || c < 0 ||
                    r >= PD_CELLS_PER_BLOCK || c >= PD_CELLS_PER_BLOCK)
                {
                    continue;
                }
                if (classes[static_cast<size_t>(r) * PD_CELLS_PER_BLOCK + c] == 'L')
                {
                    ++wallSides;
                }
            }
            if (wallSides)
            {
                why = "a critter's cell touches a wall cell";
                return false;
            }
        }
        if (spots.size() > static_cast<size_t>(PD_CRITTER_MAX_SPOTS))
        {
            why = "a layout exceeded the critter budget";
            return false;
        }
        return true;
    }

    // PDv2Classify against the kit's own surfaceClasses, chunk by chunk. This
    // is the check that lets the server DERIVE the classes instead of shipping
    // a second copy of them.
    void RunClassifyChecks()
    {
        for (auto const& kv : g_kit)
        {
            char msg[160];
            uint8_t const* const mask = MaskFor(kv.first);
            if (!mask)
            {
                std::snprintf(msg, sizeof(msg),
                              "chunk %d is in kit_meta.json but has no walk mask "
                              "in the SQL", kv.first);
                Check(false, msg, 0);
                continue;
            }
            std::snprintf(msg, sizeof(msg),
                          "chunk %d: derived surface classes differ from the kit's",
                          kv.first);
            // The prop channel must decode to exactly the count the generator
            // says it wrote - a props list the server-side scanner cannot read
            // is a block that silently loses its fountain or cave-in.
            {
                char pmsg[160];
                std::snprintf(pmsg, sizeof(pmsg),
                              "chunk %d: props decode to %d, kit_meta declares %d",
                              kv.first, static_cast<int>(kv.second.props.size()),
                              kv.second.declaredProps);
                Check(static_cast<int>(kv.second.props.size()) ==
                          kv.second.declaredProps, pmsg, 0);
                for (KitProp const& prop : kv.second.props)
                {
                    Check(prop.goEntry >= 910040 && prop.goEntry <= 910049,
                          "a kit prop names an entry outside the reserved "
                          "910040-910049 block", static_cast<uint32_t>(kv.first));
                }
            }
            std::string const derived = PDv2Classify(mask);
            std::string const& kitCls = kv.second.classes;
            bool ok = derived.size() == kitCls.size();
            if (ok)
            {
                for (size_t k = 0; k < derived.size(); ++k)
                {
                    if (derived[k] == kitCls[k])
                    {
                        continue;
                    }
                    // The ONE sanctioned divergence (Phase 4 city ring
                    // rooms): the kit may promote a derived VOID cell to
                    // WALL where a building pad keeps terrain-covered floor
                    // under a centrepiece - script 52 enforces the same rule
                    // on the actual bytes (no hole there). The server's own
                    // derivation feeds only the decor planner, which has no
                    // theme-2 rules; the day city decor rules exist, the
                    // promotion has to move into PDv2Classify itself.
                    if (derived[k] == 'V' && kitCls[k] == 'L')
                    {
                        continue;
                    }
                    ok = false;
                    break;
                }
            }
            Check(ok, msg, 0);
        }
    }

    // roleFilter is a PREFIX match, which is what makes one 'room' row cover
    // the entrance and the boss room too. Pinned here because nothing else
    // would notice it turning into an exact match.
    void RunRoleFilterChecks()
    {
        Check(DecorRoleMatches("room", BlockRoleName(BlockRole::Room)),
              "'room' does not match a room", 0);
        Check(DecorRoleMatches("room", BlockRoleName(BlockRole::RoomEntrance)),
              "'room' does not match the entrance room", 0);
        Check(DecorRoleMatches("room", BlockRoleName(BlockRole::RoomBoss)),
              "'room' does not match a boss room", 0);
        Check(!DecorRoleMatches("room_boss", BlockRoleName(BlockRole::Room)),
              "'room_boss' matched a plain room", 0);
        Check(DecorRoleMatches("corridor", BlockRoleName(BlockRole::CorridorCross)),
              "'corridor' does not match a cross corridor", 0);
        Check(DecorRoleMatches("corridor", BlockRoleName(BlockRole::CorridorDeadEnd)),
              "'corridor' does not match a dead-end corridor", 0);
        Check(!DecorRoleMatches("corridor", BlockRoleName(BlockRole::Room)),
              "'corridor' matched a room", 0);
        Check(DecorRoleMatches("", BlockRoleName(BlockRole::Room)),
              "an empty filter did not match everything", 0);
    }

    // Neither rejection gate ever fires against kit v2: its rooms are wide
    // enough that a 3 yd anchor clearance and an 8 yd spacing are met by
    // construction, so the batch's per-spot checks on them pass vacuously. A
    // guard that is never exercised is a guard that may not work, so both are
    // forced here against data built to break them.
    void RunDecorGateChecks()
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(1u, 5), &plan))
        {
            Check(false, "the gate checks could not generate a plan", 0);
            return;
        }

        // Anchors on a 4 yd lattice: no point in a block is further than
        // 2.83 yd from one of them, which is inside the 3 yd clearance, so
        // nothing may be placed anywhere.
        std::vector<DecorAnchor> lattice;
        for (int i = 0; i * 4 <= 68; ++i)
        {
            for (int j = 0; j * 4 <= 68; ++j)
            {
                DecorAnchor anchor;
                anchor.u = i * 4.0;
                anchor.v = j * 4.0;
                lattice.push_back(anchor);
            }
        }
        std::vector<DecorSpot> const smothered = BuildDecorPlan(
            plan, MaskFor,
            [&lattice](int) -> std::vector<DecorAnchor> const* { return &lattice; },
            DecorFixture(), plan.effectiveSeed);
        Check(smothered.empty(), "the anchor clearance let a spot through", 0);

        // One rule, asking for three props a block with a spacing wider than
        // the block: exactly one may land.
        std::vector<DecorRule> wide = DecorFixture();
        wide.resize(1);
        wide[0].minPerBlock = 3;
        wide[0].maxPerBlock = 3;
        wide[0].minSpacingYd = 100.0;
        std::vector<DecorSpot> const spread = BuildDecorPlan(
            plan, MaskFor, AnchorsForChunk, wide, plan.effectiveSeed);
        std::map<std::pair<int, int>, int> perBlock;
        int worst = 0;
        for (DecorSpot const& spot : spread)
        {
            int const n = ++perBlock[std::make_pair(spot.bx, spot.by)];
            worst = (n > worst) ? n : worst;
        }
        Check(!spread.empty(), "the spacing gate check placed nothing at all", 0);
        Check(worst <= 1, "the spacing rule let two props share a block", 0);
    }

    // --- decor / critter plan pins (final whole-branch review, item 6) -----
    //
    // The decor and critter streams were both changed deliberately this
    // round - item 2 removed critter rule 5, item 1 made the critter rule
    // sort stable - and neither had a pin that would go red on an
    // ACCIDENTAL stream move: RunDecorBatch's own two-build compare
    // (SameSpots/SameCritters) only catches nondeterminism, not a change
    // that moves the stream on purpose or by accident alike. Same shape as
    // PD_SPAWN_DRAW_PIN below: a fixed seed, the shipped fixture, the
    // emitted spots serialised and compared to a string constant.
    //
    // If either of these fails after a deliberate decor/critter rule
    // change, that is the change being noticed, not the pin being wrong -
    // update it in the same commit as the draw-order comment.
    //
    // Re-captured for B2's third Room look, and the pin itself says WHY the
    // diff is wider than "the two rooms that became alt 2 moved":
    // AltCountFor(Room) went 2 -> 3, so the alt draw is UniformInt(0, 2) where
    // it was UniformInt(0, 1). The raw word is the same one - only the mapping
    // moved, % 3 instead of % 2 - and that re-rolls a Room block's alt with
    // probability 2/3, so SEVERAL rooms changed chunkId, not only the two that
    // came out alt 2 (chunk 4011, the 33 yd platform). Each of those moved on
    // its OWN new walk mask, which is what both plans place against: three
    // rooms here (259,259 / 261,260 / 260,262). The other four blocks in the
    // diff are corridors (261,259 / 258,260 / 260,260 / 262,260), whose
    // chunkIds cannot move at all because AltCountFor is untouched for their
    // roles - those four moved through a LOCAL shift of the shared
    // decor/critter stream, and that shift cancels out again rather than
    // running to the end of the layout: every block from 258,261 on is
    // byte-identical, and 260,262 moved while sitting between unmoved
    // neighbours on both sides.
    // (2026-09-03, B0b's loop rooms: the per-segment detour Chance draws land
    // before the first chain step. Earlier that day, B0b task 1: the pocket's
    // forward-cut draw was withdrawn. 2026-09-02, Round B: the chain generator
    // replaced scatter + MST.)
    //
    // CAPTURE PROCEDURE, every time: run `pdblock --decor-batch` (seed 12345,
    // 5 rooms, the shipped fixtures) and paste the value out of the "plan
    // moved" failure message - never by reasoning about what it should be -
    // and only once every change that can move these streams has landed.
    char const* const PD_DECOR_PLAN_PIN =
        "258,257,1,910020,18.333333,12.500000,3.141593;258,257,4,910050,56.666666,20.833333,0.000000;258,257,5,910051,10.000000,20.833333,3.141593;258,257,5,910051,56.666666,29.166666,0.000000;258,257,6,910054,29.166666,10.000000,4.712389;258,257,7,910055,10.000000,45.833333,3.141593;258,257,10,910060,18.333333,10.000000,3.926991;258,257,10,910060,48.333333,56.666666,0.785398;258,257,11,910062,10.000000,56.666666,2.356194;258,257,11,910062,56.666666,10.000000,5.497787;259,258,3,910020,45.833333,18.333333,4.712389;258,259,9,910052,26.666666,45.833333,3.141593;258,259,12,910063,26.666666,26.666666,3.926991;259,259,1,910020,26.666666,62.500000,3.141593;259,259,4,910050,4.166667,26.666666,4.712389;259,259,7,910055,12.500000,26.666666,4.712389;259,259,11,910062,48.333333,18.333333,5.497787;259,259,11,910062,18.333333,18.333333,3.926991;259,259,13,910070,29.166666,20.833333,0.000000;259,259,13,910070,29.166666,45.833333,0.000000;260,259,9,910052,26.666666,45.833333,3.141593;258,260,3,910020,20.833333,26.666666,4.712389;260,260,3,910020,26.666666,54.166666,3.141593;260,260,9,910052,62.500000,26.666666,4.712389;260,260,12,910063,26.666666,26.666666,3.926991;261,260,1,910020,18.333333,45.833333,3.141593;261,260,1,910020,12.500000,26.666666,4.712389;261,260,1,910020,26.666666,12.500000,3.141593;261,260,4,910050,48.333333,29.166666,0.000000;261,260,6,910054,26.666666,54.166666,3.141593;261,260,7,910055,18.333333,20.833333,3.141593;261,260,10,910060,18.333333,48.333333,2.356194;261,260,11,910062,48.333333,18.333333,5.497787;261,260,11,910062,18.333333,18.333333,3.926991;261,260,13,910070,29.166666,45.833333,0.000000;262,260,3,910020,62.500000,26.666666,4.712389;258,261,1,910020,29.166666,10.000000,4.712389;258,261,1,910020,18.333333,12.500000,3.141593;258,261,1,910020,29.166666,56.666666,1.570796;258,261,5,910051,56.666666,29.166666,0.000000;258,261,5,910051,10.000000,54.166666,3.141593;258,261,6,910054,45.833333,56.666666,1.570796;258,261,7,910055,20.833333,56.666666,1.570796;258,261,10,910060,10.000000,56.666666,2.356194;258,261,11,910062,48.333333,56.666666,0.785398;258,261,11,910062,56.666666,10.000000,5.497787;258,261,13,910070,20.833333,20.833333,0.000000;258,261,13,910070,45.833333,29.166666,0.000000;258,261,13,910070,20.833333,45.833333,0.000000;262,261,3,910020,45.833333,31.666666,1.570796;260,262,1,910020,29.166666,10.000000,4.712389;260,262,1,910020,10.000000,54.166666,3.141593;260,262,1,910020,10.000000,45.833333,3.141593;260,262,6,910054,20.833333,56.666666,1.570796;260,262,7,910055,56.666666,20.833333,0.000000;260,262,8,910056,56.666666,12.500000,0.000000;260,262,10,910060,56.666666,10.000000,5.497787;261,262,3,910020,26.666666,45.833333,3.141593;261,262,9,910052,26.666666,54.166666,3.141593;262,262,1,910020,26.666666,4.166667,3.141593;262,262,2,910021,20.833333,56.666666,1.570796;262,262,2,910021,10.000000,20.833333,3.141593;262,262,7,910055,56.666666,29.166666,0.000000;262,262,10,910060,18.333333,10.000000,3.926991;262,262,11,910062,10.000000,56.666666,2.356194;262,262,13,910070,29.166666,12.500000,0.000000;262,262,14,910073,29.166666,20.833333,0.000000;262,262,14,910073,45.833333,29.166666,0.000000;262,262,14,910073,29.166666,45.833333,0.000000;260,263,3,910020,4.166667,26.666666,4.712389;260,263,9,910052,12.500000,26.666666,4.712389";
    char const* const PD_CRITTER_PLAN_PIN =
        "259,259,1,32428,29.166666,45.833333;259,259,1,32428,29.166666,29.166666;259,259,2,23086,29.166666,20.833333;261,260,1,32428,29.166666,29.166666;261,260,2,23086,29.166666,20.833333;258,261,1,32428,29.166666,45.833333;258,261,2,23086,45.833333,45.833333;258,261,2,23086,45.833333,29.166666;258,261,3,2110,29.166666,20.833333;260,261,4,26525,29.166666,29.166666;262,261,4,26525,29.166666,29.166666;262,261,4,26525,54.166666,29.166666;260,262,2,23086,20.833333,29.166666;260,262,2,23086,29.166666,29.166666;262,262,1,32428,45.833333,20.833333;262,262,2,23086,20.833333,20.833333;262,262,2,23086,45.833333,45.833333";

    bool CheckDecorPlanPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12345u, 5), &plan))
        {
            why = "the pinned decor plan could not generate a layout";
            return false;
        }
        std::vector<DecorSpot> const spots = BuildDecorPlan(
            plan, MaskFor, AnchorsForChunk, DecorFixture(), plan.effectiveSeed);

        std::string got;
        for (DecorSpot const& s : spots)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%.6f,%.6f,%.6f;",
                          s.bx, s.by, s.ruleId, s.goEntry, s.u, s.v, s.orientation);
            got += buf;
        }
        if (!got.empty())
        {
            got.pop_back();
        }
        if (got != PD_DECOR_PLAN_PIN)
        {
            why = "the decor plan moved: " + got;
            return false;
        }
        return true;
    }

    bool CheckCritterPlanPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12345u, 5), &plan))
        {
            why = "the pinned critter plan could not generate a layout";
            return false;
        }
        std::vector<CritterSpot> const spots = BuildCritterPlan(
            plan, MaskFor, CritterFixture(), plan.effectiveSeed);

        std::string got;
        for (CritterSpot const& s : spots)
        {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%.6f,%.6f;",
                          s.bx, s.by, s.ruleId, s.creatureEntry, s.u, s.v);
            got += buf;
        }
        if (!got.empty())
        {
            got.pop_back();
        }
        if (got != PD_CRITTER_PLAN_PIN)
        {
            why = "the critter plan moved: " + got;
            return false;
        }
        return true;
    }

    // Round B: the chain itself, pinned. RunLayoutFreezeCheck pins the
    // manifest bytes and would notice most draw-order moves, but two
    // different chains can in principle emit the same block set; this pin
    // reads the chain order, the pockets and the loop rooms directly.
    // Format: chain cells `|` pockets `host>x,y;` `|` loops `into>x,y;`.
    // Captured by RUNNING `pdblock --batch` and reading the "the chain moved"
    // message, never by reasoning about the value.
    char const* const PD_CHAIN_PIN = "258,261;259,259;261,260;262,262;|1>258,257;2>260,262;|";

    // A SECOND chain, because the pin above ends in an empty loop field: seed
    // 12345 draws no loop room at the default DetourChance, so it pins the
    // chain and the pockets and says nothing about where a loop strip lands.
    // 12348 carries one. Without this a B0b regression that only moved loop
    // rooms would pass every pin in the file (B0b Task 2 review, item 3).
    // Captured the same way: by RUNNING `pdblock --batch` and reading the
    // "the loop chain moved" message, never by reasoning about the value.
    char const* const PD_CHAIN_PIN_LOOP =
        "263,259;259,259;258,257;260,256;|2>257,258;1>260,260;|1>261,258;";

    std::string ChainPinString(BlockPlan const& plan)
    {
        int const len = ChainLength(plan);
        std::vector<PlacedBlock const*> chain(static_cast<size_t>(len), nullptr);
        std::vector<PlacedBlock const*> pockets;
        std::vector<PlacedBlock const*> loops;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.chainIndex >= 0) chain[static_cast<size_t>(b.chainIndex)] = &b;
            if (b.branchOf >= 0) pockets.push_back(&b);
            if (b.detourOf >= 0) loops.push_back(&b);
        }
        std::string got;
        char buf[64];
        for (PlacedBlock const* b : chain)
        {
            std::snprintf(buf, sizeof(buf), "%d,%d;", b ? b->bx : -1, b ? b->by : -1);
            got += buf;
        }
        got += '|';
        for (PlacedBlock const* p : pockets)
        {
            std::snprintf(buf, sizeof(buf), "%d>%d,%d;", p->branchOf, p->bx, p->by);
            got += buf;
        }
        got += '|';
        for (PlacedBlock const* l : loops)
        {
            std::snprintf(buf, sizeof(buf), "%d>%d,%d;", l->detourOf, l->bx, l->by);
            got += buf;
        }
        return got;
    }

    bool CheckChainPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12345u, 5), &plan))
        {
            why = "the pinned chain could not generate a layout";
            return false;
        }
        std::string const got = ChainPinString(plan);
        if (got != PD_CHAIN_PIN)
        {
            why = "the chain moved: " + got;
            return false;
        }
        return true;
    }

    bool CheckLoopChainPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12348u, 5), &plan))
        {
            why = "the pinned loop chain could not generate a layout";
            return false;
        }
        std::string const got = ChainPinString(plan);
        if (got != PD_CHAIN_PIN_LOOP)
        {
            why = "the loop chain moved: " + got;
            return false;
        }
        return true;
    }

    // Round B / B5: the ambush plan, pinned. It draws on its OWN stream
    // (layoutSeed ^ PD_AMBUSH_SEED_MIX), which is exactly why it needs a pin
    // of its own: the layout freeze, the two chain pins and the spawn-draw
    // pins are blind to it by construction, so nothing else in this file
    // would notice an ambush corridor moving.
    //
    // Chance 100 so the pin states WHERE the spots are rather than how the
    // coin fell. Format: `bx,by,segment;` per spot, in segment order.
    // Captured by RUNNING `pdblock --batch` and reading the "the ambush plan
    // moved" message, never by reasoning about the value.
    char const* const PD_AMBUSH_PLAN_PIN = "262,261,1;";

    // The shipped default of V2.Ambush.Chance. Mirrored here rather than
    // exported from the generator on purpose: the chance is an operator key
    // read live from the .conf, not a layout input, so the generator must not
    // own a default for it - the batch's yield line just needs to report
    // against the number the server ships with.
    //
    // That number is OWNED in two places, and this mirror has to be re-read
    // against both if either moves: src/PDv2Mgr.cpp:120-121 (LoadConfig's
    // GetOption fallback) and conf/mod_procedural_dungeon.conf.dist:623 (the
    // shipped line). Nothing here goes red when they drift - the yield line
    // would simply report against the old number.
    int const PD_AMBUSH_DEFAULT_CHANCE_PCT = 50;

    std::string AmbushPinString(std::vector<AmbushSpot> const& spots)
    {
        std::string got;
        for (AmbushSpot const& s : spots)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d,%d,%d;", s.bx, s.by, s.segment);
            got += buf;
        }
        return got;
    }

    bool CheckAmbushPlanPinned(std::string& why)
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(MakeCfg(12345u, 5), &plan))
        {
            why = "the pinned ambush plan could not generate a layout";
            return false;
        }
        std::string const got = AmbushPinString(BuildAmbushPlan(plan, 100, plan.effectiveSeed));
        if (got.empty())
        {
            // A pin that reads "" would go on passing after the plan stopped
            // producing anything at all - the one failure a string compare
            // alone cannot tell from success.
            why = "the pinned ambush plan is empty at chance 100";
            return false;
        }
        if (got != PD_AMBUSH_PLAN_PIN)
        {
            why = "the ambush plan moved: " + got;
            return false;
        }
        return true;
    }

    // The SECOND ambush pin, and the only one that can see the coin. Every
    // other ambush check in this file runs at chance 0 or 100, and
    // PDRandom::Chance draws NOTHING at either (PDRandom.h:58-69) - so between
    // them they pin WHERE a spot lands and say nothing at all about the draw
    // sequence around it.
    //
    // This one runs at a mid chance on a FOUR-boss layout, so Chance is drawn
    // four times and the pick is drawn wherever a segment offers two or more
    // candidates. A regression in the draw ORDER - a chance skipped for a
    // segment with no geometry, a pick taken when the coin said no, the two
    // swapped - moves this string and nothing else in the file.
    //
    // The 50 is a literal and deliberately NOT PD_AMBUSH_DEFAULT_CHANCE_PCT:
    // this pin is about the draw sequence at a mid chance, and an operator
    // changing what the server ships with must not turn the batch red.
    //
    // Captured by RUNNING `pdblock --batch` and reading the "the mid-chance
    // ambush plan moved" message, never by reasoning about the value.
    char const* const PD_AMBUSH_PLAN_PIN_MID = "258,262,1;261,262,2;";

    bool CheckAmbushPlanMidPinned(std::string& why)
    {
        // Four bosses on four rooms: the largest boss count the game math ever
        // asks for (GameBossRooms(30)) and the shortest layout that carries it,
        // so all four segments are short enough to differ in candidate count.
        BlockCfg cfg = MakeCfg(12345u, 4);
        cfg.bossRooms = 4;
        BlockPlan plan;
        if (!GenerateBlockPlan(cfg, &plan))
        {
            why = "the mid-chance ambush pin could not generate a layout";
            return false;
        }
        std::string const got = AmbushPinString(BuildAmbushPlan(plan, 50, plan.effectiveSeed));
        if (got.empty())
        {
            // Same trap as the pin above: at a mid chance an empty result is a
            // legal SHAPE, but not for this seed - it means the draw stopped
            // producing, which a string compare against "" would call a pass.
            why = "the mid-chance ambush plan is empty";
            return false;
        }
        if (got != PD_AMBUSH_PLAN_PIN_MID)
        {
            why = "the mid-chance ambush plan moved: " + got;
            return false;
        }
        return true;
    }

    // Round B / B3-B5: the doorway table the barrier seals, and the socket
    // mirror it seals the other half through. Both used to live inside
    // PDv2InstanceScript.cpp - a lambda and an anonymous namespace - where no
    // harness could reach them (B3-B5 Task 2 review, Important 1). A kit
    // change, a SOCKET_* renumber or a transposed row/col would have shipped a
    // portcullis standing in a wall; now it turns this batch red.
    //
    // Not "captured by running": this table is a fixed geometric fact of the
    // 8x8 cell block, so it is written out by hand from the kit's own layout
    // and the two implementations have to agree with IT, not the other way
    // round.
    bool CheckLaneCellsPinned(std::string& why)
    {
        struct Row
        {
            unsigned bit;
            unsigned opposite;
            int      cells[2][2];   // (row, col) x 2
            char const* name;
        };
        Row const rows[4] = {
            { SOCKET_N, SOCKET_S, { { 0, 3 }, { 0, 4 } }, "N" },
            { SOCKET_S, SOCKET_N, { { 7, 3 }, { 7, 4 } }, "S" },
            { SOCKET_W, SOCKET_E, { { 3, 0 }, { 4, 0 } }, "W" },
            { SOCKET_E, SOCKET_W, { { 3, 7 }, { 4, 7 } }, "E" },
        };
        char buf[192];
        for (Row const& r : rows)
        {
            int got[2][2] = { { -1, -1 }, { -1, -1 } };
            LaneCellsForSocket(r.bit, got);
            for (int i = 0; i < 2; ++i)
            {
                if (got[i][0] != r.cells[i][0] || got[i][1] != r.cells[i][1])
                {
                    std::snprintf(buf, sizeof(buf),
                                  "lane cell %d of socket %s is (%d,%d), want (%d,%d)",
                                  i, r.name, got[i][0], got[i][1], r.cells[i][0], r.cells[i][1]);
                    why = buf;
                    return false;
                }
            }
            if (OppositeSocket(r.bit) != r.opposite)
            {
                std::snprintf(buf, sizeof(buf), "OppositeSocket(%s) is %u, want %u",
                              r.name, OppositeSocket(r.bit), r.opposite);
                why = buf;
                return false;
            }
            if (OppositeSocket(OppositeSocket(r.bit)) != r.bit)
            {
                std::snprintf(buf, sizeof(buf), "OppositeSocket does not round trip on %s", r.name);
                why = buf;
                return false;
            }
            // The mirror's lane cells sit on the FACING edge, which is what
            // makes the two halves of a sealed doorway one lane: the pinned
            // axis flips from 0 to last (or back) and the free axis is
            // untouched.
            int opp[2][2] = { { -1, -1 }, { -1, -1 } };
            LaneCellsForSocket(OppositeSocket(r.bit), opp);
            bool const vertical = (r.bit == SOCKET_N || r.bit == SOCKET_S);
            for (int i = 0; i < 2; ++i)
            {
                int const pinnedAxis = vertical ? opp[i][0] : opp[i][1];
                int const freeAxis = vertical ? opp[i][1] : opp[i][0];
                int const wantPinned = (vertical ? r.cells[i][0] : r.cells[i][1]) == 0
                                     ? PD_CELLS_PER_BLOCK - 1 : 0;
                if (pinnedAxis != wantPinned || freeAxis != (vertical ? r.cells[i][1] : r.cells[i][0]))
                {
                    std::snprintf(buf, sizeof(buf),
                                  "socket %s's mirror lane cell %d is (%d,%d) - not the facing edge",
                                  r.name, i, opp[i][0], opp[i][1]);
                    why = buf;
                    return false;
                }
            }
        }
        return true;
    }

    int RunDecorBatch(int count)
    {
        // Rooms is the one config axis that changes what a plan is made of:
        // a 3-room layout is nearly all corridor and a 9-room one is nearly
        // all room, so the matrix covers both ends of the rule set. 15 is
        // added (final whole-branch review, item 5) because that is the
        // measured room cap (PD_GAME_ROOMS_CAP_MEASURED / `pdblock
        // --roomcap`) - without it the budget checks below never saw a
        // layout anywhere near what a real account can reach.
        int const ROOM_MATRIX[4] = { 3, 5, 8, 15 };

        std::printf("decor batch of %d seeds x %d room counts\n\n",
                    count, static_cast<int>(sizeof(ROOM_MATRIX) / sizeof(int)));

        if (g_kit.empty() || g_masks.empty())
        {
            std::printf("kit metadata is missing - nothing to check\n");
            return 1;
        }

        RunClassifyChecks();
        RunRoleFilterChecks();
        RunDecorGateChecks();
        {
            // Two statements, not one call - same argument-evaluation-order
            // trap CheckSpawnDrawPinned's own call site guards against.
            std::string why;
            bool const ok = CheckDecorPlanPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            std::string why;
            bool const ok = CheckCritterPlanPinned(why);
            Check(ok, why.c_str(), 12345u);
        }

        std::vector<DecorRule> const rules = DecorFixture();
        std::vector<CritterRule> const critterRules = CritterFixture();
        int totalSpots = 0;
        int minSpots = 1 << 30;
        int maxSpots = 0;
        int blankLayouts = 0;
        int totalCritters = 0;
        int minCritters = 1 << 30;
        int maxCritters = 0;

        // Both themes, not just theme 1: the live DB's rules carry theme = 0
        // (any look), but the fixture used to say theme = 1 for all three,
        // which meant this batch never touched the padded theme-2 rooms a
        // real city dungeon generates. Running both is what makes this the
        // first real execution of the corner and scatter collectors.
        int const THEMES[2] = { 1, 2 };
        for (int i = 0; i < count; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 1u;
            for (int const theme : THEMES)
            {
                for (int const rooms : ROOM_MATRIX)
                {
                    BlockPlan plan;
                    BlockCfg cfg = MakeCfg(seed, rooms);
                    cfg.theme = theme;
                    if (!GenerateBlockPlan(cfg, &plan))
                    {
                        Check(false, "generation failed", seed);
                        continue;
                    }

                    BlockPlan const before = plan;
                    std::vector<DecorSpot> const first = BuildDecorPlan(
                        plan, MaskFor, AnchorsForChunk, rules, plan.effectiveSeed);
                    std::vector<DecorSpot> const again = BuildDecorPlan(
                        plan, MaskFor, AnchorsForChunk, rules, plan.effectiveSeed);

                    Check(SameSpots(first, again),
                          "two decor builds of the same plan differ", seed);
                    Check(SamePlan(before, plan),
                          "BuildDecorPlan changed the plan it was given", seed);
                    Check(first.size() <= static_cast<size_t>(PD_DECOR_MAX_SPOTS),
                          "a layout exceeded the decor spot budget", seed);

                    std::string why;
                    Check(CheckRoleNamesDistinct(plan, why),
                          why.empty() ? "role naming broken" : why.c_str(), seed);
                    Check(CheckDecorSpots(plan, first, rules, why),
                          why.empty() ? "decor placement broken" : why.c_str(), seed);

                    int const spots = static_cast<int>(first.size());
                    totalSpots += spots;
                    minSpots = (spots < minSpots) ? spots : minSpots;
                    maxSpots = (spots > maxSpots) ? spots : maxSpots;
                    if (!spots)
                    {
                        ++blankLayouts;
                    }

                    std::vector<CritterSpot> const critters = BuildCritterPlan(
                        plan, MaskFor, critterRules, plan.effectiveSeed);
                    std::vector<CritterSpot> const crittersAgain = BuildCritterPlan(
                        plan, MaskFor, critterRules, plan.effectiveSeed);
                    Check(SameCritters(critters, crittersAgain),
                          "two critter builds of the same plan differ", seed);
                    Check(CheckCritterSpots(plan, critters, why),
                          why.empty() ? "critter placement broken" : why.c_str(), seed);

                    int const nCritters = static_cast<int>(critters.size());
                    totalCritters += nCritters;
                    minCritters = (nCritters < minCritters) ? nCritters : minCritters;
                    maxCritters = (nCritters > maxCritters) ? nCritters : maxCritters;
                }
            }
        }

        // Every layout has at least an entrance and a boss room, and rule 1
        // asks for at least one torch in each - so a layout with no props at
        // all means the rules stopped matching, which no per-spot check would
        // catch.
        Check(blankLayouts == 0, "a layout came back with no props at all", 0);

        std::printf("props per layout : %d..%d (%d total)\n",
                    minSpots, maxSpots, totalSpots);
        std::printf("critters per layout : %d..%d (%d total)\n",
                    minCritters, maxCritters, totalCritters);
        std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
        std::printf("%s\n", g_failures == 0 ? "ALL CHECKS PASS" : "FAILURES");
        return g_failures == 0 ? 0 : 1;
    }

    // --- pack draw (PDv2SelectSpawns, generator/PDv2PackDraw.cpp) -----------
    //
    // A characterisation test: the draw for a fixed seed and a fixed pool
    // must not move. Captured from the extraction in Task 12 of the PDv2
    // content-expansion plan, before Task 13 touches the draw order.
    //
    // The draw order is a determinism CONTRACT: changing it re-rolls which
    // creatures every stored seed spawns. This pins it. If this check fails
    // after a change to PDv2PackDraw, that is the change being noticed, not
    // the test being wrong - update the pin deliberately, in the same commit
    // as the draw-order comment.
    //
    // Re-captured for Task 13 (one pack draw inserted at the front of every
    // room - PDv2PackDraw.cpp's own THE SPAWN STREAM comment says why every
    // downstream draw had to move). Old value, for anyone diffing history:
    // "84263,84269,84268,84276,84269,84267,84266,84266,84264,84276,84288,84268,84276,".
    // Captured by RUNNING `pdblock --batch 500` and reading the "spawn draw
    // moved" failure message, not by reasoning about what the new sequence
    // should be.
    char const* const PD_SPAWN_DRAW_PIN =
        "84268,84267,84269,84276,84268,84269,84269,84267,84276,84267,84289,84263,84263,";

    // A fixed stand-in for the live pack tables: two packs, one with a
    // boss, enough members that a 5-trash room and a boss room both have
    // something to draw. The entries are the shipped stock ones so the pin
    // is readable by anyone who knows the dungeon.
    //
    // trashPackIds/meleeByPack/casterByPack are hand-built the same way
    // PDv2PackMgr::LoadFromDB and GroupByPack would (PDv2PackMgr.cpp) - both
    // packs have a melee AND a caster member, so both are eligible for
    // Task 13's per-room pack draw, and that draw is what makes this pin
    // worth re-capturing rather than assumed unchanged.
    PackPools FixedPackPools()
    {
        PackPools pools;
        pools.melee  = { {1, 84264}, {1, 84265}, {1, 84266},
                         {2, 84267}, {2, 84268}, {2, 84269} };
        pools.caster = { {1, 84263}, {2, 84276} };
        pools.boss   = { {1, 84288}, {2, 84289} };
        pools.trashPackIds = { 1, 2 };
        pools.meleeByPack = { { 1, { {1, 84264}, {1, 84265}, {1, 84266} } },
                              { 2, { {2, 84267}, {2, 84268}, {2, 84269} } } };
        pools.casterByPack = { { 1, { {1, 84263} } },
                               { 2, { {2, 84276} } } };
        return pools;
    }

    SpawnSelectInputs FixedSpawnInputs()
    {
        SpawnSelectInputs in;
        in.rooms = { { /*roomIndex*/ 0, /*isBossRoom*/ false },
                     { 1, false },
                     { 2, true } };
        in.spawnsPerRoom = 5;
        in.bossRoomAdds = 2;
        in.casterPct = 40;
        in.bandMin = 76;
        in.affixPct = 40;
        return in;
    }

    bool CheckSpawnDrawPinned(std::string& why)
    {
        SpawnSelectInputs in = FixedSpawnInputs();
        std::vector<SpawnPick> picks;
        if (!PDv2SelectSpawns(12345u, in, FixedPackPools(), picks))
        {
            why = "the pinned spawn draw refused to select";
            return false;
        }

        std::string got;
        for (SpawnPick const& p : picks)
        {
            got += std::to_string(p.entry);
            got += ',';
        }
        if (got != PD_SPAWN_DRAW_PIN)
        {
            why = "the spawn draw moved: " + got;
            return false;
        }
        return true;
    }

    // Review finding (Task 12, PDv2 content-expansion plan): FixedPackPools()
    // above always includes a boss, so it never exercises the boss-standin
    // tie-break in PDv2SelectSpawns - which is exactly how a Critical (the
    // refactor changing which member wins a weight TIE) got past a green
    // gate. This pin closes that hole: no boss anywhere, every member at the
    // default weight (a tie is not an edge case here - it is the only case,
    // since every shipped pack member weighs 100), and a CASTER that loads
    // before any melee (pack 1's real stock order: creature 84263 sorts
    // before 84264..84266 under "ORDER BY p.id, m.entry" - see
    // PDv2PackMgr::LoadFromDB). Under the pre-fix code (scan melee fully,
    // then caster) the boss room's first pick would have been melee entry
    // 84264; scanning the loader's real interleave (pools.trash) picks the
    // caster, 84263, instead, because it was seen first.
    PackPools NoBossPackPools()
    {
        PackMember const caster1{ 1, 84263, PACK_ROLE_CASTER, /*casterSpellId*/ 12345, /*weight*/ 100 };
        PackMember const melee1{ 1, 84264, PACK_ROLE_MELEE, 0, 100 };
        PackMember const melee2{ 1, 84265, PACK_ROLE_MELEE, 0, 100 };
        PackMember const melee3{ 1, 84266, PACK_ROLE_MELEE, 0, 100 };

        PackPools pools;
        pools.caster = { caster1 };
        pools.melee = { melee1, melee2, melee3 };
        // pools.boss is left empty on purpose - the whole point of this
        // fixture is that the boss-standin fallback fires.
        //
        // The real loader order: pack 1's caster (entry 84263) loads before
        // its melee (84264..84266), because m.entry sorts that way within
        // the pack. A hand-built fixture has no loader to inherit this from,
        // so it is spelled out here exactly as PDv2PackMgr::SelectSpawns
        // would build it - see PackPools::trash's own comment for why this
        // cannot be reconstructed by concatenating melee and caster above.
        pools.trash = { caster1, melee1, melee2, melee3 };
        // Only pack 1 exists here, so it is the only eligible entry in
        // trashPackIds - Task 13's per-room pack draw has nothing to choose
        // between and PDRandom::UniformInt(0, 0) answers without drawing
        // (lo >= hi), so this fixture is not expected to move
        // PD_SPAWN_DRAW_NOBOSS_PIN even though the draw now runs.
        pools.trashPackIds = { 1 };
        pools.meleeByPack = { { 1, { melee1, melee2, melee3 } } };
        pools.casterByPack = { { 1, { caster1 } } };
        return pools;
    }

    // Only one room, and it is the boss room: the trash slots draw from the
    // same two-member-tie pool as the boss stand-in, which would make the
    // pin unable to tell "picked the right stand-in" apart from "picked the
    // right trash filler" if left in. One room keeps the pin reading
    // entirely off the stand-in pick.
    SpawnSelectInputs NoBossSpawnInputs()
    {
        SpawnSelectInputs in;
        in.rooms = { { /*roomIndex*/ 0, /*isBossRoom*/ true } };
        in.spawnsPerRoom = 5;
        in.bossRoomAdds = 2;
        in.casterPct = 40;
        in.bandMin = 76;
        in.affixPct = 40;
        return in;
    }

    // Captured by running the fixed inputs above through the FIXED draw, not
    // by reasoning about what it should be - a pin that only encodes the
    // reasoning behind it could pass for the same wrong reason a hand-traced
    // "should be" value would. The first entry is what matters for Finding
    // 1: it must be the boss room's stand-in pick, and it must be 84263 (the
    // caster), never 84264 (the melee member the pre-fix code picked).
    //
    // Checked against Task 13's pack draw and left AS IS: `pdblock --batch
    // 500` still reports this pin clean. NoBossPackPools() below only ever
    // loads pack 1, so the new per-room draw has exactly one eligible pack
    // (trashPackIds = {1}) and PDRandom::UniformInt(0, 0) answers that
    // without drawing (lo >= hi) - the draw runs, but it costs the stream
    // nothing, so this pin was not expected to move and did not.
    char const* const PD_SPAWN_DRAW_NOBOSS_PIN =
        "84263,84263,84266,";

    bool CheckNoBossSpawnDrawPinned(std::string& why)
    {
        SpawnSelectInputs in = NoBossSpawnInputs();
        std::vector<SpawnPick> picks;
        if (!PDv2SelectSpawns(12345u, in, NoBossPackPools(), picks))
        {
            why = "the pinned no-boss spawn draw refused to select";
            return false;
        }

        std::string got;
        for (SpawnPick const& p : picks)
        {
            got += std::to_string(p.entry);
            got += ',';
        }
        if (got != PD_SPAWN_DRAW_NOBOSS_PIN)
        {
            why = "the no-boss spawn draw moved: " + got;
            return false;
        }
        if (picks.empty() || picks.front().entry != 84263)
        {
            why = "the boss-standin tie-break did not pick the first-loaded "
                  "member (caster 84263) - the melee/caster split order bug "
                  "is back";
            return false;
        }
        return true;
    }

    // --- one pack per room (Task 13, PDv2 content-expansion plan) ----------
    //
    // Every trash pick of a room comes from ONE pack. The boss slot is
    // exempt: it draws from the role-2 pool across all packs, so a room's
    // theme never constrains which boss can appear.
    //
    // `flat` is PDv2SelectSpawns's whole output for the run and `[begin,
    // end)` is one room's slice of it - the same slicing
    // PDv2PackMgr::SelectSpawns does with trashWanted/bossAdds
    // (PDv2PackMgr.cpp:570-588), reproduced here because that file includes
    // DatabaseEnv.h and cannot link into this harness. `hasBoss` skips slot 0
    // (the boss, always the room's first pick when present) so the boss's
    // packId 0 is never mistaken for "the room's pack is 0".
    bool CheckRoomThemeCoherent(std::vector<SpawnPick> const& flat, size_t begin,
                                size_t end, bool hasBoss, std::string& why)
    {
        int packId = 0;
        bool have = false;
        for (size_t i = hasBoss ? begin + 1 : begin; i < end; ++i)
        {
            if (!have)
            {
                packId = flat[i].packId;
                have = true;
                continue;
            }
            if (flat[i].packId != packId)
            {
                why = "a room mixes two packs";
                return false;
            }
        }
        return true;
    }

    // Real stock (mod_pdungeon_packs.sql), in full: packs 1 and 2 both carry
    // melee, pack 1 alone also carries the five RANGE entries, and pack 3 is
    // the boss draw with no trash member at all. This is not a synthetic
    // fixture - it is exactly what Rule 1 and Rule 3 of Task 13's brief are
    // about: pack 3 must never be drawable as a room's theme (it has nothing
    // to fill a trash slot with), and a room themed to pack 2 must still get
    // caster picks - from the merged pool, since pack 2 has none of its own -
    // while its melee slots stay pack 2.
    PackPools ThemeCoherencePackPools()
    {
        PackPools pools;
        pools.melee = {
            {1, 84264}, {1, 84265}, {1, 84266}, {1, 84268}, {1, 84272},
            {1, 84274}, {1, 84280}, {1, 84284},
            {2, 84267}, {2, 84269}, {2, 84270}, {2, 84271}, {2, 84273},
            {2, 84275}, {2, 84277}, {2, 84278}, {2, 84279}, {2, 84282},
            {2, 84283}, {2, 84286},
        };
        pools.caster = {
            {1, 84263, PACK_ROLE_CASTER, 47809}, {1, 84276, PACK_ROLE_CASTER, 47857},
            {1, 84281, PACK_ROLE_CASTER, 62129}, {1, 84285, PACK_ROLE_CASTER, 42842},
            {1, 84287, PACK_ROLE_CASTER, 47809},
        };
        pools.boss = { {3, 84288}, {3, 84289}, {3, 84290} };
        // Pack 3 excluded: it has zero non-boss members (Rule 1).
        pools.trashPackIds = { 1, 2 };
        pools.meleeByPack = {
            { 1, { {1, 84264}, {1, 84265}, {1, 84266}, {1, 84268}, {1, 84272},
                   {1, 84274}, {1, 84280}, {1, 84284} } },
            { 2, { {2, 84267}, {2, 84269}, {2, 84270}, {2, 84271}, {2, 84273},
                   {2, 84275}, {2, 84277}, {2, 84278}, {2, 84279}, {2, 84282},
                   {2, 84283}, {2, 84286} } },
        };
        pools.casterByPack = {
            // Pack 2 deliberately absent here (Rule 3): it has no caster
            // group, so casterOf(2) must answer empty and send those slots
            // to the merged pool above.
            { 1, { {1, 84263, PACK_ROLE_CASTER, 47809}, {1, 84276, PACK_ROLE_CASTER, 47857},
                   {1, 84281, PACK_ROLE_CASTER, 62129}, {1, 84285, PACK_ROLE_CASTER, 42842},
                   {1, 84287, PACK_ROLE_CASTER, 47809} } },
        };
        return pools;
    }

    // Review finding (Task 13 fix pass): PDv2PackMgr::SelectSpawns used to
    // hand `pools.trashPackIds` through straight from a load-time list that
    // was theme-wide, not run-filtered, so it could disagree with
    // meleeByPack/casterByPack and offer a pack with nothing to draw. The
    // fix moved the filtering itself (FilterEligibleTrashPacks,
    // generator/PDv2PackDraw.cpp) into this engine-free file specifically so
    // it is testable here - CheckRoomThemeCoherent below cannot catch this
    // failure mode no matter how it is strengthened, because
    // SpawnPick::packId records the room's DRAWN theme, not where a member
    // actually came from: every pick in a fully-fallen-back room still
    // carries the same (wrong) theme, so it still "coheres" by that
    // measure. Testing the filter directly, rather than its downstream
    // symptom, is what actually closes the gap.
    bool CheckEligibleTrashPackFilter(std::string& why)
    {
        // ThemeCoherencePackPools() only ever builds meleeByPack/casterByPack
        // groups for packs 1 and 2 (pack 3 is boss-only, per its own
        // comment). Pack 4 is not in this fixture at all - the same shape a
        // stale/theme-wide load-time list would produce: a candidate the
        // filtered groups know nothing about.
        PackPools const pools = ThemeCoherencePackPools();
        std::vector<int> const loaded = { 1, 2, 3, 4 };
        std::vector<int> const want = { 1, 2 };
        std::vector<int> const got = FilterEligibleTrashPacks(loaded, pools);
        if (got != want)
        {
            std::string gotStr;
            for (int id : got)
            {
                gotStr += std::to_string(id);
                gotStr += ',';
            }
            why = "FilterEligibleTrashPacks let an ineligible pack through, dropped an "
                  "eligible one, or lost the ascending order: got " + gotStr;
            return false;
        }
        return true;
    }

    void RunPackThemeChecks(int seeds)
    {
        PackPools const pools = ThemeCoherencePackPools();
        for (int i = 0; i < seeds; ++i)
        {
            uint32_t const seed = static_cast<uint32_t>(i) * 2246822519u + 41u;

            SpawnSelectInputs in;
            // Five normal rooms and one boss room - enough per seed that a
            // themed pack with no caster (pack 2) is exercised, without a
            // room count so large the failure log stops being readable.
            in.rooms = { {0, false}, {1, false}, {2, false},
                         {3, false}, {4, false}, {5, true} };
            in.spawnsPerRoom = 5;
            in.bossRoomAdds = 2;
            in.casterPct = 40;
            in.bandMin = 76;
            in.affixPct = 40;

            std::vector<SpawnPick> flat;
            if (!PDv2SelectSpawns(seed, in, pools, flat))
            {
                Check(false, "the theme-coherence draw refused to select", seed);
                continue;
            }

            // Every pool above is rich enough for every slot to fill, so the
            // slice sizes below always match what was actually consumed -
            // the same assumption PDv2PackMgr::SelectSpawns's own slicing
            // documents (PDv2PackMgr.cpp:553-560).
            size_t cursor = 0;
            for (RoomRequest const& room : in.rooms)
            {
                int const trashWanted = room.isBoss ? in.bossRoomAdds : in.spawnsPerRoom;
                size_t const got = static_cast<size_t>(trashWanted + (room.isBoss ? 1 : 0));
                size_t const end = (cursor + got <= flat.size()) ? cursor + got : flat.size();

                std::string why;
                Check(CheckRoomThemeCoherent(flat, cursor, end, room.isBoss, why),
                      why.empty() ? "room theme check failed" : why.c_str(), seed);

                // Rule 1: pack 3 (boss-only in the real stock) has no
                // non-boss member and must never surface as a trash pick's
                // packId - if it did, the eligibility filter let a pack with
                // nothing to fill through.
                for (size_t k = room.isBoss ? cursor + 1 : cursor; k < end; ++k)
                {
                    Check(flat[k].packId != 3,
                          "a trash pick carries the boss-only pack's id - the "
                          "eligibility filter let it through", seed);
                }

                cursor = end;
            }
        }
    }

    // Round B: the field the ENGINE runs, over the engine's real configuration
    // space rather than a diagonal of it. The fixed 8x8 batch never exercised
    // a shrunken field at all, and the first version of this sweep walked
    // `rooms = 1..15` with `bossRooms = GameBossRooms(rooms - 3)` - the player
    // who always picks the maximum, one line through a two-dimensional space.
    //
    // The two axes are independent in the engine: dlvl decides the boss count
    // (GameBossRooms) and the room CAP, the player's slider decides the rooms
    // inside that cap, so a dlvl-30 account may run four boss rooms with one
    // room or with fifteen. This mirrors PDv2Mgr::GeneratePlan exactly, with
    // the configured V2.FieldBlocks at its default 8:
    //
    //     cfg.rooms       = GameClampRooms(<the player's choice>, dlvl)
    //     cfg.bossRooms   = GameBossRooms(dlvl)
    //     cfg.fieldBlocks = min(V2.FieldBlocks, GameFieldBlocksForRooms(rooms + bossRooms))
    //
    // Every row must generate on every seed, validate, and fit the manifest
    // budget. `retries` is printed per row because it, not the failure count,
    // is what would show the free-route rule starting to strain.
    void RunEngineFieldSweep(int seeds)
    {
        int const confFieldBlocks = 8;      // V2.FieldBlocks, the shipped default
        std::printf("engine-field sweep, %d seeds per row (branches 2, V2.FieldBlocks %d)\n",
                    seeds, confFieldBlocks);
        std::printf("   dlvl  rooms  boss  field  genfail  retries  maxManifest\n");
        int const dlvls[4] = { 0, 10, 20, 30 };
        for (int dlvl : dlvls)
        {
            int const boss = GameBossRooms(dlvl);
            std::set<int> done;
            int const choices[5] = { 1, 2, 3, 4, GameRoomsCap(dlvl) };
            for (int choice : choices)
            {
                int const rooms = GameClampRooms(choice, dlvl);
                if (!done.insert(rooms).second)
                {
                    continue;               // the clamp folded two choices onto one row
                }
                int const field = std::min(confFieldBlocks,
                                           GameFieldBlocksForRooms(rooms + boss));
                int failures = 0;
                int retries = 0;
                size_t maxManifest = 0;
                for (int i = 0; i < seeds; ++i)
                {
                    uint32_t const seed = static_cast<uint32_t>(i) * 2654435761u + 5u;
                    BlockCfg cfg = MakeCfg(seed, rooms);
                    cfg.bossRooms = boss;
                    cfg.fieldBlocks = field;
                    BlockPlan plan;
                    if (!GenerateBlockPlan(cfg, &plan))
                    {
                        ++failures;
                        continue;
                    }
                    if (plan.effectiveSeed != cfg.seed)
                    {
                        ++retries;
                    }
                    std::string err;
                    Check(ValidateBlockPlan(plan, &err),
                          err.empty() ? "sweep layout failed validation" : err.c_str(), seed);
                    std::string const m = EmitManifest(plan, 99);
                    maxManifest = (m.size() > maxManifest) ? m.size() : maxManifest;
                }
                std::printf("  %5d  %5d  %4d  %5d  %7d  %7d  %11d\n", dlvl, rooms, boss,
                            field, failures, retries, static_cast<int>(maxManifest));
                char msg[200];
                std::snprintf(msg, sizeof(msg),
                              "engine field %dx%d cannot seat %d room(s) + %d boss "
                              "(dlvl %d) on %d of %d seeds",
                              field, field, rooms, boss, dlvl, failures, seeds);
                Check(failures == 0, msg, 0);
                std::snprintf(msg, sizeof(msg),
                              "an engine-field layout is over the manifest budget: "
                              "%d B at dlvl %d, %d room(s) + %d boss",
                              static_cast<int>(maxManifest), dlvl, rooms, boss);
                Check(maxManifest <= static_cast<size_t>(PD_GAME_MANIFEST_BUDGET_B), msg, 0);
            }
        }
        std::printf("\n");
    }

    // Round B / B1: the typed anchors B2's spawn placement stands on (until
    // Round C the altar stood on the entry one as well). Every room-role
    // chunk publishes an entry on a walkable cell, the entry is the first
    // point of the flat list (AnchorsFor[0] - the order 48 emits), boss rooms
    // publish a boss, rooms a chest and spawns.
    // Pinned on two chunks, captured by running: paste the value out of the
    // "typed anchors ... moved" failure message, never by reasoning about
    // what it should be. 12015 is an ordinary room (no boss, a chest, six
    // spawns), 12215 a boss room (boss + chest + four elites).
    char const* const PD_ROOM_ANCHOR_PIN_12015 =
        "E29.1667,29.1667;C45.3333,45.3333;S25.3333,25.3333,melee;S25.3333,41.3333,melee;S41.3333,25.3333,melee;S41.3333,41.3333,melee;S33.3333,21.3333,caster;S33.3333,45.3333,caster;";
    char const* const PD_ROOM_ANCHOR_PIN_12215 =
        "E29.1667,29.1667;B33.3333,33.3333;C45.3333,33.3333;S25.3333,25.3333,elite;S25.3333,41.3333,elite;S41.3333,25.3333,elite;S41.3333,41.3333,elite;";

    std::string RoomAnchorsString(RoomAnchors const& a)
    {
        char buf[96];
        std::string s;
        if (a.hasEntry) { std::snprintf(buf, sizeof(buf), "E%.4f,%.4f;", a.entry.u, a.entry.v); s += buf; }
        if (a.hasBoss)  { std::snprintf(buf, sizeof(buf), "B%.4f,%.4f;", a.boss.u, a.boss.v); s += buf; }
        if (a.hasChest) { std::snprintf(buf, sizeof(buf), "C%.4f,%.4f;", a.chest.u, a.chest.v); s += buf; }
        for (SpawnAnchor const& p : a.spawns)
        {
            std::snprintf(buf, sizeof(buf), "S%.4f,%.4f,%s;", p.u, p.v, p.role.c_str());
            s += buf;
        }
        return s;
    }

    // Round B / B2: WHERE the picks of one room stand. PlanSpawnPoints is
    // pure geometry over the anchors above - it draws nothing - so a pin
    // states the placement contract the instance script spawns on, and the
    // sweep below proves the whole kit satisfies it rather than four chunks.
    //
    // The role vectors are the shipped shape of a pack: an ordinary room asks
    // for four melee and one caster, a boss room for the boss itself (pick 0,
    // PDv2PackMgr's contract) and two adds.
    std::vector<int> RoomSpawnRoles()
    {
        return { SPAWN_ROLE_MELEE, SPAWN_ROLE_MELEE, SPAWN_ROLE_MELEE,
                 SPAWN_ROLE_MELEE, SPAWN_ROLE_CASTER };
    }

    std::vector<int> BossSpawnRoles()
    {
        return { SPAWN_ROLE_BOSS, SPAWN_ROLE_MELEE, SPAWN_ROLE_MELEE };
    }

    std::string SpawnPointsString(std::vector<PDv2SpawnPoint> const& points)
    {
        char buf[64];
        std::string s;
        for (PDv2SpawnPoint const& p : points)
        {
            std::snprintf(buf, sizeof(buf), "%.4f,%.4f;", p.u, p.v);
            s += buf;
        }
        return s;
    }

    // CAPTURE PROCEDURE, every time: run `pdblock --batch` and paste the
    // value out of the "the spawn points of chunk N moved" failure message,
    // never by reasoning about what it should be. 12015 is the 50 yd room
    // (theme 2, alt 0) on its anchor ring, 12215 the boss room whose first
    // pick takes the boss anchor, 4015 the new 33 yd room (theme 1, alt 2) on
    // the scaled ring, and 13015 the blob room (theme 2, alt 1), whose
    // anchors follow the outline rather than the ring.
    char const* const PD_SPAWN_POINTS_PIN_12015 =
        "25.3333,25.3333;25.3333,41.3333;41.3333,25.3333;41.3333,41.3333;33.3333,21.3333;";
    char const* const PD_SPAWN_POINTS_PIN_12215 =
        "33.3333,33.3333;25.3333,25.3333;25.3333,41.3333;";
    char const* const PD_SPAWN_POINTS_PIN_4015 =
        "28.0000,28.0000;28.0000,38.6667;38.6667,28.0000;38.6667,38.6667;33.3333,25.3333;";
    char const* const PD_SPAWN_POINTS_PIN_13015 =
        "20.8333,20.8333;20.8333,45.8333;45.8333,20.8333;45.8333,45.8333;12.5000,37.5000;";

    // How many sockets a chunk id's low two digits open. The kit's mask is
    // four bits (N, E, S, W); a corridor's anchor set has one arm per bit.
    int SocketCountOf(int mask)
    {
        int n = 0;
        for (int bit = 1; bit <= 8; bit <<= 1)
        {
            if (mask & bit)
            {
                ++n;
            }
        }
        return n;
    }

    void RunTypedAnchorChecks()
    {
        if (g_kit.empty()) return;
        int roomChunks = 0;
        int corridorChunks = 0;
        int deadEndChunks = 0;
        // Round B / B2: how many chunks of each room role the spawn-point
        // sweep below actually walked, checked against role x alt x mask at
        // the end. A sweep that silently stopped covering the new alt would
        // otherwise pass by saying nothing.
        int planned[3] = { 0, 0, 0 };
        for (auto const& kv : g_kit)
        {
            int const id = kv.first;
            int const role = (id % 1000) / 100;
            KitChunk const& c = kv.second;
            if (role > 2)
            {
                // Corridors are NOT anchor-less - a claim this comment made
                // until the Task 1 review caught it. 48's anchors_for gives a
                // corridor (roles 3..6: straight, corner, T, cross) one
                // "patrol" spawn per OPEN socket, on that arm's far cell, and
                // a dead-end stub (role 7, always a single socket) a chest on
                // its junction square with no spawn at all. B4's patrol
                // routes and B1's loop-room chest both stand on these, so
                // they are pinned here rather than assumed.
                if (role >= 3 && role <= 6 && SocketCountOf(id % 100) >= 2)
                {
                    ++corridorChunks;
                    int patrols = 0;
                    for (SpawnAnchor const& p : c.typed.spawns)
                    {
                        if (p.role == "patrol")
                        {
                            ++patrols;
                        }
                    }
                    Check(patrols >= 2, "a corridor chunk publishes fewer than two patrol anchors",
                          static_cast<uint32_t>(id));
                }
                if (role == 7)
                {
                    ++deadEndChunks;
                    Check(c.typed.hasChest, "a dead-end chunk publishes no chest anchor",
                          static_cast<uint32_t>(id));
                }
                continue;
            }
            ++roomChunks;
            Check(c.typed.hasEntry, "a room chunk publishes no entry anchor", static_cast<uint32_t>(id));
            if (!c.typed.hasEntry) continue;
            int const row = static_cast<int>(c.typed.entry.u / PD_CELL_SIZE_YD);
            int const col = static_cast<int>(c.typed.entry.v / PD_CELL_SIZE_YD);
            bool const inside = row >= 0 && col >= 0 && row < PD_CELLS_PER_BLOCK && col < PD_CELLS_PER_BLOCK;
            Check(inside && c.classes.size() == 64 && c.classes[static_cast<size_t>(row * PD_CELLS_PER_BLOCK + col)] == 'W',
                  "a room chunk's entry anchor is not on a walkable cell", static_cast<uint32_t>(id));
            Check(!c.anchors.empty() && c.anchors[0].u == c.typed.entry.u && c.anchors[0].v == c.typed.entry.v,
                  "the entry anchor is not the first point of the flat anchor list", static_cast<uint32_t>(id));
            if (role == 2)
            {
                Check(c.typed.hasBoss, "a boss chunk publishes no boss anchor", static_cast<uint32_t>(id));
                Check(c.typed.spawns.size() >= 2, "a boss chunk publishes fewer than two spawn anchors", static_cast<uint32_t>(id));
            }
            if (role == 0)
            {
                Check(c.typed.hasChest, "a room chunk publishes no chest anchor", static_cast<uint32_t>(id));
                Check(c.typed.spawns.size() >= 5, "a room chunk publishes fewer than five spawn anchors", static_cast<uint32_t>(id));
            }
            if (role == 1)
            {
                Check(c.typed.spawns.empty(), "the entrance chunk publishes spawn anchors", static_cast<uint32_t>(id));
            }

            // B2's placement, on EVERY room-role chunk the kit ships - role x
            // alt x mask, both themes - not just the four pinned ones: a pick
            // planted on a non-walkable cell is a mob that stands in the void
            // or inside a wall, and only the sweep can find the one mask that
            // does it. The entrance publishes no spawn anchors at all, so it
            // is also the only geometry here that walks the 12 yd overflow
            // circle.
            std::vector<int> const roles = (role == 2) ? BossSpawnRoles() : RoomSpawnRoles();
            std::vector<PDv2SpawnPoint> const points = PlanSpawnPoints(c.typed, role == 2, roles);
            Check(points.size() == roles.size(),
                  "PlanSpawnPoints returned a different number of points than picks",
                  static_cast<uint32_t>(id));
            ++planned[role];
            for (PDv2SpawnPoint const& p : points)
            {
                int const prow = static_cast<int>(p.u / PD_CELL_SIZE_YD);
                int const pcol = static_cast<int>(p.v / PD_CELL_SIZE_YD);
                bool const on = prow >= 0 && pcol >= 0 &&
                                prow < PD_CELLS_PER_BLOCK && pcol < PD_CELLS_PER_BLOCK &&
                                c.classes.size() == 64 &&
                                c.classes[static_cast<size_t>(prow * PD_CELLS_PER_BLOCK + pcol)] == 'W';
                Check(on, "a planned spawn point is not on a walkable cell of its chunk",
                      static_cast<uint32_t>(id));
            }
        }
        Check(roomChunks > 0, "no room chunk in kit_meta.json", 0);
        // Completeness of the sweep, not a sample of it: 15 masks in each of
        // the two theme namespaces, times the alts that role ships. This is
        // the check that goes red if the kit ever stops shipping the third
        // Room look while AltCountFor still promises it.
        for (int r = 0; r <= 2; ++r)
        {
            int const want = AltCountFor(static_cast<BlockRole>(r)) * 15 * 2;
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "the spawn-point sweep covered %d chunks of room role %d, "
                          "the kit ships %d (alts x masks x themes)",
                          planned[r], r, want);
            Check(planned[r] == want, msg, 0);
        }
        // Non-vacuity: the two corridor rules above are silent on an empty
        // set, and a kit that stopped shipping corridors would pass them.
        Check(corridorChunks > 0, "no multi-socket corridor chunk in kit_meta.json", 0);
        Check(deadEndChunks > 0, "no dead-end chunk in kit_meta.json", 0);
        auto pin = [&](int id, char const* want)
        {
            auto it = g_kit.find(id);
            if (it == g_kit.end()) { Check(false, "pinned chunk missing from kit_meta.json", static_cast<uint32_t>(id)); return; }
            std::string const got = RoomAnchorsString(it->second.typed);
            if (got != want)
            {
                std::string const why = "the typed anchors of chunk " + std::to_string(id) + " moved: " + got;
                Check(false, why.c_str(), static_cast<uint32_t>(id));
            }
        };
        pin(12015, PD_ROOM_ANCHOR_PIN_12015);
        pin(12215, PD_ROOM_ANCHOR_PIN_12215);

        auto pinPoints = [&](int id, bool bossRoom, std::vector<int> const& roles, char const* want)
        {
            auto it = g_kit.find(id);
            if (it == g_kit.end()) { Check(false, "pinned chunk missing from kit_meta.json", static_cast<uint32_t>(id)); return; }
            std::string const got = SpawnPointsString(PlanSpawnPoints(it->second.typed, bossRoom, roles));
            if (got != want)
            {
                std::string const why = "the spawn points of chunk " + std::to_string(id) + " moved: " + got;
                Check(false, why.c_str(), static_cast<uint32_t>(id));
            }
        };
        pinPoints(12015, false, RoomSpawnRoles(), PD_SPAWN_POINTS_PIN_12015);
        pinPoints(12215, true, BossSpawnRoles(), PD_SPAWN_POINTS_PIN_12215);
        pinPoints(4015, false, RoomSpawnRoles(), PD_SPAWN_POINTS_PIN_4015);
        pinPoints(13015, false, RoomSpawnRoles(), PD_SPAWN_POINTS_PIN_13015);

        // Overflow, the path only a raised V2.SpawnsPerRoom reaches and the
        // four pins never walk: 12015 publishes six spawn anchors, so eight
        // picks must still come back as eight points - the six anchors first,
        // then the legacy 12 yd circle around the block centre for the last
        // two, angle by pick index over the count.
        {
            auto it = g_kit.find(12015);
            if (it == g_kit.end())
            {
                Check(false, "pinned chunk missing from kit_meta.json", 12015u);
            }
            else
            {
                RoomAnchors const& a = it->second.typed;
                Check(a.spawns.size() == 6, "chunk 12015 no longer publishes six spawn anchors", 12015u);
                std::vector<int> const many(8, SPAWN_ROLE_MELEE);
                std::vector<PDv2SpawnPoint> const points = PlanSpawnPoints(a, false, many);
                Check(points.size() == 8, "the overflow plan did not return one point per pick", 12015u);
                if (points.size() == 8 && a.spawns.size() == 6)
                {
                    for (size_t i = 0; i < 6; ++i)
                    {
                        bool onAnchor = false;
                        for (SpawnAnchor const& s : a.spawns)
                        {
                            if (s.u == points[i].u && s.v == points[i].v) onAnchor = true;
                        }
                        Check(onAnchor, "an overflow plan left a published anchor unused", 12015u);
                    }
                    double const mid = PD_BLOCK_SIZE_YD / 2.0;
                    for (size_t i = 6; i < 8; ++i)
                    {
                        double const angle = 2.0 * 3.14159265358979 * static_cast<double>(i) / 8.0;
                        Check(std::fabs(points[i].u - (mid + std::cos(angle) * 12.0)) < 1e-9 &&
                              std::fabs(points[i].v - (mid + std::sin(angle) * 12.0)) < 1e-9,
                              "an overflow spawn point is not on the 12 yd circle", 12015u);
                    }
                }
            }
        }
    }

    // --- the operator's Round B layout, pinned ------------------------------
    //
    // Not a synthetic seed. This is EXACTLY the dungeon account 32 walked on
    // 2026-09-08 - the run whose patroller crossed walls and whose corridors
    // never sprang, and the run every Round C fix is aimed at. The config is
    // the account's own row (`acore_characters.pdungeon_account`, research
    // c-research-patrol-ambush.md section C): layout_seed 2052467817, theme 2,
    // cfg_rooms 13, gen_boss_rooms 2, gen_field_blocks 8, gen_origin 256/256,
    // gen_loop_pct 33, gen_branches 2. PDv2Mgr::GeneratePlan builds the same
    // BlockCfg out of those columns (PDv2Mgr.cpp:148-184), and the retry loop
    // makes plan.effectiveSeed = cfg.seed + attempt (PDBlockPlan.cpp:1692,
    // :1866) - so "attempt 0 succeeded" is itself part of what is pinned here.
    //
    // Three pins hold this one layout still, because a report that says "the
    // patrol beat of the operator's run is clean" is worth nothing once the
    // generator quietly hands that account a different dungeon.
    uint32_t const PD_OPERATOR_SEED = 2052467817u;

    BlockCfg OperatorCfg()
    {
        BlockCfg cfg;
        cfg.seed = PD_OPERATOR_SEED;
        cfg.rooms = 13;
        cfg.bossRooms = 2;
        cfg.fieldBlocks = 8;
        cfg.detourChancePct = 33;
        cfg.originBX = 256;
        cfg.originBY = 256;
        cfg.theme = 2;
        cfg.branches = 2;
        // maxTries and maxDeadEnds are left at their defaults, which is what
        // the manager does too - it sets no other field.
        return cfg;
    }

    // `blocks,rooms,boss;bytes;E;crc;`. The first three are the numbers the
    // worldserver printed for this run (Server_2026-09-08_09_49_57.log:952 -
    // "spawned 76 creature(s) in 16 room(s) (2 boss) from a 49-block plan");
    // `rooms` is counted the way SpawnFromPlan counts it into _run.roomsTotal
    // (PDv2InstanceScript.cpp:1034-1045, :1099): every block with a roomId
    // EXCEPT the entrance, loop rooms included.
    //
    // The manifest byte count and CRC trailer are the same instrument
    // RunLayoutFreezeCheck uses, added here for the reason that check exists
    // (Round C / C1 Task 3 review, Important 2): three counts do not detect a
    // layout that keeps its block, room and boss totals and rearranges
    // everything, and this pin is the ONLY guard on the promise that an
    // account which already owns a dungeon gets the same one back on the next
    // restore. EmitManifest(plan, 1) writes one `B;bx;by;chunkId;0;mask` line
    // per block sorted by (by, bx) plus a CRC32 over the whole body, so length
    // + trailer is a byte identity for the placed layout. seq 1, matching
    // RunLayoutFreezeCheck; the seq goes into the head line, so it is part of
    // the CRC. Captured by RUNNING.
    char const* const PD_OPERATOR_PLAN_PIN = "49,16,2;1081;E;2ae1a357;";

    // `k:waypoints:cells;` per boss segment - the simplified waypoint count and
    // the raw A* cell count of the beat SpawnPatrols hands segment k, planned
    // on the grid WITH both barriers sealed, the way the engine plans it.
    //
    // The pin before this one was `1:13:121;2:13:138;`, measured on the OPEN
    // grid, and adding the barriers moved it. Segment 2 is unchanged cell for
    // cell - it never touches a sealed doorway - while segment 1's A* walks a
    // DIFFERENT route of the same 121 cells around one, which the simplifier
    // renders in 12 waypoints instead of 13. Same endpoints either way, cell
    // (27,27) to cell (3,43): a 4-neighbour A* on a staircase always has an
    // equal-length alternative, so the barrier did not lengthen the beat, it
    // moved which staircase wins. The engine plans on the sealed grid, so the
    // sealed numbers are the ones worth pinning. Captured by RUNNING.
    char const* const PD_OPERATOR_PATROL_PIN = "1:12:121;2:13:138;";

    // `chance50:<n>;chance100:<bx,by,seg;...>`. At the shipped default this
    // layout arms NOTHING - the two Chance(50) coins came up 70 and 94
    // (research c-research-ambush-trigger.md), which is why the operator saw
    // no ambush and why the trigger geometry was never actually exercised. At
    // chance 100 the same layout offers two corridors, so the candidate lists
    // are populated and only the coin was in the way. Captured by RUNNING.
    char const* const PD_OPERATOR_AMBUSH_PIN =
        "chance50:0;chance100:259,259,1;257,258,2;";

    // Why Task 4 replaces the 9 yd disc, in one table (research
    // c-research-ambush-trigger.md, "Per-kind lane geometry"; measured over the
    // whole t1b kit by sampling the walk mask at 0.5 yd and maximising a
    // player's closest approach to the block centre over every socket pair).
    // NO CODE BELOW DEPENDS ON IT - it is the record of a measurement that
    // decided a design, kept beside the layout it was measured against.
    //
    //   kind                      chunks                best approach   in 9 yd?
    //   corridor_straight alt 0   2305 2310 12305 12310  8.08 yd         yes - unavoidable
    //   corridor_straight alt 1   3305 3310 13305 13310  11.43 yd        no - the centre
    //                             ((4,4) is wall)                        pillar gives a dogleg
    //   corridor_corner           2403 2406 2409 2412    11.20-11.43 yd  no - and here the
    //                             + the 124xx twins                      SHORTEST line dodges
    //   corridor_t                2507 2511 2513 2514    11.20-11.43 yd  no on a turn,
    //                             + the 125xx twins                      yes straight through
    //   corridor_cross            2615 12615             11.43 yd        no on a turn,
    //                                                                    yes straight through
    //   corridor_dead_end         2701 2702 2704 2708    n/a, one socket  never a candidate
    //
    // The 11.2-11.8 yd figures are all one number: the junction square is the
    // four centre cells, half-width 8.33 yd, so its CORNERS sit at
    // 8.33 * sqrt(2) = 11.79 yd from the centre, and a turn's geodesic touches
    // exactly that corner. The square's inscribed disc is 8.33 yd, which is why
    // a straight-through transit cannot dodge a 9 yd trap and a turn always
    // can. The two spots of the sibling seed 298623763 are the same story
    // measured on a real layout: segment 1 at (260,260) - corridor_t NES, chunk
    // 12507, transit S to N - would have fired at ~0 yd, and segment 2 at
    // (256,263) - corridor_corner NE, chunk 12403, transit E to N - would NOT,
    // at 11.79 yd. A trap half the corridor kinds can be walked past is not a
    // trap, so C2 fires on the player's CELL being in the spot's block instead,
    // which has no tuning constant and no per-kind behaviour at all.

    // The operator's layout end to end: the plan itself, the patrol beats the
    // creature AI would walk on it, and the ambush spots it arms.
    void RunOperatorLayoutChecks()
    {
        BlockPlan plan;
        if (!GenerateBlockPlan(OperatorCfg(), &plan))
        {
            Check(false, "the operator's own configuration no longer generates a layout",
                  PD_OPERATOR_SEED);
            return;
        }

        Check(plan.effectiveSeed == PD_OPERATOR_SEED,
              "the operator's layout now needs a retry - effectiveSeed is no longer the "
              "stored seed, so the account's dungeon changed under it",
              PD_OPERATOR_SEED);

        int rooms = 0, boss = 0;
        for (PlacedBlock const& b : plan.blocks)
        {
            if (b.roomId >= 0 && b.role != BlockRole::RoomEntrance)
            {
                ++rooms;
            }
            if (b.role == BlockRole::RoomBoss)
            {
                ++boss;
            }
        }
        {
            // The manifest's own bytes, not a summary of them: EmitManifest
            // ends in "E;%08x\n", so the last 11 characters are the trailer and
            // the newline is dropped to keep the pin one line.
            std::string const manifest = EmitManifest(plan, 1);
            std::string trailer =
                manifest.size() >= 11 ? manifest.substr(manifest.size() - 11) : manifest;
            if (!trailer.empty() && trailer.back() == '\n')
            {
                trailer.pop_back();
            }
            char buf[128];
            std::snprintf(buf, sizeof buf, "%u,%d,%d;%u;%s;",
                          static_cast<unsigned>(plan.blocks.size()), rooms, boss,
                          static_cast<unsigned>(manifest.size()), trailer.c_str());
            std::string const msg =
                std::string("the operator's layout moved: ") + buf +
                " - the worldserver logged 49,16,2; for this account on 2026-09-08, "
                "and the manifest of that layout was " + PD_OPERATOR_PLAN_PIN;
            Check(std::string(buf) == PD_OPERATOR_PLAN_PIN, msg.c_str(), PD_OPERATOR_SEED);
        }

        {
            // plan.effectiveSeed, never cfg.seed - the engine passes that one
            // (PDv2InstanceScript.cpp:2210-2211), and on a layout that needed a
            // retry the two differ and the spots would come off a stream the
            // run never used.
            //
            // The 50 is a LITERAL, for the reason PD_AMBUSH_PLAN_PIN_MID states
            // above: an operator moving what the server ships with must not turn
            // the batch red, and the label below hard-codes "chance50:" anyway,
            // so a constant here would print a number drawn at some other
            // chance under a label that says 50.
            std::string got = "chance50:";
            char buf[64];
            std::snprintf(buf, sizeof buf, "%u;",
                          static_cast<unsigned>(
                              BuildAmbushPlan(plan, 50, plan.effectiveSeed).size()));
            got += buf;
            got += "chance100:";
            for (AmbushSpot const& spot : BuildAmbushPlan(plan, 100, plan.effectiveSeed))
            {
                std::snprintf(buf, sizeof buf, "%d,%d,%d;", spot.bx, spot.by, spot.segment);
                got += buf;
            }
            std::string const msg = "the operator's ambush spots moved: " + got;
            Check(got == PD_OPERATOR_AMBUSH_PIN, msg.c_str(), PD_OPERATOR_SEED);
        }

        if (g_masks.empty())
        {
            // Same rule as every other walk-grid check in this file: without the
            // kit metadata a pass would be faked, and a skip is the honest
            // answer. The two pins above need no masks and have already run.
            return;
        }

        WalkGrid grid;
        std::string gridErr;
        if (!BuildWalkGrid(plan, MaskFor, &grid, &gridErr))
        {
            Check(false, gridErr.empty() ? "the operator's layout has no walk grid"
                                         : gridErr.c_str(), PD_OPERATOR_SEED);
            return;
        }

        // PDv2CreatureAI.cpp:51 (SNAP_RADIUS_CELLS) and
        // PDv2InstanceScript.cpp:91 (SPAWN_FALLBACK_SNAP_CELLS) are the same 2
        // for the same reason; the beat below walks through both of them.
        int const snapCells = 2;
        double const mid = PD_BLOCK_SIZE_YD / 2.0;
        int const chainLen = ChainLength(plan);
        int const bossRooms = std::max(1, plan.config.bossRooms);
        std::string beats;

        // SEALED, the way the run set-up seals it: SpawnBarriers runs to
        // completion for every segment BEFORE SpawnPatrols is called
        // (PDv2InstanceScript.cpp:306, :311), so the grid a patroller plans its
        // first beat on already has both portcullises down. Sealing it here is
        // not decoration - it is the only way this pin can claim to be the beat
        // the engine hands out.
        //
        // What a barrier seals is the DOORWAY into boss room k and nothing
        // else: LaneCellsForSocket names the two lane cells on the boss block's
        // entry edge, and the same two on the neighbouring corridor's opposite
        // edge, because a creature snapping within two cells could otherwise
        // step straight across (:1956-1957, :2004). It does NOT seal a boss
        // room's exit, which is why segment 2's beat - out of the corridor in
        // front of boss 2, back to boss room 1 - has a route from the start:
        // SpineRunInto answers with the ENTRY socket, boss 1 sits mid-chain,
        // and the patroller approaches it from the far side.
        //
        // EvaluateBarrier(k) can lift a barrier immediately at spawn time, but
        // only for a segment that planned no trash at all; this layout spawned
        // 76 creatures in 16 rooms, so neither of its two is in that case.
        {
            size_t sealed = 0;
            for (int k = 1; k <= bossRooms; ++k)
            {
                int const bossChain = BossChainIndex(chainLen, plan.config.bossRooms, k);
                std::vector<size_t> run;
                unsigned const bit = SpineRunInto(plan, bossChain, &run);
                if (!bit || run.empty() ||
                    (bit != SOCKET_N && bit != SOCKET_E && bit != SOCKET_S && bit != SOCKET_W))
                {
                    continue;   // the engine skips the barrier here too (:1908-1930)
                }
                PlacedBlock const* bossBlock = nullptr;
                for (PlacedBlock const& b : plan.blocks)
                {
                    if (b.chainIndex == bossChain)
                    {
                        bossBlock = &b;     // last match, the way SpineRunInto picks it
                    }
                }
                if (!bossBlock)
                {
                    continue;
                }
                struct Side { PlacedBlock const* block; unsigned edge; };
                Side const sides[2] = { { bossBlock, bit },
                                        { &plan.blocks[run.back()], OppositeSocket(bit) } };
                for (Side const& s : sides)
                {
                    int cells[2][2] = { { 0, 0 }, { 0, 0 } };
                    LaneCellsForSocket(s.edge, cells);
                    for (int i = 0; i < 2; ++i)
                    {
                        // (row, col) -> (x = col, y = row), the one translation
                        // SpawnBarriers' laneCells lambda does (:1883-1894).
                        GridPoint const p = grid.LocalFromGlobalCell(
                            s.block->bx * PD_CELLS_PER_BLOCK + cells[i][1],
                            s.block->by * PD_CELLS_PER_BLOCK + cells[i][0]);
                        if (grid.InBounds(p.x, p.y))
                        {
                            // A doorway lane cell is floor on both sides by
                            // construction - that is what makes it a doorway.
                            // If the (row, col) -> (x, y) translation above were
                            // swapped, this is where it would show: the seal
                            // would land on the wall band beside the lane and
                            // the beats below would move for a reason that has
                            // nothing to do with barriers.
                            Check(grid.At(p.x, p.y),
                                  "a barrier lane cell of the operator's layout was already "
                                  "unwalkable before it was sealed - the lane cell mapping is wrong",
                                  PD_OPERATOR_SEED);
                            grid.cells[static_cast<size_t>(p.y) * grid.width + p.x] = 0;
                            ++sealed;
                        }
                    }
                }
            }
            // Two boss segments, two lane cells on each of the two sides. A
            // silent 0 here would mean the beats below were planned on the open
            // layout after all, which is the mistake this block exists to fix.
            Check(sealed == static_cast<size_t>(bossRooms) * 4,
                  "the operator's barriers sealed a different number of lane cells than "
                  "four per boss segment - the beats below are no longer the engine's",
                  PD_OPERATOR_SEED);
        }

        for (int k = 1; k <= bossRooms; ++k)
        {
            char buf[64];

            // SpawnPatrols' own derivation, line for line
            // (PDv2InstanceScript.cpp:2038-2072): the beat runs from the LAST
            // corridor of the spine run into boss k - the block its portcullis
            // stands next to - back to chain room k-1, which for the first
            // segment is the entrance (chain index 0).
            int const bossChain = BossChainIndex(chainLen, plan.config.bossRooms, k);
            std::vector<size_t> run;
            unsigned const entryBit = SpineRunInto(plan, bossChain, &run);
            int const goalChain =
                k > 1 ? BossChainIndex(chainLen, plan.config.bossRooms, k - 1) : 0;
            PlacedBlock const* goal = nullptr;
            for (PlacedBlock const& b : plan.blocks)
            {
                // Last match, the way SpineRunInto picks it; chainIndex is -1
                // on everything that is not a spine room.
                if (b.chainIndex == goalChain)
                {
                    goal = &b;
                }
            }
            if (!entryBit || run.empty() || !goal)
            {
                // The worldserver placed two patrollers on this layout
                // (Server_2026-09-08_09_49_57.log:1008), so neither segment can
                // legitimately be beatless here.
                Check(false, "a boss segment of the operator's layout lost its patrol beat",
                      PD_OPERATOR_SEED);
                std::snprintf(buf, sizeof buf, "%d:0:0;", k);
                beats += buf;
                continue;
            }

            // float, not double, on purpose: BlockToWorld narrows to float
            // (PDv2Mgr.cpp:465-475) and the block centre sits EXACTLY on the
            // boundary between cell 3 and cell 4 (mid = BLOCK/2 = 4 * CELL), so
            // which cell WorldToCell's floor answers is decided by that
            // narrowing. Computing it in double here would pin a cell the
            // engine never uses.
            PlacedBlock const& start = plan.blocks[run.back()];
            double sxd = 0.0, syd = 0.0;
            BlockLocalToWorld(start.bx, start.by, mid, mid, sxd, syd);
            int scx = 0, scy = 0;
            WorldToCell(static_cast<float>(sxd), static_cast<float>(syd), scx, scy);
            GridPoint startCell = grid.LocalFromGlobalCell(scx, scy);

            // The spawn-side veto (PDv2InstanceScript.cpp:2148-2164): a corridor
            // whose centre cell is void seats the patroller on the nearest
            // walkable cell's CENTRE instead, and the AI then re-derives its
            // cell from that position - which lands back on the same cell.
            GridPoint snapped;
            if (!grid.At(startCell.x, startCell.y) &&
                NearestWalkable(grid, startCell.x, startCell.y, snapCells, snapped))
            {
                startCell = snapped;
            }

            // The AI's own snap of where it stands (PDv2CreatureAI.cpp:340-346).
            GridPoint here;
            if (!NearestWalkable(grid, startCell.x, startCell.y, snapCells, here))
            {
                Check(false, "the operator's patroller stands more than two cells off the "
                             "walkable surface and can never plan a beat", PD_OPERATOR_SEED);
                std::snprintf(buf, sizeof buf, "%d:0:0;", k);
                beats += buf;
                continue;
            }

            // The goal is stored as a GLOBAL cell at spawn time and snapped by
            // the AI, never at spawn (PDv2CreatureAI.cpp:355-361).
            double gxd = 0.0, gyd = 0.0;
            BlockLocalToWorld(goal->bx, goal->by, mid, mid, gxd, gyd);
            int gcx = 0, gcy = 0;
            WorldToCell(static_cast<float>(gxd), static_cast<float>(gyd), gcx, gcy);
            GridPoint const goalCell = grid.LocalFromGlobalCell(gcx, gcy);
            GridPoint goalSnapped;
            if (!NearestWalkable(grid, goalCell.x, goalCell.y, snapCells, goalSnapped))
            {
                Check(false, "the operator's patrol goal is more than two cells off the "
                             "walkable surface", PD_OPERATOR_SEED);
                std::snprintf(buf, sizeof buf, "%d:0:0;", k);
                beats += buf;
                continue;
            }

            // On the SEALED layout - both barriers are already down above, the
            // way they are when SpawnPatrols runs. Both segments still have a
            // route, and that is a fact rather than a hope: a barrier seals the
            // ENTRY doorway of its boss room, segment 1 walks AWAY from boss 1
            // towards the entrance, and segment 2 walks from the corridor in
            // front of boss 2 to boss room 1, which it reaches through that
            // room's unsealed exit socket. A refusal here is therefore a real
            // finding - the no-route branch (PDv2CreatureAI.cpp:363-366) would
            // leave that patroller standing still until a barrier lifts.
            std::vector<GridPoint> path;
            if (!FindGridPath(grid, here, goalSnapped, path))
            {
                Check(false, "the operator's patrol beat has no route with the barriers sealed",
                      PD_OPERATOR_SEED);
                std::snprintf(buf, sizeof buf, "%d:0:0;", k);
                beats += buf;
                continue;
            }
            size_t const cells = path.size();
            SimplifyGridPath(grid, path);

            // The point of the whole block: after Round C's supercover fix
            // EVERY leg the patroller is handed has to stay on the mask, judged
            // twice - by the test the engine uses and by the independent
            // sampled reference that shares no code with it.
            for (size_t i = 1; i < path.size(); ++i)
            {
                Check(GridLineWalkable(grid, path[i - 1], path[i]),
                      "a patrol leg of the operator's layout crosses an unwalkable cell",
                      PD_OPERATOR_SEED);
                Check(SampledLineWalkable(grid, path[i - 1], path[i]),
                      "a patrol leg of the operator's layout leaves the walk mask "
                      "(sampled reference)", PD_OPERATOR_SEED);
            }

            std::snprintf(buf, sizeof buf, "%d:%u:%u;", k,
                          static_cast<unsigned>(path.size()),
                          static_cast<unsigned>(cells));
            beats += buf;
        }

        std::string const msg = "the operator's patrol beats moved: " + beats;
        Check(beats == PD_OPERATOR_PATROL_PIN, msg.c_str(), PD_OPERATOR_SEED);
    }

    int RunBatch(int count, int rooms)
    {
        std::printf("batch of %d seeds, %d rooms + 1 boss each\n\n", count, rooms);

        RunLinkStateChecks();
        RunGameMathChecks();
        // Once, not per seed, and before anything that needs a layout: the
        // block derivation is a property of PDv2WorldMath.h alone, and it is
        // what Round C / C2's ambush trigger stands on.
        RunBlockDerivationChecks();
        RunChainMathChecks();
        RunLayoutFreezeCheck();
        RunTypedAnchorChecks();
        // Outside the mask guard on purpose: the corner cases are hand-built
        // 8x8 grids, so they hold the no-corner-cutting rule still even on a
        // box with no kit staged - which is exactly where the pin below cannot.
        CheckCornerRule();
        if (!g_masks.empty())
        {
            // Once, not per seed: the supercover test is a property of the KIT
            // masks, not of any layout. Skipped without the kit metadata for
            // the same reason as every other walk-grid check in this file -
            // a faked pass is worse than a skip.
            uint64_t approved = 0, rejected = 0;
            CheckSupercover(g_masks, approved, rejected);
            char buf[64];
            std::snprintf(buf, sizeof buf, "%llu,%llu;",
                          static_cast<unsigned long long>(approved),
                          static_cast<unsigned long long>(rejected));
            std::string const msg = std::string("supercover pair counts moved: ") + buf;
            Check(std::string(buf) == PD_SUPERCOVER_PAIRS_PIN, msg.c_str(), 0);
        }
        // Once, not per seed, and outside the mask guard: this is ONE stored
        // layout - the operator's - and two of its three pins need no kit at
        // all.
        RunOperatorLayoutChecks();
        {
            // Two statements, not one call: argument evaluation order is
            // unspecified, and why.c_str() must not be taken before
            // CheckSpawnDrawPinned has finished writing into `why` (the same
            // trap PDv2SelectSpawns's own pickTrash/emit split guards
            // against, one file over).
            std::string why;
            bool const ok = CheckSpawnDrawPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            // Same two-statements-not-one-call reasoning as the pin above,
            // for the same argument-evaluation-order trap.
            std::string why;
            bool const ok = CheckNoBossSpawnDrawPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            std::string why;
            bool const ok = CheckChainPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            // The loop-carrying twin of the pin above, same two-statements
            // shape for the same argument-evaluation-order reason.
            std::string why;
            bool const ok = CheckLoopChainPinned(why);
            Check(ok, why.c_str(), 12348u);
        }
        {
            // B5's own stream: no other pin in this file can see it move.
            // Same two-statements shape for the same reason.
            std::string why;
            bool const ok = CheckAmbushPlanPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            // The mid-chance twin of the pin above - the only check in the
            // file that runs where Chance actually draws. Same two-statements
            // shape for the same argument-evaluation-order reason.
            std::string why;
            bool const ok = CheckAmbushPlanMidPinned(why);
            Check(ok, why.c_str(), 12345u);
        }
        {
            // Seed-free: the doorway table and the socket mirror are geometry,
            // not a draw.
            std::string why;
            bool const ok = CheckLaneCellsPinned(why);
            Check(ok, why.empty() ? "the lane-cell table moved" : why.c_str(), 0);
        }
        // A tenth of the batch, over thirteen (rooms, bossRooms, branches,
        // detourChancePct) combos: the spine properties are STRUCTURAL and
        // hold per seed, so the sample size only decides how much of the draw
        // space gets walked, never whether a rule is true.
        //
        // sawPocket is the STATISTICAL one - non-vacuity over the sample, not
        // a property of any single layout. It is safe at the mandated 500 (a
        // tenth of it is 51 seeds per combo, and the seeds are fixed, so the
        // answer is deterministic per generator version). At a much smaller
        // --batch it could in principle go red without anything being wrong.
        bool sawPocket = false;
        bool sawDetour = false;
        RunChainChecks(count / 10 + 1, sawPocket, sawDetour);
        Check(sawPocket, "no seed in the sample produced a pocket - the pocket pass is dead code", 0);
        Check(sawDetour, "no seed in the sample produced a loop room - the detour draw is dead code", 0);
        RunPhase2Checks(count / 10 + 1);
        RunThemeParityChecks(count / 10 + 1);
        // Same tenth-of-the-batch reasoning: one pack per room is structural
        // too, and a real seed only ever gets a handful of rooms per run.
        RunPackThemeChecks(count / 10 + 1);
        {
            // Not seed-dependent - FilterEligibleTrashPacks is pure over its
            // two arguments - so one Check is enough (Task 13 fix pass).
            std::string why;
            bool const ok = CheckEligibleTrashPackFilter(why);
            Check(ok, why.empty() ? "eligible trash pack filter failed" : why.c_str(), 0);
        }

        // The city cap must hold like the mine cap - its ids are one digit
        // wider, which is exactly the kind of erosion the measurement exists
        // to catch.
        {
            int const cityCap = MeasureRoomCap(count / 5 + 1, false, 2);
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "theme-2 room cap is %d, below the encoded %d",
                          cityCap, PD_GAME_ROOMS_CAP_MEASURED);
            Check(cityCap >= PD_GAME_ROOMS_CAP_MEASURED, msg, 0);
        }

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

        RunEngineFieldSweep(count);

        size_t maxManifest = 0;
        int longestPath = 0;
        int minBlocks = 1 << 30;
        int maxBlocks = 0;
        // minPockets starts at 0, not at a sentinel: `--batch 0` runs no seed
        // at all, and a summary line reading "pockets per layout: 1073741824..0"
        // is a worse answer than "0..0".
        int minPockets = 0, maxPockets = 0;
        // B0b yield: how many boss segments across the whole batch ended up
        // with a loop room. 33 % per segment is a chance, not a quota, and the
        // geometry has to fit as well - so the summary reports what the sample
        // actually produced rather than what the config asked for.
        int loopRoomsSeen = 0, segmentsSeen = 0, loopLayouts = 0;
        // B5 yield, on the same denominator: how many boss segments across the
        // batch drew an ambush at the shipped default (V2.Ambush.Chance 50).
        // A chance, not a quota - and a segment with no spine corridor at all
        // cannot carry one however the coin falls.
        int ambushesSeen = 0;
        bool sawLayout = false;

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
            // drops rooms would make dlvl meaningless. Loop rooms are the one
            // legitimate extra: B0b adds them ON TOP of the budget, so a layout
            // whose segment drew one has exactly one room more.
            int rooms_found = 0;
            int loopsHere = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.roomId >= 0) ++rooms_found;
                if (b.detourOf >= 0) ++loopsHere;
            }
            Check(rooms_found == rooms + 1 + loopsHere,
                  "room count does not match the config plus the loop rooms", seed);

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

            int pocketsHere = 0;
            for (PlacedBlock const& b : plan.blocks)
            {
                if (b.branchOf >= 0) ++pocketsHere;
            }
            loopRoomsSeen += loopsHere;
            segmentsSeen += std::max(1, cfg.bossRooms);
            ambushesSeen += static_cast<int>(
                BuildAmbushPlan(plan, PD_AMBUSH_DEFAULT_CHANCE_PCT, plan.effectiveSeed).size());
            ++loopLayouts;
            minPockets = (!sawLayout || pocketsHere < minPockets) ? pocketsHere : minPockets;
            maxPockets = (pocketsHere > maxPockets) ? pocketsHere : maxPockets;
            sawLayout = true;
        }

        // Statistical over the whole sample, exactly like sawPocket and
        // sawDetour above: at chance 50 over the mandated 500 seeds a zero
        // here means the draw is dead code, not that the coin was unlucky.
        // Guarded by sawLayout so `--batch 0` says nothing rather than lying.
        Check(!sawLayout || ambushesSeen > 0,
              "no seed in the sample drew an ambush at the default chance - the ambush draw is dead code", 0);

        if (longestPath) std::printf("longest room-to-room path: %d cells\n", longestPath);
        std::printf("blocks per layout: %d..%d\n", minBlocks, maxBlocks);
        std::printf("pockets per layout: %d..%d\n", minPockets, maxPockets);
        std::printf("loop rooms: %d of %d segments carry one (%d layouts)\n",
                    loopRoomsSeen, segmentsSeen, loopLayouts);
        std::printf("ambushes: %d of %d segments at the default chance %d%%\n",
                    ambushesSeen, segmentsSeen, PD_AMBUSH_DEFAULT_CHANCE_PCT);
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
    // Same staging directory, beside the SQL: the surface classes and the
    // anchors the decor checks need are only in the kit's JSON.
    LoadKitMeta("C:\\\\wowstuff\\\\ForgottenLand2.0\\\\output\\\\pd_block_kit\\\\FLStream\\\\chunks\\\\t1b\\\\kit_meta.json");

    if (argc >= 2 && std::strcmp(argv[1], "--decor-batch") == 0)
    {
        return RunDecorBatch((argc >= 3) ? std::atoi(argv[2]) : 100);
    }
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
        int const theme = (argc >= 8) ? std::atoi(argv[7]) : 1;
        int const bossRooms = (argc >= 9) ? std::atoi(argv[8]) : 1;
        WriteManifest(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)),
                      rooms, argv[3], obx, oby, theme, bossRooms);
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
