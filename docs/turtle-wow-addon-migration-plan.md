# Turtle WoW 1.18.1 Azeroth Voices Addon Migration

## Summary

- Convert the bundled client from the copied 3.3.5 Chatter addon into a fully branded `AzerothVoices` addon for Turtle WoW 1.18.1 / Vanilla Interface 11200.
- Preserve all roster, trait, tone, backstory, regeneration, and bulk-forget features in one movable window.
- Keep the existing vMaNGOS server implementation; it already passes 225 tests and builds successfully into Release `mangosd`.
- Preserve all existing uncommitted repository work and make no unrelated core, database, or client-installation changes.

## Implementation Changes

- Rename `client/Chatter` and its TOC/Lua globals/assets to `client/AzerothVoices`; use `AzerothVoicesDB`, version `2.0.0`, and `## Interface: 11200`.
- Port every Lua file to the target client's Lua 5.0 conventions:
  - Replace `#`, `%`, and `string.match` with `table.getn`, `math.mod`, and capture-based `string.find`.
  - Use Vanilla globals `this`, `event`, and `arg1...argN` in frame handlers.
  - Remove Wrath Interface Options panels, chat-message filters, `HookScript`, dynamic size handlers, and unsupported EditBox calls.
  - Use fixed Vanilla-safe layout and scrolling modeled on the working Turtle addons in the supplied client.
- Provide `/azerothvoices` and `/avvoices` as primary commands while retaining `/chatter` and `/llmc` as UI aliases.
- Migrate window position and selected-bot state from `ChatterDB` once when legacy saved data exists.
- Rewrite both repository and addon READMEs around Azeroth Voices, TortoiseWoW/vMaNGOS, PlayerBots, Turtle WoW 1.18.1 installation, the single-window UI, configuration requirements, protocol behavior, and troubleshooting. Remove copied `mod-llm-chatter`, AzerothCore, Python-bridge, 3.3.5, and Interface Options instructions except where explicitly documented as legacy compatibility.
- Update installer comments and related current documentation to reference the renamed Turtle-native addon rather than an unchanged stock Chatter copy.

## Interfaces and Compatibility

- Add the private client command transport:
  - `.avaddon 1 <request-id> <part-index> <part-count> <chunk>`
  - The vMaNGOS player hook accepts it only through SAY, reassembles at most eight parts/2 KiB per player, expires incomplete requests after 10 seconds, and suppresses every control frame before broadcast.
- Return responses through `Player::SendAddonMessage("AZEROTH_VOICES", frame)` using:
  - `1\t<request-id>\t<message-index>\t<part-index>\t<part-count>\t<chunk>`
  - Dynamically size chunks so the prefix plus payload remains at or below 255 bytes.
- The client accepts responses only from the exact prefix, `GUILD` transport, and its own character sender; it bounds indexes, part counts, assembled size, and stale buffers before parsing the existing roster/profile/backstory payloads.
- Refactor addon response builders to produce transport-neutral logical payloads. Wrap them as native frames for `.avaddon` or as existing `CHATTER_ADDON` system messages for legacy `.llmc`.
- Retain `.llmc`, `CHATTER_ADDON`, and their exact legacy behavior for old Chatter clients; no public GM command, SQL schema, personality model, or provider interface changes.

## Test Plan

- Extend the C++ suite for native request framing/reassembly, response chunk boundaries, long percent-encoded backgrounds, malformed/duplicate/out-of-order/expired frames, per-player isolation, and unchanged legacy `.llmc` behavior.
- Run the production addon in TOC order through a Lua 5.0 parser and mocked Vanilla UI runtime. Exercise startup, slash commands, window movement, roster loading, selection, save confirmation, tone/backstory polling, regeneration, bulk forget, timeout/error handling, saved-variable migration, and forged-message rejection.
- Statically reject Lua 5.1+/Wrath-only constructs and verify the TOC references every renamed file.
- Re-run all module tests and build the linked Release `mangosd` with PlayerBots and Turtle addons enabled.
- Confirm documentation and source contain no stale Chatter/3.3.5/mod-llm-chatter claims outside explicit legacy notes.

## Assumptions

- Only `C:\Users\Admin\Downloads\mod-azeroth-voices\mod-azeroth-voices` is modified; the supplied game client is used as a compatibility reference and the addon is not copied into it.
- Live in-game loading remains a follow-up acceptance step because repository-only deployment was selected.
- The existing `TurtleCamps` `SetCursorPosition` error in the supplied client is unrelated and remains out of scope.
