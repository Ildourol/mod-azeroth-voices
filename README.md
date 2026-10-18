# Azeroth Voices

![Azeroth Voices](mod-azeroth-voices.jpg)

**Azeroth Voices** is an autonomous, native C++ dialogue and ambient conversation module for Turtle WoW / vMaNGOS. It brings natural personality, continuous memory, and organic responsiveness to PlayerBots and NPCs without external Python bridge scripts or database polling queues.

---

## Highlights

- **Native Zero-Bridge Engine**: Asynchronous in-process HTTP/HTTPS requests to any OpenAI-compatible or Gemini LLM endpoint.
- **Dynamic Dialogue**: Responds across Whisper, Say, Yell, Party, Raid, Guild, and World channels.
- **Persistent Personalities & Memory**: 3 distinct traits, speaking tones, background stories, and shared-adventure memories stored in SQL.
- **Player Message Coalescing**: Automatically combines rapid multi-line player messages (`"hi"`, `"ready?"`) into a single prompt turn.
- **Ambient World & Proximity**: Organic NPC Say chatter, pre-aggro boss dialogue, and natural conversation flow.
- **Companion In-Game Addon**: Client UI for managing bot contacts, viewing traits, and triggering personality regenerations.

---

## Documentation & Operator Guides

> [!TIP]
> **Complete Reference**: For an exhaustive, setting-by-setting breakdown of all 401 configuration parameters, see the **[Configuration & Tuning Guide](docs/CONFIGURATION_GUIDE.md)**.

- **[Configuration & Tuning Guide](docs/CONFIGURATION_GUIDE.md)**: Every setting explained with types, defaults, and tuning advice.
- **[Quieter World Preset](conf/presets/mod-azeroth-voices-quieter.conf.dist)**: Low-frequency configuration template for quiet servers.

---

## Installation & Quick Start

### 1. Add to Server Source
Clone or place the module in your server source:
```bash
cd /path/to/server/source/modules
git clone https://github.com/Ildourol/mod-azeroth-voices.git
```

### 2. Configure CMake & Build
Reconfigure CMake with `BUILD_PLAYERBOTS=ON` and build:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PLAYERBOTS=ON -DMODULE_MOD_AZEROTH_VOICES=static
cmake --build build --target mangosd --parallel $(nproc)
```

### 3. Initialize Persistent Database
Import all character migrations in order from `data/sql/character/` into your **characters** database:
- `2026_03_30_01_characters_azeroth_voices_history.sql`
- `2026_03_30_02_characters_azeroth_voices_snapshots.sql`
- `2026_03_30_03_characters_azeroth_voices_personalities.sql`
- `2026_03_30_04_characters_azeroth_voices_sentiment.sql`
- `2026_03_30_05_characters_azeroth_voices_addon_contacts.sql`
- `2026_03_30_06_characters_azeroth_voices_memories.sql`

### 4. Enable Configuration
Copy the template to your active modules configuration:
```bash
cp etc/modules/mod-azeroth-voices.conf.dist etc/modules/mod-azeroth-voices.conf
```
Edit `mod-azeroth-voices.conf` and set your provider credentials:
```ini
[worldserver]
AzerothVoices.Enable = 1
AiPlayerbot.LLMApiEndpoint = https://api.openai.com/v1/chat/completions
AiPlayerbot.LLMApiKey = env:OPENAI_API_KEY
AzerothVoices.Model = gpt-4o-mini
```

---

## Companion Addon

Copy `client/AzerothVoices/` into your client's `Interface/AddOns/` directory:
```text
Interface/AddOns/AzerothVoices/
```
In-game, toggle the management UI using `/azerothvoices` or `/avvoices`.

---

## In-Game Moderator Commands

| Command | Purpose |
| :--- | :--- |
| `.av status` | Displays loaded state, worker pool, and active queues |
| `.av test` | Performs a read-only configuration and provider connectivity check |
| `.av reload` / `.av restart` | Reloads configuration or reinitializes the module |
| `.av pause` / `.av resume` | Temporarily halts or resumes dialogue processing |
| `.av live <bot-name> <message>` | Triggers a direct response from an online PlayerBot |
| `.av chatter [topic]` | Manually triggers an ambient conversation pass |
| `.av personality show <bot>` | Inspects a bot's traits, tone, and backstory |
| `.av memory inspect <bot> <player>` | Inspects recorded memories for a player/bot pair |
| `.av memory forget <bot> <player>` | Clears recorded memories for a specific pair |
| `.av clearhistory` | Clears active conversation and snapshot history |

---

## License & Contributing

Built for Turtle WoW and vMaNGOS communities. PRs, improvements, and feedback are welcome!