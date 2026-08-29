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

namespace AzerothVoices
{
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
        void HandleEvent(Player* subject, std::string const& eventName, std::string const& detail = "");

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
        };

        struct TelemetryMessage
        {
            uint64_t requestId = 0;
            std::string actorName;
            std::string actorKind;
            std::string scope;
            std::string trigger;
            std::string speakerName;
            std::string text;
            std::string channelName;
            std::string model;
            uint64_t actorGuid = 0;
            int httpStatus = 0;
            uint32_t elapsedMilliseconds = 0;
            uint32_t apiAttempts = 0;
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
        void ProcessEvent(Player* subject, std::string const& eventName, std::string const& detail);
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
                                                  bool ambient, float distanceOverride = 0.0f,
                                                  uint64_t excludedActor = 0) const;
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
        std::deque<TelemetryMessage> m_telemetryRecentMessages;
        std::atomic<bool> m_started;
    };
}
