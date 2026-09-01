-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: clutter placement rules (world database)
--
-- This file used to also ship the 22 clutter gameobject_template rows
-- (910050-910057/910060-910066/910070-910076) it references below. They
-- have moved to mod_pdungeon_templates_fix.sql: AzerothCore's updater
-- applies db-world SQL in filename order with each file gated on its own
-- content hash, no cascading re-apply of siblings, and this file sorts
-- BEFORE mod_pdungeon_templates.sql - whose `DELETE FROM gameobject_template
-- WHERE entry BETWEEN 910000 AND 910099` covers this whole id range. A
-- template INSERT here could never survive a future re-apply of that file;
-- only mod_pdungeon_templates_fix.sql sorts after it and can actually
-- restore anything. Full mechanism and the failure mode it prevents are
-- documented in that file's header - read it before adding a `goEntry` here
-- for an id that doesn't exist yet.
--
-- What stays here: `pdungeon_decor_rules`, a different table entirely, not
-- touched by mod_pdungeon_templates.sql's DELETE, so it is safe to own and
-- restore-in-place from this file regardless of sort order. Every `goEntry`
-- a rule below names must resolve to a gameobject_template row that lives
-- in mod_pdungeon_templates.sql (910000-910033) or
-- mod_pdungeon_templates_fix.sql (910040-910099) - never add a rule whose
-- goEntry template you shipped only in this file, or it becomes exactly the
-- kind of orphaned rule this split exists to avoid.
--
-- Rule ids start at 4 so the shipped mod_pdungeon_decor.sql's
-- `DELETE ... WHERE id BETWEEN 1 AND 3` can never touch them.
--
-- theme 0 = any look, the same sentinel the packs use. Scoping a rule to one
-- theme is what once left the city with no server decor at all.
-- ----------------------------------------------------------------------------

DELETE FROM `pdungeon_decor_rules` WHERE `id` BETWEEN 4 AND 14;
INSERT INTO `pdungeon_decor_rules`
    (`id`,`theme`,`roleFilter`,`goEntry`,`placement`,`minPerBlock`,`maxPerBlock`,`weight`,`minSpacingYd`) VALUES
-- Wall feet. Weight is a SHARE of the block's wall-foot cells, competing with
-- the shipped torch (rule 1, weight 100) and brazier (rule 2, weight 40) - so
-- these are deliberately light: the room keeps its ring of torches and gains
-- furniture, not the other way round.
( 4, 0, 'room',              910050, 'wall_foot', 0, 2, 30, 8),
( 5, 0, 'room',              910051, 'wall_foot', 0, 2, 30, 8),
( 6, 0, 'room',              910054, 'wall_foot', 0, 1, 20, 8),
( 7, 0, 'room',              910055, 'wall_foot', 0, 1, 15, 8),
( 8, 0, 'room',              910056, 'wall_foot', 0, 1, 15, 8),
( 9, 0, 'corridor',          910052, 'wall_foot', 0, 1, 40, 8),
-- Corners. Two variants under one weight class is what stops a 15-room layout
-- from showing the same corner twelve times.
(10, 0, 'room',              910060, 'corner',    0, 2, 50, 8),
(11, 0, 'room',              910062, 'corner',    0, 2, 50, 8),
(12, 0, 'corridor_corner',   910063, 'corner',    0, 1, 60, 8),
-- Scatter. minSpacing 12 is one and a half cells, so scattered props never
-- clump; all three are flat enough to be walked over rather than walled by.
(13, 0, 'room',              910070, 'scatter',   0, 3, 60, 12),
(14, 0, 'room_boss',         910073, 'scatter',   1, 3, 80, 12);
