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
        }
        prompt << "Return exactly one JSON object and no markdown or explanation.";

        if (mode == PersonalityGenerationMode::BackgroundOnly)
        {
            prompt << " The object must contain only background. ";
        }
        else if (mode == PersonalityGenerationMode::ReplaceTraits)
        {
            prompt << " The object must contain only tone and background. Do not return or rewrite traits. ";
        }
        else
        {
            prompt << " The object must contain only traits, tone, and background. traits must be an array of exactly "
                   << PersonalityTraitCount
                   << " distinct concise descriptors chosen freely so their combination feels specific, varied, and "
                      "internally coherent. Do not use a fixed trait checklist, and do not merely restate race or "
                      "class. ";
        }

        if (mode != PersonalityGenerationMode::BackgroundOnly)
        {
            if (config.personalityGenerateTone)
                prompt << "tone must be a concise speaking-style description derived from the traits. ";
            else
                prompt << "tone must be an empty string. ";
        }

        if (!config.personalityGenerateBackground)
            prompt << "background must be an empty string. ";
        else if (config.personalityBackgroundMode == 0)
            prompt << "background must be a short, freely imagined backstory for an actual character living in "
                      "Vanilla/Turtle WoW-era Azeroth. Invent it without a fixed biography template or supplied story "
                      "ingredients. Keep it plausible for the supplied race, class, faction, and gender, avoid "
                      "later-expansion assumptions, and favor a distinctive believable premise over a generic WoW "
                      "archetype. ";
        else
            prompt << "background must be a short, freely imagined fictional real-world backstory for a person who "
                      "plays World of Warcraft. Invent the player's life and relationship with WoW without a fixed "
                      "biography template or supplied life ingredients. Choose one specific adult real-life age at "
                      "random from 18 through 60 and state that age naturally in the background story. Treat the "
                      "supplied race, class, faction, and gender as properties of the in-game avatar only and do not "
                      "infer the real person's identity from them. Do not impersonate a real person, favor a distinctive "
                      "believable persona over a generic gamer stereotype, and avoid gratuitous modern-platform "
                      "references. ";

        prompt << "Vary premise, structure, and emphasis naturally instead of forcing every identity into the same "
                  "biographical sequence. Do not mention randomness, generation, prompts, or these instructions. "
               << "Keep background at or below " << config.personalityMaxBackgroundCharacters << " characters. ";
        switch (mode)
        {
            case PersonalityGenerationMode::Full:
                prompt << "JSON shape: {\"traits\":[\"...\",\"...\",\"...\"],\"tone\":\"...\",\"background\":\"...\"}.";
                break;
            case PersonalityGenerationMode::ReplaceTraits:
                prompt << "JSON shape: {\"tone\":\"...\",\"background\":\"...\"}.";
                break;
            case PersonalityGenerationMode::BackgroundOnly:
                prompt << "JSON shape: {\"background\":\"...\"}.";
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
               << (config.personalityBackgroundMode == 0 ? "roleplay Azeroth character" : "fictional real-world WoW player");
        if (mode != PersonalityGenerationMode::Full && !fixed.traits.empty())
        {
            prompt << "\nFixed personality traits: " << JoinPersonalityTraits(fixed.traits);
            if (mode == PersonalityGenerationMode::BackgroundOnly && !fixed.tone.empty())
                prompt << "\nFixed speaking tone: " << fixed.tone;
            if (mode == PersonalityGenerationMode::BackgroundOnly && !fixed.background.empty())
                prompt << "\nDo not reuse this previous background story: " << fixed.background;
        }
        return prompt.str();
    }

    uint32_t PersonalityGenerationTokenBudget(Config const& config, PersonalityGenerationMode mode)
    {
        bool const writesBackground = config.personalityGenerateBackground ||
            mode == PersonalityGenerationMode::BackgroundOnly;
        uint32_t estimate = writesBackground
            ? config.personalityMaxBackgroundCharacters / 3 + 128 : 128;
        return std::min<uint32_t>(1024, std::max<uint32_t>(128, estimate));
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

            if (mode == PersonalityGenerationMode::Full)
            {
                if (root.size() != allowed.size() || !root.count("traits") ||
                    !root.count("tone") || !root.count("background"))
                {
                    error = "personality response must contain exactly traits, tone, and background";
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
                (tone.empty() || tone.size() > 200))
            {
                error = "personality tone is empty or longer than 200 characters";
                return false;
            }
            if (mode != PersonalityGenerationMode::BackgroundOnly && !config.personalityGenerateTone)
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
            bool const expectBackground = config.personalityGenerateBackground ||
                mode == PersonalityGenerationMode::BackgroundOnly;
            if (expectBackground &&
                (background.empty() || background.size() > config.personalityMaxBackgroundCharacters))
            {
                error = "personality background is empty or exceeds MaxBackgroundChars";
                return false;
            }
            if (!expectBackground)
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
