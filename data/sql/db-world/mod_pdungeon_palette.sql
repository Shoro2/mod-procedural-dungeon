-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: tile palette (world database)
-- Maps generator roles onto GameObject entries. Swap pieces or add themes
-- without rebuilding the server. All display ids below were verified against
-- the 3.3.5a client GameObjectDisplayInfo.dbc; walls and gates use WMO models
-- so they block creature line of sight (M2 is fine for decoration).
-- Roles: WALL, GATE, TORCH, BRAZIER, CHEST, SHRINE, EXIT, ENTRANCE
-- rot_offset: yaw added to the computed piece orientation (radians).
-- z_offset:   height added on top of the sampled ground height.
--
-- rot_offset CALIBRATION (walls + gate): the placement code already applies the
-- run-axis rotation (o=0 => runs east-west/+X; +HALF_PI for vertical runs and
-- for gates spanning across the opening). rot_offset's ONLY job is to normalize
-- a model so that at o=0 it visually runs along +X. It CANNOT be guessed from
-- the model AABB and must be measured in isolation - do NOT hard-code a value.
-- The 910002 wall's earlier rot_offset=pi/2 guess did NOT fix it in-world, so
-- these ship at the 0 baseline for calibration. Operator procedure:
--   1. Keep every WALL/GATE row at rot_offset = 0 (as below); reload the palette.
--   2. Stand on flat ground facing due East and run  .pdungeon validate
--      (each piece spawns at o = rot_offset = 0).
--   3. Piece runs East-West (+X) => leave rot_offset = 0. Runs North-South (+Y)
--      => set rot_offset = 1.5708 (use 4.7124 only if a distinct front face ends
--      up back-to-front). Write the value into that row and reload.
--   4. Re-run .pdungeon validate facing East; confirm every piece runs East-West.
-- Give 910002 and the GATE their own rows so each is independently tunable.
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_palette` (
  `id` INT UNSIGNED NOT NULL,
  `theme` VARCHAR(16) NOT NULL DEFAULT 'wg',
  `role` VARCHAR(16) NOT NULL,
  `go_entry` INT UNSIGNED NOT NULL,
  `len_tiles` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `rot_offset` FLOAT NOT NULL DEFAULT 0,
  `z_offset` FLOAT NOT NULL DEFAULT 0,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `comment` VARCHAR(64) DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

DELETE FROM `pdungeon_palette` WHERE `theme` = 'wg';
INSERT INTO `pdungeon_palette` (`id`, `theme`, `role`, `go_entry`, `len_tiles`, `rot_offset`, `z_offset`, `weight`, `comment`) VALUES
(1, 'wg', 'WALL', 910000, 3, 0, 0, 1, 'WG_Wall01.wmo 28.5yd - calibrate rot_offset'),
(3, 'wg', 'WALL', 910002, 2, 1.5708, 0, 1, 'nd_human_wall_small02.wmo 17.7yd rot90 (perp to Wall Long)'),
(4, 'wg', 'WALL', 910003, 1, 0, -1.84, 1, 'nd_human_wall_end_small02.wmo 10.4yd zoff-1.84'),
(5, 'wg', 'GATE', 910010, 1, 0, 0, 1, 'BlackRockIronDoor M2 - calibrate rot_offset'),
(6, 'wg', 'TORCH', 910020, 1, 0, 0, 1, 'ScarletO_Brazier_Lit'),
(7, 'wg', 'BRAZIER', 910021, 1, 0, 0, 1, 'Zuldrak brazier'),
(8, 'wg', 'CHEST', 910030, 1, 0, 0, 1, 'TreasureChest01'),
(9, 'wg', 'SHRINE', 910031, 1, 0, 0, 1, 'Nox portal yellow'),
(10, 'wg', 'EXIT', 910032, 1, 0, 0, 1, 'InstancePortal red'),
(11, 'wg', 'ENTRANCE', 910033, 1, 0, 0, 1, 'InstancePortal');
