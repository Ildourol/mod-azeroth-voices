#pragma once

#include "AzerothVoicesAddon.h"
#include "AzerothVoicesConfig.h"
#include "AzerothVoicesInstanceLore.h"
#include "AzerothVoicesMemory.h"
#include "AzerothVoicesPacing.h"
#include "AzerothVoicesSocial.h"
#include "AzerothVoicesTypes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Creature;
class Player;
class WorldObject;

namespace AzerothVoices
{
    struct StatusSnapshot
    {
        bool enabled = false;
        bool configurationLoaded = false;
        bool apiConfigured = false;
        bool playerbotsLlmEnabled = false;
        bool paused = false;
        uint32_t workers = 0;
        size_t queued = 0;
        uint32_t inFlight = 0;
        uint64_t accepted = 0;
        uint64_t completed = 0;
        uint64_t failed = 0;
        uint64_t dropped = 0;
        size_t conversations = 0;
        size_t surroundingScopes = 0;
        size_t snapshotHistories = 0;
        uint32_t historyStorageMode = 0;
        uint32_t snapshotStorageMode = 0;
        bool historyDatabaseAvailable = false;
        bool snapshotDatabaseAvailable = false;
        bool personalityEnabled = false;
        bool personalityDatabaseAvailable = false;
        size_t personalities = 0;
        size_t personalityGenerationsPending = 0;
        bool sentimentEnabled = false;
        bool sentimentDatabaseAvailable = false;
        size_t sentiments = 0;
        size_t sentimentWritesPending = 0;
        bool addonEnabled = false;
        bool addonDatabaseAvailable = false;
        size_t addonContacts = 0;
        bool proximityEnabled = false;
        size_t proximityScenes = 0;
        size_t proximityZoneCounters = 0;
        bool bossDialogueEnabled = false;
        size_t bossPresences = 0;
        bool groupChatterEnabled = false;
        bool raidChatterEnabled = false;
        size_t groupConversations = 0;
        size_t trackedGroups = 0;
        bool guildChatterEnabled = false;
        bool memoryEnabled = false;
        bool memoryDatabaseAvailable = false;
        size_t memoryPairs = 0;
        size_t memoryWritesPending = 0;
        std::string thinkingMode;
        std::string thinkingProvider;
        std::string thinkingCapability;
        std::string thinkingEffort;
        std::string thinkingAutoKinds;
        uint64_t thinkingFallbacks = 0;
        size_t instanceLoreEntries = 0;
        size_t generalPacingWindows = 0;
        bool ragEnabled = false;
        bool environmentEnabled = false;
        bool snapshotEnabled = false;
        size_t ragEntries = 0;
        size_t ragFiles = 0;
        size_t ragParseFailures = 0;
        size_t scheduledLines = 0;
        std::string endpoint;
        std::string model;
        std::string worldChannelName;
    };

    class Manager final
    {
    public:
        static Manager& Instance();

        void Start();
        void Reload();
        void Stop();
        void Update(uint32_t diff);

        void HandleChat(Player* speaker, ChatScope scope, std::string const& message,
                        std::string const& targetName = "", std::string const& channelName = "");
        void HandleEvent(Player* subject, std::string const& eventName, std::string const& detail = "",
                         uint32_t guildId = 0, uint32_t creatureEntry = 0,
                         uint32_t creatureRank = 0, uint32_t itemQuality = 0);
        void HandleCombatStart(Player* player, Creature* creature);
        void HandlePlayerMapChanged(Player* player);

        bool ForceAmbient(Player* anchor, std::string const& instruction = "");
        bool QueueTest(Player* requester, std::string const& actorName, std::string const& instruction);
        bool GetPersonality(std::string const& actorName, BotPersonality& personality, std::string& message);
        bool GetPersonalityGenerationStatus(std::string const& actorName, std::string& message);
        bool RegeneratePersonality(std::string const& actorName, std::string& message);
        bool DeletePersonality(std::string const& actorName, std::string& message);
        bool DeleteAllPersonalities(std::string& message);
        bool InspectSentiment(std::string const& actorName, std::string const& targetName,
                              std::string& message);
        bool SetSentiment(std::string const& actorName, std::string const& targetName,
                          int32_t score, std::string& message);
        bool ResetSentiment(std::string const& actorName, std::string const& targetName,
                            std::string& message);
        bool ResetAllSentiments(std::string& message);
        bool InspectMemories(std::string const& actorName, std::string const& targetName,
                             std::string& message);
        bool ForgetMemories(std::string const& actorName, std::string const& targetName,
                            std::string& message);
        bool ForgetAllMemories(std::string& message);
        // Native Turtle WoW AzerothVoices addon (.avaddon SAY transport)
        void HandleAvaddonSay(Player* player, std::string const& message);
        void HandleNativeAddonCommand(Player* player, std::string const& requestId,
                                      std::string const& command);

        // Core logical addon handler shared between transports
        bool HandleAddonCommandLogical(Player* player, std::string const& arguments,
                                       std::string const& channel,
                                       std::vector<std::string>& logicalPayloads);

        // Stock Chatter Companion compatibility (.llmc SAY transport)
        // The returned lines are complete `CHATTER_ADDON ...` responses that the
        // caller sends back to the requesting player as system messages.
        bool HandleAddonCommand(Player* player, std::string const& arguments,
                                std::vector<std::string>& responses);
        void OnPlayerLogout(Player* player);
        void ClearHistory();
        void SetPaused(bool paused);
        bool IsPaused() const;
        StatusSnapshot GetStatus() const;

    private:
        Manager();
        ~Manager();
        Manager(Manager const&) = delete;
        Manager& operator=(Manager const&) = delete;

        struct Candidate;
        struct RagItem;
        struct PendingHistoryWrite
        {
            std::string historyKey;
            ChatRequest request;
            std::string reply;
            uint64_t createdUnix = 0;
        };
        struct PendingSnapshotWrite
        {
            std::string actorKey;
            ChatRequest request;
            std::string snapshot;
            uint64_t createdUnix = 0;
        };
        struct InboundSignal
        {
            enum class Kind : uint8_t { Chat, Event };
            Kind kind = Kind::Chat;
            uint64_t playerGuid = 0;
            ChatScope scope = ChatScope::Say;
            std::string message;
            std::string targetName;
            std::string channelName;
            std::string eventName;
            uint32_t guildId = 0;
            uint32_t creatureEntry = 0;
            uint32_t creatureRank = 0;
            uint32_t itemQuality = 0;
        };

        // One proximity scene: a bounded, paced conversation between one and
        // four qualifying speakers observed by a real player.
        struct ProximityScene
        {
            uint64_t id = 0;
            uint64_t anchorPlayerGuid = 0;
            uint32_t mapId = 0;
            uint32_t instanceId = 0;
            uint32_t zoneId = 0;
            std::string zoneKey;
            std::vector<uint64_t> speakers;
            uint32_t maximumLines = 4;
            uint32_t deliveredLines = 0;
            uint32_t replyTurns = 0;
            bool conversation = false;
            bool instance = false;
            std::chrono::steady_clock::time_point expires;
            std::chrono::steady_clock::time_point nextTurn;
        };

        // One boss presence: a classified dungeon/raid boss that a real player
        // is approaching but has not aggroed yet.
        struct BossPresence
        {
            std::string key;
            uint64_t bossGuid = 0;
            uint32_t entry = 0;
            uint32_t mapId = 0;
            uint32_t instanceId = 0;
            uint64_t anchorPlayerGuid = 0;
            std::string name;
            std::string subName;
            std::string mapName;
            std::string area;
            std::string loreContext;
            uint32_t chance = 0;
            uint32_t automaticLines = 0;
            uint32_t directedLines = 0;
            std::chrono::steady_clock::time_point nextAutomatic;
            std::chrono::steady_clock::time_point lastSeen;
            std::map<uint64_t, std::chrono::steady_clock::time_point> directedCooldowns;
            std::deque<std::string> recentLines;
        };

        // Per bot/player memory bucket and its dirty-write queue.
        struct MemoryKey
        {
            uint64_t botGuid = 0;
            uint64_t playerGuid = 0;

            bool operator<(MemoryKey const& other) const
            {
                return botGuid < other.botGuid ||
                    (botGuid == other.botGuid && playerGuid < other.playerGuid);
            }

            bool operator==(MemoryKey const& other) const
            {
                return botGuid == other.botGuid && playerGuid == other.playerGuid;
            }
        };

        struct PendingMemoryWrite
        {
            MemoryKey key;
            MemoryRecord record;
            uint64_t createdUnix = 0;
        };

        // Bounded group/raid conversation, mirroring a proximity scene.
        struct GroupConversation
        {
            uint64_t id = 0;
            uint32_t groupId = 0;
            uint32_t mapId = 0;
            uint32_t instanceId = 0;
            uint64_t anchorPlayerGuid = 0;
            std::vector<uint64_t> speakers;
            uint32_t deliveredLines = 0;
            uint32_t replyTurns = 0;
            uint32_t maximumLines = 3;
            bool conversation = false;
            bool raid = false;
            bool guild = false;
            std::chrono::steady_clock::time_point expires;
            std::chrono::steady_clock::time_point nextTurn;
        };

        // World-thread scratch per tracked group: quest-log diffs, state-callout
        // latches, resurrect detection, and one wipe latch.
        struct GroupRuntimeState
        {
            uint32_t groupId = 0;
            std::chrono::steady_clock::time_point lastSeen;
            std::chrono::steady_clock::time_point nextIdle;
            std::chrono::steady_clock::time_point nextObject;
            std::chrono::steady_clock::time_point nextQuestion;
            std::chrono::steady_clock::time_point nextMorale;
            std::map<uint64_t, QuestLogSnapshot> questLogs;
            std::map<uint64_t, ThresholdLatch> healthLatches;
            std::map<uint64_t, ThresholdLatch> manaLatches;
            std::map<uint64_t, bool> aliveLastScan;
            WipeDetector wipe;
            uint64_t instanceKey = 0;
            uint32_t bossesKilledInInstance = 0;
        };

        enum class PreflightReason : uint8_t
        {
            NoHumanNearby,
            NoAudience,
            NpcNeutral,
            NpcHostile,
            NpcTemporary,
            InvalidActor,
            InvalidScope,
            Combat,
            NpcBoss,
            Unavailable,
            Cooldown,
            RateLimit,
            QueueFull,
            Superseded,
            Count
        };
        struct PersonalityGenerationRecord
        {
            std::string botName;
            std::string state;
            std::string detail;
            uint64_t requestId = 0;
            uint64_t updatedUnix = 0;
        };
        void WorkerLoop();
        void DrainIngress();
        void ProcessChat(Player* speaker, ChatScope scope, std::string const& message,
                         std::string const& targetName, std::string const& channelName);
        bool ProcessPlayerSay(Player* speaker, std::string const& message,
                              std::string const& targetName, std::string const& channelName);
        void ProcessEvent(Player* subject, std::string const& eventName, std::string const& detail,
                          uint32_t guildId, uint32_t creatureEntry, uint32_t creatureRank,
                          uint32_t itemQuality);
        bool QueueDialogue(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                           ChatScope scope, std::string const& channelName,
                           std::string const& trigger, std::string const& message,
                           RequestPriority priority, bool ambient, bool allowFollowup,
                           uint32_t conversationDepth = 0,
                           ProximityScene const* scene = nullptr,
                           BossPresence const* boss = nullptr,
                           bool bossDirected = false,
                           GroupConversation const* groupScene = nullptr);
        bool PreflightDialogue(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                               ChatScope scope, std::string const& channelName,
                               std::string const& trigger, RequestPriority priority,
                               bool ambient);
        bool CanEnqueueDialogue(ActorSnapshot const& actor, RequestPriority priority,
                                bool ambient);
        void RecordPreflightRejection(PreflightReason reason);
        bool Enqueue(ChatRequest request);
        bool PopRequest(ChatRequest& request);
        void DrainCompletions();
        void DeliverScheduled();
        bool Deliver(ScheduledLine const& line);
        void RunAmbient();
        void ScheduleNextAmbient();
        void MaybeQueueFollowup(ChatRequest const& request, std::string const& reply);
        void RunProximity();
        bool QueueProximityScene(Player* anchor, bool forced);
        void MaybeQueueProximityTurn(ChatRequest const& request, std::string const& reply);
        void PruneProximityScenes();
        void RunBossDialogue();
        void PruneBossPresences();
        bool QueueBossDirectedReply(Player* speaker, std::string const& message);
        void NoteBossLineDelivered(ChatRequest const& request, std::string const& text);
        void LoadInstanceLore();
        void PruneGeneralPacing();
        bool ReserveGeneralPacing(ChatScope scope, std::string const& channelName,
                                  ActorSnapshot const& location, size_t lines);
        void RunGroupChatter();
        void RunGuildChatter();
        void PruneGroupAndGuildState();
        bool TryClaimGroupTrigger(Player* subject, std::string const& event,
                                  std::string const& detail, uint32_t creatureEntry,
                                  uint32_t creatureRank, uint32_t itemQuality);
        std::vector<Player*> GroupBotsForEvent(Player* subject, Player*& anchor, bool& raid) const;
        void HandleEventMemories(Player* subject, std::string const& event,
                                 std::string const& detail, uint32_t creatureEntry,
                                 uint32_t creatureRank);
        bool HandleGuildLoginGreeting(Player* member);
        bool QueueGroupChatter(GroupTrigger trigger, Player* anchor, std::string const& detail,
                               bool conversation = false, uint64_t excludedSpeaker = 0);
        void MaybeQueueGroupTurn(ChatRequest const& request, std::string const& reply);
        void MaybeQueueGroupPlayerFollowup(ChatRequest const& request, std::string const& reply);
        std::vector<Player*> CollectGroupBots(Player* realPlayer, bool raid, uint32_t& groupId) const;
        bool GroupTriggerReady(uint32_t groupId, GroupTrigger trigger, uint32_t cooldownSeconds);
        void NoteGroupTrigger(uint32_t groupId, GroupTrigger trigger, uint32_t cooldownSeconds);
        void ScanGroupBots(GroupRuntimeState& state, Player* anchor,
                           std::vector<Player*> const& bots, bool raid);
        void RecordMemory(uint64_t botGuid, uint64_t playerGuid, MemoryType type,
                          MemoryFacts const& facts);
        uint32_t MemoryGenerationChance(MemoryType type) const;
        bool LoadMemories(MemoryKey const& key);
        void CacheMemories(MemoryKey const& key, std::vector<MemoryRecord> records);
        bool AppendMemory(MemoryKey const& key, MemoryRecord record);
        void QueueMemoryWrite(MemoryKey const& key, MemoryRecord const& record);
        void FlushMemoryWrites(bool force = false);
        void DeleteMemoryPair(MemoryKey const& key);
        void DeleteAllMemories();
        std::string BuildMemoryContext(ChatRequest const& request);
        bool ResolveMemoryPair(std::string const& actorName, std::string const& targetName,
                               MemoryKey& key, std::string& message) const;

        ChatRequest BuildRequest(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                 ChatScope scope, std::string const& channelName,
                                 std::string const& trigger, std::string const& message,
                                 RequestPriority priority, bool ambient, bool allowFollowup,
                                 ProximityScene const* scene = nullptr,
                                 BossPresence const* boss = nullptr,
                                 bool bossDirected = false,
                                 GroupConversation const* groupScene = nullptr);
        std::vector<Candidate> CollectCandidates(Player* speaker, ChatScope scope,
                                                  std::string const& targetName,
                                                  std::string const& message,
                                                  bool ambient, bool allowNpcs,
                                                  uint64_t excludedActor = 0,
                                                  uint32_t guildIdOverride = 0,
                                                  WorldObject const* dispositionTarget = nullptr);
        std::string BuildHistoryContext(ChatRequest const& request);
        std::string BuildSurroundingContext(ChatRequest const& request) const;
        std::string BuildEnvironmentContext(ChatRequest const& request) const;
        std::string BuildCurrentSnapshotContext(ChatRequest const& request) const;
        std::string BuildSnapshotHistoryContext(ChatRequest const& request);
        bool LoadPersonality(ActorSnapshot const& actor, BotPersonality& personality,
                             bool requireCurrent = true);
        bool IsPersonalityCurrent(BotPersonality const& personality) const;
        void CachePersonality(BotPersonality personality);
        bool QueuePersonalityGeneration(ActorSnapshot const& actor, bool forced,
                                        PersonalityGenerationMode mode = PersonalityGenerationMode::Full,
                                        BotPersonality const& fixed = BotPersonality());
        void HandlePersonalityCompletion(ChatCompletion const& completion);
        void PersistPersonality(BotPersonality const& personality);
        bool ResolvePersonalityActor(std::string const& actorName, ActorSnapshot& actor,
                                     std::string& message) const;
        void RecordPersonalityGenerationStatus(ActorSnapshot const& actor, std::string state,
                                               uint64_t requestId, std::string detail);
        void CancelPersonalityGeneration(uint64_t characterGuid);
        void DeletePersonalityRecord(uint64_t characterGuid);
        bool LoadSentiment(SentimentKey const& key, SentimentRecord& sentiment);
        void CacheSentiment(SentimentRecord sentiment);
        bool ApplySentimentDecay(SentimentRecord& sentiment);
        void ApplyDeliveredSentiment(ChatRequest const& request);
        void QueueSentimentWrite(SentimentRecord const& sentiment);
        void FlushSentimentWrites(bool force = false, bool ignoreDeadline = false);
        bool ResolveSentimentPair(std::string const& actorName, std::string const& targetName,
                                  SentimentKey& key, std::string& resolvedActorName,
                                  std::string& resolvedTargetName, std::string& message) const;
        std::string SelectRag(ChatRequest const& request) const;
        bool ResolveAddonContact(uint64_t playerGuid, uint64_t botGuid, std::string& botName,
                                 std::string& message) const;
        void RecordAddonContact(uint64_t playerGuid, uint64_t botGuid, std::string const& botName);
        void ForgetAddonContact(uint64_t playerGuid, uint64_t botGuid);
        bool LoadPersonalityForGuid(uint64_t botGuid, std::string const& botName,
                                    BotPersonality& personality, bool requireCurrent);
        void AddHistory(ChatRequest const& request, std::string const& reply);
        void AddSnapshotHistory(ChatRequest const& request, std::string const& snapshot);
        void RecordSurroundingChat(ChatScope scope, std::string const& channelName,
                                   ActorSnapshot const& location, SpeakerSnapshot const& speaker,
                                   std::string const& message);
        void PruneHistory();
        void LoadRag();
        void InitializeDatabaseStorage();
        void LoadDatabaseHistory(ChatRequest const& request);
        void LoadDatabaseSnapshot(ChatRequest const& request);
        void FlushDatabaseWrites(bool force = false);
        void CleanupDatabase();
        void RecordApiResult(ChatCompletion const& completion);
        void RecordGeneratedMessage(ChatCompletion const& completion,
                                    std::vector<std::string> const& lines);
        void ReportTelemetry();

        std::shared_ptr<Config const> m_config;
        std::array<std::deque<ChatRequest>, 4> m_queues;
        std::deque<InboundSignal> m_ingress;
        std::deque<ChatCompletion> m_completions;
        std::vector<ScheduledLine> m_scheduled;
        std::map<std::string, std::deque<HistoryTurn>> m_history;
        std::map<std::string, std::deque<RecentChatLine>> m_surroundingChat;
        std::map<std::string, std::deque<SnapshotRecord>> m_snapshotHistory;
        std::map<uint64_t, BotPersonality> m_personalities;
        std::deque<uint64_t> m_personalityCacheOrder;
        std::map<SentimentKey, SentimentRecord> m_sentiments;
        std::deque<SentimentKey> m_sentimentCacheOrder;
        std::map<std::string, ProximityScene> m_proximityScenes;
        std::map<std::string, uint32_t> m_proximityZoneScenes;
        std::map<uint64_t, std::chrono::steady_clock::time_point> m_proximitySpeakerCooldowns;
        std::map<std::string, BossPresence> m_bossPresences;
        std::map<MemoryKey, std::vector<MemoryRecord>> m_memories;
        std::deque<MemoryKey> m_memoryCacheOrder;
        std::set<MemoryKey> m_databaseLoadedMemoryPairs;
        std::deque<PendingMemoryWrite> m_pendingMemoryWrites;
        std::map<std::string, GroupConversation> m_groupConversations;
        std::map<uint32_t, GroupRuntimeState> m_groupStates;
        std::map<std::string, std::chrono::steady_clock::time_point> m_groupTriggerCooldowns;
        std::map<uint64_t, std::chrono::steady_clock::time_point> m_guildGreetingCooldowns;
        std::map<uint64_t, std::chrono::steady_clock::time_point> m_guildReplyDebounce;
        std::map<uint32_t, std::pair<uint64_t, std::chrono::steady_clock::time_point>> m_guildLastSpeaker;
        std::map<uint64_t, std::pair<uint32_t, std::string>> m_lastPlayerMap;
        DeliveryTimeline m_generalPacing;
        InstanceLoreRegistry m_instanceLore;
        std::vector<RagItem> m_rag;
        size_t m_ragFiles = 0;
        size_t m_ragParseFailures = 0;
        std::set<std::string> m_databaseLoadedHistoryKeys;
        std::set<std::string> m_databaseLoadedSnapshotKeys;
        std::set<uint64_t> m_databaseLoadedPersonalityGuids;
        std::set<SentimentKey> m_databaseLoadedSentimentPairs;
        std::unordered_map<uint64_t, uint64_t> m_pendingPersonalityRequests;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> m_personalityRetryAfter;
        std::unordered_map<uint64_t, PersonalityGenerationRecord> m_personalityGenerationStatus;
        std::deque<uint64_t> m_personalityGenerationStatusOrder;
        std::deque<PendingHistoryWrite> m_pendingHistoryWrites;
        std::deque<PendingSnapshotWrite> m_pendingSnapshotWrites;
        std::map<SentimentKey, SentimentRecord> m_pendingSentimentWrites;
        bool m_historyDatabaseAvailable = false;
        bool m_snapshotDatabaseAvailable = false;
        bool m_personalityDatabaseAvailable = false;
        bool m_sentimentDatabaseAvailable = false;
        bool m_addonDatabaseAvailable = false;
        bool m_memoryDatabaseAvailable = false;
        bool m_instanceLoreLoaded = false;
        uint64_t m_nextGroupConversationId = 1;
        Addon::RequestReassembler m_addonReassembler;

        mutable std::mutex m_queueMutex;
        mutable std::mutex m_ingressMutex;
        mutable std::mutex m_completionMutex;
        std::condition_variable m_queueReady;
        std::vector<std::thread> m_workers;
        std::unordered_map<uint64_t, uint64_t> m_latestRequestByActor;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> m_actorCooldowns;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> m_speakerCooldowns;
        std::map<std::string, std::chrono::steady_clock::time_point> m_eventCooldowns;
        std::deque<std::chrono::steady_clock::time_point> m_requestBudget;

        std::atomic<bool> m_stopping;
        std::atomic<bool> m_paused;
        std::atomic<uint32_t> m_inFlight;
        std::atomic<uint64_t> m_nextRequestId;
        std::atomic<uint64_t> m_accepted;
        std::atomic<uint64_t> m_completed;
        std::atomic<uint64_t> m_failed;
        std::atomic<uint64_t> m_dropped;
        std::chrono::steady_clock::time_point m_nextAmbient;
        std::chrono::steady_clock::time_point m_lastErrorLog;
        uint32_t m_suppressedErrors;
        std::chrono::steady_clock::time_point m_telemetryWindowStarted;
        std::chrono::steady_clock::time_point m_nextHistoryPrune;
        std::chrono::steady_clock::time_point m_nextDatabaseFlush;
        std::chrono::steady_clock::time_point m_nextSentimentDatabaseFlush;
        std::chrono::steady_clock::time_point m_nextDatabaseCleanup;
        std::chrono::steady_clock::time_point m_nextProximityOutdoorScan;
        std::chrono::steady_clock::time_point m_nextProximityInstanceScan;
        std::chrono::steady_clock::time_point m_nextBossScan;
        std::chrono::steady_clock::time_point m_nextGroupScan;
        std::chrono::steady_clock::time_point m_nextGuildScan;
        std::chrono::steady_clock::time_point m_nextMemoryFlush;
        uint64_t m_nextProximitySceneId = 1;
        uint64_t m_telemetryApiCalls = 0;
        uint64_t m_telemetrySuccessfulResults = 0;
        uint64_t m_telemetryFailedResults = 0;
        uint64_t m_telemetryGeneratedMessages = 0;
        std::atomic<uint64_t> m_thinkingFallbacks{ 0 };
        std::array<uint64_t, static_cast<size_t>(PreflightReason::Count)> m_preflightRejections{};
        std::atomic<bool> m_started;
    };
}
