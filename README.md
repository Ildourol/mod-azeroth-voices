# Azeroth Voices

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
- Bounded in-memory conversation history with expiry; no database is required.
- Optional lightweight file-based knowledge retrieval.
- Typing delay compatible with the restored PlayerBots behavior: generation time can be subtracted from the character-based delay.
- GM commands for status, test requests, ambient tests, pause/resume, restart, and history clearing.

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

No Python, Ollama daemon, new database, or extra HTTP/JSON package is needed. Use the normal TortoiseWoW build dependencies:

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

No SQL import is needed. History is held in memory, bounded by turn count and character count, and expires automatically. It is intentionally lost on restart; this avoids schema migrations, privacy retention, and database traffic from thousands of bots.

The optional knowledge file is also file based. To enable its example:

```bash
cp modules/mod-azeroth-voices/data/azeroth_voices_knowledge.txt.dist \
  /root/TWoWServerBots/Server/modules/azeroth_voices_knowledge.txt
```

Then set `AzerothVoices.Knowledge.Enable = 1`.

## GM commands

```text
.av status
.av pause
.av resume
.av restart
.av clearhistory
.av chatter [optional topic]
.av test <exact-bot-name> [optional prompt]
.av test - [optional prompt]       # choose a nearby eligible actor
```

After editing the config, use the core's config reload command and then `.av restart`, or restart `mangosd`. `.av status` never displays the API key.

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
[AzerothVoices] Started with 8 workers, endpoint ..., model ...
```

Then run `.av status` and `.av test BotName Reply exactly: API OK`. If the provider works but normal chat does not, verify the relevant `Replies.*` switch, chance, channel name, cooldown, and that the target is recognized by `Script_IsAIControlled`.

Do not disable certificate verification. If a private gateway uses a private CA, configure `AzerothVoices.CACertFile` with that CA bundle instead.
