#include "AzerothVoicesReasoning.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>

namespace AzerothVoices
{
    namespace Reasoning
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

            std::string Lower(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            bool StartsWith(std::string const& value, std::string const& prefix)
            {
                return value.size() >= prefix.size() &&
                    value.compare(0, prefix.size(), prefix) == 0;
            }

            std::string ModelLeaf(std::string model)
            {
                model = Lower(Trim(model));
                size_t const slash = model.rfind('/');
                if (slash != std::string::npos)
                    model.erase(0, slash + 1);
                return model;
            }

            std::string LowestEffort(Provider provider)
            {
                (void)provider;
                return "low";
            }

            std::string NormalizeEffort(Provider provider, Capability capability,
                                        bool responsesMode, std::string const& configured)
            {
                std::string effort = Lower(Trim(configured));
                if (effort != "minimal" && effort != "low" && effort != "medium" &&
                    effort != "high")
                    effort = "low";

                // Responses accepts low/medium/high. o-series and Gemini's
                // compatibility surface document low/medium/high as well, so
                // every "minimal" collapses to the lowest documented level.
                if (effort == "minimal" &&
                    (responsesMode || provider == Provider::Gemini ||
                     capability == Capability::AlwaysOn))
                    effort = "low";
                return effort;
            }

            std::mutex& UnsupportedMutex()
            {
                static std::mutex mutex;
                return mutex;
            }

            std::set<std::string>& UnsupportedKeys()
            {
                static std::set<std::string> keys;
                return keys;
            }

            std::string UnsupportedKey(std::string const& endpoint, std::string const& model)
            {
                // Keyed by recognised provider rather than the exact URL, so
                // switching between the chat-completions and Responses paths for
                // the same model does not repeat the rejected request.
                return std::string(ProviderName(DetectProvider(endpoint))) + '|' + ModelLeaf(model);
            }
        }

        char const* PurposeName(Purpose purpose)
        {
            switch (purpose)
            {
                case Purpose::None: return "none";
                case Purpose::PersonalityGeneration: return "PersonalityGeneration";
                case Purpose::DirectChat: return "DirectChat";
                case Purpose::TargetedNpc: return "TargetedNpc";
                case Purpose::EventChat: return "EventChat";
                case Purpose::AmbientChat: return "AmbientChat";
                case Purpose::FollowupChat: return "FollowupChat";
                case Purpose::Count: break;
            }
            return "none";
        }

        char const* ModeName(Mode mode)
        {
            switch (mode)
            {
                case Mode::Auto: return "Auto";
                case Mode::On: return "On";
                case Mode::Off: return "Off";
                case Mode::Count: break;
            }
            return "Auto";
        }

        char const* ProviderName(Provider provider)
        {
            switch (provider)
            {
                case Provider::None: return "none";
                case Provider::OpenAi: return "openai";
                case Provider::Gemini: return "gemini";
                case Provider::Count: break;
            }
            return "none";
        }

        char const* CapabilityName(Capability capability)
        {
            switch (capability)
            {
                case Capability::Unsupported: return "unsupported";
                case Capability::Optional: return "optional";
                case Capability::AlwaysOn: return "always-on";
                case Capability::Unknown: return "unknown";
                case Capability::Count: break;
            }
            return "unknown";
        }

        Purpose ClassifyPurpose(RequestKind kind, std::string const& trigger)
        {
            if (kind == RequestKind::PersonalityGeneration)
                return Purpose::PersonalityGeneration;
            if (trigger == "direct-chat")
                return Purpose::DirectChat;
            if (trigger.compare(0, 13, "targeted-npc-") == 0)
                return Purpose::TargetedNpc;
            if (trigger.compare(0, 6, "event:") == 0)
                return Purpose::EventChat;
            if (trigger == "ambient")
                return Purpose::AmbientChat;
            if (trigger == "generated-followup")
                return Purpose::FollowupChat;
            // overheard-chat, GM tests, boss, proximity, group, guild and any
            // future trigger stay non-reasoning in Auto mode.
            return Purpose::None;
        }

        Mode ParseMode(std::string const& value)
        {
            std::string const normalized = Lower(Trim(value));
            if (normalized == "on")
                return Mode::On;
            if (normalized == "off")
                return Mode::Off;
            return Mode::Auto;
        }

        bool ParseAutoKinds(std::string const& value, std::set<Purpose>& kinds,
                            std::string& warnings)
        {
            kinds.clear();
            warnings.clear();
            std::istringstream stream(value);
            std::string token;
            while (std::getline(stream, token, ','))
            {
                token = Trim(token);
                if (token.empty())
                    continue;
                std::string const normalized = Lower(token);
                Purpose purpose = Purpose::None;
                if (normalized == "personalitygeneration")
                    purpose = Purpose::PersonalityGeneration;
                else if (normalized == "directchat")
                    purpose = Purpose::DirectChat;
                else if (normalized == "targetednpc")
                    purpose = Purpose::TargetedNpc;
                else if (normalized == "eventchat")
                    purpose = Purpose::EventChat;
                else if (normalized == "ambientchat")
                    purpose = Purpose::AmbientChat;
                else if (normalized == "followupchat")
                    purpose = Purpose::FollowupChat;
                else
                {
                    if (!warnings.empty())
                        warnings += ' ';
                    warnings += "unknown AutoKind '" + token + "' ignored";
                    continue;
                }
                kinds.insert(purpose);   // duplicates collapse here
            }
            return true;
        }

        std::string SerializeAutoKinds(std::set<Purpose> const& kinds)
        {
            std::string result;
            for (Purpose purpose : kinds)
            {
                if (!result.empty())
                    result += ',';
                result += PurposeName(purpose);
            }
            return result;
        }

        bool AutoKindEnabled(std::string const& serializedKinds, Purpose purpose)
        {
            if (purpose == Purpose::None)
                return false;
            std::string const wanted = Lower(PurposeName(purpose));
            std::istringstream stream(serializedKinds);
            std::string token;
            while (std::getline(stream, token, ','))
                if (Lower(Trim(token)) == wanted)
                    return true;
            return false;
        }

        Provider DetectProvider(std::string const& endpoint)
        {
            std::string const normalized = Lower(Trim(endpoint));
            if (normalized.compare(0, 8, "https://") != 0)
                return Provider::None;

            size_t const authorityBegin = 8;
            size_t const pathBegin = normalized.find('/', authorityBegin);
            std::string const host = normalized.substr(authorityBegin,
                pathBegin == std::string::npos ? std::string::npos : pathBegin - authorityBegin);
            std::string const path = pathBegin == std::string::npos
                ? std::string() : normalized.substr(pathBegin);

            if (host == "api.openai.com")
            {
                if (path == "/v1/chat/completions" || path == "/v1/responses")
                    return Provider::OpenAi;
                return Provider::None;
            }
            if (host == "generativelanguage.googleapis.com" &&
                path == "/v1beta/openai/chat/completions")
                return Provider::Gemini;
            return Provider::None;
        }

        Capability DetectCapability(Provider provider, std::string const& model)
        {
            std::string const leaf = ModelLeaf(model);
            if (leaf.empty())
                return Capability::Unknown;

            if (provider == Provider::OpenAi)
            {
                // Chat-only variants do not reason.
                if (leaf.find("-chat") != std::string::npos)
                    return Capability::Unsupported;
                if (StartsWith(leaf, "gpt-4.1"))
                    return Capability::Unsupported;
                // o-series reasoning cannot be disabled, only controlled.
                if (StartsWith(leaf, "o1") || StartsWith(leaf, "o3") || StartsWith(leaf, "o4"))
                    return Capability::AlwaysOn;
                // GPT-5 family accepts an explicit effort level.
                if (StartsWith(leaf, "gpt-5"))
                    return Capability::Optional;
                return Capability::Unknown;
            }

            if (provider == Provider::Gemini)
            {
                if (StartsWith(leaf, "gemini-3"))
                    return Capability::AlwaysOn;
                if (StartsWith(leaf, "gemini-2.5-pro"))
                    return Capability::AlwaysOn;
                if (StartsWith(leaf, "gemini-2.5-flash"))
                    return Capability::Optional;
                return Capability::Unknown;
            }
            return Capability::Unknown;
        }

        Decision Decide(Mode mode, bool autoDetect, Provider provider, Capability capability,
                        Purpose purpose, bool purposeEnabled, std::string const& configuredEffort,
                        bool responsesMode)
        {
            Decision decision;
            if (provider == Provider::None || provider == Provider::Count)
            {
                decision.reason = "unsupported-endpoint";
                return decision;
            }
            if (capability == Capability::Unknown)
            {
                decision.reason = "unknown-model";
                return decision;
            }
            if (capability == Capability::Unsupported)
            {
                decision.reason = "unsupported-model";
                return decision;
            }
            // With automatic detection off, only an explicit On request applies
            // a control; the module never guesses for an unrecognised model.
            if (!autoDetect && mode != Mode::On)
            {
                decision.reason = "autodetect-disabled";
                return decision;
            }

            bool const alwaysOn = capability == Capability::AlwaysOn;
            bool apply = false;
            if (mode == Mode::On)
                apply = true;
            else if (mode == Mode::Off)
                apply = alwaysOn;      // lowest documented control, reasoning is not disabled
            else
                apply = purposeEnabled;   // Auto: provider/model plus enabled AutoKind

            decision.alwaysOn = alwaysOn;
            if (!apply)
            {
                decision.reason = mode == Mode::Off ? "off" : "purpose-disabled";
                return decision;
            }

            decision.apply = true;
            decision.effort = mode == Mode::Off
                ? LowestEffort(provider)
                : NormalizeEffort(provider, capability, responsesMode, configuredEffort);
            decision.reason = mode == Mode::Off ? "always-on-lowest"
                : (mode == Mode::On ? "explicit-on" : "auto-kind");
            (void)purpose;
            return decision;
        }

        uint32_t ReservedOutputTokens(uint32_t baseTokens, uint32_t reserve, bool applied)
        {
            if (!applied)
                return baseTokens;
            uint64_t const total = static_cast<uint64_t>(baseTokens) + reserve;
            return static_cast<uint32_t>(std::min<uint64_t>(total, 200000));
        }

        bool ApplyToRequestJson(nlohmann::json& body, Provider provider, bool responsesMode,
                                std::string const& effort, std::string& error)
        {
            error.clear();
            if (effort.empty() || !body.is_object())
            {
                error = "no effort to apply";
                return false;
            }
            if (ProviderName(provider) == std::string("none"))
            {
                error = "unsupported provider";
                return false;
            }

            if (responsesMode && provider == Provider::OpenAi)
                body["reasoning"] = { { "effort", effort } };
            else
                body["reasoning_effort"] = effort;
            return true;
        }

        bool HasExplicitReasoningField(nlohmann::json const& body)
        {
            return body.is_object() &&
                (body.count("reasoning") || body.count("reasoning_effort"));
        }

        bool IsParameterReasoningRejection(int httpStatus, std::string const& responseBody)
        {
            if (httpStatus != 400 && httpStatus != 422)
                return false;
            std::string const body = Lower(responseBody);
            bool const namesControl = body.find("reasoning_effort") != std::string::npos ||
                body.find("\"reasoning\"") != std::string::npos ||
                body.find("reasoning effort") != std::string::npos ||
                body.find("thinking_budget") != std::string::npos ||
                body.find("thinking_level") != std::string::npos;
            bool const saysRejected = body.find("unsupported") != std::string::npos ||
                body.find("unknown") != std::string::npos ||
                body.find("invalid") != std::string::npos ||
                body.find("not supported") != std::string::npos ||
                body.find("unrecognized") != std::string::npos;
            return namesControl && saysRejected;
        }

        void MarkUnsupported(std::string const& endpoint, std::string const& model)
        {
            std::lock_guard<std::mutex> lock(UnsupportedMutex());
            UnsupportedKeys().insert(UnsupportedKey(endpoint, model));
        }

        bool IsMarkedUnsupported(std::string const& endpoint, std::string const& model)
        {
            std::lock_guard<std::mutex> lock(UnsupportedMutex());
            return UnsupportedKeys().count(UnsupportedKey(endpoint, model)) != 0;
        }

        void ResetUnsupportedMarks()
        {
            std::lock_guard<std::mutex> lock(UnsupportedMutex());
            UnsupportedKeys().clear();
        }
    }
}
