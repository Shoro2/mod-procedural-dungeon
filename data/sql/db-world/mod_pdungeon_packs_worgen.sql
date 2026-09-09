-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pack 6 "Shadowfang Pack" (world database)
--
-- A worgen pack: seven Shadowfang Keep worgen at five sizes, led by a boss that
-- is a DIFFERENT worgen model at twice the biggest trash scale. Every display
-- id below was measured through CreatureDisplayInfo.dbc -> CreatureModelData.dbc
-- on this box (2026-09-09), never inferred from a creature name:
--
--   display   203 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.00  3854
--   display   202 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.00  3857
--   display   729 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.00  3853
--   display   657 -> model   44 Creature\Worgen\Worgen.mdx    scale 0.85  3855
--   display   736 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.15  3859
--   display   524 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.15  3914
--   display  1098 -> model   44 Creature\Worgen\Worgen.mdx    scale 1.45  2529
--   display 26793 -> model 2888 Creature\NorthrendWorgen\NorthrendWorgen.mdx
--                                                             scale 3.00  27580 (BOSS)
--
-- creature_template_model.DisplayScale is 1 on all eight, so the DBC scale is
-- the scale that reaches the client.
--
-- The trash ladder 0.85 -> 1.45 is what makes seven bodies read as a PACK rather
-- than seven copies; the boss then breaks that ladder on BOTH axes at once - a
-- different model file at scale 3.00, more than twice the biggest trash member -
-- which is what a boss silhouette has to do.
--
-- All eight are creature_template.type 7 HUMANOID, rank 1, exp 0 (exp 2 for the
-- boss), npcflag 0, VehicleId 0, flags_extra 0, dynamicflags 0, unit_flags 0 or
-- 0x40, unit_flags2 0x800 (REGENERATE_POWER), speed_walk 1.0 / speed_run
-- 1.14286 on every one of them, and NONE carries a ScriptName. AIName 'SmartAI'
-- on all eight is irrelevant: CreatureAISelector::SelectAI asks
-- sScriptMgr->GetCreatureAI BEFORE the AIName factory
-- (CreatureAISelector.cpp:78-88) and PDv2's binder is an AllCreatureScript that
-- claims every ownerless non-critter on map 760 (PDv2CreatureAI.cpp:1889-1926).
--
-- Factions, re-measured from FactionTemplate.dbc field 5:
--   24 (Shadowfang, the seven trash) ourMask 0x8, friendMask 0x0, enemyMask 0x1
--   16 (Monster, the boss)           ourMask 0x8, friendMask 0x0, enemyMask 0x1
-- Both are hostile to players (enemyMask 0x1 = FACTION_MASK_PLAYER) and NEITHER
-- is hostile to the other: 16's enemyFaction list is empty and 24's ourMask 0x8
-- is not in 16's hostileMask 0x1, so the boss and the adds beside him cannot
-- fight each other. Mixing factions inside one pack is corpus-normal - shipped
-- pack 4 mixes five (14 / 16 / 21 / 974 / 2068) and pack 5 mixes three.
--
-- Measured on this box (2026-09-09) against the running acore_world:
-- creature_template joined to creature_classlevelstats at level 80, so hp80 in
-- the member comments below is the health the module will ACTUALLY hand the
-- creature at difficulty 0, not the template's own level-19 number.
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 1 - creature_template.DamageModifier, the column the first pass
-- never measured
--
-- DamageModifier is applied straight to melee output in
-- Creature::UpdateDamagePhysical (StatSystem.cpp:1169):
--
--   minDamage = ((weaponMinDamage + AttackPower/14 * BaseVariance)
--                * DamageModifier * BaseAttackTime/1000) ...
--
-- with weaponMin/Max from Creature::SelectLevel (Creature.cpp:1538-1544,
-- damage_base x1.0 / x1.5). PDv2 force-levels every spawn to 80
-- (PDv2Scaling.cpp:215-230) and then applies the difficulty dial as a pure
-- MULTIPLIER on top (PDv2Scaling.cpp:133-146), so a lopsided DamageModifier is
-- amplified, never normalised. At level 80, with BaseAttackTime 2000 and
-- BaseVariance 1 on all eight, that resolves to:
--
--   unit_class 1 (AP 642, damage_base 47.2377):  186.19 x dm .. 233.43 x dm
--   unit_class 2 (AP 608, damage_base 44.2013):  175.26 x dm .. 219.46 x dm
--
-- The FIRST version of this pack shipped 3852 Shadowfang Bloodhowler and 3860
-- Shadowfang Tainted One, both at DamageModifier 7.5, where every other worgen
-- in the Shadowfang block carries 1.7. Measured per 2.0 s swing at difficulty 0:
--
--   3852 (uc1, dm 7.5)   1396 - 1751   ~787 dps   4.4x its own packmates
--   3860 (uc2, dm 7.5)   1314 - 1646   ~740 dps   4.4x its own packmates
--   3927 (uc1, dm 3.3)    614 -  770   ~346 dps   the old boss - out-hit 2.3x
--   dm 1.7 trash (uc1)    317 -  397   ~178 dps
--   dm 1.7 trash (uc2)    298 -  373   ~168 dps
--
-- Worse than a local imbalance: PDv2PackMgr::SelectSpawns fills ONE merged trash
-- pool and one merged boss pool across every qualifying pack
-- (PDv2PackMgr.cpp:437-465), so those two would have turned up as ordinary trash
-- in Crypt Horrors, Barrow Dead and Legion Rift rooms, hitting three to four
-- times harder than anything else in the room and above the previous
-- module-wide trash ceiling (20427 Veneratus, ~609 dps).
--
-- Both are gone. Every trash member of this pack now carries DamageModifier
-- **1.7** - a spread of 1.00x, measured column by column - and the boss carries
-- 4.6, i.e. 2.7x his own trash, which is the direction a boss is supposed to
-- differ in. 3852 and 3860 are additionally unfinished templates (zero rows in
-- `creature`, lootid 0, skinloot 0, 0 gold, no smart_scripts, no AIName, empty
-- subname, speed_walk 1.2 where the whole block is 1.0); the "economy-neutral
-- filler" property the first pass valued them for is a symptom of that state,
-- not a design.
--
-- Replacements, both faction 24, both Creature\Worgen\Worgen.mdx, both
-- DamageModifier 1.7, both finished content with world spawns (11 and 1):
--   3852 -> 3914 Rethilgore <The Cell Keeper>  uc1, 21368 hp, display  524 @1.15
--   3860 -> 2529 Son of Arugal                 uc1, 16026 hp, display 1098 @1.45
--
-- ----------------------------------------------------------------------------
-- REVIEW FIX 2 - the boss is now a step up on all THREE axes
--
-- The first version shipped 3927 Wolf Master Nandos and failed every axis a boss
-- is judged on:
--
--   health      32052 hp = 2.00x his own trash and 21% of the shipped boss floor
--               (149560 / 252000 / 327600). With V2.BossRoomAdds = 2 in the
--               deployed conf, and the adds drawn from the ROOM's pack, a Nandos
--               beside pack-8 trash (50400 hp each) had 57% less health than
--               each add standing next to him.
--   silhouette  the same Worgen.mdx as his own trash at 15% more scale than the
--               biggest trash member - one more rung on a ladder the player had
--               already seen four times.
--   abilities   two of his four kit rows (42397 Rend Flesh, 8599 Enrage) were
--               rows his own trash already carried, and his difficulty-75
--               "reward" row 15588 Thunderclap measures 251-259 damage.
--
-- Every faction-24 stock creature was enumerated by hp80: the whole Shadowfang
-- era tops out at 4275 Archmage Arugal / 10000 Arugal on 42740, which is 2.67x
-- the trash and still under the 3-10x band a boss should sit in (and 10000
-- carries unit_flags 0x300 IMMUNE_TO_PC/NPC, so it is unusable anyway). Every
-- creature drawing Worgen.mdx or NorthrendWorgen.mdx was then enumerated the
-- same way; exactly three land in the 48078-160260 band and two are
-- disqualified:
--
--   30772 Frenzied Worgen (1)  126000  unit_flags 0x2000000 NOT_SELECTABLE, and
--                                      a ' (1)' sniff-duplicate name suffix
--   26683 Frenzied Worgen      126000  unit_flags 0x2000000 NOT_SELECTABLE
--   27580 Selas                 75600  clean - taken
--
-- (17521 The Big Bad Wolf 322525 is 20x the trash, a rank 3 world boss and a
-- comedy name; 9029 Eviscerator 38466 and 4279 Odo 26710 are under the band;
-- 81211 Xaxtan and 930126 Packmaster Ragetooth are mod-turtle-content entries
-- and a cross-module dependency this pack must not take.)
--
-- 27580 Selas, measured: type 7, unit_class 1, faction 16, rank 1, exp 2,
-- DamageModifier 4.6, HealthModifier 6, npcflag 0, unit_flags 0x40,
-- unit_flags2 0x800, flags_extra 0, VehicleId 0, ScriptName '',
-- CreatureImmunitiesId 0 (no immunity row at all), no creature_template_addon
-- row, one world spawn, no creature_queststarter / creature_questender row.
--
--   health      12600 (basehp2, exp 2) x HealthModifier 6 = 75600 hp = 4.72x the
--               16026 melee trash - inside the 3-10x band - and 51% of the
--               shipped 149560 floor, where Nandos was 21%.
--   silhouette  Creature\NorthrendWorgen\NorthrendWorgen.mdx, a DIFFERENT model
--               file from all seven trash members, at scale 3.00 against a trash
--               ladder that stops at 1.45.
--   abilities   four rows, NONE of which any member of this pack carries, and
--               the difficulty-75 row is the heaviest thing in his kit
--               (67860 Impale, 17672-19828) instead of the lightest. See
--               mod_pdungeon_member_spells_worgen.sql.
--   damage      dm 4.6 -> 857-1074 per 2.0 s swing (~483 dps), 2.7x his own
--               trash, where Nandos at 3.3 was 1.94x and was out-hit 2.3x by two
--               of his own trash members.
--
-- The first research pass rejected Selas because he "draws NorthrendWorgen and
-- is visibly a different beast from the Shadowfang seven". That is exactly the
-- property the pack was missing, and it is why he is here.
--
-- Two things about him the operator should know rather than discover:
--   * he is faction 16, not 24 - a mixed-faction pack; see the faction block
--     above, neither side is hostile to the other; and
--   * quest 12164 "Hour of the Worg" (Grizzly Hills, level 73-75) lists 27580 as
--     a kill objective, so a level-80 player who still has that quest in the log
--     can collect the credit from a pack-6 boss room. It is one credit on an
--     abandoned low-level quest, it costs nothing and skips no content, and no
--     other member of this pack has any quest exposure at all
--     (creature_queststarter, creature_questender and quest_template
--     RequiredNpcOrGo* were checked for all eight).
--
-- ----------------------------------------------------------------------------
-- EXCLUDED members, each for a MEASURED reason
--
--  3851 Shadowfang Whitescalp  creature_template_addon.auras = 7940 "Immunity:
--       Frost" (SPELL_AURA_SCHOOL_IMMUNITY, duration -1) AND creature_immunities
--       row -6 with SchoolMask 0x10 FROST. It would spawn permanently immune to
--       a whole school: a frost mage or a frost death knight could never damage
--       it, and PDv2InstanceScript's _roomAlive counter would never reach 0 for
--       that player.
--  3852 Shadowfang Bloodhowler  DamageModifier 7.5 - see REVIEW FIX 1.
--  3860 Shadowfang Tainted One  DamageModifier 7.5 - see REVIEW FIX 1.
--  3927 Wolf Master Nandos      not a step up on any axis - see REVIEW FIX 2.
--  4279 Odo the Blindwatcher    right DamageModifier (1.7) but
--       CreatureImmunitiesId -229 = MechanicsMask CHARM|DISORIENTED|FEAR|ROOT|
--       SLEEP|SNARE|FREEZE|KNOCKOUT|POLYMORPH|BANISH|SHACKLE|TURN|DAZE|SAPPED.
--       Tolerable on a boss, wrong on trash: no player could crowd-control it at
--       all. 3886 Razorclaw the Butcher carries the same row AND dm 2.5.
--  26683 / 30772 Frenzied Worgen  unit_flags 0x2000000 NOT_SELECTABLE.
--  81211 Xaxtan / 930126 Packmaster Ragetooth  mod-turtle-content entries.
--
-- 3914 Rethilgore DOES carry a CreatureImmunitiesId, -93, but it is
-- MechanicsMask 0x800010 = FEAR | HORROR only, with SchoolMask 0 - a worgen that
-- cannot be feared, which is flavour rather than a wall and nothing like the
-- 3851 school immunity. Recorded here rather than left to be discovered.
--
-- ----------------------------------------------------------------------------
-- ECONOMY - measured against creature_loot_template / item_template, not
-- inferred from the lootid column
--
-- Seven of the eight members carry a native loot table (2529, 3853, 3854, 3855,
-- 3857, 3859, 3914, 27580), seven are skinnable (skinloot 100005/100006/100007;
-- the boss is skinloot 0), and gold spans 32-338 copper for the Shadowfang seven
-- and 1231-2051 copper for the boss, before PDv2's own loot multiplier.
--
-- ONE item deserves a decision rather than a discovery:
--
--   3914 Rethilgore drops 5254 Rugged Spaulders (Quality 3 blue, item level 20)
--   at Chance 100 / GroupId 0 - on EVERY kill, and he is trash, not a boss. It
--   is worthless at level 80 (a few silver at a vendor) but it will land on
--   every Rethilgore in every pack-6 room. This is a deliberate, measured trade:
--   he is the ONLY faction-24 Worgen.mdx creature besides 2529 that carries
--   DamageModifier 1.7 without a school immunity or a full crowd-control
--   immunity, and DamageModifier is the axis the review blocked on. If the blue
--   flood is unwanted the fix is data, not code: drop this row's `weight`, or
--   swap 3914 for 920 Nightbane Tainted One (faction 24, Worgen.mdx display 1098
--   @1.45, no blues) - which trades an economy nit for a damage and health
--   outlier, since 920 is DamageModifier 1.0 and 4701 hp at level 80.
--
-- The BOSS is now loot-clean: 27580 Selas drops only greys and whites (43851 Fur
-- Clothing Scraps 41.6%, 33470 Frostweave Cloth 25%, 33454 Salted Venison 9.9%,
-- 33444 Pungent Seal Whey 3.7%), where the previous boss 3927 dropped 3748
-- Feline Mantle (blue) at 60% and 6314 Wolfmaster Cape (blue) at 40% on nearly
-- every boss kill. Swapping the boss removed a blue flood as well as fixing the
-- three step-up axes.
--
-- No member of this pack drops anything above Quality 3, and nothing epic.
--
-- ----------------------------------------------------------------------------
-- theme 0 = any look, same as all five shipped packs: a pack whose theme is
-- neither 0 nor the live V2.Theme is invisible to the loader and the dungeon
-- fills with the placeholder creature (PDv2PackMgr.cpp:96-121). The deployed
-- ProceduralDungeon.V2.Theme is 2 and the loader's WHERE accepts theme 0, so
-- this pack is visible.
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when [level_min, level_max] intersects the
-- player's band [bandMin, bandMin+4]. These are native level 18-25 Shadowfang
-- Keep creatures and a native level 75 Grizzly Hills worgen, but every spawn is
-- force-levelled to 80 by OnBeforeCreatureSelectLevel (PDv2Scaling.cpp:215-230)
-- regardless of what is written here.
--
-- rank 1 on every member, where the shipped packs' trash is rank 0. With every
-- Rate.Creature.Elite.* key at 1 in the deployed worldserver.conf that costs and
-- buys nothing beyond the gold dragon on the nameplate - it is a LOOK change, it
-- is intentional (vanilla dungeon trash is elite), and the operator should
-- expect it rather than report it.
--
-- The pack keeps the name 'Shadowfang Pack'. pdungeon_packs.name is read once at
-- PDv2PackMgr.cpp:136 and never again - no log line, no command output, no UI -
-- so it is an internal label, and renaming it now would only churn the
-- identifier every report, review and queue entry about this pack already uses.
--
-- The CREATE TABLE IF NOT EXISTS blocks below are FREE INSURANCE, not an
-- ordering fix, and the first version of this file claimed otherwise. Measured:
-- UpdateFetcher::PathCompare compares filename().string()
-- (UpdateFetcher.cpp:521-524), and '.' (0x2E) sorts before '_' (0x5F), so
-- 'mod_pdungeon_packs.sql' - the file that owns these two CREATE statements -
-- sorts BEFORE 'mod_pdungeon_packs_worgen.sql' on a fresh database as well as on
-- this one. The repetition is harmless duplication rather than a requirement.
-- (It IS load-bearing in the companion file: 'm' < 'p' puts
-- 'mod_pdungeon_member_spells_worgen.sql' ahead of 'mod_pdungeon_packs.sql'.)
--
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM, and zero
-- spell_dbc rows. Every entry is stock 3.3.5a, none falls in 84263-84290, none is
-- used by packs 1-5 (SELECT packId, entry FROM pdungeon_pack_members WHERE entry
-- IN (...) returns 0 rows) and no other module's SQL references any of them as a
-- creature (a grep across modules/ hits only the coordinate fragment
-- '...,2529.99,...' and the ITEM id 3914 in mod-turtle-content loot tables).
--
-- role: 0 melee, 1 range, 2 boss.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_packs` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  -- 0 = ANY theme. See the header: this pack is worgen, which reads as well
  -- in a dark Gilneas street as in a keep, so it is theme-agnostic.
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

-- Idempotent re-apply: delete THIS pack's own rows, then insert. The delete is
-- scoped to packId/id 6 alone - never a BETWEEN, never a DROP - so an operator
-- who added packs of their own, and the concurrently authored sibling packs,
-- keep every row they own.
DELETE FROM `pdungeon_pack_members` WHERE `packId` = 6;
DELETE FROM `pdungeon_packs` WHERE `id` = 6;

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (6, 'Shadowfang Pack', 0, 80, 80, 0, 1);

-- casterSpellId is the role-1 member's FALLBACK filler and nothing more:
-- PDv2CreatureAI::BuildKit reads it only when no slot-0 row survived
-- (PDv2CreatureAI.cpp:1388-1396), and a role-1 member with casterSpellId 0 AND
-- no member_spells rows is silently DEMOTED to melee at load
-- (PDv2PackMgr.cpp:167-176). Each of the three values below is identical to that
-- member's own slot-0 row in mod_pdungeon_member_spells_worgen.sql, the way the
-- shipped files keep them, and each reaches at least
-- ProceduralDungeon.V2.CastRangeYd (25.0 in the DEPLOYED
-- dcore\configs\modules\mod_procedural_dungeon.conf): 69211 Shadow Bolt 30 yd,
-- 60015 Shadow Bolt 40 yd (SpellRange.dbc, measured).
INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 6 "Shadowfang Pack" - 8 members: 4 melee, 3 range, 1 boss.
  -- Every trash member is faction 24, rank 1, exp 0, DamageModifier 1.7
  -- (317-397 per 2.0 s swing for uc1, 298-373 for uc2 - a 1.06x spread).
  -- MELEE (role 0)
  (6, 3914, 0,     0, 100),  -- Rethilgore              uc1, 21368 hp, dm 1.7, display 524  scale 1.15 - imm -93 fear/horror, blue at 100%: see ECONOMY
  (6, 3854, 0,     0, 100),  -- Shadowfang Wolfguard    uc1, 16026 hp, dm 1.7, display 203  scale 1.00 - armoured guard skin, the line-holder
  (6, 3857, 0,     0, 100),  -- Shadowfang Glutton      uc1, 16026 hp, dm 1.7, display 202  scale 1.00 - the feeder
  (6, 3859, 0,     0, 100),  -- Shadowfang Ragetooth    uc1, 16026 hp, dm 1.7, display 736  scale 1.15 - carries the pack's single CC
  -- RANGE (role 1) - casterSpellId mirrors the member's own slot-0 filler
  (6, 3853, 1, 69211, 100),  -- Shadowfang Moonwalker   uc2, 12822 hp / 7988 mana, dm 1.7, display 729  scale 1.00 - the primary nuker
  (6, 3855, 1, 60015, 100),  -- Shadowfang Darksoul     uc2, 12822 hp / 7988 mana, dm 1.7, display 657  scale 0.85 - the runt, a skulking caster hanging back
  (6, 2529, 1, 60015, 100),  -- Son of Arugal           uc1, 16026 hp, dm 1.7, display 1098 scale 1.45 - Arugal's shadow-made worgen, replaces the dm-7.5 3860
  -- BOSS (role 2)
  (6, 27580, 2,    0, 100);  -- Selas                   uc1, 75600 hp, dm 4.6, exp 2, faction 16, display 26793 NorthrendWorgen scale 3.00 - see REVIEW FIX 2
