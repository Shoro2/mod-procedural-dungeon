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

#ifndef MOD_PDUNGEON_V2_WORLD_MATH_H
#define MOD_PDUNGEON_V2_WORLD_MATH_H

#include <cmath>

// World <-> block/cell coordinate math for PDv2, engine-free.
//
// The manager places spawns with the forward mapping, and the creature AI has
// to invert it to ask the walk grid about live positions. If the two
// directions lived in different files they could disagree, and a mirrored
// inverse is exactly the failure 01 §6.3 warns about: it passes every seam
// check and still sends every creature to the wrong side of the map. Keeping
// both directions here, engine-free, lets the harness check the round trip.
//
// Frames:
//   * FLPD-BLOCK-1: (u, v) in yards from a block's north-west corner, u
//     running south and v running east. The kit's anchors use this frame.
//   * world: the map frame - +X is north, +Y is west. Tile (tx, ty) has its
//     north-west world corner at ((32 - ty) * TILE, (32 - tx) * TILE),
//     mirroring pd_adt_lib. With TILE = 8 * BLOCK that collapses to the
//     block-level forms below, an algebraic identity for any bx/by.
//   * cells: the walk grid's unit, PD_CELLS_PER_BLOCK per block side. A
//     GLOBAL cell coordinate is block * PD_CELLS_PER_BLOCK + cell-in-block;
//     the walk grid itself stores cells relative to its own origin block.
namespace PDungeon
{
    // Shared with the kit generator (48_gen_t1_blockkit.py) and the composer.
    // Changing any of these here alone silently desynchronises the server's
    // idea of where a block is from the terrain the client draws.
    constexpr double PD_TILE_SIZE_YD = 533.33333;
    constexpr int    PD_BLOCKS_PER_TILE = 8;
    constexpr double PD_BLOCK_SIZE_YD = PD_TILE_SIZE_YD / PD_BLOCKS_PER_TILE;

    // Cells per block side, fixed by the kit (WALK_CELLS_PER_BLOCK).
    constexpr int    PD_CELLS_PER_BLOCK = 8;
    constexpr double PD_CELL_SIZE_YD = PD_BLOCK_SIZE_YD / PD_CELLS_PER_BLOCK;

    // The world coordinate of the tile field's north-west edge.
    constexpr double PD_WORLD_MAX_YD = 32.0 * PD_TILE_SIZE_YD;

    // FLPD-BLOCK-1 (u, v) inside block (bx, by) -> world (x, y).
    inline void BlockLocalToWorld(int bx, int by, double u, double v,
                                  double& x, double& y)
    {
        x = PD_WORLD_MAX_YD - (static_cast<double>(by) * PD_BLOCK_SIZE_YD + u);
        y = PD_WORLD_MAX_YD - (static_cast<double>(bx) * PD_BLOCK_SIZE_YD + v);
    }

    // World (x, y) -> the GLOBAL cell containing it.
    inline void WorldToCell(double x, double y, int& gcx, int& gcy)
    {
        gcx = static_cast<int>(std::floor((PD_WORLD_MAX_YD - y) / PD_CELL_SIZE_YD));
        gcy = static_cast<int>(std::floor((PD_WORLD_MAX_YD - x) / PD_CELL_SIZE_YD));
    }

    // Centre of a GLOBAL cell -> world (x, y).
    inline void CellCentreToWorld(int gcx, int gcy, double& x, double& y)
    {
        x = PD_WORLD_MAX_YD - (static_cast<double>(gcy) + 0.5) * PD_CELL_SIZE_YD;
        y = PD_WORLD_MAX_YD - (static_cast<double>(gcx) + 0.5) * PD_CELL_SIZE_YD;
    }
}

#endif
