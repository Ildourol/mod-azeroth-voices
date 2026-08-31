#pragma once

#include "AzerothVoicesConfig.h"
#include "AzerothVoicesTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    constexpr uint32_t PersonalityGenerationVersion = 1;

    std::string BuildPersonalityGenerationSystemPrompt(Config const& config);
    std::string BuildPersonalityGenerationUserPrompt(Config const& config, ActorSnapshot const& actor);
    uint32_t PersonalityGenerationTokenBudget(Config const& config);
    bool ParsePersonalityResponse(Config const& config, ActorSnapshot const& actor,
                                  std::string const& response, BotPersonality& personality,
                                  std::string& error);
    std::string BuildPersonalityPromptBlock(Config const& config, BotPersonality const& personality);
    std::string JoinPersonalityTraits(std::vector<std::string> const& traits);
    std::string SerializePersonalityTraits(std::vector<std::string> const& traits);
    bool ParseStoredPersonalityTraits(std::string const& value, std::vector<std::string>& traits);
}
