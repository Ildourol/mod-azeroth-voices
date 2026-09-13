#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace AzerothVoices
{
    enum class GlobalMode : uint8_t
    {
        Roleplay,
        Normal
    };

    struct ResponderLimit
    {
        uint32_t maximum = 2;
        bool falloffEnabled = true;
        uint32_t secondChance = 50;
        uint32_t chanceDelta = 20;

        ResponderLimit& operator=(uint32_t value)
        {
            maximum = value;
            return *this;
        }

        operator uint32_t() const
        {
            return maximum;
        }
    };

    struct TypingDelayRange
    {
        uint32_t minimum = 100;
        uint32_t maximum = 200;

        TypingDelayRange& operator=(uint32_t legacyDelayMilliseconds);
        operator uint32_t() const;
    };

    struct Config
    {
        bool enabled = true;
        bool debug = false;
        bool playerbotsLlmEnabled = false;
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
        uint32_t maxTokens = 512;
        std::string thinkingMode = "auto";
        bool thinkingAutoDetect = true;
        std::string thinkingEffort = "low";
        uint32_t thinkingTokenReserve = 1024;
        std::string thinkingAutoKinds = "PersonalityGeneration,EventChat";
        std::string reasoningEffort = "auto";
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

        GlobalMode globalMode = GlobalMode::Normal;
        std::string globalPromptRoleplay;
        std::string globalPromptNormal;
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

        bool personalityEnabled = true;
        uint32_t personalityBackgroundMode = 1;
        bool personalityGenerateBackground = true;
        bool personalityGenerateTone = true;
        bool personalityGenerateOnDemand = true;
        bool personalityUseInRandom = true;
        bool personalityUseInEvents = true;
        uint32_t personalityGenerationRetrySeconds = 300;
        uint32_t personalityMaxBackgroundCharacters = 500;
        uint32_t personalityMaxPromptCharacters = 700;

        bool sentimentEnabled = false;
        bool sentimentUseInRandom = false;
        bool sentimentUseInEvents = false;
        uint32_t sentimentConversationMaximumDelta = 2;
        uint32_t sentimentInactivityGraceDays = 7;
        uint32_t sentimentPositiveDecayPerDay = 1;
        uint32_t sentimentNegativeDecayPerDay = 2;
        uint32_t sentimentCacheMaximumEntries = 4096;
        uint32_t sentimentPendingWriteMaximum = 1024;
        uint32_t sentimentDatabaseFlushSeconds = 5;
        uint32_t sentimentDatabaseFlushBatchSize = 50;

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
        bool disableRepliesInCombat = false;
        ResponderLimit maxResponders;
        float sayDistance = 25.0f;
        float yellDistance = 100.0f;
        float npcDistance = 10.0f;
        std::set<uint32_t> npcAllowedTypes = { 2, 3, 4, 5, 6, 7, 9 };
        bool npcAllowNeutralAndHostile = true;
        uint32_t npcFriendlyReplyChance = 100;
        uint32_t npcNeutralReplyChance = 50;
        uint32_t npcHostileReplyChance = 25;
        bool npcCombatStartEnabled = true;
        uint32_t npcCombatStartChance = 30;
        uint32_t npcCombatStartCooldownSeconds = 60;
        bool targetedNpcBotCommentsEnabled = true;
        uint32_t targetedNpcBotCommentChance = 15;
        uint32_t targetedNpcMaxBotComments = 1;
        bool exclusiveNameMentionResponder = true;
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
        std::vector<std::string> commandIgnoreList;

        bool proximityEnabled = true;
        uint32_t proximityOutdoorScanSeconds = 30;
        uint32_t proximityInstanceScanSeconds = 30;
        uint32_t proximityOutdoorChance = 15;
        uint32_t proximityInstanceChance = 100;
        uint32_t proximityConversationChance = 40;
        uint32_t proximityMinimumSpeakers = 1;
        uint32_t proximityMaximumSpeakers = 4;
        uint32_t proximityMaximumLines = 4;
        uint32_t proximityTurnGapSeconds = 6;
        uint32_t proximityReplyWindowSeconds = 30;
        uint32_t proximityMaximumReplyTurns = 5;
        uint32_t proximityEntityCooldownSeconds = 60;
        uint32_t proximityZoneFatigueScenes = 3;
        uint32_t proximityZoneFatigueDecayPercent = 20;
        bool proximityIncludeInstances = true;
        std::set<uint32_t> proximitySpeakerAllowEntries;
        std::set<uint32_t> proximitySpeakerDenyEntries;
        std::set<uint32_t> proximitySpeakerNonHumanoidAllowEntries;

        bool bossDialogueEnabled = true;
        uint32_t bossDialogueScanSeconds = 2;
        float bossDialogueMaximumDistance = 80.0f;
        float bossDialogueAggroMargin = 5.0f;
        uint32_t bossDialogueInitialDelayMinimumSeconds = 2;
        uint32_t bossDialogueInitialDelayMaximumSeconds = 6;
        uint32_t bossDialogueRepeatDelayMinimumSeconds = 20;
        uint32_t bossDialogueRepeatDelayMaximumSeconds = 60;
        uint32_t bossDialogueRepeatChance = 80;
        uint32_t bossDialogueRepeatChanceFloor = 10;
        uint32_t bossDialogueRepeatChanceDecayPercent = 50;
        uint32_t bossDialoguePresenceResetSeconds = 90;
        uint32_t bossDialogueDirectedReplyCooldownSeconds = 15;
        uint32_t bossDialogueMinimumWords = 5;
        uint32_t bossDialogueMaximumWords = 22;
        uint32_t bossDialogueMaximumCharacters = 180;
        std::set<uint32_t> bossDialogueAllowEntries;
        std::set<uint32_t> bossDialogueDenyEntries;

        bool groupChatterEnabled = true;
        bool raidChatterEnabled = true;
        uint32_t groupChatterScanSeconds = 60;
        uint32_t groupConversationChance = 25;
        uint32_t groupConversationMaximumLines = 3;
        uint32_t groupConversationMaximumParticipants = 2;
        uint32_t groupConversationMaximumReplyTurns = 3;
        uint32_t groupConversationTurnGapSeconds = 6;
        uint32_t groupConversationReplyWindowSeconds = 30;
        uint32_t groupDungeonEntryChance = 60;
        uint32_t groupDungeonEntryCooldownSeconds = 300;
        uint32_t groupBossPullChance = 50;
        uint32_t groupBossPullCooldownSeconds = 300;
        uint32_t groupBossKillChance = 80;
        uint32_t groupBossKillCooldownSeconds = 300;
        uint32_t groupWipeChance = 80;
        uint32_t groupWipeCooldownSeconds = 300;
        uint32_t groupCorpseRunChance = 40;
        uint32_t groupCorpseRunCooldownSeconds = 300;
        uint32_t groupResurrectChance = 50;
        uint32_t groupResurrectCooldownSeconds = 120;
        uint32_t groupDeathChance = 25;
        uint32_t groupDeathCooldownSeconds = 60;
        uint32_t groupKillChance = 8;
        uint32_t groupKillCooldownSeconds = 180;
        uint32_t groupLootUncommonChance = 10;
        uint32_t groupLootRareChance = 35;
        uint32_t groupLootEpicChance = 70;
        uint32_t groupLootLegendaryChance = 100;
        uint32_t groupLootCooldownSeconds = 120;
        uint32_t groupQuestAcceptChance = 25;
        uint32_t groupQuestObjectiveChance = 20;
        uint32_t groupQuestObjectiveDebounceSeconds = 60;
        uint32_t groupQuestCompleteChance = 25;
        uint32_t groupQuestCooldownSeconds = 60;
        uint32_t groupSpellCastChance = 8;
        uint32_t groupSpellCastCooldownSeconds = 60;
        uint32_t groupLowHealthChance = 40;
        uint32_t groupLowHealthThresholdPercent = 35;
        uint32_t groupLowHealthCooldownSeconds = 180;
        uint32_t groupLowManaChance = 30;
        uint32_t groupLowManaThresholdPercent = 20;
        uint32_t groupLowManaCooldownSeconds = 180;
        uint32_t groupNearbyObjectChance = 10;
        uint32_t groupNearbyObjectScanSeconds = 60;
        uint32_t groupNearbyObjectCooldownSeconds = 300;
        uint32_t groupBotQuestionChance = 1;
        uint32_t groupBotQuestionScanSeconds = 60;
        uint32_t groupBotQuestionCooldownSeconds = 300;
        uint32_t groupPlayerFollowupChance = 15;
        uint32_t groupPlayerFollowupCooldownSeconds = 30;
        uint32_t groupZoneChangeChance = 15;
        uint32_t groupZoneChangeCooldownSeconds = 180;
        uint32_t groupIdleChance = 5;
        uint32_t groupIdleCooldownSeconds = 120;
        uint32_t raidBattleCryChance = 40;
        uint32_t raidBattleCryCooldownSeconds = 300;
        uint32_t raidMoraleChance = 15;
        uint32_t raidMoraleScanSeconds = 120;
        uint32_t raidMoraleCooldownSeconds = 300;
        uint32_t raidIdleChance = 10;
        uint32_t raidIdleScanSeconds = 60;
        uint32_t raidIdleCooldownSeconds = 180;

        // Party delivery gate (PartyGate)
        bool partyGateEnabled = true;
        uint32_t partyGateFillerMinGapSeconds = 12;
        uint32_t partyGateContextualMinGapSeconds = 10;
        uint32_t partyGateResponsiveMinGapSeconds = 4;
        uint32_t partyGateUrgentMinGapSeconds = 0;
        uint32_t partyGatePreLLMDeferThresholdSeconds = 4;
        uint32_t partyGateMaxFillerDelaySeconds = 45;
        bool partyGateDebugLog = false;

        bool guildChatterEnabled = true;
        uint32_t guildChatterScanSeconds = 60;
        uint32_t guildAmbientChance = 8;
        uint32_t guildAmbientCooldownSeconds = 600;
        uint32_t guildConversationChance = 35;
        uint32_t guildMaximumLines = 3;
        uint32_t guildMaximumParticipants = 2;
        uint32_t guildZoneNameChance = 15;
        uint32_t guildParticipantReferenceChance = 20;
        uint32_t guildHistoryContextChance = 25;
        uint32_t guildRecentSpeakerSuppressionSeconds = 30;
        uint32_t guildReplyDebounceSeconds = 5;
        bool guildLoginGreetingEnabled = true;
        uint32_t guildLoginGreetingChance = 50;
        uint32_t guildLoginGreetingCooldownSeconds = 1800;

        // Guild player-driven conversation controls
        bool guildPlayerRepliesEnabled = true;
        uint32_t guildPlayerRepliesDebounceSeconds = 5;
        uint32_t guildPlayerRepliesIdleSuppressionSeconds = 90;
        uint32_t guildPlayerRepliesMaxCandidates = 12;
        uint32_t guildPlayerRepliesMultiReplyChance = 35;
        uint32_t guildPlayerRepliesMultiAddressedBonus = 50;
        uint32_t guildPlayerRepliesConversationChance = 20;
        uint32_t guildPlayerRepliesMaxResponders = 3;
        uint32_t guildPlayerRepliesPlayerNameChance = 45;
        uint32_t guildPlayerRepliesCallbackChance = 25;
        uint32_t guildPlayerRepliesFollowupQuestionChance = 30;
        uint32_t guildPlayerRepliesRecentSpeakerPenalty = 60;
        uint32_t guildPlayerRepliesFirstDelayMinSeconds = 2;
        uint32_t guildPlayerRepliesFirstDelayMaxSeconds = 6;

        // Guild login greeting extensions
        uint32_t guildLoginGreetingQuickChance = 20;
        uint32_t guildLoginGreetingBusyChance = 25;
        uint32_t guildLoginGreetingRetryIntervalSeconds = 5;
        uint32_t guildLoginGreetingReadinessTimeoutSeconds = 90;
        uint32_t guildLoginGreetingMaxCandidates = 12;
        uint32_t guildLoginGreetingMultiReplyChance = 20;
        uint32_t guildLoginGreetingMaxResponders = 3;
        uint32_t guildLoginGreetingPlayerNameChance = 60;
        uint32_t guildLoginGreetingMaxCharacters = 100;

        bool memoryEnabled = true;
        uint32_t memoryRetentionDays = 180;
        uint32_t memoryMaximumPerPair = 30;
        uint32_t memoryCacheMaximumPairs = 4096;
        uint32_t memoryMaximumPromptItems = 3;
        uint32_t memoryMaximumPromptCharacters = 600;
        uint32_t memoryRecallChance = 15;
        uint32_t memoryFirstMetChance = 20;
        uint32_t memoryPartyMemberChance = 25;
        uint32_t memoryBossKillChance = 50;
        uint32_t memoryQuestCompletedChance = 25;
        uint32_t memoryPvpKillChance = 20;
        uint32_t memoryWipeChance = 50;
        uint32_t memoryLevelUpChance = 40;
        uint32_t memoryDungeonCompletedChance = 30;
        uint32_t memoryDatabaseFlushSeconds = 5;
        uint32_t memoryDatabaseFlushBatchSize = 50;

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

        bool generalChatPacingEnabled = true;
        uint32_t generalChatMinimumGapSeconds = 15;

        bool generalChatterEnabled = true;
        uint32_t generalTriggerIntervalSeconds = 30;
        uint32_t generalTriggerChance = 15;
        uint32_t generalCityMultiplier = 2;
        uint32_t generalConversationChance = 40;
        uint32_t generalNpcGossipChance = 5;
        uint32_t generalBotGossipChance = 5;
        uint32_t generalGossipTargetCooldownSeconds = 1800;
        uint32_t generalBotSpeakerCooldownSeconds = 900;

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
        TypingDelayRange typingDelayPerCharacterMilliseconds;
        bool subtractGenerationTime = true;
        uint32_t maximumReplyCharacters = 180;
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

        bool addonEnabled = true;
        uint32_t addonMaximumRosterEntries = 200;

        static Config Load();
        std::string ResolveApiKey() const;
    };
}
