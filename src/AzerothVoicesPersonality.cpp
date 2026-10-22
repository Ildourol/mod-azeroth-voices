#include "AzerothVoicesPersonality.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <random>
#include <set>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        using Json = nlohmann::json;

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string CleanGeneratedText(std::string const& value)
        {
            std::string result;
            result.reserve(value.size());
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                char output = static_cast<char>(input);
                if (std::iscntrl(input) || std::isspace(input))
                    output = ' ';
                bool const space = output == ' ';
                if (space && previousSpace)
                    continue;
                result.push_back(output);
                previousSpace = space;
            }
            return Trim(result);
        }

        std::string HeadBounded(std::string value, size_t maximum)
        {
            if (value.size() <= maximum)
                return value;
            if (maximum <= 3)
                return value.substr(0, maximum);
            value.resize(maximum - 3);
            value += "...";
            return value;
        }
    }

    std::string CleanHeadBounded(std::string value, size_t maximum)
    {
        if (value.size() <= maximum)
            return value;
        if (maximum <= 3)
            return value.substr(0, maximum);

        size_t const searchLimit = maximum - 1;
        size_t lastPunct = std::string::npos;
        for (size_t i = searchLimit; i > 0; --i)
        {
            char c = value[i];
            if (c == '.' || c == '!' || c == '?')
            {
                if (i + 1 == value.size() || value[i + 1] == ' ' || value[i + 1] == '"' || value[i + 1] == '\'')
                {
                    lastPunct = i;
                    break;
                }
            }
            if (searchLimit - i > 120)
                break;
        }

        if (lastPunct != std::string::npos && lastPunct >= (maximum / 2))
        {
            return value.substr(0, lastPunct + 1);
        }

        value.resize(maximum - 3);
        value += "...";
        return value;
    }

    namespace
    {

        bool TryParseEmbeddedJsonObject(std::string const& value, Json& parsed)
        {
            for (size_t start = value.find('{'); start != std::string::npos;
                 start = value.find('{', start + 1))
            {
                size_t depth = 0;
                bool inString = false;
                bool escaped = false;
                for (size_t i = start; i < value.size(); ++i)
                {
                    char const current = value[i];
                    if (inString)
                    {
                        if (escaped)
                            escaped = false;
                        else if (current == '\\')
                            escaped = true;
                        else if (current == '"')
                            inString = false;
                        continue;
                    }

                    if (current == '"')
                    {
                        inString = true;
                        continue;
                    }
                    if (current == '{')
                    {
                        ++depth;
                        continue;
                    }
                    if (current != '}' || !depth)
                        continue;

                    --depth;
                    if (depth)
                        continue;

                    try
                    {
                        Json candidate = Json::parse(value.substr(start, i - start + 1));
                        if (candidate.is_object())
                        {
                            parsed = std::move(candidate);
                            return true;
                        }
                    }
                    catch (std::exception const&)
                    {
                    }
                    break;
                }
            }
            return false;
        }
    }

    std::string BuildPersonalityGenerationSystemPrompt(Config const& config,
                                                       PersonalityGenerationMode mode)
    {
        std::ostringstream prompt;
        switch (mode)
        {
            case PersonalityGenerationMode::Full:
                prompt << "Create one persistent fictional World of Warcraft PlayerBot identity. ";
                break;
            case PersonalityGenerationMode::ReplaceTraits:
                prompt << "Refine one persistent fictional World of Warcraft PlayerBot identity whose three "
                          "personality traits are already fixed. ";
                break;
            case PersonalityGenerationMode::BackgroundOnly:
                prompt << "Write a new background story for one existing persistent fictional World of Warcraft "
                          "PlayerBot identity whose traits and speaking tone are already fixed. ";
                break;
            case PersonalityGenerationMode::ToneOnly:
                prompt << "Generate a speaking style and tone description for one existing persistent fictional World of Warcraft "
                          "PlayerBot identity whose personality traits are already fixed. ";
                break;
        }
        prompt << "Return exactly one JSON object and no markdown or explanation.";

        if (mode == PersonalityGenerationMode::BackgroundOnly)
        {
            prompt << " The object must contain only background. ";
        }
        else if (mode == PersonalityGenerationMode::ToneOnly)
        {
            prompt << " The object must contain only tone. Do not return or rewrite traits or background. ";
        }
        else if (mode == PersonalityGenerationMode::ReplaceTraits)
        {
            prompt << " The object must contain only tone and background. Do not return or rewrite traits. ";
        }
        else
        {
            prompt << " The object must contain only traits, tone, and background. "
                   << config.personalityPromptTraits << " ";
        }

        if (mode != PersonalityGenerationMode::BackgroundOnly)
        {
            if (config.personalityGenerateTone)
                prompt << config.personalityPromptTone << " ";
            else
                prompt << "tone must be an empty string. ";
        }

        if (mode != PersonalityGenerationMode::ToneOnly)
        {
            if (!config.personalityGenerateBackground)
                prompt << "background must be an empty string. ";
            else if (config.personalityBackgroundMode == 0)
                prompt << "background must follow this guidance: " << config.personalityPromptRoleplay << " ";
            else
                prompt << "background must follow this guidance: " << config.personalityPromptNormal << " ";

            if (config.personalityBackstorySentences > 0)
            {
                prompt << "background must be exactly " << config.personalityBackstorySentences
                       << (config.personalityBackstorySentences == 1 ? " sentence. " : " sentences. ");
            }

            prompt << "Vary premise, structure, and emphasis naturally instead of forcing every identity into the same "
                      "biographical sequence. Do not mention randomness, generation, prompts, or these instructions. ";

            if (config.personalityMaxBackgroundCharacters > 0)
                prompt << "Keep background at or below " << config.personalityMaxBackgroundCharacters << " characters. ";
        }
        switch (mode)
        {
            case PersonalityGenerationMode::Full:
                prompt << "JSON shape: {\"traits\":[\"...\",\"...\",\"...\"],\"tone\":\"...\",\"background\":\"...\"}.";
                break;
            case PersonalityGenerationMode::ReplaceTraits:
                prompt << "JSON shape: {\"tone\":\"...\",\"background\":\"...\"}.";
                break;
            case PersonalityGenerationMode::BackgroundOnly:
                prompt << "JSON shape: {\"background\":\"...\"}. The \"background\" property must contain the backstory paragraph.";
                break;
            case PersonalityGenerationMode::ToneOnly:
                prompt << "JSON shape: {\"tone\":\"...\"}.";
                break;
        }
        return prompt.str();
    }

    std::string BuildPersonalityGenerationUserPrompt(Config const& config, ActorSnapshot const& actor,
                                                     PersonalityGenerationMode mode,
                                                     BotPersonality const& fixed)
    {
        std::ostringstream prompt;
        prompt << "Character name: " << actor.name
               << "\nRace: " << actor.race
               << "\nClass: " << actor.className
               << "\nFaction: " << actor.faction
               << "\nGender: " << actor.gender
               << "\nBackground mode: "
               << (config.personalityBackgroundMode == 0 ? "roleplay Azeroth character" : "authentic real-world individual");

        if (config.personalityBackgroundMode != 0)
        {
            static thread_local std::mt19937 engine(std::random_device{}());
            uint32_t const realLifeAge = std::uniform_int_distribution<uint32_t>(19, 58)(engine);
            prompt << "\nPlayer real-life age: " << realLifeAge;
        }

        if (mode != PersonalityGenerationMode::Full && !fixed.traits.empty())
        {
            prompt << "\nFixed personality traits: " << JoinPersonalityTraits(fixed.traits);
            if (mode == PersonalityGenerationMode::BackgroundOnly && !fixed.tone.empty())
                prompt << "\nFixed speaking tone: " << fixed.tone;
            if (mode == PersonalityGenerationMode::BackgroundOnly && !fixed.background.empty())
                prompt << "\nDo not reuse this previous background story: " << fixed.background;
            if (mode == PersonalityGenerationMode::ToneOnly && !fixed.tone.empty())
                prompt << "\nDo not reuse this previous speaking tone: " << fixed.tone;
            if (mode == PersonalityGenerationMode::ToneOnly && !fixed.background.empty())
                prompt << "\nCharacter background: " << fixed.background;
        }
        return prompt.str();
    }

    uint32_t PersonalityGenerationTokenBudget(Config const& config, PersonalityGenerationMode mode)
    {
        if (config.personalityMaxTokens > 0)
            return config.personalityMaxTokens;

        bool const writesBackground = mode != PersonalityGenerationMode::ToneOnly &&
            (config.personalityGenerateBackground || mode == PersonalityGenerationMode::BackgroundOnly);

        if (!writesBackground)
            return 64;

        if (config.personalityMaxBackgroundCharacters > 0)
        {
            uint32_t estimate = config.personalityMaxBackgroundCharacters / 3 + 64;
            return std::min<uint32_t>(2048, std::max<uint32_t>(64, estimate));
        }

        // Auto mode (0 = unconstrained): default budget allowing natural rich backstories
        return 512;
    }

    bool ValidatePersonalityTraits(std::vector<std::string> const& traits, std::string& error)
    {
        if (traits.size() != PersonalityTraitCount)
        {
            error = "exactly " + std::to_string(PersonalityTraitCount) + " traits are required";
            return false;
        }
        std::set<std::string> unique;
        for (std::string const& value : traits)
        {
            std::string const trait = CleanGeneratedText(value);
            if (trait.empty() || trait.size() > PersonalityTraitMaximumCharacters)
            {
                error = "a trait is empty or longer than " +
                    std::to_string(PersonalityTraitMaximumCharacters) + " characters";
                return false;
            }
            if (!unique.insert(Lower(trait)).second)
            {
                error = "traits must be distinct";
                return false;
            }
        }
        return true;
    }

    bool ParsePersonalityResponse(Config const& config, ActorSnapshot const& actor,
                                  PersonalityGenerationMode mode, BotPersonality const& fixed,
                                  std::string const& response, BotPersonality& personality,
                                  std::string& error)
    {
        try
        {
            Json root;
            std::string const trimmed = Trim(response);
            bool isJson = false;
            try
            {
                root = Json::parse(trimmed);
                isJson = root.is_object();
            }
            catch (std::exception const& /*directError*/)
            {
                if (TryParseEmbeddedJsonObject(trimmed, root) && root.is_object())
                    isJson = true;
            }

            if (!isJson)
            {
                // Fallback for single-field modes (BackgroundOnly / ToneOnly) if LLM responded with free text
                // like mod-llm-chatter-wotlk does (no JSON brackets, plain text response).
                if (mode == PersonalityGenerationMode::BackgroundOnly && !trimmed.empty() && trimmed.front() != '<' && trimmed.front() != '[')
                {
                    root = Json::object();
                    root["background"] = trimmed;
                }
                else if (mode == PersonalityGenerationMode::ToneOnly && !trimmed.empty() && trimmed.front() != '<' && trimmed.front() != '[')
                {
                    root = Json::object();
                    root["tone"] = trimmed;
                }
                else
                {
                    error = "personality response is not a JSON object";
                    return false;
                }
            }
            static std::set<std::string> const allowed = { "traits", "tone", "background" };
            for (auto it = root.begin(); it != root.end(); ++it)
                if (!allowed.count(it.key()))
                {
                    error = "personality response contains unexpected field " + it.key();
                    return false;
                }

            if (mode == PersonalityGenerationMode::Full)
            {
                if (root.size() != allowed.size() || !root.count("traits") ||
                    !root.count("tone") || !root.count("background"))
                {
                    error = "personality response must contain exactly traits, tone, and background";
                    return false;
                }
            }
            else if (mode == PersonalityGenerationMode::ToneOnly)
            {
                if (!root.count("tone"))
                {
                    error = "personality response is missing tone field";
                    return false;
                }
            }
            else if (!root.count("background") ||
                     (mode == PersonalityGenerationMode::ReplaceTraits &&
                      config.personalityGenerateTone && !root.count("tone")))
            {
                error = "personality response is missing a required field";
                return false;
            }

            std::vector<std::string> traits;
            if (mode == PersonalityGenerationMode::Full)
            {
                if (!root["traits"].is_array() || root["traits"].size() != PersonalityTraitCount)
                {
                    error = "personality response has the wrong trait count";
                    return false;
                }
                for (Json const& value : root["traits"])
                {
                    if (!value.is_string())
                    {
                        error = "personality trait is not a string";
                        return false;
                    }
                    traits.push_back(CleanGeneratedText(value.get<std::string>()));
                }
                if (!ValidatePersonalityTraits(traits, error))
                    return false;
            }
            else
            {
                traits = fixed.traits;
                if (!ValidatePersonalityTraits(traits, error))
                {
                    error = "fixed personality traits are unusable: " + error;
                    return false;
                }
            }

            std::string tone = mode == PersonalityGenerationMode::BackgroundOnly ? fixed.tone : std::string();
            if (mode != PersonalityGenerationMode::BackgroundOnly && root.count("tone"))
            {
                if (!root["tone"].is_string())
                {
                    error = "personality tone is not a string";
                    return false;
                }
                tone = CleanGeneratedText(root["tone"].get<std::string>());
            }
            if (mode != PersonalityGenerationMode::BackgroundOnly && config.personalityGenerateTone &&
                tone.empty())
            {
                error = "personality tone is empty";
                return false;
            }
            if (mode != PersonalityGenerationMode::BackgroundOnly && tone.size() > 200)
            {
                tone = HeadBounded(tone, 200);
            }
            if (mode != PersonalityGenerationMode::BackgroundOnly && !config.personalityGenerateTone)
                tone.clear();

            std::string background = mode == PersonalityGenerationMode::ToneOnly ? fixed.background : std::string();
            if (mode != PersonalityGenerationMode::ToneOnly && root.count("background"))
            {
                if (!root["background"].is_string())
                {
                    error = "personality background is not a string";
                    return false;
                }
                background = CleanGeneratedText(root["background"].get<std::string>());
            }
            bool const expectBackground = mode != PersonalityGenerationMode::ToneOnly &&
                (config.personalityGenerateBackground || mode == PersonalityGenerationMode::BackgroundOnly);
            if (expectBackground && background.empty())
            {
                error = "personality background is empty";
                return false;
            }
            if (expectBackground && config.personalityMaxBackgroundCharacters > 0 &&
                background.size() > config.personalityMaxBackgroundCharacters)
            {
                background = CleanHeadBounded(background, config.personalityMaxBackgroundCharacters);
            }
            personality = BotPersonality();
            personality.characterGuid = actor.guid;
            personality.botName = actor.name;
            personality.traits = std::move(traits);
            personality.tone = std::move(tone);
            personality.background = std::move(background);
            personality.backgroundMode = config.personalityBackgroundMode;
            personality.generationVersion = PersonalityGenerationVersion;
            return true;
        }
        catch (std::exception const& exception)
        {
            error = std::string("personality response JSON error: ") + exception.what();
            return false;
        }
    }

    namespace
    {
        bool RollPersonalityChance(uint32_t chance)
        {
            if (chance == 0)
                return false;
            if (chance >= 100)
                return true;
            static thread_local std::mt19937 engine(std::random_device{}());
            return std::uniform_int_distribution<uint32_t>(1, 100)(engine) <= chance;
        }

    }

    std::string BuildVoicePromptBlock(Config const& config, BotPersonality const& personality)
    {
        if (!config.personalityEnabled || personality.traits.empty())
            return "";

        std::ostringstream block;
        if (config.personalityBackgroundMode == 0)
            block << "You are a character living in Azeroth. ";
        else
            block << "You are a real player behind this character with an everyday life outside WoW. ";

        block << "Traits: " << JoinPersonalityTraits(personality.traits) << ".";
        if (!personality.tone.empty())
            block << " Tone: " << personality.tone << ".";

        block << " Personality and tone shape how you speak, not subjects to repeat in dialogue.";

        return block.str();
    }

    std::string BuildBackstoryPromptBlock(Config const& config, BotPersonality const& personality,
                                          bool /*isDirectInquiry*/)
    {
        if (!config.personalityEnabled || personality.background.empty())
            return "";

        std::ostringstream block;
        block << "<backstory>\n"
              << personality.background << "\n"
              << "Keep private: only mention or draw from this if asked about who you are, what you do, or your life.\n"
              << "</backstory>";
        return block.str();
    }

    bool ShouldIncludeBackstory(Config const& config, BotPersonality const& personality,
                                std::string const& trigger)
    {
        if (!config.personalityEnabled || personality.background.empty())
            return false;

        // Tactical & combat suppression: omit during combat, aggro, tactical actions, or PvP
        if (trigger == "combat" || trigger == "boss-dialogue" || trigger == "aggro" ||
            trigger == "assist" || trigger == "tactical" || trigger == "spell-cast" ||
            trigger == "heal-request" || trigger == "death" || trigger == "flee" ||
            trigger.compare(0, 5, "duel:") == 0 || trigger.compare(0, 4, "pvp:") == 0)
            return false;

        return true;
    }

    bool ShouldIncludeBackstory(Config const& config, BotPersonality const& personality,
                                ChatScope /*scope*/, std::string const& trigger,
                                std::string const& /*message*/, bool /*directConversation*/, bool /*ambient*/)
    {
        return ShouldIncludeBackstory(config, personality, trigger);
    }

    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality,
                                            ChatScope /*scope*/, std::string const& trigger,
                                            std::string const& /*message*/, bool /*directConversation*/, bool /*ambient*/)
    {
        std::string voice = BuildVoicePromptBlock(config, personality);
        if (voice.empty())
            return "";

        std::ostringstream block;
        block << voice;

        if (ShouldIncludeBackstory(config, personality, trigger))
        {
            std::string backstory = BuildBackstoryPromptBlock(config, personality);
            if (!backstory.empty())
                block << "\n" << backstory;
        }

        if (config.personalityMaxPromptCharacters == 0)
            return block.str();

        return HeadBounded(block.str(), config.personalityMaxPromptCharacters);
    }

    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality)
    {
        std::string voice = BuildVoicePromptBlock(config, personality);
        if (voice.empty())
            return "";

        std::string backstory = BuildBackstoryPromptBlock(config, personality);
        std::string combined = backstory.empty() ? voice : (voice + "\n" + backstory);
        if (config.personalityMaxPromptCharacters == 0)
            return combined;

        return HeadBounded(combined, config.personalityMaxPromptCharacters);
    }

    std::string JoinPersonalityTraits(std::vector<std::string> const& traits)
    {
        std::string result;
        for (std::string const& trait : traits)
        {
            if (!result.empty())
                result += ", ";
            result += trait;
        }
        return result;
    }

    std::string SerializePersonalityTraits(std::vector<std::string> const& traits)
    {
        return Json(traits).dump();
    }

    bool ParseStoredPersonalityTraits(std::string const& value, std::vector<std::string>& traits)
    {
        try
        {
            Json root = Json::parse(value);
            if (!root.is_array())
                return false;
            std::vector<std::string> parsed;
            std::set<std::string> uniqueTraits;
            for (Json const& item : root)
            {
                if (!item.is_string())
                    return false;
                std::string trait = CleanGeneratedText(item.get<std::string>());
                if (trait.empty() || trait.size() > PersonalityTraitMaximumCharacters ||
                    !uniqueTraits.insert(Lower(trait)).second)
                    return false;
                parsed.push_back(std::move(trait));
            }
            traits = std::move(parsed);
            // A stored row with any other trait count is stale by definition and
            // is regenerated lazily through the normal personality pipeline.
            return traits.size() == PersonalityTraitCount;
        }
        catch (std::exception const&)
        {
            return false;
        }
    }
}
