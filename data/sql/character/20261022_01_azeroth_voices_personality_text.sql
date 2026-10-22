-- Upgrade azeroth_voices_bot_personality background column to TEXT to remove the 1500 character limitation
ALTER TABLE `azeroth_voices_bot_personality` MODIFY `background` TEXT NOT NULL;
