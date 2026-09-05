#include "AzerothVoicesConfig.h"

#include "Config/Config.h"
#include "Log.h"

#undef sConfig
// Config/Config.h defines sConfig with an unqualified Config type. This file
// also owns AzerothVoices::Config, so force the singleton's two template
// arguments to the core's global Config class and avoid namespace lookup
// selecting the module settings struct.
#define sConfig (MaNGOS::Singleton<::Config, ::Config::Lock>::Instance())

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
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
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            return value;
        }

        std::vector<std::string> Split(std::string const& value, char delimiter)
        {
            std::vector<std::string> result;
            std::stringstream input(value);
            std::string item;
            while (std::getline(input, item, delimiter))
            {
                item = Trim(item);
                if (!item.empty())
                    result.push_back(item);
            }
            return result;
        }

        uint32_t Percent(char const* key, uint32_t fallback)
        {
            int32 value = sConfig.GetIntDefault(key, static_cast<int32>(fallback));
            return static_cast<uint32_t>(std::max<int32>(0, std::min<int32>(100, value)));
        }

        uint32_t Positive(char const* key, uint32_t fallback, uint32_t minimum = 0)
        {
            int32 value = sConfig.GetIntDefault(key, static_cast<int32>(fallback));
            return static_cast<uint32_t>(std::max<int32>(static_cast<int32>(minimum), value));
        }

        uint32_t Bounded(char const* key, uint32_t fallback, uint32_t minimum, uint32_t maximum)
        {
            int32 value = sConfig.GetIntDefault(key, static_cast<int32>(fallback));
            if (value < static_cast<int32>(minimum) || value > static_cast<int32>(maximum))
            {
                sLog.outError("[AzerothVoices][CONFIG] %s=%d is outside %u-%u; using default %u.",
                    key, value, minimum, maximum, fallback);
                return fallback;
            }
            return static_cast<uint32_t>(value);
        }

        std::set<uint32_t> UnsignedSet(char const* key, char const* fallback,
                                       uint32_t maximum = std::numeric_limits<uint32_t>::max())
        {
            std::set<uint32_t> result;
            for (std::string const& item : Split(sConfig.GetStringDefault(key, fallback), ','))
            {
                if (!std::all_of(item.begin(), item.end(), [](unsigned char value) {
                        return std::isdigit(value) != 0;
                    }))
                {
                    sLog.outError("[AzerothVoices][CONFIG] %s contains invalid value '%s'; ignoring it.",
                        key, item.c_str());
                    continue;
                }

                unsigned long long const value = std::strtoull(item.c_str(), nullptr, 10);
                if (value > maximum)
                {
                    sLog.outError("[AzerothVoices][CONFIG] %s value '%s' exceeds %u; ignoring it.",
                        key, item.c_str(), maximum);
                    continue;
                }
                result.insert(static_cast<uint32_t>(value));
            }
            return result;
        }
    }

    Config Config::Load()
    {
        Config c;
        c.enabled = sConfig.GetBoolDefault("AzerothVoices.Enable", true);
        c.debug = sConfig.GetBoolDefault("AzerothVoices.Debug", false);
        c.consoleGeneratedMessages = sConfig.GetBoolDefault(
            "AzerothVoices.Console.GeneratedMessages",
            sConfig.GetBoolDefault("AzerothVoices.Telemetry.LogGeneratedMessages", false));
        c.consoleApiCallStats = sConfig.GetBoolDefault(
            "AzerothVoices.Console.ApiCallStats",
            sConfig.GetBoolDefault("AzerothVoices.Telemetry.Summary.Enable", false));
        c.consoleApiCallStatsIntervalSeconds = Positive(
            "AzerothVoices.Console.ApiCallStatsIntervalSeconds",
            Positive("AzerothVoices.Telemetry.Summary.IntervalSeconds", 60, 5), 5);

        c.providerMode = Trim(sConfig.GetStringDefault("AzerothVoices.ProviderMode", "ChatCompletions"));
        c.endpoint = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMApiEndpoint", "https://api.openai.com/v1/chat/completions"));
        c.apiKey = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMApiKey", "env:OPENAI_API_KEY"));
        c.model = Trim(sConfig.GetStringDefault("AzerothVoices.Model", "gpt-4.1-mini"));
        c.apiJsonTemplate = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMApiJson", ""));
        c.caCertFile = Trim(sConfig.GetStringDefault("AzerothVoices.CACertFile", ""));
        c.allowInsecureLocalHttp = sConfig.GetBoolDefault("AzerothVoices.AllowLocalHttp", true);
        c.connectTimeoutSeconds = Positive("AzerothVoices.ConnectTimeoutSeconds", 10, 1);
        c.requestTimeoutSeconds = Positive("AiPlayerbot.LLMGenerationTimeout", 60, 1);
        c.maxResponseBytes = Positive("AzerothVoices.MaxResponseBytes", 65536, 1024);
        c.maxTokens = Positive("AzerothVoices.MaxTokens", 80, 1);
        c.temperature = std::max(0.0f, std::min(2.0f,
            sConfig.GetFloatDefault("AzerothVoices.Temperature", 0.8f)));
        c.topP = std::max(0.0f, std::min(1.0f,
            sConfig.GetFloatDefault("AzerothVoices.TopP", 0.95f)));

        c.workerThreads = std::min<uint32_t>(32, Positive("AzerothVoices.WorkerThreads",
            Positive("AiPlayerbot.LLMMaxSimultaniousGenerations", 8, 1), 1));
        c.queueMaximum = Positive("AzerothVoices.QueueMaximum", 128, 1);
        c.highPriorityReserve = Positive("AzerothVoices.HighPriorityReserve", 32);
        c.highPriorityReserve = std::min(c.highPriorityReserve, c.queueMaximum);
        c.requestTtlSeconds = Positive("AzerothVoices.RequestTTLSeconds", 90, 1);
        c.actorCooldownSeconds = Positive("AzerothVoices.ActorCooldownSeconds", 10);
        c.ambientActorCooldownSeconds = Positive("AzerothVoices.AmbientActorCooldownSeconds", 120);
        c.retryMaximum = Positive("AzerothVoices.RetryMaximum", 1);
        c.retryBackoffMilliseconds = Positive("AzerothVoices.RetryBackoffMilliseconds", 500);
        c.globalRequestsPerMinute = Positive("AzerothVoices.GlobalRequestsPerMinute", 60, 1);
        c.speakerCooldownSeconds = Positive("AzerothVoices.SpeakerCooldownSeconds", 3);

        std::string globalMode = Trim(sConfig.GetStringDefault("AzerothVoices.GlobalMode", "Normal"));
        std::transform(globalMode.begin(), globalMode.end(), globalMode.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (globalMode == "roleplay")
            c.globalMode = GlobalMode::Roleplay;
        else
        {
            if (globalMode != "normal")
                sLog.outError("[AzerothVoices][CONFIG] AzerothVoices.GlobalMode='%s' is invalid; using Normal.",
                    globalMode.c_str());
            c.globalMode = GlobalMode::Normal;
        }

        std::string const legacyGlobalPrompt = Trim(
            sConfig.GetStringDefault("AzerothVoices.GlobalPrompt", ""));
        std::string const defaultRoleplayPrompt =
            "You create short, natural in-world dialogue for Vanilla/Turtle WoW. The speaker lives in Azeroth "
            "and treats its places, factions, quests, creatures, dungeons, travel, danger, and events as real "
            "lived experience. Stay in character and do not narrate actions. Never mention AI, prompts, policies, "
            "APIs, automation, game servers, controlling a character, or being a real-world player. Return only "
            "what the speaker says.";
        std::string const defaultNormalPrompt =
            "You create short, natural World of Warcraft chat. Interaction metadata: speaker=<bot type>; "
            "counterpart=<other type>; counterpart-race=<other race>. If the speaker is an NPC or counterpart-race "
            "is NPC, speak entirely in-world as a believable Vanilla/Turtle WoW character and never talk like "
            "someone at a computer. Otherwise the speaker is a PlayerBot representing a believable real human "
            "playing World of Warcraft: talk naturally like a real player about quests, leveling, gear, builds, "
            "talents, dungeons, guilds, PvP, travel, wipes, groups, and game mechanics when relevant; do not pretend "
            "Azeroth is real unless the conversation itself is roleplay. Avoid forced gamer slang and repetitive "
            "stereotypes. Never mention AI, prompts, policies, APIs, automation, or being a narrator. Return only "
            "what the speaker says.";
        std::string const roleplayFallback = legacyGlobalPrompt.empty()
            ? defaultRoleplayPrompt : legacyGlobalPrompt;
        c.globalPromptRoleplay = Trim(sConfig.GetStringDefault(
            "AzerothVoices.GlobalPrompt.Roleplay", roleplayFallback.c_str()));
        c.globalPromptNormal = Trim(sConfig.GetStringDefault(
            "AzerothVoices.GlobalPrompt.Normal", defaultNormalPrompt.c_str()));
        if (c.globalPromptRoleplay.empty())
            c.globalPromptRoleplay = roleplayFallback;
        if (c.globalPromptNormal.empty())
            c.globalPromptNormal = defaultNormalPrompt;
        c.globalPrompt = c.globalMode == GlobalMode::Normal
            ? c.globalPromptNormal : c.globalPromptRoleplay;

        c.prePrompt = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMPrePrompt",
            "You are <bot name>, a level <bot level> <bot race> <bot class> in <bot subzone>, <bot zone>. "
            "<bot personality block>"));
        c.prompt = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMPrompt", "<sender name>: <initial message>"));
        c.postPrompt = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMPostPrompt", ""));
        c.rpgPrompt = Trim(sConfig.GetStringDefault("AiPlayerbot.LLMRpgPrompt",
            "You are <bot name>, an NPC in <bot subzone>, <bot zone>. Reply briefly in character."));
        c.contextLength = Positive("AiPlayerbot.LLMContextLength", 4096);
        c.globalContext = sConfig.GetBoolDefault("AiPlayerbot.LLMGlobalContext", false);
        c.parserMode = Trim(sConfig.GetStringDefault("AzerothVoices.ParserMode", "ProviderJson"));
        c.responseStartPattern = sConfig.GetStringDefault("AiPlayerbot.LLMResponseStartPattern", R"(("content":\s*"))");
        c.responseEndPattern = sConfig.GetStringDefault("AiPlayerbot.LLMResponseEndPattern", R"(("|\b(?!<sender name>\b)(\w+):))");
        c.responseDeletePattern = sConfig.GetStringDefault("AiPlayerbot.LLMResponseDeletePattern", R"((\\n|<sender name>:|\\[^ ]+))");
        c.responseSplitPattern = sConfig.GetStringDefault("AiPlayerbot.LLMResponseSplitPattern", "");
        c.blockedChannels = Split(sConfig.GetStringDefault("AiPlayerbot.LLMBlockedReplyChannels", ""), ',');

        c.personalityEnabled = Bounded("AzerothVoices.Personality.Enable", 1, 0, 1) != 0;
        c.personalityBackgroundMode = Bounded("AzerothVoices.Personality.BackgroundMode", 1, 0, 1);
        c.personalityGenerateBackground = Bounded(
            "AzerothVoices.Personality.GenerateBackground", 1, 0, 1) != 0;
        c.personalityTraitCount = Bounded("AzerothVoices.Personality.TraitCount", 3, 1, 5);
        c.personalityGenerateTone = Bounded("AzerothVoices.Personality.GenerateTone", 1, 0, 1) != 0;
        c.personalityGenerateOnDemand = Bounded(
            "AzerothVoices.Personality.GenerateOnDemand", 1, 0, 1) != 0;
        c.personalityUseInRandom = Bounded(
            "AzerothVoices.Personality.UseInRandom", 1, 0, 1) != 0;
        c.personalityUseInEvents = Bounded(
            "AzerothVoices.Personality.UseInEvents", 1, 0, 1) != 0;
        c.personalityGenerationRetrySeconds = Bounded(
            "AzerothVoices.Personality.GenerationRetrySeconds", 300, 10, 86400);
        c.personalityMaxBackgroundCharacters = Bounded(
            "AzerothVoices.Personality.MaxBackgroundChars", 500, 100, 1500);
        c.personalityMaxPromptCharacters = Bounded(
            "AzerothVoices.Personality.MaxPromptChars", 700, 100, 2000);

        c.sentimentEnabled = Bounded("AzerothVoices.Sentiment.Enable", 0, 0, 1) != 0;
        c.sentimentUseInRandom = Bounded(
            "AzerothVoices.Sentiment.UseInRandom", 0, 0, 1) != 0;
        c.sentimentUseInEvents = Bounded(
            "AzerothVoices.Sentiment.UseInEvents", 0, 0, 1) != 0;
        c.sentimentConversationMaximumDelta = Bounded(
            "AzerothVoices.Sentiment.ConversationMaximumDelta", 2, 0, 2);
        c.sentimentInactivityGraceDays = Bounded(
            "AzerothVoices.Sentiment.InactivityGraceDays", 7, 0, 365);
        c.sentimentPositiveDecayPerDay = Bounded(
            "AzerothVoices.Sentiment.PositiveDecayPerDay", 1, 0, 100);
        c.sentimentNegativeDecayPerDay = Bounded(
            "AzerothVoices.Sentiment.NegativeDecayPerDay", 2, 0, 100);
        c.sentimentCacheMaximumEntries = Bounded(
            "AzerothVoices.Sentiment.CacheMaximumEntries", 4096, 1, 100000);
        c.sentimentPendingWriteMaximum = Bounded(
            "AzerothVoices.Sentiment.PendingWriteMaximum", 1024, 1, 100000);
        c.sentimentDatabaseFlushSeconds = Bounded(
            "AzerothVoices.Sentiment.DatabaseFlushSeconds", 5, 1, 3600);
        uint32 const sentimentDefaultFlushBatchSize = std::min<uint32>(50, c.sentimentPendingWriteMaximum);
        c.sentimentDatabaseFlushBatchSize = Bounded(
            "AzerothVoices.Sentiment.DatabaseFlushBatchSize", sentimentDefaultFlushBatchSize, 1,
            c.sentimentPendingWriteMaximum);

        c.whisperReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Whisper", true);
        c.sayReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Say", true);
        c.yellReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Yell", true);
        c.partyReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Party", true);
        c.raidReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Raid", true);
        c.guildReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Guild", true);
        c.officerReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.Officer", false);
        c.worldReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.World", true);
        c.customChannelReplies = sConfig.GetBoolDefault("AzerothVoices.Replies.CustomChannels", false);
        c.npcReplies = sConfig.GetBoolDefault("AzerothVoices.NPC.Enable", true);
        c.disableRepliesInCombat = sConfig.GetBoolDefault("AzerothVoices.DisableRepliesInCombat", false);
        c.maxResponders = Positive("AzerothVoices.MaxResponders", 2, 1);
        c.maxResponders.falloffEnabled = Bounded(
            "AzerothVoices.ResponderFalloff.Enable", 1, 0, 1) != 0;
        c.maxResponders.secondChance = Percent("AzerothVoices.ResponderFalloff.SecondChance", 50);
        c.maxResponders.chanceDelta = Percent("AzerothVoices.ResponderFalloff.Delta", 20);
        c.sayDistance = std::max(1.0f, sConfig.GetFloatDefault("AzerothVoices.SayDistance", 25.0f));
        c.yellDistance = std::max(c.sayDistance, sConfig.GetFloatDefault("AzerothVoices.YellDistance", 100.0f));
        c.npcDistance = std::max(1.0f, sConfig.GetFloatDefault("AzerothVoices.NPC.Distance", 10.0f));
        c.npcAllowedTypes = UnsignedSet("AzerothVoices.NPC.AllowedTypes", "2,3,4,5,6,7,9", 11);
        c.npcAllowNeutralAndHostile = sConfig.GetBoolDefault(
            "AzerothVoices.NPC.AllowNeutralAndHostile", true);
        c.npcFriendlyReplyChance = Percent("AzerothVoices.NPC.ReplyChance.Friendly", 100);
        c.npcNeutralReplyChance = Percent("AzerothVoices.NPC.ReplyChance.Neutral", 50);
        c.npcHostileReplyChance = Percent("AzerothVoices.NPC.ReplyChance.Hostile", 25);
        c.npcCombatStartEnabled = sConfig.GetBoolDefault("AzerothVoices.NPC.CombatStart.Enable", true);
        c.npcCombatStartChance = Percent("AzerothVoices.NPC.CombatStart.Chance", 30);
        c.npcCombatStartCooldownSeconds = Positive("AzerothVoices.NPC.CombatStart.CooldownSeconds", 60);
        c.targetedNpcReplyChance = Percent("AzerothVoices.NPC.TargetedReplyChance", 100);
        c.targetedNpcJoinChance = Percent("AzerothVoices.NPC.TargetedOtherNPCJoinChance", 5);
        c.targetedNpcPlayerBotJoinChance = Percent(
            "AzerothVoices.NPC.TargetedPlayerBotJoinChance", 10);
        c.exclusiveNameMentionResponder = Bounded(
            "AzerothVoices.NameMention.ExclusiveResponder", 1, 0, 1) != 0;
        c.directAddressChance = Percent("AzerothVoices.Chance.DirectAddress", 100);
        c.nameMentionChance = Percent("AzerothVoices.Chance.NameMention", 70);
        c.overhearChance = Percent("AzerothVoices.Chance.Overhear", 8);
        c.playerReplyChanceSay = Percent("AzerothVoices.PlayerReplyChance.Say", 90);
        c.botReplyChanceSay = Percent("AzerothVoices.BotReplyChance.Say", 10);
        c.playerReplyChanceChannel = Percent("AzerothVoices.PlayerReplyChance.Channel", 60);
        c.botReplyChanceChannel = Percent("AzerothVoices.BotReplyChance.Channel", 3);
        c.playerReplyChanceParty = Percent("AzerothVoices.PlayerReplyChance.Party", 90);
        c.botReplyChanceParty = Percent("AzerothVoices.BotReplyChance.Party", 25);
        c.playerReplyChanceGuild = Percent("AzerothVoices.PlayerReplyChance.Guild", 70);
        c.botReplyChanceGuild = Percent("AzerothVoices.BotReplyChance.Guild", 5);
        c.botToBotChatChance = Percent("AiPlayerbot.LLMBotToBotChatChance", 10);
        c.rpgAiChatChance = Percent("AiPlayerbot.LLMRpgAIChatChance", 30);
        c.worldChannelName = Trim(sConfig.GetStringDefault("AzerothVoices.WorldChannelName", "World"));
        c.commandBlacklist = Split(sConfig.GetStringDefault("AzerothVoices.CommandBlacklist",
            ".,!,/,#,$,autogear,talents,summon,release,revive,attack,follow,stay,cast,quest,trainer,teleport,addon,DBM,Recount,Questie"), ',');

        c.randomChatterEnabled = sConfig.GetBoolDefault("AzerothVoices.Random.Enable", true);
        c.randomMinimumIntervalSeconds = Positive("AzerothVoices.Random.MinimumIntervalSeconds", 90, 5);
        c.randomMaximumIntervalSeconds = Positive("AzerothVoices.Random.MaximumIntervalSeconds", 240, c.randomMinimumIntervalSeconds);
        c.randomFollowupChance = Percent("AzerothVoices.Random.FollowupChance", 15);
        c.randomMaximumActors = Positive("AzerothVoices.Random.MaximumActors", 2, 1);
        c.randomScopes = Split(sConfig.GetStringDefault(
            "AzerothVoices.Random.Scopes", "say,guild,world,party"), ',');
        c.randomPrompts = Split(sConfig.GetStringDefault("AzerothVoices.Random.Prompts",
            "Make a casual observation about the current zone.|Complain briefly about leveling or grinding.|"
            "Comment on loot, repairs, travel, professions, quests, dungeons, or battlegrounds.|"
            "Make a short joke that fits World of Warcraft.|Mention what you want to do next in the game."), '|');
        c.randomQuestions = Split(sConfig.GetStringDefault("AzerothVoices.Random.Questions",
            "Ask whether anyone wants to group.|Ask for an opinion about the current zone.|"
            "Ask which dungeon people like.|Ask what people are farming.|Ask about professions, gear, or PvP."), '|');
        c.environmentPrompts = Split(sConfig.GetStringDefault("AzerothVoices.Random.EnvironmentPrompts",
            "React to something that might be nearby.|Comment on the weather or surroundings.|"
            "Mention a creature, object, quest, spell, item, vendor, dungeon, or unfinished task that fits the location."), '|');
        c.guildPrompts = Split(sConfig.GetStringDefault("AzerothVoices.Random.GuildPrompts",
            "Ask whether guild members want a dungeon group.|Congratulate the guild on recent progress.|"
            "Talk about guild plans, raids, PvP, professions, loot, the guild bank, or helping another member."), '|');
        c.worldPrompts = Split(sConfig.GetStringDefault("AzerothVoices.Random.WorldPrompts",
            "Start a short world-chat topic about zones, dungeons, loot, professions, PvP, quests, travel, or server life.|"
            "Ask world chat a brief useful question.|Make a short public observation that could start a conversation."), '|');

        c.environmentContextEnabled = sConfig.GetBoolDefault("AzerothVoices.Environment.Enable", true);
        c.environmentContextDistance = std::min(100.0f, std::max(1.0f,
            sConfig.GetFloatDefault("AzerothVoices.Environment.Distance", 25.0f)));
        c.environmentMaximumCreatures = std::min<uint32_t>(50,
            Positive("AzerothVoices.Environment.MaximumCreatures", 5));
        c.environmentMaximumItems = std::min<uint32_t>(50,
            Positive("AzerothVoices.Environment.MaximumItems", 8));
        c.environmentIncludeEquipment = sConfig.GetBoolDefault("AzerothVoices.Environment.IncludeEquipment", true);
        c.environmentIncludeBackpack = sConfig.GetBoolDefault("AzerothVoices.Environment.IncludeBackpack", false);

        c.eventChatterEnabled = sConfig.GetBoolDefault("AzerothVoices.Events.Enable", true);
        c.eventResponderChance = Percent("AzerothVoices.Events.ResponderChance", 25);
        c.eventSelfCommentChance = Percent("AzerothVoices.Events.SelfCommentChance", 5);
        c.eventMaximumResponders = Positive("AzerothVoices.Events.MaximumResponders", 2, 1);
        c.eventCooldownSeconds = Positive("AzerothVoices.Events.CooldownSeconds", 60);
        c.eventChances = {
            { "creature_defeated", Percent("AzerothVoices.Events.Chance.CreatureDefeated", 8) },
            { "player_defeated", Percent("AzerothVoices.Events.Chance.PlayerDefeated", 40) },
            { "died", Percent("AzerothVoices.Events.Chance.Died", 25) },
            { "item_looted", Percent("AzerothVoices.Events.Chance.ItemLooted", 8) },
            { "quest_completed", Percent("AzerothVoices.Events.Chance.QuestCompleted", 25) },
            { "spell_learned", Percent("AzerothVoices.Events.Chance.SpellLearned", 15) },
            { "duel_requested", Percent("AzerothVoices.Events.Chance.DuelRequested", 25) },
            { "duel_started", Percent("AzerothVoices.Events.Chance.DuelStarted", 20) },
            { "duel_won", Percent("AzerothVoices.Events.Chance.DuelWon", 35) },
            { "level_up", Percent("AzerothVoices.Events.Chance.LevelUp", 50) },
            { "guild_join", Percent("AzerothVoices.Events.Chance.GuildJoin", 60) },
            { "guild_leave", Percent("AzerothVoices.Events.Chance.GuildLeave", 40) },
            { "guild_login", Percent("AzerothVoices.Events.Chance.GuildLogin", 10) },
            { "guild_promotion", Percent("AzerothVoices.Events.Chance.GuildPromotion", 50) },
            { "guild_demotion", Percent("AzerothVoices.Events.Chance.GuildDemotion", 30) },
            { "achievement", Percent("AzerothVoices.Events.Chance.Achievement", 60) },
            { "pet_defeated", Percent("AzerothVoices.Events.Chance.PetDefeated", 20) },
            { "used_object", Percent("AzerothVoices.Events.Chance.UsedObject", 10) },
            { "rare_item", Percent("AzerothVoices.Events.Chance.RareItem", 45) },
            { "epic_item", Percent("AzerothVoices.Events.Chance.EpicItem", 75) },
            { "dungeon_completed", Percent("AzerothVoices.Events.Chance.DungeonCompleted", 70) },
            { "game_event_started", Percent("AzerothVoices.Events.Chance.GameEventStarted", 50) },
            { "game_event_stopped", Percent("AzerothVoices.Events.Chance.GameEventStopped", 30) }
        };

        c.typingSimulationEnabled = sConfig.GetBoolDefault("AzerothVoices.Typing.Enable", true);
        c.typingBaseDelayMilliseconds = Positive("AzerothVoices.Typing.BaseDelayMilliseconds", 0);
        c.typingDelayPerCharacterMilliseconds = Positive("AzerothVoices.Typing.DelayPerCharacterMilliseconds", 200);
        c.subtractGenerationTime = sConfig.GetBoolDefault("AzerothVoices.Typing.SubtractGenerationTime", true);
        c.maximumReplyCharacters = Positive("AzerothVoices.Reply.MaximumCharacters", 220, 1);
        c.maximumReplyLines = Positive("AzerothVoices.Reply.MaximumLines", 3, 1);

        bool const legacyHistoryEnabled = sConfig.GetBoolDefault("AzerothVoices.History.Enable", true);
        c.historyStorageMode = std::min<uint32_t>(2, Positive(
            "AzerothVoices.History.StorageMode", legacyHistoryEnabled ? 2 : 0));
        c.historyRamMaximumTurns = Positive("AzerothVoices.History.RamMaximumTurns",
            Positive("AzerothVoices.History.MaximumTurns", 6));
        c.historyDatabaseMaximumTurns = Positive("AzerothVoices.History.DatabaseMaximumTurns", 20);
        c.historyTtlMinutes = Positive("AzerothVoices.History.TTLMinutes", 30, 1);
        c.historyDatabaseTtlMinutes = Positive("AzerothVoices.History.DatabaseTTLMinutes", 10080, 1);
        c.historyMaximumCharacters = Positive("AzerothVoices.History.MaximumCharacters", 2500, 128);
        c.historyMaximumConversations = Positive("AzerothVoices.History.MaximumConversations", 2048, 1);
        c.historyDatabaseFlushSeconds = Positive("AzerothVoices.History.DatabaseFlushSeconds", 5, 1);
        c.historyDatabaseFlushBatchSize = Positive("AzerothVoices.History.DatabaseFlushBatchSize", 20, 1);
        c.historyHeaderTemplate = sConfig.GetStringDefault("AzerothVoices.History.HeaderTemplate",
            "Recent conversation with <sender name>, use it only as context:");
        c.historyLineTemplate = sConfig.GetStringDefault("AzerothVoices.History.LineTemplate",
            "<sender name>: <sender message>\n<bot name>: <bot reply>\n");
        c.historyFooterTemplate = sConfig.GetStringDefault("AzerothVoices.History.FooterTemplate",
            "New message from <sender name>: <initial message>");

        c.surroundingChatEnabled = sConfig.GetBoolDefault("AzerothVoices.SurroundingChat.Enable", true);
        c.surroundingChatMaximumLines = Positive("AzerothVoices.SurroundingChat.MaximumLines", 8);
        c.surroundingChatTtlMinutes = Positive("AzerothVoices.SurroundingChat.TTLMinutes", 5, 1);
        c.surroundingChatMaximumCharacters = Positive("AzerothVoices.SurroundingChat.MaximumCharacters", 1200, 128);
        c.surroundingChatMaximumScopes = Positive("AzerothVoices.SurroundingChat.MaximumScopes", 512, 1);

        // Older V0.1 configurations used Environment.History* for a much
        // smaller snapshot. Read those keys only as silent migration fallbacks.
        uint32_t const legacySnapshotMode = std::min<uint32_t>(2, Positive(
            "AzerothVoices.Environment.HistoryStorageMode", 0));
        c.snapshotEnabled = sConfig.GetBoolDefault("AzerothVoices.Snapshot.Enable", legacySnapshotMode > 0);
        c.snapshotIncludeCombat = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeCombat", true);
        c.snapshotIncludeGroup = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeGroup", true);
        c.snapshotIncludeSpells = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeSpells", true);
        c.snapshotIncludeQuests = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeQuests", true);
        c.snapshotIncludeLineOfSight = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeLineOfSight", true);
        c.snapshotIncludeNearbyPlayers = sConfig.GetBoolDefault("AzerothVoices.Snapshot.IncludeNearbyPlayers", true);
        c.snapshotDistance = std::min(100.0f, std::max(1.0f,
            sConfig.GetFloatDefault("AzerothVoices.Snapshot.Distance", 40.0f)));
        c.snapshotMaximumGroupMembers = std::min<uint32_t>(40, Positive("AzerothVoices.Snapshot.MaximumGroupMembers", 5));
        c.snapshotMaximumSpells = std::min<uint32_t>(100, Positive("AzerothVoices.Snapshot.MaximumSpells", 20));
        c.snapshotMaximumQuests = std::min<uint32_t>(20, Positive("AzerothVoices.Snapshot.MaximumQuests", 10));
        c.snapshotMaximumCreatures = std::min<uint32_t>(50, Positive("AzerothVoices.Snapshot.MaximumCreatures", 10));
        c.snapshotMaximumGameObjects = std::min<uint32_t>(50, Positive("AzerothVoices.Snapshot.MaximumGameObjects", 8));
        c.snapshotMaximumPlayers = std::min<uint32_t>(50, Positive("AzerothVoices.Snapshot.MaximumPlayers", 8));
        c.snapshotMaximumCharacters = std::min<uint32_t>(16000,
            Positive("AzerothVoices.Snapshot.MaximumCharacters", 3500, 256));
        c.snapshotPromptTemplate = Trim(sConfig.GetStringDefault("AzerothVoices.Snapshot.PromptTemplate",
            "CURRENT PLAYERBOT SNAPSHOT:\\n{combat}\\n{group}\\n{spells}\\n{quests}\\n{line_of_sight}\\n{nearby_players}"));
        c.snapshotStorageMode = std::min<uint32_t>(2, Positive(
            "AzerothVoices.Snapshot.StorageMode", legacySnapshotMode));
        c.snapshotRamMaximumSnapshots = Positive("AzerothVoices.Snapshot.RamMaximumSnapshots",
            Positive("AzerothVoices.Environment.RamMaximumSnapshots", 3));
        c.snapshotDatabaseMaximumSnapshots = Positive("AzerothVoices.Snapshot.DatabaseMaximumSnapshots",
            Positive("AzerothVoices.Environment.DatabaseMaximumSnapshots", 10));
        c.snapshotHistoryTtlMinutes = Positive("AzerothVoices.Snapshot.HistoryTTLMinutes",
            Positive("AzerothVoices.Environment.HistoryTTLMinutes", 30, 1), 1);
        c.snapshotDatabaseTtlMinutes = Positive("AzerothVoices.Snapshot.DatabaseTTLMinutes",
            Positive("AzerothVoices.Environment.DatabaseTTLMinutes", 10080, 1), 1);
        c.snapshotHistoryMaximumCharacters = Positive("AzerothVoices.Snapshot.HistoryMaximumCharacters",
            Positive("AzerothVoices.Environment.HistoryMaximumCharacters", 1600, 128), 128);
        c.snapshotHistoryMaximumActors = Positive("AzerothVoices.Snapshot.HistoryMaximumActors",
            Positive("AzerothVoices.Environment.HistoryMaximumActors", 2048, 1), 1);

        c.ragEnabled = sConfig.GetBoolDefault("AzerothVoices.RAG.Enable", false);
        c.ragDirectory = Trim(sConfig.GetStringDefault("AzerothVoices.RAG.Directory",
            "modules/mod-azeroth-voices/data/rag/"));
        c.ragMaximumItems = Positive("AzerothVoices.RAG.MaximumItems", 3, 1);
        c.ragSimilarityThreshold = std::max(0.0f, std::min(1.0f,
            sConfig.GetFloatDefault("AzerothVoices.RAG.SimilarityThreshold", 0.3f)));
        c.ragMaximumCharacters = Positive("AzerothVoices.RAG.MaximumCharacters", 1200, 128);
        c.ragReloadOnRestart = sConfig.GetBoolDefault("AzerothVoices.RAG.ReloadOnRestart", true);
        c.ragPromptTemplate = Trim(sConfig.GetStringDefault("AzerothVoices.RAG.PromptTemplate",
            "RELEVANT INFORMATION:\\n{rag_info}\\nUse this information to provide accurate and detailed responses when applicable."));

        return c;
    }

    std::string Config::ResolveApiKey() const
    {
        static std::string const envPrefix = "env:";
        if (apiKey.compare(0, envPrefix.size(), envPrefix) != 0)
            return apiKey;

        std::string variable = apiKey.substr(envPrefix.size());
        char const* value = std::getenv(variable.c_str());
        return value ? value : "";
    }
}
