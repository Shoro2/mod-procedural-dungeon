-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: creature packs (world database), 01 §7 + §8
--
-- A pack is a named bag of EXISTING creature_template entries the PDv2 spawner
-- draws from. 01 §8: packs are built from creatures that already exist, so
-- native creature_loot_template drops stay intact and the dungeon becomes a
-- target-farming mechanism - no new templates, no new art, no client patch.
--
-- THIS FILE SHIPS ZERO creature_template ROWS AND ZERO UPDATES TO THEM.
-- The entries below are shared with fl-underground-dungeon's live map-741
-- experience; editing their stats, level or ScriptName here would silently
-- retune that dungeon. PDv2 does not need to: its AllCreatureScript binder
-- wins over the template ScriptName on its own map (CreatureAISelector.cpp:
-- 78-88), so fl-underground's GenericTrashAI never binds on map 760 and the
-- rows can be left exactly as that module's SQL left them.
--
-- PDv2PackMgr LEFT JOINs creature_template at load and drops (loudly) any
-- member whose entry is missing, so a foreign environment without the
-- fl-underground stock degrades to "fewer members" instead of spawning ids
-- that do not exist.
--
-- ----------------------------------------------------------------------------
-- ROLE ASSIGNMENT (role: 0 melee, 1 caster, 2 boss)
--
-- Roles are NOT taken from fl-underground's own melee/caster vectors
-- (UndergroundData.cpp:388-441): the name comments there disagree with the
-- world DB for 19 of 25 entries, and the DB is the truth. Every role below was
-- assigned from the measured entry -> ScriptName mapping (2026-08-07).
--
-- Four entries that module calls casters (84267, 84271, 84277, 84279) are
-- MELEE here on purpose: they are unit_class 1 (warrior), so at level 80 they
-- have no mana pool, and PDv2's caster AI casts real power-checked spells. A
-- uc1 "caster" would stand there doing nothing.
--
-- ----------------------------------------------------------------------------
-- casterSpellId - the ranged nuke the caster AI fires (measured, not guessed)
--
-- Each caster's spell is taken from ITS OWN fl-underground kit
-- (UndergroundData.cpp TrashKits(), resolved through the creature's REAL
-- ScriptName), and then checked against Spell.dbc + SpellRange.dbc, because a
-- kit slot that cannot reach the target is a caster that silently never lands
-- anything. The picked spell must (a) deal damage, (b) target a single enemy,
-- and (c) have a max range at or above ProceduralDungeon.V2.CastRangeYd (25).
--
--  entry  ScriptName                  spell  name                range  why
--  84263  npc_underground_occultist   47809  Shadow Bolt R13      30 yd  kit tier 0
--  84272  npc_voidbound_revenant      47857  Drain Life R9        30 yd  kit tier 0
--  84278  npc_hellpit_crawler         26476  Digestive Acid        any   kit tier 50 *
--  84280  npc_warped_bonefiend        48125  Shadow Word: Pain    30 yd  kit tier 50 *
--  84281  npc_ashen_wailer            62129  Wail of Souls       100 yd  kit tier 0
--  84282  npc_twisted_abomination     20791  Shadow Bolt          40 yd  FALLBACK **
--  84283  npc_gloomfang               47857  Drain Life R9        30 yd  kit tier 50 *
--  84285  npc_underground_spectrum    42842  Frostbolt R16        30 yd  kit tier 0
--
--  *  the kit's tier-0 damage slots are melee (5 yd Bite / Bone Slice / Shadow
--     Bite) or self-centred AoE (Acid Splash, Spirit Burst, Shadow Breath cone),
--     none of which can land from 25 yd, so the pick moves up the same kit to
--     its first ranged single-target damage slot.
--  ** npc_twisted_abomination's whole kit is Slam (10 yd), Cleave (5 yd),
--     Enrage (self buff) and Curse of Idiocy (no damage) - it has no ranged
--     damage at any tier, so it takes the generic fallback.
--
-- All ids are stock 3.3.5 Spell.dbc entries (fl-underground remapped its dead
-- FL 8006xx ids onto real spells - UndergroundData.h:27-106). No spell_dbc row
-- is added or touched by this file.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_packs` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  `theme` TINYINT UNSIGNED NOT NULL DEFAULT 1,
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

-- Idempotent re-apply: delete this file's own id range, then insert. Never
-- DROP - an operator who added packs of their own keeps them.
DELETE FROM `pdungeon_pack_members` WHERE `packId` BETWEEN 1 AND 3;
DELETE FROM `pdungeon_packs` WHERE `id` BETWEEN 1 AND 3;

-- level_min/level_max is the BAND SELECTOR, not a description of the creature:
-- a pack is in the pool when its range intersects the player's chosen band
-- [bandMin, bandMin+4]. All the stock here is native 80 (the three bosses are
-- 82, one elite step above the band they guard - deliberately not modelled in
-- v1), so all three packs sit on 80 and the only band that selects anything is
-- 76..80. GameClampBandMin's other 15 values become live with the first pack
-- that has sub-80 members.
INSERT INTO `pdungeon_packs`
  (`id`, `name`, `theme`, `level_min`, `level_max`, `unlock_dlvl`, `enabled`) VALUES
  (1, 'Crypt Horrors',     1, 80, 80, 0, 1),
  (2, 'Abyssal Broodpit',  1, 80, 80, 0, 1),
  (3, 'Lords of the Deep', 1, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 1 "Crypt Horrors" - 13 members, 5 casters
  (1, 84263, 1, 47809, 100),  -- Underground Occultist  (uc8)
  (1, 84264, 0,     0, 100),  -- Crypt Howler           (uc1)
  (1, 84265, 0,     0, 100),  -- Shadowbone Stalker     (uc8, melee by role split)
  (1, 84266, 0,     0, 100),  -- Dread Maggot           (uc8, melee by role split)
  (1, 84268, 0,     0, 100),  -- Tormented Soul         (uc8, melee by role split)
  (1, 84272, 1, 47857, 100),  -- Voidbound Revenant     (uc8)
  (1, 84274, 0,     0, 100),  -- Carrion Watcher        (uc8, melee by role split)
  (1, 84276, 0,     0, 100),  -- Soul-Leech Banshee     (uc8, melee by role split)
  (1, 84280, 1, 48125, 100),  -- Warped Bonefiend       (uc8)
  (1, 84281, 1, 62129, 100),  -- Ashen Wailer           (uc8)
  (1, 84284, 0,     0, 100),  -- Underground Ghoul      (uc1)
  (1, 84285, 1, 42842, 100),  -- Underground Spectrum   (uc8)
  (1, 84287, 0,     0, 100),  -- Awakened Bones         (uc1)
  -- Pack 2 "Abyssal Broodpit" - 12 members, 3 casters
  (2, 84267, 0,     0, 100),  -- Abyss Hound            (uc1 - demoted from caster)
  (2, 84269, 0,     0, 100),  -- Putrid Fleshbeast      (uc1)
  (2, 84270, 0,     0, 100),  -- Netherclaw Demon       (uc8, melee by role split)
  (2, 84271, 0,     0, 100),  -- Fleshbound Horror      (uc1 - demoted from caster)
  (2, 84273, 0,     0, 100),  -- Blightfang             (uc1)
  (2, 84275, 0,     0, 100),  -- Grotesque Brute        (uc1)
  (2, 84277, 0,     0, 100),  -- Rotting Hound          (uc1 - demoted from caster)
  (2, 84278, 1, 26476, 100),  -- Hellpit Crawler        (uc8)
  (2, 84279, 0,     0, 100),  -- Bloodspike Beast       (uc1 - demoted from caster)
  (2, 84282, 1, 20791, 100),  -- Twisted Abomination    (uc8, fallback nuke)
  (2, 84283, 1, 47857, 100),  -- Gloomfang              (uc8)
  (2, 84286, 0,     0, 100),  -- Disgusting Larva       (uc1)
  -- Pack 3 "Lords of the Deep" - the boss draw, one entry per boss room
  (3, 84288, 2,     0, 100),  -- Dralak      (boss_dralak)
  (3, 84289, 2,     0, 100),  -- Lord Maltrion (boss_vampir_lord)
  (3, 84290, 2,     0, 100);  -- Mor'Kar     (boss_crypt_lord)
