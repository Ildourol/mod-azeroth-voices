#include "AzerothVoicesPlayerbotBridge.h"

// PlayerBots relies on its compatibility shim and ordered header bundle.
// Keep that dependency confined to this bridge translation unit.
#include "botpch.h"

#include <algorithm>
#include <cctype>
#include <list>
#include <set>

namespace AzerothVoices::PlayerbotBridge
{
    namespace
    {
        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string Normalize(std::string const& value)
        {
            std::string result;
            result.reserve(value.size());
            bool space = true;
            for (unsigned char c : value)
            {
                if (std::isalnum(c))
                {
                    result.push_back(static_cast<char>(std::tolower(c)));
                    space = false;
                }
                else if (!space)
                {
                    result.push_back(' ');
                    space = true;
                }
            }
            if (!result.empty() && result.back() == ' ')
                result.pop_back();
            return result;
        }

        Unit* FindNearbyEnemy(Player* owner, std::string const& spokenName, float range)
        {
            std::string query = Normalize(spokenName);
            if (query.compare(0, 4, "the ") == 0)
                query.erase(0, 4);
            if (query.empty())
                return nullptr;

            std::list<Unit*> units;
            MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(owner, owner, range);
            MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
            Cell::VisitAllObjects(owner, searcher, range);

            Unit* best = nullptr;
            int bestRank = 0;
            float bestDistance = 0.0f;
            for (Unit* unit : units)
            {
                if (!unit || !unit->IsAlive() || !unit->IsInWorld() ||
                    !owner->IsValidAttackTarget(unit))
                    continue;
                std::string const name = Normalize(unit->GetName());
                int rank = name == query ? 2 : (name.find(query) != std::string::npos ? 1 : 0);
                if (!rank && query.size() > 3 && query.back() == 's')
                {
                    std::string const singular = query.substr(0, query.size() - 1);
                    rank = name == singular ? 2 : (name.find(singular) != std::string::npos ? 1 : 0);
                }
                if (!rank)
                    continue;
                float const distance = owner->GetDistance(unit);
                if (!best || rank > bestRank || (rank == bestRank && distance < bestDistance))
                {
                    best = unit;
                    bestRank = rank;
                    bestDistance = distance;
                }
            }
            return best;
        }
    }

    bool Available()
    {
        return !sPlayerbotAIConfig.commandPrefix.empty();
    }

    bool IsNativePrefixedCommand(std::string const& message)
    {
        std::string const& prefix = sPlayerbotAIConfig.commandPrefix;
        return !prefix.empty() && message.compare(0, prefix.size(), prefix) == 0;
    }

    bool IsOwnedGroupedBot(Player const* owner, Player const* bot)
    {
        if (!owner || !bot || owner == bot || !owner->IsInWorld() || !bot->IsInWorld() ||
            !owner->GetGroup() || owner->GetGroup() != bot->GetGroup())
            return false;
        PlayerbotAI* ai = GetBotAI(bot);
        return ai && ai->GetMaster() == owner;
    }

    bool Dispatch(Player* owner, Player* bot,
                  std::string const& actionInput,
                  std::string const& nativeCommand,
                  std::string const& arguments,
                  std::string& error)
    {
        error.clear();
        if (!IsOwnedGroupedBot(owner, bot) || !owner->GetSession())
        {
            error = "the PlayerBot is no longer controlled by that player in the same group";
            return false;
        }
        std::string const& prefix = sPlayerbotAIConfig.commandPrefix;
        if (prefix.empty())
        {
            error = "AiPlayerbot.CommandPrefix is empty";
            return false;
        }

        std::string trailing = Trim(arguments);
        if (trailing.size() > 160 || trailing.find('\r') != std::string::npos ||
            trailing.find('\n') != std::string::npos ||
            (!sPlayerbotAIConfig.commandSeparator.empty() &&
             trailing.find(sPlayerbotAIConfig.commandSeparator) != std::string::npos))
        {
            error = "the generated native arguments were invalid";
            return false;
        }

        if (!trailing.empty() &&
            (actionInput == "attack" || actionInput == "tank attack" || actionInput == "pull"))
        {
            Unit* target = FindNearbyEnemy(owner, trailing, 60.0f);
            if (!target)
            {
                error = "the named enemy is no longer nearby";
                return false;
            }
            // This mirrors WoW Legends: the native attack/pull actions read
            // the master's selection on the bot tick, so a resolved spoken
            // target becomes the master's live selection before handoff.
            owner->SetSelectionGuid(target->GetObjectGuid());
            trailing.clear();
        }

        PlayerbotAI* ai = GetBotAI(bot);
        if (!ai)
        {
            error = "the PlayerBot AI is unavailable";
            return false;
        }
        std::string command = prefix + nativeCommand;
        if (!trailing.empty())
            command += " " + trailing;
        ai->HandleCommand(CHAT_MSG_WHISPER, command, *owner, LANG_UNIVERSAL);
        return true;
    }

    std::vector<std::string> NearbyEnemyNames(Player* owner, size_t maximum, float range)
    {
        std::vector<std::string> result;
        if (!owner || !owner->IsInWorld() || !maximum)
            return result;

        std::list<Unit*> units;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(owner, owner, range);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(owner, searcher, range);
        std::set<std::string> seen;
        for (Unit* unit : units)
        {
            if (!unit || !unit->IsAlive() || !unit->IsInWorld() ||
                !owner->IsValidAttackTarget(unit))
                continue;
            std::string const name = unit->GetName();
            if (name.empty() || !seen.insert(Normalize(name)).second)
                continue;
            result.push_back(name);
            if (result.size() >= maximum)
                break;
        }
        return result;
    }
}
