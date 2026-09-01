-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: PDv2 ambient critters (world database)
--
-- Ambient life. A critter is NOT a pack member and NOT dungeon content: it is
-- excluded from the AI binder, from the level/HP scaling, and from every run
-- counter. See PDv2CreatureAIBinder and PDv2Scaling::IsDungeonCreature.
--
-- Every entry is a STOCK creature_template - nothing here is edited or added,
-- which is the same rule the packs follow. All ten are unit_class 1 on a
-- neutral faction template, carry no loot and no skinning, and give no XP.
--
-- Startup-only, like every other PDv2 table: a change needs a worldserver
-- restart, never `.reload config`.
--
-- roleFilter is the same PREFIX match `pdungeon_decor_rules`.`roleFilter`
-- uses against `pdungeon_chunk_meta`.`role` - see mod_pdungeon_decor.sql for
-- the full match table. 'corridor' already matches corridor_dead_end (a
-- stub reports itself as a corridor role, prefixed the same as every other
-- corridor variant), so rule 5's own 'corridor_dead_end' row STACKS on top
-- of rule 4's 'corridor' row rather than replacing it - a dead-end stub
-- gets both budgets, which is deliberate: it is otherwise the barest block
-- in the kit.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_critter_rules` (
    `id` INT UNSIGNED NOT NULL,
    -- 0 = ANY theme, the same sentinel pdungeon_decor_rules and pdungeon_packs
    -- use. Rats and roaches read as generic vermin under every theme this
    -- module ships, so nothing here is theme-scoped yet.
    `theme` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `roleFilter` VARCHAR(32) NOT NULL DEFAULT '',
    `creatureEntry` INT UNSIGNED NOT NULL,
    `minPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `maxPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `weight` INT UNSIGNED NOT NULL DEFAULT 100,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own id range, then insert. Never
-- REPLACE and never DROP - an operator who added rules of their own keeps
-- them, and a REPLACE would silently resurrect columns this file does not
-- name. The range reserves ids 1-10 for the full researched set; only the
-- six below have been verified against the live world DB and the client
-- DBCs so far, so only six are inserted - the remaining ids stay free for a
-- later addition once verified the same way.
DELETE FROM `pdungeon_critter_rules` WHERE `id` BETWEEN 1 AND 10;

-- Every creatureEntry below was checked against creature_template /
-- creature_template_model on the live world DB and against
-- CreatureDisplayInfo.dbc before shipping: type 8 (CREATURE_TYPE_CRITTER),
-- unit_class 1, a neutral faction template (31, 188 or 190 - ourMask,
-- friendlyMask and hostileMask all 0 in FactionTemplate.dbc), unit_flags
-- free of NOT_SELECTABLE and IMMUNE_TO_PC, lootid 0, skinloot 0, and
-- VerifiedBuild 12340.
--
-- 23086 (Sewer Rat) carries TWO creature_template_model rows (1141 and
-- 1418); both resolve in CreatureDisplayInfo.dbc and draw the same model, so
-- the duplicate is cosmetically inert and not a data error.
--
-- 2110 (Black Rat) carries flags_extra 2 (CIVILIAN). That is an extra layer
-- of safety on top of what the row-count filters below already give it, and
-- on top of what Task 10's harmlessness gate will add - not a reason to
-- exclude it.
INSERT INTO `pdungeon_critter_rules`
    (`id`,`theme`,`roleFilter`,`creatureEntry`,`minPerBlock`,`maxPerBlock`,`weight`) VALUES
( 1, 0, 'room',            32428, 0, 2, 100),  -- Underbelly Rat
( 2, 0, 'room',            23086, 0, 2,  80),  -- Sewer Rat
( 3, 0, 'room',             2110, 0, 1,  60),  -- Black Rat
( 4, 0, 'corridor',        26525, 0, 2, 100),  -- Cockroach
( 5, 0, 'corridor_dead_end', 2110, 0, 1, 100), -- Black Rat, stub dressing
( 6, 0, 'room_boss',       26525, 0, 1,  60);  -- Cockroach
