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
-- ROLE ASSIGNMENT (role: 0 melee, 1 range, 2 boss)
--
-- The role table below is the operator's, verified in game on 2026-08-09, and
-- it replaces the earlier assignment that was derived from fl-underground's
-- own melee/caster vectors. Those vectors could never be the source anyway:
-- their entry -> name comments (UndergroundData.cpp:388-441) disagree with the
-- world DB for 19 of 25 entries, and the DB is the truth.
--
--   RANGE (5)   84263 Underground Occultist, 84276 Soul-Leech Banshee,
--               84281 Ashen Wailer, 84285 Underground Spectrum,
--               84287 Awakened Bones
--   MELEE (20)  every other trash entry in 84264..84286
--   BOSS  (3)   84288, 84289, 84290
--
-- Five entries that used to be role 1 here are MELEE now - 84272, 84278,
-- 84280, 84282, 84283 - on the operator's live report that a demoted caster
-- "steht nach dem cast nur auf range und ist kein meele". Their ranged nukes
-- are not lost: they come back as melee-cast cooldown spells in
-- mod_pdungeon_member_spells.sql, which is where the flavour now lives.
--
-- ----------------------------------------------------------------------------
-- casterSpellId - the RANGE mob's filler, and a fallback only
--
-- pdungeon_member_spells is the truth for every spell a mob casts, including
-- the filler (its slot-0 row). This column is kept because the AI still reads
-- it when a role-1 member has no slot-0 row at all, so a half-applied SQL set
-- degrades to "the old nuke" instead of to a mob that stands and stares. The
-- two are deliberately identical today:
--
--  entry  ScriptName                  spell  name              range  cast
--  84263  npc_underground_occultist   47809  Shadow Bolt R13   30 yd  3.0 s
--  84276  npc_soul_leech_banshee      47857  Drain Life R9     30 yd  5 s channel
--  84281  npc_ashen_wailer            62129  Wail of Souls    100 yd  instant
--  84285  npc_underground_spectrum    42842  Frostbolt R16     30 yd  3.0 s
--  84287  npc_awakened_bones          47809  Shadow Bolt R13   30 yd  3.0 s
--
-- Each one deals damage to a single enemy and reaches at least
-- ProceduralDungeon.V2.CastRangeYd (25), which is the whole point of the
-- column: a filler that cannot reach is a mob that silently never lands
-- anything. 84287 is unit_class 1 and its power cost is argued in full in
-- mod_pdungeon_member_spells.sql.
--
-- All ids are stock 3.3.5 Spell.dbc entries (fl-underground remapped its dead
-- FL 8006xx ids onto real spells - UndergroundData.h:27-106). No spell_dbc row
-- is added or touched by this file.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_packs` (
  `id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  -- 0 = ANY theme. The kit's themes are ART: a pack is tied to one only if
  -- its creatures would look wrong under another look. The three packs below
  -- are undead crypt trash, which reads as well in a dark Gilneas street as
  -- in a mine, so they are theme-agnostic. Scoping them to theme 1 meant the
  -- city spawned nothing but the placeholder creature the moment it became
  -- the default look.
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
  (1, 'Crypt Horrors',     0, 80, 80, 0, 1),
  (2, 'Abyssal Broodpit',  0, 80, 80, 0, 1),
  (3, 'Lords of the Deep', 0, 80, 80, 0, 1);

INSERT INTO `pdungeon_pack_members`
  (`packId`, `entry`, `role`, `casterSpellId`, `weight`) VALUES
  -- Pack 1 "Crypt Horrors" - 13 members, 5 range
  (1, 84263, 1, 47809, 100),  -- Underground Occultist  (uc8, RANGE)
  (1, 84264, 0,     0, 100),  -- Crypt Howler           (uc1)
  (1, 84265, 0,     0, 100),  -- Shadowbone Stalker     (uc8)
  (1, 84266, 0,     0, 100),  -- Dread Maggot           (uc8)
  (1, 84268, 0,     0, 100),  -- Tormented Soul         (uc8)
  (1, 84272, 0,     0, 100),  -- Voidbound Revenant     (uc8, demoted to melee)
  (1, 84274, 0,     0, 100),  -- Carrion Watcher        (uc8)
  (1, 84276, 1, 47857, 100),  -- Soul-Leech Banshee     (uc8, RANGE)
  (1, 84280, 0,     0, 100),  -- Warped Bonefiend       (uc8, demoted to melee)
  (1, 84281, 1, 62129, 100),  -- Ashen Wailer           (uc8, RANGE)
  (1, 84284, 0,     0, 100),  -- Underground Ghoul      (uc1)
  (1, 84285, 1, 42842, 100),  -- Underground Spectrum   (uc8, RANGE)
  (1, 84287, 1, 47809, 100),  -- Awakened Bones         (uc1, RANGE)
  -- Pack 2 "Abyssal Broodpit" - 12 members, all melee
  (2, 84267, 0,     0, 100),  -- Abyss Hound            (uc1)
  (2, 84269, 0,     0, 100),  -- Putrid Fleshbeast      (uc1)
  (2, 84270, 0,     0, 100),  -- Netherclaw Demon       (uc8)
  (2, 84271, 0,     0, 100),  -- Fleshbound Horror      (uc1)
  (2, 84273, 0,     0, 100),  -- Blightfang             (uc1)
  (2, 84275, 0,     0, 100),  -- Grotesque Brute        (uc1)
  (2, 84277, 0,     0, 100),  -- Rotting Hound          (uc1)
  (2, 84278, 0,     0, 100),  -- Hellpit Crawler        (uc8, demoted to melee)
  (2, 84279, 0,     0, 100),  -- Bloodspike Beast       (uc1)
  (2, 84282, 0,     0, 100),  -- Twisted Abomination    (uc8, demoted to melee)
  (2, 84283, 0,     0, 100),  -- Gloomfang              (uc8, demoted to melee)
  (2, 84286, 0,     0, 100),  -- Disgusting Larva       (uc1)
  -- Pack 3 "Lords of the Deep" - the boss draw, one entry per boss room
  (3, 84288, 2,     0, 100),  -- Dralak      (boss_dralak)
  (3, 84289, 2,     0, 100),  -- Lord Maltrion (boss_vampir_lord)
  (3, 84290, 2,     0, 100);  -- Mor'Kar     (boss_crypt_lord)
