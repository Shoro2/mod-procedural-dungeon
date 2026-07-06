-- ----------------------------------------------------------------------------
-- mod-procedural-dungeon: run history (characters database)
-- One row per started run; basis for future leaderboards / daily seeds.
-- result: 0 = open, 1 = completed
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `pdungeon_runs` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `seed` INT UNSIGNED NOT NULL,
  `map_id` INT UNSIGNED NOT NULL,
  `instance_id` INT UNSIGNED NOT NULL,
  `leader_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `started_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `completed_at` TIMESTAMP NULL DEFAULT NULL,
  `result` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_instance` (`instance_id`),
  KEY `idx_seed` (`seed`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
