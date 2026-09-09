-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: the faceless / Old God creature pack (world database)
--
-- Pack 8 "Ahn'kahet Deep" - four type 6 nerubians out of Ahn'kahet and three
-- type 7 Twilight Hammer cultists, plus Elder Nadox as the boss. It is the
-- third pack built the way mod_pdungeon_packs_undead_demon.sql builds packs
-- 4 and 5: entirely from EXISTING creature_template entries, so no template,
-- no art and no client patch is added by this file.
--
-- Pack id 8 so this file's own delete can never touch the shipped
-- `DELETE ... WHERE id BETWEEN 1 AND 3` range of mod_pdungeon_packs.sql, nor
-- the `IN (4, 5)` of mod_pdungeon_packs_undead_demon.sql, nor the two sibling
-- packs (6, 7) authored alongside this one. The delete below is scoped to
-- packId 8 alone, in return.
--
-- theme 0 = any look, same as all seven other packs: a nerubian and a robed
-- cultist read fine under every look PDv2 currently ships, and a pack whose
-- theme is neither 0 nor the live V2.Theme is invisible to the loader and the
-- dungeon fills with the placeholder creature (PDv2PackMgr.cpp:96-121).
--
-- level_min/level_max 80/80 is the BAND SELECTOR, not a description of the
-- creature: a pack enters the pool when its range intersects the player's
-- chosen band [bandMin, bandMin+4], and every entry here is force-levelled to
-- 80 by OnBeforeCreatureSelectLevel on spawn regardless of what is written
-- here (PDv2Scaling.cpp:215-230). All eight are native Northrend stock, so -
-- same as the other seven packs - the only band that selects this one is
-- 76..80.
--
-- ----------------------------------------------------------------------------
-- THIS PACK IS TANKIER THAN THE OTHER SEVEN, AND THAT IS MEASURED, NOT GUESSED
--
-- Every member is exp 2, so its level-80 health multiplies
-- creature_classlevelstats.basehp2 (12600 for unit_class 1 and 2 alike), not
-- the basehp0 the Shadowfang / Scholomance eras multiply. Measured against
-- the running acore_world on 2026-09-09:
--
--   entry  name                      uc  exp  HealthMod  hp80    mana80
--   30176  Ahn'kahar Guardian         1   2      2        25200       0
--   30277  Ahn'kahar Slasher          1   2      4        50400       0
--   31104  Ahn'kahar Watcher          1   2      4        50400       0
--   30111  Twilight Worshipper        2   2      4        50400   27958
--   30278  Ahn'kahar Spell Flinger    2   2      4        50400   19970
--   30179  Twilight Apostle           2   2      4        50400   31952
--   30319  Twilight Darkcaster        2   2      4        50400   15976
--   29309  Elder Nadox  (BOSS)        2   2     17       214200   79880
--
-- 50400 hp of trash against the 12600-32052 the other packs field. Expect a
-- pack-8 room to take roughly twice as long at the same difficulty. If that
-- is unwanted the levers are the difficulty dial and `weight` below - NEVER
-- creature_template.HealthModifier, which is a shared column other content
-- reads.
--
-- The boss lands at 214200, between the shipped 149560 (84288-84290) and
-- 327600 (29620 Dreadlord Mal'Ganis), so it needs no special handling: there
-- is no boss-specific health normalisation anywhere in the module, a boss's
-- health is exactly its template's scaled by difficulty (PDv2Scaling.cpp:
-- 232-263).
--
-- ----------------------------------------------------------------------------
-- WHY ELDER NADOX AND NOT HERALD VOLAZJ
--
-- 29311 Herald Volazj is the obvious faceless boss and is UNUSABLE:
-- creature_template.flags_extra = 0x80000000 CREATURE_FLAG_EXTRA_HARD_RESET.
-- CreatureAI::EnterEvadeMode calls me->DespawnOnEvade() for that flag
-- (CreatureAI.cpp:271-276); PDv2's own override still calls the base
-- (PDv2CreatureAI.cpp:293); PDv2 spawns via SummonCreature, so DespawnOnEvade
-- UnSummon()s a TempSummon whose GUID no scheduled respawn can bring back
-- (Creature.cpp:2201-2219); and _roomAlive decrements ONLY in OnMobDied
-- (PDv2InstanceScript.cpp:707-709). One evade - a wipe, a run-away, a
-- knockback - therefore leaves the boss room permanently uncleared.
--
-- 29309 Elder Nadox has flags_extra 0 and takes the slot. His
-- boss_elder_nadox ScriptName never binds, and neither does 30176's
-- npc_ahnkahar_nerubian: CreatureAISelector::SelectAI asks
-- sScriptMgr->GetCreatureAI BEFORE it looks at AIName
-- (CreatureAISelector.cpp:78-88) and ScriptMgr::GetCreatureAI asks every
-- AllCreatureScript before the ScriptName registry
-- (ScriptDefines/CreatureScript.cpp:156-172). PDv2's binder is an
-- AllCreatureScript, so on map 760 it wins over both a template ScriptName
-- and AIName = 'SmartAI'.
--
-- If a true FACELESS silhouette is ever worth more than 113000 hp, 30414
-- Forgotten One (Creature\FacelessOne\FacelessOne.mdx at scale 1.5, 100800
-- hp, flags_extra 0) is the drop-in alternative and is the only other real
-- faceless one in stock. Swapping the boss is a one-line change to this file.
--
-- ----------------------------------------------------------------------------
-- WHAT EACH MEMBER LOOKS LIKE (creature_template_model -> CreatureDisplayInfo
-- -> CreatureModelData, read 2026-09-09 - never inferred from the name)
--
--   30176  28079  Creature\NerubianWarrior\NerubianWarrior.mdx  scale 1.25
--   30277  27324  Creature\NerubianWarrior\NerubianWarrior.mdx  scale 1.10
--   31104  27324  Creature\NerubianWarrior\NerubianWarrior.mdx  scale 1.10
--   30278  23821  Creature\NerubianCaster\NerubianCaster.mdx    scale 0.90
--   29309  27407  Creature\NerubianPriest\NerubianPriest.mdx    scale 4.00
--   30111  27386/27387/27388/27389  Tauren M/F + Troll M/F      scale 1.10
--   30179  27369/27370/27371/27372  Orc M/M/F/F                 scale 1.10
--   30319  27373/27374/27376/27377  Scourge M/M/F/F             scale 1.10
--
-- The three cultists each carry FOUR display variants, so a room of them is
-- four different bodies rather than one repeated model. Not one display here
-- is 11686 Creature\InvisibleStalker\InvisibleStalker.mdx - the trap that
-- cost 30621-30625 Twisted Visage their slots, since they are Volazj's
-- Insanity phantoms and would have spawned as invisible mobs.
--
-- ----------------------------------------------------------------------------
-- FLAG AND AURA TRAPS, ALL CHECKED, ALL CLEAR
--
-- Every one of these fails SILENTLY in game, so each was measured:
--
--   unit_flags       32832 (0x8040) on seven, 32768 (0x8000) on 30176.
--                    Neither carries 0x2000000 UNIT_FLAG_NOT_SELECTABLE (the
--                    trap that cost 30385 Twilight Volunteer its slot) nor
--                    0x300 IMMUNE_TO_PC/NPC.
--   flags_extra      0 on all eight (see the Volazj note above).
--   npcflag          0 on all eight - no questgiver marker, no vendor menu.
--   VehicleId        0 on all eight.
--   name suffix      no ' (1)'/' (2)'/' (3)' sniff-duplicate suffix.
--   faction          16 on all eight. FactionTemplate.dbc field 5:
--                    enemyGroupMask = 1 -> hostile to players. YES.
--   type             6 NERUBIAN-side undead and 7 HUMANOID - never 8/11/12/
--                    13/14 (critter/totem/pet).
--   unit_class       1 and 2 only - never 4 or 8, whose basehp1 is 1 at 80.
--   addon auras      TWO members spawn with a permanent aura, and BOTH were
--                    read out of Spell.dbc rather than trusted:
--                      31104 auras 18950 "Invisibility and Stealth Detection"
--                            = SPELL_AURA_MOD_INVISIBILITY_DETECT +
--                              SPELL_AURA_MOD_STEALTH_DETECT. DETECTION only.
--                      30179 auras 12550 "Lightning Shield"
--                            = SPELL_AURA_PROC_TRIGGER_DAMAGE, 2 damage.
--                    Neither is a SPELL_AURA_SCHOOL_IMMUNITY - which is what
--                    3851 Shadowfang Whitescalp's aura 7940 turned out to be,
--                    and why that entry is not in the sibling worgen pack.
--
-- ----------------------------------------------------------------------------
-- ROLES, AND THE ONE DELIBERATE DEMOTION
--
-- role: 0 melee, 1 range, 2 boss.
--
-- 30111 Twilight Worshipper is unit_class 2 with 27958 mana and would read as
-- a caster, but it is role 0 here on purpose - the same demotion the shipped
-- mod_pdungeon_packs.sql applies to 84272/84282 on the operator's own report
-- that a demoted caster "steht nach dem cast nur auf range und ist kein
-- meele". A robed zealot that closes to melee is the cult's rank and file;
-- its ranged flavour is not lost, it comes back as melee-cast cooldown spells
-- in mod_pdungeon_member_spells_faceless.sql.
--
-- casterSpellId is the role-1 member's filler and a FALLBACK only:
-- pdungeon_member_spells is the truth for every spell a mob casts. The column
-- is kept because PDv2CreatureAI::BuildKit still reads it when a role-1
-- member has no slot-0 row at all (PDv2CreatureAI.cpp:1388-1396), so a
-- half-applied SQL set degrades to "the old nuke" instead of to a mob that
-- stands and stares. It is also what keeps a role-1 member from being
-- silently demoted to melee at load (PDv2PackMgr.cpp:157-166). All three are
-- deliberately identical to that member's own slot-0 row:
--
--   entry  spell  name          range  cast    mana
--   30278  69211  Shadow Bolt   30 yd  2.2 s   0 flat / 0 %
--   30179  60015  Shadow Bolt   40 yd  3.0 s   0 flat / 0 %
--   30319  61562  Shadow Bolt   40 yd  1.5 s   0 flat / 0 %
--
-- Each reaches at least ProceduralDungeon.V2.CastRangeYd (25), which is the
-- whole point of the column: a filler that cannot reach is a mob that
-- silently never lands anything (PDv2CreatureAI.cpp:1537).
--
-- ----------------------------------------------------------------------------
-- THE ECONOMY DELTA, BEFORE IT IS DISCOVERED RATHER THAN AFTER
--
-- Seven of the eight members carry a native creature_loot_template table
-- (lootid = entry for 29309, 30111, 30179, 30277, 30278, 30319, 31104) and
-- three are skinnable (skinloot 70205 on 30277, 30278, 31104). Only 30176
-- Ahn'kahar Guardian is lootid 0, which is exactly why it is the pack's
-- economy-neutral filler. The Twilight cultists also drop real gold. The
-- design intends this - mod_pdungeon_packs.sql says packs are built from
-- existing creatures "so native creature_loot_template drops stay intact and
-- the dungeon becomes a target-farming mechanism" - but it is a
-- player-visible change from the shipped 84263-84290, every one of which is
-- lootid 0. If it is unwanted the fix is data, not code: raise 30176's
-- `weight` and drop the others', or set the boss's `weight` down.
--
-- ----------------------------------------------------------------------------
-- Not one creature_template row is edited or added by this file, and none of
-- the eight entries falls in 84263-84290 or is used by another pack -
-- `SELECT packId, entry FROM pdungeon_pack_members WHERE entry IN (...)`
-- returned 0 rows and `SELECT COUNT(*) FROM pdungeon_member_spells WHERE
-- entry IN (...)` returned 0 on 2026-09-09. The whole three-table design
-- exists so this file never has to touch a shared template.
--
-- The CREATE TABLE IF NOT EXISTS blocks below are a DELIBERATE deviation from
-- the mod_pdungeon_packs_undead_demon.sql precedent. The AC updater applies
-- every .sql under the tree in FILENAME order (UpdateFetcher.cpp:64-96:
-- "Because updates are ordered by their filenames, every name needs to be
-- unique"), and mod_pdungeon_member_spells_faceless.sql sorts BEFORE
-- mod_pdungeon_packs.sql - the only file that carries the table definitions
-- today. On this box the tables already exist and the blocks are free; on a
-- FRESH world database they are what keeps this pack from failing to apply.
--
-- THIS FILE SHIPS ZERO creature_template AND ZERO spell_dbc ROWS.
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
-- an operator who added packs of their own keeps them, and so do the seven
-- packs this file does not own.
DELETE FROM `pdungeon_pack_members` WHERE `packId` = 8;
DELETE FROM `pdungeon_packs` WHERE `id` = 8;

INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (8, 'Ahn''kahet Deep', 0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 8 "Ahn'kahet Deep" - 8 members, 3 range, all faction 16, all rank 1, all exp 2
  (8, 30176, 0,     0, 100),  -- Ahn'kahar Guardian       unit_class 1, 25200 hp, lootid 0
  (8, 30277, 0,     0, 100),  -- Ahn'kahar Slasher        unit_class 1, 50400 hp, loot, skinnable
  (8, 31104, 0,     0, 100),  -- Ahn'kahar Watcher        unit_class 1, 50400 hp, loot, skinnable
  (8, 30111, 0,     0, 100),  -- Twilight Worshipper      unit_class 2, 50400 hp, loot, demoted to melee
  (8, 30278, 1, 69211, 100),  -- Ahn'kahar Spell Flinger  unit_class 2 (RANGE), loot, skinnable
  (8, 30179, 1, 60015, 100),  -- Twilight Apostle         unit_class 2 (RANGE), loot, mana 31952
  (8, 30319, 1, 61562, 100),  -- Twilight Darkcaster      unit_class 2 (RANGE), loot, mana 15976
  (8, 29309, 2,     0, 100);  -- Elder Nadox              BOSS, rank 1, 214200 hp, flags_extra 0
