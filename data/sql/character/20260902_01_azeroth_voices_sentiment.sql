-- One directional relationship score. Runtime V1 permits only an AI-controlled
-- PlayerBot actor toward a real-player target. Generic key names intentionally
-- leave room for future actor/target kinds without replacing the schema.
CREATE TABLE IF NOT EXISTS `azeroth_voices_sentiment` (
  `actor_guid` BIGINT UNSIGNED NOT NULL,
  `target_guid` BIGINT UNSIGNED NOT NULL,
  `score` SMALLINT NOT NULL DEFAULT 0,
  `last_interaction_at` TIMESTAMP NULL DEFAULT NULL,
  `last_decay_at` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`actor_guid`, `target_guid`),
  KEY `idx_av_sentiment_target` (`target_guid`),
  KEY `idx_av_sentiment_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
