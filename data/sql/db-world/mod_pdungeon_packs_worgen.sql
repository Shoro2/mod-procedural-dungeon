-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pack 6 "Shadowfang Pack" (world database)
--
-- The first worgen pack. Every one of the eight members draws the SAME model
-- file - Creature\Worgen\Worgen.mdx - which was measured display by display
-- through CreatureDisplayInfo.dbc -> CreatureModelData.dbc, never inferred
-- from the creature name:
--
--   display 202 -> model 44 Worgen.mdx  scale 1.00   3852, 3857
--   display 203 -> model 44 Worgen.mdx  scale 1.00   3854
--   display 574 -> model 44 Worgen.mdx  scale 1.30   3860
--   display 657 -> model 44 Worgen.mdx  scale 0.85   3855
--   display 729 -> model 44 Worgen.mdx  scale 1.00   3853
--   display 736 -> model 44 Worgen.mdx  scale 1.15   3859
--   display 11179 -> model 44 Worgen.mdx scale 1.50  3927 (boss)
--
-- The scale ladder 0.85 -> 1.50 is the whole point of the roster: one model
-- at seven sizes reads as a PACK - a runt skulking at the back, a bigger
-- brute in front, a beastmaster half again the size of his own - where eight
-- creatures at scale 1.00 would read as eight copies of one mob.
--
-- All eight are creature_template.type 7 HUMANOID, faction 24, rank 1,
-- exp 0, and none carries a ScriptName that could bind (PDv2's binder is an
-- AllCreatureScript and wins over both ScriptName and AIName on map 760 -
-- CreatureAISelector.cpp:78-88). Faction 24 was re-measured against
-- FactionTemplate.dbc field 5: enemyGroupMask 1, i.e. genuinely hostile to
-- players, not merely "the Shadowfang faction".
--
-- Measured on this box (2026-09-09) against the running acore_world:
-- creature_template joined to creature_classlevelstats at level 80, so the
-- hp80/mana80 in the member comments below is the health the module will
-- ACTUALLY give the creature at difficulty 0, not the template's own level-19
-- number.
--
-- Flag traps, all checked and all clear on these eight: no ` (1)`/` (2)`/
-- ` (3)` sniff-duplicate name suffix, no UNIT_FLAG_NOT_SELECTABLE, no
-- IMMUNE_TO_PC/NPC, no CREATURE_FLAG_EXTRA_HARD_RESET, no VehicleId, no
-- npcflag, and no creature_template_addon aura on any member (3853 has an
-- addon row, but its `auras` column is empty).
--
-- 3851 Shadowfang Whitescalp is DELIBERATELY NOT a member. Its
-- creature_template_addon.auras is 7940 "Immunity: Frost" -
-- SPELL_AURA_SCHOOL_IMMUNITY with duration -1 - so it would spawn permanently
-- immune to a whole school: a frost mage or a frost death knight could never
-- damage it, and PDv2InstanceScript's _roomAlive counter would never reach 0
-- for that player. 3860 Shadowfang Tainted One takes its slot instead (no
-- addon row at all, lootid 0, and a 1.30 scale that adds to the ladder).
--
-- ----------------------------------------------------------------------------
-- OPEN DECISION, RECORDED RATHER THAN HIDDEN: the boss is soft
--
-- 3927 Wolf Master Nandos lands on 32052 hp at level 80 (basehp0 5342 x
-- HealthModifier 6, exp 0) against a shipped boss band of 149560 (84288-
-- 84290), 252000 (25352) and 327600 (29620) - a 4.7x gap. Boss rooms draw
-- from the role-2 pool across ALL packs (Round A A5), so he will also turn up
-- guarding rooms full of Barrow Dead undead and read as a trash mob wearing a
-- boss frame.
--
-- Every worgen-model stock alternative was measured, and each fails for its
-- own reason: 4279 Odo the Blindwatcher 26710 and 3886 Razorclaw the Butcher
-- 21368 are softer still; 9029 Eviscerator 38466 is a Blackrock Depths arena
-- worgen with an off-theme name; 27580 Selas 75600 draws NorthrendWorgen and
-- is visibly a different beast from these seven; 26683 Frenzied Worgen 126000
-- carries UNIT_FLAG_NOT_SELECTABLE and is unusable; 17521 The Big Bad Wolf
-- 322525 is in the band but is a rank 3 world boss with a comedy name.
--
-- Decision: ship 3927. He is the right model at the right scale, keeps the
-- pack single-faction and single-model, and carries no script. The difficulty
-- dial already multiplies boss health (x6 at difficulty 100), and swapping the
-- boss later is a ONE-LINE change to the last row of this file.
--
-- ----------------------------------------------------------------------------
-- ECONOMY - what this pack adds to the loot stream
--
-- Six of the eight members carry a native creature_loot_template table
-- (3853, 3854, 3855, 3857, 3859, 3927), six are skinnable (skinloot 100006/
-- 100007/100012), and all six drop gold (32-338 copper before PDv2's own loot
-- multiplier). Two of the eight are deliberately economy-neutral and are the
-- pack's fillers for exactly that reason: 3852 Shadowfang Bloodhowler and
-- 3860 Shadowfang Tainted One are both lootid 0, skinloot 0, 0 gold.
--
-- The boss is NOT loot-free, unlike the shipped 25352/29620: 3927 drops
-- 3748 Feline Mantle (blue, ilvl 26) at 60% and 6314 Wolfmaster Cape (blue,
-- ilvl 27) at 40%. Worthless at level 80, but they will drop on nearly every
-- boss kill. If that flood is unwanted the fix is data, not code: drop this
-- row's `weight`, or swap the boss.
--
-- ----------------------------------------------------------------------------
-- theme 0 = any look, same as all five shipped packs: a pack whose theme is
-- neither 0 nor the live V2.Theme is invisible to the loader and the dungeon
-- fills with the placeholder creature (PDv2PackMgr.cpp:96-121).
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when [level_min, level_max] intersects the
-- player's band [bandMin, bandMin+4]. These are native level 18-25 Shadowfang
-- Keep creatures, but every spawn is force-levelled to 80 by
-- OnBeforeCreatureSelectLevel (PDv2Scaling.cpp:215-230) regardless of what is
-- written here, so 80/80 is correct and the only band that selects this pack
-- is 76..80.
--
-- rank 1 on every trash member, where the shipped packs' trash is rank 0.
-- With every Rate.Creature.Elite.* key at 1 in the deployed worldserver.conf
-- that costs and buys nothing beyond the gold dragon on the nameplate - it is
-- a LOOK change, it is intentional (vanilla dungeon trash is elite), and the
-- operator should expect it rather than report it.
--
-- The CREATE TABLE IF NOT EXISTS blocks below are a DELIBERATE DEVIATION from
-- the mod_pdungeon_packs_undead_demon.sql precedent. The AC updater applies
-- files in FILENAME order (UpdateFetcher.cpp:64-96), and on a fresh database
-- mod_pdungeon_member_spells_worgen.sql sorts before mod_pdungeon_packs.sql
-- ('m' < 'p'), i.e. before the file that creates the tables. IF NOT EXISTS
-- makes the repetition free on this box and correct on a clean one.
--
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM, and
-- zero spell_dbc rows. Every entry is stock 3.3.5a, none of them falls in
-- 84263-84290, none is used by packs 1-5 (SELECT packId, entry FROM
-- pdungeon_pack_members WHERE entry IN (...) returns 0 rows), and no other
-- module's SQL references any of them (grep across modules/ hits only one
-- coincidental coordinate fragment in mod-turtle-content).
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
-- (PDv2PackMgr.cpp:157-166). Each of the three values below is identical to
-- that member's own slot-0 row in mod_pdungeon_member_spells_worgen.sql, the
-- way the shipped files keep them, and each reaches at least
-- ProceduralDungeon.V2.CastRangeYd (25): 61562 Shadow Bolt 40 yd, 60015
-- Shadow Bolt 40 yd, 69211 Shadow Bolt 30 yd (SpellRange.dbc, measured).
INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 6 "Shadowfang Pack" - 8 members: 4 melee, 3 range, 1 boss
  -- MELEE (role 0)
  (6, 3852, 0,     0, 100),  -- Shadowfang Bloodhowler  uc1, 16026 hp, display 202  scale 1.00, lootid 0 (economy-neutral filler)
  (6, 3854, 0,     0, 100),  -- Shadowfang Wolfguard    uc1, 16026 hp, display 203  scale 1.00, loot - armoured guard skin, the line-holder
  (6, 3857, 0,     0, 100),  -- Shadowfang Glutton      uc1, 16026 hp, display 202  scale 1.00, loot - the feeder
  (6, 3859, 0,     0, 100),  -- Shadowfang Ragetooth    uc1, 16026 hp, display 736  scale 1.15, loot - biggest of the plain worgen, carries the pack's single CC
  -- RANGE (role 1) - casterSpellId mirrors the member's own slot-0 filler
  (6, 3853, 1, 61562, 100),  -- Shadowfang Moonwalker   uc2, 12822 hp / 7988 mana, display 729 scale 1.00, loot - Arugal's shadow-caster, the primary nuker
  (6, 3855, 1, 60015, 100),  -- Shadowfang Darksoul     uc2, 12822 hp / 7988 mana, display 657 scale 0.85, loot - the runt, a skulking caster hanging back
  (6, 3860, 1, 69211, 100),  -- Shadowfang Tainted One  uc2, 12822 hp / 7988 mana, display 574 scale 1.30, lootid 0 - the mutated one, replaces the frost-immune 3851
  -- BOSS (role 2)
  (6, 3927, 2,     0, 100);  -- Wolf Master Nandos      uc1, 32052 hp, display 11179 scale 1.50, rank 1, no ScriptName - see the OPEN DECISION block above
