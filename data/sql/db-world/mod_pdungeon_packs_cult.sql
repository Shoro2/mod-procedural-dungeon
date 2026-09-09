-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the Cult of the Damned pack (world database)
--
-- Pack 7 "Cult of the Damned" - Scholomance. Four type 6 risen undead and
-- three type 7 robed cultists, all faction 233 (Scholomance), plus Darkmaster
-- Gandling on faction 21 (Scourge). Where pack 4 "Barrow Dead" is Northrend
-- Scourge, this one is the cult that MADE it: the necromancers' school, its
-- raised soldiery and its headmaster.
--
-- Pack id 7 is free: SELECT MAX(id) FROM pdungeon_packs = 5 on this box
-- (measured 2026-09-09), and the shipped files' own deletes reach only
-- `id BETWEEN 1 AND 3` (mod_pdungeon_packs.sql) and `id IN (4, 5)`
-- (mod_pdungeon_packs_undead_demon.sql), so neither can ever touch pack 7.
-- This file's delete is scoped to id 7 alone in return - it never uses a
-- BETWEEN and it never DROPs, so an operator's own packs survive a re-apply.
--
-- theme 0 = any look, for the same reason all five shipped packs use it:
-- robed cultists and risen dead read fine under every look PDv2 ships, not
-- just one. A pack whose theme is neither 0 nor the live V2.Theme is
-- invisible to the loader and the dungeon fills with the placeholder
-- creature (PDv2PackMgr.cpp:96-121).
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when its range intersects the player's
-- chosen band [bandMin, bandMin+4]. Every entry here is native level 55-60
-- vanilla stock and is force-levelled to 80 on spawn by
-- OnBeforeCreatureSelectLevel (PDv2Scaling.cpp:215-230) regardless of what
-- is written here, so - same as the shipped packs - the only band that
-- selects this pack is 76..80.
--
-- ----------------------------------------------------------------------------
-- WHAT THIS PACK WEIGHS, AND WHY IT IS NOT A BUG
--
-- All eight members are `rank` 1 (elite) and `exp` 0, measured in
-- creature_template on 2026-09-09. `exp` 0 means the level-80 health comes
-- from creature_classlevelstats.basehp0 - the SMALL column - times
-- HealthModifier:
--
--   entry  name                     uc  hp80    mana80  display(s)
--   10486  Risen Warrior             1  26710        0  7847
--   10488  Risen Construct           1  26710        0  12074
--   10489  Risen Guard               1  16026        0  7848
--   11551  Necrofiend                1  16026        0  11178
--   10477  Scholomance Necromancer   2  12822    11982  11163,11154,11155,11156
--   10476  Scholomance Necrolyte     2  12822    11982  11161,11151,11152,11175
--   10471  Scholomance Acolyte       2  12822    11982  11157,11145,11146,11173
--   1853   Darkmaster Gandling       2  85480    11982  11070
--
-- so the trash lands in 12822..26710, the same band as the shipped 84263-84290
-- trash (12600..32000), and the difficulty dial does the rest. `rank` 1 costs
-- and buys nothing beyond the gold elite dragon on the nameplate: every
-- Rate.Creature.Elite.* key in the deployed C:\wowstuff\dcore\configs\
-- worldserver.conf is 1, so _GetHealthMod/_GetDamageMod return 1.0. The
-- shipped packs' trash is `rank` 0, so the elite frames ARE a visible
-- difference - intentional (vanilla dungeon trash is elite), not a defect.
--
-- The three casters each publish FOUR creature_template_model rows, so a room
-- of Necrolytes is four different robed cultists rather than one repeated
-- model. That is the whole reason these three were picked over the
-- single-model Scholomance stock.
--
-- ----------------------------------------------------------------------------
-- THE BOSS, AND THE ONE OPEN NUMBER
--
-- 1853 Darkmaster Gandling: 85480 hp at level 80, which is 57% of the
-- shipped boss floor (149560 on 84288-84290; 252000 on 25352, 327600 on
-- 29620). This was decided rather than discovered. Every Scholomance and
-- Stratholme boss is `exp` 0 and therefore multiplies the same small
-- basehp0 column, so the whole era tops out low - Instructor Malicia 59836
-- (and npcflag 2 QUESTGIVER, which would carry a quest marker and a
-- right-click menu into the dungeon), The Ravenian 64104, Rattlegore 51288,
-- Jandice Barov 51288, Lord Alexei Barov 48078. The WotLK-era Cult of the
-- Damned bosses that WOULD hit the band all fail for another reason: 29112
-- Gothik the Harvester (252000) is faction 2084, which FactionTemplate.dbc
-- field 5 proves FRIENDLY to players (enemyGroupMask 0, friendGroupMask 1);
-- 16061 Instructor Razuvious and 36855 Lady Deathwhisper are `rank` 3 at
-- ~3.0M. Gandling ships: he is the single most on-theme NPC available and
-- the gap is 1.75x rather than 4.7x. The difficulty dial already multiplies
-- boss health, and swapping him later is a one-line change to this file.
--
-- His `ScriptName` boss_darkmaster_gandling never binds on map 760:
-- CreatureAISelector::SelectAI asks sScriptMgr->GetCreatureAI FIRST
-- (CreatureAISelector.cpp:78-88) and PDv2's binder is an AllCreatureScript,
-- so it wins over both a template ScriptName and the AIName 'SmartAI' every
-- member here carries. Nothing of Scholomance's own scripting runs.
--
-- Both factions were re-measured against FactionTemplate.dbc field 5, which
-- is the check that disqualified Gothik: 233 (enemyGroupMask 1, friendMask 0)
-- and 21 (enemyGroupMask 1, friendMask 8) are both HOSTILE to players.
--
-- Flags measured on all eight, because each of these fails SILENTLY in game:
-- no name carries a ` (1)`/` (2)`/` (3)` sniff-duplicate suffix, npcflag is 0
-- on every row, unit_flags is 64 (UNK_6) on the seven Scholomance entries and
-- 0 on Gandling - never 0x2000000 NOT_SELECTABLE and never the 0x300 immunity
-- pair - flags_extra is 0 except 2048 USE_OFFHAND_ATTACK on 10488 (harmless),
-- VehicleId is 0, and the five creature_template_addon rows that exist
-- (1853, 10471, 10477, 10486, 10488) carry an EMPTY `auras` column, so
-- nothing spawns with a permanent school immunity or an invisibility aura.
--
-- ----------------------------------------------------------------------------
-- casterSpellId - the RANGE mob's filler, and a FALLBACK only
--
-- pdungeon_member_spells is the truth for every spell a mob casts, including
-- the filler. This column is read only when a role-1 member has no slot-0 row
-- at all, and a role-1 member with casterSpellId 0 AND no member_spells rows
-- is silently DEMOTED to melee at load (PDv2PackMgr.cpp:157-166). All three
-- casters therefore carry both, and the two are deliberately identical:
--
--   10477 -> 60015 Shadow Bolt  40 yd, 3.0 s cast, 0 mana / 0 %
--   10476 -> 61562 Shadow Bolt  40 yd, 1.5 s cast, 0 mana / 0 %
--   10471 -> 69211 Shadow Bolt  30 yd, 2.2 s cast, 0 mana / 0 %
--
-- Each reaches at least ProceduralDungeon.V2.CastRangeYd (25), which is the
-- point of the column: a filler that cannot reach is a mob that silently
-- never lands anything (PDv2CreatureAI.cpp:1537). Full argument, and the
-- per-row identity evidence, in mod_pdungeon_member_spells_cult.sql.
--
-- ----------------------------------------------------------------------------
-- THE ECONOMY CHANGE THE OPERATOR SHOULD HEAR ABOUT BEFORE THE FIRST RUN
--
-- Measured against creature_loot_template / item_template directly, not the
-- lootid column alone: ALL EIGHT members carry a native loot table (15-25
-- item rows each), 7 of 8 drop gold (387-2890 copper on the trash, 2268-2967
-- on Gandling; only 11551 Necrofiend is 0/0), and none is skinnable
-- (skinloot 0 on all eight). The shipped 84263-84290 all have lootid 0, and
-- both Task 11 bosses were deliberately lootid 0, so this is a real change:
--
--   * 13937 Headmaster's Charge - epic, item level 62, 2% from Gandling, and
--     14514 Pattern: Robe of the Void - epic, 7% - plus ~20 blues (Tombstone
--     Breastplate, Silent Fang, Witchblade, the level-60 tier-0 helms) at
--     0-3%. In an infinitely repeatable procedural dungeon that is a farmable
--     vanilla epic, twice over.
--   * Trash blues are mostly trace (18335 Pristine Black Diamond 0.10-0.23%,
--     14536 Bonebrace Hauberk 0.02-0.06%) with three exceptions worth naming:
--     16722 Lightforge Bracers 3.64% on 10486, 16710 Shadowcraft Bracers
--     3.46% on 10488, 16702 Dreadmist Belt 1.70% on 10477.
--
-- If any of that is unwanted the fix is DATA, not code: drop the boss's
-- `weight`, or disable the pack. Editing creature_template.lootid here would
-- retune Scholomance itself.
--
-- ----------------------------------------------------------------------------
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM.
-- Not one of the eight entries falls in 84263-84290, and none of them is
-- referenced by any other pack: SELECT packId, entry FROM
-- pdungeon_pack_members WHERE entry IN (...) returns 0 rows and
-- SELECT COUNT(*) FROM pdungeon_member_spells WHERE entry IN (...) returns 0
-- (measured 2026-09-09). No spell_dbc row and no custom id is created here.
--
-- The two CREATE TABLE IF NOT EXISTS blocks below are REPEATED from
-- mod_pdungeon_packs.sql on purpose. The AC updater applies files in
-- FILENAME order (UpdateFetcher.cpp:64-96), and while `mod_pdungeon_packs_
-- cult.sql` happens to sort after it, the member_spells half of this pack
-- does not - so both new files carry their own IF NOT EXISTS block and a
-- fresh database can apply them in any order. Free on this box, correct on a
-- clean one. This is a deliberate deviation from the _undead_demon
-- precedent, which has the same exposure and has simply never been bitten.
--
-- role: 0 melee, 1 range, 2 boss.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_packs` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  `theme` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `level_min` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `level_max` TINYINT UNSIGNED NOT NULL DEFAULT 80,
  `unlock_dlvl` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS `pdungeon_pack_members` (
  `packId` INT UNSIGNED NOT NULL,
  `entry` INT UNSIGNED NOT NULL,
  `role` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `casterSpellId` INT UNSIGNED NOT NULL DEFAULT 0,
  `weight` SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  PRIMARY KEY (`packId`, `entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own pack, then insert. Never DROP -
-- an operator who added packs of their own keeps them, and the id is named
-- explicitly rather than swept with a BETWEEN.
DELETE FROM `pdungeon_pack_members` WHERE `packId` = 7;
DELETE FROM `pdungeon_packs` WHERE `id` = 7;

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (7, 'Cult of the Damned', 0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 7 "Cult of the Damned" - 8 members, 3 range, 1 boss
  -- MELEE (role 0) - the raised dead, type 6 UNDEAD, faction 233
  (7, 10486, 0,     0, 100),  -- Risen Warrior            uc1, 26710 hp, loot - the cult's raised soldiery
  (7, 10488, 0,     0, 100),  -- Risen Construct          uc1, 26710 hp, loot - stitched flesh, flags_extra 2048
  (7, 10489, 0,     0, 100),  -- Risen Guard              uc1, 16026 hp, loot - shield-bearer, carries the pack CC
  (7, 11551, 0,     0, 100),  -- Necrofiend               uc1, 16026 hp, loot - the crawling thing, no gold
  -- RANGE (role 1) - the robed cult, type 7 HUMANOID, faction 233, 4 models each
  (7, 10477, 1, 60015, 100),  -- Scholomance Necromancer  uc2, 12822 hp, 11982 mana (RANGE) - senior caster
  (7, 10476, 1, 61562, 100),  -- Scholomance Necrolyte    uc2, 12822 hp, 11982 mana (RANGE) - middle rank
  (7, 10471, 1, 69211, 100),  -- Scholomance Acolyte      uc2, 12822 hp, 11982 mana (RANGE) - novice, Mana Burn
  -- BOSS (role 2) - type 7, faction 21 (Scourge), rank 1, npcflag 0
  (7,  1853, 2,     0, 100);  -- Darkmaster Gandling      BOSS, 85480 hp, loot (see the economy block)
