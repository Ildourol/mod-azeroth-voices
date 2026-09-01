#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices::NaturalCommands
{
    struct ActionDefinition
    {
        char const* input;
        char const* dispatch;
        uint8_t presetTier;
    };

    struct ParsedAction
    {
        std::string action;
        std::string arguments;
    };

    struct ParsedReply
    {
        std::string say;
        std::vector<ParsedAction> actions;
        bool attemptedEnvelope = false;
        bool parseMiss = false;
    };

    struct FastPathAction
    {
        std::string action;
        std::string arguments;
        std::string acknowledgement;
    };

    ActionDefinition const* FindAction(std::string const& input);
    std::vector<std::string> ResolveAllowedActions(std::string const& configured,
                                                    std::vector<std::string>& invalid);
    std::vector<std::string> BuildShortlist(std::vector<std::string> const& allowed,
                                            std::string const& message,
                                            bool dynamic,
                                            size_t maximum);
    std::string BuildSystemAddendum(std::vector<std::string> const& shortlist,
                                    uint32_t maximumActions,
                                    std::vector<std::string> const& nearbyEnemies);
    ParsedReply ParseReply(std::string const& response, uint32_t maximumActions);
    bool MatchFastPath(std::string const& commandText,
                       std::vector<std::string> const& allowed,
                       FastPathAction& result);
    std::string Normalize(std::string const& value);
    std::vector<std::string> AllActionInputs();
}
