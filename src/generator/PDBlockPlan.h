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

#ifndef MOD_PDUNGEON_BLOCK_PLAN_H
#define MOD_PDUNGEON_BLOCK_PLAN_H

#include <cstdint>
#include <string>
#include <vector>

// PDv2 block-level planner.
//
// The v1 generator rasterises rooms onto a 9.5 yd tile grid and the server then
// assembles walls out of GameObjects. PDv2 replaces that: the client renders
// real terrain composed from a kit whose unit is a BLOCK of 2x2 MCNKs, 66.67 yd
// square, 8x8 of them to an ADT tile. So the server's job is no longer to
// rasterise geometry but to decide WHICH kit block sits at which block
// coordinate - and to say so in an FLPD2 manifest.
//
// Rooms are single blocks here, which makes the graph half of the problem
// simpler than in v1: no rectangle packing and no penetration-based separation,
// just distinct cells on a coarse grid.
//
// Engine-free by the same rule as the rest of src/generator/: no AzerothCore
// include may appear here, so tests/ascii_harness.cpp can compile it standalone
// and the layout can be checked without a worldserver.
//
// The manifest this emits is consumed by scripts/49_pd_compose_blocks.py in the
// ForgottenLand2.0 workspace, which is the byte-exact oracle for the client's
// composer. Anything this planner emits must survive that script unchanged.
namespace PDungeon
{
    // Socket bits. The kit uses the same values.
    enum : unsigned
    {
        SOCKET_N = 1u,
        SOCKET_E = 2u,
        SOCKET_S = 4u,
        SOCKET_W = 8u
    };

    // Index into the kit's role table;
    // chunkId = 2000 + alt * 1000 + roleIndex * 100 + mask.
    enum class BlockRole : uint8_t
    {
        Room = 0,
        RoomEntrance = 1,
        RoomBoss = 2,
        CorridorStraight = 3,
        CorridorCorner = 4,
        CorridorT = 5,
        CorridorCross = 6,
        // A single-socket stub: walked into for its chest, never fought in
        // (the kit emits no spawn anchors for it). Phase 2, 2026-08-30.
        CorridorDeadEnd = 7
    };

    // Visual alternates per role, mirrored from the kit's ALT_COUNT in
    // 48_gen_t1_blockkit.py. The harness cross-checks every (role, mask, alt)
    // combination against the shipped chunk-meta SQL, so the two tables
    // cannot drift silently.
    int AltCountFor(BlockRole role);

    struct BlockCfg
    {
        uint32_t seed = 0;
        int rooms = 3;              // ROOM blocks, before boss rooms are added
        int bossRooms = 1;
        int fieldBlocks = 8;        // planning field is fieldBlocks square
        int loopChancePct = 15;     // chance to keep a non-MST edge
        int originBX = 256;         // global block coord of the field origin
        int originBY = 256;
        int theme = 1;
        int maxTries = 12;          // seed+n retries before giving up
        int maxDeadEnds = 2;        // stub corridors attached after the loops
    };

    struct PlacedBlock
    {
        int bx = 0;                 // GLOBAL block coordinate
        int by = 0;
        BlockRole role = BlockRole::Room;
        unsigned socketMask = 0;
        int chunkId = 0;
        int roomId = -1;            // -1 for corridor blocks
        int depth = 0;              // BFS depth from the entrance, rooms only
        int alt = 0;                // visual alternate, < AltCountFor(role)
    };

    struct BlockPlan
    {
        BlockCfg config;
        uint32_t effectiveSeed = 0;
        std::vector<PlacedBlock> blocks;
        int entranceIndex = -1;     // index into blocks
        int bossIndex = -1;

        PlacedBlock const* At(int bx, int by) const;
    };

    // Deterministic: the same cfg always yields the same plan on any compiler,
    // because every draw goes through PDRandom's hand-rolled helpers.
    // Returns false only when maxTries layouts in a row failed validation.
    bool GenerateBlockPlan(BlockCfg const& cfg, BlockPlan* out);

    // Structural self-check, run inside GenerateBlockPlan and again by the
    // harness. `error` receives a short reason on failure.
    bool ValidateBlockPlan(BlockPlan const& plan, std::string* error);

    // FLPD2 manifest, LF separated, CRC32 trailer. This is the wire format the
    // DLL parses and the Python oracle composes from.
    std::string EmitManifest(BlockPlan const& plan, int seq, int manifestVer = 1);

    // zlib CRC-32, so the trailer matches Python's zlib.crc32 and the DLL's.
    uint32_t Crc32(void const* data, size_t len);

    // Human-readable block map for the harness.
    std::string AsciiBlockDump(BlockPlan const& plan);
}

#endif
