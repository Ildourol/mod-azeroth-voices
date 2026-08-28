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
        bool paused = false;
        uint32_t workers = 0;
        size_t queued = 0;
        uint32_t inFlight = 0;
        uint64_t accepted = 0;
        uint64_t completed = 0;
        uint64_t failed = 0;
        uint64_t dropped = 0;
        size_t conversations = 0;
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
        struct KnowledgeItem;
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
        std::string BuildEnvironmentContext(ChatRequest const& request) const;
        std::string SelectKnowledge(ChatRequest const& request) const;
        void AddHistory(ChatRequest const& request, std::string const& reply);
        void PruneHistory();
        void LoadKnowledge();

        std::shared_ptr<Config const> m_config;
        std::array<std::deque<ChatRequest>, 4> m_queues;
        std::deque<InboundSignal> m_ingress;
        std::deque<ChatCompletion> m_completions;
        std::vector<ScheduledLine> m_scheduled;
        std::map<std::string, std::deque<HistoryTurn>> m_history;
        std::vector<KnowledgeItem> m_knowledge;

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
        std::atomic<bool> m_started;
    };
}
