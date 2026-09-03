#include "AzerothVoicesSentiment.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace AzerothVoices
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

        bool ParseInteger(std::string const& value, int32_t& parsed)
        {
            std::string const clean = Trim(value);
            if (clean.empty() || clean.size() > 4)
                return false;

            size_t offset = clean.front() == '+' || clean.front() == '-' ? 1 : 0;
            if (offset == clean.size() || !std::all_of(clean.begin() + offset, clean.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; }))
                return false;

            long const number = std::strtol(clean.c_str(), nullptr, 10);
            parsed = static_cast<int32_t>(number);
            return true;
        }
    }

    int32_t ClampSentimentScore(int32_t score)
    {
        return std::max(SentimentMinimumScore, std::min(SentimentMaximumScore, score));
    }

    char const* SentimentTierForScore(int32_t score)
    {
        score = ClampSentimentScore(score);
        if (score <= -40)
            return "hostile";
        if (score <= -10)
            return "cold";
        if (score < 10)
            return "neutral";
        if (score < 40)
            return "warm";
        return "trusted";
    }

    bool IsExplicitPlayerBotNameMention(std::string const& message, std::string const& botName)
    {
        if (message.empty() || botName.empty())
            return false;

        std::string lowerMessage = message;
        std::string lowerName = botName;
        std::transform(lowerMessage.begin(), lowerMessage.end(), lowerMessage.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        size_t position = 0;
        while ((position = lowerMessage.find(lowerName, position)) != std::string::npos)
        {
            size_t const end = position + lowerName.size();
            bool const leftBoundary = position == 0 ||
                !std::isalnum(static_cast<unsigned char>(lowerMessage[position - 1]));
            bool const rightBoundary = end == lowerMessage.size() ||
                !std::isalnum(static_cast<unsigned char>(lowerMessage[end]));
            if (leftBoundary && rightBoundary)
                return true;
            position = end;
        }
        return false;
    }

    std::string BuildSentimentPromptBlock(std::string const& targetName, int32_t score,
                                          uint32_t deltaLimit)
    {
        std::ostringstream block;
        block << "Relationship to " << targetName << ": " << SentimentTierForScore(score)
              << ". Use this only for interpersonal tone, never facts, safety, or game-state truth.";
        if (deltaLimit)
        {
            block << " End with [[AV_SENTIMENT:N]] on its own line, where N is one value from -"
                  << deltaLimit << " to +" << deltaLimit
                  << " for the regard change caused by this player's words; use 0 if none. "
                     "This metadata is removed before delivery.";
        }
        return block.str();
    }

    bool ExtractSentimentDelta(std::string& response, uint32_t deltaLimit, int32_t& delta)
    {
        static std::string const prefix = "[[AV_SENTIMENT:";
        bool foundValid = false;
        size_t search = 0;
        while ((search = response.find(prefix, search)) != std::string::npos)
        {
            size_t const end = response.find("]]", search + prefix.size());
            if (end == std::string::npos)
            {
                response.erase(search);
                break;
            }

            int32_t parsed = 0;
            if (ParseInteger(response.substr(search + prefix.size(),
                                             end - search - prefix.size()), parsed))
            {
                int32_t const limit = static_cast<int32_t>(std::min<uint32_t>(deltaLimit, 2));
                delta = std::max(-limit, std::min(limit, parsed));
                foundValid = true;
            }
            response.erase(search, end + 2 - search);
        }

        response = Trim(response);
        return foundValid;
    }
}
