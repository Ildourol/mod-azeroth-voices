#include "AzerothVoicesManager.h"

#include "Chat.h"
#include "Player.h"
#include "ScriptObjects.h"
#include "WorldSession.h"

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

        class AzerothVoicesCommandScript final : public AllCommandScript
        {
        public:
            AzerothVoicesCommandScript() : AllCommandScript("AzerothVoicesCommandScript") {}

            bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* arguments) override
            {
                if (!command || std::strcmp(command, "av") != 0)
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
                         << ", histories=" << status.conversations
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
                    handler->SendSysMessage("Azeroth Voices in-memory chat history cleared.");
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
                if (subcommand == "test")
                {
                    std::string actor = TakeWord(rest);
                    if (actor == "-")
                        actor.clear();
                    bool queued = Manager::Instance().QueueTest(issuer, actor, rest);
                    handler->SendSysMessage(queued ? "Provider test queued; the result will be delivered in game."
                                                   : "Test was not queued. Check module status and the bot name.");
                    return false;
                }

                handler->SendSysMessage("av: status | pause | resume | restart | clearhistory | chatter [topic] | test <bot-or-> [prompt]");
                return false;
            }
        };
    }

    void RegisterAzerothVoicesCommand()
    {
        new AzerothVoicesCommandScript();
    }
}
