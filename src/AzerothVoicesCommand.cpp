#include "AzerothVoicesManager.h"
#include "AzerothVoicesPersonality.h"
#include "AzerothVoicesSentiment.h"

#include "Chat.h"
#include "Player.h"
#include "ScriptObjects.h"
#include "WorldSession.h"

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace AzerothVoices
{
    void SendAddonResponses(Player* player, std::vector<std::string> const& responses)
    {
        if (!player || !player->GetSession())
            return;
        ChatHandler handler(player->GetSession());
        for (std::string const& line : responses)
            handler.SendSysMessage(line.c_str());
    }

    void HandleAddonCommandFromPlayer(Player* player, std::string const& arguments)
    {
        std::vector<std::string> responses;
        Manager::Instance().HandleAddonCommand(player, arguments, responses);
        SendAddonResponses(player, responses);
    }

    namespace
    {
        std::string TakeWord(std::string& input)
        {
            size_t begin = input.find_first_not_of(' ');
            if (begin == std::string::npos)
            {
                input.clear();
                return "";
            }
            size_t end = input.find(' ', begin);
            std::string word = input.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
            input = end == std::string::npos ? "" : input.substr(end + 1);
            return word;
        }

        char const* HistoryModeName(uint32_t mode)
        {
            switch (mode)
            {
                case 0: return "Disabled";
                case 1: return "RAM";
                case 2: return "SQL";
                default: return "Invalid";
            }
        }

        void SendGlobalTest(ChatHandler* handler)
        {
            StatusSnapshot const status = Manager::Instance().GetStatus();
            bool errors = false;
            auto send = [&](std::string const& line) { handler->SendSysMessage(line.c_str()); };

            handler->SendSysMessage("## Azeroth Voices Global Test");
            if (!status.configurationLoaded)
            {
                handler->SendSysMessage("Module: ERROR - configuration unavailable");
                errors = true;
            }
            else if (!status.enabled)
            {
                handler->SendSysMessage("Module: Disabled");
                errors = true;
            }
            else
                handler->SendSysMessage("Module: OK");

            if (status.apiConfigured)
                handler->SendSysMessage("API: OK - configuration available");
            else
            {
                handler->SendSysMessage("API: ERROR - endpoint, model/template, or authentication is missing");
                errors = true;
            }

            if (status.endpoint.empty())
            {
                handler->SendSysMessage("Endpoint: ERROR - not configured");
                errors = true;
            }
            else
                send("Endpoint: Configured - " + status.endpoint);

            send("History: " + std::string(HistoryModeName(status.historyStorageMode)));
            if (status.historyStorageMode == 1)
            {
                handler->SendSysMessage("RAM: OK");
                handler->SendSysMessage("SQL: Not active");
            }
            else if (status.historyStorageMode == 2)
            {
                if (status.historyDatabaseAvailable)
                    handler->SendSysMessage("SQL: OK");
                else
                {
                    handler->SendSysMessage("SQL: ERROR - history table unavailable; runtime fallback is RAM");
                    handler->SendSysMessage("RAM: OK - fallback active");
                    errors = true;
                }
                if (status.historyDatabaseAvailable)
                    handler->SendSysMessage("RAM: Not active");
            }
            else
            {
                handler->SendSysMessage("RAM: Not active");
                handler->SendSysMessage("SQL: Not active");
                if (status.historyStorageMode > 2)
                    errors = true;
            }

            if (!status.ragEnabled)
                handler->SendSysMessage("RAG: Disabled");
            else if (!status.ragEntries)
            {
                send("RAG: ERROR - no entries loaded (" + std::to_string(status.ragFiles) + " files)");
                errors = true;
            }
            else if (status.ragParseFailures)
            {
                send("RAG: ERROR - " + std::to_string(status.ragFiles) + " files / " +
                    std::to_string(status.ragEntries) + " entries / " +
                    std::to_string(status.ragParseFailures) + " parse failures");
                errors = true;
            }
            else
                send("RAG: OK - " + std::to_string(status.ragFiles) + " files / " +
                    std::to_string(status.ragEntries) + " entries");

            send(std::string("Environment: ") + (status.environmentEnabled ? "Enabled" : "Disabled"));
            send(std::string("Snapshot System: ") + (status.snapshotEnabled ? "Enabled" : "Disabled"));
            if (!status.personalityEnabled)
                handler->SendSysMessage("Personality: Disabled");
            else
            {
                send("Personality: " + std::string(status.personalityDatabaseAvailable
                    ? "OK - SQL persistent" : "RAM fallback - SQL table unavailable") +
                    ", cached=" + std::to_string(status.personalities) +
                    ", pending=" + std::to_string(status.personalityGenerationsPending));
                if (!status.personalityDatabaseAvailable)
                    errors = true;
            }
            if (!status.sentimentEnabled)
                handler->SendSysMessage("Sentiment: Disabled");
            else
            {
                send("Sentiment: " + std::string(status.sentimentDatabaseAvailable
                    ? "OK - SQL persistent" : "RAM fallback - SQL table unavailable") +
                    ", cached=" + std::to_string(status.sentiments) +
                    ", pending-writes=" + std::to_string(status.sentimentWritesPending));
                if (!status.sentimentDatabaseAvailable)
                    errors = true;
            }
            send(std::string("Chat system: ") + (status.enabled ? "OK" : "Not active"));
            send("Thinking: mode=" + (status.thinkingMode.empty() ? std::string("Auto") : status.thinkingMode) +
                ", provider=" + (status.thinkingProvider.empty() ? std::string("none") : status.thinkingProvider) +
                ", capability=" + (status.thinkingCapability.empty() ? std::string("unknown") : status.thinkingCapability) +
                ", effort=" + status.thinkingEffort +
                ", kinds=" + (status.thinkingAutoKinds.empty() ? std::string("(none)") : status.thinkingAutoKinds) +
                ", fallbacks=" + std::to_string(status.thinkingFallbacks));
            if (status.playerbotsLlmEnabled)
            {
                handler->SendSysMessage("Compatibility: WARNING - AiPlayerbot.LLMEnabled is enabled! Disable it to avoid duplicate generation.");
                errors = true;
            }
            else
                send("Compatibility: OK - PlayerBots native LLM is disabled");

            if (status.workers)
                send("Workers: OK - " + std::to_string(status.workers) + " active");
            else if (status.enabled)
            {
                handler->SendSysMessage("Workers: ERROR - not initialized");
                errors = true;
            }
            else
                handler->SendSysMessage("Workers: Not active");

            handler->SendSysMessage("-----------");
            handler->SendSysMessage(errors ? "Result: ERRORS FOUND" : "Result: OK");
        }

        char const* BackgroundModeName(uint32_t mode)
        {
            return mode == 1 ? "normal real-world WoW player" : "roleplay Azeroth character";
        }

        void HandlePersonalityCommand(ChatHandler* handler, std::string rest)
        {
            std::string action = TakeWord(rest);
            std::string actor = TakeWord(rest);
            if (action.empty() || actor.empty() || !TakeWord(rest).empty() ||
                (action != "show" && action != "status" &&
                 action != "regenerate" && action != "delete"))
            {
                handler->SendSysMessage("Usage: .av personality show/status/regenerate/delete <online-bot> | delete all");
                return;
            }

            std::string message;
            if (action == "status")
            {
                Manager::Instance().GetPersonalityGenerationStatus(actor, message);
                handler->SendSysMessage(message.c_str());
                return;
            }
            if (action == "show")
            {
                BotPersonality personality;
                if (!Manager::Instance().GetPersonality(actor, personality, message))
                {
                    handler->SendSysMessage(message.c_str());
                    return;
                }
                handler->SendSysMessage("## Azeroth Voices PlayerBot Personality");
                handler->SendSysMessage(("Bot: " + personality.botName).c_str());
                handler->SendSysMessage(("Character GUID: " + std::to_string(personality.characterGuid)).c_str());
                handler->SendSysMessage(("Traits: " + JoinPersonalityTraits(personality.traits)).c_str());
                handler->SendSysMessage(("Tone: " + (personality.tone.empty() ? std::string("disabled/not generated") : personality.tone)).c_str());
                handler->SendSysMessage(("Background mode: " + std::string(BackgroundModeName(personality.backgroundMode))).c_str());
                handler->SendSysMessage(("Background: " + (personality.background.empty() ? std::string("disabled/not generated") : personality.background)).c_str());
                handler->SendSysMessage(("Generation version: " + std::to_string(personality.generationVersion)).c_str());
                return;
            }
            if (action == "regenerate")
            {
                Manager::Instance().RegeneratePersonality(actor, message);
                handler->SendSysMessage(message.c_str());
                return;
            }
            if (actor == "all")
                Manager::Instance().DeleteAllPersonalities(message);
            else
                Manager::Instance().DeletePersonality(actor, message);
            handler->SendSysMessage(message.c_str());
        }

        bool ParseSentimentScore(std::string const& value, int32_t& score)
        {
            try
            {
                size_t consumed = 0;
                long long const parsed = std::stoll(value, &consumed, 10);
                if (consumed != value.size() || parsed < SentimentMinimumScore ||
                    parsed > SentimentMaximumScore)
                    return false;
                score = static_cast<int32_t>(parsed);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        void HandleSentimentCommand(ChatHandler* handler, std::string rest)
        {
            std::string const action = TakeWord(rest);
            std::string const actor = TakeWord(rest);
            std::string const target = TakeWord(rest);
            std::string const value = TakeWord(rest);
            std::string const extra = TakeWord(rest);
            std::string message;

            if (action == "inspect" && !actor.empty() && !target.empty() &&
                value.empty() && extra.empty())
                Manager::Instance().InspectSentiment(actor, target, message);
            else if (action == "set" && !actor.empty() && !target.empty() &&
                     !value.empty() && extra.empty())
            {
                int32_t score = 0;
                if (!ParseSentimentScore(value, score))
                    message = "Sentiment score must be an exact integer from -100 through 100.";
                else
                    Manager::Instance().SetSentiment(actor, target, score, message);
            }
            else if (action == "reset" && actor == "all" && target.empty() &&
                     value.empty() && extra.empty())
                Manager::Instance().ResetAllSentiments(message);
            else if (action == "reset" && !actor.empty() && !target.empty() &&
                     value.empty() && extra.empty())
                Manager::Instance().ResetSentiment(actor, target, message);
            else
                message = "Usage: .av sentiment inspect <online-bot> <online-player> | set <online-bot> <online-player> <-100..100> | reset <online-bot> <online-player> | reset all";

            handler->SendSysMessage(message.c_str());
        }

        void HandleMemoryCommand(ChatHandler* handler, std::string rest)
        {
            std::string const action = TakeWord(rest);
            std::string const actor = TakeWord(rest);
            std::string const target = TakeWord(rest);
            std::string const extra = TakeWord(rest);
            std::string message;

            if (action == "inspect" && !actor.empty() && !target.empty() && extra.empty())
                Manager::Instance().InspectMemories(actor, target, message);
            else if (action == "forget" && actor == "all" && target.empty() && extra.empty())
                Manager::Instance().ForgetAllMemories(message);
            else if (action == "forget" && !actor.empty() && !target.empty() && extra.empty())
                Manager::Instance().ForgetMemories(actor, target, message);
            else
                message = "Usage: .av memory inspect <online-bot> <online-player> | forget <online-bot> <online-player> | forget all";

            handler->SendSysMessage(message.c_str());
        }

        class AzerothVoicesCommandScript final : public AllCommandScript
        {
        public:
            AzerothVoicesCommandScript() : AllCommandScript("AzerothVoicesCommandScript") {}

            bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* arguments) override
            {
                if (!command)
                    return true;

                // Stock Chatter Companion support. `.llmc` is a user-level
                // command: it needs an in-game player but no GM security. The
                // handler consumes the line so the core never reports it as an
                // unknown command and the message is never spoken in Say.
                if (std::strcmp(command, "llmc") == 0)
                {
                    Player* addonPlayer = handler && handler->GetSession()
                        ? handler->GetSession()->GetPlayer() : nullptr;
                    if (!addonPlayer)
                        return true;
                    HandleAddonCommandFromPlayer(addonPlayer, arguments ? arguments : "");
                    return false;
                }

                if (std::strcmp(command, "av") != 0)
                    return true;

                bool const console = !handler->GetSession();
                bool const gameMaster = console || handler->GetSession()->GetSecurity() >= SEC_MODERATOR;
                if (!gameMaster)
                {
                    handler->SendSysMessage("You are not allowed to use Azeroth Voices commands.");
                    return false;
                }

                std::string rest = arguments ? arguments : "";
                std::string subcommand = TakeWord(rest);
                if (subcommand == "test")
                {
                    if (TakeWord(rest).empty())
                        SendGlobalTest(handler);
                    else
                        handler->SendSysMessage("Usage: .av test");
                    return false;
                }

                if (subcommand == "personality")
                {
                    HandlePersonalityCommand(handler, rest);
                    return false;
                }

                if (subcommand == "sentiment")
                {
                    HandleSentimentCommand(handler, rest);
                    return false;
                }

                if (subcommand == "memory")
                {
                    HandleMemoryCommand(handler, rest);
                    return false;
                }

                if (subcommand.empty() || subcommand == "status")
                {
                    StatusSnapshot status = Manager::Instance().GetStatus();
                    std::ostringstream text;
                    text << "Azeroth Voices: " << (status.enabled ? "enabled" : "disabled")
                         << (status.paused ? " (paused)" : "")
                         << ", playerbots-llm=" << (status.playerbotsLlmEnabled ? "CONFLICT-ENABLED" : "disabled")
                         << ", workers=" << status.workers
                         << ", queued=" << status.queued
                         << ", in-flight=" << status.inFlight
                         << ", accepted=" << status.accepted
                         << ", completed=" << status.completed
                         << ", failed=" << status.failed
                         << ", dropped=" << status.dropped
                         << ", history-mode=" << status.historyStorageMode
                         << ", histories=" << status.conversations
                         << ", surrounding-scopes=" << status.surroundingScopes
                         << ", snapshot-mode=" << status.snapshotStorageMode
                         << ", snapshot-histories=" << status.snapshotHistories
                         << ", history-db=" << (status.historyStorageMode != 2 ? "not-requested" :
                            (status.historyDatabaseAvailable ? "available" : "unavailable"))
                         << ", snapshot-db=" << (status.snapshotStorageMode != 2 ? "not-requested" :
                            (status.snapshotDatabaseAvailable ? "available" : "unavailable"))
                         << ", personality=" << (status.personalityEnabled ? "enabled" : "disabled")
                         << ", personality-db=" << (status.personalityDatabaseAvailable ? "available" : "unavailable")
                         << ", personalities=" << status.personalities
                         << ", personality-pending=" << status.personalityGenerationsPending
                         << ", sentiment=" << (status.sentimentEnabled ? "enabled" : "disabled")
                         << ", sentiment-db=" << (status.sentimentDatabaseAvailable ? "available" : "unavailable")
                         << ", sentiments=" << status.sentiments
                         << ", sentiment-pending=" << status.sentimentWritesPending
                         << ", addon=" << (status.addonEnabled ? "enabled" : "disabled")
                         << ", addon-db=" << (status.addonDatabaseAvailable ? "available" : "unavailable")
                         << ", proximity=" << (status.proximityEnabled ? "enabled" : "disabled")
                         << ", proximity-scenes=" << status.proximityScenes
                         << ", proximity-zones=" << status.proximityZoneCounters
                         << ", boss-dialogue=" << (status.bossDialogueEnabled ? "enabled" : "disabled")
                         << ", boss-presences=" << status.bossPresences
                         << ", group-chatter=" << (status.groupChatterEnabled ? "enabled" : "disabled")
                         << ", raid-chatter=" << (status.raidChatterEnabled ? "enabled" : "disabled")
                         << ", group-conversations=" << status.groupConversations
                         << ", tracked-groups=" << status.trackedGroups
                         << ", guild-chatter=" << (status.guildChatterEnabled ? "enabled" : "disabled")
                         << ", memory=" << (status.memoryEnabled ? "enabled" : "disabled")
                         << ", memory-db=" << (status.memoryDatabaseAvailable ? "available" : "unavailable")
                         << ", memory-pairs=" << status.memoryPairs
                         << ", memory-pending=" << status.memoryWritesPending
                         << ", thinking=" << status.thinkingMode
                         << ", thinking-provider=" << status.thinkingProvider
                         << ", thinking-capability=" << status.thinkingCapability
                         << ", thinking-effort=" << status.thinkingEffort
                         << ", thinking-kinds=" << status.thinkingAutoKinds
                         << ", thinking-fallbacks=" << status.thinkingFallbacks
                         << ", instance-lore=" << status.instanceLoreEntries
                         << ", pacing-windows=" << status.generalPacingWindows
                         << ", rag=" << (status.ragEnabled ? "enabled" : "disabled")
                         << ", rag-entries=" << status.ragEntries
                         << ", model=" << status.model
                         << ", endpoint=" << status.endpoint;
                    handler->SendSysMessage(text.str());
                    return false;
                }

                if (subcommand == "pause")
                {
                    Manager::Instance().SetPaused(true);
                    handler->SendSysMessage("Azeroth Voices paused. Queued replies can still finish delivery.");
                    return false;
                }
                if (subcommand == "resume")
                {
                    Manager::Instance().SetPaused(false);
                    handler->SendSysMessage("Azeroth Voices resumed.");
                    return false;
                }
                if (subcommand == "clearhistory")
                {
                    Manager::Instance().ClearHistory();
                    handler->SendSysMessage("Azeroth Voices chat, surrounding-chat, and snapshot history cleared; persistent module rows are also queued for deletion when database storage is available.");
                    return false;
                }
                if (subcommand == "restart")
                {
                    Manager::Instance().Reload();
                    handler->SendSysMessage("Azeroth Voices workers restarted. Use the core config reload command first if the file changed.");
                    return false;
                }

                Player* issuer = console ? nullptr : handler->GetSession()->GetPlayer();
                if (!issuer)
                {
                    handler->SendSysMessage("This Azeroth Voices command requires an in-game GM.");
                    return false;
                }
                if (subcommand == "chatter")
                {
                    bool queued = Manager::Instance().ForceAmbient(issuer, rest);
                    handler->SendSysMessage(queued ? "Ambient chatter queued." : "No eligible nearby/world actor was available.");
                    return false;
                }
                if (subcommand == "live")
                {
                    std::string actor = TakeWord(rest);
                    if (actor == "-")
                        actor.clear();
                    bool queued = Manager::Instance().QueueTest(issuer, actor, rest);
                    handler->SendSysMessage(queued ? "Live generation test queued; the result will be delivered in game."
                                                   : "Live test was not queued. Check module status and the bot name.");
                    return false;
                }

                handler->SendSysMessage("av: test | status | pause | resume | restart | clearhistory | chatter [topic] | live <bot-or-> [prompt] | personality show/status/regenerate/delete <bot> | personality delete all | sentiment inspect/set/reset <bot> <player> [score] | sentiment reset all | memory inspect/forget <bot> <player> | memory forget all");
                return false;
            }
        };
    }

    void RegisterAzerothVoicesCommand()
    {
        new AzerothVoicesCommandScript();
    }
}
