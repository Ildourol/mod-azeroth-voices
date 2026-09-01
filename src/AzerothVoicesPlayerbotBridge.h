#pragma once

#include <cstdint>
#include <string>

class Player;

namespace AzerothVoices
{
    // Narrow world-thread-only adapter around the checked-out PlayerBots API.
    // Keeping botpch.h in the implementation prevents PlayerBots macros and
    // transitive dependencies from leaking into the main manager.
    class PlayerbotBridge final
    {
    public:
        static bool IsControlled(Player const* player);
        static Player* Master(Player* bot);
        static std::string CommandPrefix();
        static std::string CommandSeparator();
        static bool Dispatch(Player* speaker, Player* bot, std::string const& command);
    };
}
