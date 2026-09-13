# English/Greeklish Solo-Player Implementation Plan

## Summary

Refocus Azeroth Voices for one real player and two bot speech languages only:

- `English`
- `Greeklish`: conversational Greek written exclusively with Latin characters

Remove general multilingual codes, MatchSpeaker detection, and per-player language preferences. Language becomes a persistent per-PlayerBot setting, with a server default and optional weighted random assignment.

Keep ambient NPC proximity scenes around the solo player, but disable recognition, collection, and prompt inclusion of other real players.

## Language and Solo-Player Configuration

Add these settings:

```ini
# Supported values: English, Greeklish
AzerothVoices.Language.Default = English

# Assign a stable random language to PlayerBots still using Default.
AzerothVoices.Language.Random.Enable = 0
AzerothVoices.Language.Random.EnglishPercent = 50
AzerothVoices.Language.Random.GreeklishPercent = 50

# Optimize real-player discovery and context for one active owner.
AzerothVoices.SoloPlayer.Enable = 1
```

Configuration rules:

- Language names are parsed case-insensitively but stored canonically.
- No arbitrary language codes or additional languages are accepted.
- Invalid default language falls back to `English` with a startup error.
- Percentages must each be 0–100 and total exactly 100.
- Invalid percentages disable random assignment for that configuration load and use the configured default.
- Random mode affects PlayerBots only. Creatures and NPC personalities use the configured default.
- `.av status` reports the default language, random state and weights, solo-player state, and language-table availability.

Add module types:

```cpp
enum class BotLanguage : uint8
{
    Default = 0,
    English = 1,
    Greeklish = 2
};

enum class LanguageSource : uint8
{
    ServerDefault,
    RandomAssignment,
    BotOverride
};
```

Effective-language precedence:

1. Explicit per-bot `English` or `Greeklish` override.
2. Persisted random assignment when the bot preference is `Default` and random mode is enabled.
3. `AzerothVoices.Language.Default`.

## Bot Language Assignment and Prompt Behavior

### Persistent assignment

Add migration `20260912_03_azeroth_voices_bot_languages.sql`:

```sql
CREATE TABLE IF NOT EXISTS `azeroth_voices_bot_languages` (
  `character_guid` BIGINT UNSIGNED NOT NULL,
  `bot_name` VARCHAR(64) NOT NULL DEFAULT '',
  `language_preference` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `random_assignment` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
      ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

Implementation behavior:

- No row means the bot initially uses the server default.
- When random mode first encounters a `Default` bot, roll the configured weights once and persist `English` or `Greeklish`.
- Existing random assignments remain stable across conversations, logins, restarts, and percentage changes.
- Selecting `Default` in the addon clears the explicit override:
  - With random enabled, clear and immediately reroll the random assignment.
  - With random disabled, clear the random assignment and use the server default.
- An explicit addon choice overrides random mode until reset to `Default`.
- Cache records by bot GUID on the world thread; provider workers receive only the resolved immutable language.
- If the table is missing, use the same behavior in bounded RAM and report it as volatile rather than blocking dialogue.

### Central prompt directive

Resolve the bot’s language in `Manager::BuildRequest` and store the effective language and source in `ChatRequest`. Append the language directive after Normal/Roleplay prompt selection and before provider serialization.

English directive:

```text
Write the complete visible reply in natural English.
Preserve all proper names, player and guild names, locations, quests,
items, spells, item links, commands, and formatting tokens exactly.
Return only the requested dialogue.
```

Greeklish directive:

```text
Write the complete visible reply as natural conversational Greek using
Latin letters only (Greeklish). Do not output characters from the Greek
alphabet. Do not translate or transliterate established World of Warcraft
names, character names, player names, guild names, locations, quests,
items, spells, item links, commands, or formatting tokens. Return only
the requested dialogue.
```

Additional rules:

- Do not infer language from player messages; English and Greeklish are both Latin-written and can be ambiguous.
- Keep personality traits, tone, backstory, JSON keys, and internal metadata in English. Only visible dialogue follows the bot language.
- Preserve the directive when the provider replaces the global prompt with the NPC Roleplay prompt.
- Apply it consistently to Responses API, Chat Completions, and custom JSON providers.
- Warn during startup when a custom JSON template lacks `<pre prompt>` and therefore cannot receive the language directive.
- Do not add a second model request.

Add a scoped UTF-8 Greek fallback:

- Detect Greek Unicode characters in a response selected as Greeklish.
- Transliterate unexpected Greek characters to Latin output before delivery.
- Handle accents and final sigma, plus common Greek digraphs such as `αι`, `ει`, `οι`, `ου`, `μπ`, `ντ`, `γκ`, `τσ`, and `τζ`.
- Never transliterate protected WoW links, commands, formatting tokens, placeholders, or proper-name segments already supplied by the module.
- Ensure truncation and packet splitting do not bisect UTF-8 characters or percent-encoded addon tokens.

## Addon and Command Interface

Extend the shared `.avaddon` and `.llmc` parser:

```text
language get <bot-guid>
language set <bot-guid> default
language set <bot-guid> english
language set <bot-guid> greeklish
```

Add command kinds:

```cpp
LanguageGet
LanguageSet
```

Return:

```text
LANGUAGE <guid> <preference> <effective> <source> <storage>
```

Values:

- `preference`: `default`, `english`, or `greeklish`
- `effective`: `english` or `greeklish`
- `source`: `default`, `random`, or `override`
- `storage`: `persistent` or `volatile`

Turtle WoW addon changes:

- Add a Language section to each bot profile.
- Show `Default`, `English`, and `Greeklish` as the only choices.
- Display the effective language and whether it came from default, random assignment, or explicit override.
- Display a warning when changes are session-only because the database table is unavailable.
- Fetch language state whenever a bot profile is selected.
- Keep Lua 5.0 compatibility and existing 255-byte addon-message framing.
- Do not add a player-wide language selector; the config supplies the global default and the addon controls individual bots.
- Existing roster, personality, backstory, memory, and contact protocol remains compatible.

## Solo-Player Behavior

When `AzerothVoices.SoloPlayer.Enable = 1`:

- Treat the first active non-AI-controlled player session as the solo owner.
- Clear that binding when the owner logs out; the next eligible login becomes the owner.
- Ignore chat hooks and dialogue targeting from any additional real-player session while the owner is online.
- Do not enumerate other real players for nearby, guild, group, channel, world, audience-selection, sentiment, or follow-up logic.
- Disable the `{nearby_players}` snapshot section and default `Snapshot.IncludeNearbyPlayers` to false.
- Do not record other real-player speech in surrounding-chat context.
- Keep the owner’s direct messages, history, sentiment, group membership, and conversations with PlayerBots.
- Retain PlayerBot group, guild, and bot-to-bot logic where the owner is the audience or group anchor.
- Retain NPC proximity scenes, boss dialogue, events, combat dialogue, and ambient chatter around the owner.
- Keep audience-presence checks, but restrict them to the bound solo owner instead of searching for arbitrary real players.
- Do not classify PlayerBots as additional real players; continue using `Script_IsAIControlled`.

No vMaNGOS core or PlayerBots source changes are permitted. All behavior remains inside the module using existing vMaNGOS hooks, world-thread database access, script registration, and generic PlayerBots detection.

## Test and Acceptance Plan

- Verify only `English`, `Greeklish`, and `Default` are accepted by config, commands, and addon controls.
- Test precedence for explicit override, persisted random assignment, and server default.
- Test 0/100, 100/0, and mixed random weights using injectable deterministic random values.
- Confirm random assignment occurs once per Default bot and survives restart.
- Confirm resetting to Default rerolls only when random mode is enabled.
- Verify missing language table gives stable session-only assignments and never blocks dialogue.
- Assert English prompts produce an English directive and Greeklish prompts require Latin-only Greek.
- Test Greek-to-Greeklish fallback with accented Greek, final sigma, uppercase letters, and common digraphs.
- Verify WoW names, item links, commands, and formatting survive language processing unchanged.
- Test all provider formats, Normal/Roleplay prompts, NPC prompt replacement, and missing `<pre prompt>` warnings.
- Log in one real player with several PlayerBots and confirm all supported dialogue systems still work.
- Simulate a second real-player session and confirm it is ignored without disturbing the owner.
- Verify the nearby-player snapshot is empty while NPC and PlayerBot environmental context remains available.
- Confirm NPC proximity conversations still run around the owner.
- Run existing module tests plus new language, persistence, addon-protocol, solo-owner, UTF-8, and configuration tests.
- Build Debug and Release `mangosd` configurations against `tortoise-wow-extended`, including PlayerBots.
- Install the addon in Turtle WoW 1.18.1 and verify profile selection, language changes, resets, random assignments, chat rendering, and zero Lua errors.

Update the root README, sample configuration, client README, database migration instructions, and architecture explanation to describe this English/Greeklish solo-player model.
