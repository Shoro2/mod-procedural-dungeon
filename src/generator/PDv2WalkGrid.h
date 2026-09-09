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

#ifndef MOD_PDUNGEON_V2_WALK_GRID_H
#define MOD_PDUNGEON_V2_WALK_GRID_H

#include "PDBlockPlan.h"
#include "PDv2WorldMath.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The walkable surface of a PDv2 layout, and A* over it.
//
// Why this exists at all: map 760 has no mmaps, and it never will - the terrain
// is composed by the client at run time, so there is nothing offline for the
// navmesh generator to chew on. A creature that wants to chase a player has
// nothing to path over. This grid is the substitute, and it is the SAME surface
// the client renders because both come from the kit: the client from the block
// ADTs, this from the walk masks the kit generator emits alongside them.
//
// Engine-free, like everything else under src/generator/, so a layout's
// pathing can be checked without a worldserver.
namespace PDungeon
{
    // PD_CELLS_PER_BLOCK - the resolution of everything here - lives in
    // PDv2WorldMath.h next to the yard sizes it divides.

    // Hands out a block variant's 8x8 walk mask, row-major, row 0 = north edge,
    // col 0 = west edge. Returns nullptr for a chunk it does not know, which
    // BuildWalkGrid treats as an error rather than as empty space - a silently
    // unwalkable room is worse than a refusal.
    using WalkMaskProvider = std::function<uint8_t const*(int chunkId)>;

    // Round D / D2 - THE CLEARANCE LAYER, and why a walk mask needed one.
    //
    // A corridor lane is two cells = 16.67 yd wide, but in the city theme the
    // kit stands a house on each flank and the models' visible fronts intrude
    // 3.30 to 6.27 yd past the lane edge - INDEPENDENTLY on the two sides,
    // because each flank draws its own model. Measured over the shipped kit the
    // free passage is 6.6 yd wide on a straight, its centre is off the block
    // centre by up to 1.61 yd, and the clearance of the two lane cells differs
    // by up to 3.87 yd. A cell CENTRE is therefore the wrong place to walk: on
    // 80 % of the theme-2 lane rows a 2 yd body around BOTH lane-cell centres
    // is inside a house, which is what the operator kept seeing ("die pat geht
    // noch immer durch das haus").
    //
    // So the kit publishes, per walkable cell, the point inside that cell with
    // the largest distance to any facade footprint - the CLEAR POINT - and that
    // distance. Three byte grids in the walk mask's own cell order:
    //
    //   clear   quarter-yards to the nearest facade, capped at 15 (= "3.75 yd
    //           or more, free"); 0 on a non-walkable cell
    //   du/dv   the clear point's offset from the cell centre in quarter-yards
    //           along the block-local u (south) and v (east) axes
    //
    // On the wire (kit_meta.json, pdungeon_chunk_meta) du/dv are unsigned bytes
    // biased by +32; a provider hands them over in that form and BuildWalkGrid
    // is what removes the bias, so everything past this point is already
    // signed. A chunk with no layer - an old database, or theme 1, which places
    // no facades at all - reads as 15/0/0 on every walkable cell, which is
    // exactly the behaviour this module had before D2.
    struct PatrolLayers
    {
        uint8_t const* clear = nullptr;   // 64 bytes, 0..15, or null
        uint8_t const* du = nullptr;      // 64 bytes, 0..64 (offset + 32), or null
        uint8_t const* dv = nullptr;      // same
    };

    // The clearance counterpart of WalkMaskProvider. Unlike that one a null
    // answer is NOT an error: a chunk without a clearance layer is a chunk
    // whose cells are all free, and reading it as "all blocked" would strand
    // every patrol on a server whose kit predates D2.
    using PatrolLayerProvider = std::function<PatrolLayers(int chunkId)>;

    // The cap in `patrolClear`, and what an absent layer reads as.
    constexpr uint8_t PD_PATROL_CLEAR_FREE = 15;

    // The unit `patrolClear`, `patrolDu` and `patrolDv` are counted in.
    constexpr double PD_PATROL_QUARTER_YD = 0.25;

    // How far a merged patrol leg may pass from a clear point it merged away,
    // in those same quarter-yards - 2 = 0.5 yd. Named rather than written into
    // MergeClearPoints' default argument so the harness can state the number it
    // is checking instead of restating a literal that could drift.
    constexpr int PD_PATROL_MERGE_TOLERANCE_Q = 2;

    struct GridPoint
    {
        int x = 0;
        int y = 0;

        bool operator==(GridPoint const& o) const { return x == o.x && y == o.y; }
    };

    struct WalkGrid
    {
        int originBX = 0;              // block coordinate of cell column 0
        int originBY = 0;              // block coordinate of cell row 0
        int width = 0;                 // in cells
        int height = 0;
        std::vector<uint8_t> cells;    // 1 = walkable, row-major

        // Round D / D2. Indexed exactly like `cells` and sized with it by
        // BuildWalkGrid, which fills them for EVERY grid it builds - 15/0/0 on
        // a walkable cell whose chunk publishes no layer, 0/0/0 on a
        // non-walkable one. A grid assembled by hand (the harness does that)
        // leaves them empty, and empty means "feature off": every reader below
        // checks the size against `cells` and ignores a vector of any other
        // size rather than indexing past its end, the same guard FindPatrolPath
        // has always applied to the prop mask.
        //
        // A barrier does NOT touch them: SetCellsWalkable only ever writes
        // `cells`, so a lane cell a portcullis closes and re-opens keeps the
        // clearance the kit measured for it.
        std::vector<uint8_t> patrolClear;   // 0..15 quarter-yards, 15 = free
        std::vector<int8_t>  patrolDu;      // clear point offset from the cell
        std::vector<int8_t>  patrolDv;      // centre, quarter-yards, bias removed

        bool InBounds(int cx, int cy) const
        {
            return cx >= 0 && cy >= 0 && cx < width && cy < height;
        }

        bool At(int cx, int cy) const
        {
            return InBounds(cx, cy) &&
                   cells[static_cast<size_t>(cy) * width + cx] != 0;
        }

        // GLOBAL cell coordinate (PDv2WorldMath's WorldToCell frame) <-> this
        // grid's local cells. Local coordinates may fall outside the grid;
        // At() treats those as unwalkable, which is what every caller wants.
        GridPoint LocalFromGlobalCell(int gcx, int gcy) const
        {
            return { gcx - originBX * PD_CELLS_PER_BLOCK,
                     gcy - originBY * PD_CELLS_PER_BLOCK };
        }

        void GlobalFromLocalCell(GridPoint p, int& gcx, int& gcy) const
        {
            gcx = p.x + originBX * PD_CELLS_PER_BLOCK;
            gcy = p.y + originBY * PD_CELLS_PER_BLOCK;
        }

        size_t WalkableCount() const;
    };

    // Lays every placed block's mask into one grid spanning the plan's bounding
    // box. Fails when a chunk's mask is unavailable.
    //
    // `patrolFor` is optional and is read in the SAME copy loop as the mask, so
    // the clearance layer cannot end up describing a different block than the
    // walkable cell it belongs to. Omitting it - or answering with null
    // pointers for a chunk - fills that block's walkable cells with 15/0/0.
    bool BuildWalkGrid(BlockPlan const& plan, WalkMaskProvider const& maskFor,
                       WalkGrid* out, std::string* error,
                       PatrolLayerProvider const& patrolFor = PatrolLayerProvider{});

    // 4-neighbour A* over walkable cells; the returned path includes both ends.
    // 4 rather than 8 on purpose: a diagonal step between two blocked cells
    // would cut a corner the terrain does not actually have.
    bool FindGridPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                      std::vector<GridPoint>& outPath);

    // Collapses a cell path to as few waypoints as possible while keeping every
    // remaining segment on walkable cells, so a creature walks in straight lines
    // instead of stepping around a staircase of 8 yd cells.
    void SimplifyGridPath(WalkGrid const& grid, std::vector<GridPoint>& path);

    // ---- the patrol planner (Round D / D1) ----
    //
    // Why a SECOND planner beside FindGridPath: after Round C's supercover fix
    // the patroller stopped walking through walls, and the operator's next
    // report was that it walked "in einer geraden linie durch ecken von
    // haeusern und objekte hindurch" - straight legs that clip a house corner
    // and cross the module's own props. Neither of those is a wall on this
    // grid. A kit facade intrudes up to 6.1 yd into a corridor mouth while the
    // cell under it stays walkable, and a prop is a GameObject the walk mask
    // has never heard of. A guard would not hug the geometry anyway - it walks
    // down the middle of the lane and turns at the junction - so the answer is
    // not a finer collision test but a different COST:
    //
    //   step         charged for every cell entered
    //   turn         charged when the direction changes, so one long leg beats
    //                a staircase of the same Manhattan length; the first step
    //                is free, a patroller starts with no heading
    //   wallAdjacent charged when the entered cell has a blocked or
    //                out-of-bounds 4-neighbour, so the lane centre is cheaper
    //                than the wall band a facade leans over
    //   propCell     charged when a prop stands on the entered cell - a COST,
    //                not a wall, so a corridor a prop fills still has a route
    //
    // Round D / D2 adds the two terms that read the clearance layer, because
    // `wallAdjacent` could only ever say "this cell touches the WALK MASK's
    // edge" and the mask has never heard of the house leaning over it:
    //
    //   tightPerQuarter  charged per quarter-yard of clearance MISSING from
    //                    the entered cell, i.e. tightPerQuarter * (15 - clear).
    //                    At 8 a cell with 1.0 yd of room costs 88 - more than
    //                    a prop - while a free cell costs nothing, so the
    //                    planner picks the wide half of an off-centre lane
    //                    without needing to be told which half that is.
    //   minClearQ        the entered cell is BLOCKED below this many
    //                    quarter-yards. 4 = 1.0 yd: less than that is not a
    //                    passage a body fits through at all, and pricing it
    //                    would only make the planner buy it when the
    //                    alternative is long enough. A HARD reading needs the
    //                    caller's fallback (see FindPatrolPath below), which
    //                    is why it is a cost field rather than a constant.
    //
    // The chase keeps FindGridPath / SimplifyGridPath / PlanApproach: cutting
    // a corner to reach a player is fine, that is what the diagonal legs exist
    // for. This is for the beat a patrol walks when nothing is chasing anyone.
    struct PatrolCost
    {
        int step = 10;
        int turn = 30;
        int wallAdjacent = 20;
        int propCell = 60;
        int tightPerQuarter = 8;
        int minClearQ = 4;
    };

    // A* over (cell, incoming direction), not over cells alone: a turn charge
    // is a property of HOW a cell was entered, so the direction has to be part
    // of the state - otherwise the cheapest arrival at a cell would be settled
    // once and then charged wrongly for everything that leaves it. Five
    // directions per cell (none, N, E, S, W); `none` belongs to the start and
    // is what makes its first step free.
    //
    // `propCells` may be null. When given it must be indexed exactly like
    // `grid.cells` (y * width + x) and be the same size, and a non-zero byte
    // means a prop stands on that cell; a vector of any other size is IGNORED
    // rather than read past its end. `grid.patrolClear` is read under exactly
    // the same guard, so a grid without a clearance layer plans the way it did
    // before D2.
    //
    // THE START CELL IS NEVER CHARGED AND NEVER BLOCKED. Every term above is a
    // property of ENTERING a cell and the patroller is already standing on the
    // first one; refusing to plan because the square under its feet is tight
    // would strand exactly the creature this feature exists to move. The goal
    // cell IS entered, so a tight goal makes the search fail - which is the
    // point of the caller's fallback: plan again with `minClearQ` 0 and let the
    // cost alone decide. That second call still pays `tightPerQuarter`, so the
    // fallback beat is the widest route available rather than the old one.
    //
    // `outPath` comes back as the cell chain including both ends, the way
    // FindGridPath returns it: single 4-neighbour steps, so every consecutive
    // pair is axis-aligned by construction. False when either end is
    // unwalkable or nothing connects them - the caller must then hold, never
    // walk the straight line.
    //
    // Deterministic on every compiler, like everything else under
    // src/generator/: integer costs only, no floating point, fixed neighbour
    // order N/E/S/W and the open queue's ties broken by state index. A patrol
    // that took a different route each pull would look like a bug even though
    // every route was valid.
    bool FindPatrolPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                        std::vector<uint8_t> const* propCells,
                        std::vector<GridPoint>& outPath,
                        PatrolCost const& cost = PatrolCost{});

    // Drops every waypoint whose neighbours continue in the same direction:
    // the two endpoints and every turn survive, nothing else does. The patrol
    // counterpart of SimplifyGridPath, which merges as far as a walkable
    // straight line reaches and therefore produces diagonals. This one never
    // moves the route - it only says the same route in fewer points - so an
    // axis-aligned cell chain stays axis-aligned, which is the whole promise
    // D1 makes to the creature AI.
    //
    // Since Round D / D2 no patrol beat calls this directly: a beat merges
    // through MergeClearPoints below, which asks this same question about the
    // cells and a second one about the clear points. It stays as the statement
    // of the cell rule ALONE, and as the harness's oracle for it - on a grid
    // without a clearance layer the two are the same function, and the harness
    // pins that equivalence over the hand paths it states the rule with.
    void MergeCollinear(std::vector<GridPoint>& path);

    // The merge a patrol BEAT takes since Round D / D2, and why MergeCollinear
    // stopped being it.
    //
    // A beat's waypoints are walked as CLEAR POINTS (PatrolPointToWorld), not
    // as cell centres, while MergeCollinear decides what to keep from the CELLS
    // alone: it collapses a straight run of cells to its two ends, so every
    // intermediate cell's clear point - the correction this round exists to
    // publish - is thrown away and the leg is walked as one straight line
    // between the two survivors. On a lane whose two flanks are built from
    // different house models that line walks back into the facade the clear
    // points were leading the patrol around. Measured over the shipped kit's 66
    // theme-2 corridor lane runs (D2 review, Important 1): cell by cell 1 of 66
    // legs enters a facade ground box, 0.88 yd deep; merged on cells 16 of 66
    // do, up to 0.95 yd; on the old cell centres 66 of 66 did, 2.4 to 3.6 yd
    // deep. The merge, not the layer, is what put those 16 back inside a house.
    //
    // So this merge asks a second question before it drops a cell: would the
    // straight line between the two KEPT endpoints' clear points still pass
    // every dropped cell's clear point within `toleranceQ` quarter-yards,
    // measured perpendicular to that line? While it would, the cells merge
    // exactly the way MergeCollinear merges them; where it would not, the cell
    // whose passage has moved is KEPT as a waypoint and the leg is split there.
    // A turn is still kept unconditionally, so legs stay axis-aligned in cells
    // - the promise D1 makes to the creature AI - and a cell with no layer
    // reads as its own centre (PatrolInfoAt), so a grid built by hand or from a
    // kit that predates D2 merges exactly as it did before.
    //
    // `toleranceQ` is in the layer's own unit: 2 = 0.5 yd, half of the 1.0 yd
    // `PatrolCost::minClearQ` calls "a body fits". Larger merges more and lets
    // the walked line stray further from the measured passage; 0 would keep
    // every cell whose clear point is not exactly on the line, which is nearly
    // all of them.
    //
    // DEFINED OVER THE RAW CELL CHAIN FindPatrolPath returns, and unlike
    // MergeCollinear it is NOT idempotent: run it again and it sees only the
    // waypoints that survived, so a cell whose objection was raised by a
    // neighbour that has since been dropped can merge on the second pass. That
    // is a property of measuring the cells BETWEEN two waypoints, not a defect,
    // and no call site hands it anything but a fresh chain - the AI merges the
    // plan once and the rejoin merges its own walk-back once, then appends the
    // already merged tail of the beat without touching it again.
    //
    // Deterministic like the planner it follows: the distance test is integer
    // arithmetic over 1/12-yard units (a cell is 100 of them, a quarter-yard is
    // 3), never a double, because this decides HOW MANY waypoints a beat has,
    // and a beat that came out one waypoint longer on MSVC than on gcc would
    // break the determinism contract in CLAUDE.md the way a shuffled heap does.
    void MergeClearPoints(WalkGrid const& grid, std::vector<GridPoint>& path,
                          int toleranceQ = PD_PATROL_MERGE_TOLERANCE_Q);

    // The clearance layer's answer for ONE cell, with every guard in a single
    // place: a grid that carries no layer (a hand-built one, or one from a kit
    // that predates D2) and a cell outside the grid both read 15/0/0 - "free,
    // standing on the centre" - which is exactly what this module did before
    // the layer existed. Everything that reads the layer goes through here, so
    // the waypoint the creature walks to, the number the debug line prints and
    // the number the harness pins can never come from three different guards.
    struct PatrolCellInfo
    {
        uint8_t clear = PD_PATROL_CLEAR_FREE;
        int8_t  du = 0;
        int8_t  dv = 0;
    };

    PatrolCellInfo PatrolInfoAt(WalkGrid const& grid, GridPoint cell);

    // Where a patrol waypoint on `cell` (a LOCAL grid cell) actually is: the
    // cell's CLEAR POINT, not its centre. CellCentreToWorld, then the stored
    // offset - subtracted, because BlockLocalToWorld runs u against -X and v
    // against -Y, so a clear point further south (+du) is at a SMALLER world x.
    //
    // With no clearance layer, or a cell outside the grid, this is exactly
    // CellCentreToWorld and the patrol walks cell centres as it did before D2.
    // The offset can never leave the cell: a cell is 8.33 yd, the kit samples
    // the clear point 0.25 yd inside the border, so |offset| <= 16
    // quarter-yards = 4.0 yd against a half-cell of 4.17 - which the harness
    // asserts by round-tripping through WorldToCell rather than by trusting
    // the kit.
    void PatrolPointToWorld(WalkGrid const& grid, GridPoint cell,
                            double& x, double& y);

    // Nearest walkable cell to (cx, cy) within `radius`, for snapping a position
    // that landed just off the grid. Returns false when nothing is near.
    bool NearestWalkable(WalkGrid const& grid, int cx, int cy, int radius,
                         GridPoint& out);

    // A supercover line test: true when EVERY cell the straight segment between
    // the two cell centres enters is walkable, and - where the segment passes
    // exactly through a cell corner - both cells it straddles are walkable too
    // (no corner cutting; a creature has a body). This is the chase gate:
    // engine line-of-sight cannot serve on map 760, because the server has no
    // VMAP and no terrain there - the engine sees a clear line straight across
    // the void between two platforms, and a creature sent down that line walks
    // off the world. Until Round C this was a Bresenham sampler that tested one
    // rounded cell per major-axis step and silently skipped a cell at every
    // minor-axis transition, which let patrols walk through walls.
    bool GridLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b);

    // What a creature at `from` should do about a target at `to`.
    enum class ApproachKind
    {
        Direct,      // the straight line is walkable - core chase movement is safe
        Path,        // follow `waypoints` with MovePoint(generatePath = false)
        Unreachable  // no walkable route within reach; hold position
    };

    // Both endpoints are snapped to the nearest walkable cell within
    // `snapRadius` first, because a live position rarely sits dead on a
    // walkable cell centre. `waypoints` is filled for Path only: the
    // simplified cell chain from the snapped start (index 0) to the snapped
    // goal, every consecutive pair joined by a walkable straight line.
    ApproachKind PlanApproach(WalkGrid const& grid, GridPoint from, GridPoint to,
                              int snapRadius, std::vector<GridPoint>& waypoints);

    // 'RLE1:<value>*<count>[,...]' -> bytes; the wire form of a walk mask in
    // `pdungeon_chunk_meta`. Shared by the harness and the server-side loader
    // so there is exactly one parser to get wrong.
    bool DecodeWalkMaskRle(std::string const& text, std::vector<uint8_t>& out);
}

#endif
