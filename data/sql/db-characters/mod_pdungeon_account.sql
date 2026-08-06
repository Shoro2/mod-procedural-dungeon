-- PDv2 per-account persistence (characters database).
--
-- The layout is NOT stored as data: a plan is deterministic from seed +
-- generation config (the harness pins that over hundreds of seeds), so this
-- row stores exactly those and the server regenerates on login. The gen_*
-- columns freeze the generation inputs actually used, so a later change of
-- the server's V2.* config cannot silently reshape an account's existing
-- dungeon; layout_version forces a reroll when the generator itself changes
-- incompatibly.
--
-- dlvl/dxp and the cfg_* columns are the 01 §7 gameplay schema, written with
-- defaults today and owned by the gameplay slice; the persistence code only
-- ever updates the layout columns (INSERT .. ON DUPLICATE KEY UPDATE), so
-- progression can never be clobbered by a reroll.

CREATE TABLE IF NOT EXISTS `pdungeon_account` (
    `accountId`         INT UNSIGNED      NOT NULL,
    `dlvl`              INT UNSIGNED      NOT NULL DEFAULT 0,
    `dxp`               INT UNSIGNED      NOT NULL DEFAULT 0,
    `cfg_rooms`         TINYINT UNSIGNED  NOT NULL DEFAULT 5,
    `cfg_diff_x100`     SMALLINT UNSIGNED NOT NULL DEFAULT 100,
    `cfg_caster_pct`    TINYINT UNSIGNED  NOT NULL DEFAULT 60,
    `cfg_packs`         VARCHAR(255)      NOT NULL DEFAULT '',
    `cfg_mob_level_min` TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `theme`             TINYINT UNSIGNED  NOT NULL DEFAULT 1,
    `layout_seed`       INT UNSIGNED      NOT NULL DEFAULT 0,
    `layout_version`    INT UNSIGNED      NOT NULL DEFAULT 0,
    `gen_rooms`         TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `gen_boss_rooms`    TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `gen_field_blocks`  TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `gen_origin_bx`     SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `gen_origin_by`     SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `gen_loop_pct`      TINYINT UNSIGNED  NOT NULL DEFAULT 0,
    `createdAt`         TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updatedAt`         TIMESTAMP         NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`accountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
