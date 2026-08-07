-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_runs gameplay columns (characters database)
-- 01 §7 "pdungeon_runs (kept, extended)".
--
-- A SEPARATE file rather than an edit to mod_pdungeon_runs.sql on purpose: the
-- AzerothCore updater applies each SQL file exactly once and remembers it by
-- hash, so touching the base file would leave every existing database without
-- these columns while claiming to be up to date. New file = fresh databases
-- create then alter, existing ones just alter.
--
-- difficulty and lootMult are stored x100 for the reason 01 §7 gives for
-- cfg_diff_x100: the 0.25 grid must not drift through float storage, and a run
-- record that disagrees with the loot it handed out is unauditable.
--
-- account_id: runs are per ACCOUNT in v2 (01 §8 - the dungeon is per account and
-- solo-only in v1). leader_guid stays, because it is the only thing that says
-- WHICH character of that account played the run.
--
-- MySQL 8 has no ADD COLUMN IF NOT EXISTS, so each column is guarded on
-- information_schema the same way mod_pdungeon_palette.sql guards its own
-- migration. That also makes this file safe to re-apply by hand.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'dlvl');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `dlvl` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `leader_guid`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'difficulty_x100');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `difficulty_x100` SMALLINT UNSIGNED NOT NULL DEFAULT 100 AFTER `dlvl`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'loot_mult_x100');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `loot_mult_x100` SMALLINT UNSIGNED NOT NULL DEFAULT 100 AFTER `difficulty_x100`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'rooms_cleared');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `rooms_cleared` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `loot_mult_x100`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'account_id');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `account_id` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `leader_guid`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- Run history is queried per account for the future leaderboards 01 §7 names,
-- and account_id has no index from the ADD above.
SET @idx := (SELECT COUNT(*) FROM information_schema.STATISTICS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND INDEX_NAME = 'idx_account');
SET @sql := IF(@idx = 0,
    'ALTER TABLE `pdungeon_runs` ADD KEY `idx_account` (`account_id`)',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
