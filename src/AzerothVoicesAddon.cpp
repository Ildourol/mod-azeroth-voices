#include "AzerothVoicesAddon.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <set>
#include <sstream>

namespace AzerothVoices
{
    namespace Addon
    {
        namespace
        {
            std::string Trim(std::string value)
            {
                auto notSpace = [](unsigned char c) { return !std::isspace(c); };
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
                value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
                return value;
            }

            std::string Lower(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            std::vector<std::string> Split(std::string const& value)
            {
                std::vector<std::string> result;
                std::istringstream stream(value);
                std::string token;
                while (stream >> token)
                    result.push_back(token);
                return result;
            }

            bool IsUnreserved(unsigned char c)
            {
                return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
            }

            int HexValue(char c)
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return 10 + c - 'a';
                if (c >= 'A' && c <= 'F')
                    return 10 + c - 'A';
                return -1;
            }

            bool ParseGuid(std::string const& token, uint64_t& guid)
            {
                if (token.empty() || token.size() > 20 ||
                    !std::all_of(token.begin(), token.end(), [](unsigned char c) {
                        return std::isdigit(c) != 0;
                    }))
                    return false;
                errno = 0;
                char* end = nullptr;
                unsigned long long const value = std::strtoull(token.c_str(), &end, 10);
                if (errno != 0 || !end || *end != '\0' || value == 0)
                    return false;
                guid = static_cast<uint64_t>(value);
                return true;
            }

            bool ParseUint32(std::string const& token, uint32_t& out)
            {
                if (token.empty() || token.size() > 10 ||
                    !std::all_of(token.begin(), token.end(), [](unsigned char c) {
                        return std::isdigit(c) != 0;
                    }))
                    return false;
                errno = 0;
                char* end = nullptr;
                unsigned long const value = std::strtoul(token.c_str(), &end, 10);
                if (errno != 0 || !end || *end != '\0')
                    return false;
                out = static_cast<uint32_t>(value);
                return true;
            }
        }

        std::string Sanitize(std::string const& value)
        {
            std::string result;
            result.reserve(value.size());
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                char output = static_cast<char>(input);
                if (std::iscntrl(input))
                    output = ' ';
                bool const space = std::isspace(static_cast<unsigned char>(output)) != 0;
                if (space)
                {
                    if (previousSpace)
                        continue;
                    output = ' ';
                }
                result.push_back(output);
                previousSpace = space;
            }
            return Trim(result);
        }

        std::string Encode(std::string const& value)
        {
            std::string const sanitized = Sanitize(value);
            if (sanitized.empty())
                return "-";

            static char const* const hex = "0123456789ABCDEF";
            std::string result;
            result.reserve(sanitized.size() * 3);
            for (unsigned char c : sanitized)
            {
                if (IsUnreserved(c))
                {
                    result.push_back(static_cast<char>(c));
                    continue;
                }
                result.push_back('%');
                result.push_back(hex[(c >> 4) & 0x0F]);
                result.push_back(hex[c & 0x0F]);
            }
            return result;
        }

        std::string Decode(std::string const& token)
        {
            if (token == "-")
                return "";

            std::string result;
            result.reserve(token.size());
            for (size_t i = 0; i < token.size(); ++i)
            {
                if (token[i] == '%' && i + 2 < token.size())
                {
                    int const high = HexValue(token[i + 1]);
                    int const low = HexValue(token[i + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        result.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                        continue;
                    }
                }
                result.push_back(token[i]);
            }
            return Sanitize(result);
        }

        bool ValidateTraits(std::vector<std::string> const& traits, std::string& error)
        {
            if (traits.size() != TraitCount)
            {
                error = "Exactly three traits are required.";
                return false;
            }

            std::set<std::string> unique;
            for (std::string const& raw : traits)
            {
                std::string const trait = Sanitize(raw);
                if (trait.empty())
                {
                    error = "All three traits are required.";
                    return false;
                }
                if (trait.size() > TraitMaximumCharacters)
                {
                    error = "Traits must be 64 characters or fewer.";
                    return false;
                }
                if (!unique.insert(Lower(trait)).second)
                {
                    error = "Traits must be distinct.";
                    return false;
                }
            }
            return true;
        }

        bool ParseCommand(std::string const& arguments, Command& command, std::string& error)
        {
            command = Command();
            error.clear();

            std::vector<std::string> tokens = Split(arguments);
            if (!tokens.empty())
            {
                std::string const& first = tokens.front();
                std::string const lowerFirst = Lower(first);
                if (first == CommandPrefix || lowerFirst == "llmc" ||
                    first == NativeCommandPrefix || lowerFirst == "avaddon")
                {
                    tokens.erase(tokens.begin());
                }
            }
            if (tokens.empty())
            {
                error = "Usage: .llmc roster | get <guid> | set <guid> <t1> <t2> <t3> | regenbackstory <guid> | forget <guid>";
                return false;
            }

            std::string const verb = Lower(tokens[0]);
            if (verb == "roster")
            {
                if (tokens.size() != 1)
                {
                    error = "Usage: .llmc roster";
                    return false;
                }
                command.kind = CommandKind::Roster;
                return true;
            }

            if (verb != "get" && verb != "set" && verb != "regenbackstory" && verb != "forget")
            {
                error = "Unknown Chatter command.";
                return false;
            }

            if (verb == "set")
            {
                if (tokens.size() != 5)
                {
                    error = "Usage: .llmc set <guid> <trait1> <trait2> <trait3>";
                    return false;
                }
            }
            else if (tokens.size() != 2)
            {
                error = "Usage: .llmc " + verb + " <guid>";
                return false;
            }

            if (!ParseGuid(tokens[1], command.guid))
            {
                error = "A numeric character GUID is required.";
                return false;
            }

            if (verb == "get")
                command.kind = CommandKind::Get;
            else if (verb == "regenbackstory")
                command.kind = CommandKind::RegenBackstory;
            else if (verb == "forget")
                command.kind = CommandKind::Forget;
            else
            {
                command.kind = CommandKind::Set;
                for (size_t i = 2; i < 5; ++i)
                    command.traits.push_back(Decode(tokens[i]));
                if (!ValidateTraits(command.traits, error))
                    return false;
            }
            return true;
        }

        std::string LogicalRosterBegin()
        {
            return "ROSTER_BEGIN";
        }

        std::string LogicalRosterEntry(uint64_t guid, std::string const& name)
        {
            return "ROSTER " + std::to_string(guid) + " " + Encode(name);
        }

        std::string LogicalRosterEnd()
        {
            return "ROSTER_END";
        }

        std::string LogicalProfile(uint64_t guid, std::string const& name,
                                   std::vector<std::string> const& traits, std::string const& tone)
        {
            std::ostringstream line;
            line << "PROFILE " << guid << ' ' << Encode(name);
            for (size_t i = 0; i < TraitCount; ++i)
                line << ' ' << (i < traits.size() ? Encode(traits[i]) : std::string("-"));
            line << ' ' << Encode(tone);
            return line.str();
        }

        std::string LogicalBackstory(uint64_t guid, std::string const& background)
        {
            return "BACKSTORY " + std::to_string(guid) + " " + Encode(background);
        }

        std::string LogicalUpdated(uint64_t guid, std::string const& name, bool changed)
        {
            return "UPDATED " + std::to_string(guid) + " " + Encode(name) + " " +
                (changed ? "changed" : "unchanged");
        }

        std::string LogicalBackstoryRegen(uint64_t guid, std::string const& name)
        {
            return "BACKSTORY_REGEN " + std::to_string(guid) + " " + Encode(name);
        }

        std::string LogicalForgotten(uint64_t guid, std::string const& name)
        {
            return "FORGOTTEN " + std::to_string(guid) + " " + Encode(name);
        }

        std::string LogicalError(std::string const& channel, std::string const& message)
        {
            return "ERROR " + channel + " " + Encode(message);
        }

        std::string Response(std::string const& payload)
        {
            return std::string(SystemPrefix) + payload;
        }

        std::string RosterBegin()
        {
            return Response(LogicalRosterBegin());
        }

        std::string RosterEntry(uint64_t guid, std::string const& name)
        {
            return Response(LogicalRosterEntry(guid, name));
        }

        std::string RosterEnd()
        {
            return Response(LogicalRosterEnd());
        }

        std::string Profile(uint64_t guid, std::string const& name,
                            std::vector<std::string> const& traits, std::string const& tone)
        {
            return Response(LogicalProfile(guid, name, traits, tone));
        }

        std::string Backstory(uint64_t guid, std::string const& background)
        {
            return Response(LogicalBackstory(guid, background));
        }

        std::string Updated(uint64_t guid, std::string const& name, bool changed)
        {
            return Response(LogicalUpdated(guid, name, changed));
        }

        std::string BackstoryRegen(uint64_t guid, std::string const& name)
        {
            return Response(LogicalBackstoryRegen(guid, name));
        }

        std::string Forgotten(uint64_t guid, std::string const& name)
        {
            return Response(LogicalForgotten(guid, name));
        }

        std::string Error(std::string const& message)
        {
            return Response(LogicalError("llmc", message));
        }

        std::vector<std::string> BuildNativeFrames(std::string const& requestId,
                                                  size_t messageIndex,
                                                  std::string const& logicalPayload)
        {
            std::vector<std::string> frames;
            std::string const req = requestId.empty() ? "0" : requestId;
            size_t const msgIdx = messageIndex == 0 ? 1 : messageIndex;
            // Prefix ("AZEROTH_VOICES\t") is 15 bytes; 255 - 15 = 240 maximum frame bytes.
            constexpr size_t MaxFrameBytes = 240;

            auto makeHeader = [&](size_t partIndex, size_t partCount) {
                return "1\t" + req + "\t" + std::to_string(msgIdx) + "\t" +
                       std::to_string(partIndex) + "\t" + std::to_string(partCount) + "\t";
            };

            if (logicalPayload.empty())
            {
                frames.push_back(makeHeader(1, 1));
                return frames;
            }

            // Determine minimum partCount required so every frame fits <= MaxFrameBytes
            size_t partCount = 1;
            while (true)
            {
                size_t capacity = 0;
                bool headerFits = true;
                for (size_t p = 1; p <= partCount; ++p)
                {
                    size_t const hLen = makeHeader(p, partCount).size();
                    if (hLen >= MaxFrameBytes)
                    {
                        headerFits = false;
                        break;
                    }
                    capacity += (MaxFrameBytes - hLen);
                }
                if (headerFits && capacity >= logicalPayload.size())
                    break;
                ++partCount;
            }

            size_t offset = 0;
            for (size_t p = 1; p <= partCount; ++p)
            {
                std::string const header = makeHeader(p, partCount);
                size_t const maxChunk = (header.size() < MaxFrameBytes) ? (MaxFrameBytes - header.size()) : 0;
                size_t const remaining = logicalPayload.size() - offset;
                size_t const chunkSize = (p == partCount) ? remaining : std::min(maxChunk, remaining);
                frames.push_back(header + logicalPayload.substr(offset, chunkSize));
                offset += chunkSize;
            }

            return frames;
        }

        std::vector<std::string> BuildNativeFrames(std::string const& requestId,
                                                  std::vector<std::string> const& logicalPayloads)
        {
            std::vector<std::string> frames;
            for (size_t i = 0; i < logicalPayloads.size(); ++i)
            {
                std::vector<std::string> msgFrames = BuildNativeFrames(requestId, i + 1, logicalPayloads[i]);
                frames.insert(frames.end(), msgFrames.begin(), msgFrames.end());
            }
            return frames;
        }

        FrameStatus RequestReassembler::Feed(uint64_t playerGuid, std::string const& message,
                                             uint32_t nowMs, ReassembledRequest& outRequest)
        {
            outRequest = ReassembledRequest();

            // Format: .avaddon 1 <request-id> <part-index> <part-count> <chunk>
            size_t pos = message.find_first_not_of(" \t\r\n");
            if (pos == std::string::npos)
                return FrameStatus::NotAvaddon;

            size_t endPrefix = message.find_first_of(" \t\r\n", pos);
            std::string const prefixToken = message.substr(pos, endPrefix == std::string::npos ? std::string::npos : endPrefix - pos);
            if (Lower(prefixToken) != NativeCommandPrefix)
                return FrameStatus::NotAvaddon;

            auto nextToken = [&](std::string& token) -> bool {
                if (endPrefix == std::string::npos)
                    return false;
                pos = message.find_first_not_of(" \t\r\n", endPrefix);
                if (pos == std::string::npos)
                    return false;
                endPrefix = message.find_first_of(" \t\r\n", pos);
                token = message.substr(pos, endPrefix == std::string::npos ? std::string::npos : endPrefix - pos);
                return true;
            };

            std::string versionStr;
            std::string requestId;
            std::string partIndexStr;
            std::string partCountStr;

            if (!nextToken(versionStr) || !nextToken(requestId) ||
                !nextToken(partIndexStr) || !nextToken(partCountStr))
            {
                return FrameStatus::Malformed;
            }

            if (versionStr != "1" || requestId.empty() ||
                requestId.find('\t') != std::string::npos)
            {
                return FrameStatus::Malformed;
            }

            uint32_t partIndex = 0;
            uint32_t partCount = 0;
            if (!ParseUint32(partIndexStr, partIndex) || !ParseUint32(partCountStr, partCount))
                return FrameStatus::Malformed;

            if (partCount < 1 || partCount > MaxRequestParts ||
                partIndex < 1 || partIndex > partCount)
            {
                return FrameStatus::Malformed;
            }

            // Chunk is everything following the single space/delimiter after partCount
            std::string chunk;
            if (endPrefix != std::string::npos && endPrefix < message.size())
            {
                // If there's a space immediately after partCount, strip that single separator space
                size_t chunkStart = endPrefix;
                if (message[chunkStart] == ' ')
                    ++chunkStart;
                chunk = message.substr(chunkStart);
            }

            // Check existing pending request for this player
            auto it = m_pending.find(playerGuid);
            if (it != m_pending.end())
            {
                bool const isExpired = (nowMs >= it->second.receivedAtMs &&
                                        nowMs - it->second.receivedAtMs > RequestTimeoutMs);
                if (isExpired)
                {
                    bool const sameRequest = (it->second.requestId == requestId);
                    m_pending.erase(it);
                    if (sameRequest)
                        return FrameStatus::Expired;
                    it = m_pending.end();
                }
                else if (it->second.requestId != requestId)
                {
                    // New request arrived before previous completed; discard old
                    m_pending.erase(it);
                    it = m_pending.end();
                }
                else if (it->second.partCount != partCount)
                {
                    // Inconsistent partCount for same request-id
                    m_pending.erase(it);
                    return FrameStatus::Malformed;
                }
            }

            if (it == m_pending.end())
            {
                PendingRequest req;
                req.requestId = requestId;
                req.partCount = partCount;
                req.parts.resize(partCount);
                req.received.resize(partCount, false);
                req.totalBytes = 0;
                req.receivedAtMs = nowMs;
                auto inserted = m_pending.emplace(playerGuid, std::move(req));
                it = inserted.first;
            }

            PendingRequest& pending = it->second;
            size_t const partIdx0 = static_cast<size_t>(partIndex - 1);

            if (pending.received[partIdx0])
                return FrameStatus::Duplicate;

            if (pending.totalBytes + chunk.size() > MaxRequestBytes)
            {
                m_pending.erase(it);
                return FrameStatus::Malformed;
            }

            pending.parts[partIdx0] = chunk;
            pending.received[partIdx0] = true;
            pending.totalBytes += chunk.size();

            bool allReceived = true;
            for (bool received : pending.received)
            {
                if (!received)
                {
                    allReceived = false;
                    break;
                }
            }

            if (!allReceived)
                return FrameStatus::Buffered;

            outRequest.playerGuid = playerGuid;
            outRequest.requestId = pending.requestId;
            outRequest.command.clear();
            for (std::string const& piece : pending.parts)
                outRequest.command += piece;

            m_pending.erase(it);
            return FrameStatus::Complete;
        }

        void RequestReassembler::ExpireStale(uint32_t nowMs)
        {
            for (auto it = m_pending.begin(); it != m_pending.end(); )
            {
                if (nowMs >= it->second.receivedAtMs &&
                    nowMs - it->second.receivedAtMs > RequestTimeoutMs)
                {
                    it = m_pending.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void RequestReassembler::ClearPlayer(uint64_t playerGuid)
        {
            m_pending.erase(playerGuid);
        }

        void RequestReassembler::ClearAll()
        {
            m_pending.clear();
        }

        size_t RequestReassembler::ActivePlayerCount() const
        {
            return m_pending.size();
        }

        bool RequestReassembler::HasPendingRequest(uint64_t playerGuid) const
        {
            return m_pending.find(playerGuid) != m_pending.end();
        }
    }
}
