CREATE TABLE IF NOT EXISTS `azeroth_voices_bot_personality` (
  `character_guid` BIGINT UNSIGNED NOT NULL,
  `bot_name` VARCHAR(64) NOT NULL DEFAULT '',
  `traits_json` VARCHAR(512) NOT NULL,
  `tone` VARCHAR(200) NOT NULL DEFAULT '',
  `background` VARCHAR(1500) NOT NULL DEFAULT '',
  `background_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `generation_version` INT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_guid`),
  KEY `idx_av_personality_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
