#include "AzerothVoicesSocial.h"

#include <algorithm>

namespace AzerothVoices
{
    char const* GroupTriggerName(GroupTrigger trigger)
    {
        switch (trigger)
        {
            case GroupTrigger::Idle: return "idle";
            case GroupTrigger::DungeonEntry: return "dungeon-entry";
            case GroupTrigger::ZoneChange: return "zone-change";
            case GroupTrigger::TrashKill: return "trash-kill";
            case GroupTrigger::BossPull: return "boss-pull";
            case GroupTrigger::BossKill: return "boss-kill";
            case GroupTrigger::Wipe: return "wipe";
            case GroupTrigger::Death: return "death";
            case GroupTrigger::CorpseRun: return "corpse-run";
            case GroupTrigger::Resurrect: return "resurrect";
            case GroupTrigger::Loot: return "loot";
            case GroupTrigger::QuestAccept: return "quest-accept";
            case GroupTrigger::QuestObjective: return "quest-objective";
            case GroupTrigger::QuestComplete: return "quest-complete";
            case GroupTrigger::SpellCast: return "spell-cast";
            case GroupTrigger::LowHealth: return "low-health";
            case GroupTrigger::LowMana: return "low-mana";
            case GroupTrigger::NearbyObject: return "nearby-object";
            case GroupTrigger::BotQuestion: return "bot-question";
            case GroupTrigger::PlayerFollowup: return "player-followup";
            case GroupTrigger::BattleCry: return "battle-cry";
            case GroupTrigger::Morale: return "morale";
            case GroupTrigger::Count: break;
        }
        return "idle";
    }

    bool IsRaidOnlyTrigger(GroupTrigger trigger)
    {
        return trigger == GroupTrigger::BattleCry || trigger == GroupTrigger::Morale;
    }

    uint32_t LootChanceForQuality(uint32_t quality, LootChanceTable const& table)
    {
        switch (quality)
        {
            case 2: return std::min<uint32_t>(100, table.uncommon);
            case 3: return std::min<uint32_t>(100, table.rare);
            case 4: return std::min<uint32_t>(100, table.epic);
            default: return quality >= 5 ? std::min<uint32_t>(100, table.legendary) : 0;
        }
    }

    bool WipeDetector::Update(size_t members, size_t deadMembers, bool anyInCombat)
    {
        if (latched)
        {
            if (deadMembers < members || anyInCombat)
                latched = false;
            return false;
        }
        if (members && deadMembers >= members && !anyInCombat)
        {
            latched = true;
            return true;
        }
        return false;
    }

    void WipeDetector::Reset()
    {
        latched = false;
    }

    uint64_t MixQuestLogHash(uint64_t hash, uint64_t value)
    {
        // FNV-1a style mixing; order sensitive so reordered logs still differ.
        hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
        return hash;
    }

    QuestLogEvent DiffQuestLog(QuestLogSnapshot const& previous, QuestLogSnapshot const& current)
    {
        if (!current.known)
            return QuestLogEvent::None;
        if (!previous.known)
            return QuestLogEvent::None;
        if (current.entries > previous.entries)
            return QuestLogEvent::Accepted;
        if (current.entries < previous.entries)
            return QuestLogEvent::Completed;
        if (current.hash != previous.hash)
            return QuestLogEvent::ObjectiveProgressed;
        return QuestLogEvent::None;
    }

    bool ThresholdLatch::Update(bool belowThreshold, bool recovered)
    {
        if (latched)
        {
            if (recovered)
                latched = false;
            return false;
        }
        if (belowThreshold)
        {
            latched = true;
            return true;
        }
        return false;
    }

    void ThresholdLatch::Reset()
    {
        latched = false;
    }

    bool GroupConversationBudget::CanStartConversation(uint32_t eligibleSpeakers) const
    {
        return eligibleSpeakers >= 2 && maximumLines >= 2;
    }

    bool GroupConversationBudget::CanContinue(uint32_t deliveredLines, uint32_t replyTurns) const
    {
        return deliveredLines < maximumLines && replyTurns < maximumReplyTurns;
    }

    bool ShouldSendLoginGreeting(GreetingGateInput const& input)
    {
        return input.realMemberLogin && input.guildedBotOnline &&
            input.recipientCooldownReady && input.chancePassed;
    }
}
