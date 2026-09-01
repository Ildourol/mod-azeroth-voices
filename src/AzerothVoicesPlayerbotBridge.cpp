#include "AzerothVoicesPlayerbotBridge.h"

#include "botpch.h"

namespace AzerothVoices
{
    bool PlayerbotBridge::IsControlled(Player const* player)
    {
        return player && Script_IsAIControlled(player);
    }

    Player* PlayerbotBridge::Master(Player* bot)
    {
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        return ai ? ai->GetMaster() : nullptr;
    }

    std::string PlayerbotBridge::CommandPrefix()
    {
        return sPlayerbotAIConfig.commandPrefix;
    }

    std::string PlayerbotBridge::CommandSeparator()
    {
        return sPlayerbotAIConfig.commandSeparator;
    }

    bool PlayerbotBridge::Dispatch(Player* speaker, Player* bot, std::string const& command)
    {
        PlayerbotAI* ai = bot ? GetBotAI(bot) : nullptr;
        if (!speaker || !ai)
            return false;
        ai->HandleCommand(CHAT_MSG_WHISPER,
            sPlayerbotAIConfig.commandPrefix + command, *speaker, LANG_UNIVERSAL);
        return true;
    }
}
