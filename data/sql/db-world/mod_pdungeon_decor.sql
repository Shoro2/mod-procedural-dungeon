-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: PDv2 server-side decoration (world database)
--
-- The kit draws floor, walls and void and nothing else - a corridor made of
-- terrain alone reads as a texture rather than a place. The props that fix
-- that are GameObjects, which means the SERVER owns them, and a server-side
-- prop has to be placed from the same data the client composed the terrain
-- from. That data is the walk mask; src/generator/PDv2DecorPlan.* turns it
-- into surface classes and picks the wall feet, and this table is what it
-- picks FROM.
--
-- Placement is deterministic: the spots follow the layout seed through a
-- separate PDRandom stream, so re-entering a dungeon finds the torches where
-- they were and a re-roll moves them exactly as it moves the walls.
--
-- roleFilter is a PREFIX match against `pdungeon_chunk_meta`.`role`:
--   'room'       matches room, room_entrance AND room_boss
--   'room_boss'  matches room_boss only
--   'corridor'   matches corridor_straight, corridor_corner, corridor_t,
--                corridor_cross AND corridor_dead_end (the stub block; it
--                used to report itself as corridor_cross, which made a
--                corridor_cross filter fire on stubs and left a
--                corridor_dead_end filter matching nothing at all)
--   ''           matches every block
--
-- placement is 'wall_foot' for every row in THIS file: a WALK cell that
-- touches a WALL cell on one of its four sides, the prop pushed 2.5 yd into
-- that cell towards the wall and turned away from it. The planner also
-- implements 'corner' and 'scatter' placement, used by
-- mod_pdungeon_decor_clutter.sql's rules. A rule naming any placement kind
-- the planner does not implement is skipped rather than guessed at.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_decor_rules` (
    `id` INT UNSIGNED NOT NULL,
    -- 0 = ANY theme, the same sentinel pdungeon_packs uses. A rule is
    -- scoped to one theme only if its GameObject would look wrong under
    -- another; a wall-foot brazier does not. Scoping these to theme 1
    -- left the city with no server decor at all once it became the
    -- default look.
    `theme` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `roleFilter` VARCHAR(32) NOT NULL DEFAULT '',
    `goEntry` INT UNSIGNED NOT NULL,
    `placement` VARCHAR(16) NOT NULL DEFAULT 'wall_foot',
    `minPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `maxPerBlock` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `weight` INT UNSIGNED NOT NULL DEFAULT 100,
    `minSpacingYd` FLOAT NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Idempotent re-apply: delete this file's own id range, then insert. Never
-- REPLACE and never DROP - an operator who added rules of their own keeps
-- them, and a REPLACE would silently resurrect columns this file does not
-- name.
DELETE FROM `pdungeon_decor_rules` WHERE `id` BETWEEN 1 AND 3;

-- weight is a rule's SHARE of a block's candidate wall feet when several
-- rules speak for the same block. In a boss room both rule 1 and rule 2 do,
-- and the brazier is deliberately the lighter of the two: the room keeps its
-- ring of torches and gains a brazier or two, not the other way round.
--
-- minSpacingYd of 8 is one cell (8.33 yd) rounded down, so two props of the
-- same rule never end up in cells that touch.
--
-- Rule 3 is LIVE against kit v23 (previously documented here as DORMANT -
-- false since kit v23 widened every corridor to two cells). The walkable
-- cells of corridor_straight are columns 3 AND 4, not column 4 alone; only
-- index 4 (the socket track itself, the one line every player has to walk)
-- is excluded, leaving column 3 as a real wall-foot candidate. The four
-- corridor_straight variants yield 30 wall-foot candidates between them, and
-- rule 3 measures out to fire on roughly half of all corridor blocks -
-- planting a torch in the lane beside the track, not on it.
INSERT INTO `pdungeon_decor_rules`
    (`id`, `theme`, `roleFilter`, `goEntry`, `placement`, `minPerBlock`, `maxPerBlock`, `weight`, `minSpacingYd`) VALUES
    (1, 0, 'room',      910020, 'wall_foot', 1, 3, 100, 8),
    (2, 0, 'room_boss', 910021, 'wall_foot', 1, 2,  40, 8),
    (3, 0, 'corridor',  910020, 'wall_foot', 0, 1, 100, 8);

-- ----------------------------------------------------------------------------
-- GameObject templates 910020 / 910021, re-declared verbatim from
-- mod_pdungeon_templates.sql.
--
-- OWNED BY V2 DECOR SINCE PHASE 1 - THE V1 CLEANUP MUST KEEP/SKIP THESE TWO.
-- v1 places them from `pdungeon_palette` (roles TORCH and BRAZIER) and v2
-- places them from the table above; when v1's templates are eventually
-- retired, its `DELETE FROM gameobject_template WHERE entry BETWEEN 910000
-- AND 910099` must not take 910020 and 910021 with it, or every PDv2 dungeon
-- goes dark.
--
-- The DELETE names each entry rather than a range for the same reason: this
-- file speaks for exactly two ids and must not be able to remove a third.
-- ----------------------------------------------------------------------------
DELETE FROM `gameobject_template` WHERE `entry` IN (910020, 910021);
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `size`, `Data0`, `Data1`, `ScriptName`) VALUES
(910020, 5, 7858, 'PD Torch', 1, 0, 0, ''),
(910021, 5, 8191, 'PD Brazier', 1, 0, 0, '');
