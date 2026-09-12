# TortoiseWoW/vMaNGOS and PlayerBots Compatibility Verification Report

## 1. Executive Summary

- **Status:** **PASS**
- **Target Core:** TortoiseWoW / vMaNGOS (`tortoise-wow-extended`, git commit `1f18041d565d7b4a19a4`).
- **Target Addon Core:** `mod-playerbots` (built-in AI companion engine).
- **Client Target:** Turtle WoW 1.18.1 / Interface 11200 (AzerothVoices addon).
- **Module Under Test:** `mod-azeroth-voices`.
- **LLM-Chat Ownership:** Sole ownership enforced. When `AiPlayerbot.LLMEnabled` is enabled (`1`), Azeroth Voices issues an explicit startup error diagnostic in the server console and reports a warning via in-game `.av status` and `.av test`. Supported configuration: `AiPlayerbot.LLMEnabled = 0`.
- **Core Linkage & Integrity:** 0 core source changes, 0 `mod-playerbots` source changes. Clean static compilation and linkage into `mangosd.exe` (Release x64) and standalone unit test suite (Release & Debug x64: 349 checks, 0 failures; CTest 100% pass; Addon Lua 5.0 suite 100% pass).

---

## 2. Baseline Environment

| Component | Value | Notes |
| :--- | :--- | :--- |
| **Operating System** | Windows 10/11 x64 | PowerShell execution shell |
| **Compiler** | MSVC 19.44.35207 (VS 2022 BuildTools) | Standard `/std:c++17` |
| **CMake** | CMake 3.31.6 | Visual Studio 17 2022 generator |
| **Core Revision** | `tortoise-wow-extended` @ `1f18041d565d7b4a19a4` | Clean tree |
| **Module Revision** | `mod-azeroth-voices` @ `2792a232df3dc0627a8e` | All features compiled |
| **Module Loader** | `_av-verify\modules\generated\static\ModulesLoader.cpp` | `Addmod_azeroth_voicesScripts()` invoked once |
| **Build Target** | Release & Debug x64 `mangosd.exe` / `azeroth_voices_tests.exe` | Linked with OpenSSL 3 + vcpkg dependencies |

---

## 3. Hook Ledger & Script Contract Analysis

Every script class registered by `Addmod_azeroth_voicesScripts()` was audited against `src/game/ScriptObjects.h` in `tortoise-wow-extended`:

| Script Class | Registered Hooks | Hook Enumerations (`ScriptObjects.h`) | Thread Ownership & Dispatcher | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **`AzerothVoicesWorldScript`** | Startup, Update, ConfigLoad, Shutdown | `WORLDHOOK_ON_STARTUP`, `WORLDHOOK_ON_UPDATE`, `WORLDHOOK_ON_AFTER_CONFIG_LOAD`, `WORLDHOOK_ON_SHUTDOWN` | World Thread (`World::Update`) | Manages worker pool lifecycle, database flush ticks, and ambient chatter scheduling. |
| **`AzerothVoicesCombatWorldScript`** | Shutdown | `WORLDHOOK_ON_SHUTDOWN` | World Thread | Ensures graceful termination of combat monitoring buffers. |
| **`AzerothVoicesPlayerScript`** | 18 Player hooks: Death, Ghost, Quest, PvP Kill, Creature Kill, Level, Spell Learn, Spell Cast, Duel (Request/Start/End), Login, Logout, Zone Update, Map Change, Loot, Chat Command, Group Chat | `PLAYERHOOK_ON_PLAYER_JUST_DIED`, `PLAYERHOOK_ON_PLAYER_RELEASED_GHOST`, `PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST`, `PLAYERHOOK_ON_PVP_KILL`, `PLAYERHOOK_ON_CREATURE_KILL`, `PLAYERHOOK_ON_LEVEL_CHANGED`, `PLAYERHOOK_ON_LEARN_SPELL`, `PLAYERHOOK_ON_SPELL_CAST`, `PLAYERHOOK_ON_DUEL_REQUEST`, `PLAYERHOOK_ON_DUEL_START`, `PLAYERHOOK_ON_DUEL_END`, `PLAYERHOOK_ON_LOGIN`, `PLAYERHOOK_ON_LOGOUT`, `PLAYERHOOK_ON_UPDATE_ZONE`, `PLAYERHOOK_ON_MAP_CHANGED`, `PLAYERHOOK_ON_LOOT_ITEM`, `PLAYERHOOK_ON_CHAT_COMMAND`, `PLAYERHOOK_CAN_USE_GROUP_CHAT` | World Thread (`Player` context) | Extracts immutable snapshots of event details and GUIDs before queueing work. Control frames (`.avaddon`, `.llmc`) are intercepted and suppressed. |
| **`AzerothVoicesCombatPlayerScript`** | Update, Logout | `PLAYERHOOK_ON_UPDATE`, `PLAYERHOOK_ON_LOGOUT` | World Thread (`Player::Update`) | Tracks combat engagement timers and cleans up player references on logout. |
| **`AzerothVoicesServerScript`** | Packet Handled | `SERVERHOOK_ON_PACKET_HANDLED` | World / Session Thread | Exclusively handles `CMSG_MESSAGECHAT` with `CHAT_MSG_CHANNEL`. Ignores whisper, say, yell, party, and guild to guarantee zero duplication with `OnChatCommand` or `CanUseGroupChat`. |
| **`AzerothVoicesGuildScript`** | Add Member, Remove Member | `OnAddMember`, `OnRemoveMember` | World Thread (`Guild` context) | Triggers guild announcements and updates cache rosters. |
| **`AzerothVoicesCommandScript`** | Command Execution | `AllCommandScript::CanExecuteCommand` | World Thread (`ChatHandler` context) | Intercepts `.av` admin commands and `.llmc` legacy player companion queries. |

### Duplicate Processing Prevention
- **Channel vs Say/Yell/Party/Guild:** `AzerothVoicesServerScript::OnPacketHandled` only processes `CHAT_MSG_CHANNEL`. Say, Yell, Party, and Guild are handled exclusively via player hooks (`PLAYERHOOK_ON_CHAT_COMMAND` and `PLAYERHOOK_CAN_USE_GROUP_CHAT`), eliminating any double-trigger risk.
- **Addon Command Suppression:** Control messages beginning with `.avaddon` or `.llmc` return `false` from `CanUseGroupChat` and `CanExecuteCommand`, ensuring they never echo into public chat or generate "unknown command" errors.

---

## 4. PlayerBots Abstraction & Isolation

1. **Zero Internal Dependency:**
   - A full codebase grep confirms 0 includes of `playerbot.h`, `PlayerbotAI.h`, or any `mod-playerbots` source file in `mod-azeroth-voices`.
2. **Bot Detection:**
   - Detection of PlayerBots is performed strictly using the core engine abstraction:
     ```cpp
     Script_IsAIControlled(player)
     ```
   - 29 distinct call sites across the module employ this pattern safely.
3. **Sole LLM Ownership & Diagnostic Guard:**
   - In `conf/mod-azeroth-voices.conf.dist`:
     ```ini
     # The supported configuration requires AiPlayerbot.LLMEnabled = 0
     ```
   - In `AzerothVoicesManager.cpp` on server startup:
     ```cpp
     if (m_config->playerbotsLlmEnabled)
     {
         sLog.outError("[AzerothVoices][COMPATIBILITY] AiPlayerbot.LLMEnabled is enabled! mod-azeroth-voices is the sole LLM-chat owner. Disable AiPlayerbot.LLMEnabled (set AiPlayerbot.LLMEnabled = 0 in aiplayerbot.conf) to prevent duplicate generation and conflicting chat.");
     }
     ```
   - In `.av status` and `.av test`:
     - Emits `playerbots-llm=CONFLICT-ENABLED` and `Compatibility: WARNING` when enabled.
     - Emits `Compatibility: OK - PlayerBots native LLM is disabled` when properly set to `0`.

---

## 5. Configuration Parity & Compatibility Matrix

Every configuration key inherited from `aiplayerbot.conf.dist.in` has been verified for exact key naming and type compatibility:

| Option Name in `aiplayerbot.conf.dist.in` | Azeroth Voices Ingestion Key | Type | Compatible Default | Notes |
| :--- | :--- | :--- | :--- | :--- |
| `AiPlayerbot.LLMEnabled` | `AiPlayerbot.LLMEnabled` | Boolean | `0` (Disabled in bots) | Checked on startup; flagged if enabled. |
| `AiPlayerbot.LLMApiEndpoint` | `AiPlayerbot.LLMApiEndpoint` | String | `https://api.openai.com/...` | Full URL accepted; remote plain HTTP rejected. |
| `AiPlayerbot.LLMApiKey` | `AiPlayerbot.LLMApiKey` | String | `env:OPENAI_API_KEY` | Supports secret or environment variable lookup. |
| `AiPlayerbot.LLMApiJson` | `AiPlayerbot.LLMApiJson` | String | `""` (Empty) | Custom JSON templates supported. |
| `AiPlayerbot.LLMGenerationTimeout` | `AiPlayerbot.LLMGenerationTimeout` | Positive Int | `60` | Generation read/write timeout in seconds. |
| `AiPlayerbot.LLMMaxSimultaniousGenerations` | `AiPlayerbot.LLMMaxSimultaniousGenerations` | Positive Int | `8` | Exact spelling matching vMaNGOS fork preserved. |
| `AiPlayerbot.LLMPrePrompt` | `AiPlayerbot.LLMPrePrompt` | String | Contextual template | Full variable expansion (`<bot name>`, `<bot race>`, etc.). |
| `AiPlayerbot.LLMPrompt` | `AiPlayerbot.LLMPrompt` | String | Initial prompt template | Formats sender and text. |
| `AiPlayerbot.LLMResponseStartPattern` | `AiPlayerbot.LLMResponseStartPattern` | String | Regex pattern | Output cleaning regex. |
| `AiPlayerbot.LLMResponseEndPattern` | `AiPlayerbot.LLMResponseEndPattern` | String | Regex pattern | Output cleaning regex. |
| `AiPlayerbot.LLMResponseDeletePattern` | `AiPlayerbot.LLMResponseDeletePattern` | String | Regex pattern | Output cleaning regex. |
| `AiPlayerbot.LLMResponseSplitPattern` | `AiPlayerbot.LLMResponseSplitPattern` | String | Regex pattern | Multi-line splitting regex. |
| `AiPlayerbot.LLMGlobalContext` | `AiPlayerbot.LLMGlobalContext` | Boolean | `0` | Preserved for template compatibility. |
| `AiPlayerbot.LLMBotToBotChatChance` | `AiPlayerbot.LLMBotToBotChatChance` | Percentage | `10` | Bot-to-bot conversational gate. |
| `AiPlayerbot.LLMRpgAIChatChance` | `AiPlayerbot.LLMRpgAIChatChance` | Percentage | `30` | Roleplay response probability gate. |
| `AiPlayerbot.LLMBlockedReplyChannels` | `AiPlayerbot.LLMBlockedReplyChannels` | Comma List | `""` | Suppressed reply channels. |

---

## 6. Concurrency & Thread Boundary Verification

- **World Thread -> Worker Pool:**
  - When an in-game trigger occurs, `SnapshotSpeaker` and `SnapshotCreature`/`ActorSnapshot` copy all required state (GUIDs as `uint64_t`, names, health percentages, level, location strings).
  - Live engine pointers (`Player*`, `Creature*`, `Map*`, `Guild*`) **never** enter the queue or cross into worker threads.
- **Worker Pool -> Delivery:**
  - HTTP workers execute network requests with timeout and retry backoff.
  - On HTTP completion, response text and metadata are placed into `m_completions` guarded by a mutex.
  - Delivery occurs exclusively on the main World thread during `World::Update` via `Manager::Update()`.
  - Identity, online state, audience, and line-of-sight are re-validated immediately before speaking. If the target logged off or changed maps, the stale response is safely discarded.

---

## 7. Dynamic vs Static Module Linking on Windows

### Analysis
- In `tortoise-wow-extended`, the core libraries (`game.lib`, `shared.lib`, `framework.lib`) are compiled as static libraries without `__declspec(dllexport)` / `__declspec(dllimport)` annotations on engine singletons or `ScriptRegistry` template instances.
- On Windows (PE/COFF), attempting to link a module as a dynamic module (`SHARED` `.dll`) results in:
  1. Unresolved external symbols for core classes not marked for export.
  2. Duplicate singleton static storage across the DLL boundary (e.g. `ScriptRegistry<AllCommandScript>` would instantiate separately in the executable and DLL, causing registered scripts in the DLL to be invisible to the core dispatcher).
- On Linux/POSIX (`ELF`), symbol resolution can be shared across shared objects via `dlopen(..., RTLD_GLOBAL)`.
- **Conclusion:** On Windows, `STATIC` module compilation via `ModulesLoader.cpp` is the only safe and supported linkage model for TortoiseWoW/vMaNGOS. `mod-azeroth-voices` compiles cleanly and links into `mangosd.exe` statically with 0 errors.

---

## 8. Test Execution Evidence

### 1. Engine-Free C++ Suite (`azeroth_voices_tests.exe`)
- **Release x64:**
  ```text
  OK: 349 checks, 0 failures
  ```
- **Debug x64:**
  ```text
  OK: 349 checks, 0 failures
  ```
- **CTest:**
  ```text
  1/1 Test #1: azeroth_voices_tests ............. Passed 0.01 sec
  100% tests passed, 0 tests failed out of 1
  ```

### 2. Addon Test Suite (`tests/test_addon.lua`)
- **Compatibility:** Lua 5.0 & Vanilla 1.12 / Turtle WoW 1.18.1 client runtime.
- **Static Analysis:**
  - `AzerothVoices.lua`: 0 Lua 5.1+ constructs (`#`, `%`, `string.match`).
  - `AzerothVoicesRoster.lua`: 0 Lua 5.1+ constructs.
  - `AzerothVoicesUI.lua`: 0 Lua 5.1+ constructs.
- **Mocked Runtime Simulation:** 9 test cases passed (DB migration, slash commands, security rejection of forged frames, multi-part chunk reassembly, profile editing, story regeneration, bulk forget, timeout handling, window persistence).
  ```text
  ALL ADDON LUA 5.0 & RUNTIME SIMULATION TESTS PASSED SUCCESSFULLY!
  ```

### 3. Worldserver Executable Build (`mangosd.exe`)
- **Target:** `_av-verify\src\mangosd\mangosd.vcxproj` (Release x64).
- **Result:** Build succeeded with 0 errors, 1 warning (discarded nodiscard in vendored core httplib).
- **Generated Executable:** `C:\Users\Admin\Downloads\mod-azeroth-voices\tortoise-wow-extended\bin\Release\mangosd.exe` (21,406,720 bytes).

---

## 9. Conclusion

The compatibility verification of `mod-azeroth-voices` against `tortoise-wow-extended` and `mod-playerbots` is complete. All contract requirements, sole ownership protections, configuration parity checks, hook ledgers, and build/test gates have been satisfied with zero modifications to the core or PlayerBots trees.
