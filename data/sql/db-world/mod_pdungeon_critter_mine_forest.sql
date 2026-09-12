-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: MINE and FOREST ambient life (world database)
-- Round F / K5
--
-- The first `pdungeon_critter_rules` rows in this module that are not
-- `theme 0`. The operator's verdict of 2026-09-12, on a screenshot of the
-- forest, is the whole reason they exist: "Critter sollen auch passend zum
-- Theme sein" - a Sewer Rat (rule 2, theme 0) was running between the pines.
--
-- THE ENGINE RULE THESE ROWS ARE WRITTEN FOR, because it is not the additive
-- one the decor rules follow. `BuildCritterPlan` asks `SelectCritterRules`
-- (src/generator/PDv2DecorPlan.cpp) once per plan:
--
--   V2.Critters.ThemeExclusive = 1 (default) and the run's theme owns at
--     least one rule  -> ONLY that theme's rules. The theme-0 rows are out,
--     including the ones that would have fitted.
--   V2.Critters.ThemeExclusive = 1 and the theme owns none
--                       -> the theme-0 rules, i.e. exactly what this module
--                          did before K5. That is the CITY (theme 2) today:
--                          it has no rows here and its dungeons do not move.
--   V2.Critters.ThemeExclusive = 0
--                       -> the union, the pre-K5 shape.
--
-- So these ten rows do not ADD ambient life to the mine and the forest, they
-- REPLACE it there. Every role the theme-0 set covers has to be covered again
-- per theme or that role goes silent - which is why each theme below ships the
-- same five-rule shape as `mod_pdungeon_critters.sql`: three 'room' rules, one
-- 'corridor' rule and one 'room_boss' rule.
--
-- ----------------------------------------------------------------------------
-- WHY THIS FILE CARRIES A CREATE TABLE AND ITS DECOR SIBLINGS DO NOT
--
-- `UpdateFetcher::PathCompare` (AC src/server/database/Updater/
-- UpdateFetcher.cpp:521-524) is a plain byte compare of
-- `filename().string()`. Against the base file:
--
--   mod_pdungeon_critter_mine_forest.sql
--   mod_pdungeon_critters.sql
--                      ^ '_' is 0x5F, 's' is 0x73, and 0x5F < 0x73
--
-- so THIS file runs FIRST and `pdungeon_critter_rules` does not exist yet when
-- it does. `mod_pdungeon_decor_mine.sql` could drop its CREATE because '.'
-- (0x2E) sorts before '_' and `mod_pdungeon_decor.sql` therefore always runs
-- ahead of it; here the comparison points the other way. The block below is a
-- verbatim copy of the base file's, so whichever of the two runs first the
-- other one's is a no-op.
--
-- ----------------------------------------------------------------------------
-- HOW EVERY ENTRY WAS VERIFIED (read-only, live world DB, 2026-09-12)
--
-- The law is the base file's, applied to each row again rather than assumed:
-- `type` = 8 CREATURE_TYPE_CRITTER, `unit_class` 1, a neutral faction template
-- (31, 188 or 190 - ourMask, friendlyMask and hostileMask all 0 in
-- FactionTemplate.dbc), `unit_flags` 0 (so free of NOT_SELECTABLE and
-- IMMUNE_TO_PC), `npcflag` 0, `lootid` / `pickpocketloot` / `skinloot` 0,
-- `unit_flags2` 2048 like every shipped row, no `creature_onkill_reputation`
-- row, no `creature_template_addon` aura, no `difficulty_entry_*`, no
-- `gossip_menu_id`, and `VerifiedBuild` 12340. Every display id was checked to
-- have a `creature_model_info` row, which is the server-side check that
-- matters (a display without one spawns with no bounding radius and the core
-- complains at load).
--
--   entry  name            faction  flags_extra  displays        model_info
--    4075  Rat                  31            2  1141,1418,2176  all 3
--    2110  Black Rat            31            2  1141            yes
--   20725  Bat                 188            2  4732            yes
--    4076  Roach               188            2  2177            yes
--     721  Rabbit               31            2  328,4626        both
--    1412  Squirrel             31            2  134             yes
--     883  Deer                 31            2  347             yes
--    1420  Toad                188            2  901             yes
--
-- `flags_extra` 2 is CREATURE_FLAG_EXTRA_CIVILIAN, the same extra layer of
-- safety the base file records for 2110; 2110 itself is REUSED here because a
-- black rat is a mine rat as much as it is anything else, and an entry may
-- appear in rules of several themes - the rules are keyed by id, not by entry.
--
-- MOVEMENT, because three of the eight carry `MovementType` 1 (RANDOM) in
-- their template and this file spawns them as SUMMONS: `Creature::Create`
-- reads the template's movement type and immediately downgrades it -
-- `if (!m_wanderDistance && m_defaultMovementType == RANDOM_MOTION_TYPE)
-- m_defaultMovementType = IDLE_MOTION_TYPE;` (AC Creature.cpp:569-571) - and a
-- summon has no spawn row, hence no wander distance. So a PDv2 critter stands
-- where BuildCritterPlan put it whatever its template says, which is the
-- behaviour the shipped rows already have (2110 is a MovementType-1 row that
-- has been in the dungeon since Phase 4). None of the eight can fly either:
-- 721, 883, 1412, 2110, 4075 and 4076 carry `creature_template_movement` rows
-- with `Flight` = 0 (None) and `Ground` = 1 (Run), and 1420 and 20725 have no
-- such row at all, which the core reads as the default-constructed
-- `CreatureMovementData` - Flight None again (AC Creature.cpp:60-62).
--
-- WHAT IS DELIBERATELY NOT HERE
--
--   * CAVE SPIDERS. Every type-8 spider in the world DB fails the law on the
--     same column: 14881 Spider, 22306 Skittering Cavern Crawler and 32261
--     Crystal Spider all carry `unit_flags` 32768 (NOT_SELECTABLE) - they are
--     scenery props for scripted scenes, not critters a player can click, and
--     a creature the module cannot let a player interact with is not ambient
--     life. The mine takes the bat instead, which passes every column.
--   * BIRDS in the forest. 620/621/630/635 Chicken carry `npcflag` 2
--     (QUESTGIVER) and would wear a quest marker in a procedural dungeon;
--     28093 Sholazar Tickbird carries `unit_flags` 256 (IMMUNE_TO_PC); 17970
--     Stormcrow Shape is a druid-form dummy and 21247 Oronok's Chicken is
--     quest-named. 9600 Parrot passes the law but is a Stranglethorn jungle
--     bird beside Grizzly Hills pines. So no bird ships, rather than a bird
--     that reads wrong - the brief's own condition ("only if a walking
--     critter") is met by none of them for a second reason as well.
--
-- ----------------------------------------------------------------------------
-- DENSITY, by the replay the sibling headers use
--
-- Measured on this box 2026-09-12 by replaying `BuildCritterPlan` over 3 000
-- layouts per row at three room counts, with the harness's own mask loader and
-- the same seed ladder the decor batch walks (scratchpad copy of
-- tests/blockplan_harness.cpp; the shipped harness carries no measuring mode).
-- Counts are pre-truncation: `at-budget` is how many of the 3 000 reached
-- PD_CRITTER_MAX_SPOTS (100) and were cut.
--
--   rule set                     rooms 5        rooms 8        rooms 15 (cap)
--   shipped theme-0 (1-4,6)      6..38  21.71   13..49  30.39  30..70  50.40
--   mine set (11-15)             6..38  21.71   13..49  30.39  30..70  50.40
--   forest set (16-20)           6..38  21.71   13..49  30.39  30..70  50.40
--   union, ThemeExclusive = 0   20..59  38.66   32..77  53.59  60..100 87.85
--
-- The first three rows are IDENTICAL and that is by construction, not luck:
-- the three sets share their shape (rule count per role, min/max, weights) and
-- the kit's three themes share their geometry, so the draw makes the same
-- number of the same decisions and only the entry ids differ. THE EXPECTED
-- COUNT AT THE 15-ROOM CAP IS THEREFORE 30..70, MEAN 50.40, PER THEME - the
-- number the mine and the forest inherit unchanged from the dungeon they
-- replace, with 0 of 3 000 layouts hitting the budget.
--
-- The union row is the one to know about: at `ThemeExclusive = 0` a 15-room
-- forest run averages 87.85 critters and 315 of 3 000 (10.5 %) hit the 100
-- budget and are truncated. That is the operator's choice to make and it is
-- not a fault - but it is why the key defaults to 1, and it is what the budget
-- exists to catch.
--
-- ----------------------------------------------------------------------------
-- IDS
--
-- 11-20, after the highest id any shipped file owns. The base file's own
-- `DELETE ... WHERE id BETWEEN 1 AND 10` reserves 1-10 for its researched set
-- and can therefore never touch these; this file's DELETE names its own ten
-- ids in return and nothing else, so the two apply in any order and on their
-- own. Never a REPLACE and never a DROP - an operator who added rules of their
-- own keeps them.
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

DELETE FROM `pdungeon_critter_rules`
      WHERE `id` IN (11, 12, 13, 14, 15, 16, 17, 18, 19, 20);

-- The role shape mirrors mod_pdungeon_critters.sql rule for rule, because
-- under ThemeExclusive these rules ARE that file for their theme: 'room'
-- (which prefix-matches room, room_entrance AND room_boss), 'corridor' (every
-- corridor_* role, though a corridor_dead_end can never carry one - it has no
-- scatter candidate cell at all, see the base file's header) and the extra
-- 'room_boss' rule that gives a boss hall its denser ambient life.
INSERT INTO `pdungeon_critter_rules`
    (`id`,`theme`,`roleFilter`,`creatureEntry`,`minPerBlock`,`maxPerBlock`,`weight`) VALUES
-- theme 1 - THE MINE. Vermin that belong in a working dig: two plain rats and
-- the one bat in the world DB that a player can actually click.
(11, 1, 'room',            4075, 0, 2, 100),  -- Rat
(12, 1, 'room',            2110, 0, 2,  80),  -- Black Rat
(13, 1, 'room',           20725, 0, 1,  60),  -- Bat
(14, 1, 'corridor',        4076, 0, 2, 100),  -- Roach
(15, 1, 'room_boss',       4076, 0, 1,  60),  -- Roach
-- theme 3 - THE FOREST. Woodland life; the deer takes the lowest-weight room
-- slot because it is the only body of any size among them (bounding radius
-- 1.0 against the rabbit's 0.235).
(16, 3, 'room',             721, 0, 2, 100),  -- Rabbit
(17, 3, 'room',            1412, 0, 2,  80),  -- Squirrel
(18, 3, 'room',             883, 0, 1,  60),  -- Deer
(19, 3, 'corridor',        1420, 0, 2, 100),  -- Toad
(20, 3, 'room_boss',       1420, 0, 1,  60);  -- Toad
