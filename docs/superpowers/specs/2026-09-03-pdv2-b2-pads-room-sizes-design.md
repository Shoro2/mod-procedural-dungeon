# PDv2 Round B / B2 — pads, room sizes, anchor spawns (design addendum)

**Date** 2026-09-03 · **Branch** `claude/pdv2-round-b-0cf92ad4` · **Status** approved design, not implemented

Addendum to `2026-09-01-pdv2-content-expansion-design.md` §3 "B1 · Pads" and "B2 · Rooms of
different sizes" (the vault brief numbers these together as **B2**; this document uses the vault
numbering — B1 is the altar). The parent spec states the operator's decisions; this document settles
the implementation choices, corrects what has drifted, and names every gate. Research with `path:line`
anchors: `.superpowers/sdd/b2-research.md` (workbench only, gitignored).

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client by
the operator.

---

## 0. Corrections to the parent spec (measured 2026-09-03)

- **Pads exist only in theme 2 at alt 1** (`48_gen_t1_blockkit.py:252-259`): room/entrance 2×2
  (cells 3–4), boss 4×4 (cells 2–5). 45 of 214 chunks carry one. The pad is a **walk-mask veto plus a
  class promotion, not geometry**: removing it changes zero MCVT bytes but moves `walkMask`, the
  anchors and the surface classes.
- **"The small pads in ordinary rooms keep their fountain" is stale since kit v34**: the 2×2 pad carries
  one centred house; only the boss's 4×4 pad still has the fountain GameObject 910040. The ordinary
  room keeps its pad and house; nothing in this round touches it.
- **A padless boss room would keep its fountain** unless gated: `theme2_placements` calls
  `add_go("fountain", …)` whenever `add_pad_facades(pad)` returns falsy, and an empty footprint returns
  `False` (`48:2531-2532`, `:2127-2128`).
- **The walk-baseline gate fires**: `48:3656-3675` compares every chunk against
  `scripts/backup_pd_20260830/baseline_walk_v20.json` (all 214 ids) and fails the build on any change.
  Round 16's precedent (one re-capture with a written reason) is the sanctioned way through.
- **The harness completeness sweep is wrong for a Room-only alt** (`blockplan_harness.cpp:1356-1372`):
  it bounds the alt loop with `AltCountFor(Room)` but applies it to all three room roles.
- **The server has no typed anchors**: `DecorAnchor` is `{u, v}` and `DecodeAnchorList` scrapes every
  `"u"` in the blob, props included (`PDv2DecorPlan.h:136-140`, `.cpp:401-441`). Spawn placement
  needs the `boss`/`entry`/`spawns[].role` structure the kit already publishes.
- The kit's `entry` anchor is "the nearest walkable cell centre" and exists for every room variant;
  `room_entrance` publishes no spawn anchors at all (`48:746-747`); pockets are `BlockRole::Room` and
  get the plain-room anchor set.

---

## 1. Decisions

| # | Choice | Decision |
|---|---|---|
| 1 | Third alt scope | **`Room` (role 0) only**, both themes: ids `4001–4015` / `14001–14015`, +30 ADTs (217 → 247 files). Boss and entrance stay two alts (the operator asked for an open boss arena, not a small one). |
| 2 | Alt index | **alt 2**; alts 0/1 stay byte-frozen. |
| 3 | Pads | `footprint_cells` returns `set()` for roles 1 (entrance) and 2 (boss) at every alt; the room's 2×2 pad (role 0, alt 1) is untouched. The fountain call is gated on the footprint: `if pad and not add_pad_facades(pad)`. |
| 4 | Small platform | `_small_room_boxes(mask)`: platform half-quads `2·HQ_PER_CELL .. 6·HQ_PER_CELL` (cells 2–5, 33.33 yd) plus the unchanged junction square and arms, so cells 0–1 / 6–7 on a socket axis are bare throats. Walkable interior = cells 2–5 by the 50 % rule. |
| 5 | Facade band | `ROOM_WALK` becomes `platform_band(ridx, alt)`: `(2·HQ_PER_CELL·HALF_QUAD, 6·HQ_PER_CELL·HALF_QUAD)` for `(0, 2)`, the old `(ROOM_LO·HALF_QUAD, ROOM_HI·HALF_QUAD)` otherwise. **Throat flanks stay bare band in the first build**; 105/104 measure them, and a facade rule for throats is a follow-up only if the bare-line figure regresses past the v35 baseline. |
| 6 | Walk baseline | One re-capture for the whole round: `baseline_walk_v24.json` (reason "Round B: entrance/boss pads removed (theme 2 alt 1); third Room alt 33 yd"), written by script 48 itself under a new `--recapture-walk-baseline "<reason>"` flag; the default constant points at v24 afterwards. |
| 7 | `KIT_VERSION` | 23 → **24** (walk masks, anchors, props and the chunk-id set all move). |
| 8 | Typed anchors | New header-only `src/generator/PDv2SpawnAnchors.h`: `SpawnAnchor {u, v, role}`, `RoomAnchors {entry, hasBoss, boss, spawns}`, `DecodeRoomAnchors(json, out)` (a scanner like `DecodeAnchorList`, reading `"entry"`, `"boss"` and the `"spawns"` array with each entry's `"role"`), and the pure placement `PlanSpawnPoints(anchors, bossRoom, count)`. The flat `DecorAnchor` list and its decor clearance stay exactly as they are. |
| 9 | Placement | Boss room: point 0 = the `boss` anchor (the first pick IS the boss, `PDv2InstanceScript.cpp:935-941`), points 1.. = `spawns` in kit order. Room / pocket: `spawns` in kit order. **Overflow** (more picks than anchors, only reachable by raising `V2.SpawnsPerRoom` / `BossRoomAdds`): the legacy 12-yd circle points, angle by pick index over count, in order. Every point is **grid-vetoed** on the instance; a non-walkable point falls back to the `entry` anchor (always walkable by construction, and the only safe centre on a 2×2-pad room). |
| 10 | `AltCountFor(Room)` | 2 → 3; same number of draws (`UniformInt` range 2 vs 3 both take one `_rng()` per value), so only Room looks re-roll. `PD_LAYOUT_VERSION` 3 already covers it. |
| 11 | Deploy | One kit round `t1b-v36` (+ `FLStream.ini` ladder + `KitDir` flip, client closed, cold restart) **and** a worldserver restart (regenerated `mod_pdungeon_chunk_meta.sql`, kitVersion 24) — one step more than the v29–v35 rounds; MIG-017 gains the bullet. Rollback: `KitDir` back to v35 + the v35 `chunk_meta.sql` + restart. |

Not in this round: a facade rule for the throat flanks (measure first), decor density per room size,
the module's `SOCKET_TRACK` excluding only cell 4 while the kit excludes cells 3–4 (pre-existing).

---

## 2. What moves in the kit (script 48), exactly

1. `KIT_VERSION = 24`.
2. `ALT_COUNT = {0: 3, 1: 2, 2: 2, 3: 2}`.
3. `footprint_cells`: `if theme != 2 or alt != 1 or ridx != 0: return set()`; the 2×2 set for role 0
   only; the docstring loses "the silo tower arena".
4. `_small_room_boxes(mask)` beside `_blob_room_boxes`; `floor_boxes_v2`: `if alt == 2: if ridx == 0:
   return _small_room_boxes(mask)` before the `alt == 1` handling, everything else unchanged.
5. `platform_band(ridx, alt)` beside `ROOM_LO`/`ROOM_HI`; `theme2_placements` uses it where it used
   `ROOM_WALK` (both `add_facades` calls). The `[2] Design` print and `kit_meta.json` gain
   `roomPlatformSmallYd`.
6. `if pad and not add_pad_facades(pad): add_go("fountain", …)`.
7. `[6b]` reads `WALK_BASELINE_FILE = "baseline_walk_v24.json"`; with `--recapture-walk-baseline
   "<reason>"` the mismatch is reported instead of failing and, after every other check passed, the
   new baseline is written from `rows` (`{"recorded", "reason", "supersedes", "kitVersion",
   "corridorWidthYd", "chunks": [{"chunkId", "walkMask"}]}`, sorted by chunkId, LF, UTF-8). The
   Phase-1 drift constants (theme 1, 56/11) are untouched — the theme-1 alt-2 chunks are new ids,
   which the Phase-1 contract allows ("growth is legal").
8. Everything downstream is class-driven and needs no code change: scripts 51 and 52 re-texture and
   re-map the 30 padless chunks and the 30 new ones from `surfaceClasses`/`sockets`.

Expected measurable effects: `131xx` 40 → 44 walkable cells, `132xx` 28 → 44 (mask 15); `1x2xx`
publishes the default anchor set (boss at the centre, 4 elites, chest); `131xx`'s `entry` moves to
(29.17, 29.17); `4xxx`/`14xxx` have 16 platform cells plus arms; 247 files, two builds byte-identical.

---

## 3. What moves in the module

- `AltCountFor(BlockRole::Room)` → 3 with the comment mirroring `ALT_COUNT`.
- Harness: the completeness sweep bounds each role by its own `AltCountFor(role)`; a `sawAlt2Room`
  non-vacuity; the freeze/decor/critter pins re-captured (chunkIds and masks move); `PD_CHAIN_PIN`
  should hold (cells only); typed-anchor checks against the shipped `kit_meta.json`: every `room`
  chunk has ≥ 5 spawns, every `room_boss` chunk a `boss` and ≥ 2 spawns, every room-role chunk an
  `entry` on a walkable cell; a **placement pin** for `PlanSpawnPoints` on chunks 12015 (count 5),
  12215 (count 3), 4015 (count 5) and 13015 (count 5), captured by running.
- `PDv2Mgr`: `_chunkRoomAnchors` decoded from the same row beside `_chunkAnchors`;
  `RoomAnchorsFor(chunkId)`; the `.pdungeon v2 info` walk-mask line also prints how many chunks
  carry typed anchors.
- `PDv2InstanceScript::SpawnFromPlan`: `PlanSpawnPoints` + grid veto + `entry` fallback replace the
  circle; `SPAWN_SPREAD_YD` survives only inside the overflow path. `SelectSpawns` and its draw
  order are **not touched** (the spawn pins prove it).
- `mod_pdungeon_chunk_meta.sql` is regenerated by script 48 (never hand-edited).

---

## 4. Gates (T1) before the kit ships

Kit: two full chain runs byte-identical **247/247**; `51 --dry-run` all PASS; `52` ALL PASS; oracle
`49` ALL CHECKS PASS on the operator layout `pdblock --manifest 297397130 … 13 256 256 2 2`;
`flstream_tests.exe` 497/0 with both `byte-identical to the oracle` lines on the default staging;
audits on the composed operator layout: 105 bare street line ≤ 0.8 % overall **with the new room's
corners and throats reported separately**, 104 max intrusion ≤ 6.1 yd, 103 boundary step ≈ 1.9 (no
TEX-DIFF edge), 102 ≤ 2 bare cheeks (pads no longer skipped for boss/entrance — the count may move,
report it), 99 = 31 known `house_b` holes.
Module: `pdblock --batch 500`, `--decor-batch 3000`, `--roomcap 3000` green on a fresh binary
against the **new** staging SQL/`kit_meta.json`; codestyle clean; worldserver built.

In game (T2, the round's operator document): boss rooms are open 50×50 with nothing in the centre;
the start room is clear; small (33 yd) and standard (50 yd) rooms are distinguishable; mobs stand on
the kit's spawn spots, the boss in the room's centre; nothing hovers past a platform edge.

---

## 5. Risks and levers

| Risk | Lever |
|---|---|
| Facades on the 33-yd band overlap or block the throats (104/105 regress) | throat rule, or a smaller `reserve` for alt 2; measured before any change |
| The 30 padless chunks lose corner coverage (102 counted the pads as flat terrain) | `add_corner_facades` no longer skips pad noses there; re-measure, then the v34 corner menu |
| Anchor overflow at raised conf values | the legacy circle path, grid-vetoed |
| A stale `chunk_meta` on the server after the kit flip | `.pdungeon v2 info` prints kitVersion 24 and the typed-anchor count; the operator doc's start section asserts both |
