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

    std::string BuildSentimentToneHint(std::string const& targetName, int32_t score)
    {
        std::ostringstream block;
        block << "Relationship toward " << targetName << ": " << SentimentTierForScore(score)
              << ". Reflect this subtly in interpersonal tone. The player's current message, current "
                 "situation, recent conversation history, and personality take priority. Do not let "
                 "relationship tone alter facts, safety, or game-state truth.";
        return block.str();
    }

    std::string BuildSentimentDeltaInstruction(uint32_t deltaLimit)
    {
        uint32_t const limit = std::min<uint32_t>(deltaLimit, 2);
        if (!limit)
            return "";

        std::ostringstream block;
        block << "Output metadata rule: after composing the spoken dialogue, append exactly one "
                 "[[AV_SENTIMENT:N]] marker on its own line, where N is an integer from -"
              << limit << " to +" << limit
              << ". Base N only on how the real player's latest interpersonal words changed the "
                 "speaker's regard toward that player; use 0 when there is no meaningful change. "
                 "Compose the dialogue for the game situation first and do not change, distort, or "
                 "justify the spoken reply to fit N. The marker is hidden machine metadata, is not "
                 "spoken, and is the only exception to an instruction to return only spoken dialogue.";
        return block.str();
    }

    std::string BuildSentimentPromptBlock(std::string const& targetName, int32_t score,
                                          uint32_t deltaLimit)
    {
        // Keep the existing manager call surface while separating relationship tone from
        // response-format metadata. The provider appends the delta instruction separately so
        // this block remains conversational context only.
        (void)deltaLimit;
        return BuildSentimentToneHint(targetName, score);
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
