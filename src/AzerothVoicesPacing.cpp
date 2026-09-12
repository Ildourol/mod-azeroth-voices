#include "AzerothVoicesPacing.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        char const* ScopeToken(ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::World: return "world";
                case ChatScope::Channel: return "channel";
                default: break;
            }
            return "";
        }

        uint64_t MillisecondsBetween(DeliveryTimeline::TimePoint later,
                                     DeliveryTimeline::TimePoint earlier)
        {
            if (later <= earlier)
                return 0;
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier).count());
        }
    }

    std::string NormalizePacingKey(ChatScope scope, std::string const& channelName,
                                   uint32_t mapId, uint32_t instanceId, uint32_t zoneId,
                                   bool instance)
    {
        char const* const scopeToken = ScopeToken(scope);
        if (!*scopeToken)
            return "";

        std::ostringstream key;
        key << scopeToken << ':';
        key << (channelName.empty() ? "world" : Lower(channelName));
        if (instance)
            key << "|map:" << mapId << ':' << instanceId;
        else
            key << "|zone:" << zoneId;
        return key.str();
    }

    uint32_t EstimateExchangeMilliseconds(size_t lines, bool typingSimulation,
                                          uint32_t baseDelayMilliseconds,
                                          uint32_t perCharacterMilliseconds,
                                          uint32_t assumedCharactersPerLine,
                                          uint32_t interLineGapMilliseconds)
    {
        if (!lines)
            return 0;
        if (!typingSimulation)
            return interLineGapMilliseconds * static_cast<uint32_t>(lines - 1);

        uint64_t const perLine = static_cast<uint64_t>(baseDelayMilliseconds) +
            static_cast<uint64_t>(perCharacterMilliseconds) * assumedCharactersPerLine;
        uint64_t const total = perLine * lines +
            static_cast<uint64_t>(interLineGapMilliseconds) * (lines - 1);
        return static_cast<uint32_t>(std::min<uint64_t>(total, 600000));
    }

    void DeliveryTimeline::Clear()
    {
        m_nextEligible.clear();
    }

    size_t DeliveryTimeline::Size() const
    {
        return m_nextEligible.size();
    }

    bool DeliveryTimeline::CanDeliver(std::string const& key, TimePoint now) const
    {
        if (key.empty())
            return true;
        auto found = m_nextEligible.find(key);
        return found == m_nextEligible.end() || found->second <= now;
    }

    DeliveryTimeline::TimePoint DeliveryTimeline::NextEligible(std::string const& key) const
    {
        auto found = m_nextEligible.find(key);
        return found == m_nextEligible.end() ? TimePoint() : found->second;
    }

    void DeliveryTimeline::Reserve(std::string const& key, TimePoint now,
                                   uint32_t durationMilliseconds, uint32_t minimumGapMilliseconds)
    {
        if (key.empty())
            return;
        TimePoint candidate = now + std::chrono::milliseconds(
            static_cast<int64_t>(durationMilliseconds) + static_cast<int64_t>(minimumGapMilliseconds));
        auto found = m_nextEligible.find(key);
        if (found == m_nextEligible.end() || found->second < candidate)
            m_nextEligible[key] = candidate;
    }

    void DeliveryTimeline::Prune(TimePoint now)
    {
        for (auto it = m_nextEligible.begin(); it != m_nextEligible.end(); )
            it = it->second <= now ? m_nextEligible.erase(it) : std::next(it);
    }

    std::string DeliveryTimeline::Describe(TimePoint now) const
    {
        std::ostringstream text;
        for (auto const& entry : m_nextEligible)
        {
            if (text.tellp() > 0)
                text << ',';
            uint64_t const remaining = MillisecondsBetween(entry.second, now);
            text << entry.first << '=' << (remaining / 1000);
        }
        return text.str();
    }
}
