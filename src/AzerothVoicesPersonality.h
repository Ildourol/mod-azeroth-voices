#pragma once

#include "AzerothVoicesConfig.h"
#include "AzerothVoicesTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    constexpr uint32_t PersonalityGenerationVersion = 1;
    // Traits are compile-time fixed at exactly three. The former
    // AzerothVoices.Personality.TraitCount option is gone; rows with any other
    // trait count are treated as stale and regenerated lazily.
    constexpr uint32_t PersonalityTraitCount = 3;
    constexpr size_t PersonalityTraitMaximumCharacters = 64;

    std::string CleanHeadBounded(std::string value, size_t maximum);
    std::string BuildPersonalityGenerationSystemPrompt(Config const& config,
                                                       PersonalityGenerationMode mode);
    std::string BuildPersonalityGenerationUserPrompt(Config const& config, ActorSnapshot const& actor,
                                                     PersonalityGenerationMode mode,
                                                     BotPersonality const& fixed);
    uint32_t PersonalityGenerationTokenBudget(Config const& config, PersonalityGenerationMode mode);
    bool ParsePersonalityResponse(Config const& config, ActorSnapshot const& actor,
                                  PersonalityGenerationMode mode, BotPersonality const& fixed,
                                  std::string const& response, BotPersonality& personality,
                                  std::string& error);
    std::string BuildVoicePromptBlock(Config const& config, BotPersonality const& personality);
    std::string BuildBackstoryPromptBlock(Config const& config, BotPersonality const& personality);
    bool IsBackstoryInquiry(std::string const& message);
    bool IsBriefCasualMessage(std::string const& message);
    bool ShouldIncludeBackstory(Config const& config, BotPersonality const& personality,
                                ChatScope scope, std::string const& trigger,
                                std::string const& message, bool directConversation, bool ambient);
    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality,
                                            ChatScope scope, std::string const& trigger,
                                            std::string const& message, bool directConversation, bool ambient);
    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality);
    std::string JoinPersonalityTraits(std::vector<std::string> const& traits);
    std::string SerializePersonalityTraits(std::vector<std::string> const& traits);
    bool ParseStoredPersonalityTraits(std::string const& value, std::vector<std::string>& traits);
    bool ValidatePersonalityTraits(std::vector<std::string> const& traits, std::string& error);
}
