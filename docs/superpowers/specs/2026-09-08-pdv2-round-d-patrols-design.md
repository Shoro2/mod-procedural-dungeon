# PDv2 Round D — patrols that walk like guards: lane-following planner, one patrol per corridor, formations; no spawn hover

**Date** 2026-09-08 · **Branch** `claude/pdv2-round-c-0cf92ad4` (continues on the Round C branch; tip `55a09ce`) ·
**Status** **implemented T1 2026-09-08, commits `e75fc3e` (D3) / `616d59d` (D1) / `1dc9179` (D2)** on that
branch (tip now `1dc9179`); decided with the operator in chat on 2026-09-08 (three choices below).
**T2 CLOSED 2026-09-09**: the operator run of 2026-09-08 22:34 passed D2 and D3 and failed §3 on the
off-centre passages (Round D2 fixed that), and the run of 2026-09-09 passed everything
(`C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde30.md`, verdict *"ja top passt alles"*).

Source: the operator's retest after the patrol fix `55a09ce`: *"pats laufen von raum zu raum, aber in einer
geraden linie durch ecken von häusern und objekte hindurch. können wir da irgendwie pathfinding
reinbringen/hindernisse umlaufen? und wir brauchen ein pat zwischen jedem raum … ab stufe 50 2 mobs und ab 75
3 mobs pro pat (hintereinander laufen damit wir nur ein path pro pat brauchen). manche mobs schweben beim
spawn, nach einem pull oder reset sind sie normal auf dem boden."* Decisions taken: route **corridor only**
(doorway to doorway, never into a room); formation **single file by follow**; planner **axis-aligned legs that
avoid walls and props**.

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client.

---

## 0. Facts (measured 2026-09-08)

- A patrol beat today = `FindGridPath` (uniform-cost 4-neighbour A*, `PDv2WalkGrid.cpp:121`) then
  `SimplifyGridPath` (longest supercover-approved straight segments) → legs are diagonals across rooms and
  through corridor mouths. The supercover test knows cells, not the facades that intrude up to 6.1 yd into
  mouths (audit 104) nor the module's own props standing on walkable cells. Hence "durch Ecken von Häusern".
- Map 760 has no server terrain: no mmaps, no vmaps — the core cannot path; everything walkable is the
  module's 8.33 yd cell grid. Legs are straight `MovePoint` splines at `floorZ` (since `55a09ce` launched from
  `UpdateAI`, one per arrival).
- One patroller per boss segment walks the run into the boss room back to the previous boss/entrance
  (`SpawnPatrols`); the kit publishes `"role":"patrol"` anchors on corridor arms (unused).
- `SpawnTaggedMob` (and the Chromie/critter summons) call `SetDisableGravity(true)`; `Creature::Update` strips
  it again for templates whose `CreatureMovementData` is Flight = None (`Creature.cpp:3474`, research
  `c-debug-patrol.md` H5). Until that first movement update the client shows the levitation — the spawn hover
  the operator sees; after a pull or evade the flag is gone and the mob stands.
- `_run.difficulty` (1–100) is known when the instance spawns.

---

## 1. Decisions

### D1 — the patrol planner (engine-free)

| # | Decision |
|---|---|
| 1 | New `FindPatrolPath(WalkGrid const&, GridPoint from, GridPoint to, std::vector<uint8_t> const* propCells, std::vector<GridPoint>& out)` in `src/generator/PDv2WalkGrid.{h,cpp}`: A* over states (cell, incoming direction) with integer costs `PatrolCost{ step 10, turn 30, wallAdjacent 20, propCell 60 }` — a step costs 10, a change of direction +30, entering a cell with a non-walkable 4-neighbour +20, entering a cell flagged in `propCells` (same indexing as `grid.cells`, 1 = a prop stands there) +60. Fixed neighbour order (N, E, S, W), Manhattan×10 heuristic, deterministic. `propCells` may be null. |
| 2 | `MergeCollinear(std::vector<GridPoint>&)` keeps the endpoints and every turn — legs are axis-aligned, cell centre to cell centre. No diagonal simplification for patrols. |
| 3 | The chase keeps `PlanApproach` (combat approach may cut corners; not the operator's complaint). |
| 4 | Harness: a hand grid proves the turn penalty picks the straight lane over a zigzag of equal length, the wall penalty keeps a room crossing off the edge cells, a prop cell is avoided when an alternative exists and crossed when it is the only way; the operator seed's beats are pinned (`PD_OPERATOR_PATROL_PIN` re-captured: waypoints, cost) with every leg asserted axis-aligned and on walkable cells. |

### D2 — one patrol per corridor, single file, size by difficulty

| # | Decision |
|---|---|
| 1 | For every chain room i = 1..chainLen−1 the corridor run into it (`SpineRunInto(plan, i)`) gets ONE patrol. Its beat runs **corridor only**: from the doorway lane cell of the run's first block that faces room i−1 (`LaneCellsForSocket` on the socket towards that room, first cell) to the doorway lane cell of the run's last block that faces room i, then back — the patrol never enters a room. A one-block run gets the block's two doorway cells. Boss segments keep their barriers; a sealed portcullis shortens nothing (the goal cell is inside the corridor). |
| 2 | Size by difficulty: 1 creature, 2 when `_run.difficulty >= V2.Patrol.Size2Diff` (default 50), 3 when `>= V2.Patrol.Size3Diff` (default 75). All picks melee (`casterPct 0`) from one draw per corridor with `spawnsPerRoom = size`, seed `effectiveSeed ^ PD_PATROL_SEED_MIX ^ (i * PD_SEGMENT_SEED_STEP)`; pick 0 is the **leader**, the others **followers**. Every member: `countsForRun = false`, `roomIndex = PD_ROOM_NONE`, `isPatrol = true`, HP × `V2.Patrol.HealthMult`, no affix. |
| 3 | The leader plans and walks the beat (D1 planner, the rejoin/evade rules of Round C unchanged). A follower carries `patrolLeader` (GUID) and, out of combat, `MoveFollow(leader, V2.Patrol.FollowDistYd × k, π)` (k = its rank, default 3.0 yd) — single file behind the leader, one path per patrol. In combat every member fights on its own AI; a follower whose leader has a victim attacks that victim. After an evade a follower re-follows; a follower whose leader is dead holds its position (the patrol dissolves). |
| 4 | Spawn: the leader on the beat's start cell centre; follower k on the k-th cell along the planned beat (so the line stands in the corridor from the first second), grid-vetoed. |
| 5 | The old per-segment patrol code is replaced, `patrolGoalCellX/Y` become the beat's start/goal cells; `.pdungeon v2 patrol` lists every member with role (leader/follower k); the arm log says "placed N patrol(s), M creature(s) for N corridor run(s)". |

### D3 — no spawn hover

| # | Decision |
|---|---|
| 1 | Remove `SetDisableGravity(true)` from every module summon (`SpawnTaggedMob`, Chromie, critters). The core strips it anyway on the first movement update; until then it is the hover. A comment at `SpawnTaggedMob` records why it is gone and cites `Creature::Update`. Nothing else depends on it (the fall catcher handles players). |

---

## 2. Config keys (read live)

New: `V2.Patrol.Size2Diff` 50 (1..100) · `V2.Patrol.Size3Diff` 75 (1..100) · `V2.Patrol.FollowDistYd` 3.0.
Unchanged: `V2.Patrol.HealthMult` 300, `V2.Patrol.Debug` 0.

## 3. Acceptance

T1: harness — the D1 hand-grid cases, the operator beats re-pinned (axis-aligned, walkable), `--batch 500`,
`--decor-batch 3000`, `--roomcap 3000` green; worldserver builds; codestyle. T2 (`tools/pd_testlauf_runde30.md`):
every corridor between two rooms has a patrol; it walks the lane centre, turns at junctions, never cuts a
house corner or a prop; on difficulty ≥ 50 two mobs walk in single file, ≥ 75 three; a pulled patrol fights
together and re-forms afterwards; no mob hovers at spawn.
