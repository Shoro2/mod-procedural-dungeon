# PDv2 Round D2 — patrol clearance — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Patrols walk the middle of the visible passage: the kit exports per cell the clear point and its clearance, the planner avoids and blocks tight cells, waypoints sit on the clear points.

**Architecture:** Kit side (script 48): after `theme2_placements`, compute per walkable cell the max-clearance point against the chunk's facade rectangles, emit three RLE1 byte grids into `kit_meta.json` and three TEXT columns into the generated `mod_pdungeon_chunk_meta.sql` (guarded ALTER inside the file), `KIT_VERSION` 26, kit `t1b-v38` with ADTs byte-identical to v37. Module side: loader → `WalkGrid` layers → `FindPatrolPath` costs/blocks + fallback → `PatrolPointToWorld` for waypoints; harness pins.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-d2-patrol-clearance-design.md`. Research: `.superpowers/sdd/d-research-patrol-clearance.md`.

---

## Global Constraints

Round C/D constraints (branch, commits + trailer, LF, `git add` by path, no push; determinism contract; harness build line; gates; worldserver via PowerShell cmake, staged, the running server never touched by a task; kit laws: snapshot 48 before editing, `PYTHONUTF8=1`, patch scripts via the Write tool, two byte-identical builds, staging flip LAST, `[6b]` without re-capture, chain 51 → 52 → 49 → DLL, audits equal to v37).

**Data contract (binding for both tasks):** cell order = `walkMask` order (row-major r·8+c, r = u rows, c = v columns); `patrolClear` byte 0..15 quarter-yards (15 = cap, "free"); `patrolDu`/`patrolDv` byte 0..64 = signed quarter-yard offset + 32 (so 32 = no offset), u/v = block-local kit axes in yards; non-walkable cells 0/32/32; encoding `RLE1:` exactly as `walkMask`; kit_meta keys `"patrolClear"`, `"patrolDu"`, `"patrolDv"`; SQL columns `patrolClear`, `patrolDu`, `patrolDv` TEXT NULL after `walkMask`.

---

### Task A (kit): script 48 emits the clearance layer; kit v38

**Files:** `C:\wowstuff\ForgottenLand2.0\scripts\48_gen_t1_blockkit.py` (snapshot `.v38_20260908`), staging, `tools\pd_v32_patches\README.md`, module `data/sql/db-world/mod_pdungeon_chunk_meta.sql` (regenerated, committed alone).

- [ ] Capture the facade rectangles per chunk where `theme2_placements` places them (`facade_at`, `corner_facade_at`, `row_ground_box` — collect `(wu_lo, wu_hi, wv_lo, wv_hi)` block-local yards per placed facade into a per-chunk list; theme 1 has none).
- [ ] After placements, for every cell: if not walkable → 0/32/32; else sample points on a 0.5 yd lattice inside the cell (17×17 incl. edges; keep 0.25 yd inside the cell border), clearance = min over rectangles of the 2D point-to-rectangle distance (0 inside), pick the max (tie → the point nearest the cell centre, then lowest u, lowest v); store `clear = min(15, round(4·dist))`, `du/dv = round(4·offset) + 32`.
- [ ] Emit the three RLE1 strings beside `walkMask` (kit_meta + SQL); SQL writer: after `SET @KIT`, before the DELETE/INSERT, a guarded ALTER for each column (`information_schema.COLUMNS` check + `PREPARE/EXECUTE`, the `mod_pdungeon_account_branches.sql` pattern); the INSERT gains the three columns. `KIT_VERSION = 26`.
- [ ] Two builds `output5/t1b-v38a`/`b`: `ALL CHECKS PASS`, `[6b]` green (masks unchanged), `diff -r` empty, 247 files; diff vs the v37 staging: ONLY `kit_meta.json` (three new keys + kitVersion/sha) and the SQL (columns, kit 26) — any ADT byte difference is a STOP. Print the theme-2 clearance histogram and the per-kind mean of the lane cells' `clear` (expect straights ≈ 3.3 yd = 13 quarter-yards at the clear point), and the count of lane rows whose BEST cell is < 1.0 yd (should be 0 or near 0 — report it; if not near 0, the sampling or the footprints are wrong, STOP).
- [ ] Staging flip, 51 (dry+real) → 52 → 49 → DLL 497/0, audits = v37, `pdblock --batch 500` (no pin moved — the harness ignores the new keys until Task B). Commit the module SQL: `feat(kit): chunk meta v26 - patrol clearance layer (clear point + quarter-yard clearance per cell)`. README paragraph.

### Task B (module): loader, grid layers, planner costs, clear-point waypoints, harness

**Files:** `src/PDv2Mgr.cpp/.h` (`LoadChunkMeta`, providers), `src/generator/PDv2WalkGrid.h/.cpp` (`WalkGrid` layers, `BuildWalkGrid`, `PatrolCost`, `FindPatrolPath`, `PatrolPointToWorld`), `src/PDv2CreatureAI.cpp/.h` (planner call with fallback, `MoveToWaypoint` for patrols, debug lines, FollowDistYd live), `src/PDv2InstanceScript.cpp` (follower spawn cells via clear points; stale comment ~:395), `tests/blockplan_harness.cpp`.

- [ ] Harness first: `LoadMasks` learns the three keys from the staging (default 15/32/32 when absent — Task A may not have landed; assert both cases build); hand grid: a 2-wide lane where column 3 is tight (clear 2) and column 4 clear (15) → the path takes 4; a lane row with both cells at clear 2 (< minClearQ 4) → `FindPatrolPath` fails, the harness's fallback call with `minClearQ 0` succeeds; `PatrolPointToWorld` round-trip stays in the cell for du/dv = ±16; pins `PD_PATROL_CLEAR_PIN` (16-bucket histogram over the kit) and the operator beats `k:waypoints:cells:cost:offsetSum;` (doorway-to-doorway beats — re-derive them the way `SpawnPatrols` does now, Task 3 review I2).
- [ ] `WalkGrid`: `std::vector<uint8_t> patrolClear; std::vector<int8_t> patrolDu, patrolDv;` sized like `cells`, filled in `BuildWalkGrid`'s copy loop from a `PatrolLayerProvider` (three byte pointers per chunk, may be null → 15/0/0 for walkable cells). `PatrolCost{…, tightPerQuarter 8, minClearQ 4}`; `FindPatrolPath` blocks `clear < minClearQ` and adds `tightPerQuarter × (15 − clear)`; `PatrolPointToWorld(WalkGrid const&, GridPoint, double& x, double& y)`.
- [ ] `PDv2Mgr`: read the three columns (NULL-tolerant), store per chunk, provider for `BuildWalkGrid`. AI: plan with `minClearQ 4`, on failure `minClearQ 0` + one `LOG_WARN`; `MoveToWaypoint` for patrol legs uses `PatrolPointToWorld`; debug leg line prints `clear` and the offset; `FollowDistYd` read at every follow (re)issue (Task 3 review I1); stale comment (I3). Follower spawn cells → clear points.
- [ ] Gates: harness fresh (`--batch 500` — moved pins: only the operator patrol pin and the new ones; `--decor-batch 3000`, `--roomcap 3000`), worldserver build, stage, md5, codestyle, LF. Commit `feat(v2): patrols walk the clear point of each cell; tight cells cost, blocked below 1 yd`.

### Task C: deploy + docs
- [ ] Kit `t1b-v38` to the client + `patch_ini_v38.py` (client closed check); the round doc `tools/pd_testlauf_runde30.md` §3 Nachtrag → the fix, what to see (the pat in the middle of the visible passage), the debug line's `clear`; state table (md5, kit v38, SQL re-applies with the ALTER); vault (MIG-017: kit v38, chunk_meta v26 with three columns, commits; queue; log; kit README v38 paragraph + backup refresh); runbook check.

## Self-review
- Spec 1–2 → Task A; 3–5, 7 → Task B; 6 → Task B; §3 → A/B/C.
- Contract shared verbatim by A and B (the Data contract block).
