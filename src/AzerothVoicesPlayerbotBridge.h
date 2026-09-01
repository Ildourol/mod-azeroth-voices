#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Player;

namespace AzerothVoices::PlayerbotBridge
{
    bool Available();
    bool IsNativePrefixedCommand(std::string const& message);
    bool IsOwnedGroupedBot(Player const* owner, Player const* bot);
    bool Dispatch(Player* owner, Player* bot,
                  std::string const& actionInput,
                  std::string const& nativeCommand,
                  std::string const& arguments,
                  std::string& error);
    std::vector<std::string> NearbyEnemyNames(Player* owner, size_t maximum,
                                              float range = 40.0f);
}
