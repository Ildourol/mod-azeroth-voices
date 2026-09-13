#include "AzerothVoicesPartyGate.h"

#include <algorithm>
#include <cctype>

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
    }

    char const* PartyGatePolicyName(PartyGatePolicy policy)
    {
        switch (policy)
        {
            case PartyGatePolicy::Bypass: return "bypass";
            case PartyGatePolicy::Filler: return "filler";
            case PartyGatePolicy::Contextual: return "contextual";
            case PartyGatePolicy::Responsive: return "responsive";
            case PartyGatePolicy::Urgent: return "urgent";
        }
        return "bypass";
    }

    PartyGatePolicy PartyGatePolicyForTrigger(std::string const& trigger)
    {
        std::string t = Lower(trigger);

        // Strip "group:" or "raid:" prefix if present for uniform token comparison
        if (t.rfind("group:", 0) == 0)
            t = t.substr(6);
        else if (t.rfind("raid:", 0) == 0)
            t = t.substr(5);

        // Urgent: critical combat events, low resources, wipes, boss pulls
        if (t == "low_health" || t == "low_mana" || t == "aggro_loss" ||
            t == "boss_pull" || t == "wipe" || t == "battle_cry")
            return PartyGatePolicy::Urgent;

        // Responsive: direct replies to player, follow-ups to player questions
        if (t == "player_directed" || t == "player_followup" || t == "name_directed" ||
            t == "player-reply" || t == "name-mention" || t == "direct-address" ||
            t == "direct" || t == "targeted-npc-observer")
            return PartyGatePolicy::Responsive;

        // Contextual: game progression events (dungeon entry, zone changes, kills, loot, quests, resurrections)
        if (t == "dungeon_entry" || t == "zone_change" || t == "trash_kill" ||
            t == "kill" || t == "creature_killed" || t == "player_defeated" ||
            t == "loot" || t == "item_looted" || t == "quest_accept" ||
            t == "quest_accepted" || t == "quest_objective" || t == "quest_progress" ||
            t == "quest_complete" || t == "quest_completed" || t == "quest_rewarded" ||
            t == "resurrect" || t == "resurrected" || t == "death" ||
            t == "corpse_run" || t == "spell_cast" || t == "spell_learned" ||
            t == "boss_kill" || t == "morale")
            return PartyGatePolicy::Contextual;

        // Filler: ambient remark, bot questions, object remarks, idle chatter
        if (t == "idle" || t == "idle_chatter" || t == "bot_question" ||
            t == "nearby_object" || t == "used_object" || t == "ambient" ||
            t == "random")
            return PartyGatePolicy::Filler;

        // Default: If trigger wasn't empty and had a prefix, treat as Contextual
        if (!t.empty())
            return PartyGatePolicy::Contextual;

        return PartyGatePolicy::Bypass;
    }

    uint32_t PartyGateGapSeconds(PartyGatePolicy policy, Config const& config)
    {
        switch (policy)
        {
            case PartyGatePolicy::Bypass: return 0;
            case PartyGatePolicy::Filler: return config.partyGateFillerMinGapSeconds;
            case PartyGatePolicy::Contextual: return config.partyGateContextualMinGapSeconds;
            case PartyGatePolicy::Responsive: return config.partyGateResponsiveMinGapSeconds;
            case PartyGatePolicy::Urgent: return config.partyGateUrgentMinGapSeconds;
        }
        return 0;
    }

    std::string PartyPacingKey(uint32_t groupId, int32_t subgroup)
    {
        if (!groupId)
            return "";
        std::string key = "party:" + std::to_string(groupId);
        if (subgroup >= 0)
            key += ":subgroup:" + std::to_string(subgroup);
        return key;
    }

    bool ShouldDeferPartyFiller(DeliveryTimeline const& timeline,
                                std::string const& partyKey,
                                PartyGatePolicy policy,
                                uint32_t deferThresholdSeconds,
                                std::chrono::steady_clock::time_point now,
                                uint32_t* outWaitSeconds)
    {
        if (policy != PartyGatePolicy::Filler || partyKey.empty())
            return false;

        auto nextEligible = timeline.NextEligible(partyKey);
        if (nextEligible <= now)
            return false;

        uint32_t const waitSeconds = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(nextEligible - now).count());

        if (outWaitSeconds)
            *outWaitSeconds = waitSeconds;

        return waitSeconds > deferThresholdSeconds;
    }

    std::chrono::steady_clock::time_point CalculatePartyScheduledTime(
        DeliveryTimeline const& timeline,
        std::string const& partyKey,
        PartyGatePolicy policy,
        std::chrono::steady_clock::time_point requestedTime,
        std::chrono::steady_clock::time_point now,
        uint32_t maxFillerDelaySeconds)
    {
        if (policy == PartyGatePolicy::Bypass || partyKey.empty())
            return requestedTime;

        if (policy == PartyGatePolicy::Urgent)
        {
            // Urgent messages bypass normal next-slot waiting (Section 27)
            return requestedTime;
        }

        auto nextEligible = timeline.NextEligible(partyKey);
        auto scheduled = std::max(requestedTime, nextEligible);

        if (policy == PartyGatePolicy::Filler)
        {
            // Section 29: stale-message safeguard bounding extra post-generation filler delay
            auto maxFillerTime = now + std::chrono::seconds(maxFillerDelaySeconds);
            scheduled = std::min(scheduled, maxFillerTime);
        }

        return scheduled;
    }
}
