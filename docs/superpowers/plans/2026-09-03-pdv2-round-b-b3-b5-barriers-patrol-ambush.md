# PDv2 Round B / B3–B5 — barriers, patrol, ambush Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A sealed portcullis before every boss room that opens at 50 % of its segment's planned mobs (pockets and loop rooms count, patrol and ambush mobs do not); one elite patroller per boss segment walking the spine out of combat and surviving an evade; a 50 %-per-segment ambush in a spine corridor that stuns the player for 2 s and spawns four mobs around them.

**Architecture:** Shared plumbing first (Task 1): the corridor-run walk becomes a public engine-free helper, `PDv2MobData` gains `countsForRun`/`isPatrol`, `OnMobDied` branches on it, the instance keeps per-room segment bookkeeping and a grid setter, the eight config keys exist. Then the barrier (Task 2), the patrol (Task 3), the ambush plan (Task 4, engine-free + pinned) and its engine half (Task 5), then docs/gates/restage/vault (Task 6). No layout draw moves; the patrol and ambush picks use independent seeds through the existing `SelectSpawns`.

**Tech Stack:** C++17 AzerothCore module (MSVC via PowerShell), one world-SQL row (910059), the `pdblock` harness (new source `src/generator/PDv2AmbushPlan.cpp` → harness build line + cmake re-configure).

**Spec:** `docs/superpowers/specs/2026-09-03-pdv2-b3-b5-barriers-patrol-ambush-design.md`; research `.superpowers/sdd/b3-research.md`, `b4b5-research.md`.

---

## Global Constraints

- The B0b/B1 plan constraints apply verbatim (live checkout, branch `claude/pdv2-round-b-0cf92ad4`, Conventional Commits, `git -c core.autocrlf=false commit`, PowerShell for `cl.exe`/`cmake`, freshness proof, code style script, no server/DB touch, staging only, read-only SELECTs only).
- **Harness build line gains `src\generator\PDv2AmbushPlan.cpp`** (Task 4 onward) and `CLAUDE.md`'s build line is updated in the same commit. **`cmake -S … -B …` re-configure** once for the new `.cpp` (Task 4) before the worldserver build.
- **Determinism:** no draw in the layout stream moves; the layout, decor, critter and chain pins must not move. Patrol picks and ambush picks use `SelectSpawns` with their own seeds (`layoutSeed ^ MIX ^ segment mix`) so the spawn-draw pins do not move either. `BuildAmbushPlan` has its own pin.
- **All new config keys are read live** in `LoadConfig` (a `.reload config` follows them); none is persisted.
- **Every mob the module spawns goes through `SpawnTaggedMob`.** Patrol and ambush mobs carry `countsForRun = false` and `roomIndex = PD_ROOM_NONE`.
- **Client-facing notices in English.**

---

## File Structure

**Created:** `src/generator/PDv2AmbushPlan.h/.cpp` (engine-free ambush plan).

**Modified:** `src/generator/PDBlockPlan.h/.cpp` (public `RunFromSocket`, `SpineRunInto`); `src/PDv2InstanceScript.h/.cpp` (tag fields, `OnMobDied`, bookkeeping, grid setter, barriers, patrols, ambushes); `src/PDv2CreatureAI.h/.cpp` (patrol branch, evade); `src/PDv2Mgr.h/.cpp` (config keys); `src/PDDefines.h` (`PD_ROOM_NONE`); `data/sql/db-world/mod_pdungeon_templates_fix.sql` (row 910059); `conf/mod_procedural_dungeon.conf.dist`; `tests/blockplan_harness.cpp`; `CLAUDE.md`; vault + operator doc (Task 6).

---

### Task 1: Shared plumbing

**Files:**
- Modify: `src/generator/PDBlockPlan.h` (after `IsAltarRoom`), `src/generator/PDBlockPlan.cpp` (the validator's `roomAtEndOf` lambda → shared static + public wrappers)
- Modify: `src/PDDefines.h`, `src/PDv2InstanceScript.h/.cpp`, `src/PDv2Mgr.h/.cpp`, `conf/mod_procedural_dungeon.conf.dist`
- Test: `tests/blockplan_harness.cpp` (`RunChainChecks`: `SpineRunInto` against the harness's own `RoomAtEndOf`)

**Interfaces (produced):**
```cpp
// PDBlockPlan.h
int RunFromSocket(BlockPlan const& plan, size_t from, unsigned bit,
                  std::vector<size_t>* outRun, bool* junction);
unsigned SpineRunInto(BlockPlan const& plan, int chainIndex, std::vector<size_t>* outRun);
// PDDefines.h
uint32 const PD_ROOM_NONE = 0xFFFFFFFFu;
// PDv2MobData
bool countsForRun = true;   bool isPatrol = false;   int patrolGoalCellX = 0;   int patrolGoalCellY = 0;
// PDv2InstanceScript (private)
std::vector<uint16> _roomPlanned;  std::vector<int> _roomSegment;  std::vector<bool> _roomIsBoss;
std::vector<uint32> _segmentPlanned, _segmentKilled;      // index 1..N, [0] unused
void SetCellsWalkable(std::vector<GridPoint> const& cells, bool walkable);
// PDv2Config
int barrierPct = 50; float barrierOrientNS = 0.0f; float barrierOrientEW = 1.5708f;
int patrolHealthMultPct = 300; int ambushChancePct = 50; int ambushMobs = 4; float ambushRadiusYd = 9.0f; uint32 ambushStunSpell = 20170;
```

- [ ] **Step 1: The public run walk**

In `src/generator/PDBlockPlan.cpp`, lift the validator's `roomAtEndOf` lambda into a file-static function in the anonymous namespace:

```cpp
        // The corridor run behind socket `bit` of block `from`: walks corridor
        // blocks, ignores chest stubs, continues straight through a loop
        // attachment (a corridor with three non-stub sockets that is in
        // `attachments`), and returns the index of the first ROOM reached.
        // -1 when the run ends in a stub, in nothing, or forks - `junction`
        // says which. `outRun` collects the corridor blocks walked, in order.
        int WalkRun(BlockPlan const& plan, std::map<std::pair<int, int>, size_t> const& index,
                    std::set<size_t> const& attachments, size_t from, unsigned bit,
                    std::vector<size_t>* outRun, bool& junction)
        {
            junction = false;
            if (outRun) outRun->clear();
            size_t prev = from;
            unsigned entryBit = bit;
            for (size_t steps = 0; steps <= plan.blocks.size(); ++steps)
            {
                int dx = 0, dy = 0;
                StepFor(entryBit, dx, dy);
                auto it = index.find(std::make_pair(plan.blocks[prev].bx + dx, plan.blocks[prev].by + dy));
                if (it == index.end()) return -1;
                size_t const at = it->second;
                PlacedBlock const& b = plan.blocks[at];
                if (b.roomId >= 0) return static_cast<int>(at);
                if (b.role == BlockRole::CorridorDeadEnd) return -1;
                if (outRun) outRun->push_back(at);
                unsigned const cameFrom = OppositeBit(entryBit);
                unsigned next = 0;
                int outs = 0;
                for (unsigned side = 1; side <= SOCKET_W; side <<= 1)
                {
                    if (!(b.socketMask & side) || side == cameFrom) continue;
                    int nx = 0, ny = 0;
                    StepFor(side, nx, ny);
                    auto n = index.find(std::make_pair(b.bx + nx, b.by + ny));
                    if (n != index.end() && plan.blocks[n->second].role == BlockRole::CorridorDeadEnd) continue;
                    ++outs;
                    next = side;
                }
                if (outs != 1)
                {
                    if (attachments.count(at) && (b.socketMask & entryBit) && outs == 2)
                    {
                        next = entryBit;        // straight through the attachment
                    }
                    else
                    {
                        junction = true;
                        return -1;
                    }
                }
                prev = at;
                entryBit = next;
            }
            junction = true;
            return -1;
        }

        // In a validated plan the attachment cells are exactly the corridors
        // with three non-stub sockets; the validator computes its own set from
        // the loop reconstruction, the public wrappers derive it this way.
        std::set<size_t> AttachmentsByDegree(BlockPlan const& plan,
                                             std::map<std::pair<int, int>, size_t> const& index)
        {
            std::set<size_t> out;
            for (size_t i = 0; i < plan.blocks.size(); ++i)
            {
                PlacedBlock const& b = plan.blocks[i];
                if (b.roomId >= 0 || b.role == BlockRole::CorridorDeadEnd) continue;
                int through = 0;
                for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
                {
                    if (!(b.socketMask & bit)) continue;
                    int dx = 0, dy = 0;
                    StepFor(bit, dx, dy);
                    auto it = index.find(std::make_pair(b.bx + dx, b.by + dy));
                    if (it != index.end() && plan.blocks[it->second].role != BlockRole::CorridorDeadEnd) ++through;
                }
                if (through == 3) out.insert(i);
            }
            return out;
        }
```
The validator's lambda body is replaced by a call to `WalkRun(plan, index, attachment, from, bit, nullptr, junction)` (its `attachment` set stays the reconstruction's). Public wrappers (after `AltCountFor` etc.):

```cpp
    int RunFromSocket(BlockPlan const& plan, size_t from, unsigned bit,
                      std::vector<size_t>* outRun, bool* junction)
    {
        std::map<std::pair<int, int>, size_t> index;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
            index[std::make_pair(plan.blocks[i].bx, plan.blocks[i].by)] = i;
        bool j = false;
        int const end = WalkRun(plan, index, AttachmentsByDegree(plan, index), from, bit, outRun, j);
        if (junction) *junction = j;
        return end;
    }

    unsigned SpineRunInto(BlockPlan const& plan, int chainIndex, std::vector<size_t>* outRun)
    {
        if (chainIndex < 1) return 0;
        int into = -1, before = -1;
        for (size_t i = 0; i < plan.blocks.size(); ++i)
        {
            if (plan.blocks[i].chainIndex == chainIndex) into = static_cast<int>(i);
            if (plan.blocks[i].chainIndex == chainIndex - 1) before = static_cast<int>(i);
        }
        if (into < 0 || before < 0) return 0;
        for (unsigned bit = 1; bit <= SOCKET_W; bit <<= 1)
        {
            if (!(plan.blocks[static_cast<size_t>(into)].socketMask & bit)) continue;
            std::vector<size_t> run;
            bool junction = false;
            if (RunFromSocket(plan, static_cast<size_t>(into), bit, &run, &junction) == before && !junction)
            {
                if (outRun)
                {
                    std::reverse(run.begin(), run.end());   // walking order from room i-1 toward room i
                    *outRun = run;
                }
                return bit;
            }
        }
        return 0;
    }
```
Header comments as in the Interfaces block. Harness: in `RunChainChecks` per seed, for `i = 1..wantChain-1`: `SpineRunInto(plan, i, &run)` returns a non-zero bit; every block in `run` is a corridor (`roomId < 0`, not a dead end); `run.size() >= 1`; and the harness's own `RoomAtEndOf(plan, chainBlock[i], bit)` equals `chainBlock[i-1]`.

- [ ] **Step 2: The tag, the sentinel, `OnMobDied`**

`src/PDDefines.h` (in namespace PDungeon, after the enums): `uint32 const PD_ROOM_NONE = 0xFFFFFFFFu;   // roomIndex of a mob that belongs to no room (patrol, ambush)`.

`PDv2MobData` gains, after `splitDepth`:
```cpp
        // Round B. false for the patrol (B4) and the ambush mobs (B5): they
        // fight, scale, split and drop loot like any dungeon mob but move no
        // run counter and no barrier - risk on the road, not progress.
        bool   countsForRun = true;
        // B4: this creature walks the spine out of combat. The goal cell is
        // the far end of its beat in GLOBAL grid cells; the AI plans the
        // route on its first idle tick and reverses it at either end.
        bool   isPatrol = false;
        int    patrolGoalCellX = 0;
        int    patrolGoalCellY = 0;
```
`SpawnTaggedMob` copies all four; `SplitOnDeath`'s proto copies `countsForRun` (children of an uncounted mob are uncounted; `isPatrol` stays false for children). In `OnMobDied`, wrap the counter block:

```cpp
        if (tag->countsForRun)
        {
            ++_run.killed;
            if (tag->isRunBoss && _run.bossKilled < _run.bossTotal) ++_run.bossKilled;
            if (tag->roomIndex < _roomAlive.size() && _roomAlive[tag->roomIndex] > 0)
            {
                if (--_roomAlive[tag->roomIndex] == 0) ++_run.roomsCleared;
            }
            if (tag->roomIndex < _roomSegment.size() && !_roomIsBoss[tag->roomIndex])
            {
                int const seg = _roomSegment[tag->roomIndex];
                if (seg >= 1 && static_cast<size_t>(seg) < _segmentKilled.size())
                {
                    ++_segmentKilled[static_cast<size_t>(seg)];
                    EvaluateBarrier(seg);          // Task 2 defines it; declare now, body `{}` until then
                }
            }
        }
        MarkRunDirty();
```

- [ ] **Step 3: Room bookkeeping and the grid setter**

In `SpawnFromPlan`'s room loop (the one that builds `roomBlocks`/`inputs.rooms`), also record per dense room index: `_roomSegment.push_back(SegmentOf(plan, b)); _roomIsBoss.push_back(b.role == BlockRole::RoomBoss);` (clear both first). After `_run.total = …`: `_roomPlanned = _roomAlive;` and size `_segmentPlanned/_segmentKilled` to `max(1, plan.config.bossRooms) + 1` zeros, then `for r: if (!_roomIsBoss[r] && _roomSegment[r] >= 1) _segmentPlanned[_roomSegment[r]] += _roomPlanned[r];`. Reset all six in the rebuild branch beside `_run = PDv2RunState{}`.

Grid setter in the instance:
```cpp
    void PDv2InstanceScript::SetCellsWalkable(std::vector<GridPoint> const& cells, bool walkable)
    {
        if (!_gridReady) return;
        for (GridPoint const& p : cells)
        {
            if (!_grid.InBounds(p.x, p.y)) continue;
            _grid.cells[static_cast<size_t>(p.y) * _grid.width + p.x] = walkable ? 1 : 0;
        }
    }
```
and rewrite the `GetWalkGrid` comment: built once on first entry; written afterwards ONLY by the instance itself on the map thread (barriers seal and unseal lane cells, Round B); every reader runs on the same thread, so no lock.

- [ ] **Step 4: Config keys**

`PDv2Config` + `LoadConfig` (clamps: `barrierPct` 0..100, `patrolHealthMultPct` ≥ 100, `ambushChancePct` 0..100, `ambushMobs` 0..8, `ambushRadiusYd` ≥ 1) and the conf.dist blocks (one per key, in the V2 section, Round B dated). `.pdungeon v2 info` gains one line: `"pdungeon v2: barrier {}% | patrol hp {}% | ambush {}% x{} r{:.1f}"`.

- [ ] **Step 5: Build, gates, commit**

Harness (PowerShell) → `--batch 500` with the new `SpineRunInto` checks green, no pin moved; `--decor-batch 3000`, `--roomcap 3000` green. Worldserver builds. Code style. Commit `feat(v2): run-walk helpers, uncounted mobs, segment bookkeeping, Round B config keys`.

---

### Task 2: The barrier

**Files:** `data/sql/db-world/mod_pdungeon_templates_fix.sql` (row 910059), `src/PDv2InstanceScript.h/.cpp`.

- [ ] **Step 1: SQL** — DELETE list gains `910059`; INSERT gains
```sql
-- Round B / B3: the boss-room barrier. type 5 GENERIC because that is the
-- only GameObject class measured to block a player on map 760; opened by
-- Delete(), never by state. Display 7482 Vr_Portcullis.m2 spans 16.3 yd at
-- scale 1 (the lane is 16.67), so size 1.1 overlaps the flanking wall band.
(910059, 5, 7482, 'Sealed Portcullis', 1.1, 0, 0, ''),
```
(keep the file's trailing `;` on the last row); gap comment updated (910059 no longer a gap). Read-only check: `SELECT entry FROM gameobject_template WHERE entry = 910059` → none; `SELECT entry, name FROM gameobject_template WHERE displayId = 7482 LIMIT 1` (record the stock precedent).

- [ ] **Step 2: State and spawn**

```cpp
        struct Barrier
        {
            int segment = 0;                    // k; the boss room is chain b_k
            ObjectGuid guid;                    // the portcullis, empty once opened
            std::vector<GridPoint> cells;       // the four lane cells it seals
            float x = 0.0f, y = 0.0f;           // for the hint radius
            bool open = false;
            bool hinted = false;
        };
        std::vector<Barrier> _barriers;
        void SpawnBarriers(BlockPlan const& plan);
        void EvaluateBarrier(int segment);
        void OpenBarrier(Barrier& barrier, char const* why);
        void HintBarriers();                     // 1 Hz
```
`SpawnBarriers` (called after `SpawnAltars`, needs `_gridReady`): for `k = 1..N` (`N = max(1, cfg.bossRooms)`), `bossChain = BossChainIndex(ChainLength(plan), plan.config.bossRooms, k)`; `bit = SpineRunInto(plan, bossChain, &run)`; if `bit == 0 || run.empty()` → LOG_WARN, skip. Boss block `b` (chainIndex == bossChain), neighbour `n = plan.blocks[run.back()]`. Lane cells: boss side per bit N→(0,3),(0,4) S→(7,3),(7,4) W→(3,0),(4,0) E→(3,7),(4,7); neighbour side = the opposite edge's cells; convert `(row, col)` of block `(bx, by)` with `_grid.LocalFromGlobalCell(bx * PD_CELLS_PER_BLOCK + col, by * PD_CELLS_PER_BLOCK + row)`. Position: per bit `(u, v)` = N `(4.1667, 33.3333)`, S `(62.5, 33.3333)`, W `(33.3333, 4.1667)`, E `(33.3333, 62.5)`; orientation `cfg.barrierOrientNS` for N/S, `cfg.barrierOrientEW` for E/W; `SummonGameObject(GO_BARRIER, x, y, z, o, 0,0,0,0, 0)` → `_decorGuids` + `Barrier{k, guid, cells, x, y}`; `SetCellsWalkable(cells, false)`. Then `EvaluateBarrier(k)` once (a zero-planned segment opens immediately). Log `placed {} barrier(s)`.

`EvaluateBarrier(k)`: find the barrier of segment k, closed; `planned = _segmentPlanned[k]`, `killed = _segmentKilled[k]`; if `planned == 0 || killed * 100 >= planned * cfg.barrierPct` → `OpenBarrier`. `OpenBarrier`: delete the GO (find in `_decorGuids`, `go->Delete()`, erase the guid), `SetCellsWalkable(cells, true)`, `open = true`, notice to every player in the instance `"The barrier to the boss falls."`, LOG_INFO. `HintBarriers` (1 Hz, after `RespawnPending`): for each closed, un-hinted barrier, any player within 12 yd (2D) → notice `"The barrier holds - {} more of this segment's foes must fall."` with `needed = (planned * pct + 99) / 100 - killed` (≥ 1), `hinted = true`.

Rebuild: `_barriers.clear()` beside the other clears (the GOs go with `_decorGuids`; the grid is rebuilt).

- [ ] **Step 3: Build, commit** — worldserver builds; harness regression `--batch 500`; code style; commit `feat(v2): sealed portcullis before every boss room, opens at the segment threshold`.

---

### Task 3: The patrol

**Files:** `src/PDv2InstanceScript.h/.cpp` (`SpawnPatrols`), `src/PDv2CreatureAI.h/.cpp`.

- [ ] **Step 1: Spawn** — `SpawnPatrols(plan)` after `SpawnBarriers`: per segment k: `bit = SpineRunInto(plan, bossChain, &run)`; spawn block = `run.back()` (the corridor before the boss), spawn point its centre; goal = the centre cell of chain room `b_{k-1}` (0 for k = 1) in GLOBAL cells (`WorldToCell` of `BlockToWorld(bx, by, mid, mid)`). Pick: `SpawnSelectInputs in; in.rooms = { RoomRequest{0, false} }; in.spawnsPerRoom = 1; in.bossRoomAdds = 0; in.casterPct = 0; in.bandMin/unlockedDlvl/affixPct` as `SpawnFromPlan` sets them; `sPDv2PackMgr->SelectSpawns(plan.effectiveSeed ^ PD_PATROL_SEED_MIX ^ (uint32(k) * 0x9E3779B1u), in, out)` (`PD_PATROL_SEED_MIX = 0x9A7201EDu` in `PDv2InstanceScript.cpp`'s anonymous namespace); entry = `out[0].picks[0].entry` (fallback `PLACEHOLDER_CREATURE`). `proto.role = PACK_ROLE_MELEE; proto.roomIndex = PD_ROOM_NONE; proto.countsForRun = false; proto.isPatrol = true; proto.patrolGoalCellX/Y = goal;` `Creature* c = SpawnTaggedMob(entry, proto, x, y, z)`; then `SetDungeonHealth(c, c->GetMaxHealth() * cfg.patrolHealthMultPct / 100); c->SetFullHealth(); c->SetWalk(true);`. Log `placed {} patroller(s)`.

- [ ] **Step 2: The AI**

`PDv2MobAI` gains `void EnterEvadeMode(EvadeReason why) override;`, `void UpdatePatrol(uint32 diff);`, `void ResumePatrol();`, members `std::vector<GridPoint> _patrolRoute; bool _patrolActive = false; uint32 _patrolTimer = 0;`.

```cpp
    void PDv2MobAI::UpdatePatrol(uint32 diff)
    {
        WalkGrid const* grid = _instance ? _instance->GetWalkGrid() : nullptr;
        if (!grid || !_mob || !_mob->isPatrol || !me->IsAlive()) return;
        if (_patrolTimer > diff) { _patrolTimer -= diff; return; }
        _patrolTimer = REPATH_INTERVAL_MS;
        // Stale run: an evade or a knockback ended the motion but not the flags.
        if (_followingPath && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        {
            StopWaypointRun(false);
            _patrolActive = false;
        }
        if (_followingPath) return;
        if (!_patrolActive)
        {
            GridPoint const here = CellOf(*grid, me->GetPositionX(), me->GetPositionY());
            GridPoint const goal = grid->LocalFromGlobalCell(_mob->patrolGoalCellX, _mob->patrolGoalCellY);
            GridPoint a, b;
            if (!NearestWalkable(*grid, here.x, here.y, SNAP_RADIUS_CELLS, a) ||
                !NearestWalkable(*grid, goal.x, goal.y, SNAP_RADIUS_CELLS, b)) return;
            std::vector<GridPoint> path;
            if (!FindGridPath(*grid, a, b, path)) return;
            SimplifyGridPath(*grid, path);
            if (path.size() < 2) return;
            _patrolRoute = path;
            _patrolActive = true;
        }
        me->SetWalk(true);
        std::vector<GridPoint> run = _patrolRoute;
        StartWaypointRun(std::move(run), *grid);
    }

    void PDv2MobAI::ResumePatrol()
    {
        _patrolActive = false;              // re-plan from wherever we stand
        _patrolTimer = 0;
        StopWaypointRun(false);
    }

    void PDv2MobAI::EnterEvadeMode(EvadeReason why)
    {
        ScriptedAI::EnterEvadeMode(why);
        if (_mob && _mob->isPatrol)
        {
            // Home is wherever the patrol stands: the core's walk home is a
            // straight line, and on this map a straight line crosses the void.
            me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveIdle();
            ResumePatrol();
        }
    }
```
`MovementInform`: at the end of a run, `if (_mob && _mob->isPatrol) { std::reverse(_patrolRoute.begin(), _patrolRoute.end()); StopWaypointRun(false); me->SetHomePosition(current); _patrolTimer = 0; return; }` before the existing `StopWaypointRun(true)`. At every waypoint reached by a patroller, `me->SetHomePosition(current position)`. `JustReachedHome`: also `ResumePatrol()` for patrollers. `JustEngagedWith`: `me->SetWalk(false)` and `StopWaypointRun(false)` for patrollers (the chase takes over). `UpdateAI`'s idle branch: `if (_mob && _mob->isPatrol) UpdatePatrol(diff); UpdateProximityAggro(diff); return;`.

- [ ] **Step 3: Build, commit** — worldserver builds; code style; commit `feat(v2): one elite patroller per boss segment walks the spine and survives an evade`.

---

### Task 4: The ambush plan (engine-free) and its pin

**Files:** Create `src/generator/PDv2AmbushPlan.h/.cpp`; modify `tests/blockplan_harness.cpp`, `CLAUDE.md` (build line).

```cpp
// PDv2AmbushPlan.h
#include "PDBlockPlan.h"
#include <cstdint>
#include <vector>
namespace PDungeon
{
    uint32_t const PD_AMBUSH_SEED_MIX = 0xA3B05EEDu;
    struct AmbushSpot { size_t blockIndex = 0; int bx = 0; int by = 0; int segment = 0; };
    // Per boss segment k = 1..N, in order: Chance(chancePct) is ALWAYS drawn;
    // on a hit, one corridor block of the segment's spine runs (the runs into
    // chain rooms b_{k-1}+1 .. b_k, in that order, blocks in walking order)
    // is drawn with UniformInt. Runs contain neither stubs nor loop strips.
    // Its own stream (layoutSeed ^ PD_AMBUSH_SEED_MIX), so it never moves the
    // layout, the decor, the critter or the spawn draws.
    std::vector<AmbushSpot> BuildAmbushPlan(BlockPlan const& plan, int chancePct, uint32_t layoutSeed);
}
```
```cpp
// PDv2AmbushPlan.cpp
    std::vector<AmbushSpot> BuildAmbushPlan(BlockPlan const& plan, int chancePct, uint32_t layoutSeed)
    {
        std::vector<AmbushSpot> out;
        int const len = ChainLength(plan);
        if (len < 2) return out;
        int const bosses = plan.config.bossRooms > 0 ? plan.config.bossRooms : 1;
        PDRandom rng(layoutSeed ^ PD_AMBUSH_SEED_MIX);
        int prevBoss = 0;
        for (int k = 1; k <= bosses; ++k)
        {
            int const bossAt = BossChainIndex(len, plan.config.bossRooms, k);
            bool const wants = rng.Chance(chancePct);
            std::vector<size_t> candidates;
            for (int i = prevBoss + 1; i <= bossAt; ++i)
            {
                std::vector<size_t> run;
                if (SpineRunInto(plan, i, &run) != 0)
                    candidates.insert(candidates.end(), run.begin(), run.end());
            }
            if (wants && !candidates.empty())
            {
                size_t const pick = candidates[static_cast<size_t>(
                    rng.UniformInt(0, static_cast<int>(candidates.size()) - 1))];
                AmbushSpot spot;
                spot.blockIndex = pick;
                spot.bx = plan.blocks[pick].bx;
                spot.by = plan.blocks[pick].by;
                spot.segment = k;
                out.push_back(spot);
            }
            prevBoss = bossAt;
        }
        return out;
    }
```
Harness: `PD_AMBUSH_PLAN_PIN` (seed 12345, 5 rooms, chance 100; string `"%d,%d,%d;"` per spot = bx, by, segment; captured by running), `CheckAmbushPlanPinned`, plus per-seed checks in `RunChainChecks`: with chance 100 every segment that has ≥ 1 run block gets exactly one spot, each spot is a corridor block on a run of its segment (re-derive with the harness `RoomAtEndOf`), with chance 0 no spot; determinism (two calls equal). Batch line: `"ambushes: %d of %d segments"` at the default 50.

Build line (harness + `CLAUDE.md`) gains the new `.cpp`. Commit `feat(generator): ambush plan - one spine corridor per boss segment on its own stream`.

---

### Task 5: The ambush (engine)

**Files:** `src/PDv2InstanceScript.h/.cpp`.

```cpp
        struct Ambush
        {
            AmbushSpot spot;
            float x = 0.0f, y = 0.0f, z = 0.0f;   // block centre
            unsigned socketMask = 0;               // corridor axis
            std::vector<SpawnPick> picks;
            bool armed = true;
        };
        std::vector<Ambush> _ambushes;
        uint32 _ambushTimer = 0;
        void SpawnAmbushPlan(BlockPlan const& plan);   // build guard, after SpawnPatrols
        void TickAmbushes();                            // every AMBUSH_SCAN_MS
        void FireAmbush(Ambush& ambush, Player* player);
```
`SpawnAmbushPlan`: `BuildAmbushPlan(plan, cfg.ambushChancePct, plan.effectiveSeed)`; per spot: centre via `BlockToWorld(bx, by, mid, mid)`; picks via `SelectSpawns(plan.effectiveSeed ^ PD_AMBUSH_SEED_MIX ^ (uint32(segment) * 0x9E3779B1u), in, out)` with one non-boss room and `spawnsPerRoom = cfg.ambushMobs`. `Update`: `_ambushTimer` accumulates `diff`; every `AMBUSH_SCAN_MS = 250` → `TickAmbushes()`: for each armed ambush, for each player in the instance (alive, in world): `GetExactDist2d` ≤ `cfg.ambushRadiusYd` → `FireAmbush`. `FireAmbush`: `armed = false`; if `cfg.ambushStunSpell` → `player->AddAura(cfg.ambushStunSpell, player)`; notice `"Ambush!"`; offsets along the corridor axis (N|S mask → along u) `{(+6,+4),(+6,-4),(-6,+4),(-6,-4)}` yards from the PLAYER's position expressed in block-local u/v (convert player → u/v with the inverse of `BlockLocalToWorld`: `u = PD_WORLD_MAX_YD - x - by*BLOCK`, `v = PD_WORLD_MAX_YD - y - bx*BLOCK`) or, simpler, world offsets of the same magnitudes along world X (u axis) and Y (v axis); grid-veto each (`WorldToCell` → `LocalFromGlobalCell` → `At`), fallback = the player's position; `proto.role = pick.role; proto.casterSpellId = pick.casterSpellId; proto.roomIndex = PD_ROOM_NONE; proto.countsForRun = false;` `SpawnTaggedMob` then `if (CreatureAI* ai = c->AI()) ai->AttackStart(player);`. Log. Rebuild clears `_ambushes`.

Build, code style, commit `feat(v2): corridor ambushes - 2 s stun and four mobs, 50% per boss segment`.

---

### Task 6: Docs, gates, restage, operator document, vault

- `CLAUDE.md` rows (ambush plan, patrol/barrier/ambush in the instance script and the AI); the harness build line already updated in Task 4.
- Full gates on a fresh harness (`--batch 500`, `--decor-batch 3000`, `--roomcap 3000`), worldserver re-configured + built + staged (md5).
- `tools/pd_testlauf_runde28.md`: sections B3 (portcullis across the boss doorway, hint notice, opens at the threshold; *Falls doch* the orientation keys `V2.Barrier.OrientNS/EW` and the type-5 measurement), B4 (patroller walking the corridor, aggro, evade → resumes), B5 (ambush: stun 2 s, four mobs, once per corridor), the eight conf keys with defaults, the new md5.
- Vault (`main`): `06-custom-ids.md` 910059 registered; MIG-017 bullet (commits, SQL row, conf keys); `claude_log.md` END; queue row; runbook check 0 errors; one commit. No push.

---

## Self-review

- Spec coverage: B3 decisions 1–9 → Tasks 1–2; B4 1–5 → Tasks 1, 3; B5 1–4 → Tasks 4–5; §2 config → Task 1; §3 acceptance → Task 6.
- Types: `RunFromSocket`/`SpineRunInto` signatures used identically in the harness, the barrier, the patrol and `BuildAmbushPlan`; `countsForRun`/`isPatrol`/`patrolGoalCell*` copied in `SpawnTaggedMob` and `SplitOnDeath`; `PD_ROOM_NONE` used by patrol and ambush; `AmbushSpot` shared by plan and instance.
- Placeholders: the ambush pin is captured by running; the md5 at execution; `EvaluateBarrier` is declared in Task 1 with an empty body and defined in Task 2.
