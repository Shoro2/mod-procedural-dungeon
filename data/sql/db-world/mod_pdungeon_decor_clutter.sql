-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: clutter GameObjects and their placement rules
-- (world database)
--
-- Broken furniture, crates, rubble and bones for PDv2. Data only - no C++ in
-- this round.
--
-- Every entry is type 5 GENERIC: it is the only GameObject class measured to
-- block a player on map 760 (MDDF/MODF doodads do not), which is why decor is
-- GameObjects at all (see mod_pdungeon_decor.sql). `size` is the only scale
-- dial available.
--
-- Every displayId below was checked three ways before being shipped: against
-- `GameObjectDisplayInfo.dbc` (the id resolves to the model path the name/
-- comment claims), against `GameObjectModels.dtree` (proof of an actual
-- collision model and its bounding box - a displayId can exist in the DBC
-- and still have nothing to stand on for LoS/pathing), and against stock
-- `gameobject_template` rows (proof Blizzard already used it as indoor
-- dressing, not e.g. a spell-effect-only model).
--
-- 910066 (display 6926, 'PD Rubble Heap') is the one exception and the
-- weakest-verified id in this file: Blizzard used it only as ADT/WMO terrain
-- dressing, so it has no stock gameobject_template row to check against. It
-- passed the other two checks (DBC model KarazahnRockRubble01.m2; a real
-- bounding box in GameObjectModels.dtree), which is the best confirmation
-- available for it.
--
-- Six of these displayIds (130, 293, 7470 among them) are also used by stock
-- quest objects elsewhere in the world DB. That is visual confusion only,
-- never a functional clash: ours are type 5 GENERIC, not the quest GO's
-- type/ScriptName, so ours are never clickable and never satisfy anyone's
-- objective.
--
-- This file owns gameobject_template 910050-910077, a sub-range of the
-- 910000-910099 block mod_pdungeon_templates.sql claims for itself with one
-- range DELETE too many (see mod_pdungeon_templates_fix.sql for the full
-- story). The DELETE below is a range too, but - unlike that one - it is
-- scoped to exactly the ids this file inserts and nothing else in the shared
-- block, so a re-apply of this file can never take a neighbor's row with it.
--
-- Rule ids start at 4 so the shipped mod_pdungeon_decor.sql's
-- `DELETE ... WHERE id BETWEEN 1 AND 3` can never touch them.
--
-- theme 0 = any look, the same sentinel the packs use. Scoping a rule to one
-- theme is what once left the city with no server decor at all.
-- ----------------------------------------------------------------------------

DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 910050 AND 910077;
INSERT INTO `gameobject_template` (`entry`,`type`,`displayId`,`name`,`size`,`Data0`,`Data1`,`ScriptName`) VALUES
-- wall feet: things that stand against a wall
(910050, 5,  288, 'PD Barrel',              1.0,  0, 0, ''),
(910051, 5,  275, 'PD Crate',               1.0,  0, 0, ''),
(910052, 5, 7470, 'PD Plague Barrel',       1.0,  0, 0, ''),
(910053, 5,  130, 'PD Weapon Rack',         1.0,  0, 0, ''),
(910054, 5,  187, 'PD Bookshelf',           1.0,  0, 0, ''),
(910055, 5, 4391, 'PD Alchemy Bench',       0.9,  0, 0, ''),
(910056, 5,  234, 'PD Long Table',          0.8,  0, 0, ''),
(910057, 5, 5511, 'PD Grain Sack',          1.5,  0, 0, ''),
-- corners: things that wedge into an angle
(910060, 5, 1868, 'PD Crate Stack',         0.8,  0, 0, ''),
(910061, 5, 1869, 'PD Crate Stack Alt',     0.8,  0, 0, ''),
(910062, 5, 7680, 'PD Broken Crate Stack',  0.75, 0, 0, ''),
(910063, 5, 6036, 'PD Tall Barrel',         1.2,  0, 0, ''),
(910064, 5, 7526, 'PD Broken Keg',          0.6,  0, 0, ''),
(910065, 5, 8480, 'PD Cart Wheel',          1.0,  0, 0, ''),
-- 6926 has no stock gameobject_template precedent - see the header note.
(910066, 5, 6926, 'PD Rubble Heap',         1.0,  0, 0, ''),
-- scatter: flat things you walk over on open floor
(910070, 5, 7911, 'PD Rubble Low',          1.0,  0, 0, ''),
(910071, 5, 6736, 'PD Broken Boards',       0.7,  0, 0, ''),
-- Display 9 (BrokenBarrel02), not 8026 (BrokenBarrel01): 8026 has no entry in
-- GameObjectModels.dtree at all, i.e. no collision model, so it would let
-- players walk through it. Do not "fix" this to 8026 - it was checked.
(910072, 5,    9, 'PD Broken Barrel',       1.0,  0, 0, ''),
(910073, 5, 7311, 'PD Skeleton',            1.0,  0, 0, ''),
(910074, 5, 7312, 'PD Skeleton Alt',        1.0,  0, 0, ''),
(910075, 5,  293, 'PD Bone Pile',           1.0,  0, 0, ''),
(910076, 5, 7225, 'PD Coffin',              1.0,  0, 0, '');

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
