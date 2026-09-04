#pragma once

#include <cstdint>
#include <string>

namespace AzerothVoices
{
    constexpr int32_t SentimentMinimumScore = -100;
    constexpr int32_t SentimentMaximumScore = 100;

    int32_t ClampSentimentScore(int32_t score);
    char const* SentimentTierForScore(int32_t score);
    bool IsExplicitPlayerBotNameMention(std::string const& message, std::string const& botName);
    std::string BuildSentimentToneHint(std::string const& targetName, int32_t score);
    std::string BuildSentimentDeltaInstruction(uint32_t deltaLimit);
    std::string BuildSentimentPromptBlock(std::string const& targetName, int32_t score,
                                          uint32_t deltaLimit);
    bool ExtractSentimentDelta(std::string& response, uint32_t deltaLimit, int32_t& delta);
}
