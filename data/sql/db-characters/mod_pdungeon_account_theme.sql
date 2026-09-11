-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.cfg_theme (characters database)
--
-- Round F / F1 (2026-09-11, spec D1). Which LOOK this account's next dungeon is
-- generated with: 0 = follow the server's ProceduralDungeon.V2.Theme, 1 = mine,
-- 2 = city, and whatever a later kit adds. The kit has shipped both themes since
-- t1b-v39 and `.pdungeon v2 gen <seed> <theme>` could already pick one, but only
-- for a GM - this column is what puts the choice on the gen panel.
--
-- NOT the same column as `theme` on this table, and the difference is the whole
-- point: `theme` is the look the account's CURRENT stored layout was generated
-- with (SavePlanToDB writes it, and a stored dungeon regenerates with it for
-- ever), while `cfg_theme` is the look the NEXT Generate will use. Changing the
-- knob therefore never re-skins a dungeon somebody is standing in.
--
-- Per ACCOUNT, like every other cfg_* knob on this table: a layout is per
-- account, so the choice that shapes it has to be too.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_account_difficulty.sql states: the AC updater applies each
-- SQL file exactly once and remembers it by hash, so touching the base file
-- would leave every existing database without this column while claiming to be
-- up to date. New file = fresh databases create then alter, existing ones just
-- alter. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema, which also makes this file safe to re-apply by hand.
--
-- DEFAULT 0 - "follow the conf" - and no heal statement to move it: an account
-- that has never touched the panel row must keep generating exactly what the
-- operator configured, which is what every account did before this file
-- existed. The column default alone says that, both for the rows that already
-- exist and for every row a later INSERT creates.
--
-- The legal values are NOT fixed here and deliberately not constrained: what
-- exists is whatever `pdungeon_chunk_meta` carries after the kit's own SQL ran
-- (PDv2Mgr::ThemeMax/HasTheme), so a kit that ships a third theme needs no
-- migration and a database that lost its chunk meta must not be able to hand a
-- theme nobody has art for to the generator. The clamp lives where that
-- knowledge lives - in the loader, on the way in.
--
-- NO `AFTER` clause on purpose (2026-08-09 host crash-loop lesson, stated in
-- full in mod_pdungeon_runs_difficulty.sql's header): the updater applies a
-- module's NEW files in FILENAME order, so naming a column a file further down
-- that order has not created yet aborts the very first host boot with error
-- 1054 while every workbench boot looks fine. Column position is cosmetic;
-- every reader binds by name.
-- ----------------------------------------------------------------------------

SET @dbname := DATABASE();

SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
             WHERE TABLE_SCHEMA = @dbname AND TABLE_NAME = 'pdungeon_account'
               AND COLUMN_NAME = 'cfg_theme');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `cfg_theme` TINYINT UNSIGNED NOT NULL DEFAULT 0',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
