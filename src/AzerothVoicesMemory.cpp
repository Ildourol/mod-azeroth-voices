#include "AzerothVoicesMemory.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        constexpr size_t MemorySummaryMaximumCharacters = 180;

        std::string Clean(std::string const& value)
        {
            std::string result;
            result.reserve(value.size());
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                char output = static_cast<char>(input);
                if (std::iscntrl(input))
                    output = ' ';
                bool const space = std::isspace(static_cast<unsigned char>(output)) != 0;
                if (space)
                {
                    if (previousSpace)
                        continue;
                    output = ' ';
                }
                result.push_back(output);
                previousSpace = space;
            }
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            result.erase(result.begin(), std::find_if(result.begin(), result.end(), notSpace));
            result.erase(std::find_if(result.rbegin(), result.rend(), notSpace).base(), result.end());
            return result;
        }

        std::string HeadBounded(std::string value, size_t maximum)
        {
            if (value.size() <= maximum)
                return value;
            size_t const cut = value.rfind(' ', maximum);
            value.erase(cut != std::string::npos && cut > 0 ? cut : maximum);
            return Clean(value);
        }

        bool AllPresent(std::initializer_list<std::string const*> values)
        {
            for (std::string const* value : values)
                if (!value || value->empty())
                    return false;
            return true;
        }

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }
    }

    char const* MemoryTypeName(MemoryType type)
    {
        switch (type)
        {
            case MemoryType::FirstMet: return "first_met";
            case MemoryType::PartyMember: return "party_member";
            case MemoryType::QuestCompleted: return "quest_completed";
            case MemoryType::DungeonCompleted: return "dungeon_completed";
            case MemoryType::LevelUp: return "level_up";
            case MemoryType::BossKill: return "boss_kill";
            case MemoryType::PvpKill: return "pvp_kill";
            case MemoryType::Wipe: return "wipe";
            case MemoryType::Achievement: return "achievement";
            case MemoryType::Count: break;
        }
        return "first_met";
    }

    bool ParseMemoryType(std::string const& name, MemoryType& type)
    {
        std::string const value = Lower(name);
        for (uint8_t i = 0; i < static_cast<uint8_t>(MemoryType::Count); ++i)
        {
            MemoryType const candidate = static_cast<MemoryType>(i);
            if (value == MemoryTypeName(candidate))
            {
                type = candidate;
                return true;
            }
        }
        return false;
    }

    uint32_t MemoryTypeImportance(MemoryType type)
    {
        switch (type)
        {
            case MemoryType::FirstMet: return 60;
            case MemoryType::PartyMember: return 25;
            case MemoryType::QuestCompleted: return 40;
            case MemoryType::DungeonCompleted: return 70;
            case MemoryType::LevelUp: return 45;
            case MemoryType::BossKill: return 80;
            case MemoryType::PvpKill: return 35;
            case MemoryType::Wipe: return 75;
            case MemoryType::Achievement: return 55;
            case MemoryType::Count: break;
        }
        return 25;
    }

    bool BuildMemorySummary(MemoryType type, MemoryFacts const& facts, std::string& summary)
    {
        std::ostringstream text;
        switch (type)
        {
            case MemoryType::FirstMet:
                if (!AllPresent({ &facts.zone }))
                    return false;
                text << "We first met in " << facts.zone << '.';
                break;
            case MemoryType::PartyMember:
                if (!AllPresent({ &facts.zone }))
                    return false;
                text << "We adventured together in " << facts.zone << '.';
                break;
            case MemoryType::QuestCompleted:
                if (!AllPresent({ &facts.quest }))
                    return false;
                text << "We completed " << facts.quest << " together";
                if (!facts.zone.empty())
                    text << " in " << facts.zone;
                text << '.';
                break;
            case MemoryType::DungeonCompleted:
                if (!AllPresent({ &facts.instance }))
                    return false;
                text << "We cleared " << facts.instance << " together.";
                break;
            case MemoryType::LevelUp:
                if (!facts.playerName.empty() && facts.level)
                    text << "I watched " << facts.playerName << " reach level " << facts.level << '.';
                else if (facts.level)
                    text << "I watched them reach level " << facts.level << '.';
                else
                    return false;
                break;
            case MemoryType::BossKill:
                if (!AllPresent({ &facts.boss }))
                    return false;
                text << "We killed " << facts.boss << " together";
                if (!facts.instance.empty())
                    text << " in " << facts.instance;
                text << '.';
                break;
            case MemoryType::PvpKill:
                if (!AllPresent({ &facts.victim }))
                    return false;
                text << "They defeated " << facts.victim << " in PvP.";
                break;
            case MemoryType::Wipe:
                if (facts.instance.empty())
                    return false;
                text << "Our group wiped in " << facts.instance << '.';
                break;
            case MemoryType::Achievement:
                if (!AllPresent({ &facts.achievement }))
                    return false;
                text << "They earned " << facts.achievement << '.';
                break;
            case MemoryType::Count:
                return false;
        }

        summary = HeadBounded(Clean(text.str()), MemorySummaryMaximumCharacters);
        return !summary.empty();
    }

    std::vector<MemoryRecord const*> SelectMemoriesForPrompt(
        std::vector<MemoryRecord> const& records, size_t maximumItems,
        size_t maximumCharacters)
    {
        std::vector<MemoryRecord const*> ranked;
        ranked.reserve(records.size());
        for (MemoryRecord const& record : records)
            if (!record.summary.empty())
                ranked.push_back(&record);
        std::stable_sort(ranked.begin(), ranked.end(),
            [](MemoryRecord const* left, MemoryRecord const* right) {
                if (left->importance != right->importance)
                    return left->importance > right->importance;
                return left->createdUnix > right->createdUnix;
            });

        std::vector<MemoryRecord const*> selected;
        size_t used = 0;
        for (MemoryRecord const* record : ranked)
        {
            if (selected.size() >= maximumItems)
                break;
            size_t const cost = record->summary.size() + 3;
            if (used + cost > maximumCharacters)
                continue;
            selected.push_back(record);
            used += cost;
        }
        return selected;
    }

    std::string BuildMemoryPromptBlock(std::string const& playerName,
                                       std::vector<MemoryRecord const*> const& selected,
                                       size_t maximumCharacters)
    {
        if (selected.empty())
            return "";

        std::ostringstream text;
        text << "THINGS YOU REMEMBER ABOUT " << (playerName.empty() ? "THIS PLAYER" : playerName)
             << ':';
        for (MemoryRecord const* record : selected)
            text << "\n- " << record->summary;
        text << "\nTreat these as your own past experiences with this player; mention one only when it "
                "fits naturally, and never invent memories that are not listed.";

        std::string block = text.str();
        if (block.size() > maximumCharacters)
            block.erase(maximumCharacters);
        return block;
    }

    size_t PruneMemoryRecords(std::vector<MemoryRecord>& records, size_t maximumPerPair)
    {
        if (records.size() <= maximumPerPair)
            return 0;
        std::stable_sort(records.begin(), records.end(),
            [](MemoryRecord const& left, MemoryRecord const& right) {
                return left.createdUnix > right.createdUnix;
            });
        size_t const removed = records.size() - maximumPerPair;
        records.resize(maximumPerPair);
        return removed;
    }
}
