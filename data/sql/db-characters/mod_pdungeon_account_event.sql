-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.gen_event_pct (characters database)
--
-- Round E / WP5 (2026-09-10): the chain generator takes one more generation
-- input, the chance per boss segment that the segment gets an EVENT POCKET -
-- the dead-end room the event host stands in (ProceduralDungeon.V2.Event.
-- ChancePct). A layout is stored as its seed plus the inputs it was generated
-- with and REGENERATED on login, so the input has to be stored beside
-- gen_branches: read live from the conf instead, it would reshape every stored
-- dungeon the day the operator tunes it.
--
-- No heal, and none is possible. PD_LAYOUT_VERSION goes to 5 in the same
-- commit, so every row that exists today is rejected at load and rerolled
-- (layout columns only - dlvl/dxp and the cfg_* knobs have their own writers),
-- and the default of 0 is never read into a live plan. 0 is also the only
-- honest value for those rows: they were generated before event pockets
-- existed, which is exactly what 0 % means.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_account_branches.sql states: the AC updater applies each
-- SQL file exactly once and remembers it by hash, so touching the base file
-- would leave every existing database without this column while claiming to be
-- up to date. New file = fresh databases create then alter, existing ones just
-- alter. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema like mod_pdungeon_account_difficulty.sql, which also makes
-- this file safe to re-apply by hand.
--
-- NO `AFTER` clause on purpose (2026-08-09 host crash-loop lesson, stated in
-- full in mod_pdungeon_runs_difficulty.sql's header): the updater applies a
-- module's NEW files in FILENAME order, and naming a column a later file has
-- not created yet aborts the very first host boot with error 1054 while every
-- workbench boot looks fine. `AFTER gen_branches` would in fact survive here -
-- '...account_branches' sorts before '...account_event' - but that guarantee is
-- an accident of two filenames and not a rule anyone should have to re-derive.
-- Column position is cosmetic; every reader binds by name.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'gen_event_pct');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `gen_event_pct` TINYINT UNSIGNED NOT NULL DEFAULT 0',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
