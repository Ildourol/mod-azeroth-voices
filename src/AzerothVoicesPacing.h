#pragma once

#include "AzerothVoicesTypes.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace AzerothVoices
{
    // One delivery timeline per normalized automated-chat channel plus the
    // outdoor zone or map/instance identity. Direct player-directed replies are
    // never paced, and combat-start, event, boss, and dungeon/raid reactions
    // stay outside the quieter reductions.
    std::string NormalizePacingKey(ChatScope scope, std::string const& channelName,
                                   uint32_t mapId, uint32_t instanceId, uint32_t zoneId,
                                   bool instance);

    // The full duration of a generated multi-line exchange is reserved before
    // its first line is scheduled, so a later automated exchange cannot
    // interleave into the middle of it.
    uint32_t EstimateExchangeMilliseconds(size_t lines, bool typingSimulation,
                                          uint32_t baseDelayMilliseconds,
                                          uint32_t perCharacterMilliseconds,
                                          uint32_t assumedCharactersPerLine,
                                          uint32_t interLineGapMilliseconds);

    class DeliveryTimeline
    {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        void Clear();
        size_t Size() const;

        bool CanDeliver(std::string const& key, TimePoint now) const;
        TimePoint NextEligible(std::string const& key) const;

        void Reserve(std::string const& key, TimePoint now, uint32_t durationMilliseconds,
                     uint32_t minimumGapMilliseconds);
        void Prune(TimePoint now);

        // Deterministic, secret-free summary used by tests and status output,
        // for example "world:world|zone:12=4,channel:general=0".
        std::string Describe(TimePoint now) const;

    private:
        std::map<std::string, TimePoint> m_nextEligible;
    };
}
