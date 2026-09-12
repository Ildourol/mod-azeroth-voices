# Azeroth Voices Feature Import Plan

## Summary

Port the requested `mod-llm-chatter` behavior natively into `mod-azeroth-voices` using its existing C++ workers, SQL storage, RAG, scheduling, and Turtle/vMaNGOS APIs. No Python bridge, AzerothCore code, or PlayerBots modifications will be introduced.

The stock Chatter Companion addon will remain unchanged and work through server-side `.llmc`/`CHATTER_ADDON` compatibility. Personality traits become compile-time fixed at three; `AzerothVoices.Personality.GenerateTone` remains configurable with default `1`.

## Implementation Changes

### Personality and Chatter addon

- Remove `AzerothVoices.Personality.TraitCount` from configuration and hardcode exactly three traits via a named constant.
- Keep `GenerateTone` configurable with default `1`; generation, validation, persistence, placeholders, and documentation must all assume exactly three traits.
- Treat existing personality rows with a different trait count as stale so they are lazily regenerated.
- Add addon-compatible personality generation modes for replacing three supplied traits and regenerating only the background story.
- Implement user-level `.llmc roster/get/set/regenbackstory/forget` handling and `CHATTER_ADDON` system responses without GM privileges.
- Match the stock wire protocol: percent encoding, `-` empty sentinel, `ROSTER_BEGIN/ROSTER/ROSTER_END`, `PROFILE`, `BACKSTORY`, `UPDATED`, `BACKSTORY_REGEN`, `FORGOTTEN`, and `ERROR`.
- Add `azeroth_voices_addon_contacts` keyed by player GUID and bot GUID. The roster contains only bots previously encountered by that player.
- `forget` removes that player/bot contact and stored conversation history, but preserves the bot's global personality and sentiment. Missing addon tables degrade to an empty roster without disabling dialogue.
- Bundle the unchanged addon as `client/Chatter/` and install it with the module.

### Dedicated proximity chatter

- Add a proximity subsystem owning ordinary Say chatter; remove `say` from the distributed `Random.Scopes` default while routing legacy configured `say` entries through the proximity gate to avoid duplicate speech.
- Support outdoor maps and optionally dungeons/raids; always exclude battlegrounds and arenas.
- Use separate outdoor and instance settings: 30-second scans, 15% outdoor trigger chance, 100% dungeon/raid trigger chance, 60-second entity cooldown, and three-scene zone fatigue with 20% decay.
- Select one to four compatible speakers, with 40% conversation chance, four-line maximum, six-second turn gap, 30-second reply window, and five reply turns.
- Require every speaker to remain visible, alive, out of combat, in the same map/instance, within LOS, and audible to a real player. Instance identity is part of cooldown and scene keys.
- Use the ordered speaker policy: universal safety exclusions, speaker denylist, boss exclusion, guard/interactive role, humanoid, explicit non-humanoid allowlist, then rejection.
- Add `Proximity.Speaker.AllowEntries` and `DenyEntries`; deny wins over every other qualification. Existing NPC type settings remain valid for event and combat-start paths.
- Include disposition, creature type, rank, role, qualification reason, canonical instance name, current area, and curated instance lore in proximity prompts.
- Extend real-player `/say` handling to prioritize an eligible named NPC, then the selected target, then the last active scene participant, then a new proximity scene. Full-name matches beat unique first-token matches; ambiguous names never select multiple creatures.
- Implement multi-line conversations as paced single-speaker generations through the existing worker queue, allowing each delivered line to seed the next bounded turn.

### Boss and miniboss dialogue

- Add a separate pre-aggro boss subsystem for dungeon and raid maps only.
- Classify bosses from a built-in curated vanilla/Turtle entry registry derived from the checkout's audited boss data, with fallbacks for world-boss rank and instance-bind flags. Add configurable boss allow/deny entries; deny wins.
- Exclude ordinary proximity, event, and combat-start NPC generation for classified bosses so boss speech has one owner.
- Use these defaults: 2-second round-robin scans, 80-yard maximum radius, aggro-range-plus-margin safety, 2-6 second initial delay, 20-60 second repeat delay, 80% repeat chance with 50% decay and a 10% floor, 90-second presence reset, 15-second directed-reply cooldown, and unlimited automatic opportunities within one presence.
- Require the boss to be alive, hostile, out of combat, beyond calculated aggro distance, visible, and in LOS both before queueing and again before delivery. Never alter threat, aggro, facing, or movement.
- Automatically generate 5-22 word original lines of at most 180 characters. Directed `/say` replies respond to the player's meaning; later automatic lines receive recent delivered lines and must avoid repetition.
- Deliver boss lines with `MonsterYell`; keep boss dialogue enabled by default after live validation and provide a master toggle plus denylist.

### General pacing and quieter preset

- Add one delivery timeline per normalized channel plus outdoor zone or map/instance identity.
- Reserve the full duration of generated multi-line exchanges before scheduling their first line; use a default 15-second minimum gap.
- Apply pacing only to automated World/custom-channel chatter. Direct player-directed replies remain immediate.
- Keep combat-start, event, boss, and dungeon/raid reactions outside the quieter reductions.
- Add `conf/presets/mod-azeroth-voices-quieter.conf.dist`, not auto-loaded: outdoor proximity 5% per 60 seconds, instance proximity 100% per 30 seconds, conversation chance 30%, maximum three lines, General gap 30 seconds, and Random interval 180-360 seconds.

## Public Interfaces, Data, and Defaults

- Add `data/instance_lore.json` keyed by map ID. Use curated Vanilla/Turtle lore where available and the engine map name plus current area when no lore entry exists; never invent missing lore.
- Add the contacts migration under `data/sql/character/`; no existing personality, history, or sentiment table is altered.
- Extend internal request snapshots with creature entry/type/rank/role/qualification, instance identity, speaker/pacing ownership, conversation turn state, boss presence identity, and boss-yell delivery metadata.
- Add configuration groups for `AzerothVoices.Proximity.*`, `AzerothVoices.BossDialogue.*`, `AzerothVoices.GeneralChat.*`, and addon compatibility.
- Preserve all existing `.av` commands and configuration behavior except the removal of configurable `TraitCount` and the default removal of `say` from Random scopes.
- Update the CMake install rules to include instance lore, the quieter preset, and the bundled client addon.

## Test Plan

- Add focused BUILD_TESTING tests for three-trait generation and addon validation, NPC qualification precedence, boss classification and denylist behavior, name addressing and ambiguity, instance-lore fallback, General window serialization, and addon percent encoding/protocol responses.
- Configure and build static Release `mangosd` with PlayerBots and Turtle addons enabled; run the module tests through CTest and the complete configured build.
- Live-test outdoor frequency, dungeon/raid chatter, hostile-humanoid speech, functional non-humanoids, non-allowlisted non-humanoids, duplicate-name avoidance, LOS/distance rejection, and battleground exclusion.
- Live-test `/say` against selected NPCs, named NPCs, active scene participants, and bosses.
- Live-test boss approach and directed replies across multiple bosses, including no combat/threat/facing changes, safe-distance enforcement, repeat pacing, presence reset, and denylist suppression.
- Generate several concurrent World/custom-channel conversations and verify no wall-of-text delivery while direct replies stay responsive.
- Install the unchanged addon, verify roster, exact-three-trait save, tone/background polling, background regeneration, contact-scoped access, and Forget behavior.
- Verify the quieter preset and missing SQL/API failure paths without stopping dialogue or damaging existing state.

## Assumptions

- The existing Azeroth Voices provider, worker, history, RAG, and scheduling architecture remains authoritative.
- The stock Chatter Companion Lua files are not modified.
- "General chat" maps to existing World/custom-channel automated chatter because Azeroth Voices currently has no first-class zone General scope.
- Conversations use one provider request per speaker turn rather than one multi-speaker JSON response.
- World bosses and outdoor rare elites are not part of the dungeon/raid pre-aggro subsystem.
- Existing stored personalities with anything other than three traits are regenerated lazily rather than migrated.
