-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: pdungeon_account.cfg_stat_profile (characters database)
--
-- Round E / WP9 (2026-09-10). Which stat line the depths' gear rolls prefer for
-- this account: 0 Off, 1 Strength, 2 Agility, 3 Caster. Until now a run filtered
-- gear by class, faction, armour type and weapon type only, so a spell-power
-- ring could drop for a rogue; with a profile set, every cache, every boss
-- corpse and the final cache also ask whether the item's stat line suits it
-- (PDv2LootMgr / FitsProfileRaw, "primary stat wins").
--
-- Per ACCOUNT, like every other cfg_* knob on this table, and NOT per character
-- on purpose: the choice is shared, while the right to make it is earned per
-- character. That right is the Forgotten Talents node "Discerning Eye" (tag
-- 76004) and it is deliberately NOT a column here - it is an aura, read live at
-- every roll, so a refunded node stops biting at once and no migration has to
-- chase it.
--
-- A SEPARATE file rather than an edit to mod_pdungeon_account.sql, for the
-- reason mod_pdungeon_account_difficulty.sql states: the AC updater applies each
-- SQL file exactly once and remembers it by hash, so touching the base file
-- would leave every existing database without this column while claiming to be
-- up to date. New file = fresh databases create then alter, existing ones just
-- alter. MySQL 8 has no ADD COLUMN IF NOT EXISTS, so the ALTER is guarded on
-- information_schema, which also makes this file safe to re-apply by hand.
--
-- DEFAULT 0 - Off, PD_STAT_PROFILE_OFF - and no heal statement to move it: an
-- account that has never seen the panel row must roll exactly what it rolled
-- before this file existed. The column default alone says that, both for the
-- rows that already exist and for every row a later INSERT creates.
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
               AND COLUMN_NAME = 'cfg_stat_profile');
SET @sql := IF(@col = 0,
    'ALTER TABLE `pdungeon_account` ADD COLUMN `cfg_stat_profile` TINYINT UNSIGNED NOT NULL DEFAULT 0',
    'DO 0');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
