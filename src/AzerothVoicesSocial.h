#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace AzerothVoices
{
    // Party/raid/guild chatter decisions, kept free of game headers so they can be
    // unit tested without a worldserver.
    enum class GroupTrigger : uint8_t
    {
        Idle,
        DungeonEntry,
        ZoneChange,
        TrashKill,
        BossPull,
        BossKill,
        Wipe,
        Death,
        CorpseRun,
        Resurrect,
        Loot,
        QuestAccept,
        QuestObjective,
        QuestComplete,
        SpellCast,
        LowHealth,
        LowMana,
        NearbyObject,
        BotQuestion,
        PlayerFollowup,
        BattleCry,
        Morale,
        Count
    };

    char const* GroupTriggerName(GroupTrigger trigger);

    // Raid flavour triggers only fire on raid maps.
    bool IsRaidOnlyTrigger(GroupTrigger trigger);

    // Loot quality: 0 poor, 1 common, 2 uncommon, 3 rare, 4 epic, 5+ legendary.
    struct LootChanceTable
    {
        uint32_t uncommon = 0;
        uint32_t rare = 0;
        uint32_t epic = 0;
        uint32_t legendary = 0;
    };

    // Returns 0 for qualities that never trigger loot chatter.
    uint32_t LootChanceForQuality(uint32_t quality, LootChanceTable const& table);

    // Fires once per wipe: latches while every member is dead and nothing is in
    // combat, and only re-arms after the group is alive and fighting again.
    struct WipeDetector
    {
        bool latched = false;
        bool Update(size_t members, size_t deadMembers, bool anyInCombat);
        void Reset();
    };

    // Quest-log diffing: PlayerBots emit no accept/objective packets, so the
    // group scan compares a per-bot snapshot instead.
    struct QuestLogSnapshot
    {
        uint32_t entries = 0;
        uint64_t hash = 0;
        bool known = false;
    };

    enum class QuestLogEvent : uint8_t
    {
        None,
        Accepted,
        ObjectiveProgressed,
        Completed
    };

    QuestLogEvent DiffQuestLog(QuestLogSnapshot const& previous, QuestLogSnapshot const& current);
    uint64_t MixQuestLogHash(uint64_t hash, uint64_t value);

    // Low-health / low-mana callout latch with recovery hysteresis: a callout
    // fires when the actor drops below the threshold and re-arms only after it
    // climbs back above the recovery line.
    struct ThresholdLatch
    {
        bool latched = false;
        bool Update(bool belowThreshold, bool recovered);
        void Reset();
    };

    // Bounded group conversation budget, mirroring the proximity scene rules.
    struct GroupConversationBudget
    {
        uint32_t maximumLines = 3;
        uint32_t maximumParticipants = 2;
        uint32_t maximumReplyTurns = 3;
        uint32_t turnGapSeconds = 6;
        uint32_t replyWindowSeconds = 30;

        bool CanStartConversation(uint32_t eligibleSpeakers) const;
        bool CanContinue(uint32_t deliveredLines, uint32_t replyTurns) const;
    };

    struct GreetingGateInput
    {
        bool realMemberLogin = false;
        bool guildedBotOnline = false;
        bool recipientCooldownReady = false;
        bool chancePassed = false;
    };

    bool ShouldSendLoginGreeting(GreetingGateInput const& input);
}
