-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.cfg_difficulty (characters database)
--
-- The 2026-08-08 operator directive replaces 01 §8's difficulty band (0.5..3.0
-- on a 0.25 grid, its ceiling gated by dlvl) with mod-dungeon-challenge's model:
-- an INTEGER 1..100, freely choosable from the very first run. dlvl keeps
-- gating rooms and pack unlocks; it no longer gates the dial.
--
-- `cfg_diff_x100` is LEGACY-DEAD as of 2026-08-08. No code reads or writes it
-- any more. It is deliberately NOT dropped: a column drop is churn, it would
-- have to be replayed on every environment, and it destroys the only record of
-- what a player had chosen under the old system. It rides out with the next
-- schema consolidation.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_runs_gameplay.sql states: the updater applies each SQL
-- file exactly once and remembers it by hash, so touching the base file would
-- leave every existing database without this column while claiming to be up to
-- date.
--
-- MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema the same way mod_pdungeon_runs_gameplay.sql guards its
-- own. That also makes this file safe to re-apply by hand.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'cfg_difficulty');
SET @created := IF(@col = 0, 1, 0);
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `cfg_difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER `cfg_diff_x100`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- The heal, and ONLY on the apply that created the column. Every value the old
-- system stored (50..300 on the 0.25 grid) is meaningless on the new dial -
-- there is no honest mapping from "d = 1.25" to an integer 1..100 - so every
-- account starts the new system at the default of 1 and picks again.
--
-- Guarded on @created rather than run unconditionally BECAUSE it is a reset: a
-- second apply of this file must never walk over a difficulty a player has
-- since chosen. (mod_pdungeon_account_bandheal.sql can be idempotent through
-- its WHERE clause; a reset to a fixed value cannot.)
SET @sql := IF(@created = 1,
    'UPDATE `pdungeon_account` SET `cfg_difficulty` = 1',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
