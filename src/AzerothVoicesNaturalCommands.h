#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace AzerothVoices
{
    struct NaturalCommandAction
    {
        char const* name;
        char const* meaning;
        uint8_t usefulness;
        bool forbidden;
    };

    enum class NaturalCommandRisk : uint8_t { Low, Caution, High, Forbidden };
    enum class NaturalCommandArgumentMode : uint8_t { None, Optional, Required };

    struct NaturalCommandMetadata
    {
        std::string name;
        std::string canonical;
        std::string category;
        std::string meaning;
        std::string argumentGrammar;
        std::string requiredContext;
        std::vector<std::string> aliases;
        std::vector<std::string> keywords;
        NaturalCommandArgumentMode argumentMode = NaturalCommandArgumentMode::Optional;
        NaturalCommandRisk risk = NaturalCommandRisk::Low;
        bool usesSelection = false;
        bool confirmationRequired = false;
        bool forbidden = false;
    };

    std::vector<NaturalCommandAction> const& GetNaturalCommandActions();
    NaturalCommandAction const* FindNaturalCommandAction(std::string const& name);
    std::string NormalizeNaturalCommandAction(std::string value);
    bool ExpandNaturalCommandPreset(std::string const& name,
        std::set<std::string>& allowed);
    std::set<std::string> ResolveNaturalCommandAllowlist(
        std::vector<std::string> const& configured, std::vector<std::string>& invalid,
        std::vector<std::string>& forbidden);
    std::string BuildNaturalCommandPromptCatalog(std::set<std::string> const& allowed);
    std::string BuildNaturalCommandClassifierPrompt(std::set<std::string> const& allowed,
        size_t maximumActions = 1, bool includeAcknowledgement = false);
    NaturalCommandMetadata GetNaturalCommandMetadata(NaturalCommandAction const& action);
    std::set<std::string> ShortlistNaturalCommandActions(
        std::set<std::string> const& allowed, std::string const& message,
        size_t maximumActions = 20,
        std::map<std::string, uint64_t> const* actionUsage = nullptr);
    std::string NaturalCommandRiskName(NaturalCommandRisk risk);
    bool ValidateNaturalCommandArguments(std::string const& action,
        std::string const& arguments, bool hasSelection,
        std::vector<std::string> const& preservedLinks, std::string& restored,
        std::string& error);
    std::string PreserveNaturalCommandLinks(std::string const& message,
        std::vector<std::string>& links);
}
