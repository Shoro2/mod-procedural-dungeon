-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_runs.difficulty (characters database)
--
-- The run history's counterpart to mod_pdungeon_account_difficulty.sql: since
-- the 2026-08-08 operator directive a run is played at an INTEGER difficulty
-- 1..100 (the mod-dungeon-challenge model), so the history column that records
-- it has to be that integer too.
--
-- `difficulty_x100` is LEGACY-DEAD as of 2026-08-08, exactly like
-- `cfg_diff_x100` on the account table: nothing writes it any more, and it is
-- kept rather than dropped so the rows written under the old system stay
-- readable. It rides out with the next schema consolidation.
--
-- `loot_mult_x100` stays LIVE and stays x100 - the loot multiplier really is a
-- fixed-point quantity (1.00 .. 3.60) and FinishRun keeps writing it.
--
-- DEFAULT 0 rather than 1: 0 is the value a row written before this column
-- existed carries, so "0" reads as "this run predates the dial" instead of
-- claiming the run was played at the dial's floor.
--
-- Separate file + information_schema guard for the reasons
-- mod_pdungeon_runs_gameplay.sql states.
--
-- NO `AFTER` clause on purpose (2026-08-09 host crash-loop lesson): the AC
-- updater applies a module's NEW files in FILENAME order, and on a fresh
-- deployment this file ('d') runs before mod_pdungeon_runs_gameplay.sql ('g'),
-- which is what creates `difficulty_x100`. `AFTER difficulty_x100` therefore
-- aborted the very first host boot with error 1054 while every workbench boot
-- had been fine (there the files arrived one authoring day apart). Column
-- position is cosmetic; every reader binds by name. Incremental installs that
-- already ran the old text simply hit the guard's 'DO 0'.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'difficulty');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
