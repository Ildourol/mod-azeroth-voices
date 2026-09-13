-- Known-bot roster for the unchanged stock Chatter Companion addon.
--
-- One row per (real player, PlayerBot) pair the player has actually exchanged
-- chat with. The roster the addon shows is exactly these rows; forgetting a bot
-- deletes only this row and the pair's stored conversation history. The bot's
-- global personality (azeroth_voices_bot_personality) and its sentiment
-- (azeroth_voices_sentiment) are deliberately not touched.
CREATE TABLE IF NOT EXISTS `azeroth_voices_addon_contacts` (
  `player_guid` BIGINT UNSIGNED NOT NULL,
  `bot_guid` BIGINT UNSIGNED NOT NULL,
  `bot_name` VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`player_guid`, `bot_guid`),
  KEY `idx_av_addon_contacts_updated` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
