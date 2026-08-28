# Azeroth Voices

Current development milestone: **V0.2**.

`mod-azeroth-voices` is a standalone, provider-neutral conversation and ambient-chatter module for TortoiseWoW. It observes PlayerBots through the core's generic AI-control hook, so it does not modify anything under `src/modules/PlayerBots`.

It is inspired by `mod-ollama-chat`, but it does not copy its detached-thread model, Ollama-only request format, personality system, sentiment system, or disabled TLS verification. The module uses the HTTP and JSON libraries already shipped by TortoiseWoW (`cpp-httplib` and `nlohmann/json`) with OpenSSL certificate verification enabled.

The local `mod-ollama-chat-main` tree was treated strictly as a feature reference. All registration, script hooks, chat delivery, object lookup, configuration loading, CMake discovery, and PlayerBot detection in this module use the APIs and names present in the local `tortoise-wow-playerbots-integration-gh` vMaNGOS fork. No AzerothCore headers, script loaders, configuration sections, database APIs, or playerbot classes are imported.

## Included features

- Player-to-bot replies in whisper, say, yell, party, raid, guild, officer, world, and optionally custom channels.
- Bot-to-bot replies with separate probability controls.
- Nearby normal NPC replies to direct targeting, name mentions, and overheard say/yell chat.
- Random nearby, guild, and world chatter while real players are online.
- A follow-up chance for short bot/NPC conversations; lone NPCs do not start ambient monologues.
- Event chatter for deaths, kills, loot, quests, learned spells, duels, levels, guild login/join/leave, plus a public event-adapter method for other scripts.
- Guild-scoped reactions for guild activity, levels, achievements, dungeon completions, and rare/epic loot when the subject is guilded; game-event adapters can announce in world chat.
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
- Typing delay compatible with the restored PlayerBots behavior: generation time can be subtracted from the character-based delay.
- Optional terminal telemetry for per-message details and periodic API-call/result summaries with a bounded recent-message sample.
- GM commands for status, one lightweight global sanity check, explicit generation and ambient tests, pause/resume, restart, and history clearing.

There are intentionally no per-bot personalities and no sentiment tracking. `AzerothVoices.GlobalPrompt` is the one shared style/personality prompt for every PlayerBot and NPC.

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

Workers never retain or manipulate `Player`, `Creature`, `Unit`, `Guild`, `Group`, or `WorldSession` pointers. All object lookup and chat delivery happens after map updates in the world update hook.

## Dependencies

No Python, Ollama daemon, vector database, or extra HTTP/JSON package is needed. Database history uses two module-owned tables in the existing character database. Use the normal TortoiseWoW build dependencies:

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
- blocked channels and bot-to-bot/RPG chat chances.

For the safer built-in JSON path, leave `AiPlayerbot.LLMApiJson` blank and keep `AzerothVoices.ParserMode = ProviderJson`. `AiPlayerbot.LLMDefaultPromptsFile` is intentionally not loaded because per-character personalities were explicitly excluded.

## Database

`AzerothVoices.History.StorageMode` and `AzerothVoices.Snapshot.StorageMode` accept:

- `0`: no older records and no SQL writes. For Snapshot, this does not disable the current rich snapshot; `AzerothVoices.Snapshot.Enable` controls that.
- `1`: bounded RAM only; data is lost on worldserver restart.
- `2`: persistent character-database storage with a bounded lazy-loaded RAM hot cache; conversation history defaults to 2 while older snapshot storage defaults to 0.

The schema is [data/sql/Char/20260827_01_azeroth_voices_history.sql](data/sql/Char/20260827_01_azeroth_voices_history.sql). TortoiseWoW's database updater discovers enabled-module migrations under `data/sql/Char` when `Database.AutoUpdate.Enabled = 1` and the module is allowed by `Database.AutoUpdate.AllowedModules` (the normal default is `all`). Look for the module migration in the database-updater startup log.

If automatic updates are disabled, import it manually into the same character database named by `mangosd.conf`:

```bash
cd /root/TWoWServerBots/Source/tortoise-wow
CHARACTER_DB=mangoscharacters
mysql -u root -p "$CHARACTER_DB" < \
  modules/mod-azeroth-voices/data/sql/Char/20260827_01_azeroth_voices_history.sql
```

Change `mangoscharacters` to the database name from your `CharacterDatabaseInfo` setting. The migration creates only `azeroth_voices_chat_history` and `azeroth_voices_environment_history`. The second table name is retained for V0.1 database compatibility but now stores optional Azeroth Voices Snapshot records. If a feature selects mode 2 but its own table is missing, the module logs one clear startup error and only that feature safely falls back to bounded RAM for that run.

Writes are queued in small transactions and executed through vMaNGOS's asynchronous database queue. Reads happen lazily once per hot-cache key instead of once per message. Per-key row limits and TTL cleanup keep SQL bounded. `.av clearhistory` clears RAM, surrounding chat, and only these two module-owned SQL tables.

Persistent mode stores player message and generated reply text, including whispers. It never stores API keys. Treat the character database and backups as private data; use mode 0 or 1 if conversation retention is inappropriate.

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
.azerothvoices test
```

`.azerothvoices test` performs one lightweight, read-only global status check. It reports the loaded/enabled state, API and sanitized endpoint configuration, selected History backend and its already-known availability, loaded RAG file/entry counts, Environment and Snapshot switches, chat readiness, and the current worker count. It does not generate a reply, enqueue a synthetic worker job, create history, modify SQL rows, scan the world, or expose credentials. `.av live` remains the explicit generation-and-delivery test.

All commands require the existing vMaNGOS moderator/GM security level. After editing the config, use the core's config reload command and then `.av restart`, or restart `mangosd`. `.av status` and `.azerothvoices test` sanitize the endpoint and never display the API key.

## Debug diagnostics

`AzerothVoices.Debug = 1` enables a few concise activity messages, such as ambient chatter queueing, RAG match counts, and successful or discarded reply delivery. There are no debug levels, category framework, request tracing, prompt dumps, or performance profiling. Serious production failures still use normal worldserver error logging. Use `.azerothvoices test` for a private in-game GM summary.

## Event adapter for other modules and scripted NPCs

The core does not expose generic hooks for every event from AzerothCore's `mod-ollama-chat` (for example arbitrary achievements, guild rank changes, or every game-event start). Another Tortoise module can submit such an event without modifying PlayerBots:

```cpp
#include "AzerothVoicesManager.h"

AzerothVoices::Manager::Instance().HandleEvent(player, "achievement", achievementName);
```

Supported adapter event names are `achievement`, `pet_defeated`, `used_object`,
`guild_promotion`, `guild_demotion`, `dungeon_completed`,
`game_event_started`, and `game_event_stopped`. The event-specific chance and
scope are controlled in `mod-azeroth-voices.conf`.

Likewise, direct chat still needs only a `Player*`, a scope, and text. This is the supported extension seam for quest scripts, dungeon modules, world events, and future scripted-NPC systems.

## Operational checks

At startup look for:

```text
[AzerothVoices][INIT] Azeroth Voices initialized: API=enabled, endpoint=..., model=..., history=SQL, ...
```

Then run `.av status` and `.azerothvoices test`. For a deliberate bot-delivery test, run `.av live BotName Reply exactly: API OK`. If the provider works but normal chat does not, verify the relevant `Replies.*` switch, chance, channel name and cooldown, and confirm that the target passes `Script_IsAIControlled`.

### Terminal message and API telemetry

These settings are independent of `AzerothVoices.Debug`. To log each generated
reply immediately and also print a one-minute summary containing the five most
recent generated messages, use:

```ini
AzerothVoices.Console.GeneratedMessages = 1
AzerothVoices.Console.ApiCallStats = 1
AzerothVoices.Console.ApiCallStatsIntervalSeconds = 60
AzerothVoices.Console.RecentMessages = 5
```

The summary reports actual HTTP attempts (including retries), successful and failed
final results, and generated-message count for that interval. Set
`Console.RecentMessages = 0` to keep the counts without retaining or repeating
message text. Both text-producing options may expose whispers and chat history
in the worldserver terminal/logs, so they are disabled by default.

Do not disable certificate verification. If a private gateway uses a private CA, configure `AzerothVoices.CACertFile` with that CA bundle instead.
