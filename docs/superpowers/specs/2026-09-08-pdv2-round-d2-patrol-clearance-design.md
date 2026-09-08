# PDv2 Round D2 — patrol clearance: the kit tells the planner where a body actually fits

**Date** 2026-09-08 (evening) · **Branch** `claude/pdv2-round-c-0cf92ad4` (tip `e5dd64a`) · **Status** design,
decided by the session lead from the operator's T2 finding and the measurement below; not implemented.

Operator (T2 of Round D): *"die pat geht noch immer durch das haus. das problem ist, dass der durchgang durch
die häuser nicht immer mittig ist, sondern dass diese teilweise rechts oder links weiter in den gang ragen,
als auf der anderen seite."* Research with `path:line`: `.superpowers/sdd/d-research-patrol-clearance.md`.

## 0. Facts (measured 2026-09-08 on the staged kit `t1b-v37`)

- The walk mask says a corridor lane is cells 3+4 = 16.67 yd. The kit places the facade wall plane 3.0 yd
  inside the lane edge (`48:1970`, `MIN_LANE_YD 7.0`) and the models' visible fronts intrude further: house_q
  3.30 · house_v 3.52 · house_f 4.72 · house_m 4.76 · house_a 5.23 · house_b 5.32 · house_c 6.27 yd. Each
  side draws its own model (`48:2206`), corner noses are seated one at a time worst-first (`48:2442`), so the
  passage is **off-centre by up to ±1.61 yd** (theme 2) with a per-row clearance difference of up to 3.87 yd
  between the two lane cells. Theme-2 city straights: **6.6 yd median physical width, 4.63 yd pinch**.
- With a 2.0 yd body radius around the lane CELL CENTRES, 87 of 108 theme-2 lane rows (80.6 %) lose both
  cells — a boolean mask on cell centres is unusable, and the D1 planner's cell-centre waypoints (4.17 yd off
  the lane middle) are inside a house on most rows. The grid is too coarse for a 6.6 yd passage.
- Footprints exist only inside `theme2_placements` (`facade_at` `48:1931`, `corner_facade_at` `48:2402`,
  `row_ground_box` `48:2395`) and are discarded; `kit_meta` keeps a count. Masks never reach the ADT bake
  (`48:3841`); `[6b]`/`[3]` compare `walkMask` only; `LoadChunkMeta` reads an explicit column list
  (`PDv2Mgr.cpp:516`); `RLE1` carries arbitrary bytes; the harness loads the first `RLE1` per chunk; decor and
  critter plans read `WalkMaskFor` only — a second data layer moves no existing pin.
- Theme 1 has no WMO facades (100 % clear).

## 1. Decisions

| # | Decision |
|---|---|
| 1 | **The kit publishes, per chunk and per cell, the clear point and its clearance**: inside each walkable cell the point (sampled on a 0.5 yd lattice within the cell) with the largest 2D distance to every facade footprint rectangle of that chunk (block-local yards, from `facade_at`/`corner_facade_at`/`row_ground_box` at placement time) — stored as three byte grids in the walk mask's cell order: `patrolClear` (distance in quarter-yards, capped 15 = "≥ 3.75 yd, free"), `patrolDu`, `patrolDv` (offset of the clear point from the cell centre in quarter-yards, stored +32, range 0..64, u/v = the block-local kit axes). Non-walkable cells: 0 / 32 / 32. Rooms and theme 1 come out as 15 / 32 / 32 except at facade-lined edges. |
| 2 | **Transport**: three `RLE1` strings beside `walkMask` in `kit_meta.json` and three new TEXT columns `patrolClear`, `patrolDu`, `patrolDv` in `pdungeon_chunk_meta`, added by a guarded `information_schema` ALTER emitted INSIDE the generated `mod_pdungeon_chunk_meta.sql` (a sibling file would sort after it). `KIT_VERSION` 26 → kit `t1b-v38`; ADTs byte-identical to v37 (masks, anchors, props unchanged; `[6b]` passes without re-capture). |
| 3 | **Module**: `LoadChunkMeta` reads the three columns (NULL/empty → 15/32/32 for every cell, so an old DB keeps today's behaviour); `WalkGrid` gains `patrolClear` (uint8), `patrolDu`, `patrolDv` (int8, already −32) filled in `BuildWalkGrid`'s loop from the same provider table; `PatrolCost` gains `tightPerQuarter = 8` (cost += 8 × (15 − clear) on entering a cell) and `minClearQ = 4` (a cell with clear < 1.0 yd is BLOCKED for patrols); `FindPatrolPath` fails → the planner retries with `minClearQ = 0` and logs `patrol planner fell back to the walk grid` once per beat. |
| 4 | **Waypoints**: a patrol waypoint's world position is the cell's CLEAR POINT, not its centre: `PatrolPointToWorld(grid, cell, x, y)` = `CellCentreToWorld` shifted by `−du·0.25` in world X and `−dv·0.25` in world Y (u ↔ −X, v ↔ −Y as `BlockLocalToWorld`), asserted in the harness to stay inside the cell. Both patrol plan sites (`UpdatePatrol` first plan and the rejoin) and the follower spawn cells use it. The chase keeps cell centres. |
| 5 | **Beat endpoints** stay the doorway lane cells (D2); `MergeCollinear` merges on cells, so a straight lane still yields one leg whose two endpoints sit on their clear points — a line along the passage centre. |
| 6 | Harness: `PD_PATROL_CLEAR_PIN` = the histogram of `patrolClear` over the kit (16 buckets) captured by running; the operator beats re-pinned as `k:waypoints:cells:cost;` plus the sum of |du|+|dv| over the beat; a hand grid proves the tight column loses to the clear one and a fully tight lane row falls back. |
| 7 | Round D Task 3 review carry-overs ride along: `V2.Patrol.FollowDistYd` re-read every follow re-issue (live), the harness operator beats re-derived doorway-to-doorway, the stale segment comment. |

## 2. Config keys
None new. `V2.Patrol.Debug` lines gain the clear value and the offset of each leg's target.

## 3. Acceptance
T1: kit v38 built twice byte-identical (ADTs = v37), `[6b]` green, chain 51/52/49 + DLL 497/0, `pdblock`
green with only the named pins moved; worldserver builds; harness clear histogram non-trivial for theme 2.
T2 (runde30 §3 Nachtrag): the patroller walks the middle of the visible passage, never a house corner.
