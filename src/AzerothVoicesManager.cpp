#include "AzerothVoicesManager.h"

#include "AzerothVoicesProvider.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "WorldPacket.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <list>
#include <random>
#include <set>
#include <sstream>

namespace AzerothVoices
{
    struct Manager::Candidate
    {
        ActorSnapshot actor;
        uint32_t chance = 0;
        int score = 0;
    };

    struct Manager::KnowledgeItem
    {
        std::vector<std::string> keywords;
        std::string text;
    };

    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::mt19937& RandomEngine()
        {
            static thread_local std::mt19937 engine(std::random_device{}());
            return engine;
        }

        uint32_t RandomUInt(uint32_t minimum, uint32_t maximum)
        {
            if (maximum <= minimum)
                return minimum;
            return std::uniform_int_distribution<uint32_t>(minimum, maximum)(RandomEngine());
        }

        bool Roll(uint32_t chance)
        {
            return chance >= 100 || (chance > 0 && RandomUInt(1, 100) <= chance);
        }

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        void ReplaceAll(std::string& value, std::string const& from, std::string const& to)
        {
            if (from.empty())
                return;
            size_t position = 0;
            while ((position = value.find(from, position)) != std::string::npos)
            {
                value.replace(position, from.size(), to);
                position += to.size();
            }
        }

        std::string RaceName(uint8_t race)
        {
            switch (race)
            {
                case RACE_HUMAN: return "human";
                case RACE_ORC: return "orc";
                case RACE_DWARF: return "dwarf";
                case RACE_NIGHTELF: return "night elf";
                case RACE_UNDEAD: return "undead";
                case RACE_TAUREN: return "tauren";
                case RACE_GNOME: return "gnome";
                case RACE_TROLL: return "troll";
                case RACE_GOBLIN: return "goblin";
                case RACE_HIGH_ELF: return "high elf";
                default: return "unknown race";
            }
        }

        std::string ClassName(uint8_t playerClass)
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR: return "warrior";
                case CLASS_PALADIN: return "paladin";
                case CLASS_HUNTER: return "hunter";
                case CLASS_ROGUE: return "rogue";
                case CLASS_PRIEST: return "priest";
                case CLASS_SHAMAN: return "shaman";
                case CLASS_MAGE: return "mage";
                case CLASS_WARLOCK: return "warlock";
                case CLASS_DRUID: return "druid";
                default: return "adventurer";
            }
        }

        std::string GenderName(uint8_t gender)
        {
            if (gender == GENDER_MALE)
                return "male";
            if (gender == GENDER_FEMALE)
                return "female";
            return "unknown gender";
        }

        std::string TeamName(Team team)
        {
            if (team == ALLIANCE)
                return "Alliance";
            if (team == HORDE)
                return "Horde";
            return "neutral";
        }

        std::string ScopeName(ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::Say: return "say";
                case ChatScope::Yell: return "yell";
                case ChatScope::Whisper: return "whisper";
                case ChatScope::Party: return "party";
                case ChatScope::Raid: return "raid";
                case ChatScope::Guild: return "guild";
                case ChatScope::Officer: return "officer";
                case ChatScope::Channel: return "channel";
                case ChatScope::World: return "world";
            }
            return "say";
        }

        bool IsScopeEnabled(Config const& config, ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::Say: return config.sayReplies;
                case ChatScope::Yell: return config.yellReplies;
                case ChatScope::Whisper: return config.whisperReplies;
                case ChatScope::Party: return config.partyReplies;
                case ChatScope::Raid: return config.raidReplies;
                case ChatScope::Guild: return config.guildReplies;
                case ChatScope::Officer: return config.officerReplies;
                case ChatScope::World: return config.worldReplies;
                case ChatScope::Channel: return config.customChannelReplies;
            }
            return false;
        }

        bool IsBlockedChannel(Config const& config, ChatScope scope, std::string const& channelName)
        {
            std::string scopeLower = Lower(ScopeName(scope));
            std::string channelLower = Lower(channelName);
            for (std::string blocked : config.blockedChannels)
            {
                blocked = Lower(Trim(blocked));
                if (!blocked.empty() && (blocked == scopeLower || blocked == channelLower))
                    return true;
            }
            return false;
        }

        bool IsBlacklisted(Config const& config, std::string const& message)
        {
            std::string text = Lower(Trim(message));
            for (std::string token : config.commandBlacklist)
            {
                token = Lower(Trim(token));
                if (!token.empty() && text.compare(0, token.size(), token) == 0)
                    return true;
            }
            return false;
        }

        std::string GuildName(Player const* player)
        {
            if (!player || !player->GetGuildId())
                return "";
            Guild* guild = sGuildMgr.GetGuildById(player->GetGuildId());
            return guild ? guild->GetName() : "";
        }

        void FillLocation(WorldObject const* object, std::string& area, std::string& zone,
                          std::string& mapName, uint32_t& mapId, uint32_t& areaId, uint32_t& zoneId)
        {
            if (!object)
                return;

            mapId = object->GetMapId();
            areaId = object->GetAreaId();
            zoneId = object->GetZoneId();
            if (Map const* map = object->FindMap())
                mapName = map->GetMapName();
            if (AreaEntry const* entry = AreaEntry::GetById(areaId))
                area = entry->Name ? entry->Name : "";
            if (AreaEntry const* entry = AreaEntry::GetById(zoneId))
                zone = entry->Name ? entry->Name : "";
            if (zone.empty())
                zone = area;
        }

        SpeakerSnapshot SnapshotSpeaker(Player const* player)
        {
            SpeakerSnapshot result;
            if (!player)
                return result;
            result.guid = player->GetObjectGuid().GetRawValue();
            result.name = player->GetName();
            result.race = RaceName(player->GetRace());
            result.className = ClassName(player->GetClass());
            result.gender = GenderName(player->GetGender());
            result.faction = TeamName(player->GetTeam());
            result.guild = GuildName(player);
            result.groupStatus = player->GetGroup() ? "in a group" : "solo";
            result.level = player->GetLevel();
            result.isBot = Script_IsAIControlled(player);
            return result;
        }

        ActorSnapshot SnapshotBot(Player const* player)
        {
            ActorSnapshot result;
            result.kind = ActorKind::PlayerBot;
            if (!player)
                return result;
            result.guid = player->GetObjectGuid().GetRawValue();
            result.anchorPlayerGuid = result.guid;
            result.name = player->GetName();
            result.race = RaceName(player->GetRace());
            result.className = ClassName(player->GetClass());
            result.gender = GenderName(player->GetGender());
            result.faction = TeamName(player->GetTeam());
            result.guild = GuildName(player);
            result.groupStatus = player->GetGroup() ? "in a group" : "solo";
            result.level = player->GetLevel();
            result.inCombat = player->IsInCombat();
            FillLocation(player, result.area, result.zone, result.map,
                         result.mapId, result.areaId, result.zoneId);
            return result;
        }

        ActorSnapshot SnapshotCreature(Creature const* creature, Player const* anchor)
        {
            ActorSnapshot result;
            result.kind = ActorKind::Creature;
            if (!creature)
                return result;
            result.guid = creature->GetObjectGuid().GetRawValue();
            result.anchorPlayerGuid = anchor ? anchor->GetObjectGuid().GetRawValue() : 0;
            result.name = creature->GetName();
            result.race = "NPC";
            result.className = "NPC";
            result.gender = GenderName(creature->GetGender());
            result.faction = anchor && creature->IsFriendlyTo(anchor) ? "friendly" : "neutral";
            result.groupStatus = "nearby NPC";
            result.level = creature->GetLevel();
            result.inCombat = creature->IsInCombat();
            FillLocation(creature, result.area, result.zone, result.map,
                         result.mapId, result.areaId, result.zoneId);
            return result;
        }

        std::string Expand(std::string value, ChatRequest const& request)
        {
            ReplaceAll(value, "<bot name>", request.actor.name);
            ReplaceAll(value, "<bot level>", std::to_string(request.actor.level));
            ReplaceAll(value, "<bot race>", request.actor.race);
            ReplaceAll(value, "<bot class>", request.actor.className);
            ReplaceAll(value, "<bot gender>", request.actor.gender);
            ReplaceAll(value, "<bot faction>", request.actor.faction);
            ReplaceAll(value, "<bot zone>", request.actor.zone);
            ReplaceAll(value, "<bot subzone>", request.actor.area);
            ReplaceAll(value, "<bot map>", request.actor.map);
            ReplaceAll(value, "<bot guild>", request.actor.guild);
            ReplaceAll(value, "<bot type>", request.actor.kind == ActorKind::Creature ? "NPC" : "playerbot");
            ReplaceAll(value, "<expansion name>", "Turtle WoW");
            ReplaceAll(value, "<sender name>", request.speaker.name);
            ReplaceAll(value, "<receiver name>", request.actor.name);
            ReplaceAll(value, "<other name>", request.speaker.name);
            ReplaceAll(value, "<other level>", std::to_string(request.speaker.level));
            ReplaceAll(value, "<other race>", request.speaker.race);
            ReplaceAll(value, "<other class>", request.speaker.className);
            ReplaceAll(value, "<other gender>", request.speaker.gender);
            ReplaceAll(value, "<other faction>", request.speaker.faction);
            ReplaceAll(value, "<other type>", request.speaker.isBot ? "playerbot" : "player");
            ReplaceAll(value, "<unit type>", request.speaker.isBot ? "playerbot" : "player");
            ReplaceAll(value, "<unit name>", request.speaker.name);
            ReplaceAll(value, "<unit subname>", "");
            ReplaceAll(value, "<unit level>", std::to_string(request.speaker.level));
            ReplaceAll(value, "<unit gender>", request.speaker.gender);
            ReplaceAll(value, "<unit race>", request.speaker.race);
            ReplaceAll(value, "<unit faction>", request.speaker.faction);
            ReplaceAll(value, "<unit class>", request.speaker.className);
            ReplaceAll(value, "<initial message>", request.incomingMessage);
            ReplaceAll(value, "<channel name>", request.channelName.empty() ? ScopeName(request.scope) : request.channelName);
            ReplaceAll(value, "<trigger>", request.trigger);
            return value;
        }

        std::string HistoryKey(Config const& config, ActorSnapshot const& actor,
                               SpeakerSnapshot const& speaker, ChatScope scope, std::string const& channel)
        {
            std::ostringstream key;
            key << static_cast<unsigned>(actor.kind) << ':' << actor.guid;
            if (!config.globalContext)
                key << ':' << speaker.guid << ':' << static_cast<unsigned>(scope) << ':' << Lower(channel);
            return key.str();
        }

        std::vector<std::string> Words(std::string const& input)
        {
            std::vector<std::string> words;
            std::string current;
            for (unsigned char c : Lower(input))
            {
                if (std::isalnum(c))
                    current.push_back(static_cast<char>(c));
                else if (current.size() >= 3)
                {
                    words.push_back(current);
                    current.clear();
                }
                else
                    current.clear();
            }
            if (current.size() >= 3)
                words.push_back(current);
            return words;
        }

        std::vector<std::string> Split(std::string const& input, char delimiter)
        {
            std::vector<std::string> result;
            std::stringstream stream(input);
            std::string part;
            while (std::getline(stream, part, delimiter))
            {
                part = Trim(part);
                if (!part.empty())
                    result.push_back(part);
            }
            return result;
        }

        std::string Pick(std::vector<std::string> const& items)
        {
            return items.empty() ? "" : items[RandomUInt(0, static_cast<uint32_t>(items.size() - 1))];
        }

        ChatScope ParseScope(std::string value)
        {
            value = Lower(Trim(value));
            if (value == "yell") return ChatScope::Yell;
            if (value == "whisper") return ChatScope::Whisper;
            if (value == "party") return ChatScope::Party;
            if (value == "raid") return ChatScope::Raid;
            if (value == "guild") return ChatScope::Guild;
            if (value == "officer") return ChatScope::Officer;
            if (value == "world") return ChatScope::World;
            if (value == "channel") return ChatScope::Channel;
            return ChatScope::Say;
        }
    }

    Manager& Manager::Instance()
    {
        static Manager instance;
        return instance;
    }

    Manager::Manager()
        : m_stopping(false), m_paused(false), m_inFlight(0), m_nextRequestId(1),
          m_accepted(0), m_completed(0), m_failed(0), m_dropped(0),
          m_suppressedErrors(0), m_started(false)
    {
    }

    Manager::~Manager()
    {
        Stop();
    }

    void Manager::Start()
    {
        Stop();
        m_config = std::make_shared<Config const>(Config::Load());
        m_paused = false;

        if (!m_config->enabled)
        {
            sLog.outString("[AzerothVoices] Module loaded but disabled.");
            return;
        }

        std::string tlsError;
        if (!Provider::InitializeTls(tlsError))
        {
            sLog.outError("[AzerothVoices] TLS initialization failed: %s", tlsError.c_str());
            return;
        }

        LoadKnowledge();
        m_stopping = false;
        m_started = true;
        m_lastErrorLog = Clock::time_point();
        m_suppressedErrors = 0;
        for (uint32_t i = 0; i < m_config->workerThreads; ++i)
            m_workers.emplace_back(&Manager::WorkerLoop, this);
        ScheduleNextAmbient();

        sLog.outString("[AzerothVoices] Started with %u workers, endpoint %s, model %s.",
            m_config->workerThreads, m_config->endpoint.c_str(), m_config->model.c_str());
        if (!m_config->legacyCharacterCardFile.empty())
            sLog.outString("[AzerothVoices] AiPlayerbot.LLMDefaultPromptsFile is intentionally ignored; this module uses one global prompt and no per-bot personalities.");
    }

    void Manager::Reload()
    {
        sLog.outString("[AzerothVoices] Reloading configuration.");
        Start();
    }

    void Manager::Stop()
    {
        m_started = false;
        m_stopping = true;
        m_queueReady.notify_all();
        for (std::thread& worker : m_workers)
            if (worker.joinable())
                worker.join();
        m_workers.clear();

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto& queue : m_queues)
                queue.clear();
            m_latestRequestByActor.clear();
            m_requestBudget.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_completionMutex);
            m_completions.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_ingressMutex);
            m_ingress.clear();
        }
        m_scheduled.clear();
        m_actorCooldowns.clear();
        m_speakerCooldowns.clear();
        m_eventCooldowns.clear();
        m_inFlight = 0;
    }

    void Manager::Update(uint32_t /*diff*/)
    {
        if (!m_started || !m_config || !m_config->enabled)
            return;
        DrainIngress();
        DrainCompletions();
        DeliverScheduled();
        PruneHistory();
        if (!m_paused && m_config->randomChatterEnabled && Clock::now() >= m_nextAmbient)
        {
            RunAmbient();
            ScheduleNextAmbient();
        }
    }

    void Manager::ScheduleNextAmbient()
    {
        uint32_t seconds = m_config ? RandomUInt(m_config->randomMinimumIntervalSeconds,
                                                  m_config->randomMaximumIntervalSeconds) : 120;
        m_nextAmbient = Clock::now() + std::chrono::seconds(seconds);
    }

    bool Manager::PopRequest(ChatRequest& request)
    {
        for (;;)
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueReady.wait(lock, [&]() {
                if (m_stopping)
                    return true;
                for (auto const& queue : m_queues)
                    if (!queue.empty())
                        return true;
                return false;
            });
            if (m_stopping)
                return false;

            for (int priority = 3; priority >= 0; --priority)
            {
                auto& queue = m_queues[static_cast<size_t>(priority)];
                while (!queue.empty())
                {
                    request = std::move(queue.front());
                    queue.pop_front();
                    auto latest = m_latestRequestByActor.find(request.actor.guid);
                    if (latest != m_latestRequestByActor.end() && latest->second == request.id)
                        return true;
                    ++m_dropped;
                }
            }
        }
    }

    void Manager::WorkerLoop()
    {
        ChatRequest request;
        while (PopRequest(request))
        {
            if (Clock::now() > request.expires)
            {
                ++m_dropped;
                continue;
            }

            ++m_inFlight;
            ChatCompletion completion;
            uint32_t attempts = 0;
            do
            {
                completion = Provider::Execute(*m_config, request);
                bool retryable = !completion.success &&
                    (completion.httpStatus == 0 || completion.httpStatus == 429 || completion.httpStatus >= 500);
                if (!retryable || attempts >= m_config->retryMaximum || m_stopping)
                    break;
                ++attempts;
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    m_config->retryBackoffMilliseconds * attempts));
            } while (Clock::now() <= request.expires);
            --m_inFlight;

            {
                std::lock_guard<std::mutex> lock(m_completionMutex);
                m_completions.push_back(std::move(completion));
            }
        }
    }

    bool Manager::Enqueue(ChatRequest request)
    {
        if (!m_started || m_stopping || m_paused || !m_config)
            return false;

        auto const now = Clock::now();
        uint32_t cooldownSeconds = request.ambient ? m_config->ambientActorCooldownSeconds : m_config->actorCooldownSeconds;
        auto cooldown = m_actorCooldowns.find(request.actor.guid);
        if (request.priority != RequestPriority::Direct && cooldown != m_actorCooldowns.end() && cooldown->second > now)
        {
            ++m_dropped;
            return false;
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_requestBudget.empty() && now - m_requestBudget.front() >= std::chrono::minutes(1))
            m_requestBudget.pop_front();
        if (m_requestBudget.size() >= m_config->globalRequestsPerMinute)
        {
            ++m_dropped;
            return false;
        }

        auto latest = m_latestRequestByActor.find(request.actor.guid);
        if (latest != m_latestRequestByActor.end())
        {
            RequestPriority existing = RequestPriority::Ambient;
            bool found = false;
            for (size_t i = 0; i < m_queues.size() && !found; ++i)
                for (ChatRequest const& queued : m_queues[i])
                    if (queued.id == latest->second)
                    {
                        existing = queued.priority;
                        found = true;
                        break;
                    }
            if (found && static_cast<uint8_t>(request.priority) <= static_cast<uint8_t>(existing))
            {
                ++m_dropped;
                return false;
            }
        }

        size_t queuedCount = 0;
        for (auto const& queue : m_queues)
            queuedCount += queue.size();
        size_t normalLimit = m_config->queueMaximum - m_config->highPriorityReserve;
        bool highPriority = request.priority == RequestPriority::Direct || request.priority == RequestPriority::Group;
        if ((!highPriority && queuedCount >= normalLimit) || queuedCount >= m_config->queueMaximum)
        {
            if (highPriority && !m_queues[0].empty())
            {
                ChatRequest dropped = std::move(m_queues[0].back());
                m_queues[0].pop_back();
                auto oldLatest = m_latestRequestByActor.find(dropped.actor.guid);
                if (oldLatest != m_latestRequestByActor.end() && oldLatest->second == dropped.id)
                    m_latestRequestByActor.erase(oldLatest);
                ++m_dropped;
            }
            else
            {
                ++m_dropped;
                return false;
            }
        }

        request.id = m_nextRequestId++;
        request.created = now;
        request.expires = now + std::chrono::seconds(m_config->requestTtlSeconds);
        m_latestRequestByActor[request.actor.guid] = request.id;
        size_t const priorityIndex = static_cast<size_t>(request.priority);
        m_queues[priorityIndex].push_back(std::move(request));
        m_requestBudget.push_back(now);
        m_actorCooldowns[m_queues[priorityIndex].back().actor.guid] =
            now + std::chrono::seconds(cooldownSeconds);
        if (m_config->tracePrompts)
        {
            ChatRequest const& queued = m_queues[priorityIndex].back();
            sLog.outDebug("[AzerothVoices] Request %llu system: %s",
                static_cast<unsigned long long>(queued.id), queued.systemPrompt.c_str());
            sLog.outDebug("[AzerothVoices] Request %llu user: %s",
                static_cast<unsigned long long>(queued.id), queued.userPrompt.c_str());
            if (!queued.context.empty())
                sLog.outDebug("[AzerothVoices] Request %llu context: %s",
                    static_cast<unsigned long long>(queued.id), queued.context.c_str());
        }
        ++m_accepted;
        m_queueReady.notify_one();
        return true;
    }

    ChatRequest Manager::BuildRequest(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                      ChatScope scope, std::string const& channelName,
                                      std::string const& trigger, std::string const& message,
                                      RequestPriority priority, bool ambient, bool allowFollowup)
    {
        ChatRequest request;
        request.priority = priority;
        request.actor = actor;
        request.speaker = speaker;
        request.scope = scope;
        request.channelName = channelName;
        request.trigger = trigger;
        request.incomingMessage = message;
        request.ambient = ambient;
        request.allowFollowup = allowFollowup;
        request.historyKey = HistoryKey(*m_config, actor, speaker, scope, channelName);

        std::string rolePrompt = actor.kind == ActorKind::Creature ? m_config->rpgPrompt : m_config->prePrompt;
        request.systemPrompt = m_config->globalPrompt;
        if (!rolePrompt.empty())
            request.systemPrompt += "\n" + rolePrompt;
        request.systemPrompt += "\nThe reply will be sent through " +
            (channelName.empty() ? ScopeName(scope) : channelName) +
            ". Keep it concise and return dialogue only.";

        if (ambient)
            request.userPrompt = "Create one natural line now. Situation or topic: " + message;
        else if (trigger.compare(0, 6, "event:") == 0)
            request.userPrompt = "React naturally to this in-game event: " + message;
        else
            request.userPrompt = m_config->prompt;
        if (!m_config->postPrompt.empty())
            request.userPrompt += "\n" + m_config->postPrompt;

        request.systemPrompt = Expand(request.systemPrompt, request);
        request.userPrompt = Expand(request.userPrompt, request);
        request.context = BuildEnvironmentContext(request);
        std::string history = BuildHistoryContext(request);
        if (!history.empty())
            request.context += (request.context.empty() ? "" : "\n\n") + history;

        std::string knowledge = SelectKnowledge(request);
        if (!knowledge.empty())
        {
            std::string block = m_config->knowledgePromptTemplate;
            ReplaceAll(block, "<knowledge>", knowledge);
            ReplaceAll(block, "\\n", "\n");
            request.context += (request.context.empty() ? "" : "\n\n") + block;
        }
        if (request.context.size() > m_config->contextLength)
            request.context.erase(0, request.context.size() - m_config->contextLength);
        return request;
    }

    std::vector<Manager::Candidate> Manager::CollectCandidates(Player* speaker, ChatScope scope,
        std::string const& targetName, std::string const& message, bool ambient,
        float distanceOverride, uint64_t excludedActor) const
    {
        std::vector<Candidate> result;
        if (!speaker || !speaker->IsInWorld() || !m_config)
            return result;

        bool const speakerIsBot = Script_IsAIControlled(speaker);
        ObjectGuid const selected = speaker->GetSelectionGuid();
        std::string const targetLower = Lower(targetName);
        std::string const messageLower = Lower(message);
        float const nearbyDistance = distanceOverride > 0.0f ? distanceOverride :
            (scope == ChatScope::Yell ? m_config->yellDistance : m_config->sayDistance);

        auto chanceFor = [&](std::string const& actorName, ObjectGuid actorGuid, bool npc) -> std::pair<uint32_t, int>
        {
            bool const direct = scope == ChatScope::Whisper || (!selected.IsEmpty() && selected == actorGuid);
            bool const mentioned = !actorName.empty() && messageLower.find(Lower(actorName)) != std::string::npos;
            if (ambient)
                return { 100, 10 };

            uint32_t chance = m_config->overhearChance;
            int score = 10;
            if (direct)
            {
                chance = m_config->directAddressChance;
                score = 100;
            }
            else if (mentioned)
            {
                chance = m_config->nameMentionChance;
                score = 80;
            }
            else if (npc)
            {
                chance = m_config->overhearChance;
            }
            else
            {
                switch (scope)
                {
                    case ChatScope::Say:
                    case ChatScope::Yell:
                        chance = speakerIsBot ? m_config->botReplyChanceSay : m_config->playerReplyChanceSay;
                        break;
                    case ChatScope::Party:
                    case ChatScope::Raid:
                        chance = speakerIsBot ? m_config->botReplyChanceParty : m_config->playerReplyChanceParty;
                        break;
                    case ChatScope::Guild:
                    case ChatScope::Officer:
                        chance = speakerIsBot ? m_config->botReplyChanceGuild : m_config->playerReplyChanceGuild;
                        break;
                    case ChatScope::Channel:
                    case ChatScope::World:
                        chance = speakerIsBot ? m_config->botReplyChanceChannel : m_config->playerReplyChanceChannel;
                        break;
                    case ChatScope::Whisper:
                        chance = 100;
                        break;
                }
            }

            if (speakerIsBot)
                chance = std::min(chance, npc ? m_config->rpgAiChatChance : m_config->botToBotChatChance);
            return { chance, score };
        };

        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* bot = entry.second;
                if (!bot || bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                    !Script_IsAIControlled(bot) || bot->GetObjectGuid().GetRawValue() == excludedActor)
                    continue;
                if (m_config->disableRepliesInCombat && bot->IsInCombat())
                    continue;

                bool eligible = false;
                switch (scope)
                {
                    case ChatScope::Whisper:
                        eligible = !targetLower.empty() && Lower(bot->GetName()) == targetLower;
                        break;
                    case ChatScope::Say:
                    case ChatScope::Yell:
                        eligible = bot->GetMapId() == speaker->GetMapId() && speaker->IsWithinDist(bot, nearbyDistance, false);
                        break;
                    case ChatScope::Party:
                    case ChatScope::Raid:
                        eligible = speaker->GetGroup() && bot->GetGroup() == speaker->GetGroup();
                        break;
                    case ChatScope::Guild:
                    case ChatScope::Officer:
                        eligible = speaker->GetGuildId() && bot->GetGuildId() == speaker->GetGuildId();
                        break;
                    case ChatScope::Channel:
                    case ChatScope::World:
                        eligible = true;
                        break;
                }
                if (!eligible)
                    continue;

                auto chanceAndScore = chanceFor(bot->GetName(), bot->GetObjectGuid(), false);
                if (!Roll(chanceAndScore.first))
                    continue;
                Candidate candidate;
                candidate.actor = SnapshotBot(bot);
                candidate.chance = chanceAndScore.first;
                candidate.score = chanceAndScore.second + static_cast<int>(RandomUInt(0, 9));
                result.push_back(std::move(candidate));
            }
        }

        if (m_config->npcReplies && (scope == ChatScope::Say || scope == ChatScope::Yell))
        {
            float distance = distanceOverride > 0.0f ? distanceOverride : m_config->npcDistance;
            MaNGOS::AllCreaturesInRange check(speaker, distance);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
            Cell::VisitGridObjects(speaker, searcher, distance);

            for (Creature* creature : creatures)
            {
                if (!creature || !creature->IsInWorld() || !creature->IsAlive() || creature->IsPet() ||
                    creature->IsTotem() || creature->IsCritter() || creature->IsHostileTo(speaker) ||
                    creature->GetObjectGuid().GetRawValue() == excludedActor)
                    continue;
                if (m_config->disableRepliesInCombat && creature->IsInCombat())
                    continue;

                auto chanceAndScore = chanceFor(creature->GetName(), creature->GetObjectGuid(), true);
                if (!Roll(chanceAndScore.first))
                    continue;
                Candidate candidate;
                candidate.actor = SnapshotCreature(creature, speaker);
                candidate.chance = chanceAndScore.first;
                candidate.score = chanceAndScore.second + static_cast<int>(RandomUInt(0, 9));
                result.push_back(std::move(candidate));
            }
        }

        std::sort(result.begin(), result.end(), [](Candidate const& left, Candidate const& right) {
            return left.score > right.score;
        });
        return result;
    }

    void Manager::HandleChat(Player* speaker, ChatScope scope, std::string const& message,
                             std::string const& targetName, std::string const& channelName)
    {
        if (!m_started || !speaker || message.empty())
            return;
        InboundSignal signal;
        signal.kind = InboundSignal::Kind::Chat;
        signal.playerGuid = speaker->GetObjectGuid().GetRawValue();
        signal.scope = scope;
        signal.message = message;
        signal.targetName = targetName;
        signal.channelName = channelName;
        std::lock_guard<std::mutex> lock(m_ingressMutex);
        if (m_ingress.size() >= 2048)
        {
            ++m_dropped;
            return;
        }
        m_ingress.push_back(std::move(signal));
    }

    void Manager::ProcessChat(Player* speaker, ChatScope scope, std::string const& message,
                              std::string const& targetName, std::string const& channelName)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !speaker ||
            message.empty() || !IsScopeEnabled(*m_config, scope) ||
            IsBlockedChannel(*m_config, scope, channelName) || IsBlacklisted(*m_config, message))
            return;

        auto now = Clock::now();
        if (scope != ChatScope::Whisper && m_config->speakerCooldownSeconds)
        {
            auto cooldown = m_speakerCooldowns.find(speaker->GetObjectGuid().GetRawValue());
            if (cooldown != m_speakerCooldowns.end() && cooldown->second > now)
                return;
        }

        std::vector<Candidate> candidates = CollectCandidates(speaker, scope, targetName, message, false);
        if (candidates.empty())
            return;

        SpeakerSnapshot speakerSnapshot = SnapshotSpeaker(speaker);
        bool accepted = false;
        uint32_t maximum = scope == ChatScope::Whisper ? 1 : m_config->maxResponders;
        for (size_t i = 0; i < candidates.size() && i < maximum; ++i)
        {
            bool direct = scope == ChatScope::Whisper || candidates[i].score >= 80;
            RequestPriority priority = direct ? RequestPriority::Direct :
                ((scope == ChatScope::Party || scope == ChatScope::Raid || scope == ChatScope::Guild || scope == ChatScope::Officer)
                    ? RequestPriority::Group : RequestPriority::Nearby);
            ChatRequest request = BuildRequest(candidates[i].actor, speakerSnapshot, scope, channelName,
                direct ? "direct-chat" : "overheard-chat", message, priority, false, false);
            accepted = Enqueue(std::move(request)) || accepted;
        }
        if (accepted && scope != ChatScope::Whisper && m_config->speakerCooldownSeconds)
            m_speakerCooldowns[speaker->GetObjectGuid().GetRawValue()] =
                now + std::chrono::seconds(m_config->speakerCooldownSeconds);
    }

    void Manager::HandleEvent(Player* subject, std::string const& eventName, std::string const& detail)
    {
        if (!m_started || !subject)
            return;
        InboundSignal signal;
        signal.kind = InboundSignal::Kind::Event;
        signal.playerGuid = subject->GetObjectGuid().GetRawValue();
        signal.eventName = eventName;
        signal.message = detail;
        std::lock_guard<std::mutex> lock(m_ingressMutex);
        if (m_ingress.size() >= 2048)
        {
            ++m_dropped;
            return;
        }
        m_ingress.push_back(std::move(signal));
    }

    void Manager::DrainIngress()
    {
        std::deque<InboundSignal> signals;
        {
            std::lock_guard<std::mutex> lock(m_ingressMutex);
            signals.swap(m_ingress);
        }
        for (InboundSignal const& signal : signals)
        {
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(signal.playerGuid));
            if (!player || !player->IsInWorld())
                continue;
            if (signal.kind == InboundSignal::Kind::Chat)
                ProcessChat(player, signal.scope, signal.message, signal.targetName, signal.channelName);
            else
                ProcessEvent(player, signal.eventName, signal.message);
        }
    }

    void Manager::ProcessEvent(Player* subject, std::string const& eventName, std::string const& detail)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !m_config->eventChatterEnabled ||
            !subject || !subject->IsInWorld())
            return;

        std::string event = Lower(eventName);
        auto eventChance = m_config->eventChances.find(event);
        if (eventChance == m_config->eventChances.end() || !Roll(eventChance->second))
            return;
        std::string cooldownKey = event + ':' + std::to_string(subject->GetObjectGuid().GetRawValue());
        auto now = Clock::now();
        auto cooldown = m_eventCooldowns.find(cooldownKey);
        if (cooldown != m_eventCooldowns.end() && cooldown->second > now)
            return;
        m_eventCooldowns[cooldownKey] = now + std::chrono::seconds(m_config->eventCooldownSeconds);

        std::string description = event;
        ReplaceAll(description, "_", " ");
        if (!detail.empty())
            description += ": " + detail;
        SpeakerSnapshot subjectSnapshot = SnapshotSpeaker(subject);

        // Guild progression and server-wide game events should be heard in the
        // scope where they matter.  This is intentionally decided on the world
        // thread using vMaNGOS state, never inside an HTTP worker.
        static std::set<std::string> const guildEvents = {
            "guild_join", "guild_leave", "guild_login", "guild_promotion", "guild_demotion",
            "achievement", "level_up", "rare_item", "epic_item", "dungeon_completed"
        };
        ChatScope eventScope = ChatScope::Say;
        if ((event == "game_event_started" || event == "game_event_stopped") && m_config->worldReplies)
            eventScope = ChatScope::World;
        else if (guildEvents.count(event) && m_config->guildReplies && subject->GetGuildId())
            eventScope = ChatScope::Guild;

        std::string channel = eventScope == ChatScope::World ? m_config->worldChannelName : "";
        RequestPriority priority = eventScope == ChatScope::Guild || eventScope == ChatScope::World
            ? RequestPriority::Group : RequestPriority::Nearby;

        if (Script_IsAIControlled(subject) && Roll(m_config->eventSelfCommentChance))
        {
            ChatRequest request = BuildRequest(SnapshotBot(subject), subjectSnapshot, eventScope, channel,
                "event:" + event, description, priority, false, false);
            Enqueue(std::move(request));
        }

        if (!Roll(m_config->eventResponderChance))
            return;
        float distance = eventScope == ChatScope::Say ? m_config->eventRealPlayerDistance : 0.0f;
        std::vector<Candidate> candidates = CollectCandidates(subject, eventScope, "", description,
            true, distance, subject->GetObjectGuid().GetRawValue());
        uint32_t count = 0;
        for (Candidate const& candidate : candidates)
        {
            if (count >= m_config->eventMaximumResponders)
                break;
            ChatRequest request = BuildRequest(candidate.actor, subjectSnapshot, eventScope, channel,
                "event:" + event, description, priority, false, false);
            if (Enqueue(std::move(request)))
                ++count;
        }
    }

    bool Manager::ForceAmbient(Player* anchor, std::string const& instruction)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !m_config->randomChatterEnabled)
            return false;

        if (!anchor)
        {
            std::vector<Player*> realPlayers;
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (player && player->IsInWorld() && player->IsAlive() && !Script_IsAIControlled(player))
                    realPlayers.push_back(player);
            }
            if (realPlayers.empty())
                return false;
            anchor = realPlayers[RandomUInt(0, static_cast<uint32_t>(realPlayers.size() - 1))];
        }

        ChatScope scope = ChatScope::Say;
        if (!m_config->randomScopes.empty())
            scope = ParseScope(Pick(m_config->randomScopes));
        if ((scope == ChatScope::Guild || scope == ChatScope::Officer) && !anchor->GetGuildId())
            scope = ChatScope::Say;
        if (!IsScopeEnabled(*m_config, scope))
            scope = ChatScope::Say;

        std::string topic = instruction;
        if (topic.empty())
        {
            uint32_t category = RandomUInt(0, 2);
            if (scope == ChatScope::Guild)
                topic = Pick(m_config->guildPrompts);
            else if (scope == ChatScope::World || scope == ChatScope::Channel)
                topic = Pick(m_config->worldPrompts);
            else if (category == 0)
                topic = Pick(m_config->randomPrompts);
            else if (category == 1)
                topic = Pick(m_config->randomQuestions);
            else
                topic = Pick(m_config->environmentPrompts);
        }
        if (topic.empty())
            topic = "Make a brief natural comment about the current situation.";

        float distance = scope == ChatScope::Say || scope == ChatScope::Yell
            ? m_config->randomRealPlayerDistance : 0.0f;
        std::vector<Candidate> candidates = CollectCandidates(anchor, scope, "", topic, true, distance);
        if (candidates.empty() && scope != ChatScope::Say)
        {
            scope = ChatScope::Say;
            candidates = CollectCandidates(anchor, scope, "", topic, true, m_config->randomRealPlayerDistance);
        }
        if (candidates.empty())
            return false;

        // NPC ambient speech is intentionally started only when a second nearby
        // actor exists, so a lone quest giver does not continually monologue.
        if (candidates.front().actor.kind == ActorKind::Creature && candidates.size() < 2)
        {
            auto bot = std::find_if(candidates.begin(), candidates.end(), [](Candidate const& candidate) {
                return candidate.actor.kind == ActorKind::PlayerBot;
            });
            if (bot == candidates.end())
                return false;
            std::iter_swap(candidates.begin(), bot);
        }

        std::string channel = (scope == ChatScope::World || scope == ChatScope::Channel)
            ? m_config->worldChannelName : "";
        ChatRequest request = BuildRequest(candidates.front().actor, SnapshotSpeaker(anchor), scope, channel,
            "ambient", topic, RequestPriority::Ambient, true, true);
        return Enqueue(std::move(request));
    }

    void Manager::RunAmbient()
    {
        if (ForceAmbient(nullptr) && m_config->debug)
            sLog.outDebug("[AzerothVoices] Queued ambient chatter.");
    }

    bool Manager::QueueTest(Player* requester, std::string const& actorName, std::string const& instruction)
    {
        if (!requester || !m_started || !m_config || !m_config->enabled)
            return false;

        ActorSnapshot actor;
        if (!actorName.empty())
        {
            Player* bot = ObjectAccessor::FindPlayerByName(actorName.c_str());
            if (!bot || !bot->IsInWorld() || !Script_IsAIControlled(bot))
                return false;
            actor = SnapshotBot(bot);
        }
        else
        {
            std::vector<Candidate> candidates = CollectCandidates(requester, ChatScope::Say, "",
                instruction, true, m_config->sayDistance);
            if (candidates.empty())
                return false;
            actor = candidates.front().actor;
        }

        std::string prompt = instruction.empty() ? "Reply exactly: Azeroth Voices test successful." : instruction;
        ChatRequest request = BuildRequest(actor, SnapshotSpeaker(requester), ChatScope::Whisper, "",
            "gm-test", prompt, RequestPriority::Direct, false, false);
        return Enqueue(std::move(request));
    }

    void Manager::MaybeQueueFollowup(ChatRequest const& request, std::string const& reply)
    {
        if (!request.allowFollowup || !m_config->randomChatterEnabled ||
            request.conversationDepth >= m_config->randomMaximumActors ||
            !Roll(m_config->randomFollowupChance))
            return;

        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
        if (!anchor || !anchor->IsInWorld())
            anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
        if (!anchor || !anchor->IsInWorld())
            return;

        float distance = request.scope == ChatScope::Say || request.scope == ChatScope::Yell
            ? m_config->randomRealPlayerDistance : 0.0f;
        std::vector<Candidate> candidates = CollectCandidates(anchor, request.scope, "", reply,
            true, distance, request.actor.guid);
        if (candidates.empty())
            return;

        SpeakerSnapshot previous;
        previous.guid = request.actor.guid;
        previous.name = request.actor.name;
        previous.race = request.actor.race;
        previous.className = request.actor.className;
        previous.gender = request.actor.gender;
        previous.faction = request.actor.faction;
        previous.guild = request.actor.guild;
        previous.groupStatus = request.actor.groupStatus;
        previous.level = request.actor.level;
        previous.isBot = true;

        ChatRequest followup = BuildRequest(candidates.front().actor, previous, request.scope,
            request.channelName, "ambient-followup", reply, RequestPriority::Ambient, true, true);
        followup.conversationDepth = request.conversationDepth + 1;
        Enqueue(std::move(followup));
    }

    void Manager::DrainCompletions()
    {
        std::deque<ChatCompletion> completions;
        {
            std::lock_guard<std::mutex> lock(m_completionMutex);
            completions.swap(m_completions);
        }

        auto const now = Clock::now();
        for (ChatCompletion& completion : completions)
        {
            bool current = false;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                auto latest = m_latestRequestByActor.find(completion.request.actor.guid);
                current = latest != m_latestRequestByActor.end() && latest->second == completion.request.id;
                if (current)
                    m_latestRequestByActor.erase(latest);
            }
            if (!current || now > completion.request.expires)
            {
                ++m_dropped;
                continue;
            }

            if (!completion.success)
            {
                ++m_failed;
                if (m_config->debug || m_lastErrorLog == Clock::time_point() ||
                    now - m_lastErrorLog >= std::chrono::seconds(30))
                {
                    if (m_suppressedErrors)
                        sLog.outError("[AzerothVoices] %u additional provider errors were suppressed.", m_suppressedErrors);
                    sLog.outError("[AzerothVoices] Request %llu for %s failed: %s",
                        static_cast<unsigned long long>(completion.request.id),
                        completion.request.actor.name.c_str(), completion.error.c_str());
                    m_lastErrorLog = now;
                    m_suppressedErrors = 0;
                }
                else
                    ++m_suppressedErrors;
                continue;
            }

            std::vector<std::string> lines = Provider::SplitReply(*m_config, completion.responseText);
            if (lines.empty())
            {
                ++m_failed;
                continue;
            }

            uint64_t cumulativeDelay = m_config->typingSimulationEnabled
                ? m_config->typingBaseDelayMilliseconds : 0;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                uint64_t lineDelay = m_config->typingSimulationEnabled
                    ? static_cast<uint64_t>(lines[i].size()) * m_config->typingDelayPerCharacterMilliseconds
                    : 0;
                cumulativeDelay += lineDelay;
                uint64_t effectiveDelay = cumulativeDelay;
                if (m_config->typingSimulationEnabled && m_config->subtractGenerationTime)
                    effectiveDelay = effectiveDelay > completion.elapsedMilliseconds
                        ? effectiveDelay - completion.elapsedMilliseconds : 0;

                ScheduledLine line;
                line.request = completion.request;
                line.text = lines[i];
                line.due = now + std::chrono::milliseconds(effectiveDelay);
                line.firstLine = i == 0;
                m_scheduled.push_back(std::move(line));
                if (m_config->typingSimulationEnabled)
                    cumulativeDelay += 500;
            }
            ++m_completed;
            if (m_config->debug)
                sLog.outDebug("[AzerothVoices] Request %llu completed in %u ms; %u line(s) scheduled.",
                    static_cast<unsigned long long>(completion.request.id), completion.elapsedMilliseconds,
                    static_cast<unsigned>(lines.size()));
        }
    }

    void Manager::DeliverScheduled()
    {
        auto const now = Clock::now();
        for (auto it = m_scheduled.begin(); it != m_scheduled.end(); )
        {
            if (it->due > now)
            {
                ++it;
                continue;
            }

            bool delivered = Deliver(*it);
            if (delivered && it->firstLine)
            {
                AddHistory(it->request, it->text);
                MaybeQueueFollowup(it->request, it->text);
            }
            if (!delivered && m_config->debug)
                sLog.outDebug("[AzerothVoices] Discarded reply for unavailable actor %s.", it->request.actor.name.c_str());
            it = m_scheduled.erase(it);
        }
    }

    bool Manager::Deliver(ScheduledLine const& line)
    {
        ChatRequest const& request = line.request;
        if (request.actor.kind == ActorKind::PlayerBot)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            if (!bot || !bot->IsInWorld() || !Script_IsAIControlled(bot) || bot->GetName() != request.actor.name)
                return false;

            switch (request.scope)
            {
                case ChatScope::Say:
                    bot->Say(line.text, LANG_UNIVERSAL);
                    return true;
                case ChatScope::Yell:
                    bot->Yell(line.text, LANG_UNIVERSAL);
                    return true;
                case ChatScope::Whisper:
                {
                    Player* receiver = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
                    if (!receiver || !receiver->IsInWorld())
                        return false;
                    bot->Whisper(line.text, LANG_UNIVERSAL, receiver->GetObjectGuid());
                    return true;
                }
                case ChatScope::Party:
                case ChatScope::Raid:
                {
                    Group* group = bot->GetGroup();
                    if (!group)
                        return false;
                    WorldPacket packet;
                    ChatMsg type = request.scope == ChatScope::Raid ? CHAT_MSG_RAID : CHAT_MSG_PARTY;
                    ChatHandler::BuildChatPacket(packet, type, line.text, LANG_UNIVERSAL,
                        bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
                    int subgroup = request.scope == ChatScope::Party
                        ? group->GetMemberGroup(bot->GetObjectGuid()) : -1;
                    group->BroadcastPacket(&packet, false, subgroup);
                    return true;
                }
                case ChatScope::Guild:
                case ChatScope::Officer:
                {
                    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
                    if (!guild || !bot->GetSession())
                        return false;
                    if (request.scope == ChatScope::Officer)
                        guild->BroadcastToOfficers(bot->GetSession(), line.text, LANG_UNIVERSAL);
                    else
                        guild->BroadcastToGuild(bot->GetSession(), line.text, LANG_UNIVERSAL);
                    return true;
                }
                case ChatScope::Channel:
                case ChatScope::World:
                {
                    std::string channelName = request.channelName.empty()
                        ? m_config->worldChannelName : request.channelName;
                    ChannelMgr* manager = channelMgr(bot->GetTeam());
                    Channel* channel = manager ? manager->GetOrCreateChannel(channelName) : nullptr;
                    if (!channel)
                        return false;
                    channel->AsyncSay(bot->GetObjectGuid(), line.text.c_str(), LANG_UNIVERSAL, true);
                    return true;
                }
            }
        }

        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
        if (!anchor || !anchor->IsInWorld() || anchor->GetMapId() != request.actor.mapId)
            return false;
        Creature* creature = ObjectAccessor::GetCreature(*anchor, ObjectGuid(request.actor.guid));
        if (!creature || !creature->IsInWorld() || !creature->IsAlive() || creature->GetName() != request.actor.name)
            return false;

        Player* receiver = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
        if (request.scope == ChatScope::Whisper && receiver && receiver->IsInWorld())
            creature->MonsterWhisper(line.text.c_str(), receiver);
        else if (request.scope == ChatScope::Yell)
            creature->MonsterYell(line.text, LANG_UNIVERSAL, receiver);
        else
            creature->MonsterSay(line.text, LANG_UNIVERSAL, receiver);
        return true;
    }

    std::string Manager::BuildEnvironmentContext(ChatRequest const& request) const
    {
        if (!m_config->environmentContextEnabled)
            return "";

        Player* bot = nullptr;
        Player* anchor = nullptr;
        WorldObject* center = nullptr;
        if (request.actor.kind == ActorKind::PlayerBot)
        {
            bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            center = bot && bot->IsInWorld() ? static_cast<WorldObject*>(bot) : nullptr;
            anchor = bot;
        }
        else
        {
            anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
            if (anchor && anchor->IsInWorld())
                center = ObjectAccessor::GetCreature(*anchor, ObjectGuid(request.actor.guid));
        }

        std::ostringstream context;
        context << "Current environment: map=" << request.actor.map
                << ", zone=" << request.actor.zone
                << ", area=" << request.actor.area
                << ", level=" << request.actor.level
                << ", state=" << (request.actor.inCombat ? "in combat" : "out of combat")
                << ", group=" << request.actor.groupStatus;
        if (!request.actor.guild.empty())
            context << ", guild=" << request.actor.guild;
        if (center && center->FindMap() && center->FindMap()->IsDungeon())
            context << ", inside a dungeon";

        if (center && m_config->environmentMaximumCreatures)
        {
            MaNGOS::AllCreaturesInRange check(center, m_config->environmentContextDistance);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
            Cell::VisitGridObjects(center, searcher, m_config->environmentContextDistance);
            uint32_t count = 0;
            for (Creature* creature : creatures)
            {
                if (!creature || creature->GetObjectGuid().GetRawValue() == request.actor.guid ||
                    !creature->IsAlive() || creature->IsPet() || creature->IsTotem())
                    continue;
                context << (count++ == 0 ? "\nNearby creatures: " : ", ") << creature->GetName();
                if (count >= m_config->environmentMaximumCreatures)
                    break;
            }
        }

        if (bot && m_config->environmentMaximumItems &&
            (m_config->environmentIncludeEquipment || m_config->environmentIncludeBackpack))
        {
            uint32_t count = 0;
            auto appendItem = [&](Item* item)
            {
                if (!item || !item->GetProto() || count >= m_config->environmentMaximumItems)
                    return;
                context << (count++ == 0 ? "\nVisible/relevant items: " : ", ") << item->GetProto()->Name1;
            };
            if (m_config->environmentIncludeEquipment)
                for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END && count < m_config->environmentMaximumItems; ++slot)
                    appendItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            if (m_config->environmentIncludeBackpack)
                for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && count < m_config->environmentMaximumItems; ++slot)
                    appendItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        }
        return context.str();
    }

    std::string Manager::BuildHistoryContext(ChatRequest const& request)
    {
        if (!m_config->historyEnabled || m_config->historyMaximumTurns == 0)
            return "";
        auto found = m_history.find(request.historyKey);
        if (found == m_history.end() || found->second.empty())
            return "";

        auto cutoff = Clock::now() - std::chrono::minutes(m_config->historyTtlMinutes);
        std::string context = Expand(m_config->historyHeaderTemplate, request) + "\n";
        for (HistoryTurn const& turn : found->second)
        {
            if (turn.created < cutoff)
                continue;
            std::string line = m_config->historyLineTemplate;
            ReplaceAll(line, "<sender message>", turn.speakerMessage);
            ReplaceAll(line, "<bot reply>", turn.actorReply);
            context += Expand(line, request);
        }
        context += Expand(m_config->historyFooterTemplate, request);
        ReplaceAll(context, "\\n", "\n");
        if (context.size() > m_config->historyMaximumCharacters)
            context.erase(0, context.size() - m_config->historyMaximumCharacters);
        return context;
    }

    void Manager::AddHistory(ChatRequest const& request, std::string const& reply)
    {
        if (!m_config->historyEnabled || m_config->historyMaximumTurns == 0)
            return;
        HistoryTurn turn;
        turn.speakerMessage = request.incomingMessage;
        turn.actorReply = reply;
        turn.created = Clock::now();
        auto& history = m_history[request.historyKey];
        history.push_back(std::move(turn));
        while (history.size() > m_config->historyMaximumTurns)
            history.pop_front();
    }

    void Manager::PruneHistory()
    {
        if (!m_config || !m_config->historyEnabled)
            return;
        auto cutoff = Clock::now() - std::chrono::minutes(m_config->historyTtlMinutes);
        for (auto mapIt = m_history.begin(); mapIt != m_history.end(); )
        {
            auto& turns = mapIt->second;
            while (!turns.empty() && turns.front().created < cutoff)
                turns.pop_front();
            if (turns.empty())
                mapIt = m_history.erase(mapIt);
            else
                ++mapIt;
        }
    }

    void Manager::ClearHistory()
    {
        m_history.clear();
    }

    void Manager::LoadKnowledge()
    {
        m_knowledge.clear();
        if (!m_config->knowledgeEnabled || m_config->knowledgeFile.empty())
            return;

        std::ifstream input(m_config->knowledgeFile.c_str());
        if (!input)
        {
            sLog.outError("[AzerothVoices] Could not open knowledge file %s.", m_config->knowledgeFile.c_str());
            return;
        }

        std::string line;
        while (std::getline(input, line))
        {
            line = Trim(line);
            if (line.empty() || line.front() == '#')
                continue;
            KnowledgeItem item;
            size_t separator = line.find("::");
            if (separator == std::string::npos)
            {
                item.text = line;
                item.keywords = Words(line);
            }
            else
            {
                item.keywords = Split(Lower(line.substr(0, separator)), ',');
                item.text = Trim(line.substr(separator + 2));
            }
            if (!item.text.empty() && !item.keywords.empty())
                m_knowledge.push_back(std::move(item));
        }
        sLog.outString("[AzerothVoices] Loaded %u knowledge entries.", static_cast<unsigned>(m_knowledge.size()));
    }

    std::string Manager::SelectKnowledge(ChatRequest const& request) const
    {
        if (!m_config->knowledgeEnabled || m_knowledge.empty())
            return "";
        std::set<std::string> inputWords;
        for (std::string const& word : Words(request.incomingMessage + " " + request.actor.area + " " +
                                              request.actor.zone + " " + request.trigger))
            inputWords.insert(word);

        std::vector<std::pair<uint32_t, size_t>> scores;
        for (size_t i = 0; i < m_knowledge.size(); ++i)
        {
            uint32_t score = 0;
            for (std::string const& keyword : m_knowledge[i].keywords)
                if (inputWords.count(Lower(Trim(keyword))))
                    ++score;
            if (score >= m_config->knowledgeMinimumScore)
                scores.emplace_back(score, i);
        }
        std::sort(scores.begin(), scores.end(), [](auto const& left, auto const& right) {
            return left.first > right.first;
        });

        std::string result;
        size_t count = std::min<size_t>(scores.size(), m_config->knowledgeMaximumItems);
        for (size_t i = 0; i < count; ++i)
        {
            if (!result.empty())
                result += "\n- ";
            else
                result = "- ";
            result += m_knowledge[scores[i].second].text;
        }
        return result;
    }

    void Manager::SetPaused(bool paused)
    {
        m_paused = paused;
    }

    bool Manager::IsPaused() const
    {
        return m_paused;
    }

    StatusSnapshot Manager::GetStatus() const
    {
        StatusSnapshot status;
        status.enabled = m_started && m_config && m_config->enabled;
        status.paused = m_paused;
        status.workers = static_cast<uint32_t>(m_workers.size());
        status.inFlight = m_inFlight;
        status.accepted = m_accepted;
        status.completed = m_completed;
        status.failed = m_failed;
        status.dropped = m_dropped;
        status.conversations = m_history.size();
        status.scheduledLines = m_scheduled.size();
        if (m_config)
        {
            status.endpoint = m_config->endpoint;
            status.model = m_config->model;
            status.worldChannelName = m_config->worldChannelName;
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto const& queue : m_queues)
                status.queued += queue.size();
        }
        return status;
    }
}
