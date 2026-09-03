# PDv2 Round B / B1 — altar and respawn (design addendum)

**Date** 2026-09-03 · **Branch** `claude/pdv2-round-b-0cf92ad4` · **Status** approved design, not implemented

Addendum to `2026-09-01-pdv2-content-expansion-design.md` §3 "B3 · Altar and respawn" (vault
numbering: **B1**). The parent spec states the operator's decisions — an altar in the start room and
every fifth room, clicking binds it, death returns the player **alive** there with resurrection
sickness, never a ghost, never Westfall. This document settles the engine mechanics from the research
in `.superpowers/sdd/b1-research.md` (workbench only, every fact `path:line`).

Evidence tiers: **T0** written · **T1** verified offline on this box · **T2** run in a real client by
the operator.

---

## 0. Facts that shape the design (measured 2026-09-03, core `db99ebb45`)

- **No repop hook fires before the ghost exists except on the instance script itself.**
  `ZoneScript::OnUnitDeath(Unit*)` is called from `Unit::setDeathState` through
  `GetInstanceScript()` (`Unit.cpp:11439-11440`) — before `KillPlayer`, before any corpse or the ghost
  aura 8326. `OnPlayerCanRepopAtGraveyard` (`Player.cpp:4850`) fires only after the player pressed
  release and already carries the ghost aura; it can veto the graveyard trip.
- **Resurrecting inside `OnUnitDeath` is unsafe**: the core continues `Unit::Kill` → `KillPlayer`
  (`setDeathState(Corpse)`, death timer) after the hook returns. The module's own 1 Hz `Update`
  branch (`PDv2InstanceScript.cpp:1281`) is the safe place; the parent spec forbids `TaskScheduler`.
- **Nothing auto-releases on map 760** (`PlayerUpdates.cpp:359`, instanceable guard), so a
  death-triggered path is the only one that never strands a corpse.
- **Westfall is the missing `graveyard_zone` row for zone 5100** (`GameGraveyard.cpp:161-165`,
  default graveyard 4). Intercepting the release makes it moot for the normal path; the row is not
  added (a static graveyard on a per-account layout would land in the void).
- **`Player::ResurrectPlayer(pct, applySickness)`** removes the ghost aura, sets `Alive`, restores
  `pct` of health/mana, zeroes the death timer and casts 15007 when `applySickness` (level-scaled);
  `SpawnCorpseBones()` removes a corpse if one exists. No durability loss is applied by it.
- **The kit's `entry` anchor exists on every room chunk** and is "the nearest walkable cell centre
  to the block centre" — one cell off the socket track on both axes, 3 yd decor clearance, inside the
  12-yd spawn circle (nearest mob ≥ 6.1 yd). The flat `AnchorsFor` list carries it at index 0 by
  emission order only; a typed decoder is needed.
- **First free GameObject ids outside the kit-prop sub-block: 910058 (altar), 910059 (barrier)**;
  new rows in 910034–910099 must live in `mod_pdungeon_templates_fix.sql` (its header rule) with the
  explicit DELETE list extended; its gap comment says 910078 where 910077 is also free.
- **Altar model**: displayId **7355** `Altar01.m2` (generic stone altar, ~2×4 yd, collision model
  present in `GameObjectModels.dtree`); the v1 shrine's 6691 is a portal FX without collision.
- **Run state is memory-only** (no `Load`/`GetSaveData` on v2); a restart rebuilds from the plan.
- **"Every fifth chain index"** is a pure read of `plan.blocks`: `chainIndex >= 0 && chainIndex % 5
  == 0` gives the entrance plus 5, 10, …; pockets (`chainIndex −1`) never carry one. Live default
  (chain 4): the entrance altar only; at the cap (chain 15): chain 0, 5, 10.

---

## 1. Decisions

| # | Topic | Decision |
|---|---|---|
| 1 | Death path | `PDv2InstanceScript::OnUnitDeath(Unit*)` (override of `ZoneScript`) records a **pending respawn** for a dying player: `_pendingRespawn[playerGuid] = now`. The next 1 Hz `Update` tick performs it: `ResurrectPlayer(1.0f, true)` (full health, level-scaled sickness), `SpawnCorpseBones()`, `TeleportTo(map, altar x, y, z + 2, 0)`, a notice. No durability loss. |
| 2 | Release race | `OnPlayerCanRepopAtGraveyard` on the module's existing `PlayerScript` returns **false** while the player is on the PDv2 map and has a pending respawn (the tick resurrects them within a second); otherwise true. The logout-while-dead and `InstanceMap::Reset` paths keep the core's behaviour (the window is ≤ 1 s and the instance's `EvictDisconnected` already owns socket loss). |
| 3 | Respawn point | The **bound altar's room**: the player lands on that room's `entry` anchor (kit-guaranteed walkable, decor keeps 3 yd clear). Before any altar is bound: the **entrance room's** `entry` anchor. The fall catcher keeps its block-centre landing. |
| 4 | Altar spot | One cell off the `entry` anchor, the first **walkable, non-track** neighbour cell centre in the fixed order N, W, E, S (checked on the instance's walk grid); no such cell → no altar in that room (logged, never a fallback onto the track). |
| 5 | Which rooms | `IsAltarRoom(block)`: `chainIndex >= 0 && chainIndex % PD_ALTAR_EVERY_N_ROOMS == 0`, `PD_ALTAR_EVERY_N_ROOMS = 5` (a `constexpr` in `PDBlockPlan.h`, harness-pinned). The entrance always qualifies; a boss room at a multiple of 5 keeps its altar (a checkpoint before the fight). |
| 6 | Binding | Clicking binds `_boundAltar[playerGuid] = altarIndex`; memory-only, per player GUID, reset with the run (`DespawnAll` + rebuild). Announced through `PDv2UILink::SendNotice`. Altars never despawn on use. |
| 7 | The object | `gameobject_template` **910058**, `type 10` GOOBER, `displayId 7355`, name `Altar of Return`, `size 1`, `ScriptName go_pdungeon_altar` — a row in `mod_pdungeon_templates_fix.sql` (INSERT + DELETE list + the gap comment corrected to 910077); `PDDefines.h` gets `GO_ALTAR = 910058` and `GO_BARRIER = 910059` reserved. Summoned with `respawnTime 0` and deleted by `DespawnAll` like the decor (`go->Delete()`). |
| 8 | Typed anchors | New header-only `src/generator/PDv2SpawnAnchors.h`: `SpawnAnchor {u, v, role}`, `RoomAnchors {hasEntry, entry, hasBoss, boss, spawns}`, `bool DecodeRoomAnchors(std::string const& json, RoomAnchors& out)` — a scanner over the kit's `{"entry":{…},"boss":{…}|null,"chest":…,"spawns":[{…,"role":"…"}],"props":[…]}`. `PDv2Mgr` decodes it beside the flat list (`_chunkRoomAnchors`, `RoomAnchorsFor(chunkId)`). The flat list and its decor clearance are untouched. B2 reuses this header for spawn placement. |
| 9 | Harness | `IsAltarRoom` counted on generated plans (entrance always; count = `floor((L−1)/5) + 1`); every room-role chunk in `kit_meta.json` decodes an `entry` on a walkable cell and equal to `AnchorsFor[0]`; a pin of the decoded `RoomAnchors` of chunks 12015 and 12215 (captured by running). |

Not in this round: a `graveyard_zone` row; durability loss; per-account persistence of the binding;
an altar in pocket rooms.

---

## 2. Data flow

1. **Build** (`OnPlayerEnter`, inside `if (!_spawned)`, after `SpawnDeadEndChests`): `SpawnAltars(plan)`
   walks `plan.blocks`, keeps `IsAltarRoom` blocks in chain order, resolves each room's `entry` from
   `RoomAnchorsFor(chunkId)`, finds the altar cell (decision 4) on `GetWalkGrid()`, summons 910058 at
   its centre facing the entry anchor, records `_altars.push_back({chainIndex, entryWorld, altarGuid})`
   and `_altarByGuid[guid] = index`. `_boundAltar.clear()`, `_pendingRespawn.clear()`.
2. **Click** (`go_pdungeon_altar::OnGossipHello`): `dynamic_cast<PDv2InstanceScript*>(go->GetInstanceScript())`
   → `BindAltar(player, go->GetGUID())` → notice `Altar bound. If you fall, you return here.`; a
   second click on the same altar answers `This altar is already yours.` Returns true.
3. **Death** (`OnUnitDeath`): players only; `_pendingRespawn[guid] = GameTime::GetGameTimeMS()`.
4. **Tick** (`Update`, 1 Hz branch, after `CatchFallers`): for every pending entry whose player is still
   in the instance and dead → resurrect, bones, teleport to `RespawnPointFor(player)` (bound altar's
   `entryWorld`, else the entrance altar's), notice `You return to the altar, weakened.`; a player who
   is gone or already alive is dropped from the map.
5. **Release veto** (`PlayerScript::OnPlayerCanRepopAtGraveyard`): on the PDv2 map with a pending
   respawn → false.
6. **Teardown**: `DespawnAll` deletes the altar GOs with the decor; `_altars`, `_altarByGuid`,
   `_boundAltar`, `_pendingRespawn` cleared with the rest of the run state.

---

## 3. Acceptance

Offline (T1): fresh `pdblock` — `--batch 500` (altar-room rule, typed-anchor checks and the anchor
pin green), `--decor-batch 3000`, `--roomcap 3000`; worldserver compiles; codestyle clean; the SQL
row verified read-only (id free, displayId resolves, the stock precedent query for 7355).

In game (T2, the round's operator document): an altar stands in the start room and in every fifth
chain room, one cell beside the room's centre line, click → "Altar bound"; dying returns the player
alive at the bound altar (or the entrance) with resurrection sickness, within a second, without a
release dialog that leads anywhere; pressing release in that second does nothing but wait; never
Westfall; the fall catcher still returns to the entrance.
