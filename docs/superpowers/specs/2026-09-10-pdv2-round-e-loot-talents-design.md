# PDv2 Round E — loot, five currencies, the Forgotten-Talents purchase model, run cap, and the leftovers

**Date** 2026-09-10 · **Status** **APPROVED 2026-09-10 — the operator accepted the design and every default
in §2 ("ok machen wir so"). WP1 (L1–L5), WP3 (R1–R4) and WP2 (T1) are BUILT and T1 on the
workbench (PD `2ab16ec`, FT `4d4fc55`+, core `adec223ba`; installed, booted 15:00, patch-9 rebuilt;
runde31 §1–§6); T2 = Runde 31; WP4–WP6 not started** ·
**Branches** `claude/pdv2-round-e-63ac9a2a` in `mod-procedural-dungeon` (off the plan branch `117b629` =
`main` `d47e015` + docs), the same name in `mod-forgotten-talents`, `azerothcore-wotlk` (one
prepared-statement block) and, for WP6, `mod-paragon-itemgen` · **Plans**
`docs/superpowers/plans/2026-09-10-pdv2-round-e-*.md` (WP1–WP3 written with this spec; WP4–WP6 are written
when their package starts, as every earlier round did).

Source: the operator's list of 2026-09-10 (§0). Research (read-only, `path:line`, measured on this box):
four reconnaissance reports summarised in §1; the loot-pool SQL is preserved in
`docs/superpowers/notes/round-e-loot-sql/`. Evidence tiers are the WoW-server ladder: **T0** written ·
**T1** verified offline on this box (harness, build, boot log, SQL counts) · **T2** operator, in a real client.

This round is the operator's own "Runde E" from `tools/pd_testlauf_runde30.md` §7 ("Die Beutetabellen für
beide Kisten … sind Platzhalter — fünf FL-Materialien zu je 100 %. Das ist die Inhaltsrunde am Ende.").

---

## 0. The operator's list (verbatim intent), mapped to this document

| # | Operator (German kept where it is the decision) | Prio | Section | Package |
|---|---|---|---|---|
| 1 | Turm-Raum: Seitenwände sichtbar, nicht alles mit Häusern bedeckt | low | **K1** | WP4 |
| 2 | Übergang zwischen Bodentexturen ist eine harte Kante — verblenden | mid | **K2** | WP4 |
| 3 | Forgotten Talents = Skillbaum für das PD (überall aktiv); 5 Währungen (uncommon, common, rare, epic, legendary); Kosten steigen mit der Entfernung vom Start — hinten alle 5 (viel billig, wenig teuer), vorne nur die billige; uncommon von allen Mobs, common 5 %, rare 1 %, epic nur Endtruhe 50 % ab Stufe 50, legendary 10 % Endtruhe ab Stufe 75; Chancen gelten bei 10 Räumen, 1 Raum = 1/10, über 10 +1 %/Raum | high | **L2**, **T1** | WP1, WP2 |
| 4 | Normale Mobs droppen zufällige Materialien, 1–5 pro Mob nach Dungeon-Stufe (dlvl), aus allen Xpacs/Stufen | high | **L3** | WP1 |
| 5 | Normale Truhen: WotLK-Heroic-Dungeon-Epics / Non-ICC-WotLK-Normal-Raid-Items; ab dlvl 10 ICC 10/25 normal | high | **L4** | WP1 |
| 6 | Bosse: Non-ICC-WotLK-Heroic-Raid-Items | high | **L4** | WP1 |
| 7 | Endtruhe: zufälliges ICC 10/25; ab dlvl 10 ICC 10/25 heroic | high | **L4** | WP1 |
| 8 | Seltene Drops der alten Azealia-Underground-Solo-Instanz zusätzlich in die Endtruhe | high | **L5** | WP1 |
| 9 | Debug-Logs per Config abschalten / Log-Spam unterbinden | mid | **R3** | WP3 |
| 10 | `/pd` sagt rooms 14, Run-HUD rooms 0/15 — gleichziehen | low | **R2** | WP3 |
| 11 | Run-Difficulty freispielen: Abschluss mit Tod +3, ohne Tod +5, Start 1, accountweit | mid | **R1** | WP3 |
| 12 | Zukunft: Spieler konfiguriert den Loot selbst (2. Tab in `/pd`), nur abwärts; Classic+TBC Dungeon/Raid-Loot; Xpac der Mats wählbar, dann 50 % weniger | low | **F1** | WP7 |
| 13 | Layout-Vorschaufenster bei der Generierung | low | **R4** | WP3 |
| 14 | Neue Legendary-Node: spawnt Mobs im PD erneut (wie Dungeon Challenge); Zusatzmobs zählen nicht zu den %, droppen aber Mats / geben Paragon-XP; 2 Ränge, je +1 Spawn; hohe Kosten | low | **T2** | WP6 |
| 15 | Neue Legendary-Node (Crafting-Bereich): +2/4/6/8/10 % Cursed-Chance beim Craften; 5 Ränge; sehr hohe Kosten | low | **T2** | WP6 |
| 16 | Neue Legendary-Node (Questing-Bereich): +2/4/6/8/10 % Cursed-Chance bei Questabschluss; 5 Ränge; sehr hohe Kosten | low | **T2** | WP6 |
| 17 | Optionale Event-Räume: 25 % pro 5er-Segment, nie Verbindungsraum, eigener Eingang; freundlicher NPC in der Mitte, Gossip startet; 60 s (Timer oben) je 5 s ein Mob am Rand, 90 % Melee / 10 % Caster, greifen den NPC an; überlebt er: normale Truhe + Paragon-XP nach Loot-Multiplikator | mid | **R5** | WP5 |

---

## 1. Facts that shape the design (measured 2026-09-10)

### 1.1 What loot exists in a run today (`mod-procedural-dungeon` `main` = `d47e015`)

- **Chromie's Cache** GO 910068, summoned in `StartFinale()` (`src/PDv2InstanceScript.cpp:925-930`), type 3
  CHEST, lock 57, `Data1` = loot 910068, `Data3 = 1` (one payout). Its `gameobject_loot_template`
  (`data/sql/db-world/mod_pdungeon_chromie.sql:73-79`) is the placeholder: the five FL mats 920100–920104 at
  100 %. No C++ hand-out, no conf key, no `lootMult`.
- **Shifting Cache** GO 910030 is spawned only by `SpawnDeadEndChests()` (`src/PDv2InstanceScript.cpp:2367-2450`)
  — in corridor dead-end stubs (block centre) and loop rooms (the kit's `chest` anchor, else centre + `LOG_WARN`).
  **Ordinary rooms have no chest.** Loot 910030 = Frostweave ×3–5 100 %, Runic Healing Potion 60 %, Frozen Orb 20 %
  (`mod_pdungeon_phase2.sql:24-26`).
- **Bonus roll** `RollBonusLoot(Unit* killer)` (`:592-645`): `chanceBp = BonusRollPct × lootMultX100` in 1/10000,
  weighted pick over the hardcoded `BONUS_MATS[5]` (`:182-194`, 920100/60 … 920104/1), granted by
  `player->AddItem(entry, 1)` + a "Bonus: …" chat line. Called from `OnMobDied` (`:749`), suppressed for Lil' Bro
  children (`:742-750`). **The roll uses core `urand`, not the seeded `PDRandom` — the determinism boundary runs
  here** (`:609-618`); every new roll in this round stays on that side of it.
- **Gold × lootMult** in `PDv2LootScript::OnPlayerBeforeLootMoney` (`src/PDv2Scaling.cpp:334-373`); creature item
  loot is untouched.
- **lootMult** = `GameLootMultX100(diff, casterPct)` (`src/generator/PDv2GameMath.h:226-233`): x100 integer,
  (1,40)→100, (100,40)→300, (100,80)→360; frozen into `_run.lootMultX100` (`PDv2InstanceScript.cpp:1533`).
- Conf today: `V2.XP.PerRoom 10`, `V2.XP.PerDlvl 100`, `V2.Loot.BonusRollPct 15`, `V2.DlvlCap 30`. No other
  `V2.Loot.*`. Deployed workbench conf differs from `.dist` only in known keys (Enable, Center, ManifestPath,
  Ambush.Chance — see `C:\wowstuff\dcore\configs\modules\mod_procedural_dungeon.conf`).

### 1.2 Run lifecycle, kill funnel, tags

- `PDv2RunState` (`src/PDv2InstanceScript.h:124-141`): `killed/total`, `bossKilled/bossTotal`,
  `roomsCleared/roomsTotal`, `difficulty` (frozen dial), `lootMultX100`, `complete`, `started`. **No death counter
  anywhere.** `OnUnitDeath` (`:3295-3312`) only records `_pendingRespawn`; `RespawnPending()` (`:3314-3395`)
  resurrects at the checkpoint.
- **One kill funnel**: `PDv2MobAI::JustDied` → `PDv2InstanceScript::OnMobDied(Creature*, Unit* killer)`
  (`:647-756`): counters (inside `if (tag->countsForRun)` `:683-728`), split, barrier, then the loot branch
  (`:742-750`). **All new drops attach here.**
- Tag `PDv2MobData` (`src/PDv2InstanceScript.h:54-115`): `role` (melee/caster/boss), **`isRunBoss` is the boss
  test** (a trash stand-in can fill a boss slot), `roomIndex`, `countsForRun` (false for patrols and ambush mobs —
  "they fight, scale, split and drop loot like any dungeon mob but move no run counter"), `splitDepth`, `isPatrol`.
  Creature entry numbers say nothing about boss-ness (stock entries from `pdungeon_packs`).
- `FinishRun()` (`:758-829`): `GrantRunReward(_accountId, roomsTotal)`, `SendEnd`, the `pdungeon_runs` INSERT
  (`:805-811`, columns in `mod_pdungeon_runs*.sql`), `StartFinale()`. Run end = last boss dead (`:752-755`).
- Spawn funnel `SpawnTaggedMob(entry, proto, x, y, z, baseHealthOverride)` (`:1220-1378`); the ambush spawner
  `FireAmbush(Ambush&, Player*)` (`:3175-3280`) is the reusable "N mobs around a point, `countsForRun=false`,
  attack a target" primitive; `TickAmbushes()` runs on its own 250 ms cadence (`:3512-3517`).
- Account state `PDv2AccountState` (`src/PDv2Mgr.h:180-197`): `dlvl`, `dxp`, `cfgRooms`, `cfgDifficulty`,
  `cfgCasterPct`, `cfgBandMin`, `cfgPacks`; raw `CharacterDatabase` SQL, no prepared statements
  (`src/PDv2Mgr.cpp:262-348`); table `pdungeon_account` keyed by **`accountId`** — the account-wide home the
  run cap (R1) needs already exists. The dial is `GameClampDiff` 1..100 with **no unlock bound** today
  (`src/PDv2Mgr.cpp:299-301`); all bounds travel on the `C` payload (`src/PDv2UILink.cpp:443-445`), so a
  server-side cap reaches the slider with no Lua change.
- Wire (`lua_scripts/flpdui.lua:12-17`): server→client `FLPDU` kinds `C` (24 fields + verdict), `M` block map,
  `K` cleared blocks, `R` run tick, `E` completion, `N` notice (raid warning); client→server `FLPD` `UI HELLO |
  SET <key> <int> | GEN | ENTER | HUD <0|1>`. After a successful `UI GEN` the server already pushes `M` + `K`
  (`src/PDv2UILink.cpp:884-889`) — the layout data for a preview (R4) is on the client today; only the panel does
  not draw it. The HUD map is `BuildMap(m)` (`flpdui.lua:586-637`), bound to the HUD canvas.
- **Rooms mismatch** (R2), root cause: the panel shows `account.cfgRooms` = `BlockCfg::rooms` = "ROOM blocks
  before boss rooms are added" (`src/generator/PDBlockPlan.h:120`); the planner's chain is
  `total = max(2, rooms + bossRooms)` (`PDBlockPlan.cpp:888`, `:1642`) with **chain index 0 = the entrance**; the
  HUD's `roomsTotal` = `roomBlocks.size()` after `if (b.roomId < 0 || b.role == RoomEntrance) continue;`
  (`PDv2InstanceScript.cpp:1554-1559`, `:1670`) — boss rooms, pockets and loop rooms in, entrance out.
  `14 + 2 (boss at dlvl 10–19) − 1 (entrance) = 15`. Neither counter is wrong; they count different things.
- **Logging** (R3): one category `PD_LOG = "module.pdungeon"` (`src/PDDefines.h:107`), 141 call sites, zero
  `LOG_TRACE`. Ungated per-creature/per-tick lines: `OnCreatureCreate` DEBUG per spawn (`PDv2InstanceScript.cpp:491`),
  `CatchFallers` DEBUG 1 Hz (`:3422`), `SplitOnDeath` (`:1515`), `CallAlliesForHelp` (`PDv2CreatureAI.cpp:1753`),
  `OnUnitDeath` (`:3310`), `PDClientLink.cpp:172` per ack, `PDv2UILink.cpp:789/798/826/834/915/925` per client verb
  (attacker-controllable rate), and two **ungated WARNs in the patrol re-plan fallback**
  (`PDv2CreatureAI.cpp:726`, `:845`). 13 of 14 CreatureAI sites are already behind `V2.Patrol.Debug`
  (`PatrolDebug()`, `PDv2CreatureAI.cpp:79-82`). `ProceduralDungeon.Debug` is v1-only dead config
  (`PDMgr.cpp:65`, never read). No `V2.Debug*` exists.

### 1.3 Loot pools in the world DB (read-only measurement, MySQL 8.4.5)

Six schema facts a naive generator gets wrong (the resolver in `notes/round-e-loot-sql/tmpl4.sql` honours all six):

1. `creature_template.rank = 3` is **world boss**; the WotLK 5-mans return 0 rank-3 spawns; `flags_extra &
   0x10000000` is set at runtime, never in the DB. Boss predicate used: `rank = 3 OR ScriptName LIKE 'boss%' OR
   entry IN (instance_encounters.creditEntry WHERE creditType = 0)`, applied to the **base** entry.
2. `creature_loot_template.Reference <> 0` ⇒ the `Item` column is **not an item** (`LootMgr.cpp:1700` vs `:1714`);
   recurse into `reference_loot_template`.
3. Difficulty clones (`difficulty_entry_1/2/3`) keep `rank`, lose `ScriptName`; slot 1 = heroic 5-man / 25N raid,
   2 = 10H, 3 = 25H.
4. Script-summoned bosses are absent from `creature`: Anub'arak 34564, Eydis 34496, Fjola 34497, Icehowl 34797,
   Jaraxxus 34780, Yogg-Saron 33288, Sindragosa 36853, Tyrannus 36658, Black Knight 35451, Cyanigosa 31134,
   Svala 26668, Eck 29932, Drakkari Elemental 29573, Meathook 26529, Salramm 26530, Epoch 26532 — an explicit
   supplement list per pool.
5. Eye of Eternity has **zero** creature loot (Malygos `lootid = 0`); its gear is in cache GOs. 68 cache chests
   carry raid epics; the ICC / Ulduar / ToC / 5-man cache ids and their difficulty mapping are confirmed against
   the AC instance headers (list in `tmpl4.sql` seeds).
6. `dungeonencounter_dbc` is empty; there is no DB-side map↔encounter link.

Filters: gear = `Quality >= 4 AND InventoryType <> 0 AND class NOT IN (10, 12)` (class 10 = currency: `Emblem of
Frost` 49426 is Q4 and pollutes; class 12 = quest); PvE = no resilience stat (`stat_typeN = 35` splits 1 017 PvP
from 4 798 PvE items realm-wide). Two P1 outliers stripped: 50316/50317 "Papa's (Brand) New Bag" (containers).

| Pool | Definition | Items | ilvl signature |
|---|---|---|---|
| **HC5_EPIC** | heroic 5-man bosses + 6 caches, slot 1 | **160** | 232:69, 200:52, 219:37 |
| **RAID_N** | Naxx 533, OS 615, EoE 616, Ulduar 603, Ony 249, ToC 649, slots 0+1, 28 caches, **VoA 624 excluded** | **1 062** | 213:232, 245:194, 232:191, 226:180, 200:124, 219:109, 239:26 |
| **RAID_HC** | ToGC 649 slots 2+3 + tribute chests, **plus Ruby Sanctum 724 slots 2+3** | **340 + 28** | 258:184, 245:146, 272:10 · RS 271/284 |
| **ICC_N** | ICC 631 slots 0+1 + 10N/25N caches | **279** | 251:132, 264:129 (+LK 258/271) |
| **ICC_HC** | ICC 631 slots 2+3 + 10H/25H caches | **277** | 264:132, 277:127 (+LK 271/284) |
| **MATS** | `class IN (3, 7)`, `entry < 56000` (≥ 56000 is FL-custom), not deprecated (`Flags & 0x10`), Quality > 0, displayid > 0, stackable (gems are stackable 1), junk-name regex; expansion by **entry band** (< 21000 classic, < 33000 TBC, else WotLK — ItemLevel bands disagree on 357 items because WotLK inscription mats carry ilvl 1–50) | **1 125** core categories (gem 657, cooking 146, leather 65, metal 62, herb 51, elemental 46, cloth 39, enchanting 38, jewelcrafting 21); classic 280 / TBC 325 / WotLK 520 | — |

VoA would add 781 items (mostly duplicate tier pieces and PvP sets) and is the only "normal" source of ilvl 251/264 —
excluded (§2 D4). Ulduar hard mode is **not** cleanly separable (10 `LootMode & 2` items; hard caches mix normal
and hard ilvls) — it stays inside RAID_N. RS *is* cleanly separable per difficulty. Tier tokens (class 15/0, 69
realm-wide) are excluded from every pool by the `InventoryType <> 0` filter (§2 D5).

Ids: `06-custom-ids.md:61` reserves items 920100–920149 for PDv2; **920100–920104 used, 920105–920149 free**.
The module's item row shape is `data/sql/db-world/mod_pdungeon_flmats.sql` (stock displayids, no client patch).
`mod_pdungeon_flmats.sql` has **no table**; the world tables today are `pdungeon_affixes, pdungeon_chunk_meta,
pdungeon_critter_rules, pdungeon_decor_rules, pdungeon_member_spells, pdungeon_pack_members, pdungeon_packs,
pdungeon_palette` — **no loot table exists**.

### 1.4 The old Azealia Underground rewards (`fl-underground-dungeon`, deployed conf = `.dist`, zero overrides)

Boss hand-out `GiveLoot(Creature*, Unit*)` (`src/UndergroundBoss.cpp:58`), all rolls `urand(1,100) <= chance`,
delivery `MailItemOrGive(Player*, item, count, subject, Creature* sender)` (`src/UndergroundUtils.h:127`). The
rare set: five mounts **80095 Lava Mammoth, 80088 Maldraxxus Fly, 80096 Primal Dragonfly, 80100 Thunder Lizard,
80093 Lava Slug** at 1/10000 each (`:154-163`, independent rolls); **251000 Expert Emblem** 25 % at difficulty ≥ 80;
**80004 Exobeast energy plate** 5 % at difficulty ≥ 100; **251001–251004 armor Mystery Boxes** (cloth/leather/
mail/plate) 50 % at ≥ 26; **251005 Jewelry Mystery Box** 25 % at ≥ 26. (Also common: 251100 Explorer Badge 100 %
×2–11, 80002/80006/80051 relic/ore/crystal 75/50/20 %.) Mount and box ids are hardcoded there; gates 26/80/100 too.
Paragon XP: `IncreaseParagonXP(Player*, uint32)` from `mod-paragon/src/ParagonUtils.h:6`, included with
`#if __has_include("ParagonUtils.h")`; no-ops without the paragon aura or below max level; single player, no split.

### 1.5 Forgotten Talents (`mod-forgotten-talents` `main`, deployed and enabled locally, 12 conf keys)

- Tree `content/forgotten_tree.json`: 700 nodes, 727 links (stored twice: top-level `links` and per-node
  `parents`), node keys `costs[]` (per rank, Forgotten Power), `id`, `is_start`, `parents`, `permanent`,
  `source_spells`, `spells`, `x`, `y`. **Three start nodes: 90, 287, 396.** No depth stored; BFS from the starts
  reaches all 700, **max depth 26**; histogram 0:3, 1:15, 2:30, 3:42, 4:48, 5:49, 6:48, 7:50, 8:56, 9:48,
  10:43, 11:33, 12:27, 13:31, 14:29, 15:37, 16:25, 17:31, 18:16, 19:8, 20:4, 21:4, 22:4, 23:5, 24:7, 25:4, 26:3.
  Rank rows total **824** (666 single-rank, max 8). Canvas x ∈ [−1890, 2370], y ∈ [−1770, 1950], grid 30, no
  two nodes share a position. Names/icons come from `Spell.dbc` at runtime (`GetSpellInfo`).
- Importer `tools/import_ebonhold.py` (`import_content` `:557`) emits `forgotten_tree.json`,
  `src/ForgottenTalentsData.generated.h/.cpp` (`generate_cpp` `:340`; structs `RankDefinition{SpellId, Cost}`,
  `NodeDefinition{Id, Permanent, IsStart, Ranks, Parents}`), `client/…/TreeData.generated.lua` (`generate_lua`
  `:504`; per-node `{id, x, y, spells, soulPointsCosts, permanent, isStart}`). **Spell ids are positional and
  contiguous** (`:609-617`: `120000 + index in the sorted source closure`) — adding one source spell renumbers
  every id above it, and `build_client_data.py:128-132` hard-fails on a gap. Six arrays in
  `src/ForgottenTalentsMechanics.cpp:22-47` hardcode remapped ids. Next free id **120860**.
- Content version is the integer `1` hardcoded in **four** places (`import_ebonhold.py:490`, `:514`, `:687`,
  `client/…/Core.lua:6`); HELLO is a plain equality check (`ForgottenTalentsService.cpp:1093-1105`).
- Locked counts: `content/exclusions.json:28-46` (`expected`), asserted whole-dict by
  `tests/test_generated_content.py:32-36`; `:51-55` requires every `cost > 0`; `tests/test_client_content.py:35-39`
  pins **83** icons; `:10-28` pins the TOC file list. 14 Python tests, all green today.
- Service: `SaveLoadout` (`ForgottenTalentsService.cpp:615`) → `CanMutate` (`:594`, enabled/not in combat/alive) →
  `ValidateBuild` (`:394`: node exists, rank range, **every parent at MAXIMUM rank** `:421-432`, permanent
  non-decrease, `spentPower > totalPower`) → `PersistLoadout` (`:470`, delete-then-reinsert in one transaction) →
  `ApplySelectedLoadout` (`:507`: wipe all 860 owned spells, learn the selected rank per node, replacement chains
  collapsed to the highest rank). Cost lookup is `CalculateSpent` (`:377`) over `Ranks[i].Cost`. Power via
  `AddPower` (`:910`) from `OnPlayerCompleteQuest` / `OnPlayerRewardKillRewarder` (`src/ForgottenTalents.cpp:89`,
  `:100`). **Zero item handling anywhere in `src/`.** Up to 5 loadouts (`MaxLoadouts`), `Permanent` is a node flag
  (13 nodes, riding chain 582–585) meaning "may not decrease within a loadout".
- Protocol FT1 (self-whisper, `LANG_ADDON`): requests `HELLO|id|ver`, `SAVE|id|loadout|rev|n:r,n:r`,
  `SELECT|id|loadout`, `CREATE|id|name[|ranks]`, `DELETE|id|loadout`, `RENAME|id|loadout|name`; responses
  `STATE|id|part|count|chunk` (payload `<ver>;P=<power>,<sel>,<rev>[;L=<id>,<rev>,<spent>,<name>,<n:r.n:r…>]*`),
  `POWER|balance|rev`, `ERROR|id|code|msg`; responses hard-capped at 255 bytes, chunk 210. **No conf value
  crosses the wire today.** Client cost = `getNodeCost(btn, rank)` (`TreeUI.lua:180-184`) from
  `soulPointsCosts`; the single cost renderer is `addCostToTooltip` (`:205-209`, five call sites); SAVE is sent by
  `ValidateAndSendLoadout` (`:833-855`) → `Core.lua:195-224`. Bug in passing: `Core.lua:14 MAX_SOUL_ASHES =
  403448860` is off by 1 000 from the generated maximum.
- Core patch: 22 `CHAR_*_FORGOTTEN_*` statements present in the local core
  (`CharacterDatabase.h:556-577`, `.cpp:647-668`); new statements are appended inside that block and mirrored in
  `patches/azerothcore-prepared-statements.patch`.
- Client patch: `scripts/30_build_hot_dbc_patch.py` in the workspace takes the addon straight from the module
  checkout (`FT_ADDON`, `:58-60`) — an addon-only change needs a **patch-9 rebuild via script 30**, not the FT
  MPQ tool; a spell change additionally needs `tools/build_client_data.py` (Spell.dbc/SpellIcon.dbc + world SQL).

### 1.6 Cursed items, Dungeon-Challenge respawn, kit defects

- **Cursed items are made only by `mod-paragon-itemgen`**: `static bool RollCursed()` (`src/ParagonItemGen.cpp:498`)
  and `static void ApplyParagonEnchantment(Player*, Item*)` (`:533`), no public API; hooks `OnPlayerLootItem`,
  **`OnPlayerCreateItem`** (`:773`, key `ParagonItemGen.OnCreate`), **`OnPlayerQuestRewardItem`** (`:781`,
  `.OnQuest`), `OnPlayerAfterStoreOrEquipNewItem`. Deployed `ParagonItemGen.CursedChance = 50.0` (docs still say
  1 % — stale). "Cursed" = pure upside (stats ×1.5 capped 666, slot-11 marker 920001 or a passive 950001–950099,
  bound). Core hooks confirmed: `OnPlayerCreateItem(Player*, Item*, uint32)` `PlayerScript.h:435`,
  `OnPlayerCompleteQuest(Player*, Quest const*)` `:243`, `OnPlayerCreatureKill(Player*, Creature*)` `:252`,
  `OnGameObjectLootStateChanged(GameObject*, uint32 state, Unit*)` `AllGameObjectScript.h:80`.
- **DC "Mob Respawn"** (not an affix, `DungeonChallengeScripts.cpp:526-619`): arms on **entering combat** (not
  death), 3 s timer, up to 2 copies of the same entry at the home position, full difficulty scaling, `noLoot`,
  copies despawn when combat ends. The affix "Lil' Bro" (split on death, 2 × 10 % HP) is the other one; PDv2
  already has its own Lil' Bro (`SplitOnDeath`). Copies are invisible to the DC counter *structurally* (spawn-id
  store vs temp summons).
- **K1 tower room** = theme 2 / alt 1 / role `room` = chunk ids **13001–13015**, the only family with a centre pad
  (`footprint_cells()` `48_gen_t1_blockkit.py:254-260`), one centred `house_d` = GILN_HOUSE04 (42 yd tower).
  Root cause, measured: `PAD_FACADE_SLOTS = 8` (`:1169`) still reserves eight of the 24 MODF rows for a 33 yd pad
  ring that Round B/B2 deleted; the surviving 16.67 yd pad takes the single-building branch and spends **one**;
  `per_side = (24 − 8) // 4 = 4` houses per wall instead of 6 (`:2389`, call site `:2837-2843`). Shipped kit:
  alt-1 rooms 19/21.2/24 WMO rows vs 21/23.2/24 for every other family; `105_frontage_audit.py` on the live
  layout: **81 yd bare of 5 536 (1.5 %), every bare yard on 13005/13006/13010** — the vault's standing "throat
  facade" item (`rounds/pd_testlauf_runde28.md:506-513`, runde30 §7) is this constant, not the throats. A dry
  re-run with the reserve at 1 gives 21 → 23 rows on 13005/13006/13010 and clears every bare station the coverage
  proxy sees on 13006. There is **no** clearance ring around the centre prop (hypothesis ruled out).
- **K2 hard texture seam**: one floor texture kit-wide (`THEME_ROLE_FLOOR[2] = {}`, `48:1871-1887`; `103` audit:
  0 `TEX-DIFF` edges) — the seam is **not** a texture change. It is the MCNK edge-row **pin** in
  `paint_block_alpha()` (`51_texture_blockkit.py:588-604`): the outermost row/column of each MCNK keeps its
  unramped class value because MCNK flag `0x8000` (`do_not_fix_alpha_map`) is not set and the client duplicates
  that row; the pin is asymmetric (only `pr/pc == 63`), so across every MCNK boundary one side carries the raw
  mottle (2) and the other the full 4-texel feather (≈12): step **10 of 15**, exactly the `103` maximum
  (block-boundary mean 2.31, side means 6.0 vs 7.8; artefact-free reference `same block corridor` max 4 /
  mean 0.10). Pinned lines sit on the block centre cross *and* the 66.67 yd block border. Known and accepted
  since Round A as "the 0.5 yd pin/feather line at jamb feet" (`04-round-b-brief.md:17`); scripts 51 and 52 both
  defer the `0x8000` fix. Nobody writes MCNK flags today (`WRITABLE_HEADER_FIELDS` `51:189-190`). Alpha is raw
  4-bit, 2048 B, `nLayers = 2`, `MCLY_FLAG_COMPRESSED` never set; changing values is size-neutral, changing depth
  is not (48 asserts one file size, `49` is a fixed-size memcpy).

---

## 2. Decisions the operator must confirm (defaults chosen so work can start)

Each is a one-line change if he decides otherwise. Everything below is **assumed**, not approved.

| # | Decision | Default in this spec | Why |
|---|---|---|---|
| **D1** | Tier ladder names vs item colours | **Confirmed 2026-09-10 ("so wie in WoW"):** T1 = **Common** (white, 100 % per mob), T2 = **Uncommon** (green, 5 %), T3 = Rare (blue, 1 %), T4 = Epic (purple, final cache), T5 = Legendary (orange, final cache); item qualities 1..5 | WoW colour order; the operator's first wording had the two lowest swapped |
| **D2** | Forgotten Power is **retired** | **Confirmed 2026-09-10 ("ja ersetzen").** The five currencies replace it entirely: no more power rewards from quests/elites, no `.forgotten power`, the 9 `Reward.*` keys go. Existing allocations and power are **wiped** by the migration (the server is not live; the operator's test characters lose their tree) | "mit denen man die forgotten talents lernen kann" |
| **D3** | Purchases are **permanent, no refund**, one build per character | Confirmed with D2. Loadouts collapse to the single loadout 0; SELECT/CREATE/DELETE/RENAME/SAVE are refused; right-click unlearn is removed; a GM `.forgotten reset` exists for testing. A paid full reset can come later as its own feature | Consumed materials are the operator's design; a respec economy is a separate decision |
| **D4** | Raid pools | **Confirmed 2026-09-10 with one addition:** VoA excluded from RAID_N; Ruby Sanctum normal in RAID_N, RS heroic in RAID_HC; Ulduar hard-mode gear stays inside RAID_N (not separable); tier **tokens excluded** everywhere (no vendor path in the PD) — **and the finished tier-set pieces go in instead** ("dafür fertige Tier-Set-Stücke rein"): set pieces never sit in a boss loot table (they are token-bought), so script 106 adds them as a seventh source — epic level-80 armour with `itemset <> 0`, no resilience, `ItemLevel` in the tier signatures, category `tier`, weight 100 — assigned by ilvl: 200/213 (T7), 219/226 (T8), 232/245 (T9) → RAID_N; 258 (T9 heroic) → RAID_HC; 251/264 (T10, Sanctified) → ICC_N; 277 (heroic Sanctified) → ICC_HC. PvP gear excluded (resilience test) | §1.3 measurements; operator 2026-09-10 |
| **D5** | Class fit | Rolled gear is filtered to the looter: `AllowableClass` **and `AllowableRace`** (the T9 sets exist per faction), armour type = the class's highest (plate/mail/leather/cloth), shields for war/pal/sha, a static class→weapon-subclass table; relics via `AllowableClass`. **Stat profile is not filtered** (a spell-power ring can drop for a rogue). Empty filtered pool → unfiltered fallback. `V2.Loot.ClassFilter = 1` | Pure random from 1 062 items is mostly disenchant fodder |
| **D6** | Personal rewards | Currency (L2) and materials (L3) are **personal rolls for every player on the instance map** at the kill; gear (L4) goes through the **corpse / chest loot window** (group loot rules apply). Nothing is split | Solo is the main case; groups should not fight over materials |
| **D7** | Extra mobs | Patrols and ambush mobs drop currency **and** materials like any mob (they already "drop loot like any dungeon mob"); Lil' Bro split children drop nothing (as today); the WP5 event-wave mobs and the WP6 respawn copies drop **materials only, no currency** (`V2.Loot.ExtraMobsDropCurrency = 0`) | The operator tied currency to the room count; extra spawns must not inflate it |
| **D8** | Room factor applies to **every** currency roll (T1..T5) and to the legacy rare rolls (L5); not to materials, not to gear | `factor = rooms/10` below 10 rooms, `1 + (rooms − 10) × 1 %` above, `rooms` = the HUD's room count of the run (R2 semantics) | "damit nicht einfach nur ein-raum-runs gespammt werden" |
| **D9** | Gear item counts scale with lootMult | `expected = Items × lootMult` (integer part + a roll on the fraction): chest 1, boss 1, final cache 1 at lootMult 1.0 → three items at difficulty 100 | Otherwise the difficulty dial does nothing for gear |
| **D10** | Materials pool | The nine core categories only (gem, cloth, leather, metal/stone, herb, elemental, enchanting, cooking, jewelcrafting), gems weighted **20** against 100 because they are 47 % of the pool; parts/explosives/devices/vellum/other excluded | "Materialien" |
| **D11** | Legacy rares carried | The five mounts (1/10000 each), Expert Emblem (25 %, ≥ 80), Exobeast plate (5 %, ≥ 100), the armour box (50 %, ≥ 26, picked by the looter's armour type), the jewelry box (25 %, ≥ 26). Badges/ore/relics are not carried | "seltene drops" |
| **D12** | Cost model | **Confirmed 2026-09-10 ("ja schön langer Grind soll sein"):** depth-banded, five tiers, server-authoritative; constants live in the FT conf and travel to the client at HELLO; defaults in T1 §3; the generated **economy report** stays as the tuning tool, but a long grind is the intent, not a defect | The drop rates he gave are per-mob; the tree has 824 ranks — the report shows the runs-to-max per tier |
| **D13** | Rooms semantic (R2) | The slider counts **ordinary rooms** (no entrance, no boss rooms); the planner adds the entrance and the boss rooms on top; HUD and panel show the same ordinary-room count and bosses separately | The only reading where 14 means 14 |
| **D14** | Kit fixes ride one kit round (v39) | K1 and K2 together, later, because every kit change costs a chunk-meta regeneration, a restart and a launcher payload | cost of a kit deploy |
| **D15** | Run cap raise rule | **Confirmed 2026-09-10.** `cap = min(100, max(cap, runDifficulty + (deaths ? 3 : 5)))`, account-wide, all existing accounts start at 1 (the operator's own included; GM `.pdungeon v2 cap <n>` for testing) | relative to the run completed, so low runs cannot inch the cap up |

---

## 3. Design

### L1 — loot pools as data (`pdungeon_loot_pool`, `pdungeon_loot_bonus`)

A workspace script **`scripts/106_pd_loot_pools.py`** (read-only against `acore_world`, DSN from
`worldserver.conf`, password never on argv — the pattern of `notes/round-e-loot-sql/q.sh`) resolves the six pools
with the §1.3 resolver and writes **`data/sql/db-world/mod_pdungeon_loot_pools.sql`** (DELETE-before-INSERT per
pool, 100-row INSERT chunks, a header with the run date, DB host, per-pool counts and ilvl histogram). It is
**regenerated, never hand-edited** (the `mod_pdungeon_chunk_meta.sql` discipline); `--check` prints the counts
without writing.

```sql
CREATE TABLE IF NOT EXISTS `pdungeon_loot_pool` (
  `pool`      VARCHAR(16)       NOT NULL,            -- HC5_EPIC RAID_N RAID_HC ICC_N ICC_HC MATS
  `item`      INT UNSIGNED      NOT NULL,
  `weight`    SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `expansion` TINYINT UNSIGNED  NOT NULL DEFAULT 2,  -- 0 classic 1 tbc 2 wotlk (F1 reads it)
  `category`  VARCHAR(16)       NOT NULL DEFAULT '', -- MATS: gem cloth leather metal_stone herb elemental enchanting cooking_meat jewelcrafting
  `comment`   VARCHAR(96)       NOT NULL DEFAULT '', -- item name, ilvl (generator output, human aid)
  PRIMARY KEY (`pool`, `item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `pdungeon_loot_bonus` (   -- independent rolls, final cache only (L5)
  `id`         SMALLINT UNSIGNED NOT NULL,
  `item`       INT UNSIGNED      NOT NULL,
  `chance_bp`  INT UNSIGNED      NOT NULL,           -- 1/10000 before the room factor
  `min_count`  SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `max_count`  SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `min_diff`   TINYINT UNSIGNED  NOT NULL DEFAULT 1, -- run difficulty gate
  `armor_pick` TINYINT UNSIGNED  NOT NULL DEFAULT 0, -- 1: item + 0..3 by the looter's armour type (cloth leather mail plate)
  `comment`    VARCHAR(96)       NOT NULL DEFAULT '',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```

Engine side: a new **`PDv2LootMgr`** (`src/PDv2LootMgr.h/.cpp`, singleton like `PDv2PackMgr`, startup-loaded and
read-only after): per-pool `std::vector<PoolEntry{item, weight, expansion, category}>`, the bonus rows, and the
roll API used by L2–L5. Boot log: one line per pool with the row count and, for MATS, the expansion split;
`LOG_ERROR` and an empty pool when a row's item is unknown to `sObjectMgr`. `.pdungeon v2 info` gains a
`loot:` line (`pools 6 · HC5_EPIC 160 · RAID_N 1062 · … · bonus 9`).

```cpp
// PDv2LootMgr — every roll uses core urand (the determinism boundary of §1.1), never PDRandom.
uint32 RollGear(std::string_view pool, Player const* forClass);      // 0 = pool empty; D5 filter with fallback
uint32 RollMaterial(uint8 expansionMask = 0x7);                      // weighted; mask for F1
std::vector<LootBonusHit> RollBonus(uint8 difficulty, int roomFactorX100, Player const* looter);
bool   ItemFitsClass(ItemTemplate const* proto, uint8 classId);      // D5, pure, header-visible for the harness
```

### L2 — the five currencies and their drop rules

Items **920105–920109** in `data/sql/db-world/mod_pdungeon_currency.sql` (the `flmats.sql` row shape: class 7,
subclass 0, stock `displayid`, `bonding 1`, `stackable 1000`, ItemLevel 80, Quality 1..5):

| Tier | Item | Name | Quality | Source | Base chance | Gate |
|---|---|---|---|---|---|---|
| T1 | 920105 | Faded Remnant | 1 white | every dungeon mob | 100 % | — |
| T2 | 920106 | Gleaming Remnant | 2 green | every dungeon mob | 5 % | — |
| T3 | 920107 | Radiant Remnant | 3 blue | every dungeon mob | 1 % | — |
| T4 | 920108 | Sovereign Remnant | 4 purple | Chromie's Cache only | 50 % | run difficulty ≥ 50 |
| T5 | 920109 | Eternal Remnant | 5 orange | Chromie's Cache only | 10 % | run difficulty ≥ 75 |

**Room factor** (D8), integer x100 in `PDv2GameMath.h` so the harness pins it:

```cpp
constexpr int GameRoomFactorX100(int rooms, int baseline = 10, int bonusPctPerRoom = 1)
{   // rooms 1..9 → 10..90; 10 → 100; 11 → 101 …
    if (rooms <= 0) return 0;
    if (rooms < baseline) return rooms * 100 / baseline;
    return 100 + (rooms - baseline) * bonusPctPerRoom;
}
```

Per-mob roll (in `OnMobDied`, after the counters, for every tagged creature except split children; patrols and
ambush mobs included; event/respawn mobs only if `ExtraMobsDropCurrency`): for each player on the map, for T1..T3:
`chanceBp = ChancePct × 100 × roomFactorX100 / 100`, `urand(1, 10000) <= chanceBp` → `GrantItem(player, item, 1)`.
Final cache (T4, T5): rolled once **per looter** when the cache is opened (see L4's injection point), difficulty gate
first, then the same room-factor chance; the item goes into the cache's loot for that looter's window. The
`rooms` input is the run's ordinary-room count (D13), frozen into `PDv2RunState::roomFactorX100` at spawn.

`GrantItem(Player*, item, count)` = `AddItem`, and when the bags are full a mail from Chromie (NPC 910550) with the
item — the `MailItemOrGive` pattern of `fl-underground-dungeon/src/UndergroundUtils.h:127`. `AddItem` already
prints the client's "You receive item" line, so no extra chat.

Conf (`ProceduralDungeon.V2.Loot.Currency.*`): `Tier<N>.Item`, `Tier<N>.ChancePct`, `Tier4.MinDiff 50`,
`Tier5.MinDiff 75`, `RoomsBaseline 10`, `RoomsBonusPctPerRoom 1`, `ExtraMobsDropCurrency 0`. Code defaults equal the
`.dist` values, so no key has to be set.

### L3 — materials from every mob

Same funnel, same recipients: per player, `V2.Loot.Mats.ChancePct` (100) → `count = urand(1, maxCount)`,
`maxCount = 1 + (V2.Loot.Mats.MaxPerMobAtCap − 1) × dlvl / V2.DlvlCap` (dlvl 0 → 1 … dlvl 30 → 5), item =
`RollMaterial()` (uniform over the weighted MATS pool, all three expansions). `GrantItem` as above. The `dlvl`
is the **account's** dlvl at spawn (frozen into the run like the difficulty). Split children excluded; event and
respawn mobs included (D7).

### L4 — gear from chests, bosses, the final cache

| Source | Pool below `V2.Loot.IccDlvl` (10) | Pool from dlvl 10 | Count |
|---|---|---|---|
| Shifting Cache 910030 (dead ends, loop rooms) | HC5_EPIC ∪ RAID_N (one draw over the union, weights as stored) | ICC_N | `Chest.Items 1` × lootMult |
| Run boss (`isRunBoss`) | RAID_HC | RAID_HC | `Boss.Items 1` × lootMult |
| Chromie's Cache 910068 | ICC_N | ICC_HC | `Final.Items 1` × lootMult |

Count (D9): `n = floor(Items × lootMultX100 / 100)`, plus one when `urand(1,100) <= (Items × lootMultX100) % 100`.

Injection points:
- **Boss**: in `OnMobDied`, before the existing loot branch — `creature->loot.AddItem(LootStoreItem(item, 0,
  100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1))` per rolled item. The corpse loot is already generated by
  `Unit::Kill` before `JustDied` runs, so the item lands in the normal loot window with the group's loot rules.
- **Chests**: a new `PDv2ChestLootScript : public AllGameObjectScript` on `OnGameObjectLootStateChanged(go,
  GO_ACTIVATED, unit)`. `Player::SendLoot` fills the template loot (`Player.cpp:7893`) and then calls
  `SetLootState(GO_ACTIVATED, this)` (`:7930`) — the hook fires there, before the loot packet — so the script adds
  the rolled gear (and, for 910068, the T4/T5 currency and the L5 bonus hits) to `go->loot` once per GO
  (guard: a `PDv2ChestData : DataMap::Base` flag on the GameObject). The template loot rows of both chests stay
  (they become filler: Frostweave/potion/orb for 910030; for 910068 the five FL mats are cut to 25 % each). Gate:
  map 760, a live `PDv2InstanceScript`, entry ∈ {910030, 910068}.
- The class filter uses the **looter** (`unit->ToPlayer()`) for chests and the **killer's** player for the boss.

### L5 — legacy rares in the final cache

Rows 1–9 of `pdungeon_loot_bonus` (hand-authored in `mod_pdungeon_loot_pools.sql` by the script from a fixed
table, so regeneration keeps them): 80095/80088/80096/80100/80093 `chance_bp 1`; 251000 `2500, min_diff 80`;
80004 `500, min_diff 100`; 251001 `5000, min_diff 26, armor_pick 1`; 251005 `2500, min_diff 26`. Each row rolls
independently, chance × room factor, once per looter at cache activation; hits are added to the cache loot.
`RollBonus` verifies every item exists at load; the armour pick maps cloth/leather/mail/plate = +0/+1/+2/+3 by the
looter's class (mage/warlock/priest → cloth; rogue/druid → leather; hunter/shaman → mail; warrior/paladin/DK →
plate).

### T1 — the Forgotten-Talents purchase model (WP2, `mod-forgotten-talents` + one core block)

**Currency.** Conf `ForgottenTalents.Currency.Tier1..5` = 920105..920109 (the module never hardcodes PD ids;
the item templates are PD's, applied by PD's SQL). Startup validates the five templates exist; a missing one
disables the module like a missing spell does.

**Depth.** The importer emits `depth` per node (BFS from the `is_start` nodes, min over the three starts) into
JSON, C++ (`NodeDefinition::Depth`, `uint8`) and Lua (`depth = N`), plus `costMult` (`uint16`, 100 for every
imported node; WP6's nodes use it). `content_version` 1 → **2** in all four places. The per-rank `costs[]` stay in
the JSON/report as import evidence but are no longer read by the server or the client.

**Cost.** Integer, server-authoritative, five tiers from conf (`ForgottenTalents.Cost.Tier<k>.MinDepth`,
`.Start`, `.GrowthPct`; `ForgottenTalents.Cost.RankStepPct`):

```
base_k(d)   = 0                                            if d < MinDepth_k
            = max(1, Start_k * (100 + GrowthPct_k * (d - MinDepth_k)) / 100)   (integer division)
cost_k(d,r) = ceil(base_k(d) * costMult / 100 * (100 + RankStepPct * (r - 1)) / 100)   for rank r >= 1
```

Defaults (the economy report decides the final numbers, see below):

| Tier | MinDepth | Start | GrowthPct | base at depth 26 |
|---|---|---|---|---|
| T1 | 0 | 4 | 30 | 35 |
| T2 | 5 | 1 | 12 | 3 |
| T3 | 14 | 1 | 10 | 2 |
| T4 | 20 | 1 | 10 | 1 |
| T5 | 24 | 1 | 0 | 1 |

`RankStepPct 50` (rank 8 = ×4.5). A start node costs 4 T1; a depth-26 node needs all five ("viel billig, wenig
teuer"). The server sends the **computed base table** to the client at HELLO (`;C=<rankStep>|<the five item
ids>|<d0: t1,t2,t3,t4,t5>|<d1: …>|…|<d26>`, 27 rows, ≈ 500 bytes → three or four STATE chunks) so the client's
only formula is the rank step, it never hardcodes an item id, and a conf change reaches every client on relog
without a patch.

**Economy report.** `tools/economy_report.py` (new, pure Python, reads `forgotten_tree.json` + the cost constants
as arguments, defaults = the conf defaults) prints per tier: total cost to max the whole tree, the expected drops
per 10-room run at difficulty 100 (T1 ≈ mobs/run, T2 5 %, T3 1 %, T4 0.5, T5 0.1 — the mob count per room from
`PDv2GameMath` is a parameter), and **runs-to-max**. The operator tunes `Start/Growth/MinDepth` until the curve
matches his intent; the report is part of the round's handover.

**Protocol.** New request `BUY|<reqId>|<nodeId>|<rank>`; server: `CanMutate` → node exists → `rank ==
currentRank + 1` (exactly the next rank) → every parent at max rank (existing rule) → compute the five costs →
`HasItemCount` per tier, else `ERROR|COST|Missing: 3x Gleaming Remnant, …` → `DestroyItemCount` ×5 →
`CHAR_UPS_FORGOTTEN_NODE` (loadout 0) + `CHAR_INS_FORGOTTEN_PURCHASE` in one transaction → `++revision` →
`ApplySelectedLoadout` → full `STATE`. `SAVE/SELECT/CREATE/DELETE/RENAME` answer `ERROR|REJECTED|Loadouts are
retired` (one loadout, id 0, "Default", created by `EnsureDefaultState` as today). `STATE`'s `P=` segment becomes
`P=<ranksLearned>,<sel>,<rev>`; `POWER` is gone. Removed: `AddPower/SetPower`, `PowerSource`, both reward hooks,
the nine `Reward.*` keys, `MaxLoadouts`, `.forgotten power`. Added GM commands: `.forgotten grant <tier> <count>`
(gives currency, `RBAC_PERM_COMMAND_MODIFY`), `.forgotten reset` (wipes the character's ranks, unlearns, no
refund), `.forgotten cost <node>` (prints the five costs of the next rank — the harness for the formula).

**Persistence.** `character_forgotten_node` keeps its shape (always `loadout_id 0`); new
`character_forgotten_purchase(guid, node_id, rank, t1, t2, t3, t4, t5, created_at)` (append-only, the audit trail
that `character_forgotten_ledger` was); `character_forgotten_ledger` is **dropped**;
`character_forgotten_power.total_power` is retired (kept at 0, documented — dropping a column the updater already
recorded is a migration of its own). Migration SQL (`data/sql/db-characters/updates/`): create purchase, drop
ledger, `DELETE FROM character_forgotten_node`, `UPDATE character_forgotten_power SET total_power = 0`. Core
block: **remove** `CHAR_UPD_FORGOTTEN_POWER_ADD`, `CHAR_UPD_FORGOTTEN_POWER_SET`, `CHAR_INS_FORGOTTEN_LEDGER`,
`CHAR_DEL_FORGOTTEN_LEDGER`; **add** `CHAR_UPS_FORGOTTEN_NODE`, `CHAR_INS_FORGOTTEN_PURCHASE`,
`CHAR_DEL_FORGOTTEN_PURCHASES` (character delete) — in `CharacterDatabase.h/.cpp` and the patch file.

**Client.** `TreeUI.lua`: left-click = `BUY` immediately (no Apply button, no pending build); right-click does
nothing; the tooltip's cost line lists the five tiers (`|T<icon>:16|t <need>` in green when
`GetItemCount(item) >= need`, red otherwise; zero-cost tiers omitted); the bottom bar shows the five bag counts
and `Ranks learned: n / 824`; the loadout dropdown, Create/Delete/Rename/Apply/Reset and the power bar are removed;
`MAX_SOUL_ASHES` goes with them. `Core.lua`: `CONTENT_VERSION = 2`, parse `;C=`, send `BUY`, drop `POWER`.
Delivery: patch-9 rebuild via workspace script 30 (`FT_ADDON` = the module checkout) + the loose dev copy;
`/reload` after deploy.

**Everywhere active**: nothing in T1 depends on the map — the tree works exactly as before outside the dungeon.

### T2 — three legendary nodes (WP6, design only here)

- **Extension contract** in the importer: `content/extra_nodes.json` (nodes with `id ≥ 2000`, `parents`, `x/y`
  on free grid points, `ranks[]` with the spell's name/icon/description/basePoints, `costMult`) merged after
  `assert_graph`; **spell ids become sticky** — the importer reads `spell_id_map.json` and keeps every existing
  assignment, appending new ids from **120860**; `build_client_data.py`'s contiguity check becomes "unique inside
  120000–129999". Synthetic spells are passive `SPELL_AURA_DUMMY` (effect 0) with **`EffectMiscValue` as a tag** and
  `BasePoints = value − 1` (the `spell_dbc` off-by-one): tag **76001** respawn (value = rank, 2 ranks),
  **76002** cursed-craft (2r %, 5 ranks), **76003** cursed-quest (2r %, 5 ranks). `depth` = parent depth + 1 (so
  they sit in the most expensive band); `costMult` 200 (respawn) / 300 (the two curse nodes). Icons from
  `client/staging` (83 → 86 in the test). `content_version` 2 → 3. Parents: the deepest node of the crafting
  branch and of the questing branch (identified by spell names in `spell_records.json` when WP6 starts); the
  respawn node hangs off the deepest combat node.
- **Consumers read the tag, never the id**: `TaggedAuraAmount(Player*, int32 miscValue)` = the amount of the
  dummy aura effect with that MiscValue, 0 when absent. PD: in `OnMobDied`, for a `countsForRun` trash kill
  (never a boss, never a copy), `n = TaggedAuraAmount(killer, 76001)` → `n` copies of the entry at grid-vetoed
  offsets around the corpse (the `SplitOnDeath` placement, `FireAmbush`'s attack start), `countsForRun=false`,
  `roomIndex=PD_ROOM_NONE`, `isRespawnCopy=true` (materials yes, currency no, Paragon XP through the normal kill
  path), no second respawn from a copy. Cursed: inside `mod-paragon-itemgen`, `RollCursed()` gains
  `(Player*, Context)` and adds `TaggedAuraAmount(player, 76002)` (create) / `(…, 76003)` (quest reward) to the
  percent chance. No cross-module include anywhere.

### R1 — the account-wide run cap (WP3)

`pdungeon_account.diff_cap TINYINT UNSIGNED NOT NULL DEFAULT 1` (new SQL file, the
`mod_pdungeon_account_difficulty.sql` add-column pattern) → `PDv2AccountState::diffCap`; `SetAccountCfg` clamps
`cfgDifficulty ≤ diffCap`; `SendCfg` sends `diffMax = diffCap` (the slider follows, no Lua change);
`PDv2RunState::deaths` (uint8) incremented in `OnUnitDeath` for players; `FinishRun`: `unlock = deaths ?
V2.Cap.DeathUnlock (3) : V2.Cap.CleanUnlock (5)`, `newCap = min(100, max(cap, difficulty + unlock))`,
persisted (`RaiseDiffCap(accountId, newCap)`, `UPDATE … SET diff_cap = GREATEST(diff_cap, ?)`), `N` notice
"Difficulty N unlocked (M deaths)" + a fresh `C`; `pdungeon_runs.deaths` column for history; GM
`.pdungeon v2 cap <n>`. Frozen difficulty stays the run's; the cap only bounds the next choice.

### R2 — rooms semantic (WP3, D13)

`PDBlockPlan`: `total = rooms + bossRooms + 1` (the entrance is no longer one of the wanted rooms);
`PD_LAYOUT_VERSION` 3 → 4 (stored layouts regenerate); `GameRoomsCap` unchanged, `pdblock --roomcap 3000`
re-pins the manifest budget (if the 15 + 2 + 1 case exceeds `PD_GAME_MANIFEST_BUDGET_B`, `PD_GAME_ROOMS_MAX`
drops to 14 — the harness decides). `roomsTotal`/`roomsCleared` count non-boss rooms (`!_roomIsBoss`) — bosses
have their own counter; the panel's "Current depths" line uses the same two numbers. New harness pin: for 200
seeds, `ordinaryRooms == cfg.rooms + pockets + loopRooms` and the entrance is counted nowhere.

### R3 — one debug switch (WP3)

`ProceduralDungeon.V2.Debug = 0` → `PDv2Config::debug`, live-reloadable; `inline bool PDv2Debug()` in
`PDv2Mgr.h`; every per-creature / per-tick / per-verb line from §1.2 goes behind it (`if (PDv2Debug())
LOG_INFO(...)`), the two re-plan WARNs become debug lines, the per-run `LOG_INFO` summaries (spawn summary,
`FinishRun`, boot lines) stay. `V2.Patrol.Debug` stays as the patrol-specific switch. The v1 `ProceduralDungeon.
Debug` key is untouched (v1 deletion is its own open item).

### R4 — layout preview in the gen panel (WP3)

Lua only: `BuildMap(m, canvas, pool)` takes its target; the gen panel gets a 160 × 160 canvas below the
"Current depths" line (panel height + 170) fed by the same `M`/`K` payloads the server already sends after
`UI GEN` and on HELLO; the entrance is green, boss rooms dark red, corridors thin bars, exactly the HUD's drawing.
No server change.

### R5 — event rooms (WP5, design only here)

- **Planner** (`PDBlockPlan`, engine-free, harness-pinned): per boss segment a seeded coin `V2.Event.ChancePct`
  (25) → an **event pocket**: one ordinary room block hung off a spine room of that segment by its own corridor,
  one socket, never on the chain (the pocket machinery of B0b; flagged `isEvent` in `BlockInfo`, invisible to the
  manifest — the block uses the ordinary room chunk, so the composer/DLL contract is untouched; the oracle run
  proves it). Event rooms are not counted in `roomsTotal` and never carry a pack, chest or patrol.
- **Engine**: NPC **910551** (reserved; name/model chosen at WP5) at the room's `entry` anchor, gossip "Hold the
  line" → `StartEvent(room)`: `V2.Event.DurationSec` (60), every `V2.Event.SpawnEverySec` (5) one creature from
  the run's pack draw (`V2.Event.CasterPct` 10 → caster else melee) at the room's outer spawn anchors (the
  `spawns` list, farthest-from-centre first), `countsForRun=false`, `roomIndex=PD_ROOM_NONE`, `isEvent=true`,
  `AttackStart(npc)`; the NPC's faction must be **attackable by the packs and friendly to both player factions** —
  measured in game at WP5 (candidates: a neutral template the monster factions are hostile to; forced
  `AttackStart` needs only "not friendly"). Ticks on the ambush cadence. NPC dies → "The refugee has fallen",
  remaining wave mobs despawn. NPC survives → Shifting Cache at the room's `chest` anchor (L4 gear) +
  `IncreaseParagonXP(player, V2.Event.ParagonXp × lootMultX100 / 100)` for every player on the map (the
  underground's guarded include). Wave mobs drop materials, no currency (D7).
- **HUD**: the `R` tick gains two trailing fields `eventSecLeft eventNpcPct` (0 0 when idle); the addon draws a
  top-centre bar "Hold the line — 0:42 · 78 %" in the HUD's frame (the timer/`hudGate` line style), shown only
  while an event runs. Raid warnings at start/end via `N`.

### K1 + K2 — kit v39 (WP4, design only here)

- **K1**: `48_gen_t1_blockkit.py:2841-2843` — reserve `1` when the pad's span is below `PAD_MIN_SPAN_YD`, `8` only
  for a ring pad (`PAD_FACADE_SLOTS` keeps its meaning for a pad that could come back). Expected: 21 → 23 rows on
  13005/13006/13010, `105` bare line 81 yd → ≈ 0 on those chunks. Placement-only: the walk contract `[6b]` is
  unchanged, but 15 chunk hashes move.
- **K2**: `51_texture_blockkit.py` `paint_block_alpha()` — drop the `on_mcnk_edge` pin (`:597-604`) **and** set
  MCNK header flag `0x8000` (`do_not_fix_alpha_map`) on every kit chunk (new writer: widen
  `WRITABLE_HEADER_FIELDS` `:189-190` to the flags dword, or set it in 48's donor blob — decide at WP4 by
  reading both), mirror the change in the verify gate (`:1019-1025`), and widen `FEATHER_TEXELS` 4 → 6.
  Size-neutral (values + a header bit). Measure with `103_floor_seam_audit.py`: block-boundary max step 10 →
  ≤ 4, side means equal. Risk: the flag has never been seen by the composer or the DLL (both copy header bytes
  verbatim — the oracle + `flstream_tests.exe` 497/0 gate it) nor by our client (a stock WoW flag — the in-game
  look is the T2).
- Both ship as **kit `t1b-v39` / `KIT_VERSION 27`** through the documented chain (48 → 51 → 52 → 49, the gates in
  `python_scripts/pdv2-kit/README.md`, chunk-meta SQL + restart + MIG entry + launcher payload via script 102,
  kit and SQL in the same window). Script diffs are recorded in `docs/superpowers/notes/` (the Round A pattern).

### F1 — player-configured loot (WP7, not scheduled)

What this round leaves ready: pools keyed by name (a Classic/TBC pool is one more generator case), `expansion`
on every MATS row, `RollMaterial(expansionMask)` with a 50 % count malus when the mask is not "all", and the
panel's second-tab framework question answered by R4's canvas (child frames under `FLPDGenPanel`). Account
columns `cfg_loot_*` and the tab itself are WP7.

---

## 4. Work packages, order, evidence

| WP | Contents | Repo(s) | Plan | Operator round |
|---|---|---|---|---|
| **WP1** | L1 pools + script 106, L2 currencies, L3 materials, L4 gear injection, L5 legacy rares | mod-procedural-dungeon (+ workspace script 106) | `…-round-e-wp1-loot-pools-and-drops.md` (written) | Runde 31 |
| **WP2** | T1 purchase model: importer depth, cost conf + wire, BUY, migration, core statements, client UI, patch-9 | mod-forgotten-talents, azerothcore-wotlk, workspace script 30 | `…-round-e-wp2-forgotten-talents-purchase-model.md` (written, lives in the FT repo) | Runde 31 |
| **WP3** | R1 cap, R2 rooms, R3 debug, R4 preview | mod-procedural-dungeon | `…-round-e-wp3-cap-rooms-debug-preview.md` (written) | Runde 31 |
| **WP4** | K1 + K2 = kit v39 | workspace scripts 48/51 (+ notes diffs), module chunk-meta SQL | written at start | Runde 32 (+ launcher) |
| **WP5** | R5 event rooms | mod-procedural-dungeon | written at start | Runde 33 |
| **WP6** | T2 three legendary nodes + PD respawn + itemgen curse hooks | mod-forgotten-talents, mod-procedural-dungeon, mod-paragon-itemgen | written at start | Runde 33 |
| **WP7** | F1 | — | not scheduled | — |

Order: **WP1 → WP3 → WP2** (WP2 only needs the five item ids, fixed above, so it can run in parallel in its own
repo); one operator round for all three. WP1 and WP3 share the module branch `claude/pdv2-round-e-<sid>`; WP2
uses the same branch name in its repos. Each package ends with the round's pre-commit gates: `pdblock --batch 500`
/ `--decor-batch 3000` / `--roomcap 3000` (0 failures), codestyle, the staged worldserver build + install
(build-ready order), boot-log diff against the current 11-line baseline, and the deployed Lua copies.

Evidence per package: **T1** = the harness pins named in each plan, the build, the boot lines (`N loot pool
rows …`, `860 … spells / 700 nodes` for FT), `SELECT COUNT(*)` per pool, `.pdungeon v2 info` / `.forgotten cost`
outputs; **T2** = the operator's `tools/pd_testlauf_runde31.md` checklist (written with WP3's close): currency
drops per kill and their room factor, a chest and a boss item of the right pool and class, T4/T5 at difficulty
50/75, a legacy rare via GM-forced chance, the tree purchase loop, the cap raise after a death-free run, "0/14
rooms" against the slider, the preview canvas, silence in the log.

Host: nothing is host-relevant until a package merges; then one `MIG` entry per package (module-pull; WP2 is a
core-pull + module-pull + client-patch; WP4 a kit round like MIG-017). Conf keys: every new key has a code default
equal to `.dist`, so the host conf needs no edit unless the operator tunes.

Docs in the same commits: this spec's status, `CLAUDE.md`/`README.md` of both modules, `06-custom-ids.md`
(920105–920109, NPC 910551 reserved, FT 120860+ when WP6 lands), `09-db-tables.md` (the two loot tables, the
purchase table, the dropped ledger), `12-server-todo.md`, `claude_log.md` at the END.

---

## 5. Open questions parked (not blocking)

- Whether the run's currency should also reach a **group member who is dead** at the kill (default: yes, "on the
  map" is the test).
- Whether the five currencies should be tradeable (default: `bonding 1`, soulbound).
- The name and model of NPC 910551 and the exact faction (WP5).
- Which nodes are "the crafting area" / "the questing area" of the imported tree (WP6 reads the spell names).
- Sound on raid warnings (runde30 §7 question 8) — unchanged, still the operator's.
