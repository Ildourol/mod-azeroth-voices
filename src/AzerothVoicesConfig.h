#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace AzerothVoices
{
    struct Config
    {
        bool enabled = true;
        bool debug = false;
        bool consoleGeneratedMessages = false;
        bool consoleApiCallStats = false;
        uint32_t consoleApiCallStatsIntervalSeconds = 60;

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
        std::vector<std::string> blockedChannels;

        bool naturalCommandsEnabled = true;
        bool naturalCommandsMasterOnly = true;
        bool naturalCommandsLocalFastPath = true;
        bool naturalCommandsLlmFallback = true;
        std::string naturalCommandsModel;
        float naturalCommandsMinimumConfidence = 0.90f;
        uint32_t naturalCommandsRequestTtlSeconds = 15;
        uint32_t naturalCommandsRetryMaximum = 0;
        uint32_t naturalCommandsMaximumPendingPerBot = 2;
        uint32_t naturalCommandsShortlistMaximum = 20;
        uint32_t naturalCommandsMaximumRecipients = 1;
        uint32_t naturalCommandsMaximumActions = 1;
        bool naturalCommandsConfirmationEnabled = true;
        uint32_t naturalCommandsConfirmationTtlSeconds = 20;
        bool naturalCommandsFeedbackEnabled = true;
        std::string naturalCommandsAcknowledgementMode = "local";
        bool naturalCommandsTelemetryEnabled = false;
        bool naturalCommandsPromoteFrequentlyUsedActions = true;
        bool naturalCommandsAuditEnabled = false;
        uint32_t naturalCommandsAuditMaximumRecords = 500;
        bool naturalCommandsAuditIncludeArguments = false;
        std::set<std::string> naturalCommandsAllowedActions;
        std::vector<std::string> naturalCommandsExcludedChannels;

        bool personalityEnabled = true;
        uint32_t personalityBackgroundMode = 0;
        bool personalityGenerateBackground = true;
        uint32_t personalityTraitCount = 3;
        bool personalityGenerateTone = true;
        bool personalityGenerateOnDemand = true;
        uint32_t personalityGenerationRetrySeconds = 300;
        uint32_t personalityMaxBackgroundCharacters = 500;
        uint32_t personalityMaxPromptCharacters = 700;

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
        float sayDistance = 25.0f;
        float yellDistance = 100.0f;
        float npcDistance = 10.0f;
        std::set<uint32_t> npcAllowedTypes = { 2, 3, 4, 5, 6, 7, 9 };
        std::set<uint32_t> npcAllowedEntries;
        std::set<uint32_t> npcExcludedEntries;
        bool npcAllowNeutral = false;
        bool npcAllowHostile = false;
        std::set<uint32_t> npcAllowedNeutralEntries;
        std::set<uint32_t> npcAllowedHostileEntries;
        uint32_t npcFriendlyReplyChance = 100;
        uint32_t npcNeutralReplyChance = 50;
        uint32_t npcHostileReplyChance = 25;
        uint32_t targetedNpcReplyChance = 100;
        uint32_t targetedNpcJoinChance = 5;
        uint32_t targetedNpcPlayerBotJoinChance = 10;
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

        uint32_t historyStorageMode = 2;
        uint32_t historyRamMaximumTurns = 6;
        uint32_t historyDatabaseMaximumTurns = 20;
        uint32_t historyTtlMinutes = 30;
        uint32_t historyDatabaseTtlMinutes = 10080;
        uint32_t historyMaximumCharacters = 2500;
        uint32_t historyMaximumConversations = 2048;
        uint32_t historyDatabaseFlushSeconds = 5;
        uint32_t historyDatabaseFlushBatchSize = 20;
        std::string historyHeaderTemplate;
        std::string historyLineTemplate;
        std::string historyFooterTemplate;

        bool surroundingChatEnabled = true;
        uint32_t surroundingChatMaximumLines = 8;
        uint32_t surroundingChatTtlMinutes = 5;
        uint32_t surroundingChatMaximumCharacters = 1200;
        uint32_t surroundingChatMaximumScopes = 512;

        bool snapshotEnabled = false;
        bool snapshotIncludeCombat = true;
        bool snapshotIncludeGroup = true;
        bool snapshotIncludeSpells = true;
        bool snapshotIncludeQuests = true;
        bool snapshotIncludeLineOfSight = true;
        bool snapshotIncludeNearbyPlayers = true;
        float snapshotDistance = 40.0f;
        uint32_t snapshotMaximumGroupMembers = 5;
        uint32_t snapshotMaximumSpells = 20;
        uint32_t snapshotMaximumQuests = 10;
        uint32_t snapshotMaximumCreatures = 10;
        uint32_t snapshotMaximumGameObjects = 8;
        uint32_t snapshotMaximumPlayers = 8;
        uint32_t snapshotMaximumCharacters = 3500;
        std::string snapshotPromptTemplate;
        uint32_t snapshotStorageMode = 0;
        uint32_t snapshotRamMaximumSnapshots = 3;
        uint32_t snapshotDatabaseMaximumSnapshots = 10;
        uint32_t snapshotHistoryTtlMinutes = 30;
        uint32_t snapshotDatabaseTtlMinutes = 10080;
        uint32_t snapshotHistoryMaximumCharacters = 1600;
        uint32_t snapshotHistoryMaximumActors = 2048;

        bool ragEnabled = false;
        std::string ragDirectory;
        uint32_t ragMaximumItems = 3;
        float ragSimilarityThreshold = 0.3f;
        uint32_t ragMaximumCharacters = 1200;
        bool ragReloadOnRestart = true;
        std::string ragPromptTemplate;

        static Config Load();
        std::string ResolveApiKey() const;
    };
}
