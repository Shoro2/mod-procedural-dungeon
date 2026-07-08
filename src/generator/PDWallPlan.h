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

#ifndef MOD_PDUNGEON_WALL_PLAN_H
#define MOD_PDUNGEON_WALL_PLAN_H

#include "PDLayout.h"
#include <cstdint>
#include <vector>

// Engine-free, deterministic wall/gate geometry analysis. Pure functions of the
// PDLayout tile grid only (no engine include, no palette, no world coordinates),
// so both the engine glue (PDWorldBuilder) and the ASCII harness derive the
// identical run list and gate openings. Fixed y-then-x scans, fixed neighbour
// order; same seed => same plan on gcc/clang/MSVC.
namespace PDungeon
{
    // The "true axis" a Wall tile belongs to, from its wall neighbours. This is
    // what stops the horizontal-first greedy from consuming a vertical wall's
    // tile horizontally (the "verdrehte kleine Waende" fragment bug): the
    // horizontal pass only ever touches Horizontal tiles, the vertical pass only
    // Vertical tiles.
    enum class WallAxis : uint8_t
    {
        None = 0,
        Horizontal,
        Vertical,
        Isolated
    };

    // One maximal straight wall run along a single axis. startX/startY is the low
    // tile of the run's BODY (the Wall tiles it owns); abutLow/abutHigh mark that
    // a perpendicular wall/corner sits immediately beyond that end, so the run's
    // terminal piece must bury one tile into it (taper-gap fix).
    struct WallRun
    {
        int startX = 0;
        int startY = 0;
        int length = 0;     // body Wall tiles along the run axis
        bool horizontal = true;
        bool abutLow = false;   // wall beyond the low end (perpendicular junction/corner)
        bool abutHigh = false;  // wall beyond the high end
    };

    // A genuine gate opening derived locally from the tile grid (not the doorway
    // blob centroid/bbox): the run of Doorway tiles that actually pierces a
    // room's wall ring, capped by wall at both span ends.
    struct GateOpening
    {
        bool valid = false;
        bool spanAlongX = false;    // gate bar lies along X (through-axis is Y)
        int spanTiles = 1;          // width of the opening along the span axis
        float centerTileX = 0.0f;
        float centerTileY = 0.0f;
        int anchorX = 0;            // deterministic salt anchor (run low end)
        int anchorY = 0;
    };

    // The true axis of the Wall tile at (x, y); WallAxis::None if it is not a
    // Wall tile.
    WallAxis WallAxisOf(PDLayout const& layout, int x, int y);

    // Axis-partitioned maximal runs over every Wall tile: horizontal runs first
    // (only Horizontal tiles), then vertical (only Vertical), then isolated
    // pillars. Every Wall tile is the body tile of exactly one run.
    std::vector<WallRun> BuildWallRuns(PDLayout const& layout);

    // Tiles where the wall line makes a TURN - a Wall tile with EXACTLY one
    // horizontal and exactly one vertical wall neighbour: L-corners and offset
    // "steps" (NOT straight walls, straight-through T/+ junctions, or the interior
    // of 2-tile-thick wall bands). A chunky tower dropped on each covers the seam
    // the straight runs leave there (tapered bases + offset runs). Deterministic
    // y-then-x scan.
    std::vector<TilePos> WallJunctions(PDLayout const& layout);

    // The genuine gate opening for one doorway, or {valid=false} when the doorway
    // blob merely hugs a room edge / turns a corner without a real wall-capped
    // choke (so no stray gate is spawned there).
    GateOpening FindGateOpening(PDLayout const& layout, Doorway const& doorway);
}

#endif
