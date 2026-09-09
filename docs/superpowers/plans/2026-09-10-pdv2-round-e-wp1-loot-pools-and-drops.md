# PDv2 Round E / WP1 — loot pools, five currencies, materials, gear, legacy rares — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. One Opus agent per task, small and targeted; the planner reviews between tasks (no review loops, Sonnet for the short reviews).

**Goal:** A PDv2 run drops the five Round-E currencies and random materials from every mob, WotLK gear from chests and bosses by pool and dungeon level, and the old Azealia-Underground rares from Chromie's Cache — all data-driven from two new world tables generated out of the world DB.

**Architecture:** A workspace script (106) resolves six loot pools from `acore_world` into a module SQL file; a new `PDv2LootMgr` loads them at startup and owns every roll (core `urand`, never `PDRandom`); the existing kill funnel `OnMobDied` grows the per-player currency/material rolls and the boss corpse injection; a new `AllGameObjectScript` injects chest gear at `GO_ACTIVATED`; the pure math (room factor, counts) lives in `PDv2GameMath.h` and is pinned by the `pdblock` harness.

**Tech Stack:** C++17 AzerothCore module (`-Werror`, codestyle), MySQL 8.4 world SQL (DELETE-before-INSERT, updater-applied), Python 3 workspace script, `pdblock` harness (MSVC `cl`).

**Spec:** `docs/superpowers/specs/2026-09-10-pdv2-round-e-loot-talents-design.md` §1.1–1.4, §2 D1–D11, §3 L1–L5. Research SQL: `docs/superpowers/notes/round-e-loot-sql/`.

---

## Global Constraints

- Branch `claude/pdv2-round-e-<sessionId>` off `main` (`d47e015`) in `mod-procedural-dungeon`; Conventional Commits, English, LF; one commit per task.
- **The determinism boundary** (`PDv2InstanceScript.cpp:609-618`): every loot roll uses `urand`; nothing loot-related touches `PDRandom` or the seeded draw.
- Pre-commit gates every task that touches `src/`: `apps/codestyle/codestyle-cpp.py` clean; `pdblock --batch 500`, `--decor-batch 3000`, `--roomcap 3000` = 0 failures (harness build line in `CLAUDE.md`); staged worldserver build + install (`cmake --build C:\wowstuff\dcore_bin --config RelWithDebInfo --parallel` then `--install`), md5 of the new binary noted, boot-log diff against the 11-line baseline. **New `.cpp` files ⇒ re-run cmake configure first.**
- Read-only DB during development: script 106 only SELECTs; the module SQL is applied by the updater on the staged server's restart, never by hand against production.
- Every new conf key has a code default equal to its `.conf.dist` value.
- Ids: items **920105–920109** only; no new GO/NPC in this package.
- Docs land in the same commit as the code they describe (`CLAUDE.md`, `README.md`, the vault files named in Task 8).

---

## File Structure

**Created (workspace, not git — copy to the vault):** `C:\wowstuff\ForgottenLand2.0\scripts\106_pd_loot_pools.py` (+ backup copy `share-public/python_scripts/pdv2-loot/106_pd_loot_pools.py`).
**Created (module data):** `data/sql/db-world/mod_pdungeon_loot_pools.sql` (generated: two CREATE TABLEs, six pools, nine bonus rows), `data/sql/db-world/mod_pdungeon_currency.sql` (five item rows), `data/sql/db-world/mod_pdungeon_chromie_loot_e.sql` (final-cache filler chances).
**Created (module src):** `src/PDv2LootMgr.h`, `src/PDv2LootMgr.cpp` (pools, rolls, class filter, boot line), `src/PDv2ChestLoot.cpp` (the `AllGameObjectScript` + `PDv2ChestData`).
**Modified:** `src/generator/PDv2GameMath.h` (room factor, loot count, mats count), `tests/blockplan_harness.cpp` (pins), `src/PDv2Mgr.h/.cpp` (conf), `conf/mod_procedural_dungeon.conf.dist`, `src/PDv2InstanceScript.h/.cpp` (run-state fields, `OnMobDied`, `GrantItem`, boss injection, `SpawnFromPlan` freeze), `src/PDv2Commands.cpp` (`info` loot line), `src/mod_procedural_dungeon_loader.cpp` (`AddPDv2LootScripts`), `CLAUDE.md`, `README.md`.

---

### Task 1: Script 106 — the pool generator and the two tables

**Files:**
- Create: `C:\wowstuff\ForgottenLand2.0\scripts\106_pd_loot_pools.py`; copy to `C:\Users\Anwender\Documents\GitHub\share-public\python_scripts\pdv2-loot\106_pd_loot_pools.py`
- Create (generated): `data/sql/db-world/mod_pdungeon_loot_pools.sql`

**Interfaces:**
- Consumes: the resolver in `docs/superpowers/notes/round-e-loot-sql/tmpl4.sql`, the seeds `cs_p1b/gs_p1`, `_cs2c/_gs2c`, `_cs3/_gs3`, `_cs4/_gs4`, `_cs5/_gs5`, `p6b.sql`; DSN from `C:\wowstuff\dcore\configs\worldserver.conf` (`WorldDatabaseInfo`, field 4 = password, read like `q.sh`, exported via `MYSQL_PWD`, never on argv); client `C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe`.
- Produces: `python scripts\106_pd_loot_pools.py [--check] [--out <path>]` → the SQL file; `--check` prints the per-pool counts and ilvl histograms and writes nothing. Exit 1 when any pool is empty or a count deviates more than 10 % from the pinned expectations below (the script carries them as constants; update them consciously).

- [ ] **Step 1: Pool definitions as data in the script.** One dict per pool: `maps`, `supplement` (the §1.3 summoned-boss ids), `slots` (0/1/2/3 → base entry or `difficulty_entry_N`), `go_entries`, `lootmode_mask = 1`, and the gear filter (`Quality >= 4 AND InventoryType <> 0 AND class NOT IN (10, 12)`, no resilience `stat_typeN = 35`, strip 50316/50317). Pools: `HC5_EPIC` (maps 574,575,576,578,595,599,600,601,602,604,608,619,632,650,658,668; supplement 36658,35451,26529,26530,26532,31134,26668,29932,29573; slot 1; GOs 202336,193603,193996,195710,195375,195324), `RAID_N` (533,615,616,603,249,649 **and 724 slots 0+1**; supplement 33288,34564,34496,34497,34797,34780; slots 0,1; GOs 181366,193426,190663,193597,193905,193967,194158,194159,195046,195047,194312,194314,194307,194308,194789,194956,194821,194822,194324,194326,194328,194330,195631,195632), `RAID_HC` (649 slots 2,3 with the ToC supplement; **plus 724 slots 2,3**; GOs 195633,195635,195665-195672), `ICC_N` (631; supplement 36853; slots 0,1; GOs 201873,202239,201959,201874,202240,202339,202180), `ICC_HC` (631; 36853; slots 2,3; GOs 201872,202238,202338,202177,201875,202241,202340,202179), `MATS` (the `p6b.sql` census restricted to the nine core categories, `expansion` by entry band, weight 20 for `gem`, 100 otherwise). Expected counts (pins): 160 / 1 062+28 / 340+28 / 279 / 277 / 1 125.
- [ ] **Step 2: Bonus rows (L5) as a fixed table in the script**, emitted into the same file: `(1, 80095, 1, 1, 1, 1, 0, 'Lava Mammoth')`, `(2, 80088, 1, …)`, `(3, 80096, …)`, `(4, 80100, …)`, `(5, 80093, …)`, `(6, 251000, 2500, 1, 1, 80, 0, 'Expert Emblem')`, `(7, 80004, 500, 1, 1, 100, 0, 'Exobeast energy plate')`, `(8, 251001, 5000, 1, 1, 26, 1, 'armor Mystery Box (+armour type)')`, `(9, 251005, 2500, 1, 1, 26, 0, 'Jewelry Mystery Box')`. The script verifies each id exists in `item_template` and aborts otherwise.
- [ ] **Step 3: Emit the SQL.** Header comment (generator name, date, host, per-pool count + ilvl histogram, the six schema facts in one line each); the two `CREATE TABLE IF NOT EXISTS` from the spec §3 L1 verbatim; per pool `DELETE FROM `pdungeon_loot_pool` WHERE `pool` = 'X';` then 100-row `INSERT INTO … VALUES` chunks with `comment` = `"<name> ilvl <n>"`; `DELETE FROM `pdungeon_loot_bonus` WHERE `id` BETWEEN 1 AND 9;` + one INSERT. 4-space indent, backticks, semicolons, LF. The file must be **byte-identical across two runs** (sort by item id; no timestamps below the header — put the date in the header only, and make `--check` compare a fresh render against the file on disk, reporting `unchanged`/`changed`).
- [ ] **Step 4: Run it.** `python scripts\106_pd_loot_pools.py --check` (counts match the pins), then without `--check` → the file; run twice, diff = header date only. Load the file into a scratch schema? **No** — read-only rule; the updater applies it on the staged restart in Task 8. Sanity: `grep -c "^    (" data/sql/db-world/mod_pdungeon_loot_pools.sql` ≈ 3 100 rows.
- [ ] **Step 5: Commit** `feat(DB): loot pools generated from the world DB (script 106)` with the SQL file and a `docs/superpowers/notes/round-e-loot-sql/README.md` line pointing at the script's vault backup.

---

### Task 2: The five currency items and the final-cache filler

**Files:**
- Create: `data/sql/db-world/mod_pdungeon_currency.sql`, `data/sql/db-world/mod_pdungeon_chromie_loot_e.sql`

**Interfaces:**
- Produces: `item_template` rows 920105–920109 exactly as spec §3 L2 (names Faded/Gleaming/Radiant/Sovereign/Eternal Remnant, Quality 1..5, class 7, subclass 0, `bonding 1`, `stackable 1000`, `ItemLevel 80`, `SellPrice 0`, `BuyCount 1`, `Material 2`, `Description 'Currency of the Forgotten Depths. Spent in the Forgotten Talents tree.'`); stock `displayid`s chosen from existing gem/essence items of matching colour (pick five distinct ids by `SELECT entry, displayid FROM item_template WHERE class = 7 AND subclass = 4 AND Quality = N LIMIT 5` per quality and record the borrowed source entry in the row's comment). Same DELETE-BETWEEN + INSERT shape as `mod_pdungeon_flmats.sql`.
- Produces: `UPDATE `gameobject_loot_template` SET `Chance` = 25 WHERE `Entry` = 910068 AND `Item` IN (920100, 920101, 920102, 920103, 920104);` (filler, spec L4).

- [ ] **Step 1:** Write both files (header comments say why: D1 colours, filler role).
- [ ] **Step 2:** No hand-apply and no scratch schema (read-only rule): the Task 8 staged restart is the test — the updater applies both files and aborts the boot on a bad statement. Before that, check the five borrowed `displayid`s exist with a read-only `SELECT entry, displayid, name FROM item_template WHERE entry IN (<the five source entries>)` through `notes/round-e-loot-sql/q.sh`.
- [ ] **Step 3: Commit** `feat(DB): five Round-E currency items 920105-920109, final-cache filler at 25%`.

---

### Task 3: Pure math — room factor and counts, harness-pinned

**Files:**
- Modify: `src/generator/PDv2GameMath.h` (after `GameLootMultX100`, `:226-233`), `tests/blockplan_harness.cpp`

**Interfaces:**
- Produces (engine-free, `constexpr`):
  ```cpp
  constexpr int GameRoomFactorX100(int rooms, int baseline, int bonusPctPerRoom);  // 0 for rooms<=0; rooms*100/baseline below; 100+(rooms-baseline)*bonus at/above
  constexpr int GameScaledCount(int items, int lootMultX100, int rollPct);          // items*lootMultX100/100 integer part, +1 when rollPct(1..100) <= remainder%; rollPct==0 -> floor only
  constexpr int GameMatsMaxCount(int dlvl, int dlvlCap, int maxAtCap);              // 1 + (maxAtCap-1)*dlvl/dlvlCap, clamped [1, maxAtCap]
  constexpr int GameChanceBp(int chancePct, int roomFactorX100);                    // chancePct*100*roomFactorX100/100, clamped [0, 10000]
  ```
  `GameScaledCount` takes the roll as a parameter so the harness pins it without `urand`; the engine passes `urand(1, 100)`.

- [ ] **Step 1:** Add the four functions with the spec's comments (D8, D9, L3).
- [ ] **Step 2:** Harness pins in the existing `--batch` self-test block: `GameRoomFactorX100(1,10,1)==10`, `(9,10,1)==90`, `(10,10,1)==100`, `(15,10,1)==105`, `(0,10,1)==0`; `GameScaledCount(1,100,50)==1`, `(1,300,1)==3`, `(1,250,50)==3`, `(1,250,51)==2`, `(2,360,100)==7`; `GameMatsMaxCount(0,30,5)==1`, `(8,30,5)==2`, `(30,30,5)==5`, `(31,30,5)==5`; `GameChanceBp(100,100)==10000`, `(5,10)==50`, `(1,105)==105`, `(100,150)==10000`.
- [ ] **Step 3:** Rebuild `pdblock`, `pdblock --batch 500` → the check count grows by 19, 0 failures. Codestyle. Commit `feat(math): room factor, scaled loot count and mats count, harness-pinned`.

---

### Task 4: `PDv2LootMgr` — load the tables, own every roll

**Files:**
- Create: `src/PDv2LootMgr.h`, `src/PDv2LootMgr.cpp`
- Modify: `src/PDWorldScript.cpp` (`OnStartup` loads it after `sPDv2PackMgr`), `src/PDv2Commands.cpp` (`info` gains a `loot:` line), `src/mod_procedural_dungeon_loader.cpp` (nothing — the manager is not a script; only Task 7 adds a loader call)

**Interfaces:**
- Consumes: `WorldDatabase.Query` (startup only), `sObjectMgr->GetItemTemplate`, `PDv2GameMath.h`.
- Produces (`namespace PDungeon`, singleton `sPDv2LootMgr` like `sPDv2PackMgr`):
  ```cpp
  struct LootPoolEntry { uint32 item; uint16 weight; uint8 expansion; std::string category; };
  struct LootBonusRow  { uint16 id; uint32 item; uint32 chanceBp; uint16 minCount; uint16 maxCount; uint8 minDiff; bool armorPick; };
  struct LootBonusHit  { uint32 item; uint32 count; };
  class PDv2LootMgr
  {
  public:
      static PDv2LootMgr* instance();
      void   Load();                                             // both tables; LOG_INFO one line per pool; unknown item -> LOG_ERROR + row dropped
      uint32 PoolSize(std::string_view pool) const;
      uint32 RollGear(std::string_view pool, Player const* looter) const;   // weighted; D5 filter when cfg.lootClassFilter; empty filtered -> unfiltered; 0 = empty pool
      uint32 RollGearUnion(std::string_view a, std::string_view b, Player const* looter) const; // HC5_EPIC + RAID_N as one weighted draw
      uint32 RollMaterial(uint8 expansionMask = 0x7) const;
      std::vector<LootBonusHit> RollBonus(uint8 difficulty, int roomFactorX100, Player const* looter) const;
      static bool ItemFitsClass(ItemTemplate const* proto, uint8 classId);  // pure; D5 table
      std::string Describe() const;                              // "pools 6 - HC5_EPIC 160 - ... - bonus 9"
  };
  #define sPDv2LootMgr PDungeon::PDv2LootMgr::instance()
  ```
  `ItemFitsClass`: `AllowableClass` mask (−1 = all) AND (armour: subclass 1..4 must equal the class's type — cloth {mage, warlock, priest}, leather {rogue, druid}, mail {hunter, shaman}, plate {warrior, paladin, DK}; subclass 6 shield only warrior/paladin/shaman; subclass 0 and 7..10 pass) AND (weapon class 2: subclass ∈ the class table — Warrior {0,1,2,3,4,5,6,7,8,10,13,15,16,18}, Paladin {0,1,4,5,6,7,8}, Hunter {0,1,2,3,6,7,8,10,13,15,16,18}, Rogue {0,2,3,4,7,13,15,16,18}, Priest {4,10,15,19}, DK {0,1,4,5,6,7,8}, Shaman {0,1,4,5,10,13,15}, Mage {7,10,15,19}, Warlock {7,10,15,19}, Druid {4,5,6,10,13,15}). Armour-pick mapping for bonus rows: cloth 0 / leather 1 / mail 2 / plate 3 by the same class→type table.

- [ ] **Step 1:** Header + cpp; `Load()` with `SELECT pool, item, weight, expansion, category FROM pdungeon_loot_pool` and `SELECT id, item, chance_bp, min_count, max_count, min_diff, armor_pick FROM pdungeon_loot_bonus ORDER BY id`; totals per pool in the boot line: `PDv2 loot: HC5_EPIC 160, RAID_N 1090, RAID_HC 368, ICC_N 279, ICC_HC 277, MATS 1125 (classic 280 / tbc 325 / wotlk 520), bonus 9`.
- [ ] **Step 2:** Rolls: weighted pick = `urand(1, totalWeight)` walk (the `RollBonusLoot` idiom); the D5 filter builds a filtered index vector per call (pools are ≤ 1 100 entries — no cache needed; measure once with `getMSTime()` in a debug line, expect < 1 ms); `RollBonus`: per row `difficulty >= minDiff` then `urand(1, 10000) <= chanceBp * roomFactorX100 / 100` then `count = urand(min, max)`, `item += armourIndex` when `armorPick`.
- [ ] **Step 3:** `PDWorldScript::OnStartup`: `sPDv2LootMgr->Load();` right after the pack manager; `PDv2Commands.cpp` `info`: append `handler->PSendSysMessage("loot: {}", sPDv2LootMgr->Describe());`.
- [ ] **Step 4:** Harness: `ItemFitsClass` is pure but needs `ItemTemplate` — keep it in the engine file and pin it through a tiny table-driven self-test under `.pdungeon v2 info`? No: add `static bool FitsClassRaw(uint32 allowableClass, uint8 itemClass, uint8 subclass, uint8 classId)` (no engine types) to `PDv2GameMath.h` and pin **that** in the harness (12 cases: plate for mage → false, cloth for mage → true, shield for shaman → true, dagger for paladin → false, wand for priest → true, ring (armour 0) for anyone → true, `AllowableClass` excluding the class → false); `ItemFitsClass` is a two-line wrapper.
- [ ] **Step 5:** cmake configure (new `.cpp`), build, boot the staged server once to see the boot line (the SQL from Tasks 1–2 applies here — a bad file aborts boot; fix and re-run). Codestyle. Commit `feat(loot): PDv2LootMgr loads the loot pools and owns the rolls`.

---

### Task 5: Conf keys and `PDv2Config`

**Files:**
- Modify: `src/PDv2Mgr.h` (`PDv2Config`), `src/PDv2Mgr.cpp` (`LoadConfig`), `conf/mod_procedural_dungeon.conf.dist` (after the `V2.Loot.BonusRollPct` block, `:486-497`)

**Interfaces:**
- Produces in `PDv2Config`: `uint32 lootCurrencyItem[5] = {920105, 920106, 920107, 920108, 920109}; int lootCurrencyChancePct[5] = {100, 5, 1, 50, 10}; int lootCurrencyMinDiff[5] = {1, 1, 1, 50, 75}; int lootRoomsBaseline = 10; int lootRoomsBonusPctPerRoom = 1; bool lootExtraMobsDropCurrency = false; int lootMatsChancePct = 100; int lootMatsMaxPerMobAtCap = 5; int lootChestItems = 1; int lootBossItems = 1; int lootFinalItems = 1; int lootIccDlvl = 10; bool lootClassFilter = true;`
- Keys (`ProceduralDungeon.V2.Loot.…`): `Currency.Tier<1..5>.Item`, `Currency.Tier<1..5>.ChancePct`, `Currency.Tier4.MinDiff`, `Currency.Tier5.MinDiff`, `RoomsBaseline`, `RoomsBonusPctPerRoom`, `ExtraMobsDropCurrency`, `Mats.ChancePct`, `Mats.MaxPerMobAtCap`, `Chest.Items`, `Boss.Items`, `Final.Items`, `IccDlvl`, `ClassFilter`. Clamps: percents 0..100, counts 0..10, items ≥ 1, `IccDlvl` 0..`dlvlCap`.

- [ ] **Step 1:** Fields + loads (the `lootBonusRollPct` pattern, `PDv2Mgr.cpp:74-75`), read live like every V2 knob.
- [ ] **Step 2:** `.conf.dist` blocks in the house style (Description / Default / the "why" paragraph), defaults identical to the code.
- [ ] **Step 3:** Build; `.reload config` path unchanged. Commit `feat(conf): Round-E loot keys (currencies, materials, gear counts, class filter)`.

---

### Task 6: The kill funnel — currencies, materials, boss gear

**Files:**
- Modify: `src/PDv2InstanceScript.h` (`PDv2RunState`, `PDv2MobData`, new members), `src/PDv2InstanceScript.cpp` (`SpawnFromPlan` freeze, `OnMobDied`, new helpers), `src/PDDefines.h` (nothing)

**Interfaces:**
- Consumes: `sPDv2LootMgr`, `sPDv2Mgr->GetConfig()`, `PDv2GameMath.h` (Task 3), `MailDraft`/`MailSender` (the `MailItemOrGive` pattern from `fl-underground-dungeon/src/UndergroundUtils.h:127`), `LootStoreItem` (`LootMgr.h:141`).
- Produces:
  ```cpp
  // PDv2RunState
  uint16 roomFactorX100 = 100;   // GameRoomFactorX100(ordinary rooms, cfg) frozen at spawn
  uint8  dlvl = 0;               // the account's dlvl at spawn (materials count)
  // PDv2MobData
  bool   isExtra = false;        // WP5 event waves / WP6 respawn copies: materials yes, currency only if cfg.lootExtraMobsDropCurrency
  // PDv2InstanceScript (private)
  void RollCurrency(Creature* creature, PDv2MobData const& tag);   // T1..T3 per player on the map
  void RollMaterials(Creature* creature, PDv2MobData const& tag);  // per player on the map
  void InjectBossGear(Creature* creature, Unit* killer);            // RAID_HC x GameScaledCount into creature->loot
  void GrantItem(Player* player, uint32 item, uint32 count) const; // AddItem, else mail from NPC_CHROMIE
  static void ForEachRunPlayer(Map* map, std::function<void(Player*)> const& fn); // GetPlayers(), skipping null sessions
  ```

- [ ] **Step 1: Freeze.** In `SpawnFromPlan`, next to `_run.difficulty`/`lootMultX100` (`:1532-1534`): `_run.dlvl = account.dlvl` and, after `roomsTotal` is known (`:1670`), `_run.roomFactorX100 = GameRoomFactorX100(ordinaryRooms, cfg.lootRoomsBaseline, cfg.lootRoomsBonusPctPerRoom)` where `ordinaryRooms` = the room blocks with `!_roomIsBoss` (WP3/R2 makes `roomsTotal` mean exactly that; until then compute it here explicitly so WP1 does not depend on WP3).
- [ ] **Step 2: `OnMobDied`.** After `MarkRunDirty()` and the split-child branch (`:742-750`), for non-split creatures: `RollCurrency(creature, *tag)` when `!tag->isExtra || cfg.lootExtraMobsDropCurrency`; `RollMaterials(creature, *tag)`; `if (tag->isRunBoss) InjectBossGear(creature, killer)`. `RollBonusLoot` stays. Order: injection **before** `FinishRun()` (the corpse loot must be complete before the finale starts).
- [ ] **Step 3: `RollCurrency`.** For tiers 0..2 (T1–T3): `chanceBp = GameChanceBp(cfg.lootCurrencyChancePct[t], _run.roomFactorX100)`; `ForEachRunPlayer`: `if (urand(1, 10000) <= chanceBp) GrantItem(player, cfg.lootCurrencyItem[t], 1)`. Tiers 3..4 are **not** rolled here (final cache, Task 7).
- [ ] **Step 4: `RollMaterials`.** `if (urand(1,100) > cfg.lootMatsChancePct) return;` per player: `count = urand(1, GameMatsMaxCount(_run.dlvl, cfg.dlvlCap, cfg.lootMatsMaxPerMobAtCap))`, `item = sPDv2LootMgr->RollMaterial()`, `GrantItem(player, item, count)` (one item id per kill per player; the count is the stack).
- [ ] **Step 5: `InjectBossGear`.** `n = GameScaledCount(cfg.lootBossItems, _run.lootMultX100, urand(1,100))`; looter = `killer->GetCharmerOrOwnerPlayerOrPlayerItself()`; for `n`: `item = sPDv2LootMgr->RollGear("RAID_HC", looter)`, `if (item) creature->loot.AddItem(LootStoreItem(item, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1));`. Read `Unit::Kill` in the local core once to confirm the corpse loot is filled before `JustDied` (expected: the loot block precedes "Call creature just died function"); note the line numbers in the commit message.
- [ ] **Step 6: `GrantItem`.** `if (player->AddItem(item, count)) return;` else build a `MailDraft("The Forgotten Depths", "Your bags were full.")`, `Item::CreateItem(item, count, player)` → `draft.AddItem(mailItem)` (save the item in the transaction like the underground helper), `draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_CREATURE, NPC_CHROMIE))`, commit, and one `PSendSysMessage("Your bags are full - {} was mailed to you.", name)`.
- [ ] **Step 7:** Build, codestyle, `pdblock` gates, staged boot. Commit `feat(loot): currencies and materials per kill, boss gear in the corpse loot`.

---

### Task 7: Chest injection — Shifting Cache and Chromie's Cache

**Files:**
- Create: `src/PDv2ChestLoot.cpp`
- Modify: `src/PDv2InstanceScript.h` (`PDv2RunState const& GetRunState() const` exists — reuse; add `uint8 RunDlvl() const`), `src/mod_procedural_dungeon_loader.cpp` (`AddPDv2LootScripts()`)

**Interfaces:**
- Consumes: `AllGameObjectScript::OnGameObjectLootStateChanged(GameObject*, uint32 state, Unit*)` (`AllGameObjectScript.h:80`), `Player::SendLoot` order (`Player.cpp:7867-7930`: template fill, then `SetLootState(GO_ACTIVATED, this)`), `GO_CHEST 910030`, `GO_REWARD_CHEST 910068`, `sPDv2LootMgr`, `PDv2InstanceScript` via `map->GetInstanceScript()`.
- Produces:
  ```cpp
  struct PDv2ChestData : public DataMap::Base { bool injected = false; };
  class PDv2ChestLootScript : public AllGameObjectScript { void OnGameObjectLootStateChanged(GameObject* go, uint32 state, Unit* unit) override; };
  void AddPDv2LootScripts();
  ```

- [ ] **Step 1:** Gate: `state == GO_ACTIVATED`, `go->GetMap()->GetId() == cfg.mapId`, entry ∈ {910030, 910068}, `unit && unit->IsPlayer()`, a live `PDv2InstanceScript`, and `!go->CustomData.GetDefault<PDv2ChestData>("mod-procedural-dungeon")->injected` (then set it). Read `Player::SendLoot` once and cite in the commit message that the loot response is sent after `SetLootState` (so items added here appear in the window).
- [ ] **Step 2: Shifting Cache (910030).** `pool = run.dlvl >= cfg.lootIccDlvl ? "ICC_N" : union(HC5_EPIC, RAID_N)`; `n = GameScaledCount(cfg.lootChestItems, run.lootMultX100, urand(1,100))`; `go->loot.AddItem(LootStoreItem(item, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1))` per rolled item (looter's class for the filter).
- [ ] **Step 3: Chromie's Cache (910068).** Gear: `pool = run.dlvl >= cfg.lootIccDlvl ? "ICC_HC" : "ICC_N"`, `n = GameScaledCount(cfg.lootFinalItems, …)`. Currency T4/T5: for `t` in {3, 4}: `if (run.difficulty >= cfg.lootCurrencyMinDiff[t] && urand(1,10000) <= GameChanceBp(cfg.lootCurrencyChancePct[t], run.roomFactorX100))` → add the currency item ×1 to the loot. Bonus: `for (hit : sPDv2LootMgr->RollBonus(run.difficulty, run.roomFactorX100, looter)) go->loot.AddItem(LootStoreItem(hit.item, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, hit.count, hit.count));`. One `LOG_INFO` per cache: `PDv2 loot: final cache for account {} (diff {}, dlvl {}, rooms x{}): {} gear, T4 {}, T5 {}, bonus {}`.
- [ ] **Step 4:** Loader: `AddPDv2LootScripts()` registering the script; `mod_procedural_dungeon_loader.cpp` calls it. cmake configure (new `.cpp`), build, codestyle, staged boot. Commit `feat(loot): chest gear, final-cache currencies and legacy rares at GO_ACTIVATED`.

---

### Task 8: Docs, deploy, the operator checklist

**Files:**
- Modify: `CLAUDE.md` (Layout table: `PDv2LootMgr`, `PDv2ChestLoot.cpp`, the two tables, script 106), `README.md` (a "Loot" paragraph after the Round-D one), vault `docs/World of Warcraft/06-custom-ids.md` (`920105–920109` authored), `09-db-tables.md` (`pdungeon_loot_pool`, `pdungeon_loot_bonus`), `12-server-todo.md` (the Round-E row → "WP1 T1, T2 owed"), `claude_log.md` (END), `forgotten-land/15-host-migration-log.md` (no entry yet — merge-time)
- Create: `C:\wowstuff\ForgottenLand2.0\tools\pd_testlauf_runde31.md` (WP1 section; WP2/WP3 append theirs)

- [ ] **Step 1:** Staged deploy per the build-ready order: `cmake --build … && cmake --install …`, note the md5 of `C:\wowstuff\dcore\worldserver.exe`, keep `worldserver.exe.pre_roundE_<date>` beside it; restart the staged server (nobody online); confirm the boot lines (`PDv2 loot: …`, no updater error, the 11-line baseline unchanged); `.pdungeon v2 info` shows the `loot:` line.
- [ ] **Step 2:** runde31 WP1 section (German, the runde30 voice): what to look for — every kill drops a Faded Remnant (stack grows), the occasional Gleaming/Radiant, 1–5 materials per kill ("You receive item" lines), a Shifting Cache with a purple item of the right armour type, the boss corpse with a ToGC/RS-heroic item, Chromie's Cache with an ICC item (+ Sovereign at difficulty ≥ 50); how to force a legacy rare for the test: `.reload config` is not enough — set `chance_bp` of row 1 to 10000 in a scratch copy? **No hand SQL on the live DB** — instead the checklist says: run at difficulty 26+ and expect a Mystery Box at 50 % over two runs; mounts are not testable in one evening and say so.
- [ ] **Step 3:** Docs + vault + log entry; commit `docs(round-e): WP1 loot pools and drops - CLAUDE/README, custom ids, tables, queue, runde31`.
