# PDv2 Round B / B0b — loop rooms instead of shortcuts (design addendum)

**Date** 2026-09-03 · **Branch** `claude/pdv2-round-b-0cf92ad4` · **Status** approved design, not implemented

Revision of `2026-09-02-pdv2-b0-spine-generator-design.md` after the operator's instruction of
2026-09-03. The B0 spine, its pockets, the boss cut property and the harness discipline stay; the
**shortcut** mechanism is removed and a new side structure, the **loop room**, is added.

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client by
the operator.

---

## 0. The operator's decisions (2026-09-03)

Verbatim intent: *"es soll keine Abkürzungen geben, eher Alternativknoten … zusätzliche Räume, die
parallel abseits der Hauptroute eine zusätzliche Abzweigung in einem Gang sind. Diese sollen einen
Weg hin und einen zweiten Weg zurück haben. Dort ist dann sowas wie eine Truhe usw. … so eine Chance
von 33 % pro 5er/Boss-Segment."*

Recorded as:

1. **No shortcuts.** The pocket → later-spine-room corridor and its draw are removed. `V2.LoopChance`
   is retired; the persisted `gen_loop_pct` column keeps its bytes and carries the new detour chance.
2. **Loop rooms** ("Alternativknoten", *detours* in code): an extra room beside a straight corridor
   run of the spine, entered from the run and left back into the same run two cells further along —
   out to the side, corner, room (entrance and exit on opposite sides), corner, back. **33 % chance
   per boss segment**, one loop room per segment at most.
3. **Pockets stay** as built in B0 (dead-end room off a spine room, `V2.Branches`), minus the
   shortcut draw.
4. **Loop rooms are additional** to the room budget (the spine always has `rooms + bossRooms`
   rooms; a segment with a loop room has one room more).
5. **Contents**: a normal room pack (the room counts toward B3's barrier like any branch room) plus a
   **chest** on the kit's `chest` anchor (placed by B1, which brings the typed anchor decoder).

---

## 1. Geometry (block cells)

A **detour step** replaces one ordinary chain step: spine room `i−1` at `P`, spine room `i` at
`P + 4d` (straight, Manhattan 4, direction `d`), side `s ⟂ d` (left or right):

```
main run:   P (room i-1) -> P+d -> P+2d -> P+3d -> P+4d (room i)
strip:                     S1 = P+d+s   R = P+2d+s   S2 = P+3d+s

sockets:  P: +d           P+d: -d +d +s      P+2d: -d +d        P+3d: -d +d +s      P+4d: -d
          S1: -s +d       R: -d +d           S2: -d -s
```

Read along the operator's sketch with `d` = north and `s` = east: out of the run to the right at
`P+d`, left around the corner at `S1`, through the loop room `R` (entrance south, exit north),
left again at `S2`, back into the run at `P+3d`. `P+2d` keeps a wall toward `R`.

Rules: all seven cells free and inside the field; `R` and `P+4d` obey `MIN_ROOM_GAP` against every
room placed so far (`R` is Manhattan 3 from both spine rooms); the direction bias applies to `d`
like any step. `R` is `BlockRole::Room` with `detourOf = i` (the spine room the run leads into),
`chainIndex −1`, `branchOf −1`. `SegmentOf(R)` = the segment of room `i`.

---

## 2. Draw order (the contract; every stored seed rerolls once more — accepted, `PD_LAYOUT_VERSION` stays 3, the server is not live)

1. start cell: `x`, `y`
2. **per boss segment `k = 1..N`, in order: `Chance(detourChancePct)`** → `wantDetour[k]`
   (`Chance` at 0/100 draws nothing)
3. each chain step: when the step leads into a room of a segment that wants a detour and has none
   yet, the candidate list is the **detour candidates** (destination cell in `(y, x)` order, side
   left before right, each side its own candidate — no axis coin); when that list is empty (or all
   its subtrees fail), the ordinary candidates follow; a candidate index is drawn only when the list
   has more than one entry. Backtracking re-draws from the shrunken list as in B0.
4. pockets, exactly as in B0 **without** the shortcut `Chance` and target draws
5. dead-end stubs (unchanged; a stub may hang off a detour corridor or the loop room)
6. visual alternates (unchanged, last)

A segment whose detour never fits keeps none — 33 % is a chance, not a quota; the harness prints the
yield.

---

## 3. Data model and config

- `PlacedBlock`: `shortcutTo` **removed**; new `int detourOf = -1` (loop rooms: the chain index of
  the spine room their run leads into).
- `BlockCfg`: `loopChancePct` → **`detourChancePct`**, default **33**. `branches` unchanged.
- `PDv2Config::detourChancePct` from **`ProceduralDungeon.V2.DetourChance`** (default 33, clamped
  0..100). `V2.LoopChance` is no longer read; the dist documents the rename. Persisted in the
  existing `gen_loop_pct` column (comment in `SavePlanToDB`/`LoadPlanFromDB` and in
  `mod_pdungeon_account.sql`'s successor file is not needed — a one-line comment in code suffices).
- Boot line / `.pdungeon v2 info`: `pockets {} | detour {}%`.
- `AsciiBlockDump`: loop rooms as `o`; pockets stay `r`.
- `ChainSummary`: `pockets:` line without shortcut text; new `loops:` line
  `R#i run + loop room (bx,by) [segment k]`.
- `PD_CHAIN_PIN` format: chain cells `|` pockets `host>x,y;` `|` loops `into>x,y;`.

---

## 4. Validator (engine, every generation) and harness

The B0 rules stay; the junction rule and the corridor-run walk learn the one sanctioned fork:

- **Detour physics**, reconstructed from `R`'s sockets: exactly two, opposite (`−d`, `+d`);
  `S1 = R − d` is a corridor with sockets exactly `{+d, t}` and `S2 = R + d` with `{−d, t}` for the
  same `t ⟂ d`; `M1 = S1 + t` and `M2 = S2 + t` are corridors with sockets exactly `{−d, +d, −t}`;
  `M1 + d = M2 − d` is a straight corridor with sockets exactly `{−d, +d}`; `M1 − d` is the spine
  room `detourOf − 1` and `M2 + d` the spine room `detourOf`. `M1`/`M2` are collected as the
  **attachment cells**.
- **Junction rule**: a corridor block has exactly two non-stub sockets **unless it is an attachment
  cell**, which has exactly three.
- **Corridor-run walk** (`roomAtEndOf`): at an attachment cell entered along the run, continue
  straight (the socket opposite the entry); entered from the strip side, it is the end of the
  strip — the spine-adjacency walk from room `i−1` therefore still reaches room `i` exactly once,
  and a pocket's walk still ends at its host.
- **Pocket physics**: host once, nothing else (no shortcut branch).
- **Boss cut**: unchanged; a detour never crosses a room.
- **Count**: loop rooms ≤ the number of segments, one per segment; rooms total =
  `rooms + bossRooms + loopRooms`.

Harness (`RunChainChecks`, independent re-derivation): the geometry above from sockets; `sawDetour`
non-vacuity on a `detourPct 100` combo; the `detourPct 0` combo yields none; yield line in the batch
summary (`segments with a loop room: n of m`); the engine-field sweep and the room cap re-measured
(a detour costs 3 extra cells).

---

## 5. Engine and docs follow-ups

- `SpawnFromPlan` treats a loop room as a room (it is one); B1 adds the chest on its `chest` anchor.
- B3's segment denominator: loop rooms via `SegmentOf` (`detourOf`), pockets via `branchOf`.
- B4's patrol walks the spine; the attachment cells are on its route (the walk rule above).
- Vault: the "shortcut yield" decision row is removed; MIG-017 bullet, `claude_log.md` entry,
  operator document `runde28` updated (boot/info lines, the look: loop rooms, no shortcuts).
- The B0 spec's §0.1 (segment-local shortcuts) is superseded by this document; note it there.
