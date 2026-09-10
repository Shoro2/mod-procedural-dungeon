-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_runs.deaths (characters database)
--
-- Round E / R1 (2026-09-10, spec D15). The run history's half of the cap rule:
-- a completed run raises the account's difficulty cap by V2.Cap.CleanUnlock
-- when nobody died and only by V2.Cap.DeathUnlock when somebody did, so how
-- many player deaths a run cost is the one new fact FinishRun has to record
-- next to the difficulty it was played at.
--
-- TINYINT UNSIGNED, matching the uint8 the run state counts in: the counter
-- saturates at 255 rather than wrapping, and a run with 255 deaths and a run
-- with 300 are the same story - "somebody died" is what the unlock reads.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_runs.sql or to
-- mod_pdungeon_runs_gameplay.sql, for the reason that file's own header
-- states: the AC updater applies each SQL file exactly once and remembers it
-- by hash, so touching an already-applied file would leave every existing
-- database without this column while claiming to be up to date. MySQL 8 has no
-- ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on information_schema,
-- which also makes this file safe to re-apply by hand.
--
-- DEFAULT 0 rather than any other value: 0 is what a row written before this
-- column existed carries, and for this column that reads correctly - a run
-- that predates the counter recorded no deaths. It is also the honest starting
-- value for the INSERT that opens a run, which happens before anyone can die.
--
-- NO `AFTER` clause on purpose (2026-08-09 host crash-loop lesson, stated in
-- full in mod_pdungeon_runs_difficulty.sql's header): the updater applies a
-- module's NEW files in FILENAME order, so on a fresh deployment this file
-- ('...runs_deaths') runs before mod_pdungeon_runs_gameplay.sql ('...runs_
-- gameplay') and any column of that file named here would abort the very first
-- host boot with error 1054, while every workbench boot - where the files
-- arrive days apart - looks fine. Column position is cosmetic; every reader
-- binds by name.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_runs'
               AND COLUMN_NAME = 'deaths');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_runs` ADD COLUMN `deaths` TINYINT UNSIGNED NOT NULL DEFAULT 0',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
