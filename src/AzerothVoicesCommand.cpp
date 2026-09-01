#include "AzerothVoicesManager.h"
#include "AzerothVoicesPersonality.h"

#include "Chat.h"
#include "Player.h"
#include "ScriptObjects.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

namespace AzerothVoices
{
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
            send("Natural commands: " + std::string(status.naturalCommandsEnabled ? "Enabled" : "Disabled") +
                ", allowed-actions=" + std::to_string(status.naturalCommandActions) +
                ", shortlist-cap=" + (status.naturalCommandShortlistMaximum == 0
                    ? "auto(" + std::to_string(status.naturalCommandEffectiveShortlistMaximum) + ")"
                    : std::to_string(status.naturalCommandEffectiveShortlistMaximum)) +
                ", recipients/actions=" + std::to_string(status.naturalCommandMaximumRecipients) +
                "x" + std::to_string(status.naturalCommandMaximumActions) +
                ", native-prefix=" + std::string(status.naturalCommandPrefixConfigured ? "configured" : "EMPTY/unsafe") +
                ", pending=" + std::to_string(status.naturalCommandsPending) +
                ", confirmations=" + std::to_string(status.naturalConfirmationsPending) +
                ", classified=" + std::to_string(status.naturalClassified) +
                ", dispatched=" + std::to_string(status.naturalDispatched) +
                ", rejected=" + std::to_string(status.naturalRejected) +
                ", expired=" + std::to_string(status.naturalExpired) +
                ", model=" + status.naturalCommandModel +
                ", acknowledgment=" + status.naturalAcknowledgementMode +
                ", avg-shortlist=" + std::to_string(status.naturalAverageShortlist) +
                ", avg-prompt-chars=" + std::to_string(status.naturalAveragePromptCharacters) +
                ", avg-latency-ms=" + std::to_string(status.naturalAverageClassifierLatencyMilliseconds) +
                ", audit=" + std::string(status.naturalAuditEnabled ? "enabled" : "disabled") +
                "(" + std::to_string(status.naturalAuditRecords) + ")" +
                (status.naturalMostUsedActions.empty() ? "" : ", top=" + status.naturalMostUsedActions) +
                (status.naturalLastFailure.empty() ? "" : ", last-failure=" + status.naturalLastFailure));
            if (status.naturalCommandsEnabled && !status.naturalCommandPrefixConfigured)
                errors = true;
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
            send(std::string("Chat system: ") + (status.enabled ? "OK" : "Not active"));
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

        void HandleNaturalCommandAdmin(ChatHandler* handler, std::string rest)
        {
            std::string action = TakeWord(rest);
            std::string argument = TakeWord(rest);
            if (action != "audit" || !TakeWord(rest).empty())
            {
                handler->SendSysMessage("Usage: .av natural audit [1-50|clear]");
                return;
            }
            if (argument == "clear")
            {
                Manager::Instance().ClearNaturalCommandAudit();
                handler->SendSysMessage("Azeroth Voices natural-command RAM audit cleared.");
                return;
            }
            size_t maximum = 20;
            if (!argument.empty())
            {
                if (argument.size() > 2 ||
                    !std::all_of(argument.begin(), argument.end(), [](unsigned char c) {
                        return std::isdigit(c) != 0;
                    }))
                {
                    handler->SendSysMessage("Usage: .av natural audit [1-50|clear]");
                    return;
                }
                maximum = static_cast<size_t>(std::stoul(argument));
                if (!maximum || maximum > 50)
                {
                    handler->SendSysMessage("Audit display count must be between 1 and 50.");
                    return;
                }
            }
            StatusSnapshot const status = Manager::Instance().GetStatus();
            if (!status.naturalAuditEnabled)
                handler->SendSysMessage("Natural-command audit is disabled; enable NaturalCommands.Audit.Enable and reload configuration.");
            std::vector<NaturalCommandAuditRecord> const records =
                Manager::Instance().GetNaturalCommandAudit(maximum);
            handler->SendSysMessage(("## Azeroth Voices Natural-Command Audit (newest first, " +
                std::to_string(records.size()) + " record(s))").c_str());
            for (NaturalCommandAuditRecord const& record : records)
            {
                std::ostringstream line;
                line << "time=" << record.createdUnix
                     << " request=" << record.requestId
                     << " player=" << record.playerGuid
                     << " bot=" << record.botGuid
                     << " action=" << (record.action.empty() ? "-" : record.action)
                     << " source=" << record.source
                     << " result=" << record.result
                     << " confidence=" << static_cast<uint32_t>(record.confidence * 100.0) << "%"
                     << " latency=" << record.latencyMilliseconds << "ms";
                if (!record.arguments.empty())
                    line << " arguments=" << record.arguments;
                handler->SendSysMessage(line.str());
            }
        }

        class AzerothVoicesCommandScript final : public AllCommandScript
        {
        public:
            AzerothVoicesCommandScript() : AllCommandScript("AzerothVoicesCommandScript") {}

            bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* arguments) override
            {
                bool const shortCommand = command && std::strcmp(command, "av") == 0;
                if (!shortCommand)
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

                if (subcommand == "natural")
                {
                    HandleNaturalCommandAdmin(handler, rest);
                    return false;
                }

                if (subcommand.empty() || subcommand == "status")
                {
                    StatusSnapshot status = Manager::Instance().GetStatus();
                    std::ostringstream text;
                    text << "Azeroth Voices: " << (status.enabled ? "enabled" : "disabled")
                         << (status.paused ? " (paused)" : "")
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
                         << ", natural-commands=" << (status.naturalCommandsEnabled ? "enabled" : "disabled")
                         << ", natural-native-prefix=" << (status.naturalCommandPrefixConfigured ? "configured" : "empty-unsafe")
                         << ", natural-actions=" << status.naturalCommandActions
                         << ", natural-shortlist-cap="
                         << (status.naturalCommandShortlistMaximum == 0 ? "auto(" : "")
                         << status.naturalCommandEffectiveShortlistMaximum
                         << (status.naturalCommandShortlistMaximum == 0 ? ")" : "")
                         << ", natural-recipients/actions=" << status.naturalCommandMaximumRecipients
                         << "x" << status.naturalCommandMaximumActions
                         << ", natural-pending=" << status.naturalCommandsPending
                         << ", natural-confirmations=" << status.naturalConfirmationsPending
                         << ", natural-classified=" << status.naturalClassified
                         << ", natural-dispatched=" << status.naturalDispatched
                         << ", natural-rejected=" << status.naturalRejected
                         << ", natural-expired=" << status.naturalExpired
                         << (status.naturalLastFailure.empty() ? "" : ", natural-last-failure=" + status.naturalLastFailure)
                         << ", rag=" << (status.ragEnabled ? "enabled" : "disabled")
                         << ", rag-entries=" << status.ragEntries
                         << ", model=" << status.model
                         << ", endpoint=" << status.endpoint;
                    handler->SendSysMessage(text.str());
                    std::ostringstream natural;
                    natural << "Natural command metrics: considered=" << status.naturalConsidered
                            << ", local=" << status.naturalLocalFastPath
                            << ", classifier=" << status.naturalClassifierQueued
                            << ", avg-shortlist=" << status.naturalAverageShortlist
                            << ", avg-prompt-chars=" << status.naturalAveragePromptCharacters
                            << ", avg-latency-ms=" << status.naturalAverageClassifierLatencyMilliseconds
                            << ", model=" << status.naturalCommandModel
                            << ", acknowledgment=" << status.naturalAcknowledgementMode
                            << ", audit=" << (status.naturalAuditEnabled ? "enabled" : "disabled")
                            << '(' << status.naturalAuditRecords << ')'
                            << (status.naturalMostUsedActions.empty() ? "" :
                                ", top=" + status.naturalMostUsedActions);
                    handler->SendSysMessage(natural.str());
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

                handler->SendSysMessage("av: test | status | pause | resume | restart | clearhistory | chatter [topic] | live <bot-or-> [prompt] | personality show/status/regenerate/delete <bot> | personality delete all | natural audit [1-50|clear]");
                return false;
            }
        };
    }

    void RegisterAzerothVoicesCommand()
    {
        new AzerothVoicesCommandScript();
    }
}
