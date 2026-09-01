# PDv2 content expansion — design

**Date** 2026-09-01 · **Branch** `claude/pdv2-expansion-9583c476` · **Status** approved design, not implemented

Twelve operator asks across three subsystems, cut into three deployable rounds. Each round is
built, deployed and in-game accepted on its own before the next one starts, so a defect is
attributable to one round's changes rather than to twelve.

Architecture and history live in the vault: `share-public/docs/World of Warcraft/procedural-dungeon/`
— [01](../../../../../../share-public/docs/World%20of%20Warcraft/procedural-dungeon/01-map-chunk-architecture.md)
is the architecture of record, [02](../../../../../../share-public/docs/World%20of%20Warcraft/procedural-dungeon/02-pdv2-session-resume.md)
the state and the findings ledger, [03](../../../../../../share-public/docs/World%20of%20Warcraft/procedural-dungeon/03-city-facades.md)
the city look. This document does not restate them; it says what changes.

Evidence tiers as used across this project: **T0** written · **T1** verified offline on this box ·
**T2** run in a real client by the operator.

---

## 0. The asks, and what was decided

| # | Ask | Decision | Round |
|---|---|---|---|
| 1 | More doodads — broken furniture, crates, dungeon clutter | Wall feet **plus** two new placement kinds: corners and scattered on open floor | A |
| 2 | Better floor — hard borders, monotonous | Feather the alpha, break the cell grid, per-role textures, stock ground-effect doodads. **Flatness kept.** | A |
| 3 | Critters | Yes, as a spawn source outside the combat bookkeeping entirely | A |
| 4 | More undead / demon mobs in the existing style | Pure world SQL: new packs from stock templates | A |
| 5 | One theme per room | Yes — draw a pack per room, then fill from it | A |
| 6 | Mainly ONE path connecting the boss rooms, with a few branches | Chain generator replaces scatter+MST. Branches mostly dead-end, occasional shortcut | B |
| 7 | Rooms of different sizes | A third room alt with a 33 yd platform, against the 50 yd standard | B |
| 8 | Boss rooms bigger, no centre object | Pad removed → full open 50×50 arena. **Not** a multi-block arena | B |
| 9 | Start room without a centre object | Pad removed from the entrance role | B |
| 10 | An altar every 5 rooms that serves as the respawn point | Clickable GO, binds the run's respawn point; death returns the player **alive** | B |
| 11 | Barriers before boss rooms, opening at 50 % of the previous rooms' mobs | GO barrier per boss room, per-segment denominator; branch rooms count | B |
| 12 | Cooler boss fights with reactive mechanics at higher levels | Own `PDv2BossAI` with three mechanics staged on the 1–100 difficulty dial | C |

Decisions the operator made explicitly, recorded so no implementing session re-opens them:
boss room = pad removed, not a multi-block arena · death = alive at the last altar · boss
mechanics staged on the **difficulty dial**, not on dlvl · **one theme per room** · doodads in
corners and on open floor, not only at wall feet · the patrol is a real threat but does **not**
count toward the barrier · barrier segment = rooms since the previous boss room · three rounds ·
branches mostly dead-end with an occasional shortcut · branch rooms **do** count toward the barrier.

---

## 1. Ground truth that shapes every part of this

These are measured, not assumed. Violating one of them produces a defect that is invisible
offline and only shows up in a client.

1. **The server has no terrain on map 760.** No `.map`, no VMAP, no mmaps. `Map::GetHeight`
   answers `INVALID_HEIGHT`. Everything — players, creatures, chests, props — is pinned to the
   config constant `V2.FloorZ = 50.0`, and creatures spawn with `SetDisableGravity(true)`
   (`src/PDv2InstanceScript.cpp:599`). A fall catcher at 1 Hz, not the ground, is the floor
   (`:1094`).
2. **Engine line of sight is meaningless here.** `IsWithinLOSInMap` is true between any two
   points, including straight across the void. Every position decision goes through
   `PlanApproach` on the walk grid (`src/PDv2CreatureAI.cpp:301`).
3. **The walk grid resolves 8.33 yd.** Nothing smaller can be validated against it.
4. **Any Z movement is permanent.** Gravity is off, so knockback, charge and jump leave a unit
   hovering. Two spells are already excluded or demoted for exactly this
   (`mod_pdungeon_member_spells.sql:162`, `:208`).
5. **The block grid is load-bearing into the client.** The composer memcpys four fixed-size MCNK
   payloads per block into fixed slots and rebuilds no offset table
   (`scripts/49_pd_compose_blocks.py:10`), and every block must present its socket on cells 3+4
   (`scripts/48_gen_t1_blockkit.py:149`). Blocks must tile and doors must align.
6. **Three implementations share the manifest** — the emitter (`PDBlockPlan.cpp:761`), the Python
   oracle (script 49) and the DLL's parser. They are checked byte-for-byte, not assumed compatible.
7. **The manifest must fit one addon packet.** 2048 B hard, 1900 B budgeted; the sender logs an
   error and sends nothing rather than truncating (`PDv2UILink.cpp:487`).
8. **Startup-only tables.** Chunk meta, decor rules and packs load once in
   `PDWorldScript::OnStartup` so map threads read them lock-free. Any SQL change needs a
   worldserver **restart** — `.reload config` silently does nothing.
9. **`creature_template` 84263–84290 and spells 900050–900060 belong to other live modules**
   (fl-underground-dungeon's map 741, mod-dungeon-challenge). They must never be edited.
10. **The spawn RNG stream is a contract.** `PDv2PackMgr::SelectSpawns` documents every draw in
    consumption order; adding, removing or reordering one re-rolls which creatures every stored
    seed spawns. A dead knob is still drawn purely to keep the stream aligned (`:600`).
11. **`PDv2InstanceScript::Update` does not call the base class**, so `InstanceScript`'s
    `TaskScheduler` never runs. Every timer in this spec is an own timer.
12. **The live checkout is `azerothcore-wotlk/modules/mod-procedural-dungeon`.** The clone at
    `GitHub/mod-procedural-dungeon` is a stale v1 tree with no PDv2 sources; editing it loses
    the work silently.

### Deploy axes

Three independent axes, and a change on one does not imply the others:

- **Kit bytes** (scripts 48/51/52) → new `t1b-vNN` directory, `KitDir` flip in `FLStream.ini`
  with the client closed, **cold client restart**. `KitDir` is read once at DLL attach.
- **Client patch** (a new BLP or DBC row) → rebuild `patch-9.MPQ`, **client restart**. A running
  client holds `patch-9.MPQ` so hard it cannot even be renamed.
- **Server SQL / C++** → **worldserver restart**. `KIT_VERSION` only needs bumping when
  `walkMask`, anchors, props or the chunkId set change; a texture-only pass does not touch them.

Every kit round appends its paragraph to the `FLStream.ini` version ladder. That ladder is the
rollback map. It currently stops at v23/v24 while `KitDir` points at **t1b-v28** and the deployed
`patch-9.MPQ` is md5 `97fc5cd5` — rounds 17–19 shipped without a ladder entry and without an
operator report. **Round A closes that gap before it adds to it.**

---

## 2. Round A — the look

Deploy footprint: one kit round, one new `patch-9.MPQ`, one worldserver restart, one cold client
restart.

### A1 · The floor

Four edits, all in `scripts/51_texture_blockkit.py` except where noted. **No ADT file changes
size**, so MCLY, MCIN, MCNK payload size and the composer are all untouched.

**A1.1 — Feather the class border.** Today the MCAL alpha steps binary at the cell line (walkable
8..13 → wall/void 0). Replace the per-cell constant lookup with a signed-distance ramp over N=4
alpha texels (~2 yd).

Three constraints, each of which has already cost this project a round:

- The class-invariant gate at `51:754` currently **fails the build** for any wall/void pixel that
  is not exactly rock 15. It must be **rewritten, not deleted** — new form: *a pixel further than
  N from a class boundary obeys the old rule*. It is the only check that stops the wall inheriting
  the floor texture, the defect that took rounds 3, 6, 7 and 11 to close.
- Feather **inward from the wall only**. The wall texture sits on the un-alpha'd base layer
  because a thin filtered alpha band was measured to lose the rock on the inner wall face
  (`48:1410`). A feather that eats into the wall reproduces that failure.
- Alpha values must be a pure function of **block-global** pixel coordinates, and the outermost
  pixel row and column of each block must stay at their current class value. MCNK flag 0x8000 is
  not set, so the client duplicates the last alpha row and column; a gradient there becomes a
  visible 0.52 yd discontinuity every 33.33 yd.

`MCSH` in `52_punch_kit_holes.py:263` steps on the identical line. Either feather it the same way
or drop it — the two must not disagree.

**A1.2 — Break the 8.33 yd ladder.** The "worn track" is drawn per cell as a cross through the
cell centre (`51:437`), so chained cells produce a ruled grid at cell pitch. Make the track a
function of the block-global coordinate **along the corridor axis** — one continuous 2–3 yd band,
no cross-ties — and drop `edge_pass` (`51:428`) so a stripe appears only where a track actually
runs, not on every block-boundary ring.

**A1.3 — A texture per room role.** `TEXTURE_SUPERSET` (`48:1379`) holds four entries and MCLY has
four slots' worth of encoding, but `nLayers` is 2 and each theme paints with one fixed pair. Give
Room, Corridor and RoomBoss different **layer-1 texIds** out of an enlarged superset. The MTEX
table stays kit-wide and uniform; only the 4-byte texId per MCNK moves.

Any new terrain texture must live under `TILESET\` — anything else renders solid green, measured —
and must ship a sibling `<name>_s.blp` specular, whose absence produced green terrain in exactly
the two kits that lacked it. Neither rule has an automated check. Pick tiles by **measured axis
imbalance**, not by eye: terrain UV is axis-aligned to the tile and never to the street, so a
directional texture reads rotated on half the surfaces (`100_stage_city_wall_texture.py:44`).

**A1.4 — Stock detail doodads, for free.** `MCLY_EFFECT_NONE = -1` at `51:119`. Pointing it at a
real `GroundEffectTexture.dbc` row makes the client scatter pebbles and tufts across the floor for
**zero bytes** of ADT growth. Costs one DBC row in `patch-9.MPQ`.

**Explicitly not done: height noise.** The realistic budget without a server-side height channel
is ±0.3 yd, pinned to 0 on every block edge (or neighbouring blocks step against each other) and
0 under every facade footprint (or the WMOs float). What is left is a per-block dimple repeating
every 66.67 yd — the same monotony, moved into geometry — bought at the risk of visibly sinking
every mob, chest and torch, because the server places all of them at `FloorZ`. Not worth it.

Also not done: **do not flatten the wall band to open the rooms up.** The crest is the doorway
system and the only movement barrier on the map. Halving it to 6.0 yd was already the measured
floor of what is safe.

### A2 · Doodads

**Two new placement kinds** beside `DECOR_PLACEMENT_WALL_FOOT` (`PDv2DecorPlan.h:66`):

- `corner` — a walkable cell with wall neighbours on two adjacent sides. Stacked crates, barrels,
  a lean-to of broken boards.
- `scatter` — a walkable cell in the room interior, honouring `minSpacingYd` and excluding the
  socket track. Overturned furniture, rubble, bones, a broken cart.

Each needs a candidate collector beside `CollectWallFeet` (`PDv2DecorPlan.cpp:93`) and a branch at
the placement test in `BuildDecorPlan` (`:444`). **Keep the draw order and the "consume the
candidate even on rejection" rule**, or every stored seed re-decorates itself.

GameObject templates go in **910050–910099** (free; `PDDefines.h` allocates 910000–910033 and the
harness hard-asserts kit props live in 910040–910049, of which only two remain). All `type = 5`
GENERIC — the only collision class measured to block a player on this map, which is why decor is
GameObjects at all. `size` is the only scale dial.

Rules go in a **new SQL file**, not an edit of `mod_pdungeon_decor.sql`: that file's idempotent
`DELETE` covers ids 1–3 only, so new rules with higher ids survive a re-apply of the old file.

Two corrections ride along, both measured:

- `mod_pdungeon_decor.sql`'s comment claims rule 3 is dormant "because the walkable cells of
  `corridor_straight` are column 4 and nothing else". True of kit v2; **false since kit v23** —
  city corridors are two cells wide and `SOCKET_TRACK` excludes only index 4, so rule 3 is
  planting a torch in the lane the player walks. Fix the placement and the comment.
- `BlockRoleName()` maps `CorridorDeadEnd` through `default:` to `"corridor_cross"`
  (`PDv2DecorPlan.cpp:246`). Give the dead end its own string and make `CorridorCross` explicit,
  so a dead-end filter can address dead ends and a cross filter stops firing on them.

**No GO cap exists in v2** (v1's decor-first truncation was never carried over), and everything is
summoned in one synchronous loop. Round A adds placement kinds; it must also add a per-instance
decor budget so a 15-room layout cannot spike the summon loop.

### A3 · Critters

A critter is not a `PackRole` — it must sit outside the combat bookkeeping entirely. **Four things
change together**, and omitting any one produces a level-80 elite rabbit or a room that can never
be cleared:

1. A spawn source that is not the pack draw — modelled on the decor rules (own table, own RNG
   stream, own placement), so it never touches `SelectSpawns`'s documented draw order.
2. The spawn loop must **not** count it into `_roomAlive` or `_run.total`.
3. `PDv2CreatureAIBinder::GetCreatureAI` must return `nullptr` for it. It currently hands
   `PDv2MobAI` — with its own 20 yd proximity aggro — to every ownerless creature on map 760.
4. `PDv2Scaling::IsDungeonCreature` gates on **map only**, so a critter would be forced to level 80
   and HP-scaled by difficulty. It needs the same exclusion.

Precedent to reuse rather than invent: the void-zone neutraliser at `PDv2InstanceScript.cpp:223`
switches anything carrying `UNIT_FLAG_NOT_SELECTABLE` to friendly + passive. A flag-based rule is
better than an entry list.

Critters spawn with gravity disabled at `FloorZ` like everything else, and their positions must be
grid-vetoed — a critter past the platform edge hovers in the void where players can see it.

### A4 · More undead and demons

Pure world SQL. New `pdungeon_packs` rows at **id ≥ 4** (the shipped file's idempotent `DELETE`
covers 1–3 only), members drawn from **existing stock `creature_template` entries**, 2–3
`pdungeon_member_spells` rows per creature following the established cadence (melee: cd 6000–8000
@ minDiff 1, 8000–10000 @ 50, 8000–12000 @ 75; range: one slot-0 filler at cd 0 plus the two gated
rows; any crowd control: cd 60000, never in position 1).

Five data traps, every one of which fails silently in game:

- **`unit_class 1` creatures have basemana 0.** A spell with a **flat** mana cost fails
  `CheckPower` with `NO_POWER` on every cast; a percentage-cost spell resolves to 0 and works.
  11 of the 28 stock entries in use are `unit_class 1`.
- **Theme scoping.** A pack whose `theme` is neither 0 nor the live `V2.Theme` (2) is invisible to
  the loader and the dungeon fills with Ironwool Mammoths. This has already happened once.
- **Silent role demotion.** A role-1 member with no `casterSpellId` and no spell rows becomes melee
  at load (LOG_ERROR only); one with rows but no slot-0 filler keeps role 1 and spams the pack
  column (LOG_WARN only). Both read in game as "the caster is broken".
- **A range filler must actually reach.** Its spell range must be ≥ `V2.CastRangeYd` (25.0).
- **`pdungeon_member_spells` PK is `(entry, spellId)`** — one creature cannot carry the same spell
  in two slots or two difficulty tiers.

Displays: use **stock 3.3.5a `CreatureDisplayInfo` ids**. Everything in use today is stock, which is
why packs need no client art. A custom display would need a DBC merge and an MPQ deploy; the design
deliberately avoids it.

### A5 · One theme per room

`SelectSpawns` merges every unlocked pack's members into one melee pool and one caster pool, and
every trash slot rolls independently — so crypt undead and abyssal demons stand side by side.

Change: draw **one pack per room** first, then fill that room's trash slots from that pack's pools
(falling back to the merged pool if the drawn pack cannot fill a role). The boss slot keeps drawing
from the role-2 pool across all packs, so a room's theme does not constrain which boss appears.

This inserts a draw into the documented stream and therefore **re-rolls which creatures every
stored seed spawns**. Acceptable: the server is not public yet and character progress is expendable.
The new draw must be added to the draw-order comment at `PDv2PackMgr.cpp:477` in the same commit.

### Round A acceptance

Offline (T1): `pdblock --batch 500`, `--decor-batch 3000`, `--roomcap 3000`, `flstream_tests.exe`,
and the three-way byte parity of `FLPD_32_32.adt` across generator, oracle and DLL. Run the kit
chain **twice** and diff the md5 set — the kit must be deterministic.

In game (T2), against a written round document in `ForgottenLand2.0/tools/`: the class border reads
as a transition rather than a cut · no ruled grid on corridor floors · rooms, corridors and boss
rooms are visibly different surfaces · ground detail is present · crates and furniture appear in
corners and on open floor, not only against walls · critters are present, ignorable, and killing one
does not move any counter · undead and demons appear and each room reads as one faction.

---

## 3. Round B — layout and progression

Deploy footprint: kit round, worldserver restart, cold client restart. **`PD_LAYOUT_VERSION` is
bumped once for the whole round**, and the 571-byte manifest freeze pin in
`tests/blockplan_harness.cpp:1122` is re-pinned against the new generator.

### B0 · The spine generator

**Today** `GenerateBlockPlan` scatters rooms by rejection sampling, then builds a Kruskal MST over
**all pairs** weighted by squared distance plus `loopChancePct` extra edges. Nothing ever plans a
route; the layout is the cheapest tree through a point cloud, which is exactly why it reads as
randomly connected rooms.

**New: a chain.**

1. **`ChainRooms` replaces `ScatterRooms`.** Rooms are placed in order: from a start cell, each
   next room 2–3 blocks away, respecting `MIN_ROOM_GAP` (2, Manhattan) against every placed room,
   with a direction bias against immediate reversal. **Backtrack** on a dead end rather than
   restarting the whole layout — at 15 rooms on an 8×8 field the packing is at its limit and
   rejection sampling fails often. The short step is deliberate: a long step buys corridor blocks,
   and corridor blocks are what the manifest budget pays for.
2. **Only consecutive chain members are connected**, through the existing L-walk. No all-pairs
   graph. The result is one path by construction.
3. **Branches** hang off chain rooms — never the entrance, never a boss room. Exactly **one room per
   branch**. Count is a new config key `V2.Branches`, default 2, effective count
   `min(V2.Branches, floor(chainRooms / 3))` so a small dungeon cannot become more branch than
   spine. **Branch rooms come out of the same `cfg.rooms` budget**, so the total room count of a
   layout is unchanged by branching — a 5-room dungeon with 2 branches is 3 chain rooms plus 2
   branch rooms, not 7 rooms. A branch room can hold spawns, an altar and loot like any other.
4. **Shortcuts.** `LoopChancePct` is repurposed rather than retired: it is now the chance that a
   branch, instead of dead-ending, routes back onto a **later** chain room. Default 15 stays.
   The key is persisted per account as `gen_loop_pct`; its meaning changes under the version bump.
5. **Boss rooms sit at fixed chain positions.** The last chain room is always a boss; with N boss
   rooms, boss *k* sits at chain index `round(k · (len − 1) / N)`. Segments come out even, which is
   what the altar cadence and the barrier denominator both key off.
6. **The entrance is chain member 0**, replacing "the room nearest the field's north-west corner".
7. **Dead-end chest stubs stay exactly as they are**, and stay after every routing draw.

`depth` remains the BFS depth (decor and the HUD read it) but for chain rooms it now coincides with
the chain index, which is what makes the segment arithmetic trivial.

**The one real risk: manifest size.** A chain visits rooms in order; an MST connects neighbours. The
chain therefore spends **more corridor blocks** for the same room count. The budget is 1900 B and
the measured maximum at 15 rooms was 1406 B. `pdblock --roomcap 3000` must be re-measured **before**
any deploy. If 15 rooms no longer fit, the lever is the **room cap**, not the step distance — a
longer step buys headroom by turning the dungeon into empty corridor maze.

Validation is unchanged in kind: `ValidateBlockPlan`'s connectivity BFS, socket agreement and
coordinate-uniqueness rules all still apply and all still have to pass.

### B1 · Pads

`footprint_cells(theme, roleIndex, alt)` in `48_gen_t1_blockkit.py:248` returns the centre pad. It
returns an empty set for the **boss** role and the **entrance** role from now on. The boss room
becomes a full open 50×50 arena; the start room is clear, so the player no longer materialises
inside an object.

No RNG draw moves — `alt` is still drawn, it just no longer implies a pad for those two roles. This
part alone would need no layout-version bump; it rides along with the rest of Round B.

The small pads in ordinary rooms **keep** their fountain. The round-16 report offered to trade the
fountain for a built-up pad and the operator never took it; the ask now is the opposite direction.

### B2 · Rooms of different sizes

A third alt for the **Room** role with a platform of cells 2..5 (33.3 yd) instead of 1..6 (50 yd).
The socket arms stay, so the room reads as a smaller chamber entered through throats. Against the
50 yd standard and the now-open 50×50 boss arena, that gives three legible sizes.

Costs, all of which land in this round anyway: the walk mask changes → `mod_pdungeon_chunk_meta.sql`
regenerated by script 48 (**never hand-edited**) → `KIT_VERSION` bump; and `AltCountFor(Room)` goes
2 → 3, which **shifts the draw stream** → `PD_LAYOUT_VERSION` bump. Every combination of
(role, mask, alt) the planner can emit must have a chunk-meta row **in both theme namespaces**, or
`BuildWalkGrid` fails and creatures stand still on that block. The harness's chunk-meta completeness
sweep (`blockplan_harness.cpp:1170`) is what proves it.

Spawn placement is a hardcoded 12 yd circle around the block centre
(`PDv2InstanceScript.cpp:64`, `:889`) and does **not** follow room geometry. In a 33 yd room that
still lands inside the platform, but the kit already publishes per-chunk spawn / entry / boss / chest
anchors (`PDv2Mgr::AnchorsFor`) that only the decor planner uses. Moving spawns onto those anchors
makes placement room-shape-aware for free and is the right fix in this round.

### B3 · Altar and respawn

**The object.** A new `gameobject_template` in the 910000–910099 block, `type = 10` GOOBER like v1's
shrine, with `ScriptName` `go_pdungeon_altar` and a `GameObjectScript::OnGossipHello` handler
modelled on `src/PDExitObjects.cpp:39`. Do **not** set `GO_FLAG_NOT_SELECTABLE` — `GameObject.cpp:1463`
returns before the script runs. Reach instance state through
`dynamic_cast<PDv2InstanceScript*>(go->GetInstanceScript())`, the pattern `PDv2MobAI` already uses.

**Placement.** On the room's kit `entry` anchor via `AnchorsFor` + `BlockToWorld`. Always in the
entrance room; then in every fifth room **by chain index** — branch rooms have no chain index and
never carry an altar. With rooms = 3 + dlvl capped at 15 that is one to three altars beyond the
start.

**Clicking** binds the run's respawn point. Announce it through `PDv2UILink::SendNotice`.

**Death.** Not a `game_graveyard` row. A static coordinate on a map whose layout is per-account and
per-seed would drop a resurrecting player into the void, where only the fall catcher could save them
— and only if that instance has an entrance cached. Instead the module intercepts the repop and
returns the player **alive** at the bound altar (before the first altar: the entrance), with
resurrection sickness. No ghost: a ghost on this map walks through the client-only walls and falls
out of the world. This is what removes Westfall — which is not a module bug but a missing
`graveyard_zone` row for zone 5100, so the core falls back to the default graveyard.

### B4 · Barriers

**The object.** One blocking GameObject on the socket edge leading into each boss room, positioned
through `PDv2Mgr::BlockToWorld(bx, by, u, v, ...)` with u/v on the edge the `socketMask` names.
Opened with `SetGoState(GO_STATE_ACTIVE)`, exactly as v1's `OpenDoorGroup` did
(`PDInstanceScript.cpp:462`). Spawned from the same `if (!_spawned)` guard as the rest of the build
and pushed onto `_decorGuids` so `DespawnAll` tears it down.

Player blocking is the GO's own collision — the only evidence a GO blocks a player here is one
measured type-5 brazier, so the **width problem v1 needed `GateWidthExtra` for will come back**, and
a WMO barrier cannot be scaled to fix it (only M2 scales). Budget a calibration pass. Creatures do
not respect GO collision: flip the affected walk-grid cells to 0 while the barrier is shut and back
on open. The grid is a plain `std::vector<uint8_t>` read on this map's own update thread, so no lock
is needed — but it is treated as immutable today and that assumption has to be lifted deliberately.

**The state that does not exist yet**, all of which this round adds beside `_roomAlive`
(`PDv2InstanceScript.h:249`):

- `_roomPlanned` — the denominator. It cannot be derived from `_roomAlive`, because the Lil' Bro
  affix **inflates** `_roomAlive` mid-run (`:770`); a denominator taken from it would move under the
  barrier's feet. Splits raise the numerator's pool, not the planned total.
- `roomIndex → PlacedBlock` — the `roomBlocks` vector at `:801` is function-local and thrown away,
  so nothing on the instance can map a room back to a block.
- Segment membership — chain rooms strictly after the previous boss room up to this one, **plus the
  branch rooms hanging off them** (operator decision: branch rooms count).

**The rule.** `killed × 100 ≥ planned × V2.Barrier.Pct`, default 50, re-evaluated in `OnMobDied`
right after `MarkRunDirty()`. Announce state changes through `SendNotice` or a trailing field on the
`R` run frame — the frame is a full statement each tick, so a trailing field is backward-safe.

**Softlock guard, mandatory.** `roomsCleared` only increments on a decrement-to-zero, so a room that
spawned nothing is never counted, and `roomsCleared` can be permanently short of `roomsTotal`
(`:428`). v1 guarded against exactly this. The barrier counts **planned spawns**, not rooms, and a
segment whose planned total is zero opens immediately.

**Re-entry.** A completed run re-entered re-populates the same seed, so barrier state must be part
of what the rebuild branch resets (`:139`). Run state is memory-only — v2 overrides neither `Load`
nor `GetSaveData` — so a worldserver restart mid-run loses barrier state and the instance rebuilds
from scratch anyway.

### B5 · Patrol

One elite patroller per boss segment, walking the spine from the boss room back to the previous boss
room (or the entrance) and returning. Everything needed exists: `GetWalkGrid`, `FindGridPath`,
`SimplifyGridPath`, and `PDv2MobAI::StartWaypointRun` (`PDv2CreatureAI.cpp:150`) already consumes
that shape. What is new is the **out-of-combat** loop — today the waypoint runner is driven only
from `UpdateGridChase` inside combat — and re-acquiring the route after an evade. Seed it from
`plan.effectiveSeed` so the patrol is as deterministic as the spawn draw.

It aggros on proximity like everything else and **does not** count toward the barrier (operator
decision) — it is risk on the road, not progress. It therefore must not enter `_roomPlanned` or
`_roomAlive`.

Two constraints: there is deliberately **no distance leash** in v2, and `SetHomePosition` plus
`ScriptedAI`'s evade will drag a patroller back to its spawn point — the patrol state has to survive
that, or the first evade parks it forever. And no `TaskScheduler` (§1.11): own timer.

### Round B acceptance

Offline (T1): the full harness, with `--roomcap 3000` **re-measured** and the manifest freeze pin
re-pinned; chunk-meta completeness across both theme namespaces; three-way byte parity.

In game (T2): the layout reads as one path with side pockets · boss rooms are open 50×50 with no
centre object · the start room is clear · small and standard rooms are distinguishable · an altar in
the start room and every fifth room, clicking binds it · dying returns you alive to the bound altar,
never to Westfall · the barrier before a boss room is solid, cannot be walked or jumped past, and
opens at the announced threshold · the patroller walks its segment, aggros, and returns to its route
after an evade.

---

## 4. Round C — bosses

A boss today is a trash mob in the boss slot: same `UpdateGridChase` + `CastReadyKitSpell` +
`DoMeleeAttackIfReady` loop, no phases, no enrage, no adds, no immunity. `grep` for
`EventMap|SetPhase|SetImmuneTo|SummonList` across the module returns two comments and one
`SetReactState`.

**The split.** A `PDv2BossAI` branch inside `UpdateAI`, keyed on the `isRunBoss` tag next to the
existing caster-role branch (`PDv2CreatureAI.cpp:731`). **Not** in the binder: the binder runs inside
`SummonCreature`/`AIM_Initialize`, **before** `SpawnTaggedMob` writes the tag, so the tag is not
available there. `_mob` is re-fetched every tick at `:689`, which is why the tick-side branch works.

Three mechanics, staged on the **1–100 difficulty dial**. The thresholds are three config keys —
`V2.Boss.TelegraphMinDiff` (default 1), `V2.Boss.ImmunityMinDiff` (34), `V2.Boss.OrbMinDiff` (67) —
so the operator can move a mechanic without a rebuild. They are read at `.reload config` time like
every other key and are **not** inputs to any seeded stream:

**C1 — Telegraphed AoE (from low difficulty).** A marker GameObject on the ground, ~2 s of warning,
then damage in radius. **Radius ≥ 12 yd** — one walk-grid cell is 8.33 yd and nothing smaller can be
validated against the grid; `UpdateImmolation` already skips the grid gate for exactly this reason
(`:587`). Damage goes through the **normal spell path**, so defensive cooldowns work.

**C2 — Immunity until the add dies (from mid difficulty).** The boss goes immune and passive, two
adds spawn, immunity drops when the last add dies. Add positions must be **grid-vetoed** before
spawning — `SplitOnDeath` already does this (`PDv2InstanceScript.cpp:722`) because a gravity-less add
past the platform edge hovers unreachable in the void.

**C3 — Protective orb plus room-wide AoE (high difficulty).** An orb GameObject appears; shortly
after, a room-wide strike hits everyone **not** standing within the orb's radius. Because the module
deals this damage itself, "inside the orb" is a distance check, not aura bookkeeping.

The complete ground-hazard primitive already exists and is generic: `TickVoidZones`
(`PDv2InstanceScript.cpp:255`) keeps a vector of carrier GUIDs, prunes it each tick and runs a
per-player distance scan that reads its radius and base value live out of `SpellInfo`. It ticks at
**1 Hz** — a telegraph needs its own timer, not this one.

**Two things this round will not do.**

*No knockback, charge or jump.* Gravity is off; every Z displacement is permanent. `64666 Savage
Pounce` is already excluded from two kits for handing the creature to the core's jump generator, and
`62129 Wail of Souls` was demoted to a 60 s cooldown because its 30 yd knockback punts players off
the platform.

*No damage outside the spell system.* Affixes and void zones deal raw `Unit::DealDamage` /
environmental damage that nothing resists, absorbs, reflects or logs normally. A "lethal AoE" built
that way is unavoidable by any player defensive — the exact opposite of a dodgeable mechanic.

**One balance change rides along.** Bosses currently carry the room's affix on top of the difficulty
curve (`PDv2PackMgr.cpp:573`), reversing mod-dungeon-challenge's own rule that excludes bosses. A
boss that is simultaneously Bigger Boy and Hell Touched is already at the edge of fightable; three
scripted mechanics on top can make it unbeatable. **Bosses are removed from the affix draw in this
round.** Removing the draw shifts the seeded stream and must be recorded in the draw-order comment.

Boss state (phase index, mechanic timers, add-alive flags) lives on the `PDv2MobData` tag, which is
memory-only. A worldserver restart mid-fight loses it and the instance rebuilds; that is accepted,
consistent with the rest of the run state.

### Round C acceptance

In game (T2), one run per difficulty tier: the telegraph is visible with enough warning to move out
of it, and moving out actually avoids the damage · the immunity phase cannot be burned through and
ends on the add's death · the orb protects those inside and only those · no mechanic displaces a
player or a creature in Z · a boss at difficulty 100 is hard and finishable.

---

## 5. Cross-cutting rules for every round

- **Branch per round**, Conventional Commits, English in code, docs and commit messages.
- **Determinism.** No `std::random` distribution in `src/generator/`, fixed iteration orders, and
  every new draw documented in the consumption-order comment it belongs to. `PDRandom::Chance`
  short-circuits at 0 and 100 without drawing, so even those values shift a stream.
- **Generated files are never hand-edited**: `mod_pdungeon_chunk_meta.sql` (script 48) and
  `mod_pdungeon_prop_displays.sql` (script 57).
- **Every new startup-loaded table gets a row count printed by `.pdungeon v2 info`** in the same
  commit. That line is how the operator's ritual catches "the SQL never reached the DB", and it is
  why `0 walk masks` is a known failure signature rather than a mystery.
- **Read config from the deployed file**, `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf`,
  never from the `.dist`. The `.dist` beside it is stale from 2026-07-08 and is never loaded. Three
  keys already drift between code default and dist comment; `V2.Theme` is 2 in the dist and **1 in
  `PDv2Mgr.cpp:50`**, so a deployment whose conf omits the key silently generates the mine.
- **Every round ships its operator document** in `ForgottenLand2.0/tools/pd_testlauf_rundeNN.md` in
  the runde14–16 shape: state table (kit / patch-9 md5 / worldserver.exe / FLStream.dll / SQL
  kitVersion), the exact start sequence with the walk-mask assertion, one section per ask with an
  explicit *Erwartet* and a *Falls doch*, a mine regression probe (`.pdungeon v2 gen 0 1`), what is
  still open, and a rollback table.
- **Every round that touches the kit appends its paragraph to the `FLStream.ini` ladder** (A and B;
  C is server-side only). Round A additionally back-fills the missing v25–v28 entries, or the
  current kit has no documented rollback path at all.
- **Share-public duties, in the same commit as the work**: the item in
  `docs/World of Warcraft/12-server-todo.md`, an MIG entry in
  `forgotten-land/15-host-migration-log.md` for every host-relevant change, and the log line at the
  very end of `claude_log.md`.

## 6. Assumptions recorded

- Character progress is expendable, so `PD_LAYOUT_VERSION` bumps and spawn-stream shifts are
  acceptable. If the server goes public before Round B, this changes.
- `V2.Branches` default 2 and `V2.Barrier.Pct` default 50 are starting values for operator tuning,
  not design constants.
- "An altar every 5 rooms" is read as: always in the entrance room, then every fifth room by chain
  index.
- Round B's kit round and Round A's kit round are separate deploys. If Round A slips, they may be
  merged, but then a defect is attributable to two rounds at once.
