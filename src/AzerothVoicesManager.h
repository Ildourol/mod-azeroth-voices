#pragma once

#include "AzerothVoicesConfig.h"
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

class Player;
class WorldObject;

namespace AzerothVoices
{
    struct NaturalCommandAuditRecord
    {
        uint64_t createdUnix = 0;
        uint64_t requestId = 0;
        uint64_t playerGuid = 0;
        uint64_t botGuid = 0;
        std::string action;
        std::string arguments;
        std::string source;
        std::string result;
        double confidence = 0.0;
        uint32_t latencyMilliseconds = 0;
    };

    struct StatusSnapshot
    {
        bool enabled = false;
        bool configurationLoaded = false;
        bool apiConfigured = false;
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
        bool ragEnabled = false;
        bool environmentEnabled = false;
        bool snapshotEnabled = false;
        bool naturalCommandsEnabled = false;
        size_t naturalCommandActions = 0;
        uint32_t naturalCommandShortlistMaximum = 20;
        size_t naturalCommandEffectiveShortlistMaximum = 0;
        uint32_t naturalCommandMaximumRecipients = 1;
        uint32_t naturalCommandMaximumActions = 1;
        size_t naturalCommandsPending = 0;
        size_t naturalConfirmationsPending = 0;
        bool naturalCommandPrefixConfigured = false;
        uint64_t naturalClassified = 0;
        uint64_t naturalDispatched = 0;
        uint64_t naturalRejected = 0;
        uint64_t naturalExpired = 0;
        uint64_t naturalConsidered = 0;
        uint64_t naturalLocalFastPath = 0;
        uint64_t naturalClassifierQueued = 0;
        size_t naturalAverageShortlist = 0;
        size_t naturalAveragePromptCharacters = 0;
        uint32_t naturalAverageClassifierLatencyMilliseconds = 0;
        std::string naturalCommandModel;
        std::string naturalAcknowledgementMode;
        std::string naturalMostUsedActions;
        bool naturalAuditEnabled = false;
        size_t naturalAuditRecords = 0;
        std::string naturalLastFailure;
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
                         uint32_t guildId = 0);

        bool ForceAmbient(Player* anchor, std::string const& instruction = "");
        bool QueueTest(Player* requester, std::string const& actorName, std::string const& instruction);
        bool GetPersonality(std::string const& actorName, BotPersonality& personality, std::string& message);
        bool GetPersonalityGenerationStatus(std::string const& actorName, std::string& message);
        bool RegeneratePersonality(std::string const& actorName, std::string& message);
        bool DeletePersonality(std::string const& actorName, std::string& message);
        bool DeleteAllPersonalities(std::string& message);
        void ClearHistory();
        void SetPaused(bool paused);
        bool IsPaused() const;
        StatusSnapshot GetStatus() const;
        std::vector<NaturalCommandAuditRecord> GetNaturalCommandAudit(size_t maximum) const;
        void ClearNaturalCommandAudit();

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
        struct PendingNaturalConfirmation
        {
            struct Action
            {
                std::string action;
                std::string arguments;
                double confidence = 1.0;
            };
            uint64_t speakerGuid = 0;
            std::vector<uint64_t> botGuids;
            std::vector<Action> actions;
            ChatScope scope = ChatScope::Whisper;
            std::string addressing;
            std::string source = "local";
            uint64_t requestId = 0;
            uint32_t latencyMilliseconds = 0;
            std::string acknowledgement;
            ChatRequest acknowledgementRequest;
            std::chrono::steady_clock::time_point expires;
        };
        struct NaturalCommandTelemetryWindow
        {
            uint64_t considered = 0;
            uint64_t localFastPath = 0;
            uint64_t classifierQueued = 0;
            uint64_t classifierResults = 0;
            uint64_t classifierLatencyMilliseconds = 0;
            uint64_t shortlistActions = 0;
            uint64_t promptCharacters = 0;
            uint64_t conversation = 0;
            uint64_t unsupported = 0;
            uint64_t lowConfidence = 0;
            uint64_t invalidDecision = 0;
            uint64_t rejected = 0;
            uint64_t confirmationRequired = 0;
            uint64_t confirmationConfirmed = 0;
            uint64_t confirmationCancelled = 0;
            uint64_t confirmationExpired = 0;
        };
        void WorkerLoop();
        void DrainIngress();
        void ProcessChat(Player* speaker, ChatScope scope, std::string const& message,
                         std::string const& targetName, std::string const& channelName);
        bool TryHandleNaturalCommand(Player* speaker, ChatScope scope,
                                     std::string const& message,
                                     std::string const& targetName,
                                     std::string const& channelName);
        std::vector<Player*> ResolveNaturalCommandBots(Player* speaker, ChatScope scope,
                                                       std::string const& message,
                                                       std::string const& targetName,
                                                       std::string& addressing,
                                                       std::string& addressedMessage) const;
        bool NaturalCommandTargetValid(Player* speaker, Player* bot, ChatScope scope,
                                       std::string const& addressing) const;
        bool QueueNaturalCommandInterpretation(Player* speaker,
                                               std::vector<Player*> const& bots,
                                               ChatScope scope,
                                               std::string const& channelName,
                                               std::string const& addressing,
                                               std::string const& message);
        void HandleNaturalCommandCompletion(ChatCompletion const& completion);
        bool ExecuteNaturalCommand(Player* speaker, Player* bot,
                                   std::string const& action,
                                   std::string const& arguments,
                                   ChatScope scope,
                                   std::string const& addressing,
                                   bool confirmed = false,
                                   bool sendFeedback = true,
                                   std::string const& source = "local",
                                   double confidence = 1.0,
                                   uint64_t requestId = 0,
                                   uint32_t latencyMilliseconds = 0);
        bool ExecuteNaturalCommandBatch(Player* speaker,
                                        std::vector<Player*> const& bots,
                                        std::vector<PendingNaturalConfirmation::Action> const& actions,
                                        ChatScope scope, std::string const& addressing,
                                        bool confirmed = false,
                                        std::string const& source = "local",
                                        uint64_t requestId = 0,
                                        uint32_t latencyMilliseconds = 0,
                                        std::string const& acknowledgement = "",
                                        ChatRequest const* acknowledgementRequest = nullptr);
        void SendNaturalCommandFeedback(Player* speaker, std::string const& message) const;
        void RejectNaturalCommand(Player* speaker, std::string const& reason);
        void RecordNaturalCommandAudit(uint64_t playerGuid, uint64_t botGuid,
                                       std::string const& action,
                                       std::string const& arguments,
                                       std::string const& source,
                                       std::string const& result,
                                       double confidence, uint64_t requestId,
                                       uint32_t latencyMilliseconds);
        void ScheduleNaturalCommandAcknowledgement(ChatRequest const& request,
                                                   std::string const& acknowledgement);
        std::string NaturalCommandMostUsedActions(size_t maximum) const;
        void ProcessEvent(Player* subject, std::string const& eventName, std::string const& detail,
                          uint32_t guildId);
        bool QueueDialogue(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                           ChatScope scope, std::string const& channelName,
                           std::string const& trigger, std::string const& message,
                           RequestPriority priority, bool ambient, bool allowFollowup,
                           uint32_t conversationDepth = 0);
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

        ChatRequest BuildRequest(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                 ChatScope scope, std::string const& channelName,
                                 std::string const& trigger, std::string const& message,
                                 RequestPriority priority, bool ambient, bool allowFollowup);
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
        bool QueuePersonalityGeneration(ActorSnapshot const& actor, bool forced);
        void HandlePersonalityCompletion(ChatCompletion const& completion);
        void PersistPersonality(BotPersonality const& personality);
        bool ResolvePersonalityActor(std::string const& actorName, ActorSnapshot& actor,
                                     std::string& message) const;
        void RecordPersonalityGenerationStatus(ActorSnapshot const& actor, std::string state,
                                               uint64_t requestId, std::string detail);
        void CancelPersonalityGeneration(uint64_t characterGuid);
        void DeletePersonalityRecord(uint64_t characterGuid);
        std::string SelectRag(ChatRequest const& request) const;
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
        std::vector<RagItem> m_rag;
        size_t m_ragFiles = 0;
        size_t m_ragParseFailures = 0;
        std::set<std::string> m_databaseLoadedHistoryKeys;
        std::set<std::string> m_databaseLoadedSnapshotKeys;
        std::set<uint64_t> m_databaseLoadedPersonalityGuids;
        std::unordered_map<uint64_t, uint64_t> m_pendingPersonalityRequests;
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> m_personalityRetryAfter;
        std::unordered_map<uint64_t, PersonalityGenerationRecord> m_personalityGenerationStatus;
        std::deque<uint64_t> m_personalityGenerationStatusOrder;
        std::deque<PendingHistoryWrite> m_pendingHistoryWrites;
        std::deque<PendingSnapshotWrite> m_pendingSnapshotWrites;
        bool m_historyDatabaseAvailable = false;
        bool m_snapshotDatabaseAvailable = false;
        bool m_personalityDatabaseAvailable = false;

        mutable std::mutex m_queueMutex;
        mutable std::mutex m_ingressMutex;
        mutable std::mutex m_completionMutex;
        std::condition_variable m_queueReady;
        std::vector<std::thread> m_workers;
        std::unordered_map<uint64_t, uint64_t> m_latestRequestByActor;
        std::unordered_map<uint64_t, std::deque<uint64_t>> m_pendingNaturalCommandsByActor;
        std::unordered_map<uint64_t, PendingNaturalConfirmation> m_pendingNaturalConfirmations;
        std::map<std::string, uint64_t> m_naturalActionUsage;
        std::deque<NaturalCommandAuditRecord> m_naturalCommandAudit;
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
        std::chrono::steady_clock::time_point m_nextDatabaseCleanup;
        uint64_t m_telemetryApiCalls = 0;
        uint64_t m_telemetrySuccessfulResults = 0;
        uint64_t m_telemetryFailedResults = 0;
        uint64_t m_telemetryGeneratedMessages = 0;
        uint64_t m_naturalClassified = 0;
        uint64_t m_naturalDispatched = 0;
        uint64_t m_naturalRejected = 0;
        uint64_t m_naturalExpired = 0;
        uint64_t m_naturalConsidered = 0;
        uint64_t m_naturalLocalFastPath = 0;
        uint64_t m_naturalClassifierQueued = 0;
        uint64_t m_naturalClassifierResults = 0;
        uint64_t m_naturalClassifierLatencyMilliseconds = 0;
        uint64_t m_naturalShortlistActions = 0;
        uint64_t m_naturalPromptCharacters = 0;
        NaturalCommandTelemetryWindow m_naturalTelemetry;
        std::string m_naturalLastFailure;
        std::array<uint64_t, static_cast<size_t>(PreflightReason::Count)> m_preflightRejections{};
        std::atomic<bool> m_started;
    };
}
