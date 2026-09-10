# PDv2 Round E / WP8 — the nine Runde-31 findings — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks 1–4 are independent (different repos/files) and run in parallel, one Opus agent each; the planner reviews (Sonnet, short), does the docs, the cache-version bump, the install and the deploys.

**Goal (operator's in-game verdict, 2026-09-10 evening, verbatim in `.superpowers/sdd/wp8-recon.md`):** (1) Shifting Cache not clickable; (2) mats must be what the Endless Storage stores; (3) cut gems drop; (4) Shadowfang mobs keep their stock loot; (5) the boss-gate line on the HUD "does not update"; (6) event NPC and bosses should face the entrance; (7) no progress bar for the event; (8) when the event is won a small chest spawns where the NPC stood and the NPC despawns; (9) *Restless Echoes* moves behind *Enduring Magic*.

**What the recon settled (read `wp8-recon.md` first):** (1) is the client's stale `gameobjectcache.wdb` (lock 0 cached before the 2026-09-08 fix) — a cache-version bump by the planner, no code. (2)+(3) are one change: the storage predicate is `(class 7 or 3) and stackable > 1` (+ recipes, food) and the MATS pool includes 591 cut gems (stack 1) — the pool is regenerated FROM that predicate. (5) is semantics: the line shows the next closed barrier, so after a gate opens it jumps to the next segment's `0/n` and sits there while the current segment is finished — it will show the segment the player stands in. There is no automatic loot-filter → storage path (out of scope, told to the operator).

**Tech Stack:** C++17 module (`-Werror`, `codestyle-cpp.py`), module world SQL, AIO Lua (`flpdui.lua`), workspace Python script 106 (not git; vault copy), FT content JSON + Python tests.

---

## Global Constraints

- Branches: `mod-procedural-dungeon` `claude/pdv2-round-e-63ac9a2a` (HEAD `4a48b91` + this plan), `mod-forgotten-talents` same name (HEAD `21566ac`). Conventional Commits, English, comments say WHY in the house voice.
- **Never edit** `data/sql/db-world/mod_pdungeon_templates.sql` or `mod_pdungeon_phase2.sql` (their wide DELETE ranges re-apply on any byte change and wipe 910040-910099 — `mod_pdungeon_templates_fix.sql:40-80`). New GO rows go into `mod_pdungeon_event.sql` with entry-exact DELETEs.
- The `R` tick is append-only: new fields go after `eventNpcPct`; a 15-field client must still parse.
- Gates per task: PD C++ → `codestyle-cpp.py` clean + `cmake --build C:\wowstuff\dcore_bin --config RelWithDebInfo --target worldserver --parallel 4` (never unbounded; **Task 1 is the only task that builds**); FT → `python -m unittest discover -s tests` green + `--from-canonical` regenerated; script 106 → the script's own pin + a row-count check; Lua → syntax check with `luac -p` if available, else a careful read (no Lua interpreter on this box). No install, no restart, no deploy (planner).
- Every new conf key has a code default equal to the `.conf.dist` value.

---

### Task 1: PD engine — native loot off, gate line per segment, event chest in the NPC, facing the doorway

**Repo:** `mod-procedural-dungeon`. **Files:** `src/PDv2InstanceScript.h/.cpp` (`OnMobDied` `:991-1145`, `NextClosedBarrier` `:2704-2745`, `SpawnFromPlan`/`SpawnTaggedMob` `:2470-2530`/`:1696-1712`, `SpawnEventRooms` `:4362-4384`/`:4480-4493`, `CloseEvent` `:4714-4820`, `EventRoom` `.h:636-638`), `src/PDv2UILink.cpp` (`SendRunTick` `:626-706`), `src/PDv2ChestLoot.cpp` (`:382-386`, `:418-425`), `src/PDDefines.h` (`:37`, `:52`), `src/PDv2Mgr.h/.cpp` (conf), `conf/mod_procedural_dungeon.conf.dist`, `data/sql/db-world/mod_pdungeon_event.sql`.

**Interfaces:**
```cpp
// PDDefines.h
uint32 const GO_EVENT_CHEST = 910034;     // Pilgrim's Cache: the small chest the won event leaves where the host stood
// PDv2Config
bool lootNativeItems = false;             // ProceduralDungeon.V2.Loot.NativeItems: keep the creature's own item table (default off)
// PDv2InstanceScript
bool GateFieldsFor(Player const* player, uint32& planned, uint32& killed, uint32& pct, bool& open) const;   // the segment the player stands in; falls back to NextClosedBarrier when not in a room; planned == 0 → "no gate"
float FacingTowardDoorway(PlacedBlock const& block, float fromX, float fromY) const;   // angle from (fromX, fromY) to the centre of the block's doorway cells (event pocket: its single socket; chain room: SpineRunInto's socket), NormalizeOrientation; 0 when the block has no socket
Creature* SpawnTaggedMob(uint32 entry, PDv2MobData const& proto, float x, float y, float z, uint32 baseHealthOverride = 0, float orientation = 0.0f);   // orientation into SummonCreature AND SetHomePosition
```
1. **Native loot (finding 4).** In the `OnMobDied` loot branch (`else` side, before `RollBonusLoot` `:1109`): when `!cfg.lootNativeItems` — keep the gold, drop the items: `uint32 gold = creature->loot.gold; creature->loot.clear(); creature->loot.gold = gold;` (nothing has opened the corpse yet — `Unit::Kill` filled it at `Unit.cpp:14083-14092` before `JustDied`). Then the existing `InjectBossGear` for `isRunBoss` adds the gear. After the branch: `if (creature->loot.empty()) creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);` (an empty corpse must not sparkle; the Lil' Bro precedent `:1103-1107`). Comment block: why (the SFK/Scholomance tables are not FL content; mats/currency go to the bags; a boss corpse carries only the injected gear), and that gold is kept on purpose. Conf dist block after the `V2.Loot.Currency.*` keys.
2. **Gate line (finding 5).** `GateFieldsFor`: room index of the player's position (find the existing position→room helper — the `px py` fields of the tick and `_roomSegment`/`_roomIsBoss` are the vocabulary; if no helper exists, add `int RoomIndexAt(float x, float y) const` via `WorldToCell` → block → `roomOf`) → `seg = _roomSegment[room]` → `planned = _segmentPlanned[seg]`, `killed = _segmentKilled[seg]`, `pct` as `NextClosedBarrier` computes it, `open = barrier seg is open (or has no barrier)`; not in a room / boss room / seg < 1 → `NextClosedBarrier` + `open = false`. `SendRunTick` uses it per player and appends **`segOpen`** (0/1) as the 16th field after `eventNpcPct` (`:691-706`; comment the append-only rule again). `NextClosedBarrier` stays for its other callers.
3. **Event finish (finding 8).** New GO template **910034 `Pilgrim's Cache`** (type 3, displayId **10** `World\…\Chest01.mdx` — verify the three-way check and write it into the SQL comment: the DBC model path from `GameObjectDisplayInfo.dbc`, `SELECT COUNT(*) FROM gameobject_template WHERE displayId = 10 AND type = 3` as the stock precedent, presence/absence in `GameObjectModels.dtree` under `C:\wowstuff\dcore\Data\vmaps`; if 10 fails a check, take the next id from the recon's chest list that passes), size 1, Data0 57, Data1 910034, Data2 0, Data3 1, groupLootRules 0; `gameobject_loot_template` 910034 = a copy of 910030's three rows. Both with entry-exact DELETE + INSERT in `mod_pdungeon_event.sql`. `CloseEvent` Won branch: compute `o = FacingTowardDoorway(block, hostX, hostY)`, summon `GO_EVENT_CHEST` at the host's position/z with `o` (the chest sits where he stood), push to `_decorGuids`, then `host->DespawnOrUnsummon()` and clear the gossip-flag branch `:4802-4808` (it is gone); `EventRoom::chestX/Y/Z` and the chest-anchor lookup in `SpawnEventRooms` `:4374-4384` are removed (dead). Lost branch unchanged. `PDv2ChestLoot.cpp`: the entry gate `:382-386` admits `GO_EVENT_CHEST`, the dispatch `:418` sends it to `InjectShiftingCache` (same loot as a dead-end cache; say so in the comment).
4. **Facing (finding 6).** `FacingTowardDoorway`: event pocket → its single socket bit from `socketMask`; chain room → `SpineRunInto(plan, chainIndex, run)` socket; `LaneCellsForSocket(bit, cells)` → `CellCentreToWorld` of both cells → midpoint → `Position(fromX, fromY).GetAngle(mx, my)` normalised. Host: `SummonCreature(NPC_EVENT_HOST, Position(hostX, hostY, event.z, o))` + `SetHomePosition(…, o)`. Boss: in `SpawnFromPlan` for `isBossRoom && i == 0` compute `o` from the boss's own spawn point and pass it through the new `SpawnTaggedMob` parameter (everything else keeps 0). One `PDv2Debug()` line per computed facing is enough.
5. Conf dist (`V2.Loot.NativeItems`), `codestyle-cpp.py`, build. Commit `feat(run): native item loot off, gate line per segment, event chest where the host stood, host and boss face the doorway`.

- [ ] Step 1 native loot · [ ] Step 2 gate fields + tick · [ ] Step 3 chest + SQL + injection · [ ] Step 4 facing · [ ] Step 5 dist, codestyle, build, commit.

---

### Task 2: HUD — event progress bar, gate line with the open state

**Repo:** `mod-procedural-dungeon`. **File:** `lua_scripts/flpdui.lua` (`ParseRun` `:171-210`, HUD frame `:552-615`, `RenderCounts` `:761-794`, the `xpBg`/`xpFill` bar idiom `:280-290`/`:488`).

- `ParseRun`: a third optional group of one field after the event pair → `segOpen` (0 when absent, so a 15-field server still parses); keep the existing group code shape.
- Gate line: `planned == 0` → `""`; `segOpen == 1` → `"Gate open  %d/%d"`; else `"Gate %d/%d (%d%%)"` (the numbers now describe the segment the player stands in — comment it).
- Event row: replace the text-only row with a **bar in the addon's texture idiom**: `evBg` (BAR_W × 12, dark), `evFill` (time left / duration — the server sends seconds left; duration is the first non-zero value seen for this event, or `V2.Event.DurationSec` 60 as the fallback: keep `eventDurationSeen` per event, reset when `eventSec` returns to 0), a second thin fill `evHp` (4 px, under the bar, NPC health %), and the text `Hold the line m:ss  n%` centred over the bar. Idle: fill widths 1, alpha 0 on the textures, text `""`; the row keeps its reserved height so the map never jumps (`Hud:SetHeight` and the `+14` comment updated to the new row height, `hudSep` still anchors below the row).
- No behaviour change elsewhere. If `luac` is not on the box, read the diff twice for syntax; the planner deploys to `C:\wowstuff\dcore\lua_scripts\ProcDungeon\`. Commit `feat(ui): event progress bar, gate line shows the player's segment and its open state`.

- [ ] Step 1 parse · [ ] Step 2 gate text · [ ] Step 3 bar · [ ] Step 4 commit.

---

### Task 3: The MATS pool = the Endless Storage predicate

**Files (workspace, NOT git):** `C:\wowstuff\ForgottenLand2.0\scripts\106_pd_loot_pools.py` (`MATS_QUERY` `:282-306`, categories `:160-165`, `EXPECTED_MATS` `:188`; back it up as `106_pd_loot_pools.py.pre_wp8_20260910` first). **Output (git, PD repo):** `data/sql/db-world/mod_pdungeon_loot_pools.sql` (regenerated). Reference: `C:\wowstuff\dcore\lua_scripts\Storage\endless_storage_server.lua:88-99` (`IsEligible`).

- `MATS_QUERY` predicate becomes the storage's material half, verbatim: `class IN (3, 7) AND stackable > 1`, keeping the sanity filters (`entry < 56000`, `(Flags & 0x10) = 0`, `Quality > 0`, `displayid > 0`, the name regex). Recipes (class 9) and food (0/5) are storable but are NOT mats — left out on purpose; say so in the script header, with the Lua line reference, so the next reader sees the single source of truth. Cut gems (class 3, stack 1) vanish; raw gems (stack 20) stay in the `gem` category (weight 20 unchanged).
- Re-pin `EXPECTED_MATS` to the measured count (expected 1125 − 591 = 534; measure, do not assume). Run the script exactly as the loot notes describe (`docs/superpowers/notes/round-e-loot-sql/` and the script's docstring: DSN from `worldserver.conf`, password via `MYSQL_PWD`, never on argv), regenerate the SQL, report the per-category counts before/after and confirm zero class-3 rows with `stackable = 1` remain (a read-only SQL against the generated file's values or the live table after the planner's boot).
- Commit the regenerated SQL in the PD repo: `feat(loot): MATS pool follows the Endless Storage predicate - no cut gems`. Copy the script to `C:\Users\Anwender\Documents\GitHub\share-public\python_scripts\pdv2-loot\` is the planner's vault duty — do not touch the vault.

- [ ] Step 1 backup + predicate + header · [ ] Step 2 run, re-pin, regenerate · [ ] Step 3 counts in the report, commit the SQL.

---

### Task 4: FT — *Restless Echoes* behind *Enduring Magic*

**Repo:** `mod-forgotten-talents`. **Files:** `content/extra_nodes.json` (node 2001), regenerated artefacts via `python tools/import_ebonhold.py --from-canonical --output-root .`, `tests/test_generated_content.py` (max depth 27 → **26**), `tests/test_extra_nodes.py` (2001: parents `[933]`, position `(-1470, 990)`, depth 10, `progress_pct` 100), any test pinning node 933's progress (100 → 90).

- Node 2001 keeps id, tag 76001, icon 221, name, description, `cost_mult` 200, ranks; only `parents` → `[933]` (*Enduring Magic*, the chain end at (−1350, 870), depth 9) and `x/y` → `(-1470, 990)` (the chain's own (−120, +120) step; verify it is free — it was at recon time). Content version stays 4 (a content move, not a format change). `maximum_power` unchanged; `expected` counts unchanged.
- Gates: regenerate (only 2001's row, 933's `progress_pct`, and the depth/progress fields of nothing else should change — put the `git diff --stat` and the changed values in the report), suite green (54 today), idempotent second run. Commit `feat(content): Restless Echoes hangs off Enduring Magic (933) - the deepest end of the self-buff branch`.

- [ ] Step 1 contract edit · [ ] Step 2 regenerate + tests · [ ] Step 3 commit.

---

### Task 5 (planner): reviews, cache bump, docs, install, deploys, vault

- [ ] Sonnet reviews of Tasks 1–4; fixes by the same implementers.
- [ ] `C:\wowstuff\dcore\configs\worldserver.conf:1013` `ClientCacheVersion = 18` (backup; host via the MIG) — fixes finding 1 for every client on next login; note the trap in `CLAUDE.md`/`README.md` (any `gameobject_template` change to a seen entry needs a cache bump).
- [ ] Docs: PD `CLAUDE.md` row + `README.md` (native loot off, gate semantics, event chest, facing, MATS predicate, the cache trap), FT `log.md`; loot notes (`docs/superpowers/notes/round-e-loot-sql/`) get the predicate; spec status line.
- [ ] Install (backup `worldserver.exe.pre_roundE_wp8_20260910`), conf key appended, restart, boot log (`mod_pdungeon_event.sql` + `mod_pdungeon_loot_pools.sql` re-applied, MATS count in the loot line), deploy `flpdui.lua` + FT addon (loose), patch-9 rebuild when the client is closed; runde31 §10; vault (queue, log END, 06 GO 910034, script copy); memory.
