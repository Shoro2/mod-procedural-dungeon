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

#ifndef MOD_PDUNGEON_V2_DECOR_PLAN_H
#define MOD_PDUNGEON_V2_DECOR_PLAN_H

#include "PDBlockPlan.h"
#include "PDv2WorldMath.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Where a PDv2 layout's props stand.
//
// The kit draws floor, walls and void; it draws no torches, no braziers and
// nothing else that makes a corridor read as a place rather than a texture.
// Those are GameObjects, which means the SERVER owns them - and a server-side
// prop has to be placed from the same data the client composed the terrain
// from, or it hangs in the air or sinks into a wall.
//
// That data is the walk mask. A mask alone cannot say "wall", though: it only
// says walkable / not. The kit therefore publishes a SURFACE CLASS per cell -
// WALK, WALL, VOID - and this file re-derives it by the kit's own stated rule
// so the two can be compared cell for cell (`pdblock --decor-batch` does
// exactly that against kit_meta.json). Re-deriving rather than shipping the
// string is deliberate: the class of a cell must follow the mask the server
// actually loaded, not a second copy of it that a kit regeneration could
// silently leave behind.
//
// Engine-free by the same rule as the rest of src/generator/: no AzerothCore
// include may appear here, so the placement can be checked in the harness
// without a worldserver.
//
// DETERMINISM. Decor draws from its OWN PDRandom stream, seeded from the
// layout seed through PD_DECOR_SEED_MIX. Sharing the layout stream would mean
// that adding one torch shifts every later draw and re-rolls the dungeon; a
// separate stream makes decoration additive, and BuildDecorPlan only ever
// READS the plan it is handed.
namespace PDungeon
{
    // The kit's own letters (kit_meta.json "surfaceClasses.letters"), so a
    // comparison against its metadata is a string compare and nothing else.
    char const DECOR_CLASS_WALK = 'W';
    char const DECOR_CLASS_WALL = 'L';
    char const DECOR_CLASS_VOID = 'V';

    // The only placement kind implemented so far. A rule naming anything else
    // is skipped rather than guessed at - a prop placed by a rule nobody wrote
    // is worse than a prop that never appears.
    char const* const DECOR_PLACEMENT_WALL_FOOT = "wall_foot";

    // Mixed into the layout seed to open the decor stream. An arbitrary odd
    // constant, fixed for ever: changing it re-decorates every stored layout.
    uint32_t const PD_DECOR_SEED_MIX = 0x5EC0DE0Fu;

    // How far into its own cell a wall-foot prop is pushed, towards the wall
    // it belongs to. Under half a cell (4.17 yd) on purpose, so the prop stays
    // on the WALK cell that was drawn for it - which is what makes "every spot
    // is on a walkable cell" a property the harness can check.
    double const DECOR_WALL_NUDGE_YD = 2.5;

    // Clearance every prop keeps from every anchor of its chunk (entry, boss,
    // chest, spawn points). A brazier standing on the boss's feet is not a
    // decoration, it is a bug report.
    double const DECOR_ANCHOR_CLEAR_YD = 3.0;

    // Derives the kit's surface classes from a walk mask: 64 characters,
    // row-major, row 0 = north and col 0 = west, the walk-mask frame.
    //
    // The rule is the kit's, quoted from kit_meta.json: "W = walkable cell;
    // L = unwalkable cell with a walkable 8-neighbour; V = everything else."
    // Neighbours outside the block do not exist - the kit classifies one block
    // at a time and so does this.
    //
    // A null mask yields 64 VOID cells rather than a refusal, because that is
    // what an unknown chunk looks like: nothing to decorate.
    std::string PDv2Classify(uint8_t const* walkMask);

    // One row of `pdungeon_decor_rules`.
    //
    // `roleFilter` is a PREFIX match against the kit's role name, so 'room'
    // covers room, room_entrance and room_boss and 'corridor' covers all four
    // corridor shapes, while 'room_boss' still names exactly one. An empty
    // filter matches every block.
    struct DecorRule
    {
        int         id = 0;
        int         theme = 1;
        std::string roleFilter;
        int         goEntry = 0;
        std::string placement = DECOR_PLACEMENT_WALL_FOOT;
        int         minPerBlock = 0;
        int         maxPerBlock = 0;
        int         weight = 1;
        double      minSpacingYd = 0.0;
    };

    // A kit anchor, in the block-local FLPD-BLOCK-1 frame the walk masks and
    // the spawn placement already use: u runs south, v runs east, both in
    // yards from the block's north-west corner. Only the two axes are kept -
    // the anchors' z is the floor plane, which is where the props stand too.
    struct DecorAnchor
    {
        double u = 0.0;
        double v = 0.0;
    };

    // One prop. `u`/`v` are block-local; the caller turns them into world
    // coordinates with PDv2WorldMath so there is one such conversion in the
    // module and not two.
    struct DecorSpot
    {
        int    bx = 0;              // GLOBAL block coordinate
        int    by = 0;
        int    ruleId = 0;          // the DecorRule that placed it
        int    goEntry = 0;
        double u = 0.0;
        double v = 0.0;
        double orientation = 0.0;   // radians, world frame
    };

    // Same shape as WalkGrid's WalkMaskProvider, and on the server it is the
    // same function: an 8x8 mask per chunk id, or nullptr for one it does not
    // know.
    using DecorMaskProvider = std::function<uint8_t const*(int chunkId)>;

    // The chunk's anchor points, or nullptr when it has none. A corridor has
    // none, so nullptr is an ordinary answer here and not an error.
    using DecorAnchorProvider =
        std::function<std::vector<DecorAnchor> const*(int chunkId)>;

    // The kit's role name for a block role, spelled exactly as
    // `pdungeon_chunk_meta`.`role` spells it - that is what roleFilter is
    // matched against.
    char const* BlockRoleName(BlockRole role);

    // Prefix match, empty filter matches anything.
    bool DecorRoleMatches(std::string const& roleFilter, char const* roleName);

    // The decor stream's seed. Exposed so the harness can prove two callers
    // holding the same layout seed decorate identically.
    uint32_t DecorSeedFrom(uint32_t layoutSeed);

    // '{"entry":{"u":..,"v":..,"z":..},...}' -> the u/v pairs it contains, in
    // the order they appear. Deliberately a scanner and not a parser: it reads
    // the `anchors` column of `pdungeon_chunk_meta` and the same field of
    // kit_meta.json, and both are generated by 48_gen_t1_blockkit.py. Shared
    // by the harness and the server-side loader so there is exactly one such
    // reader to get wrong, exactly like DecodeWalkMaskRle.
    bool DecodeAnchorList(std::string const& json, std::vector<DecorAnchor>& out);

    // The props for a layout, in a fixed order: blocks in plan order, rules by
    // ascending id, candidate cells row-major. Never fails - a block with no
    // candidate cells simply gets no props.
    std::vector<DecorSpot> BuildDecorPlan(BlockPlan const& plan,
                                          DecorMaskProvider const& maskFor,
                                          DecorAnchorProvider const& anchorsFor,
                                          std::vector<DecorRule> const& rules,
                                          uint32_t layoutSeed);
}

#endif
