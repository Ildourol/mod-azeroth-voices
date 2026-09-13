#include "AzerothVoicesBossDialogue.h"

#include <algorithm>
#include <cctype>

namespace AzerothVoices
{
    namespace
    {
        // CreatureFlagsExtra, kept local so this file stays free of game headers.
        constexpr uint32_t ExtraFlagInstanceBind = 0x00000001;
        // CREATURE_ELITE_WORLDBOSS
        constexpr uint32_t RankWorldBoss = 3;

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

        uint32_t CountWords(std::string const& value)
        {
            uint32_t words = 0;
            bool inWord = false;
            for (unsigned char c : value)
            {
                if (std::isalnum(c))
                {
                    if (!inWord)
                    {
                        inWord = true;
                        ++words;
                    }
                    continue;
                }
                inWord = false;
            }
            return words;
        }
    }

    BossClassification ClassifyBoss(uint32_t entry, uint32_t rank, uint32_t flagsExtra,
                                    bool inDungeonOrRaid, bool allowListHit, bool denyListHit)
    {
        BossClassification result;
        if (denyListHit)
        {
            result.source = BossClassificationSource::Denied;
            return result;
        }
        if (allowListHit)
        {
            result.boss = true;
            result.source = BossClassificationSource::Allowed;
            return result;
        }
        if (inDungeonOrRaid && entry && IsCuratedBossEntry(entry))
        {
            result.boss = true;
            result.source = BossClassificationSource::CuratedRegistry;
            return result;
        }
        if (inDungeonOrRaid && (flagsExtra & ExtraFlagInstanceBind))
        {
            result.boss = true;
            result.source = BossClassificationSource::InstanceBindFlag;
            return result;
        }
        if (rank == RankWorldBoss)
        {
            result.boss = true;
            result.source = BossClassificationSource::WorldBossRank;
            return result;
        }
        return result;
    }

    char const* BossClassificationSourceName(BossClassificationSource source)
    {
        switch (source)
        {
            case BossClassificationSource::None: return "none";
            case BossClassificationSource::Denied: return "denied";
            case BossClassificationSource::Allowed: return "allow-list";
            case BossClassificationSource::CuratedRegistry: return "curated-registry";
            case BossClassificationSource::InstanceBindFlag: return "instance-bind";
            case BossClassificationSource::WorldBossRank: return "world-boss-rank";
            case BossClassificationSource::Count: break;
        }
        return "none";
    }

    bool ValidateBossLine(std::string const& line, uint32_t minimumWords,
                          uint32_t maximumWords, uint32_t maximumCharacters,
                          std::string& cleaned)
    {
        cleaned = Clean(line);
        if (cleaned.empty() || !maximumCharacters)
            return false;

        if (cleaned.size() > maximumCharacters)
        {
            size_t const cut = cleaned.rfind(' ', maximumCharacters);
            cleaned.erase(cut != std::string::npos && cut > 0 ? cut : maximumCharacters);
            cleaned = Clean(cleaned);
        }
        if (cleaned.empty())
            return false;

        uint32_t const words = CountWords(cleaned);
        return words >= minimumWords && words <= maximumWords;
    }
}
