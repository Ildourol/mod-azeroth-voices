#include "AzerothVoicesProximity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::vector<std::string> WordTokens(std::string const& value)
        {
            std::vector<std::string> words;
            std::string current;
            for (unsigned char c : value)
            {
                if (std::isalnum(c))
                {
                    current.push_back(static_cast<char>(std::tolower(c)));
                    continue;
                }
                if (!current.empty())
                {
                    words.push_back(current);
                    current.clear();
                }
            }
            if (!current.empty())
                words.push_back(current);
            return words;
        }

        bool ContainsPhrase(std::vector<std::string> const& words,
                            std::vector<std::string> const& phrase)
        {
            if (phrase.empty() || phrase.size() > words.size())
                return false;
            for (size_t start = 0; start + phrase.size() <= words.size(); ++start)
            {
                bool match = true;
                for (size_t i = 0; i < phrase.size(); ++i)
                    if (words[start + i] != phrase[i])
                    {
                        match = false;
                        break;
                    }
                if (match)
                    return true;
            }
            return false;
        }

        bool ContainsWord(std::vector<std::string> const& words, std::string const& word)
        {
            return std::find(words.begin(), words.end(), word) != words.end();
        }
    }

    ProximitySpeakerDecision EvaluateProximitySpeaker(ProximitySpeakerInput const& input)
    {
        ProximitySpeakerDecision decision;
        if (input.safetyExcluded)
        {
            decision.reason = ProximityRejection::SafetyExclusion;
            return decision;
        }
        if (input.denied)
        {
            decision.reason = ProximityRejection::Denied;
            return decision;
        }
        if (input.boss)
        {
            decision.reason = ProximityRejection::Boss;
            return decision;
        }
        if (input.guardOrService)
        {
            decision.reason = ProximityRejection::ServiceRole;
            return decision;
        }
        if (!input.allowListEmpty && !input.entryAllowed)
        {
            decision.reason = ProximityRejection::NotAllowed;
            return decision;
        }
        if (input.creatureTypeKnown && input.humanoid)
        {
            decision.eligible = true;
            return decision;
        }
        if (input.creatureTypeKnown && input.entryAllowedAsNonHumanoid)
        {
            decision.eligible = true;
            return decision;
        }
        decision.reason = ProximityRejection::NonHumanoid;
        return decision;
    }

    char const* ProximityRejectionName(ProximityRejection reason)
    {
        switch (reason)
        {
            case ProximityRejection::None: return "eligible";
            case ProximityRejection::SafetyExclusion: return "safety-exclusion";
            case ProximityRejection::Denied: return "denied";
            case ProximityRejection::Boss: return "boss";
            case ProximityRejection::ServiceRole: return "service-role";
            case ProximityRejection::NotAllowed: return "not-allowed";
            case ProximityRejection::NonHumanoid: return "non-humanoid";
            case ProximityRejection::Count: break;
        }
        return "unknown";
    }

    bool IsProximityMapEligible(bool battleground, bool arena, bool dungeon, bool allowInstances)
    {
        if (battleground || arena)
            return false;
        if (dungeon)
            return allowInstances;
        return true;
    }

    uint32_t ApplyZoneFatigue(uint32_t baseChance, uint32_t scenes,
                              uint32_t threshold, uint32_t decayPercent)
    {
        if (!baseChance)
            return 0;
        if (scenes <= threshold || decayPercent == 0)
            return baseChance;

        double factor = 1.0;
        uint32_t const steps = std::min<uint32_t>(scenes - threshold, 64);
        for (uint32_t i = 0; i < steps; ++i)
            factor *= (100.0 - std::min<uint32_t>(100, decayPercent)) / 100.0;

        double const scaled = static_cast<double>(baseChance) * factor;
        uint32_t const result = static_cast<uint32_t>(scaled);
        return std::max<uint32_t>(1, std::min(baseChance, result));
    }

    uint32_t DecayZoneScenes(double scenes, uint32_t decayPercent)
    {
        if (scenes <= 0.0)
            return 0;
        double const factor = (100.0 - std::min<uint32_t>(100, decayPercent)) / 100.0;
        double const decayed = scenes * factor;
        if (decayed < 1.0)
            return 0;
        return static_cast<uint32_t>(std::floor(decayed));
    }

    NpcNameMatch SelectNamedNpc(std::vector<std::string> const& candidateNames,
                                std::string const& message)
    {
        NpcNameMatch result;
        std::vector<std::string> const spoken = WordTokens(message);
        if (spoken.empty() || candidateNames.empty())
            return result;

        std::vector<size_t> fullMatches;
        for (size_t i = 0; i < candidateNames.size(); ++i)
        {
            std::vector<std::string> const phrase = WordTokens(candidateNames[i]);
            if (phrase.size() >= 2 && ContainsPhrase(spoken, phrase))
                fullMatches.push_back(i);
        }
        if (fullMatches.size() == 1)
        {
            result.kind = NameMatchKind::FullName;
            result.index = fullMatches.front();
            result.found = true;
            return result;
        }
        if (fullMatches.size() > 1)
        {
            result.kind = NameMatchKind::Ambiguous;
            return result;
        }

        std::vector<size_t> firstMatches;
        for (size_t i = 0; i < candidateNames.size(); ++i)
        {
            std::vector<std::string> const phrase = WordTokens(candidateNames[i]);
            if (!phrase.empty() && ContainsWord(spoken, phrase.front()))
                firstMatches.push_back(i);
        }
        if (firstMatches.size() == 1)
        {
            result.kind = NameMatchKind::UniqueFirstName;
            result.index = firstMatches.front();
            result.found = true;
            return result;
        }
        if (firstMatches.size() > 1)
            result.kind = NameMatchKind::Ambiguous;
        return result;
    }
}
