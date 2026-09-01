-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: undead and demon creature packs (world database)
--
-- Task 11: PDv2 has, up to this file, drawn every spawn from one imported
-- set (mod_pdungeon_packs.sql, 84263-84290, shared with fl-underground-
-- dungeon). These two packs are the first that do not: pack 4 "Barrow Dead"
-- is undead (creature_template.type = 6) and pack 5 "Legion Rift" is demon
-- (type = 3). type = 5 is GIANT, not demon - Northrend ice giants would pass
-- a naive "type = 5" filter and spawn as a "demon" pack that is nothing of
-- the sort. Neither pack was built that way.
--
-- Pack ids start at 4 so the shipped mod_pdungeon_packs.sql's own
-- `DELETE ... WHERE id BETWEEN 1 AND 3` can never touch them, and this file's
-- own delete is scoped the same way in return.
--
-- theme 0 = any look, for the same reason the shipped packs use it: this is
-- crypt/demon trash, which reads fine under every look PDv2 currently ships,
-- not just one.
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when its range intersects the player's
-- chosen band [bandMin, bandMin+4], and every entry here is force-levelled to
-- 80 by OnBeforeCreatureSelectLevel on spawn regardless of what is written
-- here. All 24 members are native level 80/82 stock, so - same as the
-- shipped packs - the only band that selects either pack is 76..80.
--
-- Every entry below is verified against the running acore_world DB
-- (2026-09-01): creature_template.type matches the pack's theme, unit_class
-- is 1 or 2 (never the 4/8 basehp1-is-1-at-80 trap classes), faction is one
-- of the six factions already proven hostile to players for this project
-- (14, 16, 21, 90, 974, 2068 - against FactionTemplate.dbc field 5), and no
-- name carries a ` (1)`/` (2)`/` (3)` sniff-duplicate suffix. Full per-
-- creature table in .superpowers/sdd/task-11-report.md.
--
-- Not one creature_template row is edited or added, and none of the 24
-- entries below falls in 84263-84290 or is referenced by any other module's
-- SQL (checked by grep across modules/) - the whole three-table design
-- exists so this file never has to touch that range.
--
-- role: 0 melee, 1 range, 2 boss.
-- ----------------------------------------------------------------------------

DELETE FROM `pdungeon_pack_members` WHERE `packId` IN (4, 5);
DELETE FROM `pdungeon_packs` WHERE `id` IN (4, 5);

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (4, 'Barrow Dead',  0, 80, 80, 0, 1),
  (5, 'Legion Rift',  0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 4 "Barrow Dead" - 12 members, undead (creature_template.type = 6)
  (4, 31438, 0,     0, 100),  -- Shadow Vault Abomination     unit_class 1, 25200 hp
  (4, 31721, 0,     0, 100),  -- Frostbrood Sentry            unit_class 1, 25200 hp
  (4, 29974, 0,     0, 100),  -- Niffelem Forefather          unit_class 1, 15120 hp, loot
  (4, 28349, 0,     0, 100),  -- Risen Vrykul Berserker       unit_class 1, 12600 hp
  (4, 30921, 0,     0, 100),  -- Skeletal Runesmith           unit_class 1, 12600 hp, loot
  (4, 31278, 0,     0, 100),  -- Ravenous Ghoul                unit_class 1, 12600 hp
  (4, 31847, 0,     0, 100),  -- Scavenging Geist              unit_class 1, 12600 hp, loot
  (4, 30203, 1, 60015, 100),  -- Forgotten Depths High Priest  unit_class 2 (RANGE)
  (4, 32284, 1, 69211, 100),  -- Scourge Soulbinder             unit_class 2 (RANGE), loot
  (4, 30482, 1, 22088, 100),  -- Wrathstrike Gargoyle           unit_class 2 (RANGE), loot
  (4, 28350, 1, 60015, 100),  -- Risen Vrykul Magus             unit_class 1 caster - basemana 0, free kit only
  (4, 25352, 2,     0, 100),  -- Scourge Overlord               BOSS, rank 1, 252000 hp, loot-free
  -- Pack 5 "Legion Rift" - 12 members, demons (creature_template.type = 3)
  (5, 20427, 0,     0, 100),  -- Veneratus the Many             unit_class 1, 36860 hp, loot
  (5, 20403, 0,     0, 100),  -- Legion Destroyer               unit_class 1, 18430 hp
  (5, 31528, 0,     0, 100),  -- Plagued Felbeast               unit_class 1, 12600 hp
  (5, 23075, 0,     0, 100),  -- Legion Ring Infernal           unit_class 1, 11980 hp
  (5, 19746, 0,     0, 100),  -- Pit Breaker                    unit_class 1,  9215 hp
  (5, 18862, 0,     0, 100),  -- Dread Overlord                 unit_class 1,  9215 hp
  (5, 18871, 0,     0, 100),  -- Voidlord                       unit_class 1,  9215 hp
  (5, 31529, 1, 60015, 100),  -- Ravishing Betrayer             unit_class 2 (RANGE)
  (5, 18859, 1, 22088, 100),  -- Wrath Priestess                unit_class 2 (RANGE), loot
  (5, 18870, 1, 69211, 100),  -- Voidshrieker                   unit_class 2 (RANGE), loot, mana 11982
  (5, 24919, 1, 60015, 100),  -- Wrath Herald                   unit_class 2 (RANGE), loot
  (5, 29620, 2,     0, 100);  -- Dreadlord Mal'Ganis            BOSS, rank 1, 327600 hp, loot-free
