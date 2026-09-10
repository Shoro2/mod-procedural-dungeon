-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.diff_cap (characters database)
--
-- Round E / R1 (2026-09-10, spec D15). The 1..100 difficulty dial that
-- mod_pdungeon_account_difficulty.sql opened is no longer freely choosable: an
-- account may only pick up to its own CAP, and the cap grows by finishing runs
-- (`cap = min(100, max(cap, runDifficulty + (deaths ? 3 : 5)))`, the unlock
-- steps being V2.Cap.DeathUnlock / V2.Cap.CleanUnlock). The cap is per ACCOUNT
-- and not per run, so it belongs on this table and nowhere else - the run
-- history records the difficulty a run was PLAYED at, which the cap only
-- bounds at the moment the player chooses it.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_account_difficulty.sql states: the AC updater applies
-- each SQL file exactly once and remembers it by hash, so touching the base
-- file would leave every existing database without this column while claiming
-- to be up to date. New file = fresh databases create then alter, existing
-- ones just alter. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is
-- guarded on information_schema, which also makes this file safe to re-apply
-- by hand.
--
-- DEFAULT 1 - the floor of the dial, PD_GAME_DIFF_MIN - and no heal statement
-- to move it: spec D15 says EVERY existing account starts capped at 1, the
-- operator's own included, and earns its way up from there. The column default
-- alone therefore says the whole rule, both for the accounts that already have
-- a row and for every row a later INSERT creates. (The GM test path is
-- `.pdungeon v2 cap <n>`, not a migration.)
--
-- NO `AFTER` clause on purpose (2026-08-09 host crash-loop lesson, stated in
-- full in mod_pdungeon_runs_difficulty.sql's header): the updater applies a
-- module's NEW files in FILENAME order, so on a fresh deployment this file
-- ('a...diffcap') runs before files that create the columns one would want to
-- sit behind, and naming a column that does not exist yet aborts the very
-- first host boot with error 1054 while every workbench boot looks fine.
-- Column position is cosmetic; every reader binds by name.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'diff_cap');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `diff_cap` TINYINT UNSIGNED NOT NULL DEFAULT 1',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
