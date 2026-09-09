# PDv2 Round C / C1–C3 — patrol supercover, ambush cell trigger, the cache — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The patroller stays on the walk mask (supercover line test, capped rejoin, vetoed spawn), an armed corridor fires when a player stands in its block (cell test), and the Shifting Cache opens once and faces the room.

**Architecture:** One engine-free root-cause fix in `src/generator/PDv2WalkGrid.cpp` (`GridLineWalkable` becomes a supercover test with the no-corner-cutting rule) that every consumer inherits (`SimplifyGridPath`, `PlanApproach`, the AI's chase gate and rejoin), pinned in the harness against every kit walk mask and against a legacy copy of the Bresenham sampler. Engine-side hardening in the patrol AI and the patrol spawn. The ambush trigger moves from a 9 yd disc to "the player's cell is in the spot's block", with the block derivation asserted in the harness. The chest fix is SQL in the file that runs last plus one orientation literal.

**Tech Stack:** C++17 module + `pdblock.exe` harness (MSVC via PowerShell), AzerothCore worldserver build (`cmake --build`), MySQL 8.4 read-only checks.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-c-design.md` §2 C1–C3, §1 facts. Research: `.superpowers/sdd/c-research-patrol-ambush.md` (A1–A5, fixes 1–5), `c-research-ambush-trigger.md` (per-kind lane distances, the two spots of seed 298623763, the replay of seed 2052467817), `c-research-chest-altar-finale-ui.md` §1.

---

## Global Constraints

- Branch `claude/pdv2-round-c-0cf92ad4` (off `5fc686c`). Conventional Commits, English, trailer `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`, `git -c core.autocrlf=false commit`, touched files converted to LF before committing, `git diff --stat` checked for whole-file rewrites, `git add` by path only. No push.
- Determinism contract in `src/generator`: no `<random>`, no unordered containers, no shuffle; `PDRandom::UniformInt(lo, hi)` draws nothing at `lo >= hi`. No draw order changes in this plan — every existing pin except the ones this plan names must stay unmoved.
- Harness build (PowerShell, module root): `cl /nologo /std:c++17 /EHsc /W4 /O2 /I src /Fo:build_tmp\ /Fe:pdblock.exe tests\blockplan_harness.cpp src\generator\PDBlockPlan.cpp src\generator\PDv2WalkGrid.cpp src\generator\PDv2LinkState.cpp src\generator\PDv2DecorPlan.cpp src\generator\PDv2PackDraw.cpp src\generator\PDv2AmbushPlan.cpp` (one accepted warning C4456); freshness `stat -c '%y %n' pdblock.exe src/generator/*.cpp src/generator/*.h tests/blockplan_harness.cpp | sort | tail -3` (exe newest). Gates: `.\pdblock.exe --batch 500`, `--decor-batch 3000`, `--roomcap 3000` all green. Pins are captured by RUNNING (the failure message prints the value), never reasoned.
- Worldserver: `cmake --build C:/wowstuff/dcore_bin --config RelWithDebInfo --target worldserver -- /m` (PowerShell), exit 0, no module warnings; stage `C:\wowstuff\dcore_bin\bin\RelWithDebInfo\worldserver.exe` and report its md5. The operator's worldserver (`C:\wowstuff\dcore\worldserver.exe`, running since 09:49) is never stopped, restarted or overwritten by a task. Code style: `python apps/codestyle/codestyle-cpp.py` from the module root.
- DB: read-only `SELECT`s only (`"/c/Program Files/MySQL/MySQL Server 8.4/bin/mysql.exe" -h127.0.0.1 -uacore -pacore`); SQL changes ship as module files the updater applies on the operator's restart. `creature_template` 84263–84290 and generated SQL files are never edited.
- Kit staging `C:\wowstuff\ForgottenLand2.0\output\pd_block_kit\FLStream\chunks\t1b` (v36c, 244 masks) is read by the harness at start-up and must not change in this plan.
- World frame (`src/generator/PDv2WorldMath.h`): `gcx = floor((MAX - y) / cell)`, `gcy = floor((MAX - x) / cell)`; block `bx = gcx / PD_CELLS_PER_BLOCK`, `by = gcy / PD_CELLS_PER_BLOCK` (global cells are non-negative inside the field).

---

## File Structure

**Modified (generator):** `src/generator/PDv2WalkGrid.cpp` (`GridLineWalkable` :209-228 → supercover), `src/generator/PDv2WalkGrid.h` (:116-122 comment).
**Modified (engine):** `src/PDv2CreatureAI.cpp` (`StartWaypointRun` :210-221, rejoin loop :380-411, `SNAP_RADIUS_CELLS` comment :42-45), `src/PDv2CreatureAI.h` (a constant), `src/PDv2InstanceScript.cpp` (patrol spawn :2104-2121, `SpawnAmbushPlan` log :2241, `TickAmbushes` :2246-2290, chest orientation :1722), `src/PDv2InstanceScript.h` (`Ambush` comment :349-360), `src/PDv2Mgr.cpp/.h` (remove `ambushRadiusYd`), `src/PDv2Commands.cpp:210`, `conf/mod_procedural_dungeon.conf.dist` (remove `V2.Ambush.RadiusYd`).
**Modified (tests):** `tests/blockplan_harness.cpp` (supercover check + pins, legacy sampler copy, block-derivation assert, the operator-layout pins).
**Modified (data):** `data/sql/db-world/mod_pdungeon_templates_fix.sql` (910030 in the DELETE list, its row, an `UPDATE` for `Data0`/`Data3`).
**Docs:** `CLAUDE.md` (walk-grid row, ambush row), `README.md` (ambush sentence), `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde29.md` (new; Task 6), vault (Task 6).

---

### Task 1: `GridLineWalkable` becomes a supercover test (generator + harness)

**Files:**
- Modify: `src/generator/PDv2WalkGrid.cpp:206-228`, `src/generator/PDv2WalkGrid.h:116-122`
- Test: `tests/blockplan_harness.cpp` (new check `CheckSupercover`, called from `RunBatch` once; new pins `PD_SUPERCOVER_PAIRS_PIN`, `PD_SUPERCOVER_REJECTED_PIN`)

**Interfaces:**
- Consumes: `WalkGrid::At(int, int)`, `GridPoint`, `MaskFor(chunkId)` (harness, 64 bytes per chunk, row-major `cells[y * 8 + x]` exactly as `BuildWalkGrid` copies a chunk's mask into the grid — read `BuildWalkGrid` in `PDv2WalkGrid.cpp` and copy its indexing).
- Produces: the same signature `bool GridLineWalkable(WalkGrid const&, GridPoint a, GridPoint b)` with supercover semantics: **true iff every cell the segment between the two cell centres enters is walkable, and at an exact corner crossing both straddling cells are walkable**.

- [ ] **Step 1: Write the failing harness check.** Add to `tests/blockplan_harness.cpp` beside `CheckApproachPolicy`:

```cpp
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

    // One 8x8 grid per kit chunk, built the way BuildWalkGrid copies a mask.
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
                for (int ax = 0; ax < g.width; ++ax)
                {
                    if (!g.At(ax, ay)) continue;
                    for (int by = 0; by < g.height; ++by)
                        for (int bx = 0; bx < g.width; ++bx)
                        {
                            if (!g.At(bx, by)) continue;
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
        Check(rejectedByFix > 0, "the legacy sampler approved nothing the supercover test rejects (vacuous)", 0);
    }
```

In `RunBatch`, once (not per seed), after the masks are loaded:

```cpp
    {
        uint64_t approved = 0, rejected = 0;
        CheckSupercover(g_masks /* the harness's chunk->mask map; use its real name */, approved, rejected);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%llu,%llu;", (unsigned long long)approved, (unsigned long long)rejected);
        Check(std::string(buf) == PD_SUPERCOVER_PAIRS_PIN, "supercover pair counts moved", 0);
        // The failure message must print buf so the pin can be captured by running.
    }
```

with `char const* const PD_SUPERCOVER_PAIRS_PIN = "";` beside the other pins. `Check(...)` is the harness's existing helper; print `buf` in the message the way the other pins do (`pin` lambda pattern).

- [ ] **Step 2: Build the harness and run `--batch 500` — it must FAIL** on "supercover approved a segment that leaves the walk mask" (the old Bresenham approves such segments; the research measured 1.70 % of pairs) — record the first failing chunk id. If it passes, the check is wrong (the mask indexing or the sampler): stop and fix the check before touching the generator.

- [ ] **Step 3: Implement the supercover test.** Replace `GridLineWalkable` in `src/generator/PDv2WalkGrid.cpp:206-228`:

```cpp
    // SUPERCOVER: every cell the segment between the two cell CENTRES enters
    // must be walkable. Exact integer DDA - the next x boundary is crossed at
    // t = (2 nx + 1) / (2 ax), the next y boundary at (2 ny + 1) / (2 ay), and
    // the two are compared cross-multiplied so a tie is exact. A tie is the
    // segment passing through a cell CORNER: then BOTH straddling cells must
    // be walkable (no corner cutting - a creature has a body). Round B's
    // version sampled one rounded cell per major-axis step and skipped a cell
    // at every minor-axis transition, which is how the patroller walked
    // through walls (Round C research A1).
    bool GridLineWalkable(WalkGrid const& grid, GridPoint a, GridPoint b)
    {
        if (!grid.At(a.x, a.y))
        {
            return false;
        }
        int const ax = std::abs(b.x - a.x);
        int const ay = std::abs(b.y - a.y);
        int const sx = b.x > a.x ? 1 : -1;
        int const sy = b.y > a.y ? 1 : -1;
        int x = a.x, y = a.y, nx = 0, ny = 0;
        while (nx < ax || ny < ay)
        {
            bool const stepX = nx < ax && (ny >= ay || (2LL * nx + 1) * ay < (2LL * ny + 1) * ax);
            bool const stepY = ny < ay && (nx >= ax || (2LL * ny + 1) * ax < (2LL * nx + 1) * ay);
            if (!stepX && !stepY)
            {
                // Exact corner crossing.
                if (!grid.At(x + sx, y) || !grid.At(x, y + sy))
                {
                    return false;
                }
                x += sx;
                y += sy;
                ++nx;
                ++ny;
            }
            else if (stepX)
            {
                x += sx;
                ++nx;
            }
            else
            {
                y += sy;
                ++ny;
            }
            if (!grid.At(x, y))
            {
                return false;
            }
        }
        return true;
    }
```

Rewrite the header comment at `PDv2WalkGrid.h:116-122` to say exactly this (supercover, corner rule), and the comment at `.cpp:206-208`.

- [ ] **Step 4: Build, run `--batch 500`.** Expected failures now: ONLY the empty `PD_SUPERCOVER_PAIRS_PIN` (capture it from the message) — plus, possibly, existing layout/decor/critter pins if any of them depend on `SimplifyGridPath`/`PlanApproach` results (they should not: the layout, decor and critter draws never call the line test — verify by grep before accepting any other moved pin; a moved pin here is a STOP). Re-run: ALL CHECKS PASS. Then `--decor-batch 3000`, `--roomcap 3000`.

- [ ] **Step 5: Codestyle, LF, commit** `fix(generator): GridLineWalkable is a supercover test with the no-corner-cutting rule` (files: the two generator files + the harness).

---

### Task 2: Patrol AI hardening — capped rejoin, honest index 0, vetoed spawn

**Files:**
- Modify: `src/PDv2CreatureAI.cpp` (:42-45 comment, :210-221 `StartWaypointRun`, :368-411 rejoin), `src/PDv2CreatureAI.h` (declare nothing new except the constant if you place it there), `src/PDv2InstanceScript.cpp:2104-2121` (patrol spawn)

**Interfaces:**
- Consumes: Task 1's `GridLineWalkable`; `FindGridPath`, `SimplifyGridPath`, `NearestWalkable` (`PDv2WalkGrid.h`); `GetWalkGrid()` on the instance script (the pattern `SpawnFromPlan` uses since `f92146f`).
- Produces: no new public API.

- [ ] **Step 1: Rejoin cap.** In the rejoin loop (`PDv2CreatureAI.cpp:380-401`) add the constant and the cap:

```cpp
        // A rejoin is WALKED, not flown: a waypoint further than this is not
        // "nearby" whatever the line test says (research A4: the old rejoin
        // was an uncapped beeline of up to ~530 yd).
        int const REJOIN_MAX_CELLS = 4;
```

and inside the loop, before the line test:

```cpp
                if (dist > REJOIN_MAX_CELLS)
                {
                    continue;
                }
```

Replace the `best >= _patrolRoute.size()` fallback (`:403-410`) with a walk back onto the beat:

```cpp
            if (best >= _patrolRoute.size())
            {
                // Nothing on the beat is within a straight, short walk. Walk
                // the GRID to the nearest waypoint instead of giving the route
                // up (which used to collapse the beat to "here -> goal").
                size_t nearest = 0;
                int nearestDist = std::numeric_limits<int>::max();
                for (size_t i = 0; i < _patrolRoute.size(); ++i)
                {
                    int const d = std::abs(_patrolRoute[i].x - here.x) + std::abs(_patrolRoute[i].y - here.y);
                    if (d < nearestDist)
                    {
                        nearestDist = d;
                        nearest = i;
                    }
                }
                std::vector<GridPoint> back;
                if (!FindGridPath(*grid, here, _patrolRoute[nearest], back))
                {
                    // Off the beat's component entirely: the old fallback.
                    _patrolActive = false;
                    return;
                }
                SimplifyGridPath(*grid, back);
                leg = back;                              // here ... route[nearest]
                for (size_t i = nearest + 1; i < _patrolRoute.size(); ++i)
                {
                    leg.push_back(_patrolRoute[i]);
                }
                if (leg.size() < 2)
                {
                    std::reverse(_patrolRoute.begin(), _patrolRoute.end());
                    leg = _patrolRoute;
                }
            }
            else
            {
                leg.push_back(here);
                ... (the existing best-based leg construction, unchanged)
            }
```

(`#include <limits>` if missing.)

- [ ] **Step 2: Honest index 0.** `StartWaypointRun` (`:210-221`): walk to waypoint 0 unless the creature already stands in that cell:

```cpp
    void PDv2MobAI::StartWaypointRun(std::vector<GridPoint>&& waypoints, WalkGrid const& grid)
    {
        _waypoints = std::move(waypoints);
        if (_waypoints.size() < 2)
        {
            _followingPath = false;
            return;
        }
        // Index 0 is the planner's SNAPPED start cell. When the snap moved us
        // (up to SNAP_RADIUS_CELLS away) the first leg is the walk INTO that
        // cell; only when we already stand in it is it skipped.
        GridPoint const standing = CellOf(grid, me->GetPositionX(), me->GetPositionY());
        _waypointIndex = (standing == _waypoints[0]) ? 1 : 0;
        _followingPath = true;
        MoveToWaypoint(_waypointIndex, grid);
    }
```

Fix the `SNAP_RADIUS_CELLS` comment (`:42-45`) to the measured "2 cells = up to 16.7 yd".

- [ ] **Step 3: Vetoed patrol spawn.** In `SpawnPatrols` (`PDv2InstanceScript.cpp:2104-2107`), after `BlockToWorld(start.bx, start.by, mid, mid, x, y, z)`, apply the veto exactly like `SpawnFromPlan`'s fallback (`f92146f`): `WorldToCell` → `grid->LocalFromGlobalCell` → if `!grid->At(...)` → `NearestWalkable(*grid, cell.x, cell.y, 2, out)` → `GlobalFromLocalCell` → `CellCentreToWorld` → overwrite `x, y`; `LOG_WARN` once per segment when the veto moved the spawn; keep the centre when the grid is null or nothing is walkable within 2 cells.

- [ ] **Step 4: Build the worldserver** (exit 0, no module warnings), stage, md5. Codestyle. Commit `fix(v2): patroller rejoins its beat by grid walk, honest first waypoint, vetoed spawn`.

---

### Task 3: The operator's layout in the harness — patrol beats and ambush spots pinned

**Files:**
- Test: `tests/blockplan_harness.cpp` (new `--operator` sanity block inside `RunBatch`, three pins)

**Interfaces:**
- Consumes: `GenerateBlockPlan`, `BuildWalkGrid` + `MaskFor`, `FindGridPath`/`SimplifyGridPath`, `BossChainIndex`/`ChainLength`/`SpineRunInto`/`RunFromSocket` (`PDBlockPlan.h`), `BuildAmbushPlan` (`PDv2AmbushPlan.h`).
- Produces: pins `PD_OPERATOR_PLAN_PIN`, `PD_OPERATOR_PATROL_PIN`, `PD_OPERATOR_AMBUSH_PIN`.

- [ ] **Step 1: The config.** From `acore_characters.pdungeon_account` (read-only, research §C): seed **2052467817**, theme 2, rooms 13, bossRooms 2, fieldBlocks 8, origin (256,256), detour 33, branches 2. Build `BlockCfg` from it in the harness and `Check(plan.blocks.size() == 49, …)`, `Check(rooms == 16 incl. boss, boss == 2)` — the numbers the log printed (`spawned 76 creature(s) in 16 room(s) (2 boss) from a 49-block plan`).

- [ ] **Step 2: Patrol beats.** Replicate `SpawnPatrols`' start/goal (read `PDv2InstanceScript.cpp:2040-2075`: goal = the centre cell of chain room `BossChainIndex(chainLen, bossRooms, k-1)` or the entrance for k = 1; start = the centre cell of `run.back()` of `SpineRunInto(plan, BossChainIndex(...k), run)`), `NearestWalkable(2)` both, `FindGridPath`, `SimplifyGridPath`; per segment: `Check` every consecutive waypoint pair passes `GridLineWalkable` **and** `SampledLineWalkable` (Task 1's reference), and pin `"k:waypoints:cells;"` per segment as `PD_OPERATOR_PATROL_PIN` (captured by running).

- [ ] **Step 3: Ambush spots.** `BuildAmbushPlan(plan, 50, plan.effectiveSeed)` → expect **0** spots (the replay: draws 70 and 94); at chance 100 → **2** spots; pin `"chance50:0;chance100:<bx,by,seg;...>"` as `PD_OPERATOR_AMBUSH_PIN`. Also assert for every corridor kind in the kit (research `c-research-ambush-trigger.md` table) that the block centre is within 9 yd of the lane ONLY for straight corridors — i.e. document in a comment why Task 4 replaces the disc; no code depends on it.

- [ ] **Step 4: Build, capture the three pins by running, `--batch 500` / `--decor-batch 3000` / `--roomcap 3000` green.** Commit `test(harness): the operator's Round B layout pinned - plan, patrol beats, ambush spots`.

---

### Task 4: Ambush fires on the player's cell, arm log names the spot, `RadiusYd` removed

**Files:**
- Modify: `src/PDv2InstanceScript.cpp` (`SpawnAmbushPlan` :2200-2245, `TickAmbushes` :2246-2290), `src/PDv2InstanceScript.h:336-360` (struct comment), `src/PDv2Mgr.cpp:124-125`, `src/PDv2Mgr.h:134`, `src/PDv2Commands.cpp:210`, `conf/mod_procedural_dungeon.conf.dist:640-673`
- Test: `tests/blockplan_harness.cpp` (block-derivation assert)

- [ ] **Step 1: Harness assert first** (engine-free, `PDv2WorldMath.h` only): for blocks `(0,0)`, `(256,256)`, `(263,259)`, `(511,511)` and for the four block-local points `(0.5,0.5)`, `(mid,mid)`, `(66.6,66.6)`, `(33.3,0.5)`: `BlockLocalToWorld` → `WorldToCell` → `gcx / PD_CELLS_PER_BLOCK == bx && gcy / PD_CELLS_PER_BLOCK == by`. Build, run, green (it documents the derivation Task 4 relies on).

- [ ] **Step 2: The cell test.** Replace the distance test in `TickAmbushes`:

```cpp
        WalkGrid const* grid = GetWalkGrid();   // null when the grid never built
        ...
                int gcx = 0, gcy = 0;
                WorldToCell(player->GetPositionX(), player->GetPositionY(), gcx, gcy);
                if (gcx < 0 || gcy < 0 ||
                    gcx / PD_CELLS_PER_BLOCK != ambush.spot.bx ||
                    gcy / PD_CELLS_PER_BLOCK != ambush.spot.by)
                {
                    continue;               // not in this corridor block
                }
                if (grid)
                {
                    GridPoint const cell = grid->LocalFromGlobalCell(gcx, gcy);
                    if (!grid->At(cell.x, cell.y))
                    {
                        continue;           // in the block but off its lane (a jump over the wall band)
                    }
                }
                FireAmbush(ambush, player);
                break;
```

Delete `radius`; keep `ambush.x/y/z` (the centre is still where the spawn offsets start). Rewrite the header comment (`PDv2InstanceScript.h:349-360`): the trigger is the block, not a disc; the lane distances that broke the disc (8.08 / 11.20–11.79 yd).

- [ ] **Step 3: Arm-time log.** In `SpawnAmbushPlan`, per spot after `_ambushes.push_back(ambush)`:

```cpp
            LOG_INFO(PD_LOG, "PDv2: instance {} armed segment {} at block ({},{}) chunk {} centre ({:.1f},{:.1f})",
                     instance->GetInstanceId(), ambush.spot.segment, ambush.spot.bx, ambush.spot.by,
                     plan.blocks[ambush.spot.blockIndex].chunkId, ambush.x, ambush.y);
```

- [ ] **Step 4: Remove the key.** `PDv2Mgr.h:134`, `PDv2Mgr.cpp:124-125`, the `r{:.1f}` part of the info line in `PDv2Commands.cpp:210` (line becomes `barrier 50% | patrol hp 300% | ambush 50% x4`), the conf.dist key and its paragraph (the `Mobs`/`Chance`/`StunSpell` text stays; say the trigger is the corridor block).

- [ ] **Step 5: Worldserver build, stage, md5; harness `--batch 500` green (no pin moves — no draw touched); codestyle; commit** `fix(v2): an ambush fires when a player stands in its corridor block; arm log names the spot; RadiusYd removed`.

---

### Task 5: The Shifting Cache — lock, single use, facing

**Files:**
- Modify: `data/sql/db-world/mod_pdungeon_templates_fix.sql` (DELETE list + rows + a trailing `UPDATE`), `src/PDv2InstanceScript.cpp:1722`

- [ ] **Step 1: SQL.** In `mod_pdungeon_templates_fix.sql`: add `910030` to the DELETE list; add the row `(910030, 3, 259, 'Shifting Cache', 1, 57, 910030, ''),` under a `-- the loop-room / pocket cache (Round C / C3: lock 57 so the client's Opening cast accepts it)` comment; after the INSERT add

```sql
-- Chest data beyond the INSERT's column list: Data3 = consumable (one loot per spawn),
-- Data2 = restock 0. Without Data3 the cache refilled every tick (Round C research 1.3).
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910030;
```

The two declaring files (`mod_pdungeon_templates.sql:26`, `mod_pdungeon_phase2.sql:20`) are **left untouched** on purpose: the updater re-applies any file whose bytes change, and a re-applied `mod_pdungeon_templates.sql` runs its wide `DELETE 910000–910099` ahead of an unchanged fix file — the fix file is the one place that runs last on every database. (Write this reason into the file's header comment and into the spec's C3 line in Task 6.)

- [ ] **Step 2: Facing.** `SummonGameObject(GO_CHEST, x, y, z, 4.712389f, 0.0f, 0.0f, 0.0f, 0.0f, 0)` with the comment `// -pi/2: "90 Grad nach rechts" (T2 2026-09-08); WoW orientation is counter-clockwise`. Verify by reading `WorldObject::SummonGameObject` in the core that `ang` is what sets the rotation when the four quaternion parts are zero (cite the line in the report).

- [ ] **Step 3: Read-only verification** of today's row (`SELECT entry, type, Data0, Data1, Data2, Data3 FROM acore_world.gameobject_template WHERE entry = 910030;` → 0/910030/0/0 today) and of the loot rows (three, valid). Build the worldserver (the .cpp changed), stage, md5. Commit `fix(v2): the Shifting Cache has a lock, is single-use and faces the room`.

---

### Task 6: Docs, gates, operator document, vault

**Files:**
- Modify: `CLAUDE.md` (walk-grid row: supercover; ambush row: block trigger), `README.md` (ambush sentence), `docs/superpowers/specs/2026-09-08-pdv2-round-c-design.md` (C3 line: fix file only, with the reason; §3 config keys already says RadiusYd removed)
- Create: `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde29.md` (German prose, English identifiers; the Round-28 document is the template: state table, start sequence, one section per Round-C item as they land — here C1, C2, C3 with *Erwartet* / *Falls doch* / *Rückmeldung*)
- Vault (`share-public` `main`, one commit): `forgotten-land/15-host-migration-log.md` MIG-017 (title → "Round A + B + C", the C1–C3 commits, `RadiusYd` removed from the key list, the 910030 fix), `12-server-todo.md` Round C row (C1–C3 T1), `claude_log.md` END; `python python_scripts/build_host_runbook.py --check` 0 errors.

- [ ] **Step 1: Gates on a fresh harness** (`--batch 500`, `--decor-batch 3000`, `--roomcap 3000`), worldserver build + stage + md5 (from the branch tip), codestyle.
- [ ] **Step 2: Docs + operator document + vault as listed.** runde29 §C1: the patroller walks the lane, hugs corners, after a pull returns by walking (never flies); *Falls doch*: `.gps` at the moment it leaves the floor + the segment number from `.pdungeon v2 info`. §C2: with `V2.Ambush.Chance = 100` in the conf (say so: the operator sets it for the test and resets it) every segment arms; the arm log lines name the blocks; walking into that block fires; *Falls doch*: the `sprang the ambush` line missing while standing in the named block. §C3: the cache opens once, faces the room's centre.
- [ ] **Step 3: Commit** module `docs: Round C C1-C3 rows, spec C3 note` and vault `docs(pdv2): Round C C1-C3 - patrol, ambush trigger, cache; MIG-017, queue, log`.

---

## Self-review

- Spec coverage: C1.1 → Task 1; C1.2–C1.4 → Task 2; C1.5 → Tasks 1 + 3; C2.1–C2.3 → Task 4; C3.1–C3.3 → Task 5; §3 (RadiusYd removed) → Task 4; §4 T1 gates → every task, Task 6.
- Types: `GridLineWalkable(WalkGrid const&, GridPoint, GridPoint)` unchanged; `SampledLineWalkable`/`LegacyLineWalkable`/`GridFromMask` harness-only; `REJOIN_MAX_CELLS` local to the AI file.
- Placeholders: the three new pin values and the md5s come from runs.
