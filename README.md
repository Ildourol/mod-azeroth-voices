# Azeroth Voices

![Azeroth Voices](mod-azeroth-voices.jpg)

Azeroth Voices adds generated dialogue and ambient conversations to a custom Turtle WoW-compatible Tortoise/vMaNGOS server. The module uses native C++ provider requests and world-thread delivery to let supported PlayerBots and NPCs respond to players, react to events, and participate in configurable world chatter. It does not require a separate Python bridge.

This is a community server module, not an official Turtle WoW service or client feature.

## Features

- **Direct dialogue:** configurable responses in whisper, say, yell, party, raid, guild, and world/custom-channel contexts, subject to configured gates and speaker eligibility.
- **Character personalities:** configurable personality traits, tone, and backstory for PlayerBots, backed by SQL persistence. Features a dual-layer lightweight architecture: an active voice layer (Traits + Tone + Priority Rule, ~25 tokens) always present, paired with context-gated backstory sampling (~60 tokens in `<backstory>` tags with soft negative steering) and guaranteed 100% backstory inclusion on direct inquiries.
- **Conversation context:** bounded history, snapshots, player sentiment, and a deterministic memory ledger for more consistent interactions.
- **Ambient dialogue:** optional nearby, proximity, guild, world-channel, party, and raid chatter with separate probabilities, cooldowns, and pacing.
- **Event reactions:** contextual lines for supported combat, group, quest, loot, and environmental events.
- **NPC and encounter dialogue:** supported NPC interactions and configurable dungeon/raid pre-aggro boss speech.
- **In-process provider client:** asynchronous requests to supported Chat Completions endpoints, with a separate OpenAI Responses mode, configurable timeouts, retries, workers, and token limits.
- **Optional client addon:** contact and personality management interface for the compatible 1.12.1-based client. (feat(personality): lightweight dual-layer persona architecture and backstory gating)

Feature activity depends on the configuration, available bots/NPCs, database setup, client support, and the server build.

## Requirements

- A compatible Tortoise/vMaNGOS-derived custom server with the required module integration points and PlayerBot support.
- A C++ build environment for the target server and its module dependencies.
- Access to the server's characters database to install the module's persistent tables.
- A supported LLM HTTP endpoint and any credentials it requires.
- The optional `client/AzerothVoices` addon if you want the in-game companion interface.

Verify the exact branch, build flags, and module configuration deployment paths for your server; paths and flags can vary across Tortoise forks.

## Installation

### 1. Install module source

Place this repository in the target server's `modules/` directory:

```bash
cd /path/to/server/source/modules
git clone https://github.com/Ildourol/mod-azeroth-voices.git
```

### 2. Build for your target

Enable the module and PlayerBots in your target's CMake configuration as supported by that source tree. A typical configuration uses `BUILD_PLAYERBOTS=ON` and `MODULE_MOD_AZEROTH_VOICES=static`, but confirm these options against your installed module loader and build scripts. Reconfigure and compile the server using your normal build procedure; rebuild after module source changes.

### 3. Initialize persistent storage

Review and import the applicable files in `data/sql/character/` into the **characters** database, in filename order, after backing it up. Reconcile the files with any configured automatic database updater so that each migration is applied exactly once. Do not import these files into the world or logon database.

### 4. Configure the server

Use `conf/mod-azeroth-voices.conf.dist` as the configuration source. Copy it to the active **module configuration location of your target server** (the installed runtime path may differ from this repository's `conf/` directory). Keep the `[worldserver]` section and edit the keys in the resulting live configuration:

```ini
[worldserver]
AzerothVoices.Enable = 1
AzerothVoices.ProviderMode = ChatCompletions
AiPlayerbot.LLMApiEndpoint = https://api.openai.com/v1/chat/completions
AiPlayerbot.LLMApiKey = env:OPENAI_API_KEY
AzerothVoices.Model = gpt-4o-mini
```

The example above mirrors the template's provider defaults, not a universal recommendation for model choice. Set an environment variable accessible to the server process or use another supported credential configuration. Avoid committing credentials or including them in diagnostic logs. Remote plain HTTP is rejected; local HTTP has its own setting.

### 5. Start and verify

Start the updated server through your regular process. Check startup diagnostics, database initialization, and provider connectivity. Use `.av status` and `.av test` where supported, then send a controlled in-game message before enabling higher-volume ambient behavior. Monitor API usage and request failures as you adjust the limits.

## Configuration

The complete, setting-by-setting reference is the [Configuration and Tuning Guide](docs/CONFIGURATION_GUIDE.md). The authoritative defaults are the actual assignments in [`conf/mod-azeroth-voices.conf.dist`](conf/mod-azeroth-voices.conf.dist).

| Setting | Template default | Purpose |
| --- | --- | --- |
| `AzerothVoices.Enable` | `1` | Module master switch |
| `AzerothVoices.ProviderMode` | `ChatCompletions` | Provider request format |
| `AzerothVoices.Model` | `gpt-4o-mini` | Model identifier |
| `AzerothVoices.MaxTokens` | `100` | Output token limit |
| `AzerothVoices.Thinking.Mode` | `Auto` | Reasoning-control policy |
| `AzerothVoices.Thinking.TokenReserve` | `1024` | Conditional reasoning token headroom |
| `AzerothVoices.WorkerThreads` | `8` | Background request worker count |
| `AzerothVoices.QueueMaximum` | `128` | Queued request capacity |
| `AzerothVoices.GlobalRequestsPerMinute` | `60` | Shared request rate cap |
| `AzerothVoices.GlobalMode` | `Normal` | Default PlayerBot dialogue style |

In `Thinking.Mode=Auto`, reasoning controls are conditional on the recognized official provider/model and `AzerothVoices.Thinking.AutoKinds` (default: `PersonalityGeneration,EventChat`). Some reasoning models cannot fully disable reasoning. For conversational latency and cost, consider output limits, request rates, the selected provider, and ambient trigger frequency together.

The template includes per-feature enable switches, chances, distances, cooldowns, message length limits, SQL retention settings, and logging controls. `AzerothVoices.Console.GeneratedMessages=1` may log private dialogue; leave it disabled unless needed for a carefully controlled investigation.

## Client addon

Copy the directory `client/AzerothVoices/` into the game's `Interface/AddOns/` directory without changing its folder name:

```text
Interface/AddOns/AzerothVoices/
```

On a compatible client, open the companion interface with `/azerothvoices` or `/avvoices`. The server module remains responsible for dialogue generation; the addon is not a substitute for the server-side module or provider access.

## Administration commands

| Command | Action |
| --- | --- |
| `.av status` | Inspect module state and queues |
| `.av test` | Check module/provider configuration |
| `.av reload` / `.av restart` | Reload or reinitialize supported module state |
| `.av pause` / `.av resume` | Temporarily stop or resume generation |
| `.av live <bot-name> <message>` | Request a direct bot response |
| `.av chatter [topic]` | Trigger ambient dialogue |
| `.av personality show <bot>` | Inspect a bot's identity |
| `.av memory inspect <bot> <player>` | Inspect recorded memories |
| `.av memory forget <bot> <player>` | Remove a specific memory relationship |
| `.av clearhistory` | Clear stored conversation/snapshot history |

Command availability and permissions depend on the compiled module revision. Destructive memory/history commands should be used deliberately.

## Maintenance and troubleshooting

If responses stop, check the master switch, relevant feature gate, bot/NPC eligibility, API credentials, endpoint mode, TLS policy, request timeouts, token budget, queue/rate limits, and server logs. A successful provider request does not guarantee a deliverable line when the response is empty, filtered, or out of context.

After updating the module, compare the shipped `.conf.dist` with your live configuration before applying changes. Do not overwrite customized values blindly. Review migration ordering and make a database backup before schema changes. For a rollback, restore the prior compatible module binary/configuration and, where necessary, the matching database backup.

## Contributing

Issue reports and pull requests are welcome. Include the server revision, module commit, relevant configuration keys (without secrets), reproducible steps, and logs with private messages and credentials removed.
