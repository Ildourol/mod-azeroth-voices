-- Deterministic per-bot/player memory ledger (V0.8).
--
-- One row per remembered fact a PlayerBot holds about one real player. Summaries
-- are written by C++ from verified facts only: no provider call is made and no
-- player-written text is ever stored. Retention is bounded by the module: the
-- flush keeps the newest Memory.MaximumPerPair rows per pair and the hourly
-- cleanup deletes rows untouched for Memory.RetentionDays days.
--
-- Forgetting a bot through the Chatter addon, or `.av memory forget`, deletes
-- only that pair's rows. Personality, sentiment, and conversation history live in
-- their own tables and are never touched by this migration.
CREATE TABLE IF NOT EXISTS `azeroth_voices_bot_memory` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `bot_guid` BIGINT UNSIGNED NOT NULL,
  `player_guid` BIGINT UNSIGNED NOT NULL,
  `memory_type` VARCHAR(32) NOT NULL,
  `summary` VARCHAR(512) NOT NULL,
  `importance` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_av_memory_pair` (`bot_guid`, `player_guid`, `created_at`),
  KEY `idx_av_memory_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
