# TortoiseWoW/vMaNGOS and PlayerBots Compatibility Verification Plan

## Summary

Verify and, where necessary, correct `mod-azeroth-voices` against the checked-out `tortoise-wow-extended` fork. Preserve all existing module work and make changes only inside the module. TortoiseWoW core and `mod-playerbots` remain unchanged.

Azeroth Voices will be the sole LLM-chat owner. The supported configuration requires `AiPlayerbot.LLMEnabled = 0`; if PlayerBots' native LLM is enabled, Azeroth Voices will issue a clear startup warning.

Existing evidence—225 passing unit checks and a linked Release `mangosd.exe`—is the baseline, not final runtime certification.

## Verification and Remediation

- Record core/module revisions, dirty-tree state, compiler, dependencies, CMake options, generated loader, and module junction before changing anything.
- Build a contract ledger for every World, Player, Server, Guild, update, shutdown, and command hook:
  - Match the exact vMaNGOS enum, signature, registration, dispatcher, production caller, execution thread, ordering, parameters, and return semantics.
  - Confirm `Addmod_azeroth_voicesScripts()` is generated and invoked exactly once.
  - Detect duplicate processing between packet, chat-command, group-chat, combat-update, and world-update hooks.
- Enforce the TortoiseWoW boundary:
  - Use C++17 and only APIs, types, naming conventions, database primitives, logging, GUID handling, and script interfaces present in this fork.
  - Reject remaining AzerothCore headers, loaders, namespaces, configuration APIs, and assumptions.
  - Detect PlayerBots exclusively through `Script_IsAIControlled`; do not include or call PlayerBots implementation classes.
  - Compare every inherited `AiPlayerbot.LLM*` setting with PlayerBots for exact spelling and compatible type, including the fork's `LLMMaxSimultaniousGenerations` spelling.
- Add a configuration contract check:
  - Every active distributed option must be consumed exactly as documented.
  - Legacy-only reads must be explicitly listed as migration fallbacks.
  - Detect unused, misspelled, duplicated, wrongly typed, or undocumented options and mismatched defaults/ranges.
- Audit ownership and concurrency:
  - Hooks may capture only copied GUIDs, strings, enums, and snapshots before crossing queues.
  - HTTP workers must never access live `Player`, `Creature`, `Unit`, `Map`, `Group`, `Guild`, `Channel`, or `WorldSession` objects.
  - Completions must return to the owning update path and re-resolve identity, online state, map/instance, audience, and actor eligibility before delivery.
  - Reload and shutdown must reject new work, wake and join workers safely, discard stale completions, flush only valid world-owned persistence, and finish before database teardown.
- Audit database compatibility:
  - Validate all migrations against the fork's character-database conventions and actual query APIs.
  - Confirm escaping, integer widths, timestamp behavior, transactions, bounded caches, cleanup, lazy loading, missing-table fallback, and Forget/reset isolation.
  - Never edit an already released migration to repair a deployed schema; add a new ordered migration when required.
- Preserve existing `.av` commands and the Chatter `.llmc`/`CHATTER_ADDON` protocol unless a proven compatibility defect requires a backward-compatible module-side correction.

## Interfaces and Documentation

- Do not change any TortoiseWoW or PlayerBots public API.
- Do not add a direct dependency on PlayerBots internals.
- Add a startup compatibility diagnostic for `AiPlayerbot.LLMEnabled > 0`; document that production must disable PlayerBots' built-in LLM to prevent duplicate generation.
- Produce `docs/vmangos-playerbots-compatibility-verification-report.md` containing the baseline, hook ledger, configuration comparison, test evidence, corrections, unresolved limitations, and an explicit pass/fail result.
- Update the README and configuration comments only where verified behavior differs from their current claims.

## Test Plan and Acceptance

- Automated verification:
  - Add contract tests for hook declarations/registration, loader inclusion, PlayerBots abstraction usage, forbidden AzerothCore symbols, configuration parity, legacy fallbacks, and install contents.
  - Extend unit coverage for hook adapters, malformed packets, duplicate suppression, queue replacement, expiry, lifecycle transitions, and configuration boundaries.
  - Use a deterministic localhost provider fixture for success, timeout, disconnect, malformed JSON, HTTP 429/5xx, reasoning-parameter rejection, and oversized responses.
- Build matrix:
  - Fresh Windows x64 Debug and Release static builds with PlayerBots enabled.
  - Release dynamic-module build because the README advertises it; either make it pass or withdraw that claim with evidence.
  - Run CTest, build and link `mangosd`, inspect the generated loader, and perform a staged install verifying config, SQL, data, addon, and documentation.
- Disposable runtime:
  - Create isolated login/world/character databases and dedicated configuration/ports following the fork's documented base-import and updater process.
  - Apply all module migrations through the supported module updater, then verify restart idempotency and missing-table RAM fallbacks.
  - Create dedicated real-player, GM, PlayerBot, guild, group, raid, and channel fixtures; do not use production accounts or databases.
- Gameplay scenarios:
  - Exercise whisper, say, yell, party/subgroup, raid, guild, officer, world, and custom-channel input and delivery.
  - Verify login/logout, zone/map change, death/release, kill, loot, quest completion, level, learned spell, spell cast, duel, guild membership, combat-start, proximity, boss, group, raid, guild, memory, personality, and sentiment paths.
  - Confirm bot-only audiences remain silent, disabled scopes remain silent, and each accepted trigger creates at most one Azeroth Voices generation.
  - Change logout, map, instance, target, group, guild, channel, creature lifetime, and PlayerBot control state while requests are in flight; stale replies must be dropped safely.
  - Reload during queued/in-flight work and stop during retry/backoff; require clean joins, no deadlock, no post-shutdown delivery, and successful restart.
  - Verify persistence limits, pruning, RAM fallback, contact creation, addon Forget, `.av memory forget`, and isolation between history, personality, sentiment, contacts, and memory.
  - Run the Chatter addon with the specified Turtle WoW `1.18.1-7272-Hotfix-2026-04-12` client and require successful roster/get/set/regenerate/forget round trips with no Lua errors.
- Final acceptance requires:
  - No module compile/link/test errors or new module warnings.
  - Every registered hook has a confirmed native caller and documented ownership.
  - No live core pointer crosses an HTTP-worker boundary.
  - No duplicate PlayerBots/Azeroth Voices LLM response path in the supported configuration.
  - No core or PlayerBots source modifications.
  - Clean startup, reload, gameplay, persistence, addon operation, and shutdown on the disposable realm.

## Assumptions

- `C:\Users\Admin\Downloads\mod-azeroth-voices\tortoise-wow-extended` is the authoritative core.
- `C:\Users\Admin\Downloads\mod-azeroth-voices\mod-azeroth-voices` is the only permitted source-edit boundary.
- Existing uncommitted module work is intentional and must be preserved.
- Azeroth Voices owns all generated dialogue; PlayerBots remains responsible for character AI and ordinary gameplay behavior.
- Runtime tests use isolated databases, accounts, ports, configs, and a deterministic local provider. A paid external LLM call is not required for compatibility certification.
