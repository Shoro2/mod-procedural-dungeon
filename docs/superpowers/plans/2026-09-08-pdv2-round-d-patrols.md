# PDv2 Round D — patrols (planner, per corridor, formations) and the spawn hover — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Patrols walk the lane centre with axis-aligned legs that avoid walls and props, one patrol per corridor between two rooms, 1/2/3 creatures in single file by difficulty, and no mob hovers at spawn.

**Architecture:** D1 is a new engine-free planner beside the existing A* (`FindPatrolPath` with turn/wall/prop costs + `MergeCollinear`), pinned in the harness. D2 replaces the per-segment patrol loop with a per-corridor loop that spawns a leader plus followers; the leader uses the D1 planner, followers use the core's follow generator. D3 deletes the gravity flag at the summon sites.

**Tech Stack:** C++17 module + `pdblock.exe` harness (MSVC via PowerShell), worldserver build.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-d-patrols-design.md`. Research: `.superpowers/sdd/c-debug-patrol.md` (H5 gravity, the movement facts), `c-fix-patrol2-report.md` (the leg runner as it is now).

---

## Global Constraints

Same as the Round C plans (branch `claude/pdv2-round-c-0cf92ad4`, Conventional Commits + trailer, LF, `git add` by path, no push; determinism contract in `src/generator` — integer costs, fixed neighbour order, no unordered containers; harness build line in the module `CLAUDE.md`, pins captured by running, gates `--batch 500` / `--decor-batch 3000` / `--roomcap 3000`; worldserver via PowerShell cmake, staged, the running server never touched by a task; codestyle; read-only DB).

---

### Task 1 (D3): no gravity flag at spawn

**Files:** `src/PDv2InstanceScript.cpp` (`SpawnTaggedMob` ~:1225-1238, Chromie ~:884, critters ~:2198), `src/PDv2CreatureAI.cpp:1502` (comment)

- [ ] Remove the three `SetDisableGravity(true)` calls; at `SpawnTaggedMob` leave a comment: the flag was stripped by `Creature::Update` on the first movement update anyway (`Creature.cpp:3474`, Flight = None) and until then it showed as a hover — the mobs stand at `floorZ` without it. Fix the AI comment at `:1502`. Build, stage, md5, codestyle, LF. Commit `fix(v2): no gravity flag at spawn - the hover was the flag, the core dropped it anyway`.

### Task 2 (D1): the patrol planner

**Files:** `src/generator/PDv2WalkGrid.h/.cpp`, `tests/blockplan_harness.cpp`

**Interfaces (produces):**
```cpp
struct PatrolCost { int step = 10; int turn = 30; int wallAdjacent = 20; int propCell = 60; };
// A* over (cell, incoming direction). propCells: same indexing as grid.cells, 1 = a prop stands there; may be null.
bool FindPatrolPath(WalkGrid const& grid, GridPoint from, GridPoint to,
                    std::vector<uint8_t> const* propCells, std::vector<GridPoint>& outPath,
                    PatrolCost const& cost = PatrolCost{});
void MergeCollinear(std::vector<GridPoint>& path);   // endpoints + turns only
```

- [ ] **Step 1: harness first.** Hand grids (8×8 or 16×8): (a) a 2-wide lane with a zigzag of equal Manhattan length: the path must run straight along one column (turn penalty); (b) a 6×6 room with doorways on opposite sides offset by two cells: the crossing must use interior cells, not the edge row (wall penalty), and be L-shaped (one turn); (c) a lane cell flagged as prop with the other lane cell free → the path takes the free one; (d) both lane cells flagged → the path still exists (prop is a cost, not a wall). `MergeCollinear` on a hand path keeps exactly the turns. Each case a named `Check`. Build, run: all FAIL (functions absent).
- [ ] **Step 2: implement.** A* with `gScore` per state (cell × 5 incoming dirs), priority queue keyed by f = g + Manhattan×step, fixed neighbour order N/E/S/W, tie-break on state index; costs per the spec; reconstruct via `cameFrom` states. `MergeCollinear`: drop interior points whose direction equals the previous. Header comment: why (the operator's "durch Ecken von Häusern"), the cost meanings, determinism.
- [ ] **Step 3: operator beats.** In the harness's operator block replace `FindGridPath`+`SimplifyGridPath` by `FindPatrolPath`+`MergeCollinear` for the beats (propCells null there), assert every leg axis-aligned and every cell walkable, pin `PD_OPERATOR_PATROL_PIN` re-captured as `k:waypoints:cells:cost;`. Gates green. Commit `feat(generator): patrol planner - axis-aligned legs that avoid walls and props`.

### Task 3 (D2): one patrol per corridor, single file, size by difficulty

**Files:** `src/PDv2InstanceScript.h/.cpp` (`SpawnPatrols` rewritten; `_propCells` built after `SpawnDecor`/`SpawnKitProps`), `src/PDv2CreatureAI.h/.cpp` (leader planning via `FindPatrolPath`+`MergeCollinear`; follower behaviour), `src/PDv2Mgr.h/.cpp` + `conf/mod_procedural_dungeon.conf.dist` (three keys), `src/PDv2Commands.cpp` (`.pdungeon v2 patrol` roles; info line `patrol hp 300% x1/2/3@50/75`), `src/PDDefines.h` if a constant is needed

- [ ] **Step 1: prop cells.** After `SpawnKitProps` the instance walks `_decorGuids` (GameObjects standing in the run) and marks each GO's cell in `_propCells` (a `std::vector<uint8_t>` sized like the grid's `cells`; barriers excluded — their lane cells are flipped anyway). Exposed via `PropCells()` for the AI.
- [ ] **Step 2: `SpawnPatrols` per corridor.** For i = 1..chainLen−1: `SpineRunInto(plan, i, &run)` (skip with a LOG_WARN when empty); start = first lane cell of `LaneCellsForSocket(run.front(), socket towards room i−1)` (the socket is the one whose `RunFromSocket` reaches chain room i−1 — reuse the helper the barrier code uses to find the entry edge; for the run's first block it is the socket facing away from the run), goal = first lane cell of `LaneCellsForSocket(run.back(), the entry socket into room i)` (= `SpineRunInto`'s returned bit mirrored via `OppositeSocket` — verify against the barrier code); size = `1 + (diff >= size2) + (diff >= size3)`; one draw with `spawnsPerRoom = size`; leader tag `{isPatrol, patrolStartCellX/Y = start global cell, patrolGoalCellX/Y = goal global cell}`; follower tag `{isPatrol, patrolLeader = leader GUID, patrolRank = k}`; spawn the leader at the start cell centre, follower k at the k-th cell of `FindPatrolPath(start → goal)` (planned once here; if the path is shorter than k, the last cell); grid-vetoed; HP mult; `SpawnTaggedMob`. Log `placed {} patrol(s), {} creature(s) for {} corridor run(s)`.
- [ ] **Step 3: AI.** Leader: `UpdatePatrol` plans with `FindPatrolPath(here → goal, PropCells())` + `MergeCollinear` (replacing `FindGridPath` + `SimplifyGridPath`); everything else (rejoin, reversal, `_legPending`, evade order) unchanged. Follower: out of combat, if not already following → `me->GetMotionMaster()->MoveFollow(leader, followDist * rank, float(M_PI))`; if the leader has a victim and the follower has none → `AttackStart(victim)`; leader gone/dead → `MoveIdle` and hold. `EnterEvadeMode` for followers: the Round C order (home before base, `Clear` + `StopMoving`), then re-follow on the next tick. `JustEngagedWith` of the leader: followers within 20 yd `AttackStart` the same target (call via the instance's creature list or `_patrolLeader` back-references — keep it simple: followers poll their leader's victim in `UpdateAI`).
- [ ] **Step 4: keys, command, docs.** `V2.Patrol.Size2Diff` 50, `V2.Patrol.Size3Diff` 75, `V2.Patrol.FollowDistYd` 3.0 in `PDv2Mgr` + conf.dist; `.pdungeon v2 patrol` prints `leader`/`follower k` and the beat's start/goal cells; info line. `CLAUDE.md` rows. Build, stage, md5, harness unchanged (no generator draw touched — the patrol pick draw grows to `size` picks: the spawn-draw pins are room draws, unaffected; the harness's operator ambush/patrol pins do not include the pick), codestyle, LF. Commit `feat(v2): one patrol per corridor, single file by difficulty, lane-following beats`.

### Task 4: gates, operator document, vault

- [ ] Fresh harness gates + worldserver from the tip (stage, md5); codestyle. `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde30.md` (new, German prose, structure of runde29: state table, start sequence with the swap, §D1 lane-following legs — the pat walks the lane centre, turns at junctions, no house corner, no prop; §D2 one pat per corridor, `V2.Patrol.Size2Diff`/`Size3Diff` — for the test set the dungeon difficulty to 50 and 75 (the operator's difficulty dial) and expect 2/3 in single file, they fight together and re-form; §D3 no hover at spawn; `V2.Patrol.Debug` + `.pdungeon v2 patrol` as the evidence tools; rollback = `worldserver.exe.pre_roundD_20260908`). Vault (`main` via temp index): MIG-017 (commits, the three keys), queue row (Round D T1), `claude_log.md` END; runbook check.

---

## Self-review

- Spec coverage: D1.1–D1.2 → Task 2; D1.3 → no work; D1.4 → Task 2 Step 1/3; D2.1–D2.5 → Task 3; D3.1 → Task 1; §2 keys → Task 3 Step 4; §3 → Task 4.
- Types: `PatrolCost`, `FindPatrolPath`, `MergeCollinear` as declared; `PropCells()` returns `std::vector<uint8_t> const*`; tag fields `patrolStartCellX/Y`, `patrolGoalCellX/Y`, `patrolLeader` (ObjectGuid), `patrolRank` (uint8).
- Placeholders: pins and md5s from runs.
