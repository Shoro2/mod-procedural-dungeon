# PDv2 Round C / C7–C8 — gate progress, cleared rooms, raid warnings; Chromie, the cache, the portal — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The HUD shows the next sealed gate's kill progress, the map paints cleared rooms green, every notice appears as a raid warning; and when the last boss dies Chromie appears, speaks three lines, a reward cache stands beside her and a portal to Azealia opens.

**Architecture:** Server side, the run tick (`R`) grows three trailing fields and a new `K` message carries cleared room blocks (both budget-checked like `M`); the addon parses tolerantly, keeps a cleared set for `BuildMap`, and routes `N` through `RaidNotice_AddMessage`. The finale is a small state machine in the instance script's 1 Hz branch (`_finale`), driven from `FinishRun`; Chromie has no script (the instance script makes her speak by GUID), the portal is a `GameObjectScript` in the existing `PDExitObjects.cpp`, the cache is a plain lockable chest. All three are summoned into the run's own GUID lists and go down with the run.

**Tech Stack:** C++17 module, AIO Lua addon (`lua_scripts/flpdui.lua`, 3.3.5a client API), module SQL.

**Spec:** `docs/superpowers/specs/2026-09-08-pdv2-round-c-design.md` §2 C7, C8; §1 "Run end", "UI link". Research: `.superpowers/sdd/c-research-chest-altar-finale-ui.md` §3 (finale, Azealia, scaffolds) and §4 (UI link, addon rendering, the scope of the three features).

---

## Global Constraints

Same as the C1–C3 plan (branch, commits, LF, gates, staged worldserver, read-only DB). Additionally: this plan depends on plan C5–C6 Task 1 having landed (`_roomBX`, `_roomBY`, `_roomSpot`, `_checkpointRoom` exist). No new `.cpp` (the portal script lives in `src/PDExitObjects.cpp`), so no re-configure. Addon texts and notices are English. `flpdui.lua` is deployed by copying the module file over the deployed one (find it under `C:\wowstuff\dcore\lua_scripts\` — the deployed copy is content-identical to the module's today, CRLF only); the server reloads it on the operator's `.reload eluna` (or restart) and the client on `/reload` — both go into runde29.

---

## File Structure

**Modified (server):** `src/PDv2UILink.cpp` (`SendRunTick` :508-561, new `SendCleared`, `OnInstanceTick` :586-613, HELLO push), `src/PDv2UILink.h`, `src/PDv2InstanceScript.h/.cpp` (`NextClosedBarrier`, `ClearedRoomBlocks`, `_finale`, `StartFinale`, `TickFinale`), `src/PDDefines.h` (`NPC_CHROMIE`, `GO_AZEALIA_PORTAL`, `GO_REWARD_CHEST`), `src/PDExitObjects.cpp` (`go_pdungeon_azealia_portal`).
**Modified (addon):** `lua_scripts/flpdui.lua` (`ParseRun`, `RenderCounts`, `BuildMap`, the `K` and `N` handlers) + the deployed copy.
**Created (data):** `data/sql/db-world/mod_pdungeon_chromie.sql` (creature template 910550, loot 910068).
**Modified (data):** `data/sql/db-world/mod_pdungeon_templates_fix.sql` (GO 910067, 910068 rows, DELETE list, `UPDATE` for the chest data).
**Docs:** `CLAUDE.md`, `README.md`, runde29, vault (`06-custom-ids.md`, MIG-017, queue, log).

---

### Task 1: Gate progress and cleared rooms on the wire (C7 server)

**Files:**
- Modify: `src/PDv2InstanceScript.h/.cpp`, `src/PDv2UILink.h/.cpp`

**Interfaces:**
- Consumes: `_barriers` (`{segment, guid, cells, open, hinted}`), `_segmentPlanned/_segmentKilled`, `PDv2Config::barrierPct`, `_roomAlive`, `_roomBX/_roomBY` (plan C5–C6), `PD_GAME_MANIFEST_BUDGET_B`, `SendAddonWhisper`, `PlanOwnerFor`.
- Produces:
  ```cpp
  // PDv2InstanceScript
  bool NextClosedBarrier(uint32& planned, uint32& killed, uint32& pct) const; // lowest sealed segment; false = none sealed
  void ClearedRoomBlocks(std::vector<std::pair<int, int>>& out) const;         // (bx, by) of every room with _roomAlive == 0
  uint32 RoomsClearedCount() const;                                            // _run.roomsCleared
  // PDv2UILink
  void SendCleared(Player* player, PDv2InstanceScript const* script);          // "K bx,by;bx,by;..." (may be "K " when none)
  ```
  Wire: `R` gains ` segPlanned segKilled segPct` after `state` (`0 0 0` when no gate is sealed); `K` is sent on HELLO/enter after `M`, and from `OnInstanceTick` whenever `RoomsClearedCount()` differs from the value last sent to that player.

- [ ] **Step 1:** `NextClosedBarrier`: iterate `_barriers` in ascending `segment`, first with `!open` → `planned = _segmentPlanned[seg]`, `killed = _segmentKilled[seg]`, `pct = planned ? std::min<uint32>(100, killed * 100 / planned) : 100`. `ClearedRoomBlocks`: for `r` in `_roomAlive` with value 0 and `r < _roomBX.size()` push `{_roomBX[r], _roomBY[r]}`.
- [ ] **Step 2:** `SendRunTick`: append `<< ' ' << segPlanned << ' ' << segKilled << ' ' << segPct` (zeros when `NextClosedBarrier` is false or the script is null). `SendCleared`: build `"K "` + `bx,by;` per pair, budget-check against `PD_GAME_MANIFEST_BUDGET_B` like `SendMap` (log and skip when over). Send it right after `SendMap` on HELLO and after a GEN; in `OnInstanceTick` keep a per-player `lastClearedSent` (a small map keyed by player GUID inside the link, cleared on link-state change) and resend when the count changed.
- [ ] **Step 3:** Build, stage, md5; codestyle; commit `feat(ui-link): gate progress on the run tick, cleared rooms as a K message`.

---

### Task 2: The addon — gate line, green rooms, raid warnings (C7 client)

**Files:**
- Modify: `lua_scripts/flpdui.lua` (`ParseRun` ~:130-150, `RenderCounts` :600-610, `BuildMap` :540-580, the dispatcher :755-770), the deployed copy

- [ ] **Step 1: Tolerant parse.** `ParseRun` reads three optional trailing numbers (`segPlanned, segKilled, segPct`; default 0). A new `ParseCleared(body)` returns a set `cleared["bx,by"] = true` (empty body → empty set).
- [ ] **Step 2: Gate line.** Add a `hudGate` FontString under `hudCounts` (same font, size, anchoring pattern as `hudCounts` :482). `RenderCounts` sets it: `segPlanned > 0` → `string.format("Gate |cffffffff%d/%d|r  (%d%%)", segKilled, segPlanned, segPct)`, else `"Gate |cff00ff00open|r"`.
- [ ] **Step 3: Green rooms.** Keep `clearedSet` module-local; in `BuildMap`'s non-corridor branch pick the colour: `clearedSet[b.bx .. "," .. b.by]` → `{0.20, 0.75, 0.30}` for `R`, `{0.10, 0.50, 0.20}` for `B`; the `K` handler stores the set and calls `BuildMap(mapData)` when `mapData` exists.
- [ ] **Step 4: Raid warning.** The `N` handler: `RaidNotice_AddMessage(RaidWarningFrame, body, ChatTypeInfo["RAID_WARNING"])` and the existing chat line.
- [ ] **Step 5: Deploy the addon copy** (`copy` over the deployed file, preserving its CRLF if that is how it is stored; show the diff is content-only), note the reload steps in the report. Commit `feat(addon): gate progress line, cleared rooms in green, notices as raid warnings`.

---

### Task 3: The finale's data — Chromie, the cache, the portal (C8 data)

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_chromie.sql`
- Modify: `data/sql/db-world/mod_pdungeon_templates_fix.sql`, `src/PDDefines.h`

- [ ] **Step 1: Creature 910550.** Copy the column shape of the module's existing custom creature insert (`npc_pdungeon_entrance` 910510 — find its SQL file with `grep -rn 910510 data/sql`), then:

```sql
DELETE FROM `creature_template` WHERE `entry` = 910550;
INSERT INTO `creature_template` (`entry`, `modelid1`, `name`, `subname`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `unit_flags`, `unit_class`, `ScriptName`, ...)
VALUES (910550, 10008, 'Chromie', 'Timewalker', 80, 80, 35, 0, 2 | 512, 1, '', ...);
-- unit_flags 2 = UNIT_FLAG_NON_ATTACKABLE, 512 = UNIT_FLAG_IMMUNE_TO_PC; faction 35 = friendly to all
```

(use the exact column list the copied insert uses; every other column its default). Loot for the cache:

```sql
DELETE FROM `gameobject_loot_template` WHERE `Entry` = 910068;
INSERT INTO `gameobject_loot_template` (`Entry`, `Item`, `Reference`, `Chance`, `QuestRequired`, `LootMode`, `GroupId`, `MinCount`, `MaxCount`, `Comment`) VALUES
(910068, 920100, 0, 100, 0, 1, 0, 3, 5, 'PD finale cache - Forgotten Shard (placeholder)'),
(910068, 920101, 0, 100, 0, 1, 0, 2, 4, 'PD finale cache - Forgotten Sliver (placeholder)'),
(910068, 920102, 0, 100, 0, 1, 0, 1, 2, 'PD finale cache - Forgotten Fragment (placeholder)'),
(910068, 920103, 0, 100, 0, 1, 0, 1, 1, 'PD finale cache - Forgotten Core (placeholder)'),
(910068, 920104, 0, 100, 0, 1, 0, 1, 1, 'PD finale cache - Forgotten Relic (placeholder)');
```

- [ ] **Step 2: The two GOs** in `mod_pdungeon_templates_fix.sql` (DELETE list + rows + trailing UPDATE):

```sql
(910067, 10, 9041, 'Portal to Azealia', 1, 0, 0, 'go_pdungeon_azealia_portal'),
(910068, 3, 259, 'Chromie''s Cache', 2, 57, 910068, ''),
...
UPDATE `gameobject_template` SET `Data2` = 0, `Data3` = 1 WHERE `entry` = 910068;
```

- [ ] **Step 3:** `PDDefines.h`: `NPC_CHROMIE = 910550`, `GO_AZEALIA_PORTAL = 910067`, `GO_REWARD_CHEST = 910068` beside `GO_ALTAR`/`GO_BARRIER`. Read-only check that 910550/910067/910068 are free today. Commit `feat(data): Chromie, the finale cache and the Azealia portal templates`.

---

### Task 4: The finale (C8 engine)

**Files:**
- Modify: `src/PDv2InstanceScript.h/.cpp` (`FinishRun` :646-706, the 1 Hz branch :2582-2613, rebuild :249, `DespawnAll`), `src/PDExitObjects.cpp`

**Interfaces:**
- Consumes: `_roomSpot[_checkpointRoom]` (the last boss's arena centre — `FinishRun` runs after `OnMobDied` moved the checkpoint), `SummonCreature`/`SummonGameObject` patterns (`:795-810`, `:1976`), `_spawnedGuids`/`_decorGuids`, `Unit::Say(std::string_view, Language, WorldObject const*)`.
- Produces:
  ```cpp
  struct Finale { bool active = false; uint32 step = 0; uint32 nextAtMs = 0; ObjectGuid chromie; float x = 0.f, y = 0.f, z = 0.f; };
  Finale _finale;
  void StartFinale();      // from FinishRun
  void TickFinale();       // 1 Hz branch
  ```

- [ ] **Step 1: `StartFinale`** (called at the end of `FinishRun`): `x,y,z` = `_roomSpot[_checkpointRoom]` (if `_checkpointRoom < 0` use the entrance — log it); Chromie at `(x + 6, y, z)` facing `(x, y)` (orientation `atan2(y - cy, x - cx)` via `Position::GetAngle`), `SummonCreature(NPC_CHROMIE, Position(...))`, `SetDisableGravity(true)` like every summon here, push the GUID into `_spawnedGuids`; the cache at `(x + 6, y + 4, z)` with orientation towards the centre, `SummonGameObject(GO_REWARD_CHEST, …)` into `_decorGuids`; `_finale = { true, 0, getMSTime() + 4000, chromieGuid, x, y, z }`.
- [ ] **Step 2: `TickFinale`** in the 1 Hz branch after `HintBarriers()`: when `_finale.active && getMSTime() >= _finale.nextAtMs`: step 0..2 → `Creature* c = instance->GetCreature(_finale.chromie)`; `c->Say(LINE[step], LANG_UNIVERSAL)` with

```cpp
        char const* const CHROMIE_LINES[3] = {
            "Well, that took you long enough! The timeways are humming again.",
            "Take what the Depths owe you - you have earned every bit of it.",
            "When you are ready, step through. Azealia is waiting."
        };
```

`nextAtMs += 4000`, `++step`; step 3 → `SummonGameObject(GO_AZEALIA_PORTAL, x - 6, y, z, orientation towards the centre)` into `_decorGuids`, notice to every player "A portal to Azealia opens.", `_finale.active = false`. A missing Chromie (despawned) ends the finale early with a LOG_WARN.
- [ ] **Step 3: Teardown.** The rebuild branch resets `_finale = Finale{}`; `DespawnAll` already walks both GUID lists. A completed run re-entered later (B0 rule: same seed re-populates) starts fresh — no special case.
- [ ] **Step 4: The portal script** in `src/PDExitObjects.cpp`:

```cpp
// Round C / C8: the way home after the last boss. Click, not automatic - the
// cache beside Chromie is looted first (operator decision 2026-09-08).
class go_pdungeon_azealia_portal : public GameObjectScript
{
public:
    go_pdungeon_azealia_portal() : GameObjectScript("go_pdungeon_azealia_portal") { }

    bool OnGossipHello(Player* player, GameObject* /*go*/) override
    {
        // game_tele 20040 `flraidazealia` (acore_world), map 727 = Azealia.
        player->TeleportTo(727, 13611.1f, 13655.7f, 9.87548f, 4.7776f);
        return true;
    }
};
```

registered in `AddPDExitObjectScripts()`.
- [ ] **Step 5:** Build, stage, md5; codestyle; commit `feat(v2): the finale - Chromie speaks, the cache stands, the portal to Azealia opens`.

---

### Task 5: Docs, gates, operator document, vault

- [ ] Gates (harness — no draw touched, no pin may move; worldserver from the tip), codestyle; `CLAUDE.md` rows (UI link messages `K` + `R` fields, the finale, `PDExitObjects.cpp` portal), `README.md` finale sentence.
- [ ] `tools/pd_testlauf_runde29.md` §C7 (the Gate line counts up and reads `open` when the portcullis falls; cleared rooms turn green on the map, boss rooms darker; every notice appears top-centre as a raid warning AND in chat; reload steps: `.reload eluna` on the server console — or the restart — and `/reload` in the client), §C8 (last boss dies → Chromie 6 yd from the arena centre, three lines 4 s apart, the cache beside her opens once with the five mats, the portal appears after the third line and a click lands at Azealia's arrival spot; *Falls doch*: which of the four objects is missing, the server log lines).
- [ ] Vault (`share-public` `main`, one commit): `06-custom-ids.md` rows (NPC 910550, GO 910067, 910068 in the v2 sub-block; the fix file's DELETE list extended), MIG-017 (new `mod_pdungeon_chromie.sql`, the fix file, the addon deploy = `lua_scripts/flpdui.lua` to the host's mod-ale/AIO dir per the ledger's Lua rule), queue row (C7/C8 T1), `claude_log.md` END; runbook check 0 errors.

---

## Self-review

- Spec coverage: C7.1 → Tasks 1–2; C7.2 → Tasks 1–2; C7.3 → Task 2; C7.4 → Task 2 Step 5; C8.1 → Tasks 3–4; C8.2 → Task 4; C8.3 → Tasks 3–4; C8.4 → Task 4 Step 3; C8.5 → Tasks 3 + 5.
- Types: `NextClosedBarrier`, `ClearedRoomBlocks`, `RoomsClearedCount`, `SendCleared`, `Finale`, `StartFinale`, `TickFinale`, `NPC_CHROMIE`, `GO_AZEALIA_PORTAL`, `GO_REWARD_CHEST` as declared; `_roomBX/_roomBY/_roomSpot/_checkpointRoom` from plan C5–C6.
- Placeholders: none — the loot rows are the placeholder content by decision, the texts are final.
