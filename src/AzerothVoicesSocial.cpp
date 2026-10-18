#include "AzerothVoicesSocial.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace AzerothVoices
{
    char const* GroupTriggerName(GroupTrigger trigger)
    {
        switch (trigger)
        {
            case GroupTrigger::Idle: return "idle";
            case GroupTrigger::DungeonEntry: return "dungeon-entry";
            case GroupTrigger::ZoneChange: return "zone-change";
            case GroupTrigger::TrashKill: return "trash-kill";
            case GroupTrigger::BossPull: return "boss-pull";
            case GroupTrigger::BossKill: return "boss-kill";
            case GroupTrigger::Wipe: return "wipe";
            case GroupTrigger::Death: return "death";
            case GroupTrigger::CorpseRun: return "corpse-run";
            case GroupTrigger::Resurrect: return "resurrect";
            case GroupTrigger::Loot: return "loot";
            case GroupTrigger::QuestAccept: return "quest-accept";
            case GroupTrigger::QuestObjective: return "quest-objective";
            case GroupTrigger::QuestComplete: return "quest-complete";
            case GroupTrigger::SpellCast: return "spell-cast";
            case GroupTrigger::LowHealth: return "low-health";
            case GroupTrigger::LowMana: return "low-mana";
            case GroupTrigger::NearbyObject: return "nearby-object";
            case GroupTrigger::BotQuestion: return "bot-question";
            case GroupTrigger::PlayerFollowup: return "player-followup";
            case GroupTrigger::BattleCry: return "battle-cry";
            case GroupTrigger::Morale: return "morale";
            case GroupTrigger::Count: break;
        }
        return "idle";
    }

    bool IsRaidOnlyTrigger(GroupTrigger trigger)
    {
        return trigger == GroupTrigger::BattleCry || trigger == GroupTrigger::Morale;
    }

    uint32_t LootChanceForQuality(uint32_t quality, LootChanceTable const& table)
    {
        switch (quality)
        {
            case 2: return std::min<uint32_t>(100, table.uncommon);
            case 3: return std::min<uint32_t>(100, table.rare);
            case 4: return std::min<uint32_t>(100, table.epic);
            default: return quality >= 5 ? std::min<uint32_t>(100, table.legendary) : 0;
        }
    }

    bool WipeDetector::Update(size_t members, size_t deadMembers, bool anyInCombat)
    {
        if (latched)
        {
            if (deadMembers < members || anyInCombat)
                latched = false;
            return false;
        }
        if (members && deadMembers >= members && !anyInCombat)
        {
            latched = true;
            return true;
        }
        return false;
    }

    void WipeDetector::Reset()
    {
        latched = false;
    }

    uint64_t MixQuestLogHash(uint64_t hash, uint64_t value)
    {
        // FNV-1a style mixing; order sensitive so reordered logs still differ.
        hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
        return hash;
    }

    QuestLogEvent DiffQuestLog(QuestLogSnapshot const& previous, QuestLogSnapshot const& current)
    {
        if (!current.known)
            return QuestLogEvent::None;
        if (!previous.known)
            return QuestLogEvent::None;
        if (current.entries > previous.entries)
            return QuestLogEvent::Accepted;
        if (current.entries < previous.entries)
            return QuestLogEvent::Completed;
        if (current.hash != previous.hash)
            return QuestLogEvent::ObjectiveProgressed;
        return QuestLogEvent::None;
    }

    bool ThresholdLatch::Update(bool belowThreshold, bool recovered)
    {
        if (latched)
        {
            if (recovered)
                latched = false;
            return false;
        }
        if (belowThreshold)
        {
            latched = true;
            return true;
        }
        return false;
    }

    void ThresholdLatch::Reset()
    {
        latched = false;
    }

    bool GroupConversationBudget::CanStartConversation(uint32_t eligibleSpeakers) const
    {
        return eligibleSpeakers >= 2 && maximumLines >= 2;
    }

    bool GroupConversationBudget::CanContinue(uint32_t deliveredLines, uint32_t replyTurns) const
    {
        return deliveredLines < maximumLines && replyTurns < maximumReplyTurns;
    }

    bool ShouldSendLoginGreeting(GreetingGateInput const& input)
    {
        return input.realMemberLogin && input.guildedBotOnline &&
            input.recipientCooldownReady && input.chancePassed;
    }

    namespace
    {
        std::string Trim(std::string const& str)
        {
            size_t const first = str.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return "";
            size_t const last = str.find_last_not_of(" \t\r\n");
            return str.substr(first, (last - first + 1));
        }

        std::string StripAddressingPrefix(std::string const& text)
        {
            static char const* const prefixes[] = {
                "all ",
                "@tank ",
                "@heal ",
                "@dps ",
                "@melee ",
                "@ranged ",
                "@role "
            };
            for (char const* prefix : prefixes)
            {
                size_t const len = std::strlen(prefix);
                if (text.size() > len)
                {
                    bool match = true;
                    for (size_t i = 0; i < len; ++i)
                    {
                        if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i])
                        {
                            match = false;
                            break;
                        }
                    }
                    if (match)
                        return Trim(text.substr(len));
                }
            }
            return text;
        }

        bool MatchToken(std::string const& text, std::string const& token)
        {
            if (token.empty() || text.size() < token.size())
                return false;

            for (size_t i = 0; i < token.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(text[i])) !=
                    std::tolower(static_cast<unsigned char>(token[i])))
                    return false;
            }

            // If the token starts with a non-alphanumeric punctuation/symbol (e.g. '.', '/', '!', '#', '$', '~', '@'),
            // any message beginning with that prefix is an ignored server/addon/bot command.
            unsigned char const firstTokenChar = static_cast<unsigned char>(token.front());
            if (!std::isalnum(firstTokenChar))
                return true;

            // If token explicitly ends with whitespace (e.g. "! "), matching the prefix already verified the delimiter.
            if (std::isspace(static_cast<unsigned char>(token.back())))
                return true;

            // If exact length match (e.g. "c", "stats", "attack", "follow", "ready")
            if (text.size() == token.size())
                return true;

            // If text is followed immediately by '?' (e.g. "stats?", "ready?", "who?"), treat as command query.
            if (text.size() == token.size() + 1 && text[token.size()] == '?')
                return true;

            // Otherwise, the character right after the token in text must be a delimiter (not alphanumeric).
            // This prevents "c" from blocking "can", "follow" from blocking "following", etc.
            unsigned char const nextChar = static_cast<unsigned char>(text[token.size()]);
            if (std::isalnum(nextChar))
                return false;

            // Single-letter commands ("c", "e", "u", "s", "b"):
            // In Warband, these are only standalone ("c") or followed by an item link ([Item Name]) or flags (?, +, -).
            // Do NOT match conversational sentences like "u want to quest", "c you soon".
            if (token.size() == 1)
            {
                if (text.size() > 2 && text[1] == ' ')
                {
                    char const c = text[2];
                    return c == '[' || c == '?' || c == '+' || c == '-';
                }
                return false;
            }

            // Check if the remainder is a command argument or flag (e.g. "+...", "-...", "?...", "~...", item link, digit).
            std::string remainder = text.substr(token.size());
            size_t const start = remainder.find_first_not_of(" \t");
            if (start == std::string::npos)
                return true; // only trailing whitespace

            char const remFirst = remainder[start];
            if (remFirst == '?' || remFirst == '+' || remFirst == '-' || remFirst == '~' || remFirst == '[' ||
                std::isdigit(static_cast<unsigned char>(remFirst)))
                return true;

            // If token is a multi-word command (contains space, e.g. "d buff", "stance tank", "save mana", "pull rti")
            // matching the prefix is authoritative.
            if (token.find(' ') != std::string::npos)
                return true;

            // For common English words (who, where, home, ready, trade, talk, friends, dungeon, quest, quests, spells, stats, talents, trainer):
            // In Playerbots, these commands take no target name or multi-word sentences.
            // If the message has multiple words continuing as normal text, it is natural conversation, NOT a bot command.
            static char const* const kConversationalCommandWords[] = {
                "who", "where", "home", "ready", "trade", "talk", "friends",
                "dungeon", "quest", "quests", "spells", "stats", "talents", "trainer", "mail", "online", "roles"
            };
            for (char const* convWord : kConversationalCommandWords)
            {
                if (token == convWord)
                    return false; // conversational argument, not a bot command!
            }

            // For other command verbs (like follow, attack, summon, cast, formation, strategy):
            // If remainder has only 1 single argument (e.g. "follow bob", "attack skull"), treat as command.
            // If it has multiple words (e.g. "follow me to the castle on the hill"), treat as conversation.
            size_t wordCount = 0;
            bool inWord = false;
            for (size_t i = start; i < remainder.size(); ++i)
            {
                if (!std::isspace(static_cast<unsigned char>(remainder[i])))
                {
                    if (!inWord)
                    {
                        inWord = true;
                        ++wordCount;
                    }
                }
                else
                {
                    inWord = false;
                }
            }

            return wordCount <= 1;
        }

        // Comprehensive master directory of all Warband / Playerbots commands,
        // strategy prefixes, Action "d" engine actions, stances, formations,
        // RTI targets, cheats, and single-letter whisper commands from warband_command_guide.html.
        static char const* const kHardcodedIgnoredCommands[] = {
            // Special command prefixes and punctuation symbols
            ".",
            "/",
            "!",
            "#",
            "$",
            "~",
            "@",

            // Single-letter commands (Warband / Playerbots whisper and action shortcuts)
            "c",         // Section 2 ID 64 / Section 7: List bags / inventory
            "e",         // Section 2 ID 66 / Section 7: Equip gear item link
            "u",         // Section 2 ID 68 / Section 7: Use item link
            "s",         // Section 2 ID 67 / Section 7: Sell item link
            "b",         // Section 7: Buy item link

            // Two-letter and short command prefixes
            "ue",        // Section 7: Unequip slot
            "co",        // Section 1: Combat strategy toggle / query (co +, co -, co ~, co ?)
            "nc",        // Section 1: Non-combat strategy toggle / query (nc +, nc -, nc ~, nc ?)
            "de",        // Section 1: Dead strategy toggle / query (de +, de ?)
            "ll",        // Section 2 ID 128-135: Loot strategy filters (ll ~equip, ll ?)
            "ss",        // Section 2 ID 152: Soulstone status query (ss ?)
            "r ?",       // Section 2 ID 155: Instance reset status query

            // Multi-letter strategy, cheat, and management prefixes
            "react",     // Section 1: Reaction strategy
            "cheat",     // Section 6: Bot cheats (cheat +taxi, cheat +repair, cheat ?)
            "save mana", // Section 2 ID 136-141: Mana conservation (save mana 1..5, save mana ?)
            "save ai",   // Section 7: Save AI profile
            "load ai",   // Section 7: Load AI profile
            "reset ai",  // Section 1 / Section 2 ID 9, 10: Reset AI defaults
            "reset strats", // Section 1: Reset strategies
            "reset",     // Section 2 ID 85: Reset state

            // Action "d" Engine commands (Section 2 Groups 1, 2, 3, 4, 7, 11)
            "d attack my target",
            "d pull my target",
            "d stop follow",
            "d buff",
            "d food",
            "d drink",
            "d add all loot",
            "d equip upgrades",
            "d repair",
            "d sell",
            "d revive targets",
            "d give leader",
            "d use go",

            // Combat & Targeting commands (Section 2 Group 1, Section 4)
            "attack",
            "attack rti",
            "pull rti",
            "pull",
            "pull back",
            "combatstop",
            "flee",
            "tank assist",
            "dps assist",
            "dps aoe",
            "threat",
            "kite",
            "behind",
            "avoid mobs",
            "avoid aoe",
            "focus heal targets",
            "focus rti targets",
            "mark rti",
            "wait for attack",
            "passive",
            "potions",
            "racials",
            "conserve mana",

            // Raid Target Icons (RTI) (Section 2 Group 1)
            "rti",
            "rti skull",
            "rti cross",
            "rti square",
            "rti moon",
            "rti triangle",
            "rti diamond",
            "rti circle",
            "rti star",
            "rti none",
            "rti cc",

            // Movement, Stances & Guard Posts (Section 2 Group 2)
            "follow",
            "stay",
            "position guard set",
            "guard",
            "free",
            "stance",
            "stance near",
            "stance tank",
            "stance turnback",
            "stance behind",
            "stance spread",
            "stance show",

            // Support, Upkeep, Food & Autonomous RPG (Section 2 Group 3)
            "ready",
            "grind",
            "release",
            "revive",

            // Bag, Inventory & Equipment management (Section 2 Group 4, Section 7)
            "destroy",
            "keep",
            "repair",
            "trade",
            "spells",
            "cast",
            "who",
            "where",
            "home",

            // Information, Training & Status (Section 2 Groups 5, 6)
            "stats",
            "quests",
            "quest",
            "talents",
            "trainer",
            "trainer learn",

            // Party Management, Server Spawning, Formations & Utilities (Section 2 Groups 7-11)
            ".bot",
            "summon",
            "uninviteunit",
            "strategy",
            "formation",
            "online",
            "roles",
            "mail",
            "accept",
            "talk",
            "friends",

            // Addon Slash Commands (Section 8)
            "/wb",
            "/warband",
            "/ubc",

            // Encounter & Dungeon Strategies (Section 5)
            "dungeon",
            "onyxia",
            "onyxia's lair",
            "molten core",
            "magmadar",
            "blackwing lair",
            "karazhan",
            "naxxramas",
            "four horseman",

            // Third-party / addon prefixes
            "autogear",
            "teleport",
            "addon",
            "dbm",
            "recount",
            "questie"
        };
    }

    bool IsCommandIgnored(std::string const& message,
                          std::vector<std::string> const& customBlacklist)
    {
        std::string const text = Trim(message);
        if (text.empty())
            return false;

        std::string const stripped = StripAddressingPrefix(text);

        // 1. Check hardcoded Warband / Playerbots commands
        for (char const* hardcoded : kHardcodedIgnoredCommands)
        {
            if (MatchToken(text, hardcoded))
                return true;
            if (stripped != text && MatchToken(stripped, hardcoded))
                return true;
        }

        // 2. Check custom tokens from configuration (AzerothVoices.CommandBlacklist / CommandIgnoreList)
        for (std::string const& customToken : customBlacklist)
        {
            std::string const token = Trim(customToken);
            if (token.empty())
                continue;
            if (MatchToken(text, token))
                return true;
            if (stripped != text && MatchToken(stripped, token))
                return true;
        }

        return false;
    }

    char const* GeneralSubjectTypeName(GeneralSubjectType type)
    {
        switch (type)
        {
            case GeneralSubjectType::Plain: return "plain";
            case GeneralSubjectType::NpcGossip: return "npc";
            case GeneralSubjectType::BotGossip: return "bot";
            case GeneralSubjectType::Quest: return "quest";
            case GeneralSubjectType::Loot: return "loot";
            case GeneralSubjectType::Trade: return "trade";
            case GeneralSubjectType::Spell: return "spell";
        }
        return "plain";
    }

    GeneralSubjectType SelectGeneralSubjectType(uint32_t npcGossipChance,
                                               uint32_t botGossipChance,
                                               uint32_t roll100)
    {
        if (roll100 <= npcGossipChance)
            return GeneralSubjectType::NpcGossip;
        if (roll100 <= npcGossipChance + botGossipChance)
            return GeneralSubjectType::BotGossip;
        return GeneralSubjectType::Plain;
    }

    bool IsVanillaCapitalCityZone(uint32_t zoneId)
    {
        switch (zoneId)
        {
            case 1519: // Stormwind City
            case 1537: // Ironforge
            case 1657: // Darnassus
            case 1637: // Orgrimmar
            case 1638: // Thunder Bluff
            case 1497: // Undercity
            case 3524: // Alah'Thaliel
            case 3525: // Gilneas City
                return true;
            default:
                return false;
        }
    }

    uint32_t CalculateGeneralTriggerChance(uint32_t baseChance, uint32_t cityMultiplier, bool isCity)
    {
        if (!isCity || cityMultiplier <= 1)
            return std::min<uint32_t>(100, baseChance);
        return std::min<uint32_t>(100, baseChance * cityMultiplier);
    }

    bool GeneralSpeakerTracker::IsOnCooldown(uint64_t guid, uint32_t nowSeconds, uint32_t cooldownSeconds) const
    {
        if (!cooldownSeconds)
            return false;
        auto it = m_lastSpokeAt.find(guid);
        if (it == m_lastSpokeAt.end())
            return false;
        return (nowSeconds >= it->second) && ((nowSeconds - it->second) < cooldownSeconds);
    }

    void GeneralSpeakerTracker::RecordSpeech(uint64_t guid, uint32_t nowSeconds)
    {
        m_lastSpokeAt[guid] = nowSeconds;
    }

    void GeneralSpeakerTracker::Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds)
    {
        if (!maxTtlSeconds)
            return;
        for (auto it = m_lastSpokeAt.begin(); it != m_lastSpokeAt.end(); )
        {
            if (nowSeconds < it->second || (nowSeconds - it->second) >= maxTtlSeconds)
                it = m_lastSpokeAt.erase(it);
            else
                ++it;
        }
    }

    void GeneralSpeakerTracker::Clear()
    {
        m_lastSpokeAt.clear();
    }

    size_t GeneralSpeakerTracker::Size() const
    {
        return m_lastSpokeAt.size();
    }

    bool GossipTargetTracker::IsOnCooldown(std::string const& key, uint32_t nowSeconds, uint32_t cooldownSeconds) const
    {
        if (!cooldownSeconds || key.empty())
            return false;
        auto it = m_lastTargetedAt.find(key);
        if (it == m_lastTargetedAt.end())
            return false;
        return (nowSeconds >= it->second) && ((nowSeconds - it->second) < cooldownSeconds);
    }

    void GossipTargetTracker::RecordTarget(std::string const& key, uint32_t nowSeconds)
    {
        if (!key.empty())
            m_lastTargetedAt[key] = nowSeconds;
    }

    void GossipTargetTracker::Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds)
    {
        if (!maxTtlSeconds)
            return;
        for (auto it = m_lastTargetedAt.begin(); it != m_lastTargetedAt.end(); )
        {
            if (nowSeconds < it->second || (nowSeconds - it->second) >= maxTtlSeconds)
                it = m_lastTargetedAt.erase(it);
            else
                ++it;
        }
    }

    void GossipTargetTracker::Clear()
    {
        m_lastTargetedAt.clear();
    }

    size_t GossipTargetTracker::Size() const
    {
        return m_lastTargetedAt.size();
    }

    char const* TargetedNpcSayActionName(TargetedNpcSayAction action)
    {
        switch (action)
        {
            case TargetedNpcSayAction::NormalSay: return "normal";
            case TargetedNpcSayAction::NpcOnly: return "npc_only";
            case TargetedNpcSayAction::NpcWithObserver: return "npc_with_observer";
            case TargetedNpcSayAction::ExplicitBotDirectReply: return "explicit_bot_direct";
        }
        return "normal";
    }

    TargetedNpcSayAction DecideTargetedNpcSayAction(
        bool senderIsRealPlayer,
        bool selectedTargetIsNpc,
        bool selectedTargetIsPlayerBot,
        bool explicitPlayerBotNameMention,
        bool botCommentsEnabled,
        uint32_t botCommentChance,
        uint32_t maxBotComments,
        uint32_t roll100)
    {
        if (!senderIsRealPlayer)
            return TargetedNpcSayAction::NormalSay;

        // PlayerBot target remains separate (Test G)
        if (selectedTargetIsPlayerBot)
            return TargetedNpcSayAction::NormalSay;

        // No NPC targeted -> normal Say handling (Test E)
        if (!selectedTargetIsNpc)
            return TargetedNpcSayAction::NormalSay;

        // Targeted NPC + explicit PlayerBot name mention -> explicitly named bot responds directly (Test F)
        if (explicitPlayerBotNameMention)
            return TargetedNpcSayAction::ExplicitBotDirectReply;

        // Targeted NPC + ordinary message -> generic direct-Say responses are suppressed (Test A).
        // Check if an observer comment is triggered.
        if (botCommentsEnabled && maxBotComments > 0 && roll100 <= botCommentChance)
            return TargetedNpcSayAction::NpcWithObserver;

        return TargetedNpcSayAction::NpcOnly;
    }

    TargetedNpcObserverPrompt BuildTargetedNpcObserverPrompt(TargetedNpcObserverPromptInput const& input)
    {
        TargetedNpcObserverPrompt prompt;
        std::string npcDesc = input.npcName.empty() ? "a nearby NPC" : input.npcName;
        if (!input.npcRole.empty())
            npcDesc += " (" + input.npcRole + ")";

        std::string const location = input.zoneOrArea.empty() ? "the area" : input.zoneOrArea;

        prompt.systemPromptExtension = "You overheard the real player " + input.playerName +
            " speaking to the NPC " + npcDesc + " in " + location + ". " +
            "You are " + (input.botName.empty() ? "a nearby adventurer" : input.botName) +
            ", a nearby PlayerBot who overheard this conversation. " +
            "Do not answer the player's question as though they addressed you, and do not pretend to be " +
            (input.npcName.empty() ? "the NPC" : input.npcName) + ". " +
            "Instead, make one short, natural comment or reaction about the conversation you overheard.";

        prompt.userPrompt = "The player " + input.playerName + " said to NPC " + npcDesc +
            ": \"" + input.playerMessage + "\"\n" +
            "You overheard this exchange. Do not answer " + input.playerName +
            "'s question as though they addressed you. " +
            "Make one short, natural comment or reaction about what you overheard.";

        return prompt;
    }

    NpcReplyObserverPrompt BuildNpcReplyObserverPrompt(NpcReplyObserverPromptInput const& input)
    {
        NpcReplyObserverPrompt prompt;
        std::string npcDesc = input.npcName.empty() ? "a nearby NPC" : input.npcName;
        if (!input.npcRole.empty())
            npcDesc += " (" + input.npcRole + ")";

        std::string const location = input.zoneOrArea.empty() ? "the area" : input.zoneOrArea;

        prompt.systemPromptExtension = "You are " + (input.botName.empty() ? "a nearby adventurer" : input.botName) +
            ", an adventurer in " + location + ". You just overheard the NPC " + npcDesc +
            " reply to player " + input.playerName + " with: \"" + input.npcReply + "\". " +
            "React naturally in character to what the NPC said or comment on the exchange in one short, natural sentence. " +
            "Do not answer for the player, do not pretend to be " + (input.npcName.empty() ? "the NPC" : input.npcName) +
            ", and do not repeat verbatim what was already said.";

        prompt.userPrompt = "NPC " + npcDesc + " answered " + input.playerName +
            " (who asked: \"" + input.playerMessage + "\") with: \"" + input.npcReply + "\"\n" +
            "React naturally in character to the NPC's answer in one short sentence.";

        return prompt;
    }

    std::vector<ObserverCandidate> SelectObserverCandidates(
        std::vector<ObserverCandidate> const& eligibleBots,
        uint32_t maxBotComments)
    {
        if (eligibleBots.empty() || !maxBotComments)
            return {};

        std::vector<ObserverCandidate> sorted = eligibleBots;
        std::sort(sorted.begin(), sorted.end(), [](ObserverCandidate const& a, ObserverCandidate const& b) {
            return a.distance < b.distance;
        });

        if (sorted.size() > maxBotComments)
            sorted.resize(maxBotComments);

        return sorted;
    }

    void GuildSessionHistoryRing::AddTurn(std::string const& playerMsg, std::string const& botMsg, uint32_t nowSeconds)
    {
        if (playerMsg.empty() && botMsg.empty())
            return;
        m_turns.push_back({ playerMsg, botMsg, nowSeconds });
        if (m_turns.size() > MaxTurns)
            m_turns.pop_front();
    }

    std::string GuildSessionHistoryRing::FindRelevantCallback(std::string const& currentMessage) const
    {
        if (m_turns.empty())
            return "";

        // Search previous turns backwards, finding a turn that is meaningful (> 5 chars)
        // and not identical to the current incoming message.
        for (auto it = m_turns.rbegin(); it != m_turns.rend(); ++it)
        {
            if (it->playerMessage.size() > 5 && it->playerMessage != currentMessage)
                return it->playerMessage;
        }
        return "";
    }

    uint32_t CalculateGuildBotWeight(bool explicitlyNamed, bool spokeRecently, uint32_t penaltyPercent)
    {
        if (explicitlyNamed)
            return 200; // Priority selection & penalty bypass
        if (spokeRecently)
        {
            uint32_t const penalty = std::min(100u, penaltyPercent);
            return 100 - penalty;
        }
        return 100;
    }

    std::vector<GuildReplyCandidate> SelectWeightedGuildCandidates(
        std::vector<GuildReplyCandidate> candidates,
        uint32_t maxCandidates)
    {
        if (candidates.empty() || !maxCandidates)
            return {};

        // Stable sort: explicitly named first, then descending weight
        std::stable_sort(candidates.begin(), candidates.end(),
            [](GuildReplyCandidate const& a, GuildReplyCandidate const& b) {
                if (a.explicitlyNamed != b.explicitlyNamed)
                    return a.explicitlyNamed > b.explicitlyNamed;
                return a.weight > b.weight;
            });

        if (candidates.size() > maxCandidates)
            candidates.resize(maxCandidates);

        return candidates;
    }

    char const* GuildReplyModeName(GuildReplyMode mode)
    {
        switch (mode)
        {
            case GuildReplyMode::Single: return "single";
            case GuildReplyMode::MultiReply: return "multi_reply";
            case GuildReplyMode::Conversation: return "conversation";
        }
        return "single";
    }

    GuildReplyMode DecideGuildReplyMode(
        uint32_t eligibleBotCount,
        bool conversationEnabled,
        uint32_t conversationChance,
        uint32_t multiReplyChance,
        uint32_t multiAddressedBonus,
        bool multipleBotsNamed,
        uint32_t rollConversation,
        uint32_t rollMultiReply)
    {
        if (eligibleBotCount < 2)
            return GuildReplyMode::Single;

        // Section 7: Evaluate ConversationChance first
        if (conversationEnabled && rollConversation <= conversationChance)
            return GuildReplyMode::Conversation;

        // Evaluate MultiReplyChance with multi-address bonus
        uint32_t effectiveMulti = multiReplyChance;
        if (multipleBotsNamed)
            effectiveMulti = std::min(100u, effectiveMulti + multiAddressedBonus);

        if (rollMultiReply <= effectiveMulti)
            return GuildReplyMode::MultiReply;

        return GuildReplyMode::Single;
    }

    char const* LoginGreetingBandName(LoginGreetingBand band)
    {
        switch (band)
        {
            case LoginGreetingBand::Quick: return "quick";
            case LoginGreetingBand::Normal: return "normal";
            case LoginGreetingBand::Busy: return "busy";
        }
        return "normal";
    }

    LoginGreetingBand SelectLoginGreetingBand(
        uint32_t quickChance,
        uint32_t busyChance,
        uint32_t roll100)
    {
        uint32_t const clampedBusy = (quickChance + busyChance > 100) ? (100 - quickChance) : busyChance;

        if (roll100 <= quickChance)
            return LoginGreetingBand::Quick;
        if (roll100 <= quickChance + clampedBusy)
            return LoginGreetingBand::Busy;
        return LoginGreetingBand::Normal;
    }

    uint32_t PickLoginGreetingDelaySeconds(
        LoginGreetingBand band,
        uint32_t randomRangeVal)
    {
        switch (band)
        {
            case LoginGreetingBand::Quick:
                // 2-5 seconds (span of 4: 2, 3, 4, 5)
                return 2 + (randomRangeVal % 4);
            case LoginGreetingBand::Busy:
                // 25-45 seconds (span of 21: 25..45)
                return 25 + (randomRangeVal % 21);
            case LoginGreetingBand::Normal:
            default:
                // 8-20 seconds (span of 13: 8..20)
                return 8 + (randomRangeVal % 13);
        }
    }

    std::string StripSpeakerPrefix(std::string value, std::string const& speakerName)
    {
        auto trimStr = [](std::string const& s) -> std::string {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;
            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;
            return s.substr(start, end - start);
        };

        auto unwrapQuotes = [&trimStr](std::string s) -> std::string {
            s = trimStr(s);
            if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                                  (s.front() == '\'' && s.back() == '\'')))
            {
                s = trimStr(s.substr(1, s.size() - 2));
            }
            return s;
        };

        auto toLowerChar = [](char c) -> char {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };

        auto startsWithCaseInsensitive = [&toLowerChar](std::string const& str, size_t pos, std::string const& prefix) -> bool {
            if (pos + prefix.size() > str.size())
                return false;
            for (size_t i = 0; i < prefix.size(); ++i)
            {
                if (toLowerChar(str[pos + i]) != toLowerChar(prefix[i]))
                    return false;
            }
            return true;
        };

        value = trimStr(value);
        bool strippedAny = false;

        // Loop up to 3 times to strip repeated/nested prefixes (e.g. "[Runebane] whispers: Runebane: Hi")
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            value = trimStr(value);
            if (value.empty())
                break;

            value = unwrapQuotes(value);
            if (value.empty())
                break;

            bool matched = false;

            // 1. Try matching with known speakerName if provided
            std::string const trimmedSpeaker = trimStr(speakerName);
            if (!trimmedSpeaker.empty())
            {
                size_t pos = 0;
                char closeBracket = 0;
                if (pos < value.size())
                {
                    if (value[pos] == '[') closeBracket = ']';
                    else if (value[pos] == '<') closeBracket = '>';
                    else if (value[pos] == '(') closeBracket = ')';
                }

                if (closeBracket != 0)
                    ++pos;

                if (startsWithCaseInsensitive(value, pos, trimmedSpeaker))
                {
                    pos += trimmedSpeaker.size();
                    if (closeBracket != 0 && pos < value.size() && value[pos] == closeBracket)
                        ++pos;

                    // Skip spaces
                    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])))
                        ++pos;

                    // Check for optional dialogue verbs
                    static const char* const verbs[] = {
                        "whispers", "whisper", "says", "say", "yells", "yell",
                        "asks", "ask", "replies", "reply", "tells you", "tells",
                        "exclaims", "shouts", nullptr
                    };
                    for (size_t v = 0; verbs[v] != nullptr; ++v)
                    {
                        std::string verb = verbs[v];
                        if (startsWithCaseInsensitive(value, pos, verb))
                        {
                            pos += verb.size();
                            while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])))
                                ++pos;
                            break;
                        }
                    }

                    // Check for separator ':', '-', '--', '—'
                    bool hadSeparator = false;
                    if (pos < value.size() && (value[pos] == ':' || value[pos] == '-'))
                    {
                        hadSeparator = true;
                        ++pos;
                        if (pos < value.size() && value[pos] == '-')
                            ++pos;
                    }
                    else if (pos + 2 < value.size() &&
                             static_cast<unsigned char>(value[pos]) == 0xe2 &&
                             static_cast<unsigned char>(value[pos + 1]) == 0x80 &&
                             (static_cast<unsigned char>(value[pos + 2]) == 0x94 || static_cast<unsigned char>(value[pos + 2]) == 0x93))
                    {
                        // UTF-8 em-dash or en-dash
                        hadSeparator = true;
                        pos += 3;
                    }

                    if (hadSeparator || closeBracket != 0 || pos > trimmedSpeaker.size())
                    {
                        value = trimStr(value.substr(pos));
                        matched = true;
                        strippedAny = true;
                        continue;
                    }
                }
            }

            // 2. Generic dialogue verb pattern: "[Name] whispers: ", "Name says: ", etc.
            static const char* const genericVerbSuffixes[] = {
                " whispers:", " whisper:", " says:", " say:", " yells:", " yell:",
                " asks:", " replies:", " tells you:", nullptr
            };
            for (size_t g = 0; genericVerbSuffixes[g] != nullptr; ++g)
            {
                std::string gv = genericVerbSuffixes[g];
                for (size_t i = 0; i <= 35 && i + gv.size() <= value.size(); ++i)
                {
                    if (startsWithCaseInsensitive(value, i, gv))
                    {
                        size_t cut = i + gv.size();
                        value = trimStr(value.substr(cut));
                        matched = true;
                        strippedAny = true;
                        break;
                    }
                }
                if (matched)
                    break;
            }
            if (matched)
                break;

            // 3. Generic bracketed prefix: "^\[[A-Za-z0-9' _\-]{1,24}\]\s*:\s*"
            if (!value.empty() && value.front() == '[')
            {
                size_t close = value.find(']');
                if (close != std::string::npos && close >= 2 && close <= 30)
                {
                    size_t pos = close + 1;
                    while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])))
                        ++pos;
                    if (pos < value.size() && value[pos] == ':')
                    {
                        ++pos;
                        value = trimStr(value.substr(pos));
                        matched = true;
                        strippedAny = true;
                        continue;
                    }
                }
            }

            // 4. Generic label prefix: "NPC:", "Bot:", "Playerbot:", "Character:"
            static const char* const genericLabels[] = {
                "npc:", "bot:", "playerbot:", "character:", "response:", "dialogue:", nullptr
            };
            for (size_t l = 0; genericLabels[l] != nullptr; ++l)
            {
                std::string label = genericLabels[l];
                if (startsWithCaseInsensitive(value, 0, label))
                {
                    value = trimStr(value.substr(label.size()));
                    matched = true;
                    strippedAny = true;
                    break;
                }
            }
            if (matched)
                continue;

            // 5. If an outer prefix was already stripped (e.g. "[Runebane] whispers: "),
            // any immediate "Name: " is a nested duplicate speaker prefix.
            if (strippedAny)
            {
                size_t colon = value.find(':');
                if (colon != std::string::npos && colon >= 1 && colon <= 24)
                {
                    bool validName = true;
                    for (size_t i = 0; i < colon; ++i)
                    {
                        unsigned char c = static_cast<unsigned char>(value[i]);
                        if (!std::isalnum(c) && c != ' ' && c != '\'' && c != '_' && c != '-')
                        {
                            validName = false;
                            break;
                        }
                    }
                    if (validName)
                    {
                        value = trimStr(value.substr(colon + 1));
                        matched = true;
                        continue;
                    }
                }
            }

            break;
        }

        if (strippedAny)
            value = unwrapQuotes(value);

        return value;
    }

    namespace
    {
        bool IsSentenceAbbreviation(std::string const& word)
        {
            if (word.empty())
                return false;

            std::string lower;
            lower.reserve(word.size());
            for (char c : word)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            static const char* const abbreviations[] = {
                "etc", "eg", "e.g", "ie", "i.e", "vs", "mr", "mrs", "ms", "dr", "prof",
                "st", "co", "inc", "ltd", "gen", "col", "capt", "sgt", "cpl", "pvt",
                "approx", "dept", "min", "max", "sec", "lvl", "no", "nr", "rep", "sen",
                nullptr
            };

            for (size_t i = 0; abbreviations[i] != nullptr; ++i)
            {
                if (lower == abbreviations[i])
                    return true;
            }
            return false;
        }

        std::string CleanDialogueText(std::string value)
        {
            auto replaceSub = [](std::string& str, std::string const& from, std::string const& to) {
                if (from.empty()) return;
                size_t pos = 0;
                while ((pos = str.find(from, pos)) != std::string::npos)
                {
                    str.replace(pos, from.size(), to);
                    pos += to.size();
                }
            };

            replaceSub(value, "```json", "");
            replaceSub(value, "```", "");
            replaceSub(value, "\r", "");
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return c < 0x20 && c != '\n' && c != '\t';
            }), value.end());

            auto trimStr = [](std::string const& s) -> std::string {
                size_t start = 0;
                while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                    ++start;
                size_t end = s.size();
                while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                    --end;
                return s.substr(start, end - start);
            };

            value = trimStr(value);
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                      (value.front() == '\'' && value.back() == '\'')))
            {
                value = trimStr(value.substr(1, value.size() - 2));
            }
            return value;
        }

        std::vector<std::string> SplitSingleSentence(std::string const& s, size_t limit)
        {
            auto trimStr = [](std::string const& str) -> std::string {
                size_t start = 0;
                while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start])))
                    ++start;
                size_t end = str.size();
                while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1])))
                    --end;
                return str.substr(start, end - start);
            };

            std::vector<std::string> chunks;
            std::string rem = trimStr(s);

            while (rem.size() > limit)
            {
                size_t const minBound = std::max<size_t>(10, limit / 3);
                size_t bestSplit = std::string::npos;

                // 1. Look for clause punctuation (, ; : -) followed by space
                for (size_t p = limit; p >= minBound; --p)
                {
                    char c = rem[p - 1];
                    char nextC = (p < rem.size()) ? rem[p] : ' ';

                    if ((c == ',' || c == ';' || c == ':' || c == '-') &&
                        std::isspace(static_cast<unsigned char>(nextC)))
                    {
                        bestSplit = p;
                        break;
                    }
                }

                // 2. If no clause punctuation, look for word boundary (space)
                if (bestSplit == std::string::npos)
                {
                    size_t space = rem.rfind(' ', limit);
                    if (space != std::string::npos && space >= minBound / 2)
                        bestSplit = space;
                }

                // 3. Fallback: hard break at limit
                if (bestSplit == std::string::npos || bestSplit == 0)
                    bestSplit = limit;

                std::string chunk = trimStr(rem.substr(0, bestSplit));
                if (!chunk.empty())
                    chunks.push_back(chunk);

                rem = trimStr(rem.substr(bestSplit));
            }

            if (!rem.empty())
                chunks.push_back(rem);

            return chunks;
        }
    }

    std::vector<std::string> SplitIntoSentences(std::string const& text)
    {
        auto trimStr = [](std::string const& s) -> std::string {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;
            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;
            return s.substr(start, end - start);
        };

        std::vector<std::string> sentences;
        if (text.empty())
            return sentences;

        size_t start = 0;
        size_t const n = text.size();

        for (size_t i = 0; i < n; ++i)
        {
            char c = text[i];
            if (c != '.' && c != '!' && c != '?' && c != ';')
                continue;

            // Advance through repeated punctuation (e.g. "...", "!!", "!?", "?!")
            size_t endPunct = i;
            while (endPunct + 1 < n && (text[endPunct + 1] == '.' || text[endPunct + 1] == '!' ||
                                        text[endPunct + 1] == '?' || text[endPunct + 1] == '~'))
            {
                ++endPunct;
            }

            // Include any closing quotes, brackets or parentheses
            size_t endQuote = endPunct + 1;
            while (endQuote < n && (text[endQuote] == '"' || text[endQuote] == '\'' ||
                                    text[endQuote] == ')' || text[endQuote] == ']' ||
                                    text[endQuote] == '>' || text[endQuote] == '}' ||
                                    static_cast<unsigned char>(text[endQuote]) == 0x9d ||
                                    static_cast<unsigned char>(text[endQuote]) == 0x99))
            {
                ++endQuote;
            }

            // If we reached the end of string, this is the final sentence
            if (endQuote >= n)
            {
                std::string s = trimStr(text.substr(start));
                if (!s.empty())
                    sentences.push_back(s);
                start = n;
                break;
            }

            // Must be followed by whitespace or newline to be a sentence break
            if (!std::isspace(static_cast<unsigned char>(text[endQuote])))
            {
                // Not followed by whitespace (e.g., 3.14, domain.com, etc.)
                i = endPunct;
                continue;
            }

            // Find next non-whitespace character
            size_t nextCharPos = endQuote;
            while (nextCharPos < n && std::isspace(static_cast<unsigned char>(text[nextCharPos])))
                ++nextCharPos;

            if (nextCharPos >= n)
            {
                // Trailing whitespace at end of string
                std::string s = trimStr(text.substr(start));
                if (!s.empty())
                    sentences.push_back(s);
                start = n;
                break;
            }

            // For period '.', check abbreviation, initial, numbers, and lowercase follow-up:
            if (c == '.' && endPunct == i)
            {
                // Find previous word token before the period:
                size_t wordEnd = i;
                size_t wordStart = i;
                while (wordStart > 0 && (std::isalnum(static_cast<unsigned char>(text[wordStart - 1])) ||
                                         text[wordStart - 1] == '.' || text[wordStart - 1] == '-'))
                {
                    --wordStart;
                }
                std::string const prevWord = text.substr(wordStart, wordEnd - wordStart);

                // 1. Single-letter initial/acronym (e.g. "U. S.", "John D. Rockefeller"):
                if (prevWord.size() == 1 && std::isalpha(static_cast<unsigned char>(prevWord[0])))
                {
                    i = endPunct;
                    continue;
                }

                // 2. All digits (e.g. "1. First" or "42. Item"):
                bool allDigits = !prevWord.empty();
                for (char pc : prevWord)
                {
                    if (!std::isdigit(static_cast<unsigned char>(pc)))
                    {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits)
                {
                    i = endPunct;
                    continue;
                }

                // 3. Known abbreviation (e.g. "etc.", "e.g.", "dr.", "lvl."):
                if (IsSentenceAbbreviation(prevWord))
                {
                    i = endPunct;
                    continue;
                }

                // 4. Followed by lowercase ASCII letter (e.g. "etc. in a convenient..."):
                if (std::islower(static_cast<unsigned char>(text[nextCharPos])))
                {
                    i = endPunct;
                    continue;
                }
            }

            // Genuine sentence boundary:
            std::string s = trimStr(text.substr(start, endQuote - start));
            if (!s.empty())
                sentences.push_back(s);
            start = nextCharPos;
            i = nextCharPos - 1;
        }

        if (start < n)
        {
            std::string s = trimStr(text.substr(start));
            if (!s.empty())
                sentences.push_back(s);
        }

        return sentences;
    }

    std::vector<std::string> SplitReplyLinesStructured(
        std::string const& text,
        std::string const& speakerName,
        size_t maximumCharacters,
        size_t maximumLines,
        bool sentenceAware)
    {
        auto trimStr = [](std::string const& s) -> std::string {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;
            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;
            return s.substr(start, end - start);
        };

        if (maximumLines == 0 || maximumCharacters == 0)
            return {};

        std::string cleaned = StripSpeakerPrefix(text, speakerName);
        cleaned = CleanDialogueText(cleaned);
        cleaned = StripSpeakerPrefix(cleaned, speakerName);
        if (cleaned.empty())
            return {};

        size_t const totalBudget = maximumCharacters;
        size_t const maxLinePacketLimit = std::min<size_t>(totalBudget, 240);

        if (!sentenceAware)
        {
            // Legacy greedy space-splitting behavior bounded by totalBudget
            std::stringstream stream(cleaned);
            std::string line;
            std::vector<std::string> rawLines;
            while (std::getline(stream, line))
                rawLines.push_back(line);

            std::vector<std::string> result;
            size_t totalCharacters = 0;
            for (std::string l : rawLines)
            {
                l = StripSpeakerPrefix(l, speakerName);
                while (!l.empty() && result.size() < maximumLines && totalCharacters < totalBudget)
                {
                    size_t const remaining = totalBudget - totalCharacters;
                    size_t const chunkLimit = std::min<size_t>(remaining, maxLinePacketLimit);
                    if (chunkLimit == 0)
                        break;

                    if (l.size() <= chunkLimit)
                    {
                        totalCharacters += l.size();
                        result.push_back(l);
                        break;
                    }
                    size_t split = l.rfind(' ', chunkLimit);
                    if (split == std::string::npos || split < chunkLimit / 2)
                        split = chunkLimit;
                    std::string chunk = trimStr(l.substr(0, split));
                    if (!chunk.empty())
                    {
                        totalCharacters += chunk.size();
                        result.push_back(chunk);
                    }
                    l = trimStr(l.substr(split));
                    l = StripSpeakerPrefix(l, speakerName);
                }
                if (result.size() >= maximumLines || totalCharacters >= totalBudget)
                    break;
            }
            return result;
        }

        // Sentence-aware structured splitting respecting cumulative total budget
        std::stringstream stream(cleaned);
        std::string paragraph;
        std::vector<std::string> paragraphs;
        while (std::getline(stream, paragraph))
        {
            paragraph = trimStr(paragraph);
            if (!paragraph.empty())
                paragraphs.push_back(paragraph);
        }

        std::vector<std::string> result;
        size_t totalCharacters = 0;
        std::string currentLine;

        for (std::string const& para : paragraphs)
        {
            if (result.size() >= maximumLines || totalCharacters >= totalBudget)
                break;

            std::vector<std::string> sentences = SplitIntoSentences(para);
            for (std::string const& s : sentences)
            {
                if (result.size() >= maximumLines || totalCharacters >= totalBudget)
                    break;

                if (s.empty())
                    continue;

                if (currentLine.empty())
                {
                    size_t const remaining = (totalBudget > totalCharacters) ? (totalBudget - totalCharacters) : 0;
                    if (remaining == 0)
                        break;

                    size_t const lineLimit = std::min<size_t>(remaining, maxLinePacketLimit);
                    if (s.size() <= lineLimit)
                    {
                        currentLine = s;
                    }
                    else if (result.empty())
                    {
                        // First line must fit what it can into the budget
                        std::vector<std::string> chunks = SplitSingleSentence(s, lineLimit);
                        for (size_t k = 0; k < chunks.size(); ++k)
                        {
                            if (k + 1 < chunks.size())
                            {
                                if (result.size() < maximumLines && totalCharacters + chunks[k].size() <= totalBudget)
                                {
                                    totalCharacters += chunks[k].size();
                                    result.push_back(chunks[k]);
                                    if (result.size() >= maximumLines || totalCharacters >= totalBudget)
                                        break;
                                }
                            }
                            else
                            {
                                if (totalCharacters + chunks[k].size() <= totalBudget)
                                    currentLine = chunks[k];
                            }
                        }
                    }
                    else
                    {
                        // Clean stop at sentence boundary if previous complete lines exist
                        break;
                    }
                }
                else
                {
                    size_t const committed = totalCharacters + currentLine.size();
                    size_t const remaining = (totalBudget > committed) ? (totalBudget - committed) : 0;

                    // Check if appending s fits in line limit and remaining budget
                    if (1 + s.size() <= remaining && currentLine.size() + 1 + s.size() <= maxLinePacketLimit)
                    {
                        currentLine += " " + s;
                    }
                    else
                    {
                        // Current line ends cleanly at sentence boundary!
                        result.push_back(currentLine);
                        totalCharacters += currentLine.size();
                        currentLine.clear();

                        if (result.size() >= maximumLines || totalCharacters >= totalBudget)
                            break;

                        size_t const newRemaining = (totalBudget > totalCharacters) ? (totalBudget - totalCharacters) : 0;
                        if (newRemaining == 0)
                            break;

                        size_t const newLineLimit = std::min<size_t>(newRemaining, maxLinePacketLimit);
                        if (s.size() <= newLineLimit)
                        {
                            currentLine = s;
                        }
                        else
                        {
                            // Clean stop at complete sentence boundary already committed
                            break;
                        }
                    }
                }
            }

            // Paragraph boundary: flush currentLine
            if (!currentLine.empty())
            {
                result.push_back(currentLine);
                totalCharacters += currentLine.size();
                currentLine.clear();
            }
        }

        if (!currentLine.empty() && result.size() < maximumLines && totalCharacters + currentLine.size() <= totalBudget)
        {
            result.push_back(currentLine);
            totalCharacters += currentLine.size();
            currentLine.clear();
        }

        for (std::string& resLine : result)
            resLine = StripSpeakerPrefix(resLine, speakerName);

        return result;
    }
}

