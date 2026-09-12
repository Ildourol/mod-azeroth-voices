#pragma once

#include "AzerothVoicesTypes.h"
#include "json.hpp"

#include <cstdint>
#include <set>
#include <string>

namespace AzerothVoices
{
    // V0.7 Thinking policy for the official OpenAI and official Gemini
    // OpenAI-compatible endpoints only. Everything here is pure so the policy,
    // detection and JSON shapes can be unit tested without a worldserver.
    namespace Reasoning
    {
        enum class Purpose : uint8_t
        {
            None,
            PersonalityGeneration,
            DirectChat,
            TargetedNpc,
            EventChat,
            AmbientChat,
            FollowupChat,
            Count
        };

        enum class Mode : uint8_t
        {
            Auto,
            On,
            Off,
            Count
        };

        enum class Provider : uint8_t
        {
            None,
            OpenAi,
            Gemini,
            Count
        };

        enum class Capability : uint8_t
        {
            Unsupported,
            Optional,
            AlwaysOn,
            Unknown,
            Count
        };

        char const* PurposeName(Purpose purpose);
        char const* ModeName(Mode mode);
        char const* ProviderName(Provider provider);
        char const* CapabilityName(Capability capability);

        // Purpose is classified from the request kind and trigger, never from
        // the delivery channel.
        Purpose ClassifyPurpose(RequestKind kind, std::string const& trigger);

        Mode ParseMode(std::string const& value);

        // Case-insensitive, whitespace-trimmed AutoKinds parsing with duplicate
        // suppression. Invalid entries are reported in `warnings` and skipped.
        bool ParseAutoKinds(std::string const& value, std::set<Purpose>& kinds,
                            std::string& warnings);
        std::string SerializeAutoKinds(std::set<Purpose> const& kinds);
        // Cheap check against a serialized AutoKinds list, used per request.
        bool AutoKindEnabled(std::string const& serializedKinds, Purpose purpose);

        // Only the official HTTPS endpoint shapes are recognised; every proxy or
        // other compatible provider stays a normal non-thinking provider.
        Provider DetectProvider(std::string const& endpoint);

        // Conservative, no-probe recognition from the model name.
        Capability DetectCapability(Provider provider, std::string const& model);

        struct Decision
        {
            bool apply = false;
            bool alwaysOn = false;
            std::string effort;
            std::string reason;
        };

        Decision Decide(Mode mode, bool autoDetect, Provider provider, Capability capability,
                        Purpose purpose, bool purposeEnabled, std::string const& configuredEffort,
                        bool responsesMode);

        // The reserve is added only when a reasoning control is actually applied.
        uint32_t ReservedOutputTokens(uint32_t baseTokens, uint32_t reserve, bool applied);

        // Writes the documented control for the provider, and nothing else:
        // Responses -> reasoning.effort; OpenAI chat completions -> reasoning_effort;
        // Gemini compatibility -> reasoning_effort (never thinking_level,
        // thinking_budget, native SDK fields, or thought summaries).
        bool ApplyToRequestJson(nlohmann::json& body, Provider provider, bool responsesMode,
                                std::string const& effort, std::string& error);

        // True when a request body already carries an explicit reasoning field,
        // which custom AiPlayerbot.LLMApiJson templates must keep untouched.
        bool HasExplicitReasoningField(nlohmann::json const& body);

        // Parameter-specific rejection from an official provider: the request was
        // understood but the reasoning control itself was refused.
        bool IsParameterReasoningRejection(int httpStatus, std::string const& responseBody);

        // Process-wide capability mark set after such a rejection, so later
        // requests skip the control without another failed round trip.
        void MarkUnsupported(std::string const& endpoint, std::string const& model);
        bool IsMarkedUnsupported(std::string const& endpoint, std::string const& model);
        void ResetUnsupportedMarks();
    }
}
