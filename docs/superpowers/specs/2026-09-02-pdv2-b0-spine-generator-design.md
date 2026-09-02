# PDv2 Round B / B0 — the spine generator (design addendum)

**Date** 2026-09-02 · **Branch** `claude/pdv2-round-b-0cf92ad4` · **Status** approved design, not implemented

Addendum to `2026-09-01-pdv2-content-expansion-design.md` §3 "B0 · The spine generator". That
section states the intent and the operator's decisions; this document settles how the chain is
built out of `PDBlockPlan`, what the harness shows and pins, and what the engine glue persists.
Nothing here re-opens a decision taken there. Two gaps in it were decided with the operator on
2026-09-02 and are recorded in §0.

Evidence tiers as used across this project: **T0** written · **T1** verified offline on this box ·
**T2** run in a real client by the operator.

---

## 0. Decisions taken 2026-09-02 (operator)

1. **Shortcuts are segment-local.** A shortcut leaves a pocket room and lands on a *later* chain
   room **of the same segment** that is **not a boss room**. The parent spec's "a later chain room"
   would let a shortcut land behind a boss room, and B4's barrier could then be walked around
   through the pocket. Every boss room stays a cut vertex of the block graph; the validator and the
   harness both prove it (§6, §7).
2. **`V2.Branches` is persisted per account as `gen_branches`.** A layout is stored as its seed plus
   its generation inputs and regenerated on login; a generator input that is read live from the
   conf would reshape every stored dungeon the day the operator tunes it and trip the
   "PD_LAYOUT_VERSION should have been bumped" error path. New additive column, new SQL file (§5).

Two corrections to the parent spec, measured on `main` `5776b17`:

- **`PlacedBlock::depth` is read by nobody outside the planner.** No engine file, the decor planner
  and the HUD included, reads it (`grep depth src/ src/generator/PDv2*`: only Lil' Bro's unrelated
  `splitDepth`). Block-BFS depth also counts corridor blocks, so it can never equal a room index.
  B0 therefore adds an explicit `chainIndex` and leaves `depth` alone.
- **The batch never runs the field the engine runs.** `MakeCfg` pins `fieldBlocks = 8`
  (`tests/blockplan_harness.cpp:89`) while `PDv2Mgr::GeneratePlan` shrinks the field to
  `min(8, GameFieldBlocksForRooms(rooms))` — 3×3 at one or two rooms, 5×5 at the live default of
  five. A chain packs tighter than an MST, so B0 adds a sweep at engine field sizes (§7.3).

---

## 1. Vocabulary

| Term | Meaning |
|---|---|
| **chain** | The ordered list of spine rooms. Index 0 is the entrance, the last index is always a boss room. `chainLen` = its length |
| **chain index** | Position of a spine room in the chain; `-1` for corridors and pocket rooms |
| **segment** *k* | The chain rooms strictly after boss *k−1* (or after the entrance) up to and including boss *k*, plus the pocket rooms hanging off them. B3/B4 count kills per segment |
| **pocket** | A branch room: exactly one room hanging off a spine room through its own corridor, dead-ending unless it carries a shortcut. Pocket rooms are `BlockRole::Room` and count toward the barrier (operator decision) |
| **shortcut** | A second corridor from a pocket room to a later spine room of the same segment |
| **stub** | The Phase-2 chest dead end (`CorridorDeadEnd`), unchanged |
| **step** | The Manhattan distance between two consecutive room cells, 2 or 3, i.e. one or two corridor blocks |

---

## 2. Room budget (arithmetic, no draw)

```
total    = max(2, rooms + bossRooms)                    // today's `wanted`
pockets  = min(V2.Branches, total / 3, (rooms - 1) / 2) // integer division
chainLen = total - pockets
boss k (k = 1..N) sits at chain index (2 * k * (chainLen - 1) + N) / (2 * N)   // round half up
```

`(rooms − 1) / 2` is the host clamp: a pocket hangs off a spine room that is neither the entrance
nor a boss, one pocket per host, so `pockets ≤ chainLen − 1 − bossRooms`. It also guarantees
`chainLen − 1 ≥ N`, which is what makes the boss positions distinct and ≥ 1.

| rooms + bosses | pockets | chain | boss indices | reads as |
|---|---|---|---|---|
| 1 + 1 | 0 | `E B` | 1 | boss rush |
| 2 + 1 | 0 | `E R B` | 2 | |
| 3 + 1 | 1 | `E R B` + 1 pocket | 2 | |
| 5 + 1 (live default) | 2 | `E R R B` + 2 pockets | 3 | one path, two side pockets |
| 8 + 1 | 2 | `E R R R R R B` + 2 pockets | 6 | |
| 12 + 2 | 2 | chain 12 + 2 pockets | 6, 11 | segments 1..6, 7..11 |
| 15 + 2 (cap) | 2 | chain 15 + 2 pockets | 7, 14 | segments 1..7, 8..14 |

`V2.Branches` default 2 is a tuning value, not a design constant (parent spec §6).

---

## 3. The chain walk (`ChainRooms` replaces `ScatterRooms`)

Field: `cfg.fieldBlocks` square, cells `(x, y)`, the kit convention (`bx` east, `by` south).
Occupancy is one set of cells (rooms and corridors alike); rooms are additionally kept in order.

1. **Start cell**: `x = UniformInt(0, F−1)`, `y = UniformInt(0, F−1)`. Chain index 0, the entrance.
2. **Each next spine room** is drawn from the candidate list of the current room:
   - a free in-field cell at Manhattan distance 2 or 3 from the current room cell — the diagonal
     `(±1, ±1)` counts as a step of 2 and yields one corner corridor block;
   - `MIN_ROOM_GAP` (2, Manhattan) against **every** room placed so far;
   - **at least one feasible L-route**: x-first or y-first, every interior cell free (not a room,
     not a corridor). This is the rule that makes the layout one path by construction — no
     corridor ever crosses another or passes through a room;
   - **direction bias**: with the previous step vector `p` and the candidate step `s`, a candidate
     with `p · s < 0` (heading back) is dropped, unless nothing else is left.
   Candidates are enumerated in the field's fixed `(y, x)` order, one is drawn with
   `UniformInt(0, n−1)` (no draw when `n == 1`, `PDRandom` contract), and the axis order of its
   L-route is `Chance(50)` **only if both orders are feasible**; a single feasible order costs no
   draw. Committing a candidate claims its room cell and its corridor cells.
3. **Backtracking**: depth-first over chain positions. A failed subtree removes the drawn candidate
   from that position's list and draws again from the remainder; an empty list fails the position
   above. A node budget (`CHAIN_BUDGET`, a compile-time constant, 4000 commits) bounds the search;
   exhausting it fails the attempt. The draw sequence is a deterministic function of the seed,
   whatever path the search takes.
4. **Attempt failure** (budget, or pockets unplaceable in §4) → next `seed + attempt`, `maxTries`
   12 unchanged.

The short step is deliberate (parent spec): every extra corridor block costs manifest bytes and
walking time; the tight field (`GameFieldBlocksForRooms`) is what keeps small dungeons small.

---

## 4. Pockets, shortcuts, stubs, roles

Runs after the chain is complete, in this order, so each part is additive to the stream.

1. **Boss indices** are computed from §2 first — no draw, and the pocket host rule needs them.
2. **Per pocket** (`pockets` times, in order):
   - **host**: drawn among spine rooms that are neither the entrance nor a boss, do not host a
     pocket yet, and have at least one candidate cell under the §3 step rule (distance 2..3, gap 2
     against every room, one feasible L-route). Hosts enumerated by chain index;
     `UniformInt(0, n−1)`. No eligible host → the attempt fails (§3.4).
   - **cell**: drawn from that host's candidate list exactly as a chain step (same ordering, same
     axis-order rule). No direction bias for pockets.
   - **shortcut**: `Chance(cfg.loopChancePct)`. On success the target list is every chain room *j*
     with `host < j < nextBoss(host)` (strictly before the segment's boss) that has a feasible
     L-route from the pocket room (interior free, distance ≥ 2 is implied by the gap rule);
     `UniformInt(0, n−1)` over chain-index order, then the route is committed with the same
     axis-order rule. No feasible target → the pocket stays a dead end; the `Chance` draw is still
     consumed and no target draw happens.
   The pocket room gets `chainIndex −1`, `branchOf = host`, `shortcutTo = j` or `−1`.
3. **Stubs**: the existing dead-end pass, unchanged code, unchanged position (after every routing
   draw). A stub cell is free by construction, so it never closes a cycle.
4. **Roles**: entrance = chain index 0; `RoomBoss` = the §2 indices; every other room `Room`;
   corridors by socket count as today. `plan.bossIndex` = the last chain room (the end of the
   dungeon); `plan.entranceIndex` = chain 0. `depth` keeps its block-BFS meaning and is filled by
   the existing BFS.
5. **Materialisation** in the ordered `(y, x)` map as today; **alts** drawn last, unchanged.

---

## 5. Data model and engine glue

**`src/generator/PDBlockPlan.h`**

```cpp
struct BlockCfg   { ... int branches = 2; ... };          // V2.Branches
struct PlacedBlock
{
    ...
    int chainIndex = -1;   // spine rooms 0..chainLen-1, else -1
    int branchOf   = -1;   // pocket rooms: host's chain index, else -1
    int shortcutTo = -1;   // pocket rooms with a shortcut: target chain index, else -1
};
```

Plus two header-level helpers for the later parts of the round, both pure reads of the plan:
`int ChainLength(BlockPlan const&)` and `int SegmentOf(BlockPlan const&, PlacedBlock const&)`
(the boss number *k* whose segment the room belongs to; pockets inherit their host's segment).
B1 (altar every fifth chain index), B3 (segment denominators) and B4 (patrol route) key off these
three fields and nothing else.

**`ValidateBlockPlan`** additionally refuses: chain indices not exactly `0..chainLen−1` once each;
entrance not chain 0; last chain room not a boss; boss count ≠ `bossRooms`; a pocket whose host is
the entrance, a boss, or not a spine room; two pockets on one host; a `shortcutTo` that is a boss,
not later than the host, or in another segment; and the **boss cut property**: removing boss block
*k* from the block graph must leave every spine room with a chain index above *k*, and every
pocket whose host is above *k*, unreachable from the entrance. All of it runs in the engine on
every generation, so a bypassable boss can never be shipped.

**Draw-order comment** in `GenerateBlockPlan` rewritten to the §3/§4 sequence in the same commit.

**Manifest**: format untouched (`B;bx;by;chunkId;0;mask`), emitter untouched. The content moves,
so one real manifest goes through `49_pd_compose_blocks.py` again before merge.

**`PD_LAYOUT_VERSION` 2 → 3**, once for the whole round (B2's alt-count change rides the same
bump), with the v3 paragraph beside the v2 one in `PDv2Mgr.h`.

**Config**: `ProceduralDungeon.V2.Branches` (default 2) → `PDv2Config::branches` → `BlockCfg::branches`
in `GeneratePlan`. Documented in `conf/mod_procedural_dungeon.conf.dist` next to `LoopChance`, whose
comment is rewritten to the new meaning (shortcut chance per pocket); the key is added explicitly to
the deployed `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf` at deploy time (a
`.conf` key change is host-relevant → MIG entry with the round's deploy).

**Persistence**: new file `data/sql/db-characters/mod_pdungeon_account_branches.sql`, the
guarded-ALTER shape of `mod_pdungeon_account_difficulty.sql`: `gen_branches TINYINT UNSIGNED NOT
NULL DEFAULT 0 AFTER gen_loop_pct`. No heal: every existing row is stamped `layout_version 2` and is
rejected at load anyway. `SavePlanToDB` writes it in the INSERT and the ON DUPLICATE KEY UPDATE list
beside `gen_loop_pct`; `LoadPlanFromDB` reads it into `cfg.branches`. `.pdungeon v2 info` prints
`branches {} | loop {}%` on its config line, so the ritual sees the value the server runs with.

**Not in B0**: the instance script keeps its plan-order room list and its 12 yd spawn circle; the
HUD map still sends `R` for a pocket; no kit, walk-mask, anchor or prop changes; no new startup
table. B0 is deployable on its own: rebuilt worldserver + restart (the updater applies the column),
the client needs nothing new.

---

## 6. What becomes visible

- `pdblock --path <seed> [rooms]` prints the chain first, then the existing walk-grid A* from the
  entrance to `bossIndex` (now the whole spine):

  ```
  chain (4 rooms, 1 boss): E#0 (256,258) -> R#1 (258,258) -> R#2 (258,261) -> B#3 (261,261)
  pockets: R#1 + pocket (256,260) [shortcut -> R#2]   R#2 + pocket (260,259) [dead end]
  segment 1: chain 1..3, pockets 2, boss B#3
  ```

- `pdblock <seed>` adds the same `chain:` line above its ASCII dump.
- `AsciiBlockDump`: pocket rooms as lowercase `r`; `E`, `R`, `B`, `D` and the corridor glyphs
  unchanged.

---

## 7. Harness

### 7.1 `--batch` invariants (new, per seed)

1. Rooms found = `rooms + bossRooms`; chain rooms + pocket rooms = that total; pockets = §2.
2. Entrance is chain 0; the last chain room is a boss; `bossIndex` is that room; boss indices are
   exactly the §2 formula.
3. Consecutive chain rooms are joined by a corridor run in which every corridor block has exactly
   two sockets that lead to non-stub blocks; every further socket leads to a stub
   (`CorridorDeadEnd` neighbour). No other junction exists.
4. **Boss cut property**, re-derived in the harness independently of the validator (the
   `EdgesAgree` pattern): removing boss block *k* leaves no spine room above *k* and no pocket
   hosted above *k* reachable from the entrance.
5. Every pocket hosts on a `Room` spine block, one pocket per host; every shortcut target is a
   non-boss chain room strictly between the host and the segment's boss.
6. Determinism, manifest ≤ 2048 B, CRC round trip, walk-grid connectivity, approach policy, world
   math — all unchanged.

`RunBossRoomChecks` (`blockplan_harness.cpp:1320`) loses its "N deepest by depth" assertions and
gains the chain-position ones; `RunPhase2Checks` (stubs, alts non-vacuity) and
`RunThemeParityChecks` stay as they are — the new fields are theme-independent by construction.

### 7.2 Non-vacuity over the sample

At least one seed with a pocket, at least one with a shortcut, at least one with a stub, at least
one alt of each family — or a draw is dead code.

### 7.3 Engine-field sweep (new)

For `rooms = 1..15`: `fieldBlocks = min(8, GameFieldBlocksForRooms(rooms))`,
`bossRooms = GameBossRooms(max(0, rooms − 3))`, `branches = 2`, over the batch's seed count:
**zero generation failures**, every plan validates, the largest manifest printed. This is the
configuration space the engine actually runs; today's batch (always 8×8) never covered it. If a
row fails, the lever is `GameFieldBlocksForRooms` (cells per room 4 → 5), a game-math change the
existing field-size checks pin; the room cap is the lever for the 8×8 rows only.

### 7.4 Room cap

`pdblock --roomcap 3000` re-measured before any deploy and recorded in the `PD_GAME_ROOMS_CAP_MEASURED`
comment. If 15 + 2 no longer packs on 8×8, the constant drops (parent spec: the cap is the lever,
never the step).

### 7.5 Pins

| Pin | Fate |
|---|---|
| Layout freeze `571 B / E;85fc0e4c` (`RunLayoutFreezeCheck`) | re-pinned under `PD_LAYOUT_VERSION` 3, captured by running |
| `PD_DECOR_PLAN_PIN`, `PD_CRITTER_PLAN_PIN` | move (both generate the seed-12345 layout); re-captured after the chain lands, before any decor change |
| `PD_SPAWN_DRAW_PIN`, `PD_SPAWN_DRAW_NOBOSS_PIN` | unchanged: fixed inputs, no layout involved |
| **new `PD_CHAIN_PIN`** | seed 12345, 5 rooms: chain cells in order, then `host>pocket>shortcut` triples, serialised; notices a draw-order move even when the manifest happens to match |
| `PD_GAME_ROOMS_CAP_MEASURED` | re-measured (§7.4) |

Every pin is captured by running the harness and reading the failure message, never by reasoning
about what it should be.

---

## 8. Acceptance for B0 (T1, before B1 starts)

- Fresh `pdblock.exe` (binary newer than every source, proven by timestamp), `--batch 500`,
  `--decor-batch 3000`, `--roomcap 3000`: 0 failures, cap recorded.
- One real manifest through the oracle: `pdblock --manifest 297397130 <file> 13 256 256 2 2` then
  `python 49_pd_compose_blocks.py --manifest <file>` — ALL PASS.
- `--path` on three seeds reads as one path with pockets by eye.
- Worldserver rebuilt on this box; `.pdungeon v2 info` line shows `branches` (owed to the operator's
  restart, T2 later with the round).

The in-game look of B0 (operator, optional before B1–B4): the layout reads as one path with side
pockets; every boss room is entered through exactly one corridor. The round's operator document
carries it as its first section.

---

## 9. Risks and their levers

| Risk | Lever |
|---|---|
| 17 rooms stop packing on 8×8 under the free-route rule | `PD_GAME_ROOMS_CAP_MEASURED` drops (§7.4) |
| Tight fields (3×3, 4×4) fail for small room counts | `GameFieldBlocksForRooms` cells per room 4 → 5 (§7.3) |
| Manifest grows with the extra corridor blocks | measured by the batch; 1900 B budget, ~1470 B today |
| Pocket placement fails after a good chain | attempt retry (`maxTries`); if the sweep shows it, pockets move into the DFS |

---

## 10. Files touched by B0

- `src/generator/PDBlockPlan.h/.cpp` — the generator, validator, dump.
- `src/PDv2Mgr.h/.cpp`, `src/PDv2Commands.cpp` — config key, `branches` plumbing, persistence, version 3, info line.
- `data/sql/db-characters/mod_pdungeon_account_branches.sql` — new.
- `conf/mod_procedural_dungeon.conf.dist` — `V2.Branches`, `V2.LoopChance` comment.
- `tests/blockplan_harness.cpp` — `--path`, invariants, sweep, pins.
- `CLAUDE.md` (planner row), `README.md` (a PDv2 note under the v1 pipeline line).
- Vault, same commit as the merge: `procedural-dungeon/01` planner paragraph, `12-server-todo.md`
  row, `claude_log.md` at the end, MIG entry with the round's deploy.

---

## 11. As built (2026-09-03, after the final review)

Everything above is the approved design and stands. This section records where the shipped code
says something more specific, or something else, than §2–§9 — B1–B4 are written from this document,
so the code wins here and the text above is history.

**The info line prints `pockets {} | shortcut {}%`,** not §5's `branches {} | loop {}%`. Same two
values, named after what they do; `tools/pd_testlauf_runde28.md` and the operator ritual quote the
code's wording.

**The host clamp is `(total − 1 − N) / 2` with `N = max(1, bossRooms)`,** not §2's `(rooms − 1) / 2`.
The two agree wherever `bossRooms ≥ 1`; they differ at `bossRooms = 0`, which is reachable from
`ProceduralDungeon.V2.BossRooms` for an account with no row and where the planner still seats one
boss. `PocketCountFor` and the harness row `{5, 0, 2}` both carry the implemented one.

**Pockets are seated inside the chain search, not after it** (§4 said "runs after the chain is
complete"). A spine the pockets do not fit around is backtracked rather than failing the attempt, so
§3.3's "no eligible host → the attempt fails" is now "the search unwinds and tries another spine".
That is what makes the live default (5 rooms, 2 pockets) reliable; measured, and the reason the
draw-order comment lists the pockets under item 3 of the same search.

**A placed stub never hosts another stub.** §4.3's "unchanged code" is no longer true: before Round B
the layout was full of junctions anyway, so the stub pass could chain stubs. It cannot now — a stub
on a stub would give the block they hang off a third socket to a non-stub block and leave the middle
stub with two, which is exactly the junction the spine rules out.

**The validator proves the corridors, not just the declared fields** (§5 listed the field-level rules
only). `ValidateBlockPlan` additionally enforces, from the sockets, on every generation:

- **junction rule** — a corridor block that is not a `CorridorDeadEnd` has exactly two sockets
  leading to non-stub blocks;
- **spine adjacency** — consecutive chain rooms are joined by exactly one corridor run;
- **pocket physics** — the rooms a pocket's corridors actually reach are exactly its declared host
  (once) and, if it declares one, its shortcut target (once);
- **chain length** — `chainLen == max(2, rooms + bossRooms) − pockets`;
- every interior spine position that is not a boss carries `BlockRole::Room`.

Without these a pocket labelled into segment *k* while physically hanging off a room behind boss *k*
passed both cut floods, and B3 would have counted its spawns into a barrier the player cannot clear
yet.

**The field follows the TOTAL room count.** `GeneratePlan` sizes it
`min(V2.FieldBlocks, GameFieldBlocksForRooms(rooms + bossRooms))`. Sizing it from `cfg.rooms` alone
handed a dlvl-30 account that picks two rooms a 3×3 field for six rooms — a field that provably
cannot hold the plan. §7.3's sweep now walks the engine's real space (`dlvl ∈ {0, 10, 20, 30}` ×
room choice `∈ {1, 2, 3, 4, cap(dlvl)}`) rather than the `rooms = 3 + dlvl` diagonal.

**B4's entry-edge rule.** The stub pass enumerates every occupied cell, so a **boss room may carry a
stub doorway** and present two or three doorways in all (spine in, spine out, stub). §8's in-game
line "every boss room is entered through exactly one corridor" therefore reads "exactly one *non-stub*
corridor". B4 must pick the entry edge as *the socket whose corridor run leads to chain room k−1*
(equivalently: whose flood with the boss excluded reaches it) — never "the boss room's only socket".
`chainIndex` makes that a ten-line helper; it belongs beside `SegmentOf` so B4 and B5 share it.

**Measured shortcut yield: 11 of 500 layouts, 2.2 %,** at the live default (5 rooms, 2 pockets,
`V2.LoopChance` 15). Far below what the key reads as, because a pocket only gets a target list when
one exists inside its segment. The operator decision on whether to raise it is queued
(`12-server-todo.md`, "DECISION: PDv2 shortcut yield") and is owed **before the round deploys**:
every candidate fix moves the draw stream, and so do the optional stub-host filter and the
backward-candidate lever for the chain search. All of them are free under the single
`PD_LAYOUT_VERSION` 2 → 3 bump this round already spends — and cost another forced reroll of every
stored layout once the round ships. Batch them or drop them.
