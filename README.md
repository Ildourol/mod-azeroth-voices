# Azeroth Voices

![Azeroth Voices](mod-azeroth-voices.jpg)

**Azeroth Voices** is an immersive dialogue and ambient conversation module for Turtle WoW server environments. It brings greater personality, continuity, and responsiveness to PlayerBots and eligible non-player characters, making the world feel more inhabited during exploration, group play, social interaction, and combat.

The module supports player-directed conversations, spontaneous discussions, location-aware remarks, dungeon and raid dialogue, and contextual reactions to significant gameplay events. It is designed for configurable, paced in-game communication rather than constant chat output: eligible speakers, listeners, channels, distance, cooldowns, and current game conditions are considered before a line is delivered.

Azeroth Voices also provides persistent character identities, optional relationship-aware dialogue, encounter memories, and a companion in-game interface for reviewing and customizing known bot personalities.

## Contents

- [Description](#description)
- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Configuration](#configuration)
- [Companion addon](#companion-addon)
- [Gameplay behavior](#gameplay-behavior)
- [Data and persistence](#data-and-persistence)
- [Administration](#administration)
- [Troubleshooting](#troubleshooting)

## Description

Azeroth Voices expands everyday interaction throughout Azeroth by allowing the characters around a player to participate in a broader range of believable conversations. A PlayerBot can respond to a question in a whisper, make a remark while traveling with a party, acknowledge a memorable victory, or contribute to guild discussion. Nearby inhabitants can react to direct speech and occasionally exchange short conversations with one another, while dungeon and raid bosses can address approaching adventurers before combat begins.

Dialogue reflects the situation in which it occurs. Depending on the feature and available context, a character may take account of the current zone or subzone, nearby creatures, group membership, combat conditions, equipped equipment, quests, or previous exchanges. This helps keep responses relevant to what players are doing rather than treating every message as an isolated conversation.

A central part of the experience is continuity. PlayerBots can maintain a consistent personality built around three distinct traits, a speaking style, and a background story. Optional familiarity and memory systems allow interactions to develop across sessions: a character can recognize a returning player, refer to a shared dungeon clear, or respond with a more familiar or reserved tone based on previous direct conversations. Those systems are kept distinct so that personality, relationship tone, chat history, and verified gameplay memories can each be configured independently.

The module is also designed to respect the rhythm of a multiplayer world. Background conversations have probability and pacing controls; public chat can be spaced out to avoid message floods; and speech is checked against proximity, visibility, encounter state, and the presence of real players. Dungeon chatter is kept distinct from boss dialogue, and related event handlers are coordinated to avoid duplicate reactions to the same activity. Server operators can use the standard configuration file to tune the experience from occasional atmosphere to more frequent roleplay-focused interaction.

## Features

### Natural player interaction

- **Multiple communication channels:** PlayerBots can reply through whisper, say, yell, party, raid, guild, officer, world, and optionally configured custom channels.
- **Direct conversation:** Players can address nearby characters, selected targets, or known bots, with eligible responses routed through the appropriate channel.
- **Conversation follow-ups:** Short exchanges can continue between characters when the participants and intended audience remain valid.
- **Context-aware responses:** Available world and character information helps speakers refer to relevant locations, group situations, combat events, and nearby activity.

### Ambient world dialogue

- **Proximity chatter:** Eligible nearby NPCs can speak individually or participate in short, paced conversations observed by real players.
- **Targeted NPC replies:** Player `/say` messages prioritize a matching named NPC, followed by the selected target or an existing nearby conversation participant.
- **Environmental safeguards:** NPC eligibility includes creature type, ownership, alive/combat state, line of sight, range, and instance checks.
- **Varied conversation frequency:** Separate outdoor and instance settings, per-speaker cooldowns, and zone fatigue reduce excessive repetition.
- **Combat-opening reactions:** An eligible creature may react when a real player initiates combat, subject to one-shot and cooldown restrictions.

### Dungeon, raid, and group experiences

- **Boss and miniboss dialogue:** Supported dungeon and raid enemies can deliver pre-combat lines while players approach, without changing movement, threat, or combat behavior.
- **Party chatter:** PlayerBots can comment on travel, dungeon entry, zone changes, kills, deaths, resurrections, loot, quests, resources, and other group events.
- **Raid atmosphere:** Encounter-related battle cries, morale remarks, and raid-specific idle dialogue provide additional group flavor.
- **Coordinated reactions:** Shared gameplay events are assigned to an appropriate dialogue path to limit duplicate messages.
- **Audience awareness:** Group conversations require eligible real-player listeners and do not run as unattended bot-only conversations.

### Social identity and continuity

- **Persistent personalities:** Each PlayerBot can have three stable personality traits, a speaking tone, and a background story.
- **Configurable character style:** Backgrounds can present an in-world adventurer or a fictional player persona.
- **Guild conversations:** Bots can greet members, reference guild activity, and participate in paced conversations based on available guild context.
- **Optional sentiment:** Direct interactions may influence how familiar, warm, distant, or hostile an individual bot sounds toward a particular player.
- **Shared-adventure memories:** Bots can retain bounded records of verified meetings, group activity, quest completions, dungeon clears, boss kills, wipes, and level milestones.
- **Conversation history:** Recent exchanges can be retained in memory or in the character database, subject to configured storage and retention limits.

### Server controls and reliability

- **Fine-grained settings** for channel participation, speaking probability, delays, conversation length, eligibility, and enabled subsystems.
- **Paced public conversations** to reduce collisions and excessive chatter in shared channels.
- **Bounded processing** with worker scheduling, timeouts, retries, and queue controls.
- **Persistent or memory-only storage**, depending on feature and configuration.
- **In-game moderator commands** for status checks, pausing activity, controlled testing, and managing stored records.
- **Optional diagnostic output** for investigating delivery and configuration issues.

## Requirements

- A compatible Turtle WoW server source and build environment with PlayerBots enabled.
- CMake and the build dependencies required by the server.
- Access to the server's configuration directory and character database when using persistent features.
- A configured dialogue service endpoint and its required credentials, if applicable.
- For the optional companion interface: a Turtle WoW 1.18.1-compatible client using Vanilla Interface `11200`.

> The server module and the companion addon are separate installation steps. The addon is optional for ordinary in-game dialogue, but required for its graphical personality-management interface.

## Installation

### 1. Add the module to the server source

From the server source directory, place this repository at:

```text
modules/mod-azeroth-voices/
```

For example:

```bash
cd /path/to/server/source
mkdir -p modules
git clone https://github.com/Ildourol/mod-azeroth-voices.git modules/mod-azeroth-voices
```

### 2. Configure and build

Reconfigure CMake so it discovers the module. The following Linux example uses a static build and assumes the server's other build dependencies are already installed:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/server/install \
  -DBUILD_PLAYERBOTS=ON \
  -DALLOW_TURTLE_ADDONS=ON \
  -DUSE_EXTRACTORS=OFF \
  -DMODULE_MOD_AZEROTH_VOICES=static

cmake --build build --target mangosd --parallel "$(nproc)"
cmake --install build
```

Adjust the install prefix and other CMake options to match your existing server build. On Windows, select the corresponding module option in your CMake configuration and build the worldserver in Release mode.

### 3. Enable the configuration

Locate the installed configuration template, normally at:

```text
<server-install>/etc/modules/mod-azeroth-voices.conf.dist
```

Copy it to the active configuration file:

```bash
cd /path/to/server/install/etc/modules
cp mod-azeroth-voices.conf.dist mod-azeroth-voices.conf
```

Edit `mod-azeroth-voices.conf`, keep its `[worldserver]` section header, and enable the module:

```ini
[worldserver]
AzerothVoices.Enable = 1
```

Complete the endpoint, access credentials, and service settings documented in the distributed configuration template. Store sensitive credentials in protected environment variables where supported rather than committing them to source control.

### 4. Initialize persistent data

The module includes character-database migrations in:

```text
data/sql/character/
```

When the server's database auto-updater is enabled and permitted to process installed module migrations, the required tables can be applied during startup. Otherwise, review and import the SQL migrations from that directory into the configured **character database**, in filename order, before enabling persistent features.

The database additions cover conversation history, snapshots, personalities, sentiment, addon contacts, and shared-adventure memories. Some features can continue with bounded in-memory storage if their tables are unavailable, but persistence across restarts requires the appropriate tables.

### 5. Start and verify

Restart the worldserver after completing installation and configuration. Review its startup logs for Azeroth Voices initialization and any configuration or database errors. With a moderator account, run:

```text
.av status
.av test
```

These commands provide a quick check of the module's loaded state, configuration, and available subsystems. Use `.av live <bot-name> <message>` when you want to test a deliberate in-game response from an eligible online PlayerBot.

## Configuration

The main settings file is `etc/modules/mod-azeroth-voices.conf`. The included `.conf.dist` template is the authoritative reference for supported options, defaults, and valid values.

| Area | What it controls |
| --- | --- |
| General | Master enable switch, debugging, and overall dialogue behavior |
| Replies and channels | Where characters can respond and which audiences are eligible |
| Proximity | Outdoor/instance NPC chatter, speaker selection, scene length, and cooldowns |
| Boss dialogue | Eligible encounters, detection distances, speaking frequency, and repeat behavior |
| Group and raid chatter | Party and raid events, reaction chances, and conversational pacing |
| Guild chatter | Ambient guild exchanges, greetings, and reply frequency |
| Personality | Persistent traits, speaking tone, background style, and regeneration |
| Sentiment | Optional bot-to-player relationship tone and gradual decay |
| Memory | Verified shared events, retention, and recall behavior |
| History and snapshot | Conversation persistence and optional extended character context |
| RAG | Optional local structured game knowledge |
| Console | Diagnostic messages and operational statistics |

A few commonly useful switches are:

```ini
AzerothVoices.Enable = 1
AzerothVoices.Personality.Enable = 1
AzerothVoices.GroupChatter.Enable = 1
AzerothVoices.GuildChatter.Enable = 1
AzerothVoices.Memory.Enable = 1
AzerothVoices.Sentiment.Enable = 0
```

These are illustrative settings, not a replacement for the complete configuration. The optional sentiment feature is disabled by default. After editing the active file, use the server's configuration reload facility or restart the worldserver; `.av restart` reinitializes the module using settings already loaded by the server.

For a quieter world, the repository also includes [`conf/presets/mod-azeroth-voices-quieter.conf.dist`](conf/presets/mod-azeroth-voices-quieter.conf.dist). Copy the settings you want into the active configuration; the preset file is not automatically loaded.

## Companion addon

The optional **AzerothVoices** client addon provides a movable in-game window for managing known PlayerBots. Its main functions include viewing a contact list, inspecting the three personality traits and background, replacing traits, regenerating a background story, and forgetting a bot's stored contact and interaction data for the current player.

To install it, copy:

```text
client/AzerothVoices/
```

into the game client's addon directory as:

```text
Interface/AddOns/AzerothVoices/
```

Enable the addon on the character-selection addon screen. Once logged in, open its interface with:

```text
/azerothvoices
```

The shorter `/avvoices` command is also available. A bot enters the addon contact list after the player has actually exchanged delivered dialogue with that bot. The addon does not require moderator privileges for its normal interface actions.

## Gameplay behavior

### Speaking to characters

Players can use their normal chat channels to address eligible PlayerBots. Nearby NPC dialogue uses `/say` and considers the player’s addressed name, selected creature, and ongoing local conversation. If multiple creatures have ambiguous names, the system avoids choosing an arbitrary match. Not every creature is allowed to speak: temporary creatures, controlled units, certain service actors, and other excluded categories are filtered out.

### Ambient conversations

Outdoor chatter is intentionally occasional. Dungeon and raid proximity scenes can be more frequent, with independent controls for scan intervals and trigger chances. Each scene is limited by speaker eligibility, audience distance, line of sight, timing, and conversation length. A zone-fatigue mechanism further limits repetitive activity in areas where several scenes have recently occurred.

### Boss encounters

Dungeon and raid boss dialogue operates before combat, when a qualifying boss is visible but still outside its attack distance with an additional safety margin. Lines are delivered as encounter speech without forcing movement or combat. Once the encounter begins, ordinary boss pre-combat dialogue does not continue through this path.

### Group and guild communication

When a real player groups with PlayerBots, the bots may react to noteworthy travel and combat moments, loot, quests, or recovery after a wipe. Raid-specific behavior adds encounter atmosphere while still respecting chance and cooldown settings. Guild dialogue can include greetings and occasional member-aware conversation without overwhelming guild chat. Automated public messages use additional pacing rules, while direct player replies retain their own responsiveness.

## Data and persistence

Azeroth Voices separates different kinds of information to keep behavior predictable and storage manageable:

- **Personality:** Stable bot-specific traits, speaking tone, and background.
- **History:** Previous delivered dialogue, retained according to configured limits.
- **Environment and snapshots:** Current world context and, optionally, stored observations of PlayerBot state.
- **Sentiment:** Optional directional familiarity between an individual PlayerBot and a real player, with bounded changes and gradual inactivity decay.
- **Memory:** Compact records derived from confirmed gameplay events rather than unrestricted player-written notes.
- **Contacts:** The set of bots a player has actually interacted with, used by the companion addon.

Persistent features use the character database when their tables are available and configured. Other storage modes can disable older records or keep bounded session-only data in RAM. Stored memories and contact data can be cleared through the provided controls without automatically deleting a bot's global personality.

## Administration

Moderators can inspect and manage Azeroth Voices through in-game commands. Common operations include:

| Command | Purpose |
| --- | --- |
| `.av status` | Show current module state |
| `.av test` | Run a lightweight configuration and subsystem check |
| `.av pause` / `.av resume` | Temporarily stop or resume dialogue activity |
| `.av restart` | Reinitialize the module with the loaded settings |
| `.av chatter [topic]` | Trigger an ambient conversation test |
| `.av live <bot-name> [message]` | Test a response from an eligible online bot |
| `.av clearhistory` | Clear conversation and snapshot history |
| `.av personality show <bot-name>` | Inspect an online bot's personality |
| `.av personality regenerate <bot-name>` | Regenerate an online bot's personality |
| `.av sentiment inspect <bot-name> <player-name>` | Inspect an optional bot-to-player relationship |
| `.av memory inspect <bot-name> <player-name>` | Inspect stored shared-adventure memories |
| `.av memory forget <bot-name> <player-name>` | Remove memories for a specific bot/player pair |

Additional management commands are available for resetting personalities, sentiment, and memories. Some are destructive, including their explicit `all` variants; verify the intended scope before running them. Administrative commands require the server's applicable moderator permissions.

## Troubleshooting

**The module does not start.** Verify that its build option was enabled, the worldserver was rebuilt and installed, and `AzerothVoices.Enable = 1` appears under `[worldserver]` in the active module configuration. Review startup logs and run `.av status`.

**Characters do not respond.** Check the configured dialogue service, credentials, reply switches, channel restrictions, and cooldowns. The intended speaker must be eligible and the audience must pass the relevant checks. `.av test` provides a read-only configuration check; `.av live` helps test deliberate delivery.

**Nearby NPCs remain silent.** Confirm the proximity subsystem is enabled and that the player is within the configured audible distance. Creature type, line of sight, combat state, speaker exclusions, scene probability, and instance restrictions can all suppress a line.

**Personality, history, or memories disappear after a restart.** Verify that the relevant character-database migrations were applied and the chosen storage mode supports persistence. Memory-only data is not expected to survive a worldserver restart.

**The companion addon shows no contacts.** Ensure the addon is installed and enabled, that the player has exchanged dialogue with an eligible bot, and that the addon-contact database table is available.

**There is too much chatter.** Lower the relevant proximity and group-chat probabilities, increase scan intervals and cooldowns, or apply the quieter configuration preset. Keep public-channel pacing enabled for a more restrained experience.

---

Azeroth Voices is intended to make exploration and social play feel more reactive while leaving server operators in control of where, when, and how often the world speaks.