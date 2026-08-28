CREATE TABLE IF NOT EXISTS `azeroth_voices_chat_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `history_key` VARCHAR(191) NOT NULL,
  `actor_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `speaker_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `scope` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `channel_name` VARCHAR(96) NOT NULL DEFAULT '',
  `speaker_name` VARCHAR(64) NOT NULL DEFAULT '',
  `speaker_message` TEXT NOT NULL,
  `actor_name` VARCHAR(64) NOT NULL DEFAULT '',
  `actor_reply` TEXT NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_av_chat_key_created` (`history_key`, `created_at`),
  KEY `idx_av_chat_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `azeroth_voices_environment_history` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `actor_key` VARCHAR(96) NOT NULL,
  `actor_guid` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_kind` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_name` VARCHAR(64) NOT NULL DEFAULT '',
  `map_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `zone_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `area_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `snapshot` TEXT NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_av_environment_actor_created` (`actor_key`, `created_at`),
  KEY `idx_av_environment_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
