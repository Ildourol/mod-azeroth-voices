#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Checks if a chat message begins with a hardcoded or configured Warband / Playerbot
    // command, strategy token, action prefix, or ignored word/letter so that it will
    // not trigger an LLM API call or chat generation.
    bool IsCommandIgnored(std::string const& message,
                          std::vector<std::string> const& customBlacklist = {});

    // Dedicated General/World chatter decisions and tracking
    enum class GeneralSubjectType : uint8_t
    {
        Plain,
        NpcGossip,
        BotGossip,
        Quest,
        Loot,
        Trade,
        Spell
    };

    char const* GeneralSubjectTypeName(GeneralSubjectType type);

    // Roll order: NPC gossip -> Bot gossip -> Plain
    GeneralSubjectType SelectGeneralSubjectType(uint32_t npcGossipChance,
                                               uint32_t botGossipChance,
                                               uint32_t roll100);

    // Identifies Vanilla and Turtle WoW capital cities
    bool IsVanillaCapitalCityZone(uint32_t zoneId);

    // Computes effective trigger chance with city multiplier clamp
    uint32_t CalculateGeneralTriggerChance(uint32_t baseChance, uint32_t cityMultiplier, bool isCity);

    // Per-bot speaker cooldown tracker for General/World chat
    class GeneralSpeakerTracker
    {
    public:
        bool IsOnCooldown(uint64_t guid, uint32_t nowSeconds, uint32_t cooldownSeconds) const;
        void RecordSpeech(uint64_t guid, uint32_t nowSeconds);
        void Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds);
        void Clear();
        size_t Size() const;

    private:
        std::unordered_map<uint64_t, uint32_t> m_lastSpokeAt;
    };

    // Gossip target cooldown tracker (by type:key, e.g. "npc:Hogger" or "bot:Warriorbot")
    class GossipTargetTracker
    {
    public:
        bool IsOnCooldown(std::string const& key, uint32_t nowSeconds, uint32_t cooldownSeconds) const;
        void RecordTarget(std::string const& key, uint32_t nowSeconds);
        void Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds);
        void Clear();
        size_t Size() const;

    private:
        std::unordered_map<std::string, uint32_t> m_lastTargetedAt;
    };

    // Action decided when a player speaks in /say with an NPC or PlayerBot selected
    enum class TargetedNpcSayAction : uint8_t
    {
        NormalSay,              // No NPC selected, or target is PlayerBot -> normal Say handling
        NpcOnly,                // Targeted NPC responds; generic bot replies suppressed; no observer comment
        NpcWithObserver,        // Targeted NPC responds; generic bot replies suppressed; 1 nearby bot comments
        ExplicitBotDirectReply  // Targeted NPC was selected BUT player explicitly named a PlayerBot in message
    };

    char const* TargetedNpcSayActionName(TargetedNpcSayAction action);

    // Determines the /say responder action when a message is sent
    TargetedNpcSayAction DecideTargetedNpcSayAction(
        bool senderIsRealPlayer,
        bool selectedTargetIsNpc,
        bool selectedTargetIsPlayerBot,
        bool explicitPlayerBotNameMention,
        bool botCommentsEnabled,
        uint32_t botCommentChance,
        uint32_t maxBotComments,
        uint32_t roll100);

    struct TargetedNpcObserverPromptInput
    {
        std::string playerName;
        std::string npcName;
        std::string npcRole;
        std::string playerMessage;
        std::string zoneOrArea;
        std::string botName;
    };

    struct TargetedNpcObserverPrompt
    {
        std::string systemPromptExtension;
        std::string userPrompt;
    };

    TargetedNpcObserverPrompt BuildTargetedNpcObserverPrompt(TargetedNpcObserverPromptInput const& input);

    struct ObserverCandidate
    {
        uint64_t guid = 0;
        std::string name;
        float distance = 0.0f;
    };

    // Selects at most maxBotComments closest candidates from eligible bots
    std::vector<ObserverCandidate> SelectObserverCandidates(
        std::vector<ObserverCandidate> const& eligibleBots,
        uint32_t maxBotComments);

    // Guild player-reply controls and session history
    struct GuildSessionKey
    {
        uint64_t playerGuid = 0;
        uint32_t guildId = 0;

        bool operator<(GuildSessionKey const& other) const
        {
            if (playerGuid != other.playerGuid)
                return playerGuid < other.playerGuid;
            return guildId < other.guildId;
        }
        bool operator==(GuildSessionKey const& other) const
        {
            return playerGuid == other.playerGuid && guildId == other.guildId;
        }
    };

    struct GuildSessionTurn
    {
        std::string playerMessage;
        std::string botReply;
        uint32_t timestampSeconds = 0;
    };

    class GuildSessionHistoryRing
    {
    public:
        static constexpr size_t MaxTurns = 12;

        void AddTurn(std::string const& playerMsg, std::string const& botMsg, uint32_t nowSeconds);
        std::string FindRelevantCallback(std::string const& currentMessage) const;
        size_t Size() const { return m_turns.size(); }
        void Clear() { m_turns.clear(); }
        std::deque<GuildSessionTurn> const& Turns() const { return m_turns; }

    private:
        std::deque<GuildSessionTurn> m_turns;
    };

    struct GuildReplyCandidate
    {
        uint64_t guid = 0;
        std::string name;
        bool explicitlyNamed = false;
        bool spokeRecently = false;
        uint32_t weight = 100;
    };

    uint32_t CalculateGuildBotWeight(bool explicitlyNamed, bool spokeRecently, uint32_t penaltyPercent);

    std::vector<GuildReplyCandidate> SelectWeightedGuildCandidates(
        std::vector<GuildReplyCandidate> candidates,
        uint32_t maxCandidates);

    enum class GuildReplyMode : uint8_t
    {
        Single,
        MultiReply,
        Conversation
    };

    char const* GuildReplyModeName(GuildReplyMode mode);

    GuildReplyMode DecideGuildReplyMode(
        uint32_t eligibleBotCount,
        bool conversationEnabled,
        uint32_t conversationChance,
        uint32_t multiReplyChance,
        uint32_t multiAddressedBonus,
        bool multipleBotsNamed,
        uint32_t rollConversation,
        uint32_t rollMultiReply);

    // Guild login greeting timing bands and state
    enum class LoginGreetingBand : uint8_t
    {
        Quick,
        Normal,
        Busy
    };

    char const* LoginGreetingBandName(LoginGreetingBand band);

    LoginGreetingBand SelectLoginGreetingBand(
        uint32_t quickChance,
        uint32_t busyChance,
        uint32_t roll100);

    uint32_t PickLoginGreetingDelaySeconds(
        LoginGreetingBand band,
        uint32_t randomRangeVal);

    struct PendingGuildGreeting
    {
        uint64_t playerGuid = 0;
        uint32_t guildId = 0;
        std::string playerName;
        std::chrono::steady_clock::time_point scheduledAt;
        std::chrono::steady_clock::time_point nextRetry;
        std::chrono::steady_clock::time_point deadline;
        uint32_t delaySeconds = 0;
        bool cancelled = false;
    };
}
