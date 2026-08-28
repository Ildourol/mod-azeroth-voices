#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace AzerothVoices
{
    struct Config
    {
        bool enabled = true;
        bool debug = false;
        bool tracePrompts = false;

        std::string providerMode;
        std::string endpoint;
        std::string apiKey;
        std::string model;
        std::string apiJsonTemplate;
        std::string caCertFile;
        bool allowInsecureLocalHttp = true;
        uint32_t connectTimeoutSeconds = 10;
        uint32_t requestTimeoutSeconds = 60;
        uint32_t maxResponseBytes = 65536;
        uint32_t maxTokens = 80;
        float temperature = 0.8f;
        float topP = 0.95f;

        uint32_t workerThreads = 8;
        uint32_t queueMaximum = 128;
        uint32_t highPriorityReserve = 32;
        uint32_t requestTtlSeconds = 90;
        uint32_t actorCooldownSeconds = 10;
        uint32_t ambientActorCooldownSeconds = 120;
        uint32_t retryMaximum = 1;
        uint32_t retryBackoffMilliseconds = 500;
        uint32_t globalRequestsPerMinute = 60;
        uint32_t speakerCooldownSeconds = 3;

        std::string globalPrompt;
        std::string prePrompt;
        std::string prompt;
        std::string postPrompt;
        std::string rpgPrompt;
        uint32_t contextLength = 4096;
        bool globalContext = false;
        std::string parserMode;
        std::string responseStartPattern;
        std::string responseEndPattern;
        std::string responseDeletePattern;
        std::string responseSplitPattern;
        std::string legacyCharacterCardFile;
        std::vector<std::string> blockedChannels;

        bool whisperReplies = true;
        bool sayReplies = true;
        bool yellReplies = true;
        bool partyReplies = true;
        bool raidReplies = true;
        bool guildReplies = true;
        bool officerReplies = false;
        bool worldReplies = true;
        bool customChannelReplies = false;
        bool npcReplies = true;
        bool disableRepliesInCombat = true;
        uint32_t maxResponders = 2;
        float sayDistance = 30.0f;
        float yellDistance = 100.0f;
        float npcDistance = 25.0f;
        uint32_t directAddressChance = 100;
        uint32_t nameMentionChance = 70;
        uint32_t overhearChance = 8;
        uint32_t playerReplyChanceSay = 90;
        uint32_t botReplyChanceSay = 10;
        uint32_t playerReplyChanceChannel = 60;
        uint32_t botReplyChanceChannel = 3;
        uint32_t playerReplyChanceParty = 90;
        uint32_t botReplyChanceParty = 25;
        uint32_t playerReplyChanceGuild = 70;
        uint32_t botReplyChanceGuild = 5;
        uint32_t botToBotChatChance = 10;
        uint32_t rpgAiChatChance = 30;
        std::string worldChannelName = "World";
        std::vector<std::string> commandBlacklist;

        bool randomChatterEnabled = true;
        uint32_t randomMinimumIntervalSeconds = 90;
        uint32_t randomMaximumIntervalSeconds = 240;
        float randomRealPlayerDistance = 150.0f;
        uint32_t randomFollowupChance = 15;
        uint32_t randomMaximumActors = 2;
        std::vector<std::string> randomScopes;
        std::vector<std::string> randomPrompts;
        std::vector<std::string> randomQuestions;
        std::vector<std::string> environmentPrompts;
        std::vector<std::string> guildPrompts;
        std::vector<std::string> worldPrompts;

        bool environmentContextEnabled = true;
        float environmentContextDistance = 25.0f;
        uint32_t environmentMaximumCreatures = 5;
        uint32_t environmentMaximumItems = 8;
        bool environmentIncludeEquipment = true;
        bool environmentIncludeBackpack = false;

        bool eventChatterEnabled = true;
        float eventRealPlayerDistance = 40.0f;
        uint32_t eventResponderChance = 25;
        uint32_t eventSelfCommentChance = 5;
        uint32_t eventMaximumResponders = 2;
        uint32_t eventCooldownSeconds = 60;
        std::map<std::string, uint32_t> eventChances;

        bool typingSimulationEnabled = true;
        uint32_t typingBaseDelayMilliseconds = 0;
        uint32_t typingDelayPerCharacterMilliseconds = 200;
        bool subtractGenerationTime = true;
        uint32_t maximumReplyCharacters = 220;
        uint32_t maximumReplyLines = 3;

        bool historyEnabled = true;
        uint32_t historyMaximumTurns = 6;
        uint32_t historyTtlMinutes = 30;
        uint32_t historyMaximumCharacters = 2500;
        std::string historyHeaderTemplate;
        std::string historyLineTemplate;
        std::string historyFooterTemplate;

        bool knowledgeEnabled = false;
        std::string knowledgeFile;
        uint32_t knowledgeMaximumItems = 3;
        uint32_t knowledgeMinimumScore = 1;
        std::string knowledgePromptTemplate;

        static Config Load();
        std::string ResolveApiKey() const;
    };
}
