# PDv2 Round B / B3–B5 — barriers, patrol, ambush (design addendum)

**Date** 2026-09-03 · **Branch** `claude/pdv2-round-b-0cf92ad4` · **Status** approved design, not implemented

Addendum to `2026-09-01-pdv2-content-expansion-design.md` §3 "B4 · Barriers" and "B5 · Patrol"
(vault numbering **B3** and **B4**) plus the operator's new ask of 2026-09-03, **B5 ambush**: *"pro
Segment zu 50 % ein Überfall, während der Spieler in einem Gang ist (Areatrigger in einem Gang);
der Spieler wird 2 Sekunden gestunned und es spawnen 4 Mobs um ihn herum."* Research with
`path:line`: `.superpowers/sdd/b3-research.md`, `b4b5-research.md` (workbench only).

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client.

---

## 0. Facts that shape the design (measured 2026-09-03)

- **Only a type-5 GENERIC GameObject has been measured to block a player on map 760**; a type-0
  DOOR's client-side blocking is unproven there. Player movement is client-authoritative.
- **Creatures ignore GameObject collision**; the module's A* walk grid is the only thing they path
  over. A closed barrier must flip **both** sides' lane cells (4 cells, 16.7 yd) to survive the
  AI's 2-cell snap radius. All grid readers and writers run on the map thread.
- **`_roomAlive` is inflated mid-run by Lil' Bro**; the denominator must be `_roomPlanned`, a copy
  taken at the end of `SpawnFromPlan`. `roomIndex` is dense over `plan.blocks` order excluding the
  entrance, and `roomBlocks` is discarded today.
- **A segment whose only room is its boss room exists** (dlvl 30 with `cfg_rooms 1`: four
  single-boss segments); counting the boss room's own pack in its barrier's denominator softlocks it.
- **`PDv2MobAI` has a complete waypoint runner** (`StartWaypointRun`/`MoveToWaypoint`/
  `MovementInform`) driven only from combat; out of combat it runs proximity aggro and nothing else.
  `EnterEvadeMode` is not overridden: the core's `MoveTargetedHome` is a straight line to the spawn
  point, across the void. There is no elite role, rank or pool in PDv2 — "elite" is an HP lever
  through `SpawnTaggedMob`'s `baseHealthOverride`.
- **`OnMobDied` increments `_run.killed` unconditionally and `PDv2MobData::roomIndex` defaults to 0**
  — an untagged-for-room mob would decrement room 0.
- **A DBC-free area trigger is impossible** (the id comes from the client). The instance's
  `Update` runs every map tick (10 ms); its own 1 Hz branch exists; a second, faster own timer is the
  documented pattern.
- **Stun candidates**: spell **20170 "Stun"** (aura-only `MOD_STUN`, flat 2000 ms, 100 yd, mechanic
  12) applies through `Unit::AddAura(spellId, target)` with the player as caster — the client shows a
  real stun with a debuff icon and the aura releases itself. Gravity is off, so no knockback spells.
- **The kit publishes `"role":"patrol"` anchors** on corridor arms and `"elite"` anchors in boss
  rooms; nothing reads them yet (B1's typed decoder makes them readable).
- **Independent RNG streams** are the module's precedent: `layoutSeed ^ MIX` (decor, critters).

---

## 1. Decisions

### B3 — barrier before every boss room

| # | Topic | Decision |
|---|---|---|
| 1 | Denominator | **Reading B**: the planned spawns of the segment's rooms **excluding the boss room itself** — chain rooms strictly between boss k−1 and boss k, plus the pockets and loop rooms hanging off them (`SegmentOf` == k, not `RoomBoss`). Patrol and ambush mobs never count (their tag says so). A zero denominator opens the barrier immediately (spec's softlock guard). |
| 2 | Numerator | Kills among that same room set (`_segmentKilled[k]`), moved in `OnMobDied` before `MarkRunDirty()` and re-evaluated right after it: `killed × 100 ≥ planned × V2.Barrier.Pct` (default 50). |
| 3 | The object | `gameobject_template` **910059**, `type 5` GENERIC (the measured blocker), `displayId 7482` `Vr_Portcullis.m2` (16.3 yd wide at scale 1), `size 1.1`, name `Sealed Portcullis`, no script. Row in `mod_pdungeon_templates_fix.sql` (INSERT + DELETE list). |
| 4 | Placement | One cell inside the boss block on its **entry edge** (the socket whose corridor run reaches chain room k−1, found with the run walk of decision 8), at the lane centre: N `(4.17, 33.33)`, S `(62.5, 33.33)`, W `(33.33, 4.17)`, E `(33.33, 62.5)` in block-local yards. Orientation from two conf keys, `V2.Barrier.OrientNS` (default 0.0) and `V2.Barrier.OrientEW` (default 1.5708) — calibrated by the operator without a rebuild. |
| 5 | Opening | `go->Delete()` (removed from `_decorGuids`) — no dependence on door-state collision; the portcullis simply vanishes. Announced with `SendNotice("The barrier to the boss falls.")`. |
| 6 | Creatures | While closed, the four lane cells on both sides of the edge (boss block cells and the corridor neighbour's mirror cells) are flipped to 0 on the instance's grid through a new `SetCellsWalkable(cells, bool)`; restored on open. `GetWalkGrid`'s "read-only afterwards" comment is rewritten. No forced repath (mobs re-decide within 500 ms). |
| 7 | Hint | A one-shot notice per barrier when a player first comes within 12 yd of a closed one: `"The barrier holds - N more of this segment's foes must fall."` (1 Hz branch). |
| 8 | Run walk | The validator's `roomAtEndOf` lambda becomes a public engine-free helper `RunFromSocket(plan, blockIndex, bit, outRun) -> endBlockIndex` (straight through loop attachments, −1 for stubs/nowhere/junction) plus `SpineRunInto(plan, chainIndex, outRun) -> entrySocketBit` (the run into chain room i from i−1 and the socket it arrives through). The validator uses the same function. |
| 9 | State | Beside `_roomAlive`: `_roomPlanned`, `_roomSegment`, `_roomIsBoss` (per dense roomIndex), `_segmentPlanned/_segmentKilled` (1..N), `_barriers` (`{k, guid, cells, open, hinted}`), reset in the rebuild branch with the rest; `.pdungeon v2 info` unchanged. Run frame not extended (the HUD drops unknown tails; a later Lua change may show it). |

### B4 — patrol per boss segment

| # | Topic | Decision |
|---|---|---|
| 1 | Route | Runtime A* on the walk grid from the corridor block adjacent to boss room k's entry edge (the last block of its run) to the centre cell of chain room b_{k−1} (the previous boss, or the entrance), `SimplifyGridPath`; the patroller walks it back and forth (ping-pong), `SetWalk(true)` out of combat. |
| 2 | Who | One creature per segment from the **melee trash pool**, drawn on its own stream (`layoutSeed ^ PD_PATROL_SEED_MIX`, one `UniformInt` per segment over the pool in pool order); HP × `V2.Patrol.HealthMult` (percent, default 300) through `baseHealthOverride`; level 80 and difficulty scaling as every mob; no affix; `V2.AggroRangeYd` proximity aggro as everything else. |
| 3 | Counters | `PDv2MobData::countsForRun` (new, default true) is **false** for patrol and ambush mobs: `OnMobDied` still marks `counted`, rolls loot and splits, but touches neither `_run.killed`, `_roomAlive`, nor the barrier; `roomIndex` sentinel `PD_ROOM_NONE = UINT32_MAX`. `SplitOnDeath` copies the flag. `_run.total` excludes them. |
| 4 | Evade | `PDv2MobAI::EnterEvadeMode` override: base call (combat stop, health), then for a patroller `SetHomePosition(current position)` and `ResumePatrol()` (restart the run toward the nearest waypoint by grid distance); `JustReachedHome` also calls `ResumePatrol()`; a stale-run guard like the chase's (motion generator no longer `POINT_MOTION_TYPE` → replan). At every waypoint reached the home position moves along (short, in-lane home walks). |
| 5 | AI branch | In `UpdateAI`'s out-of-combat path: `if (_mob->isPatrol) UpdatePatrol(diff);` before `UpdateProximityAggro`. `MovementInform` at the end of a patrol run reverses the waypoint list instead of `StopWaypointRun(true)`. |

### B5 — ambush per boss segment

| # | Topic | Decision |
|---|---|---|
| 1 | Plan | Engine-free `BuildAmbushPlan(plan, chancePct, layoutSeed)` in new `src/generator/PDv2AmbushPlan.{h,cpp}` (cmake re-configure, harness build line): `PDRandom rng(layoutSeed ^ PD_AMBUSH_SEED_MIX)`; per boss segment k = 1..N, in order: `Chance(chancePct)` **always drawn**; on hit, the candidates are the corridor blocks of the spine runs into chain rooms `b_{k−1}+1 .. b_k` (via `SpineRunInto`), in that order, excluding dead ends and loop strips (a run never contains them); `UniformInt(0, n−1)`; empty candidates → no ambush for that segment. Output `AmbushSpot {blockIndex, bx, by, segment}`. Read live from `V2.Ambush.Chance` (default 50) like the decor rules — not a layout input. |
| 2 | Trigger | An own 250 ms timer in `Update`: every armed spot, every player in the instance within `V2.Ambush.RadiusYd` (default 9.0, 2D) of the block centre → fire once, then disarm. |
| 3 | Effect | `player->AddAura(V2.Ambush.StunSpell /*20170*/, player)` (2 s, self-releasing, client stun UI); `SendNotice("Ambush!")`; `V2.Ambush.Mobs` (default 4) creatures at ±6 yd along the corridor axis × ±4 yd across, grid-vetoed with the player's own cell as fallback, tagged `countsForRun = false`, `roomIndex = PD_ROOM_NONE`, no affix; `AttackStart(player)` on each. Picks: one synthetic room draw per spot through the pure spawn draw with seed `layoutSeed ^ PD_AMBUSH_SEED_MIX ^ (segment * 0x9E3779B1u)`, `spawnsPerRoom = V2.Ambush.Mobs`, no boss — computed at build time and stored. |
| 4 | Reset | Spots rebuilt with the run; a triggered spot stays disarmed until the rebuild. |

---

## 2. Config keys (all read live in `LoadConfig`, none persisted)

`V2.Barrier.Pct` 50 (0..100) · `V2.Barrier.OrientNS` 0.0 · `V2.Barrier.OrientEW` 1.5708 ·
`V2.Patrol.HealthMult` 300 (percent, ≥ 100) · `V2.Ambush.Chance` 50 (0..100) · `V2.Ambush.Mobs` 4
(0..8) · `V2.Ambush.RadiusYd` 9.0 · `V2.Ambush.StunSpell` 20170 (0 = no stun).

## 3. Acceptance

T1: fresh harness — `RunFromSocket`/`SpineRunInto` checked against the validator's own adjacency
rule; `BuildAmbushPlan` pinned (seed 12345, chance 100) and non-vacuous; the patrol/ambush counter
exemption covered by the existing spawn pins (no draw moved); worldserver builds after the
re-configure; the SQL row verified read-only.
T2 (round document): the portcullis stands across the boss room's doorway, cannot be walked or
jumped past, opens with the announced threshold, and creatures never path through it while closed;
the patroller walks its segment, aggros, returns to its route after an evade, does not move the
barrier's count; the ambush fires once per armed corridor, stuns for 2 s, four mobs appear beside the
player, no counter but the kill log moves.
