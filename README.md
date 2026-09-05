# Azeroth Voices

![Azeroth Voices](mod-azeroth-voices.jpg)

Current development milestone: **V0.6**. The sentiment subsystem described below is its V1 design.

`mod-azeroth-voices` is a standalone, provider-neutral conversation and ambient-chatter module for TortoiseWoW. It observes PlayerBots through the core's generic AI-control hook, so it does not modify anything under `src/modules/PlayerBots`.

It is inspired by `mod-ollama-chat`, while the persistent identity and relationship methodology is adapted selectively from `mod-llm-chatter`, Soulbind, and `mod-pbc`: stable GUID identities, distinct traits, derived speaking tone, persistent background, and a bounded relationship ledger. It does not copy their Python bridges, full-table startup loads, text relationship summaries, romance/bot-social graphs, database polling, detached-thread models, AzerothCore APIs, unrelated memory systems, or disabled TLS behavior. The module uses the HTTP and JSON libraries already shipped by TortoiseWoW (`cpp-httplib` and `nlohmann/json`) with OpenSSL certificate verification enabled.

The local reference-module trees were treated strictly as feature references. All registration, script hooks, chat delivery, object lookup, configuration loading, CMake discovery, and PlayerBot detection in this module use the APIs and names present in the local `tortoise-wow-playerbots-integration-gh` vMaNGOS fork. No AzerothCore headers, script loaders, configuration sections, database APIs, or playerbot classes are imported.

## Included features

- Player-to-bot replies in whisper, say, yell, party, raid, guild, officer, world, and optionally custom channels.
- Bot-to-bot whisper replies are disabled. Bot-originated say/yell requires a nearby real player; bot-only party/raid chat is disabled, with party requiring a real member in the bot's subgroup and raid requiring a real raid member both before queueing and again before delivery. Guild/officer replies require a real member of the corresponding audience. World replies require one online real player anywhere on the server, while custom-channel replies retain the conservative public-count gate. Channel reply actors are not faction-filtered.
- NPC generation covers ambient chatter, qualifying nearby event reactions, fresh-combat opening reactions, and reactions to human or PlayerBot Say. NPCs use Say only, are discovered within `NPC.Distance` of normal speakers/anchors (10 yards by default), require a real human within `SayDistance` (25 yards by default), and must be a static creature of an allowed type. Neutral and hostile categories are enabled by default through `NPC.AllowNeutralAndHostile = 1`, while the normal NPC paths retain separate friendly/neutral/hostile probability gates. Owned, charmed, summoned, pet, guardian, totem, trigger, critter, and temporary creatures remain excluded without using service `npc_flags`. When a real player explicitly selects an eligible NPC and speaks in Say, the selected NPC is considered first; other eligible NPCs and PlayerBots within `NPC.Distance` may join using the three `NPC.Targeted*Chance` controls. Replies while actors are in combat are enabled by default and can still be disabled globally.
- Fresh-combat creature speech is a separate one-shot path: when a real player moves from out of combat into combat by attacking a creature, only that exact opening creature can roll `NPC.CombatStart.Chance` (30% by default) for one short generated Say line. Pulling or attacking another creature while the same combat remains active cannot trigger another opening line. The latch resets only after the player leaves combat, and `NPC.CombatStart.CooldownSeconds` adds a separate per-real-player accepted-reaction cooldown (60 seconds by default). Body-aggro without a player attack does not qualify.
- Random chatter defaults to `say,guild,world,party`. Party is used only when Random selects the Party scope and requires the chosen PlayerBot to share a subgroup with an online real player. Say remains Say; when no eligible local Say speaker exists, that Random attempt is skipped. World chatter occurs only when Random selects the configured World scope. Other scopes retain their normal audience rules; there is no separate Party-priority or Random-distance option.
- A follow-up chance for short PlayerBot/NPC conversations after ambient lines, bot-originated replies in every non-whisper scope, and event lines. The triggering AI is preferred for the next turn when still eligible. Each turn repeats the scope's real-audience rule; `AiPlayerbot.LLMBotToBotChatChance` caps PlayerBot-to-PlayerBot follow-ups, while `AiPlayerbot.LLMRpgAIChatChance` caps any follow-up involving an NPC. Every follow-up pair containing an NPC must be within `NPC.Distance` actor-to-actor and share one real-human observer within `SayDistance`; these checks repeat before delivery. NPC turns remain Say-only, and lone NPCs do not start ambient monologues. Combat-start reactions deliberately disable generated follow-ups.
- Event chatter for deaths, kills, loot, quests, learned spells, duels, levels, guild login/join/leave, plus a public event-adapter method for other scripts. Guild join/leave/login/promotion/demotion and a guilded level-up use Guild and require an online real guild listener. Other events prioritize Party for PlayerBots in the subject's party subgroup, then use local Say for remaining PlayerBot or NPC responders. Every generated actor passes the audience rule for its actual delivery scope before a provider request is built.
- Lightweight live environment context (map, zone, subzone, dungeon/combat/group state, nearby creatures, equipped items, and optionally backpack items), built only on the world thread and capped by configuration.
- Fixed worker pool, priority queues, a reserved high-priority section, global rate limit, TTLs, cooldowns, retry/backoff, and stale-request replacement.
- Proper JSON request creation and response parsing for OpenAI-compatible Chat Completions and OpenAI Responses-style APIs.
- OpenAI, Gemini's OpenAI-compatible API, OpenRouter, llama.cpp-compatible servers, and other compatible endpoints through configuration.
- OpenSSL 3 default-provider initialization, full certificate verification, optional custom CA path, and HTTP restricted to localhost.
- Conversation storage modes: disabled, bounded RAM, or persistent character-database history (default), with lazy reads, batched asynchronous writes, TTL cleanup, and per-conversation limits.
- Bounded recent surrounding chat isolated by whisper pair, group, guild, channel, or local map/area.
- Optional rich PlayerBot snapshots containing combat/resources/target, group members, highest useful spell ranks, active quests, line-of-sight creatures/game objects, and nearby players, all captured through bounded world-thread searches.
- Optional older snapshot retention in bounded RAM or SQL under the same Snapshot system; the current snapshot and lightweight live environment always override stale records.
- Optional structured local JSON RAG with deterministic weighted similarity and the complete adapted Vanilla/Turtle corpus; the former text-file knowledge subsystem has been removed.
- Persistent per-PlayerBot personalities keyed by character GUID, with 1-5 generated traits, optional speaking tone and background, roleplay or fictional real-world-player mode, lazy generation, SQL persistence, and a bounded RAM hot cache. The distributed default is the fictional real-world WoW player background mode.
- Persistent one-directional PlayerBot-to-real-player sentiment, with a `-100…100` score, C++-derived tiers, direct-chat-only bounded changes, lazy asymmetric decay, bounded lazy RAM/SQL storage, stale-neutral cleanup, and private moderator controls.
- Dynamic current talent specialization and point-split context derived on the world thread; talent changes affect later prompts without rewriting persistent identity.
- Typing delay compatible with the restored PlayerBots behavior: generation time can be subtracted from the character-based delay.
- Optional terminal telemetry for one-time per-message details and periodic counters-only API-call/result summaries.
- GM commands for status, one lightweight global sanity check, explicit generation and ambient tests, pause/resume, restart, and history clearing.

Personality and sentiment are additional PlayerBot prompt layers. They do not replace directed chat, random/ambient chatter, world/guild/group delivery, history, Environment, Snapshot, RAG, provider scheduling, or cooldown behavior. NPCs use neither personality nor sentiment records. Sentiment is never tracked for NPCs, real-player-to-bot direction, or PlayerBot-to-PlayerBot pairs.

## V0.6 feature review and design decisions

The original notes point in the right direction: an integer ledger, derived tiers, lazy decay, and bounded lazy persistence are simpler and safer than importing the much larger relationship systems from the reference modules. V0.6 implements those ideas with the following decisions and additions:

| Feature | V0.6 decision/comment | Token efficiency | Complexity | How it works | Example |
|---|---|---:|---:|---|---|
| Persistent bot→player affinity | Keep one directional integer for each PlayerBot/real-player pair. This is enough for tone without introducing romance, memories, or a social graph. | None at storage time | Medium | SQL and C++ use generic `actor_guid`/`target_guid`; runtime accepts only an AI-controlled PlayerBot actor and real-player target. | Bot A → Alice can be `24`; Alice → Bot A and Bot A → Bot B do not exist. |
| Relationship tiers | Derive tiers in C++; never persist duplicate tier state. The exact score remains available to moderators. | Very low | Low | `hostile ≤ -40`, `cold -39…-10`, `neutral -9…9`, `warm 10…39`, `trusted ≥ 40`. | Score `24` injects `warm`, while `.av sentiment inspect` shows `24`. |
| Bounded relationship changes | Only qualifying player-written chat can change a score, and the configured `±2` limit is a hard bound rather than prompt-only advice. | A few output tokens | Low | Whisper always qualifies; Party qualifies with exactly one subgroup PlayerBot or an explicit full-name mention; Say and World require the full bot name. Event, Random, other scopes, NPCs, and AI speakers cannot mutate sentiment. | A malicious `+999` marker becomes `+2`; an unmentioned World responder receives no marker and no change. |
| Relationship decay | Lazy decay avoids global scans. The grace period and asymmetric rates preserve earned friendship longer than grudges. | None | Medium | On pair read/change, full inactive days after the grace period move the score toward zero; defaults are `+1/day` decay for positive scores and `2/day` toward zero for negative scores. | After the 7-day grace, `-20` becomes `-18` after one full day; `+20` becomes `+19`. |
| Persistent SQL storage | Bounded lazy loading, coalesced dirty writes, and RAM fallback are the right trade-off. No relationship table is bulk-loaded at startup. | None | Medium | A 4,096-pair hot cache and 1,024-pair dirty map are bounded; repeated changes to one pair coalesce; normal flushes queue 50 upserts. | Five quick changes for one pair produce one latest-value upsert. |
| GM inspect/set/reset | Private moderator commands use exact online identities and exact integer validation. Pair reset and reset-all are intentionally different commands. | None | Low | `inspect`, `set`, and pair `reset` require one exact online PlayerBot plus one exact online real player; `reset all` is explicit. | `.av sentiment set Brann Alice 25` reports tier `warm`. |
| Relationship affects replies | Inject only the tier near personality context and charge the full block against the existing context budget. It changes tone, never facts or safety. | Low | Low | The model receives the target name and derived tier, not the exact score. | A trusted bot can answer Alice warmly but cannot invent quest completion. |
| Personality in random/events | Keep the common request path, but make random and event use separately configurable. | No extra call | Low | `Personality.UseInRandom` and `Personality.UseInEvents` gate those prompt layers. | An event responder keeps its stable traits when event use is enabled. |
| Sentiment in random/events | Random and Event sentiment are optional read-only tone context and default off. Gameplay events never change relationships. | Low when enabled | Low | `Sentiment.UseInRandom` and `Sentiment.UseInEvents` gate tier injection only; both paths force a zero delta limit and request no metadata. | Alice's quest event can sound warm when enabled, but the score remains unchanged. |
| Additional: no second sentiment API call | Piggyback a tiny hidden integer on the existing dialogue completion. This is lighter than the separate classifier call used by some references. | High savings | Medium | The model appends `[[AV_SENTIMENT:N]]`; C++ strips it before splitting/logging/delivery and hard-clamps it. Missing/malformed metadata leaves dialogue usable and makes no change. | `Welcome back!` is delivered; `[[AV_SENTIMENT:+1]]` is not. |
| Additional: commit after delivery | Relationship state should not change for stale, failed, or undeliverable text. | None | Low | The parsed delta is applied only after successful first-line delivery, alongside history/snapshot commit timing. | If the player logs out before a whisper is delivered, no score changes. |
| Additional: fail-soft observability | Missing SQL must not stop dialogue, but operators need to see the fallback. | None | Low | Startup logs plus `.av status`/`.av test` report SQL versus RAM, cached pairs, and pending dirty pairs. | A missing migration reports RAM fallback while replies continue. |
| Additional: stale-neutral cleanup | Neutral rows should not accumulate forever on a long-running realm. | None | Low | The existing hourly SQL cleanup deletes only score-zero rows whose last update is older than 90 days. | A recently neutral relationship stays available; a 90-day-old neutral row is removed without scanning on every tick. |

### V0.6 implementation plan

1. Enforce PlayerBot→real-player scope at request creation, moderator lookup, and post-delivery commit; keep NPC and bot-target paths record-free.
2. Represent one generic GUID pair and integer score, derive the five tiers in C++, and place only the tier beside personality context.
3. Reuse the existing dialogue response for one stripped, hard-clamped `±2` delta only for qualifying direct player-written chat; Event and Random are read-only.
4. Add lazy asymmetric decay, a bounded lazy cache, a bounded coalescing dirty map, small character-database upserts, and fail-soft RAM operation.
5. Apply a delta only after first-line delivery and after world-thread identity re-resolution.
6. Expose private exact-name inspect/set/pair-reset/reset-all commands and SQL/RAM status without leaking credentials.
7. Verify the actual TortoiseWoW hooks and APIs, generated module loader, migration discovery path, install manifest, parser boundaries, and Release build.

### Reusable implementation/audit prompt

```text
Continue the existing mod-azeroth-voices module; do not rewrite it or modify the
TortoiseWoW core/PlayerBots. Preserve unrelated work. Audit the checked-out
vMaNGOS and IKE3 APIs before using a hook. Keep all live game objects on the
world thread and send only copied value data through the fixed worker pool.

Maintain sentiment V1 as one directional PlayerBot actor -> online real-player
target state. Never create NPC, real-player actor, or PlayerBot-target sentiment.
Use generic actor_guid/target_guid SQL keys, one clamped -100..100 score, and
C++-derived hostile/cold/neutral/warm/trusted tiers. Give the model only the tier
near personality context and charge it to the existing context budget. It may
change interpersonal tone, never facts, safety, or game-state truth.

Reuse the dialogue completion for stripped [[AV_SENTIMENT:N]] metadata. Clamp
qualifying player-written chat changes to +/-2. Whisper always qualifies; Party
qualifies with exactly one subgroup PlayerBot or a full-name mention when there
are several; Say and World require a full-name mention. Event and Random may use
the tier but must not mutate it. Apply a valid delta only after successful first-
line delivery and world-thread re-resolution. Decay lazily after an inactivity
grace period, with negative scores fading faster. Keep SQL reads lazy, RAM and
pending writes bounded, writes dirty/coalesced, and dialogue functional in RAM
when SQL is unavailable. Delete neutral SQL rows older than 90 days through the
bounded periodic cleanup. Keep exact private GM inspect/set/pair-reset/reset-all
commands, with reset-all explicitly separate. Never add Event/Random deltas.

Update configuration, character migration, commands, status/test output, and
V0.6 documentation together. Reconfigure CMake, inspect its generated loader and
install rules, run parser/tier boundary tests, build Release mangosd, and report
source/build/SQL/startup/live-game evidence as separate validation levels.
```

## Thread-safety model

```text
validated game chat/event
        |
        v
snapshot strings and GUIDs on a game/world thread
        |
        v
bounded priority queues -> fixed HTTP worker threads
        |
        v
completion queue (strings/GUIDs only)
        |
        v
WorldScript::OnUpdate resolves GUIDs and sends chat
```

Missing PlayerBot personalities use the same bounded worker/provider/completion pipeline as dialogue. The accepted dialogue request is queued first and proceeds without blocking; one low-priority personality job is then queued on demand. Its value-owned completion is validated, cached, and persisted on the world thread and influences subsequent prompts.

Sentiment adds no worker-side live state and no second provider request. A value-owned tier/instruction crosses to the worker; the world thread strips and validates returned metadata, then re-resolves the PlayerBot and real player before applying a delivered delta.

Workers never retain or manipulate `Player`, `Creature`, `Unit`, `Guild`, `Group`, or `WorldSession` pointers. All object lookup and chat delivery happens after map updates in the world update hook. The combat-start detector also runs on `PlayerScript::OnUpdate` on the game/world thread; it stores only real-player GUID keys and timestamps, then the normal request pipeline copies value snapshots before worker dispatch.

## Dependencies

No Python, Python proxy, Ollama daemon, vector database, embedding service, or extra HTTP/JSON package is needed. History, snapshots, personalities, and sentiment use four distinct module-owned tables in the existing character database. Use the normal TortoiseWoW build dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake git libace-dev libboost-all-dev \
  default-libmysqlclient-dev libssl-dev zlib1g-dev libbz2-dev mariadb-server
```

OpenSSL 3 is supported. `libssl-dev` supplies the headers and libraries used by the vendored HTTPS client.

## Configure, build, and install

The module system discovers `modules/mod-azeroth-voices/src` automatically. You do not add source files manually to the root `CMakeLists.txt`.

From the repository root, re-run configuration once so CMake discovers the new module, then build and install:

```bash
cd /root/TWoWServerBots/Source/tortoise-wow

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/root/TWoWServerBots/Server \
  -DBUILD_PLAYERBOTS=ON \
  -DALLOW_TURTLE_ADDONS=ON \
  -DUSE_EXTRACTORS=OFF \
  -DMODULES=static

cmake --build build --target mangosd --parallel "$(nproc)"
cmake --install build
```

If your existing `build/CMakeCache.txt` already contains the correct install prefix and PlayerBots settings, the shorter reconfigure is enough:

```bash
cd /root/TWoWServerBots/Source/tortoise-wow
cmake -S . -B build -DMODULE_MOD_AZEROTH_VOICES=static
cmake --build build --target mangosd --parallel "$(nproc)"
cmake --install build
```

`static` is recommended for this server. `dynamic` is supported, but it adds a runtime module file and another deployment item without improving LLM latency.

### V0.6 validation record

The 2 September 2026 source audit used the checked-out Windows x64 Visual Studio 2022 build configured with PlayerBots enabled, Turtle addons enabled, extractors disabled, and static modules. CMake discovered `AzerothVoicesSentiment.cpp`, generated a static loader that calls `Addmod_azeroth_voicesScripts`, and generated install rules for the module config, data tree, and README. Standalone C++ boundary checks passed for score clamping, every tier edge, exact case-insensitive whole-name matching, metadata stripping, the absolute `±2` delta clamp, and zero-delta tier-only prompt text. The module target, linked Release `mangosd` target, and complete configured Release solution built successfully; the resulting worldserver executable is `bin/Release/mangosd.exe`.

This is source and build evidence only for that V0.6 validation record. The newer combat-start/default changes described here require a fresh build and live-game validation.

## Install the configuration

On Linux the template installs to:

```text
/root/TWoWServerBots/Server/etc/modules/mod-azeroth-voices.conf.dist
```

Create the active file while keeping the template:

```bash
cd /root/TWoWServerBots/Server/etc/modules
cp -n mod-azeroth-voices.conf.dist mod-azeroth-voices.conf
nano mod-azeroth-voices.conf
```

The distributed template is also the full configuration reference: every setting
has its type, accepted range or values, default, and runtime effect immediately
above it.

At minimum set:

```ini
AzerothVoices.Enable = 1
AiPlayerbot.LLMApiEndpoint = https://api.openai.com/v1/chat/completions
AiPlayerbot.LLMApiKey = env:OPENAI_API_KEY
AzerothVoices.Model = gpt-4.1-mini
```

Azeroth Voices loads `etc/modules/mod-azeroth-voices.conf`. Compatible legacy
provider and prompt values must be copied into this module file; they are not
automatically imported from `aiplayerbot.conf`. The module's only master switch
is `AzerothVoices.Enable`. Keep the `[worldserver]` section header at the top of
the module configuration; TortoiseWoW ignores settings outside a named INI
section.

The distributed dialogue defaults use `AzerothVoices.GlobalMode = Normal`, `AiPlayerbot.LLMPrompt = <sender name>: <initial message>`, and an empty `AiPlayerbot.LLMPostPrompt`. `<sender name>` is the incoming speaker; the responding actor is already identified by the system/pre-prompt, so the old `<bot name>:` post-cue is no longer emitted by default.

### Keep the API key out of the config

For tmux, an environment file is more reliable than relying on tmux's server environment (an already-running tmux server may retain an older environment). Create:

```bash
nano /root/TWoWServerBots/Server/etc/azeroth-voices.env
chmod 600 /root/TWoWServerBots/Server/etc/azeroth-voices.env
```

Contents:

```bash
OPENAI_API_KEY='replace-with-your-key'
```

In the start script, define the mangos command like this:

```bash
mangosd="set -a; source ./etc/azeroth-voices.env; set +a; exec ./bin/mangosd -c ./etc/mangosd.conf"
```

The command is executed inside the tmux pane, so the variable reaches `mangosd` even if the tmux server was already running. You set the key once in the protected environment file; you do not run the earlier OpenSSL diagnostic commands on every startup.

For a systemd unit, use the same environment file with restrictive permissions:

```ini
EnvironmentFile=/root/TWoWServerBots/Server/etc/azeroth-voices.env
```

```bash
chmod 600 /root/TWoWServerBots/Server/etc/azeroth-voices.env
```

## Provider examples

### OpenAI

```ini
AzerothVoices.ProviderMode = ChatCompletions
AiPlayerbot.LLMApiEndpoint = https://api.openai.com/v1/chat/completions
AiPlayerbot.LLMApiKey = env:OPENAI_API_KEY
AzerothVoices.Model = gpt-4.1-mini
AiPlayerbot.LLMApiJson =
```

### Gemini through its OpenAI-compatible endpoint

```ini
AzerothVoices.ProviderMode = ChatCompletions
AiPlayerbot.LLMApiEndpoint = https://generativelanguage.googleapis.com/v1beta/openai/chat/completions
AiPlayerbot.LLMApiKey = env:GEMINI_API_KEY
AzerothVoices.Model = your-supported-gemini-model
AiPlayerbot.LLMApiJson =
```

Set `GEMINI_API_KEY` in the same manner as the OpenAI key. Pick a model enabled for your Google project; provider model availability changes independently of this module.

### Other compatible endpoints

Point `AiPlayerbot.LLMApiEndpoint` at the provider's complete `/chat/completions` URL and select its model. Plain HTTP is accepted only for `localhost`, `127.0.0.1`, or `::1`, and only while `AzerothVoices.AllowLocalHttp = 1`.

## Existing PlayerBots LLM settings

These legacy key names are read directly from `mod-azeroth-voices.conf`, so an existing setup can migrate by copying its values without translating every name:

- endpoint, API key, JSON template, generation timeout, and misspelled simultaneous-generation limit;
- pre/prompt/post/RPG prompts and context size/global scope;
- response regex/split patterns when `ParserMode = LegacyRegex`;
- blocked channels, the bot-to-bot chat chance, and `AiPlayerbot.LLMRpgAIChatChance`. The RPG chance caps NPC reactions to PlayerBot Say and generated follow-ups involving an NPC.

For the safer built-in JSON path, leave `AiPlayerbot.LLMApiJson` blank and keep `AzerothVoices.ParserMode = ProviderJson`. When a custom JSON template is used, personality jobs apply their larger bounded output budget through `max_tokens` or `max_output_tokens` so the generated identity is not limited by the shorter dialogue budget. Persistent identities are generated, validated, and stored by the module rather than loaded as foreign character-card files.

## Database

`AzerothVoices.History.StorageMode` and `AzerothVoices.Snapshot.StorageMode` accept:

- `0`: no older records and no SQL writes. For Snapshot, this does not disable the current rich snapshot; `AzerothVoices.Snapshot.Enable` controls that.
- `1`: bounded RAM only; data is lost on worldserver restart.
- `2`: persistent character-database storage with a bounded lazy-loaded RAM hot cache; conversation history defaults to 2 while older snapshot storage defaults to 0.

The current source template sets `Database.AutoUpdate.CharUpdateName = "character"`, so updater-ready migrations are under [data/sql/character](data/sql/character). TortoiseWoW discovers enabled-module migrations when `Database.AutoUpdate.Enabled = 1` and the module is allowed by `Database.AutoUpdate.AllowedModules` (the normal default is `all`). Look for the history/snapshot, personality, and sentiment migrations in the updater startup log.

If automatic updates are disabled, import it manually into the same character database named by `mangosd.conf`:

```bash
cd /root/TWoWServerBots/Source/tortoise-wow
CHARACTER_DB=mangoscharacters
mysql -u root -p "$CHARACTER_DB" < \
  modules/mod-azeroth-voices/data/sql/character/20260827_01_azeroth_voices_history.sql

mysql -u root -p "$CHARACTER_DB" < \
  modules/mod-azeroth-voices/data/sql/character/20260829_01_azeroth_voices_personality.sql

mysql -u root -p "$CHARACTER_DB" < \
  modules/mod-azeroth-voices/data/sql/character/20260902_01_azeroth_voices_sentiment.sql
```

Change `mangoscharacters` to the database name from your `CharacterDatabaseInfo` setting. The history migration creates `azeroth_voices_chat_history` and `azeroth_voices_environment_history`; the second name stores optional Snapshot records. The other migrations create `azeroth_voices_bot_personality` and `azeroth_voices_sentiment`. If a requested table is missing, only that storage feature falls back to bounded RAM and dialogue continues.

History/snapshot and dirty sentiment writes use small transactions through vMaNGOS's database queue. Personality upserts and deletions use the same character-database API on the world thread. Reads happen lazily once per hot-cache key instead of once per message. `.av clearhistory` clears only conversation, surrounding-chat, and snapshot history; it never deletes personality or sentiment rows.

Persistent mode stores player message and generated reply text, including whispers. It never stores API keys. Treat the character database and backups as private data; use mode 0 or 1 if conversation retention is inappropriate.

## Persistent PlayerBot personalities

`AzerothVoices.Personality.Enable = 1` adds one persistent identity per AI-controlled PlayerBot character GUID. A missing identity is generated only after that bot receives an accepted dialogue request; the first dialogue is not delayed and may use no personality, while later dialogue uses the completed record. The module never bulk-generates identities at startup. `GenerateOnDemand = 0` simply omits missing personality context until a GM explicitly regenerates that online bot.

The generated JSON contains exactly the configured number of distinct traits plus optional tone and background fields. It is strictly parsed with `nlohmann/json`; unknown fields, wrong trait counts, duplicates, empty required fields, and oversized values are rejected. A failed job may retry only after `GenerationRetrySeconds`. Stored identities survive worldserver restarts in `azeroth_voices_bot_personality`; a bounded 2,048-entry RAM cache avoids per-line SQL reads. If SQL is unavailable, personality remains fail-soft and cache-only for that run.

Configuration defaults are:

```ini
AzerothVoices.Personality.Enable = 1
AzerothVoices.Personality.BackgroundMode = 1
AzerothVoices.Personality.GenerateBackground = 1
AzerothVoices.Personality.TraitCount = 3
AzerothVoices.Personality.GenerateTone = 1
AzerothVoices.Personality.GenerateOnDemand = 1
AzerothVoices.Personality.UseInRandom = 1
AzerothVoices.Personality.UseInEvents = 1
AzerothVoices.Personality.GenerationRetrySeconds = 300
AzerothVoices.Personality.MaxBackgroundChars = 500
AzerothVoices.Personality.MaxPromptChars = 700
```

Background mode `0` creates a fictional character who lives in Azeroth, grounded in Vanilla/Turtle-compatible race, class, faction, upbringing, formative events, and motivations. Mode `1`—the default—creates a fictional real-world WoW player persona with believable work/school/family, schedule, guild, raid, PvE/PvP, and MMO-culture details; it never impersonates a real person. The modes are mutually exclusive. Disabling background or tone generation does not automatically delete existing stored fields, and disabling the personality master toggle neither generates nor inserts personality context and does not delete SQL data.

All PlayerBot dialogue reaches the common `BuildRequest` path, so the same identity can affect whisper, say/yell, party/raid, guild/officer, world/custom channel, event, random/ambient, follow-up, and GM live-generation prompts. `UseInRandom` and `UseInEvents` can omit personality from those trigger families without deleting the stored identity. Traits are instructions for vocabulary, opinions, humor, emotions, confidence, caution, and social behavior—not text the bot should recite. The live talent tree and point split are recalculated from the current character spellbook for every actor snapshot, so a respec changes later prompts without modifying the persistent row.

Available prompt placeholders are:

- `<bot personality>` — comma-separated traits;
- `<bot tone>` — stored speaking style or empty;
- `<bot background>` — stored background or empty;
- `<bot personality block>` — a bounded grammatical block that omits absent fields safely;
- `<bot specialization>` — current dominant talent tree, class, and three-tree point split.

`MaxPromptChars` bounds the personality block, and its size plus any sentiment block is charged against `AiPlayerbot.LLMContextLength` before history, surrounding chat, snapshots, Environment, and RAG are selected. Existing custom PlayerBot pre-prompts that lack the new placeholders still receive one appended personality block and specialization line, so prompt coverage does not depend on replacing an operator's live template.

## PlayerBot-to-player sentiment V1

`AzerothVoices.Sentiment.Enable = 1` tracks only one direction: an AI-controlled PlayerBot's regard toward a real player. A pair changes automatically only when a valid hidden model delta accompanies a successfully delivered first line from a qualifying player-written interaction, or through an exact moderator `set` command. Whisper always qualifies. Party qualifies automatically when exactly one online PlayerBot shares the real speaker's subgroup; with several subgroup PlayerBots, only a bot whose full name is mentioned qualifies. Say and World require the responding bot's full name as a case-insensitive whole-word mention. Yell, Raid, Guild, Officer, custom channels, AI-written chat, generated follow-ups, gameplay Events, and Random chatter never change sentiment. NPC actors and targets, PlayerBot targets, and real-player actors remain excluded.

The exact score is an integer from `-100` through `100`. C++ derives the tier on demand: `hostile` at `-40` or below, `cold` from `-39` through `-10`, `neutral` from `-9` through `9`, `warm` from `10` through `39`, and `trusted` from `40` upward. SQL stores no tier column. The model receives only a short tier block and is explicitly told to use it for interpersonal tone rather than facts, safety, or current game state.

Qualifying direct conversation asks the same provider response for a final `[[AV_SENTIMENT:N]]` metadata line, with `N` hard-clamped to the configured `-2…+2` range. Event and Random requests always have a zero delta limit; when their optional context switches are enabled they receive only the existing tier and request no marker. The marker is removed before reply splitting, generated-message logging, delivery, surrounding chat, and history. Missing, invalid, or truncated metadata does not fail the dialogue and does not change the relationship. No classifier request, embeddings request, Python bridge, or second API call is used.

Defaults are:

```ini
AzerothVoices.Sentiment.Enable = 1
AzerothVoices.Sentiment.UseInRandom = 0
AzerothVoices.Sentiment.UseInEvents = 0
AzerothVoices.Sentiment.ConversationMaximumDelta = 2
AzerothVoices.Sentiment.InactivityGraceDays = 7
AzerothVoices.Sentiment.PositiveDecayPerDay = 1
AzerothVoices.Sentiment.NegativeDecayPerDay = 2
AzerothVoices.Sentiment.CacheMaximumEntries = 4096
AzerothVoices.Sentiment.PendingWriteMaximum = 1024
AzerothVoices.Sentiment.DatabaseFlushSeconds = 5
AzerothVoices.Sentiment.DatabaseFlushBatchSize = 50
```

Decay is evaluated only when a pair is read or changed. After the inactivity grace period, full inactive days move the score toward neutral. Negative scores use the faster negative rate, so grudges fade faster than positive trust. Each applied decay is dirty only when the integer score actually changes. The module never scans every relationship at startup or on a world tick.

`azeroth_voices_sentiment` uses the generic composite primary key `(actor_guid, target_guid)`, but V1 runtime validation still enforces PlayerBot→real-player. The bounded hot cache lazily loads individual pairs. The bounded dirty map coalesces repeated changes and queues `INSERT … ON DUPLICATE KEY UPDATE` batches. The existing hourly database-maintenance pass deletes only neutral rows whose `updated_at` is more than 90 days old, preventing indefinite neutral-ledger growth without a world-tick sweep. If the table is missing or a transaction cannot start, current-session sentiment remains in bounded RAM and dialogue continues.

## Context layers and storage

The lightweight Environment layer is enabled by default and cheaply supplies map, zone, area, level, dungeon/combat/group/guild state, nearby creature names, and bounded equipment or backpack names. It is useful for ordinary chatter and does not require the Snapshot system.

The optional Snapshot layer is PlayerBot-only and richer. It resolves the live bot on the world thread, reads current health and class power, victim details, bounded same-map group state, highest useful known spell ranks, occupied quest-log entries, nearest creatures and spawned game objects that pass LOS, and nearby non-GM players. Every list, search radius, and rendered block has a hard configuration limit. NPC requests receive the lightweight Environment context but do not attempt PlayerBot spellbook, quest-log, or group inspection.

`AzerothVoices.Snapshot.StorageMode` controls only older successfully delivered snapshots. Mode 0 still supplies the current rich snapshot when `Snapshot.Enable = 1`; mode 1 retains older snapshots in RAM; mode 2 persists them. Legacy V0.1 snapshot-storage keys are silently read as migration fallbacks but are intentionally absent from the V0.2 template.

Conversation history stores what a player said and the generated reply. Surrounding Chat stores recent chat lines in the same whisper, group, guild, channel, or local map/area scope. Neither is RAG: RAG is curated read-only game knowledge, while Environment and Snapshot describe live or previously observed runtime state.

The prompt gives priority to the newest player message, current live Environment, and current Snapshot. Older direct history, recent same-scope chat, older snapshots, and retrieved lore are explicitly marked as optional and possibly stale. Context budgeting reserves current context before lower-priority older/RAG content.

RAM maps, lines, turns, snapshots, rendered characters, pending queues, and telemetry samples all have hard configuration bounds. Pruning is time-sliced once per minute rather than scanning all history every world tick. Stale/dropped provider requests do not enter history; an exchange is retained only after its first line is successfully delivered in game.

## Structured local RAG

Set `AzerothVoices.RAG.Enable = 1` to load JSON documents from `AzerothVoices.RAG.Directory`. The installer copies the supplied data to `modules/mod-azeroth-voices/data/rag`, and the loader also checks the configured and source-tree paths. Every `.json` file is discovered once at startup in deterministic filename order. Files can contain one object, an array, or an `entries`/`items` array. Supported fields are `id`, `title`, `category`, `keywords` or `tags`, `text` or `content`, and `source`.

Retrieval uses the incoming message, useful trigger, map, zone, and area. Keyword matches receive the strongest weight, title/category matches receive medium weight, and body matches receive lower weight. Entries are rejected below `RAG.SimilarityThreshold`, sorted best-first, and bounded by `RAG.MaximumItems` and `RAG.MaximumCharacters`. No empty RAG block is added when nothing qualifies. With `AzerothVoices.Debug = 1`, one concise line reports how many loaded RAG entries matched a request.

The corpus includes every imported reference category—classes/factions, dungeons/raids, general tips, items, mechanics, NPCs, professions, PvP, quests, and zones—plus a compact Turtle-specific world file. Later-expansion material was rewritten for the Vanilla-plus level-60 timeline, High Elves, Goblins, custom Turtle locations/content, weapon skill, the Vanilla honor system, and supported professions. Some entries intentionally say that a later feature does not exist to prevent model hallucination. See [data/rag/README.md](data/rag/README.md) for the full file list and schema.

Retrieval is entirely local and cached—no embeddings, Python, vector database, network lookup, or second LLM/API call. A malformed file is logged and skipped without stopping the remaining corpus.

## GM commands

```text
.av status
.av pause
.av resume
.av restart
.av clearhistory
.av chatter [optional topic]
.av live <exact-bot-name> [optional prompt]
.av live - [optional prompt]       # choose a nearby eligible actor
.av test
.av personality show <exact-online-bot-name>
.av personality status <exact-online-bot-name>
.av personality regenerate <exact-online-bot-name>
.av personality delete <exact-online-bot-name>
.av personality delete all
.av sentiment inspect <exact-online-bot-name> <exact-online-player-name>
.av sentiment set <exact-online-bot-name> <exact-online-player-name> <-100..100>
.av sentiment reset <exact-online-bot-name> <exact-online-player-name>
.av sentiment reset all
```

`.av test` performs one lightweight, read-only global status check. It reports the loaded/enabled state, API and sanitized endpoint configuration, selected History backend and its already-known availability, personality and sentiment SQL/RAM state, cached and pending counts, loaded RAG file/entry counts, Environment and Snapshot switches, chat readiness, and the current worker count. It does not generate a reply, enqueue a synthetic worker job, create history, modify SQL rows, scan the world, or expose credentials. `.av live` remains the explicit generation-and-delivery test and intentionally does not change sentiment.

Personality `show`, `status`, `regenerate`, and single-bot `delete` require an exact online AI-controlled PlayerBot name so the command cannot attach identity data to an unrelated character. `status` reports the bounded last generation result for that bot in the current server session, including a sanitized provider, timeout, or JSON-validation error. Regeneration invalidates older queued work and asynchronously creates a replacement, but preserves the current usable personality until the replacement succeeds. Single and `delete all` operations affect only `azeroth_voices_bot_personality` plus its cache, bounded generation status, and pending personality jobs; conversation history, snapshots, Environment, RAG, character records, and PlayerBots data remain untouched. `delete all` is intentionally explicit and destructive.

Sentiment `inspect`, `set`, and pair `reset` require an exact online AI-controlled PlayerBot name plus an exact online real-player name. `set` rejects non-integers and values outside `-100…100`; it does not silently clamp moderator input. Pair `reset` deletes only that direction. The destructive `.av sentiment reset all` spelling is separate and explicit; it never removes personality, history, snapshot, character, or PlayerBots data.

All commands require the existing vMaNGOS moderator/GM security level and respond privately through the command handler. After editing the config, use the core's config reload command, which reloads Azeroth Voices automatically, or restart `mangosd`. `.av restart` only recycles the manager with settings already loaded by the core. `.av status` and `.av test` sanitize the endpoint and never display the API key.

## Debug diagnostics

`AzerothVoices.Debug = 1` enables a few concise activity messages, such as ambient chatter queueing, RAG match counts, and successful or discarded reply delivery. There are no debug levels, category framework, request tracing, prompt dumps, or performance profiling. Serious production failures still use normal worldserver error logging. Use `.av test` for a private in-game GM summary.

## Event adapter for other modules and scripted NPCs

The core does not expose generic hooks for every event from AzerothCore's `mod-ollama-chat` (for example arbitrary achievements, guild rank changes, or every game-event start). Another Tortoise module can submit such an event without modifying PlayerBots:

```cpp
#include "AzerothVoicesManager.h"

AzerothVoices::Manager::Instance().HandleEvent(player, "achievement", achievementName);
```

Supported adapter event names are `achievement`, `pet_defeated`, `used_object`,
`guild_promotion`, `guild_demotion`, `dungeon_completed`,
`game_event_started`, and `game_event_stopped`. Event-specific chances are
controlled in `mod-azeroth-voices.conf`. Guild promotion/demotion should be submitted while the
player still has the target guild, or with the optional fourth `guildId` argument. Guild-scoped
events require an online real player in that guild. Other events prioritize Party when the
subject and generated PlayerBot share the party subgroup, then fall back to Say; NPC event
reactions remain Say-only.

Likewise, direct chat still needs only a `Player*`, a scope, and text. This is the supported extension seam for quest scripts, dungeon modules, world events, and future scripted-NPC systems.

## Operational checks

At startup look for the current concise startup line plus the enabled storage lines:

```text
[AzerothVoices] Started with 8 workers, endpoint ..., model ...
[AzerothVoices][PERSONALITY][SQL] Persistent PlayerBot personality storage is available.
[AzerothVoices][SENTIMENT][SQL] Persistent PlayerBot-to-player sentiment storage is available.
```

Then run `.av status` and `.av test`. For a deliberate bot-delivery test, run `.av live BotName Reply exactly: API OK`. If the provider works but normal chat does not, verify the relevant `Replies.*` switch, chance, channel name and cooldown, and confirm that the target passes `Script_IsAIControlled`.

For the combat-start path, test from fully out of combat: attack one eligible neutral or hostile creature and confirm at most one opening Say line can be queued; attack a second creature before combat ends and confirm it cannot trigger; then leave combat fully and start a new fight after the configured cooldown to confirm the latch resets.

### Terminal message and API telemetry

These settings are independent of `AzerothVoices.Debug`. To log each generated
reply once and print a one-minute counters-only summary, use:

```ini
AzerothVoices.Console.GeneratedMessages = 1
AzerothVoices.Console.ApiCallStats = 1
AzerothVoices.Console.ApiCallStatsIntervalSeconds = 60
```

The summary reports actual HTTP attempts (including retries), successful and failed
final results, generated-message count, and aggregate preflight rejection reasons
for that interval. Telemetry never retains or replays generated text; only
`Console.GeneratedMessages` prints it. Generated output may expose whispers and
chat history in the worldserver terminal/logs, so it is disabled by default.

Do not disable certificate verification. If a private gateway uses a private CA, configure `AzerothVoices.CACertFile` with that CA bundle instead.
