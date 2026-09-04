#include "AzerothVoicesProvider.h"

#include "AzerothVoicesPersonality.h"
#include "AzerothVoicesSentiment.h"

#include "httplib.h"
#include "json.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
#include <openssl/provider.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace AzerothVoices
{
    namespace
    {
        using Json = nlohmann::json;

        struct ParsedEndpoint
        {
            std::string base;
            std::string path;
            bool secure = false;
            bool local = false;
        };

        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        void ReplaceAll(std::string& value, std::string const& from, std::string const& to)
        {
            if (from.empty())
                return;
            size_t offset = 0;
            while ((offset = value.find(from, offset)) != std::string::npos)
            {
                value.replace(offset, from.size(), to);
                offset += to.size();
            }
        }

        std::string RedactSecrets(Config const& config, std::string value)
        {
            std::string const apiKey = config.ResolveApiKey();
            if (!apiKey.empty())
                ReplaceAll(value, apiKey, "[REDACTED]");
            return value;
        }

        std::string ScopeName(ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::Say: return "say";
                case ChatScope::Yell: return "yell";
                case ChatScope::Whisper: return "whisper";
                case ChatScope::Party: return "party";
                case ChatScope::Raid: return "raid";
                case ChatScope::Guild: return "guild";
                case ChatScope::Officer: return "officer";
                case ChatScope::Channel: return "channel";
                case ChatScope::World: return "world";
            }
            return "say";
        }

        std::string SpeakerKindName(SpeakerKind kind)
        {
            switch (kind)
            {
                case SpeakerKind::PlayerBot: return "playerbot";
                case SpeakerKind::Creature: return "NPC";
                case SpeakerKind::RealPlayer: return "player";
            }
            return "player";
        }

        std::unordered_map<std::string, std::string> Placeholders(
            ChatRequest const& request, bool resolveCreatureSpeaker = true,
            bool includeProviderOnly = true)
        {
            std::unordered_map<std::string, std::string> values;
            values["<bot name>"] = request.actor.name;
            values["<bot level>"] = std::to_string(request.actor.level);
            values["<bot race>"] = request.actor.race;
            values["<bot class>"] = request.actor.className;
            values["<bot gender>"] = request.actor.gender;
            values["<bot faction>"] = request.actor.faction;
            values["<bot zone>"] = request.actor.zone;
            values["<bot subzone>"] = request.actor.area;
            values["<bot map>"] = request.actor.map;
            values["<bot guild>"] = request.actor.guild;
            values["<bot specialization>"] = request.actor.talentBuild;
            values["<bot personality>"] = JoinPersonalityTraits(request.personality.traits);
            values["<bot background>"] = request.personality.background;
            values["<bot tone>"] = request.personality.tone;
            values["<bot personality block>"] = request.personalityBlock;
            values["<bot type>"] = request.actor.kind == ActorKind::Creature ? "NPC" : "playerbot";
            values["<expansion name>"] = "Turtle WoW";
            values["<sender name>"] = request.speaker.name;
            values["<receiver name>"] = request.actor.name;
            values["<other name>"] = request.speaker.name;
            values["<other level>"] = std::to_string(request.speaker.level);
            values["<other race>"] = request.speaker.race;
            values["<other class>"] = request.speaker.className;
            values["<other gender>"] = request.speaker.gender;
            values["<other faction>"] = request.speaker.faction;
            SpeakerKind const speakerKind = resolveCreatureSpeaker
                ? request.speaker.ResolvedKind()
                : (request.speaker.isBot ? SpeakerKind::PlayerBot : SpeakerKind::RealPlayer);
            values["<other type>"] = SpeakerKindName(speakerKind);
            values["<unit type>"] = values["<other type>"];
            values["<unit name>"] = request.speaker.name;
            values["<unit subname>"] = "";
            values["<unit level>"] = std::to_string(request.speaker.level);
            values["<unit gender>"] = request.speaker.gender;
            values["<unit race>"] = request.speaker.race;
            values["<unit faction>"] = request.speaker.faction;
            values["<unit class>"] = request.speaker.className;
            values["<initial message>"] = request.incomingMessage;
            values["<channel name>"] = request.channelName.empty()
                ? ScopeName(request.scope) : request.channelName;
            values["<trigger>"] = request.trigger;
            if (includeProviderOnly)
            {
                values["<context>"] = request.context;
                values["<prompt>"] = request.userPrompt;
                values["<pre prompt>"] = request.systemPrompt;
                values["<post prompt>"] = "";
            }
            return values;
        }

        void ApplyPlaceholders(std::string& value, ChatRequest const& request,
                               bool resolveCreatureSpeaker = true,
                               bool includeProviderOnly = true)
        {
            for (auto const& pair : Placeholders(
                     request, resolveCreatureSpeaker, includeProviderOnly))
                ReplaceAll(value, pair.first, pair.second);
        }

        void ApplyPlaceholders(Json& value, ChatRequest const& request,
                               bool resolveCreatureSpeaker = true,
                               bool includeProviderOnly = true)
        {
            if (value.is_string())
            {
                std::string text = value.get<std::string>();
                ApplyPlaceholders(text, request, resolveCreatureSpeaker, includeProviderOnly);
                value = text;
                return;
            }

            if (value.is_array() || value.is_object())
            {
                for (auto it = value.begin(); it != value.end(); ++it)
                    ApplyPlaceholders(*it, request, resolveCreatureSpeaker, includeProviderOnly);
            }
        }

        void ApplyTokenOverride(Json& body, Config const& config, ChatRequest const& request)
        {
            if (!request.maxTokensOverride || !body.is_object())
                return;

            bool applied = false;
            if (body.count("max_tokens"))
            {
                body["max_tokens"] = request.maxTokensOverride;
                applied = true;
            }
            if (body.count("max_output_tokens"))
            {
                body["max_output_tokens"] = request.maxTokensOverride;
                applied = true;
            }
            if (applied)
                return;

            if (Lower(config.providerMode) == "responses")
                body["max_output_tokens"] = request.maxTokensOverride;
            else
                body["max_tokens"] = request.maxTokensOverride;
        }

        bool ParseEndpoint(std::string const& endpoint, ParsedEndpoint& parsed, std::string& error)
        {
            static std::regex const expression(R"(^(https?)://([^/]+)(/.*)?$)", std::regex::icase);
            std::smatch match;
            if (!std::regex_match(endpoint, match, expression))
            {
                error = "endpoint must start with http:// or https:// and contain a host";
                return false;
            }

            std::string scheme = Lower(match[1].str());
            std::string host = match[2].str();
            parsed.base = scheme + "://" + host;
            parsed.path = match[3].matched ? match[3].str() : "/";
            parsed.secure = scheme == "https";

            std::string hostLower = Lower(host);
            if (hostLower.compare(0, 5, "[::1]") == 0)
                parsed.local = true;
            else
            {
                size_t colon = hostLower.find(':');
                if (colon != std::string::npos)
                    hostLower.erase(colon);
                parsed.local = hostLower == "localhost" || hostLower == "127.0.0.1" || hostLower == "::1";
            }
            return true;
        }

        std::string OpenSslError()
        {
            unsigned long code = ERR_get_error();
            if (!code)
                return "unknown OpenSSL error";
            char buffer[256] = {};
            ERR_error_string_n(code, buffer, sizeof(buffer));
            return buffer;
        }

        std::string ExtractContent(Json const& root)
        {
            if (root.is_object() && root.count("choices") && root["choices"].is_array() && !root["choices"].empty())
            {
                Json const& choice = root["choices"][0];
                if (choice.is_object() && choice.count("message") && choice["message"].is_object())
                {
                    Json const& message = choice["message"];
                    if (message.count("content"))
                    {
                        Json const& content = message["content"];
                        if (content.is_string())
                            return content.get<std::string>();
                        if (content.is_array())
                        {
                            std::string combined;
                            for (auto const& part : content)
                            {
                                if (part.is_object() && part.count("text") && part["text"].is_string())
                                {
                                    if (!combined.empty())
                                        combined += '\n';
                                    combined += part["text"].get<std::string>();
                                }
                            }
                            if (!combined.empty())
                                return combined;
                        }
                    }
                }
                if (choice.is_object() && choice.count("text") && choice["text"].is_string())
                    return choice["text"].get<std::string>();
            }

            if (root.is_object() && root.count("output_text") && root["output_text"].is_string())
                return root["output_text"].get<std::string>();
            if (root.is_object() && root.count("response") && root["response"].is_string())
                return root["response"].get<std::string>();
            if (root.is_object() && root.count("content") && root["content"].is_string())
                return root["content"].get<std::string>();

            if (root.is_object() && root.count("output") && root["output"].is_array())
            {
                for (auto const& output : root["output"])
                {
                    if (!output.is_object() || !output.count("content") || !output["content"].is_array())
                        continue;
                    for (auto const& part : output["content"])
                    {
                        if (part.is_object() && part.count("text") && part["text"].is_string())
                            return part["text"].get<std::string>();
                    }
                }
            }

            if (root.is_object() && root.count("candidates") && root["candidates"].is_array() && !root["candidates"].empty())
            {
                Json const& candidate = root["candidates"][0];
                if (candidate.is_object() && candidate.count("content") && candidate["content"].is_object())
                {
                    Json const& content = candidate["content"];
                    if (content.count("parts") && content["parts"].is_array() && !content["parts"].empty())
                    {
                        Json const& part = content["parts"][0];
                        if (part.is_object() && part.count("text") && part["text"].is_string())
                            return part["text"].get<std::string>();
                    }
                }
            }

            return "";
        }

        std::string LegacyParse(Config const& config, ChatRequest const& request, std::string const& raw, std::string& error)
        {
            try
            {
                std::string text = raw;
                std::string startPattern = config.responseStartPattern;
                std::string endPattern = config.responseEndPattern;
                std::string deletePattern = config.responseDeletePattern;
                ReplaceAll(startPattern, "<sender name>", request.speaker.name);
                ReplaceAll(endPattern, "<sender name>", request.speaker.name);
                ReplaceAll(deletePattern, "<sender name>", request.speaker.name);

                if (!startPattern.empty())
                {
                    std::smatch match;
                    if (!std::regex_search(text, match, std::regex(startPattern)))
                    {
                        error = "legacy response start pattern did not match";
                        return "";
                    }
                    text = text.substr(static_cast<size_t>(match.position() + match.length()));
                }

                if (!endPattern.empty())
                {
                    std::smatch match;
                    if (std::regex_search(text, match, std::regex(endPattern)))
                        text = text.substr(0, static_cast<size_t>(match.position()));
                }

                if (!deletePattern.empty())
                    text = std::regex_replace(text, std::regex(deletePattern), "");
                ReplaceAll(text, R"(\")", "'");
                return text;
            }
            catch (std::regex_error const& exception)
            {
                error = std::string("legacy response regex error: ") + exception.what();
                return "";
            }
        }

        std::string SanitizeReply(std::string value)
        {
            ReplaceAll(value, "```json", "");
            ReplaceAll(value, "```", "");
            ReplaceAll(value, "\r", "");
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return c < 0x20 && c != '\n' && c != '\t';
            }), value.end());
            value = Trim(value);
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                      (value.front() == '\'' && value.back() == '\'')))
                value = value.substr(1, value.size() - 2);
            return Trim(value);
        }

        struct ThreadClient
        {
            std::string base;
            std::unique_ptr<httplib::Client> client;
        };
    }

    bool Provider::InitializeTls(std::string& error)
    {
#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
        static OSSL_PROVIDER* const defaultProvider = OSSL_PROVIDER_load(nullptr, "default");
        if (!defaultProvider)
        {
            error = "failed to load OpenSSL default provider: " + OpenSslError();
            return false;
        }
        if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr) != 1)
        {
            error = "failed to initialize OpenSSL: " + OpenSslError();
            return false;
        }
#else
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
#endif
        return true;
    }

    std::string Provider::BuildBody(Config const& config, ChatRequest const& request, std::string& error)
    {
        try
        {
            ChatRequest prepared = request;
            if (prepared.kind == RequestKind::Dialogue)
            {
                bool const npcInteraction = prepared.actor.kind == ActorKind::Creature ||
                    prepared.speaker.ResolvedKind() == SpeakerKind::Creature;
                std::string desiredGlobalPrompt =
                    (npcInteraction || config.globalMode == GlobalMode::Roleplay)
                        ? config.globalPromptRoleplay : config.globalPromptNormal;
                std::string currentGlobalPrompt = config.globalPrompt;

                // Manager currently expands the configured global prompt before the
                // value-owned request reaches a worker. Recreate that exact legacy
                // expansion only to identify the prefix, then replace it with the
                // typed effective prompt. This keeps history/sentiment/personality
                // layers untouched while making NPC roleplay a C++-enforced rule.
                ApplyPlaceholders(currentGlobalPrompt, prepared, false, false);
                ApplyPlaceholders(desiredGlobalPrompt, prepared, true, false);
                if (!currentGlobalPrompt.empty() &&
                    prepared.systemPrompt.compare(0, currentGlobalPrompt.size(), currentGlobalPrompt) == 0)
                {
                    prepared.systemPrompt.replace(
                        0, currentGlobalPrompt.size(), desiredGlobalPrompt);
                }
            }

            std::string const sentimentDeltaInstruction =
                BuildSentimentDeltaInstruction(prepared.sentimentDeltaLimit);
            if (!sentimentDeltaInstruction.empty())
            {
                if (!prepared.systemPrompt.empty())
                    prepared.systemPrompt += "\n\n";
                prepared.systemPrompt += sentimentDeltaInstruction;
            }

            Json body;
            if (!config.apiJsonTemplate.empty())
            {
                body = Json::parse(config.apiJsonTemplate);
                ApplyPlaceholders(body, prepared);
                ApplyTokenOverride(body, config, prepared);
            }
            else if (Lower(config.providerMode) == "responses")
            {
                body["model"] = config.model;
                body["instructions"] = prepared.systemPrompt;
                body["input"] = prepared.context.empty() ? prepared.userPrompt : prepared.context + "\n\n" + prepared.userPrompt;
                body["max_output_tokens"] = prepared.maxTokensOverride
                    ? prepared.maxTokensOverride : config.maxTokens;
            }
            else
            {
                body["model"] = config.model;
                body["messages"] = Json::array();
                body["messages"].push_back({ { "role", "system" }, { "content", prepared.systemPrompt } });
                if (!prepared.context.empty())
                    body["messages"].push_back({ { "role", "system" }, { "content", prepared.context } });
                body["messages"].push_back({ { "role", "user" }, { "content", prepared.userPrompt } });
                body["max_tokens"] = prepared.maxTokensOverride
                    ? prepared.maxTokensOverride : config.maxTokens;
                body["temperature"] = config.temperature;
                body["top_p"] = config.topP;
            }

            if (!body.count("model") && !config.model.empty())
                body["model"] = config.model;
            return body.dump();
        }
        catch (std::exception const& exception)
        {
            error = std::string("request JSON error: ") + exception.what();
            return "";
        }
    }

    std::string Provider::ParseResponse(Config const& config, ChatRequest const& request,
                                        std::string const& raw, std::string& error)
    {
        if (Lower(config.parserMode) == "legacyregex")
            return SanitizeReply(LegacyParse(config, request, raw, error));

        try
        {
            Json root = Json::parse(raw);
            std::string content = ExtractContent(root);
            if (content.empty())
            {
                error = "provider JSON did not contain a supported text field";
                return "";
            }
            return SanitizeReply(content);
        }
        catch (std::exception const& exception)
        {
            error = std::string("response JSON error: ") + exception.what();
            return "";
        }
    }

    ChatCompletion Provider::Execute(Config const& config, ChatRequest const& request)
    {
        ChatCompletion completion;
        completion.request = request;
        auto const started = std::chrono::steady_clock::now();
        auto finish = [&completion, started]() {
            completion.elapsedMilliseconds = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());
            return completion;
        };

        ParsedEndpoint endpoint;
        if (!ParseEndpoint(config.endpoint, endpoint, completion.error))
            return finish();
        if (!endpoint.secure && !endpoint.local)
        {
            completion.error = "plain HTTP is allowed only for localhost endpoints";
            return finish();
        }
        if (!endpoint.secure && endpoint.local && !config.allowInsecureLocalHttp)
        {
            completion.error = "local HTTP endpoint is disabled by configuration";
            return finish();
        }
        std::string body = BuildBody(config, request, completion.error);
        if (body.empty())
            return finish();

        thread_local ThreadClient slot;
        if (!slot.client || slot.base != endpoint.base)
        {
            slot.base = endpoint.base;
            slot.client.reset(new httplib::Client(endpoint.base));
        }

        slot.client->set_connection_timeout(static_cast<time_t>(config.connectTimeoutSeconds), 0);
        slot.client->set_read_timeout(static_cast<time_t>(config.requestTimeoutSeconds), 0);
        slot.client->set_write_timeout(static_cast<time_t>(config.requestTimeoutSeconds), 0);
        slot.client->set_keep_alive(true);
        slot.client->enable_server_certificate_verification(true);
        if (!config.caCertFile.empty())
            slot.client->set_ca_cert_path(config.caCertFile.c_str());

        httplib::Headers headers;
        headers.emplace("Accept", "application/json");
        std::string apiKey = config.ResolveApiKey();
        if (!apiKey.empty())
            headers.emplace("Authorization", "Bearer " + apiKey);

        completion.httpAttemptCount = 1;
        auto result = slot.client->Post(endpoint.path, headers, body, "application/json");

        if (!result)
        {
            completion.error = std::string("HTTP transport error: ") + httplib::to_string(result.error());
            return finish();
        }

        completion.httpStatus = result->status;
        if (result->body.size() > config.maxResponseBytes)
        {
            completion.error = "provider response exceeded MaxResponseBytes";
            return finish();
        }
        completion.rawResponse = result->body;
        if (result->status < 200 || result->status >= 300)
        {
            std::string preview = result->body.substr(0, std::min<size_t>(result->body.size(), 512));
            completion.error = "provider returned HTTP " + std::to_string(result->status) + ": " +
                RedactSecrets(config, preview);
            return finish();
        }

        completion.responseText = ParseResponse(config, request, result->body, completion.error);
        completion.success = !completion.responseText.empty();
        return finish();
    }

    std::vector<std::string> Provider::SplitReply(Config const& config, std::string text)
    {
        std::vector<std::string> rawLines;
        if (Lower(config.parserMode) == "legacyregex" && !config.responseSplitPattern.empty())
        {
            try
            {
                std::regex pattern(config.responseSplitPattern);
                for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
                    rawLines.push_back(it->str());
            }
            catch (std::regex_error const&)
            {
                rawLines.clear();
            }
        }

        if (rawLines.empty())
        {
            std::stringstream stream(text);
            std::string line;
            while (std::getline(stream, line))
                rawLines.push_back(line);
        }

        std::vector<std::string> result;
        for (std::string line : rawLines)
        {
            line = SanitizeReply(line);
            while (!line.empty() && result.size() < config.maximumReplyLines)
            {
                size_t limit = std::min<size_t>(config.maximumReplyCharacters, 240);
                if (line.size() <= limit)
                {
                    result.push_back(line);
                    break;
                }

                size_t split = line.rfind(' ', limit);
                if (split == std::string::npos || split < limit / 2)
                    split = limit;
                result.push_back(Trim(line.substr(0, split)));
                line = Trim(line.substr(split));
            }
            if (result.size() >= config.maximumReplyLines)
                break;
        }
        return result;
    }
}
