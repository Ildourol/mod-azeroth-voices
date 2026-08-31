#include "AzerothVoicesPersonality.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
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

    std::string BuildPersonalityGenerationSystemPrompt(Config const& config)
    {
        std::ostringstream prompt;
        prompt << "Create one persistent fictional World of Warcraft PlayerBot identity. "
               << "Return exactly one JSON object and no markdown or explanation. The object must contain only "
               << "traits, tone, and background. traits must be an array of exactly "
               << config.personalityTraitCount
               << " distinct concise descriptors chosen from different personality dimensions such as temperament, "
                  "social style, outlook, courage, values, humor, demeanor, drive, loyalty, or discipline. "
                  "Traits must be mutually compatible and must not merely restate race or class. ";

        if (config.personalityGenerateTone)
            prompt << "tone must be a concise speaking-style description derived from all traits. ";
        else
            prompt << "tone must be an empty string. ";

        if (!config.personalityGenerateBackground)
            prompt << "background must be an empty string. ";
        else if (config.personalityBackgroundMode == 0)
            prompt << "background must be a short Vanilla/Turtle WoW-compatible backstory for an actual character "
                      "living in Azeroth. It may include upbringing, family, mentors, training, formative events, "
                      "motivations, fears, ambitions, and reasons for adventuring. Avoid later-expansion assumptions. ";
        else
            prompt << "background must be a short fictional real-world WoW-player persona. It may include life stage, "
                      "work or school, family, play schedule, guild or raid memories, PvE/PvP preferences, humor, and "
                      "casual or hardcore habits. Do not impersonate a real person and avoid gratuitous modern-platform references. ";

        prompt << "Keep background at or below " << config.personalityMaxBackgroundCharacters
               << " characters. JSON shape: {\"traits\":[\"...\"],\"tone\":\"...\",\"background\":\"...\"}.";
        return prompt.str();
    }

    std::string BuildPersonalityGenerationUserPrompt(Config const& config, ActorSnapshot const& actor)
    {
        std::ostringstream prompt;
        prompt << "Character name: " << actor.name
               << "\nRace: " << actor.race
               << "\nClass: " << actor.className
               << "\nFaction: " << actor.faction
               << "\nGender: " << actor.gender
               << "\nBackground mode: "
               << (config.personalityBackgroundMode == 0 ? "roleplay Azeroth character" : "fictional real-world WoW player");
        return prompt.str();
    }

    uint32_t PersonalityGenerationTokenBudget(Config const& config)
    {
        uint32_t estimate = config.personalityGenerateBackground
            ? config.personalityMaxBackgroundCharacters / 3 + 128
            : 128;
        return std::min<uint32_t>(1024, std::max<uint32_t>(128, estimate));
    }

    bool ParsePersonalityResponse(Config const& config, ActorSnapshot const& actor,
                                  std::string const& response, BotPersonality& personality,
                                  std::string& error)
    {
        try
        {
            Json root;
            std::string const trimmed = Trim(response);
            try
            {
                root = Json::parse(trimmed);
            }
            catch (std::exception const& directError)
            {
                if (!TryParseEmbeddedJsonObject(trimmed, root))
                {
                    error = std::string("personality response JSON error: ") + directError.what();
                    return false;
                }
            }
            if (!root.is_object())
            {
                error = "personality response is not a JSON object";
                return false;
            }
            static std::set<std::string> const allowed = { "traits", "tone", "background" };
            for (auto it = root.begin(); it != root.end(); ++it)
                if (!allowed.count(it.key()))
                {
                    error = "personality response contains unexpected field " + it.key();
                    return false;
                }
            if (root.size() != allowed.size() || !root.count("traits") ||
                !root.count("tone") || !root.count("background"))
            {
                error = "personality response must contain exactly traits, tone, and background";
                return false;
            }
            if (!root["traits"].is_array() || root["traits"].size() != config.personalityTraitCount)
            {
                error = "personality response has the wrong trait count";
                return false;
            }

            std::vector<std::string> traits;
            std::set<std::string> uniqueTraits;
            for (Json const& value : root["traits"])
            {
                if (!value.is_string())
                {
                    error = "personality trait is not a string";
                    return false;
                }
                std::string trait = CleanGeneratedText(value.get<std::string>());
                if (trait.empty() || trait.size() > 64 || !uniqueTraits.insert(Lower(trait)).second)
                {
                    error = "personality trait is empty, duplicated, or longer than 64 characters";
                    return false;
                }
                traits.push_back(std::move(trait));
            }

            std::string tone;
            if (root.count("tone"))
            {
                if (!root["tone"].is_string())
                {
                    error = "personality tone is not a string";
                    return false;
                }
                tone = CleanGeneratedText(root["tone"].get<std::string>());
            }
            if (config.personalityGenerateTone && (tone.empty() || tone.size() > 200))
            {
                error = "personality tone is empty or longer than 200 characters";
                return false;
            }
            if (!config.personalityGenerateTone)
                tone.clear();

            std::string background;
            if (root.count("background"))
            {
                if (!root["background"].is_string())
                {
                    error = "personality background is not a string";
                    return false;
                }
                background = CleanGeneratedText(root["background"].get<std::string>());
            }
            if (config.personalityGenerateBackground &&
                (background.empty() || background.size() > config.personalityMaxBackgroundCharacters))
            {
                error = "personality background is empty or exceeds MaxBackgroundChars";
                return false;
            }
            if (!config.personalityGenerateBackground)
                background.clear();

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

    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality)
    {
        if (!config.personalityEnabled || personality.traits.empty())
            return "";

        std::ostringstream block;
        if (config.personalityBackgroundMode == 0)
            block << "You are a distinct character in Azeroth's story. ";
        else
            block << "You portray a distinct fictional real-world World of Warcraft player behind this character; ordinary out-of-game life details from this persona are allowed when natural, but remain focused on WoW and never impersonate a real person. ";
        block << "Your personality traits are " << JoinPersonalityTraits(personality.traits)
              << ". Let them naturally shape vocabulary, opinions, humor, confidence, caution, emotions, and social behavior; "
                 "do not list or announce the traits in dialogue.";
        if (!personality.tone.empty())
            block << " Your speaking style is " << personality.tone << '.';
        if (!personality.background.empty() && personality.backgroundMode == config.personalityBackgroundMode)
            block << " Your persistent background: " << personality.background;
        if (config.personalityBackgroundMode == 0)
            block << " Stay in character and keep race, class, background, and current specialization consistent.";
        else
            block << " Speak consistently as this recurring player persona while remaining focused on World of Warcraft.";
        return HeadBounded(block.str(), config.personalityMaxPromptCharacters);
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
                if (trait.empty() || trait.size() > 64 ||
                    !uniqueTraits.insert(Lower(trait)).second)
                    return false;
                parsed.push_back(std::move(trait));
            }
            traits = std::move(parsed);
            return !traits.empty();
        }
        catch (std::exception const&)
        {
            return false;
        }
    }
}
