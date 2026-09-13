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

            // If the token starts with a non-alphanumeric punctuation/symbol (e.g. '.', '/', '!', '#', '$', '?', '~', '@'),
            // any message beginning with that prefix is an ignored command.
            unsigned char const firstTokenChar = static_cast<unsigned char>(token.front());
            if (!std::isalnum(firstTokenChar))
                return true;

            // If token explicitly ends with whitespace (e.g. "! "), matching the prefix already verified the delimiter.
            if (std::isspace(static_cast<unsigned char>(token.back())))
                return true;

            // If exact length match (e.g. "c", "stats", "attack", "follow", "ready")
            if (text.size() == token.size())
                return true;

            // Otherwise, the character right after the token in text must be a delimiter (not alphanumeric).
            // This prevents "c" from blocking "can", "d" from blocking "do", "follow" from blocking "following", etc.
            unsigned char const nextChar = static_cast<unsigned char>(text[token.size()]);
            return !std::isalnum(nextChar);
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
            "?",
            "~",
            "@",

            // Single-letter commands (Warband / Playerbots whisper and action shortcuts)
            "c",         // Section 2 ID 64 / Section 7: List bags / inventory
            "e",         // Section 2 ID 66 / Section 7: Equip gear item link
            "u",         // Section 2 ID 68 / Section 7: Use item link
            "s",         // Section 2 ID 67 / Section 7: Sell item link
            "b",         // Section 7: Buy item link
            "d",         // Action "d" Engine prefix (buff, food, drink, loot, equip, repair, etc.)

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
}
