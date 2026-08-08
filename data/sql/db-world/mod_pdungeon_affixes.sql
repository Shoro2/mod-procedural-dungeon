-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: affix assignment metadata (world database)
--
-- WHAT THIS TABLE IS, AND WHAT IT IS NOT.
--
-- The affix SPELLS are mod-dungeon-challenge's. They live in Spell.dbc and the
-- server's spell_dbc rows (ids 900050-900060, hand-built in the spell editor),
-- they are applied to that module's dungeons by that module, and NOTHING here
-- creates, edits or deletes one. This table is only the ASSIGNMENT metadata
-- PDv2 needs - which spell, from which difficulty - so the dungeon can be
-- retuned with a SQL statement instead of a rebuild, and without reaching into
-- a module PDv2 does not own.
--
-- Both dungeons therefore share the same visual language on purpose: a Speedy
-- mob looks like a Speedy mob wherever a player meets it.
--
-- ----------------------------------------------------------------------------
-- THE EXTRACTION (from mod-dungeon-challenge C++, 2026-08-08, read-only)
--
-- Source of the gates:   DungeonChallengeMgr::LoadAffixData()   (DungeonChallenge.cpp)
-- Source of the spells:  DungeonChallengeMgr::ApplyAffixToCreature()'s switch
-- Source of the ids:     enum DungeonChallengeAffix              (DungeonChallenge.h)
--
-- The spell per affix is taken from the SWITCH, not from the constant names or
-- the comments beside them, because the names are only labels - the switch is
-- what actually casts.
--
--   id  affix            spellId  minDiff  DC's own C++ side beyond the aura
--    1  Call for Help     900060       10  pulls allies within 30 y on aggro
--    2  Speedy            900050       20  none - the DBC aura is the whole affix
--    3  Big Boy           900056       30  SetMaxHealth x1.5 (aura carries size)
--    4  Immolation Aura   900051       40  2 s tick, difficulty x 80 fire in 8 y
--    5  CC Immunity       900052       50  none - the DBC aura is the whole affix
--    6  Heavy Hits        900058       60  none - the DBC aura is the whole affix
--    7  Lil' Bro          900059       70  splits 1->2->4 on death, -90 % HP each
--    8  Damage Reduce     900055       80  -25 % damage taken by allies in 30 y
--    9  Bigger Boy        900057       90  SetMaxHealth x1.5 (aura carries size+dmg)
--   10  Hell Touched      900054      100  666 hellfire on hit + player debuff
--
-- PDv2 CASTS THE AURA AND NOTHING ELSE, which is exactly what the last column
-- makes visible: affixes 2, 5 and 6 (and the damage half of 9) arrive whole,
-- because their DBC aura IS the effect; 1, 3, 4, 7, 8, 10 arrive as the visual
-- plus whatever the aura itself does, because their teeth live in that module's
-- script hooks and copying those hooks would be a second implementation of
-- somebody else's feature. Extending them is a later slice with its own test.
--
-- 900053 is deliberately ABSENT: it is the Hell Touched DEBUFF, cast on the
-- PLAYER by that module's damage hook, not an aura a creature ever wears.
--
-- The gates are difficulty 10, 20 ... 100 - PDv2's dial is the same 1..100, so
-- they carry over unchanged. Below difficulty 10 a run has no affixes at all,
-- which is the gentle floor the 2026-08-08 directive asked for.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_affixes` (
  `id` TINYINT UNSIGNED NOT NULL,
  `spellId` INT UNSIGNED NOT NULL,
  `minDiff` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply the way mod_pdungeon_packs.sql does it: delete this
-- file's own id range, then insert. Never DROP - an operator who added an
-- affix of their own above id 10 keeps it.
DELETE FROM `pdungeon_affixes` WHERE `id` BETWEEN 1 AND 10;

INSERT INTO `pdungeon_affixes` (`id`, `spellId`, `minDiff`, `enabled`) VALUES
  ( 1, 900060,  10, 1),   -- Call for Help
  ( 2, 900050,  20, 1),   -- Speedy
  ( 3, 900056,  30, 1),   -- Big Boy
  ( 4, 900051,  40, 1),   -- Immolation Aura
  ( 5, 900052,  50, 1),   -- CC Immunity
  ( 6, 900058,  60, 1),   -- Heavy Hits
  ( 7, 900059,  70, 1),   -- Lil' Bro
  ( 8, 900055,  80, 1),   -- Damage Reduce
  ( 9, 900057,  90, 1),   -- Bigger Boy
  (10, 900054, 100, 1);   -- Hell Touched
