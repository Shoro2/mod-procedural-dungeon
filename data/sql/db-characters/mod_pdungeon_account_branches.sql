-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.gen_branches (characters database)
--
-- Round B (2026-09-02): the chain generator takes one more generation input,
-- the number of pocket rooms (ProceduralDungeon.V2.Branches). A layout is
-- stored as its seed plus the inputs it was generated with and regenerated on
-- login, so the input has to be stored beside gen_loop_pct - read live from
-- the conf it would reshape every stored dungeon the day the operator tunes
-- it and trip the "PD_LAYOUT_VERSION should have been bumped" error path.
--
-- No heal: every row that exists today is stamped layout_version 2 and is
-- rejected at load anyway (the reroll the version bump asks for), so the
-- default of 0 is never read into a live plan.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_runs_gameplay.sql states: the updater applies each SQL
-- file exactly once and remembers it by hash, so touching the base file would
-- leave every existing database without this column while claiming to be up
-- to date. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema like mod_pdungeon_account_difficulty.sql, which also
-- makes this file safe to re-apply by hand.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'gen_branches');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `gen_branches` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `gen_loop_pct`',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
