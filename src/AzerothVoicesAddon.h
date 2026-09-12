#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace AzerothVoices
{
    // Wire protocol support for both the native Turtle WoW AzerothVoices addon
    // (.avaddon SAY transport + AZEROTH_VOICES SendAddonMessage frames) and the
    // legacy stock Chatter Companion (.llmc SAY transport + CHATTER_ADDON system
    // responses). Every string field is percent encoded and `-` is the sentinel
    // for an empty string. Nothing in this file touches live game objects, so
    // it stays independently testable.
    namespace Addon
    {
        // Legacy Chatter wire constants
        constexpr char const* SystemPrefix = "CHATTER_ADDON ";
        constexpr char const* CommandPrefix = ".llmc";

        // Native AzerothVoices wire constants
        constexpr char const* NativePrefix = "AZEROTH_VOICES";
        constexpr char const* NativeCommandPrefix = ".avaddon";
        constexpr size_t MaxWireLength = 255;
        constexpr size_t MaxRequestParts = 8;
        constexpr size_t MaxRequestBytes = 2048;
        constexpr uint32_t RequestTimeoutMs = 10000;

        constexpr size_t TraitCount = 3;
        constexpr size_t TraitMaximumCharacters = 64;

        enum class CommandKind : uint8_t
        {
            Invalid,
            Roster,
            Get,
            Set,
            RegenBackstory,
            Forget
        };

        struct Command
        {
            CommandKind kind = CommandKind::Invalid;
            uint64_t guid = 0;
            std::vector<std::string> traits;
        };

        // Collapses control characters and whitespace exactly like the addon's
        // sanitizeInput(), but never empty: an empty value becomes "-".
        std::string Sanitize(std::string const& value);

        // Percent encodes everything outside [A-Za-z0-9-_.~] as %XX with upper
        // case hex digits, matching the addon's Encode().
        std::string Encode(std::string const& value);
        std::string Decode(std::string const& token);

        // Parses the text that follows `.llmc` or `.avaddon`. Leading command
        // tokens are tolerated so both chat and command-table call sites can share it.
        bool ParseCommand(std::string const& arguments, Command& command, std::string& error);

        // Exactly three distinct, non-empty, bounded traits.
        bool ValidateTraits(std::vector<std::string> const& traits, std::string& error);

        // Transport-neutral logical payloads
        std::string LogicalRosterBegin();
        std::string LogicalRosterEntry(uint64_t guid, std::string const& name);
        std::string LogicalRosterEnd();
        // PROFILE must always carry exactly six space separated fields, so a
        // missing trait is written as the empty sentinel.
        std::string LogicalProfile(uint64_t guid, std::string const& name,
                                   std::vector<std::string> const& traits, std::string const& tone);
        std::string LogicalBackstory(uint64_t guid, std::string const& background);
        std::string LogicalUpdated(uint64_t guid, std::string const& name, bool changed);
        std::string LogicalBackstoryRegen(uint64_t guid, std::string const& name);
        std::string LogicalForgotten(uint64_t guid, std::string const& name);
        std::string LogicalError(std::string const& channel, std::string const& message);

        // Legacy Chatter response builders (wrapped with "CHATTER_ADDON ")
        std::string Response(std::string const& payload);
        std::string RosterBegin();
        std::string RosterEntry(uint64_t guid, std::string const& name);
        std::string RosterEnd();
        std::string Profile(uint64_t guid, std::string const& name,
                            std::vector<std::string> const& traits, std::string const& tone);
        std::string Backstory(uint64_t guid, std::string const& background);
        std::string Updated(uint64_t guid, std::string const& name, bool changed);
        std::string BackstoryRegen(uint64_t guid, std::string const& name);
        std::string Forgotten(uint64_t guid, std::string const& name);
        std::string Error(std::string const& message);

        // Native frame builder
        // Returns frames for Player::SendAddonMessage("AZEROTH_VOICES", frame)
        // Frame format: 1\t<request-id>\t<message-index>\t<part-index>\t<part-count>\t<chunk>
        // Total wire size ("AZEROTH_VOICES\t" + frame) dynamically bounded <= 255 bytes.
        std::vector<std::string> BuildNativeFrames(std::string const& requestId,
                                                  size_t messageIndex,
                                                  std::string const& logicalPayload);
        std::vector<std::string> BuildNativeFrames(std::string const& requestId,
                                                  std::vector<std::string> const& logicalPayloads);

        // Request reassembly state machine
        struct ReassembledRequest
        {
            uint64_t playerGuid = 0;
            std::string requestId;
            std::string command;
        };

        enum class FrameStatus : uint8_t
        {
            NotAvaddon,
            Malformed,
            Duplicate,
            Buffered,
            Complete,
            Expired
        };

        class RequestReassembler
        {
        public:
            RequestReassembler() = default;

            FrameStatus Feed(uint64_t playerGuid, std::string const& message,
                             uint32_t nowMs, ReassembledRequest& outRequest);

            void ExpireStale(uint32_t nowMs);
            void ClearPlayer(uint64_t playerGuid);
            void ClearAll();
            size_t ActivePlayerCount() const;
            bool HasPendingRequest(uint64_t playerGuid) const;

        private:
            struct PendingRequest
            {
                std::string requestId;
                uint32_t partCount = 0;
                std::vector<std::string> parts;
                std::vector<bool> received;
                size_t totalBytes = 0;
                uint32_t receivedAtMs = 0;
            };

            std::unordered_map<uint64_t, PendingRequest> m_pending;
        };
    }
}
