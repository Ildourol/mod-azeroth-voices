#include "AzerothVoicesManager.h"

#include "AzerothVoicesNaturalCommands.h"
#include "AzerothVoicesPersonality.h"
#include "AzerothVoicesPlayerbotBridge.h"
#include "AzerothVoicesProvider.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <list>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace AzerothVoices
{
    struct Manager::Candidate
    {
        ActorSnapshot actor;
        uint32_t chance = 0;
        int score = 0;
        bool targetedNpcConversation = false;
        bool selectedNpcTarget = false;
    };

    struct Manager::RagItem
    {
        std::string id;
        std::string title;
        std::string category;
        std::string source;
        std::vector<std::string> keywords;
        std::set<std::string> keywordWords;
        std::set<std::string> headingWords;
        std::set<std::string> contentWords;
        std::string text;
    };

    namespace
    {
        using Clock = std::chrono::steady_clock;
        namespace fs = std::filesystem;
        using Json = nlohmann::json;
        constexpr size_t MaximumPersonalityCacheEntries = 2048;

        uint64_t UnixNow()
        {
            return static_cast<uint64_t>(std::time(nullptr));
        }

        std::mt19937& RandomEngine()
        {
            static thread_local std::mt19937 engine(std::random_device{}());
            return engine;
        }

        uint32_t RandomUInt(uint32_t minimum, uint32_t maximum)
        {
            if (maximum <= minimum)
                return minimum;
            return std::uniform_int_distribution<uint32_t>(minimum, maximum)(RandomEngine());
        }

        bool Roll(uint32_t chance)
        {
            return chance >= 100 || (chance > 0 && RandomUInt(1, 100) <= chance);
        }

        bool IsOnlineRealPlayer(Player const* player)
        {
            return player && player->IsInWorld() && player->GetSession() &&
                !Script_IsAIControlled(player);
        }

        Player* FindOnlineRealPlayer()
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (IsOnlineRealPlayer(player))
                    return player;
            }
            return nullptr;
        }

        Player* FindNearbyRealPlayer(WorldObject const* center, float distance)
        {
            if (!center || !center->IsInWorld())
                return nullptr;

            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (!IsOnlineRealPlayer(player) || player == center ||
                    player->GetMapId() != center->GetMapId())
                    continue;
                if (center->IsWithinDist(player, distance, false))
                    return player;
            }
            return nullptr;
        }

        bool HasNearbyRealPlayer(WorldObject const* center, float distance)
        {
            return FindNearbyRealPlayer(center, distance) != nullptr;
        }

        enum class NpcEligibilityResult : uint8_t
        {
            Eligible,
            Invalid,
            Temporary,
            Filtered,
            Neutral,
            Hostile,
            NoHumanNearby
        };

        enum class NpcDisposition : uint8_t
        {
            Friendly,
            Neutral,
            Hostile
        };

        NpcDisposition ClassifyNpcDisposition(Creature const* creature,
                                              WorldObject const* counterpart)
        {
            ReputationRank const reaction = creature && counterpart
                ? creature->GetReactionTo(counterpart) : REP_NEUTRAL;
            if (reaction >= REP_FRIENDLY)
                return NpcDisposition::Friendly;
            if (reaction == REP_NEUTRAL)
                return NpcDisposition::Neutral;
            return NpcDisposition::Hostile;
        }

        std::string NpcDispositionName(NpcDisposition disposition)
        {
            switch (disposition)
            {
                case NpcDisposition::Friendly:
                    return "friendly";
                case NpcDisposition::Neutral:
                    return "neutral";
                case NpcDisposition::Hostile:
                    return "hostile";
            }
            return "neutral";
        }

        uint32_t NpcDispositionReplyChance(NpcDisposition disposition, Config const& config)
        {
            switch (disposition)
            {
                case NpcDisposition::Friendly:
                    return config.npcFriendlyReplyChance;
                case NpcDisposition::Neutral:
                    return config.npcNeutralReplyChance;
                case NpcDisposition::Hostile:
                    return config.npcHostileReplyChance;
            }
            return config.npcNeutralReplyChance;
        }

        ReputationRank NpcPlayableFactionReaction(Creature const* creature)
        {
            FactionTemplateEntry const* creatureFaction = creature
                ? creature->GetFactionTemplateEntry() : nullptr;
            if (!creatureFaction)
                return REP_NEUTRAL;

            bool hostileToPlayableFaction = false;
            if (FactionEntry const* reputationFaction =
                    sObjectMgr.GetFactionEntry(creatureFaction->faction))
            {
                if (reputationFaction->CanHaveReputation())
                {
                    // Reputation factions override the template masks in this
                    // core. Evaluate their DBC base standing for every normal
                    // playable race/class combination, independent of whichever
                    // faction the nearby observer happens to use.
                    for (uint8 race = 1; race < MAX_RACES; ++race)
                    {
                        uint32 const raceMask = 1u << (race - 1);
                        if (!(RACEMASK_ALL_PLAYABLE & raceMask))
                            continue;
                        for (uint8 playerClass = 1; playerClass < MAX_CLASSES; ++playerClass)
                        {
                            uint32 const classMask = 1u << (playerClass - 1);
                            if (!(CLASSMASK_ALL_PLAYABLE & classMask))
                                continue;
                            int const index = reputationFaction->GetIndexFitTo(raceMask, classMask);
                            ReputationRank rank = index >= 0
                                ? ReputationMgr::ReputationToRank(reputationFaction->BaseRepValue[index])
                                : REP_NEUTRAL;
                            if (index >= 0 &&
                                (reputationFaction->ReputationFlags[index] & FACTION_FLAG_AT_WAR))
                                rank = std::min(rank, REP_NEUTRAL);
                            if (rank >= REP_FRIENDLY)
                                return REP_FRIENDLY;
                            if (rank < REP_NEUTRAL)
                                hostileToPlayableFaction = true;
                        }
                    }
                    return hostileToPlayableFaction ? REP_HOSTILE : REP_NEUTRAL;
                }
            }

            for (uint8 race = 1; race < MAX_RACES; ++race)
            {
                if (!(RACEMASK_ALL_PLAYABLE & (1u << (race - 1))))
                    continue;
                FactionTemplateEntry const* playableFaction = sObjectMgr.GetFactionTemplateEntry(
                    Player::GetFactionForRace(race));
                if (!playableFaction)
                    continue;

                // Match this core's faction-template reaction order. A normal
                // Alliance or Horde NPC is allowed when it is genuinely friendly
                // to at least one playable race, independent of the observer's
                // faction. Merely being non-hostile never qualifies.
                if (creatureFaction->IsHostileTo(*playableFaction))
                    hostileToPlayableFaction = true;
                else if (creatureFaction->IsFriendlyTo(*playableFaction) ||
                         playableFaction->IsFriendlyTo(*creatureFaction))
                    return REP_FRIENDLY;
            }
            return hostileToPlayableFaction ? REP_HOSTILE : REP_NEUTRAL;
        }

        NpcEligibilityResult EvaluateNpcSpeaker(Creature const* creature, float observerDistance,
                                                Config const& config)
        {
            if (!creature || !creature->IsInWorld() || !creature->IsAlive() ||
                !creature->GetCreatureInfo())
                return NpcEligibilityResult::Invalid;

            // Generic, static database creatures are the only valid NPC
            // speakers. This excludes pets, guardians/summons, totems,
            // charmed/possessed units, triggers, critters, and other temporary
            // combat entities without depending on service npc_flags.
            if (creature->IsPet() || creature->IsTotem() || creature->IsTemporarySummon() ||
                creature->IsCharmed() || !creature->GetCharmerOrOwnerGuid().IsEmpty() ||
                creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_POSSESSED) ||
                creature->IsTrigger() || creature->IsCritter() ||
                !creature->HasStaticDBSpawnData())
                return NpcEligibilityResult::Temporary;

            uint32_t const entry = creature->GetEntry();
            if (config.npcExcludedEntries.count(entry))
                return NpcEligibilityResult::Filtered;
            if (!config.npcAllowedEntries.count(entry) &&
                !config.npcAllowedTypes.count(creature->GetCreatureInfo()->type))
                return NpcEligibilityResult::Filtered;

            ReputationRank const reaction = NpcPlayableFactionReaction(creature);
            if (reaction == REP_NEUTRAL)
            {
                if (!config.npcAllowNeutral && !config.npcAllowedNeutralEntries.count(entry))
                    return NpcEligibilityResult::Neutral;
            }
            else if (reaction < REP_FRIENDLY && !config.npcAllowHostile &&
                     !config.npcAllowedHostileEntries.count(entry))
                return NpcEligibilityResult::Hostile;
            if (!HasNearbyRealPlayer(creature, observerDistance))
                return NpcEligibilityResult::NoHumanNearby;
            return NpcEligibilityResult::Eligible;
        }

        Player* FindOnlineRealGuildAudience(uint32_t guildId, ChatScope scope,
                                            Player const* excludedPlayer = nullptr)
        {
            if (!guildId)
                return nullptr;
            Guild* guild = sGuildMgr.GetGuildById(guildId);
            if (!guild)
                return nullptr;

            uint32 const listenRight = scope == ChatScope::Officer
                ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN;
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (player != excludedPlayer && IsOnlineRealPlayer(player) &&
                    player->GetGuildId() == guildId &&
                    guild->HasRankRight(player->GetRank(), listenRight))
                    return player;
            }
            return nullptr;
        }

        Player* FindOnlineRealGuildAudience(Player const* speaker, ChatScope scope)
        {
            return speaker
                ? FindOnlineRealGuildAudience(speaker->GetGuildId(), scope, speaker)
                : nullptr;
        }

        Player* FindOnlineRealGroupAudience(Player const* speaker, ChatScope scope)
        {
            Group const* group = speaker ? speaker->GetGroup() : nullptr;
            if (!group)
                return nullptr;

            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (player == speaker || !IsOnlineRealPlayer(player) || player->GetGroup() != group)
                    continue;
                if (scope == ChatScope::Raid ||
                    group->GetMemberGroup(player->GetObjectGuid()) ==
                        group->GetMemberGroup(speaker->GetObjectGuid()))
                    return player;
            }
            return nullptr;
        }

        bool HasRealPlayerAudience(Player* speaker, ChatScope scope,
                                   std::string const& channelName, float localDistance)
        {
            if (!speaker || !speaker->IsInWorld())
                return false;
            if (scope == ChatScope::Whisper)
                return false;
            if (scope == ChatScope::Say || scope == ChatScope::Yell)
                return HasNearbyRealPlayer(speaker, localDistance);
            if (scope == ChatScope::World)
                return FindOnlineRealPlayer() != nullptr;
            if (scope == ChatScope::Guild || scope == ChatScope::Officer)
                return FindOnlineRealGuildAudience(speaker, scope) != nullptr;
            if (scope == ChatScope::Party || scope == ChatScope::Raid)
                return FindOnlineRealGroupAudience(speaker, scope) != nullptr;

            Channel* channel = nullptr;
            if (scope == ChatScope::Channel)
            {
                ChannelMgr* manager = channelMgr(speaker->GetTeam());
                channel = manager ? manager->GetChannel(channelName, speaker, false) : nullptr;
                if (!channel)
                    return false;
            }
            else
                return false;

            if (channel)
            {
                uint32_t onlineBots = 0;
                bool onlineRealPlayer = false;
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& entry : sObjectAccessor.GetPlayers())
                {
                    Player* player = entry.second;
                    if (!player || !player->IsInWorld() || !player->GetSession())
                        continue;
                    if (Script_IsAIControlled(player))
                        ++onlineBots;
                    else
                        onlineRealPlayer = true;
                }

                // Channel membership lookup is private in this core. If the
                // public member count exceeds every online bot that could
                // possibly be in the cross-faction channel, at least one member is
                // necessarily a real player. This conservative test may skip
                // calls, but it cannot approve a provably bot-only channel.
                return onlineRealPlayer && channel->GetNumPlayers() > onlineBots;
            }

            return false;
        }

        class BoundedCreatureRangeCheck
        {
        public:
            BoundedCreatureRangeCheck(WorldObject const* focus, float range, size_t maximum)
                : m_focus(focus), m_range(range), m_maximum(maximum) {}
            WorldObject const& GetFocusObject() const { return *m_focus; }
            bool operator()(Creature* creature)
            {
                if (!creature || m_accepted >= m_maximum || !m_focus->IsWithinDist(creature, m_range, false))
                    return false;
                ++m_accepted;
                return true;
            }
        private:
            WorldObject const* m_focus;
            float m_range;
            size_t m_maximum;
            size_t m_accepted = 0;
        };

        class BoundedGameObjectRangeCheck
        {
        public:
            BoundedGameObjectRangeCheck(WorldObject const* focus, float range, size_t maximum)
                : m_focus(focus), m_range(range), m_maximum(maximum) {}
            WorldObject const& GetFocusObject() const { return *m_focus; }
            bool operator()(GameObject* object)
            {
                if (!object || m_accepted >= m_maximum || !m_focus->IsWithinDist(object, m_range, false))
                    return false;
                ++m_accepted;
                return true;
            }
        private:
            WorldObject const* m_focus;
            float m_range;
            size_t m_maximum;
            size_t m_accepted = 0;
        };

        class BoundedPlayerRangeCheck
        {
        public:
            BoundedPlayerRangeCheck(WorldObject const* focus, float range, size_t maximum)
                : m_focus(focus), m_range(range), m_maximum(maximum) {}
            WorldObject const& GetFocusObject() const { return *m_focus; }
            bool operator()(Player* player)
            {
                if (!player || m_accepted >= m_maximum || !player->IsAlive() ||
                    !m_focus->IsWithinDistInMap(player, m_range))
                    return false;
                ++m_accepted;
                return true;
            }
        private:
            WorldObject const* m_focus;
            float m_range;
            size_t m_maximum;
            size_t m_accepted = 0;
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
            size_t position = 0;
            while ((position = value.find(from, position)) != std::string::npos)
            {
                value.replace(position, from.size(), to);
                position += to.size();
            }
        }

        std::string SanitizeEndpoint(std::string endpoint)
        {
            size_t const scheme = endpoint.find("://");
            size_t const authorityBegin = scheme == std::string::npos ? 0 : scheme + 3;
            size_t const pathBegin = endpoint.find('/', authorityBegin);
            size_t const authorityEnd = pathBegin == std::string::npos ? endpoint.size() : pathBegin;
            size_t const at = endpoint.rfind('@', authorityEnd);
            if (at != std::string::npos && at >= authorityBegin)
                endpoint.erase(authorityBegin, at - authorityBegin + 1);
            size_t const query = endpoint.find('?');
            if (query != std::string::npos)
                endpoint.replace(query, std::string::npos, "?[REDACTED]");
            size_t const fragment = endpoint.find('#');
            if (fragment != std::string::npos)
                endpoint.erase(fragment);
            return endpoint;
        }

        std::string RedactSecrets(Config const& config, std::string value)
        {
            std::string const apiKey = config.ResolveApiKey();
            if (!apiKey.empty())
                ReplaceAll(value, apiKey, "[REDACTED]");
            return value;
        }

        bool IsLocalEndpoint(std::string endpoint)
        {
            endpoint = Lower(std::move(endpoint));
            return endpoint.find("http://localhost") == 0 ||
                   endpoint.find("http://127.0.0.1") == 0 ||
                   endpoint.find("http://[::1]") == 0;
        }

        std::string RaceName(uint8_t race)
        {
            switch (race)
            {
                case RACE_HUMAN: return "human";
                case RACE_ORC: return "orc";
                case RACE_DWARF: return "dwarf";
                case RACE_NIGHTELF: return "night elf";
                case RACE_UNDEAD: return "undead";
                case RACE_TAUREN: return "tauren";
                case RACE_GNOME: return "gnome";
                case RACE_TROLL: return "troll";
                case RACE_GOBLIN: return "goblin";
                case RACE_HIGH_ELF: return "high elf";
                default: return "unknown race";
            }
        }

        std::string ClassName(uint8_t playerClass)
        {
            switch (playerClass)
            {
                case CLASS_WARRIOR: return "warrior";
                case CLASS_PALADIN: return "paladin";
                case CLASS_HUNTER: return "hunter";
                case CLASS_ROGUE: return "rogue";
                case CLASS_PRIEST: return "priest";
                case CLASS_SHAMAN: return "shaman";
                case CLASS_MAGE: return "mage";
                case CLASS_WARLOCK: return "warlock";
                case CLASS_DRUID: return "druid";
                default: return "adventurer";
            }
        }

        char const* TalentTreeName(uint8_t playerClass, uint32_t tab)
        {
            static char const* const warrior[] = { "arms", "fury", "protection" };
            static char const* const paladin[] = { "holy", "protection", "retribution" };
            static char const* const hunter[] = { "beast mastery", "marksmanship", "survival" };
            static char const* const rogue[] = { "assassination", "combat", "subtlety" };
            static char const* const priest[] = { "discipline", "holy", "shadow" };
            static char const* const shaman[] = { "elemental", "enhancement", "restoration" };
            static char const* const mage[] = { "arcane", "fire", "frost" };
            static char const* const warlock[] = { "affliction", "demonology", "destruction" };
            static char const* const druid[] = { "balance", "feral combat", "restoration" };
            if (tab > 2)
                return "undeveloped";
            switch (playerClass)
            {
                case CLASS_WARRIOR: return warrior[tab];
                case CLASS_PALADIN: return paladin[tab];
                case CLASS_HUNTER: return hunter[tab];
                case CLASS_ROGUE: return rogue[tab];
                case CLASS_PRIEST: return priest[tab];
                case CLASS_SHAMAN: return shaman[tab];
                case CLASS_MAGE: return mage[tab];
                case CLASS_WARLOCK: return warlock[tab];
                case CLASS_DRUID: return druid[tab];
                default: return "undeveloped";
            }
        }

        std::string TalentBuild(Player const* player)
        {
            if (!player)
                return "unknown";
            std::array<uint32_t, 3> points = { 0, 0, 0 };
            uint32_t const classMask = player->getClassMask();
            for (uint32_t i = 0; i < sTalentStore.GetNumRows(); ++i)
            {
                TalentEntry const* talent = sTalentStore.LookupEntry(i);
                if (!talent)
                    continue;
                TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
                if (!tab || !(classMask & tab->ClassMask) || tab->tabpage > 2)
                    continue;
                for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
                    if (talent->RankID[rank] && player->HasSpell(talent->RankID[rank]))
                    {
                        points[tab->tabpage] += static_cast<uint32_t>(rank + 1);
                        break;
                    }
            }

            uint32_t dominant = 0;
            if (points[1] > points[dominant])
                dominant = 1;
            if (points[2] > points[dominant])
                dominant = 2;
            std::ostringstream result;
            uint32_t const total = points[0] + points[1] + points[2];
            if (!total)
                result << "no developed " << ClassName(player->GetClass()) << " specialization yet";
            else
                result << TalentTreeName(player->GetClass(), dominant) << ' ' << ClassName(player->GetClass());
            result << " (" << points[0] << '/' << points[1] << '/' << points[2] << ')';
            return result.str();
        }

        std::string GenderName(uint8_t gender)
        {
            if (gender == GENDER_MALE)
                return "male";
            if (gender == GENDER_FEMALE)
                return "female";
            return "unknown gender";
        }

        std::string TeamName(Team team)
        {
            if (team == ALLIANCE)
                return "Alliance";
            if (team == HORDE)
                return "Horde";
            return "neutral";
        }

        char const* GameObjectTypeName(GameobjectTypes type)
        {
            switch (type)
            {
                case GAMEOBJECT_TYPE_DOOR: return "door";
                case GAMEOBJECT_TYPE_BUTTON: return "button";
                case GAMEOBJECT_TYPE_QUESTGIVER: return "quest object";
                case GAMEOBJECT_TYPE_CHEST: return "chest";
                case GAMEOBJECT_TYPE_TRAP: return "trap";
                case GAMEOBJECT_TYPE_CHAIR: return "chair";
                case GAMEOBJECT_TYPE_SPELL_FOCUS: return "spell focus";
                case GAMEOBJECT_TYPE_TEXT: return "readable object";
                case GAMEOBJECT_TYPE_GOOBER: return "interactive object";
                case GAMEOBJECT_TYPE_TRANSPORT:
                case GAMEOBJECT_TYPE_MO_TRANSPORT: return "transport";
                case GAMEOBJECT_TYPE_FISHINGNODE: return "fishing node";
                case GAMEOBJECT_TYPE_SUMMONING_RITUAL: return "summoning ritual";
                case GAMEOBJECT_TYPE_MAILBOX: return "mailbox";
                case GAMEOBJECT_TYPE_AUCTIONHOUSE: return "auction house";
                case GAMEOBJECT_TYPE_MEETINGSTONE: return "meeting stone";
                case GAMEOBJECT_TYPE_FISHINGHOLE: return "fishing pool";
                default: return "world object";
            }
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

        std::string ActorKindName(ActorKind kind)
        {
            return kind == ActorKind::Creature ? "npc" : "playerbot";
        }

        std::string SanitizeLogText(std::string const& value)
        {
            constexpr size_t maximumLength = 500;
            std::string result;
            result.reserve(std::min(value.size(), maximumLength));
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                char output = static_cast<char>(input);
                if (output == '\r' || output == '\n' || output == '\t' || std::iscntrl(input))
                    output = ' ';
                else if (output == '"')
                    output = '\'';

                bool const space = std::isspace(static_cast<unsigned char>(output)) != 0;
                if (space && previousSpace)
                    continue;
                result.push_back(output);
                previousSpace = space;
                if (result.size() >= maximumLength)
                {
                    result += "...";
                    break;
                }
            }
            return Trim(result);
        }

        std::string JoinReplyLines(std::vector<std::string> const& lines)
        {
            std::string result;
            for (std::string const& line : lines)
            {
                if (!result.empty())
                    result += " / ";
                result += line;
            }
            return SanitizeLogText(result);
        }

        bool IsScopeEnabled(Config const& config, ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::Say: return config.sayReplies;
                case ChatScope::Yell: return config.yellReplies;
                case ChatScope::Whisper: return config.whisperReplies;
                case ChatScope::Party: return config.partyReplies;
                case ChatScope::Raid: return config.raidReplies;
                case ChatScope::Guild: return config.guildReplies;
                case ChatScope::Officer: return config.officerReplies;
                case ChatScope::World: return config.worldReplies;
                case ChatScope::Channel: return config.customChannelReplies;
            }
            return false;
        }

        bool IsBlockedChannel(Config const& config, ChatScope scope, std::string const& channelName)
        {
            std::string scopeLower = Lower(ScopeName(scope));
            std::string channelLower = Lower(channelName);
            for (std::string blocked : config.blockedChannels)
            {
                blocked = Lower(Trim(blocked));
                if (!blocked.empty() && (blocked == scopeLower || blocked == channelLower))
                    return true;
            }
            return false;
        }

        bool IsBlacklisted(Config const& config, std::string const& message)
        {
            std::string text = Lower(Trim(message));
            for (std::string token : config.commandBlacklist)
            {
                token = Lower(Trim(token));
                if (!token.empty() && text.compare(0, token.size(), token) == 0)
                    return true;
            }
            return false;
        }

        bool IsNameBoundary(std::string const& value, size_t position)
        {
            return position >= value.size() ||
                !std::isalnum(static_cast<unsigned char>(value[position]));
        }

        bool ContainsExplicitName(std::string const& message, std::string const& name)
        {
            std::string const text = Lower(message);
            std::string const wanted = Lower(name);
            if (wanted.empty())
                return false;
            size_t position = text.find(wanted);
            while (position != std::string::npos)
            {
                bool const left = position == 0 ||
                    !std::isalnum(static_cast<unsigned char>(text[position - 1]));
                bool const right = IsNameBoundary(text, position + wanted.size());
                if (left && right)
                    return true;
                position = text.find(wanted, position + 1);
            }
            return false;
        }

        std::string RemoveExplicitName(std::string message, std::string const& name)
        {
            std::string const text = Lower(message);
            std::string const wanted = Lower(name);
            size_t position = text.find(wanted);
            while (position != std::string::npos)
            {
                bool const left = position == 0 ||
                    !std::isalnum(static_cast<unsigned char>(text[position - 1]));
                bool const right = IsNameBoundary(text, position + wanted.size());
                if (left && right)
                {
                    message.erase(position, wanted.size());
                    break;
                }
                position = text.find(wanted, position + 1);
            }
            message = Trim(message);
            while (!message.empty() && (message.front() == ',' || message.front() == ':' ||
                message.front() == '-' || message.front() == '@'))
                message = Trim(message.substr(1));
            return message;
        }

        std::string NormalizeNaturalPhrase(std::string value)
        {
            value = Lower(Trim(value));
            std::string result;
            result.reserve(value.size());
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                bool const word = std::isalnum(input) != 0 || input == '\'';
                if (word)
                {
                    result.push_back(static_cast<char>(input));
                    previousSpace = false;
                }
                else if (!previousSpace && !result.empty())
                {
                    result.push_back(' ');
                    previousSpace = true;
                }
            }
            if (!result.empty() && result.back() == ' ')
                result.pop_back();
            return result;
        }

        NaturalCommandAction const* FindNaturalCommandPrefix(std::string message)
        {
            message = NormalizeNaturalCommandAction(std::move(message));
            std::string const prefix = NormalizeNaturalCommandAction(
                PlayerbotBridge::CommandPrefix());
            if (!prefix.empty() && message.compare(0, prefix.size(), prefix) == 0)
                message = NormalizeNaturalCommandAction(message.substr(prefix.size()));

            NaturalCommandAction const* best = nullptr;
            for (NaturalCommandAction const& action : GetNaturalCommandActions())
            {
                size_t const length = std::char_traits<char>::length(action.name);
                if (message.compare(0, length, action.name) != 0 ||
                    !IsNameBoundary(message, length))
                    continue;
                if (!best || length > std::char_traits<char>::length(best->name))
                    best = &action;
            }
            return best;
        }

        bool NaturalCommandChannelExcluded(Config const& config, ChatScope scope,
                                           std::string const& channelName)
        {
            std::string const scopeName = Lower(ScopeName(scope));
            std::string const channel = Lower(Trim(channelName));
            for (std::string excluded : config.naturalCommandsExcludedChannels)
            {
                excluded = Lower(Trim(excluded));
                if (excluded.empty())
                    continue;
                if (excluded == scopeName || excluded == channel ||
                    (scope == ChatScope::Channel && excluded == "custom") ||
                    (scope == ChatScope::Channel && excluded.compare(0, 8, "channel:") == 0 &&
                        Trim(excluded.substr(8)) == channel))
                    return true;
            }
            return false;
        }

        bool LooksLikeNaturalCommand(std::string const& message)
        {
            std::string const phrase = NormalizeNaturalPhrase(message);
            if (phrase.empty())
                return false;
            static std::vector<std::string> const informational = {
                "can you tell me about ", "could you tell me about ",
                "would you tell me about ", "tell me about "
            };
            for (std::string const& opening : informational)
                if (phrase.compare(0, opening.size(), opening) == 0)
                    return false;
            static std::vector<std::string> const openings = {
                "please ", "can you ", "could you ", "would you ", "will you ",
                "i need you to ", "i want you to ", "go ", "come ", "stop ",
                "start ", "attack ", "pull ", "follow ", "wait ", "stay ",
                "guard ", "flee ", "run ", "buff ", "heal ", "revive ",
                "invite ", "join ", "leave ", "equip ", "unequip ", "use ",
                "sell ", "buy ", "loot ", "repair ", "cast ", "talk ",
                "accept ", "share ", "drop ", "summon ", "travel ", "move ",
                "grind ", "farm ", "learn ", "train ", "craft ", "mail ",
                "set ", "change ",
                "switch ", "promote ", "demote ", "remove ", "give me "
            };
            for (std::string const& opening : openings)
                if (phrase.compare(0, opening.size(), opening) == 0)
                    return true;
            return false;
        }

        bool ConservativeMultilingualCommandCandidate(std::string const& message,
                                                      std::string const& addressing)
        {
            bool const nonAscii = std::any_of(message.begin(), message.end(), [](unsigned char c) {
                return c >= 0x80;
            });
            if (!nonAscii || message.find('?') != std::string::npos)
                return false;
            if (addressing == "whisper")
                return !message.empty() && message.back() == '!';
            if (addressing != "named" && addressing != "party-single")
                return false;
            size_t words = 0;
            bool inWord = false;
            for (unsigned char c : message)
            {
                bool const word = std::isalnum(c) != 0 || c >= 0x80;
                if (word && !inWord)
                    ++words;
                inWord = word;
            }
            return words >= 2 && words <= 12;
        }

        bool LooksLikeMultipleNaturalCommands(std::string const& message,
                                               std::string const& separator)
        {
            std::string const phrase = NormalizeNaturalPhrase(message);
            return message.find('\r') != std::string::npos ||
                message.find('\n') != std::string::npos ||
                message.find(';') != std::string::npos ||
                (!separator.empty() && message.find(separator) != std::string::npos) ||
                phrase.find(" and ") != std::string::npos ||
                phrase.find(" and then ") != std::string::npos ||
                phrase.find(" then also ") != std::string::npos;
        }

        std::vector<uint64_t> NaturalCommandRecipientGuids(ChatRequest const& request)
        {
            if (!request.naturalRecipientGuids.empty())
                return request.naturalRecipientGuids;
            return { request.actor.guid };
        }

        bool NaturalCommandPendingForAll(
            std::unordered_map<uint64_t, std::deque<uint64_t>> const& pending,
            ChatRequest const& request, bool requireFront)
        {
            for (uint64_t guid : NaturalCommandRecipientGuids(request))
            {
                auto found = pending.find(guid);
                if (found == pending.end() || found->second.empty())
                    return false;
                if (requireFront)
                {
                    if (found->second.front() != request.id)
                        return false;
                }
                else if (std::find(found->second.begin(), found->second.end(), request.id) ==
                    found->second.end())
                    return false;
            }
            return true;
        }

        bool RemoveNaturalCommandPending(
            std::unordered_map<uint64_t, std::deque<uint64_t>>& pending,
            ChatRequest const& request)
        {
            bool removed = false;
            for (uint64_t guid : NaturalCommandRecipientGuids(request))
            {
                auto found = pending.find(guid);
                if (found == pending.end())
                    continue;
                auto id = std::find(found->second.begin(), found->second.end(), request.id);
                if (id != found->second.end())
                {
                    found->second.erase(id);
                    removed = true;
                }
                if (found->second.empty())
                    pending.erase(found);
            }
            return removed;
        }

        bool NaturalArgumentsComeFromMessage(std::string const& arguments,
                                             std::string const& protectedMessage)
        {
            std::set<std::string> const ignored = {
                "a", "an", "and", "at", "for", "from", "in", "my", "of", "on",
                "please", "the", "this", "to", "with", "your"
            };
            auto words = [&](std::string const& value) {
                std::set<std::string> result;
                std::istringstream stream(NormalizeNaturalPhrase(value));
                for (std::string word; stream >> word;)
                    if (word.size() > 1 && !ignored.count(word))
                        result.insert(std::move(word));
                return result;
            };
            std::set<std::string> const source = words(protectedMessage);
            for (std::string const& word : words(arguments))
                if (!source.count(word))
                    return false;
            return true;
        }

        bool FindLocalNaturalCommand(std::string const& message,
                                     std::set<std::string> const& allowed,
                                     std::string& action, std::string& arguments)
        {
            struct LocalPhrase { char const* phrase; char const* action; char const* arguments; };
            static LocalPhrase const phrases[] = {
                { "follow me", "follow", "" },
                { "please follow me", "follow", "" },
                { "come with me", "follow", "" },
                { "come along with me", "follow", "" },
                { "attack my target", "attack", "" },
                { "please attack my target", "attack", "" },
                { "pull my target", "pull", "" },
                { "buff me", "buff", "" },
                { "hold position", "stay", "" },
                { "hold your position", "stay", "" },
                { "wait here", "stay", "" },
                { "do not move", "stay", "" },
                { "stop moving", "stay", "" },
                { "run away", "runaway", "" },
                { "get out of here", "runaway", "" },
                { "show me your quests", "quests", "" },
                { "tell me your quests", "quests", "" },
                { "show me your stats", "stats", "" },
                { "tell me your stats", "stats", "" },
                { "list your spells", "spells", "" },
                { "show me your spells", "spells", "" },
                { "tell me your position", "position", "" },
                { "show me your position", "position", "" },
                { "check if you are ready", "ready", "" }
            };
            std::string const phrase = NormalizeNaturalPhrase(message);
            for (LocalPhrase const& candidate : phrases)
            {
                if (phrase == candidate.phrase && allowed.count(candidate.action))
                {
                    action = candidate.action;
                    arguments = candidate.arguments;
                    return true;
                }
            }
            return false;
        }

        std::string GuildName(Player const* player)
        {
            if (!player || !player->GetGuildId())
                return "";
            Guild* guild = sGuildMgr.GetGuildById(player->GetGuildId());
            return guild ? guild->GetName() : "";
        }

        void FillLocation(WorldObject const* object, std::string& area, std::string& zone,
                          std::string& mapName, uint32_t& mapId, uint32_t& areaId, uint32_t& zoneId)
        {
            if (!object)
                return;

            mapId = object->GetMapId();
            areaId = object->GetAreaId();
            zoneId = object->GetZoneId();
            if (Map const* map = object->FindMap())
                mapName = map->GetMapName();
            if (AreaEntry const* entry = AreaEntry::GetById(areaId))
                area = entry->Name ? entry->Name : "";
            if (AreaEntry const* entry = AreaEntry::GetById(zoneId))
                zone = entry->Name ? entry->Name : "";
            if (zone.empty())
                zone = area;
        }

        SpeakerSnapshot SnapshotSpeaker(Player const* player)
        {
            SpeakerSnapshot result;
            if (!player)
                return result;
            result.guid = player->GetObjectGuid().GetRawValue();
            result.name = player->GetName();
            result.race = RaceName(player->GetRace());
            result.className = ClassName(player->GetClass());
            result.gender = GenderName(player->GetGender());
            result.faction = TeamName(player->GetTeam());
            result.guild = GuildName(player);
            result.groupStatus = player->GetGroup() ? "in a group" : "solo";
            result.level = player->GetLevel();
            result.groupId = player->GetGroup() ? player->GetGroup()->GetId() : 0;
            result.guildId = player->GetGuildId();
            result.isBot = Script_IsAIControlled(player);
            return result;
        }

        ActorSnapshot SnapshotBot(Player const* player)
        {
            ActorSnapshot result;
            result.kind = ActorKind::PlayerBot;
            if (!player)
                return result;
            result.guid = player->GetObjectGuid().GetRawValue();
            result.anchorPlayerGuid = result.guid;
            result.name = player->GetName();
            result.race = RaceName(player->GetRace());
            result.className = ClassName(player->GetClass());
            result.gender = GenderName(player->GetGender());
            result.faction = TeamName(player->GetTeam());
            result.guild = GuildName(player);
            result.groupStatus = player->GetGroup() ? "in a group" : "solo";
            result.talentBuild = TalentBuild(player);
            result.level = player->GetLevel();
            result.inCombat = player->IsInCombat();
            FillLocation(player, result.area, result.zone, result.map,
                         result.mapId, result.areaId, result.zoneId);
            return result;
        }

        ActorSnapshot SnapshotCreature(Creature const* creature, Player const* anchor,
                                       NpcDisposition disposition)
        {
            ActorSnapshot result;
            result.kind = ActorKind::Creature;
            if (!creature)
                return result;
            result.guid = creature->GetObjectGuid().GetRawValue();
            result.anchorPlayerGuid = anchor ? anchor->GetObjectGuid().GetRawValue() : 0;
            result.name = creature->GetName();
            result.race = "NPC";
            result.className = "NPC";
            result.gender = GenderName(creature->GetGender());
            result.disposition = NpcDispositionName(disposition);
            result.faction = result.disposition + " NPC";
            result.groupStatus = "nearby NPC";
            result.level = creature->GetLevel();
            result.inCombat = creature->IsInCombat();
            FillLocation(creature, result.area, result.zone, result.map,
                         result.mapId, result.areaId, result.zoneId);
            return result;
        }

        std::string Expand(std::string value, ChatRequest const& request)
        {
            ReplaceAll(value, "<bot name>", request.actor.name);
            ReplaceAll(value, "<bot level>", std::to_string(request.actor.level));
            ReplaceAll(value, "<bot race>", request.actor.race);
            ReplaceAll(value, "<bot class>", request.actor.className);
            ReplaceAll(value, "<bot gender>", request.actor.gender);
            ReplaceAll(value, "<bot faction>", request.actor.faction);
            ReplaceAll(value, "<bot zone>", request.actor.zone);
            ReplaceAll(value, "<bot subzone>", request.actor.area);
            ReplaceAll(value, "<bot map>", request.actor.map);
            ReplaceAll(value, "<bot guild>", request.actor.guild);
            ReplaceAll(value, "<bot specialization>", request.actor.talentBuild);
            ReplaceAll(value, "<bot personality>", JoinPersonalityTraits(request.personality.traits));
            ReplaceAll(value, "<bot background>", request.personality.background);
            ReplaceAll(value, "<bot tone>", request.personality.tone);
            ReplaceAll(value, "<bot personality block>", request.personalityBlock);
            ReplaceAll(value, "<bot type>", request.actor.kind == ActorKind::Creature ? "NPC" : "playerbot");
            ReplaceAll(value, "<expansion name>", "Turtle WoW");
            ReplaceAll(value, "<sender name>", request.speaker.name);
            ReplaceAll(value, "<receiver name>", request.actor.name);
            ReplaceAll(value, "<other name>", request.speaker.name);
            ReplaceAll(value, "<other level>", std::to_string(request.speaker.level));
            ReplaceAll(value, "<other race>", request.speaker.race);
            ReplaceAll(value, "<other class>", request.speaker.className);
            ReplaceAll(value, "<other gender>", request.speaker.gender);
            ReplaceAll(value, "<other faction>", request.speaker.faction);
            ReplaceAll(value, "<other type>", request.speaker.isBot ? "playerbot" : "player");
            ReplaceAll(value, "<unit type>", request.speaker.isBot ? "playerbot" : "player");
            ReplaceAll(value, "<unit name>", request.speaker.name);
            ReplaceAll(value, "<unit subname>", "");
            ReplaceAll(value, "<unit level>", std::to_string(request.speaker.level));
            ReplaceAll(value, "<unit gender>", request.speaker.gender);
            ReplaceAll(value, "<unit race>", request.speaker.race);
            ReplaceAll(value, "<unit faction>", request.speaker.faction);
            ReplaceAll(value, "<unit class>", request.speaker.className);
            ReplaceAll(value, "<initial message>", request.incomingMessage);
            ReplaceAll(value, "<channel name>", request.channelName.empty() ? ScopeName(request.scope) : request.channelName);
            ReplaceAll(value, "<trigger>", request.trigger);
            return value;
        }

        std::string HistoryKey(Config const& config, ActorSnapshot const& actor,
                               SpeakerSnapshot const& speaker, ChatScope scope, std::string const& channel)
        {
            std::ostringstream key;
            key << static_cast<unsigned>(actor.kind) << ':' << actor.guid;
            if (!config.globalContext)
                key << ':' << speaker.guid << ':' << static_cast<unsigned>(scope) << ':' << Lower(channel);
            return key.str();
        }

        std::string ActorKey(ActorSnapshot const& actor)
        {
            return std::to_string(static_cast<unsigned>(actor.kind)) + ':' + std::to_string(actor.guid);
        }

        std::string ScopeKey(ChatScope scope, std::string const& channel,
                             ActorSnapshot const& location, SpeakerSnapshot const& speaker)
        {
            std::ostringstream key;
            key << static_cast<unsigned>(scope) << ':';
            switch (scope)
            {
                case ChatScope::Whisper:
                    key << std::min(speaker.guid, location.guid) << ':'
                        << std::max(speaker.guid, location.guid);
                    break;
                case ChatScope::Party:
                case ChatScope::Raid:
                    key << "group:" << speaker.groupId;
                    break;
                case ChatScope::Guild:
                case ChatScope::Officer:
                    key << "guild:" << speaker.guildId;
                    break;
                case ChatScope::Channel:
                case ChatScope::World:
                    key << "channel:" << Lower(channel);
                    break;
                case ChatScope::Say:
                case ChatScope::Yell:
                    key << "place:" << location.mapId << ':' << location.areaId;
                    break;
            }
            return key.str();
        }

        std::string HeadBounded(std::string value, size_t maximum)
        {
            if (value.size() <= maximum)
                return value;
            if (maximum <= 3)
                return value.substr(0, maximum);
            value.resize(maximum - 3);
            value += "...";
            return value;
        }

        std::string TailBounded(std::string value, size_t maximum)
        {
            if (value.size() <= maximum)
                return value;
            if (maximum <= 3)
                return value.substr(value.size() - maximum);
            return "..." + value.substr(value.size() - (maximum - 3));
        }

        std::vector<std::string> Words(std::string const& input)
        {
            std::vector<std::string> words;
            std::string current;
            for (unsigned char c : Lower(input))
            {
                if (std::isalnum(c))
                    current.push_back(static_cast<char>(c));
                else if (current.size() >= 3)
                {
                    words.push_back(current);
                    current.clear();
                }
                else
                    current.clear();
            }
            if (current.size() >= 3)
                words.push_back(current);
            return words;
        }

        std::vector<std::string> Split(std::string const& input, char delimiter)
        {
            std::vector<std::string> result;
            std::stringstream stream(input);
            std::string part;
            while (std::getline(stream, part, delimiter))
            {
                part = Trim(part);
                if (!part.empty())
                    result.push_back(part);
            }
            return result;
        }

        std::string Pick(std::vector<std::string> const& items)
        {
            return items.empty() ? "" : items[RandomUInt(0, static_cast<uint32_t>(items.size() - 1))];
        }

        ChatScope ParseScope(std::string value)
        {
            value = Lower(Trim(value));
            if (value == "yell") return ChatScope::Yell;
            if (value == "whisper") return ChatScope::Whisper;
            if (value == "party") return ChatScope::Party;
            if (value == "raid") return ChatScope::Raid;
            if (value == "guild") return ChatScope::Guild;
            if (value == "officer") return ChatScope::Officer;
            if (value == "world") return ChatScope::World;
            if (value == "channel") return ChatScope::Channel;
            return ChatScope::Say;
        }
    }

    Manager& Manager::Instance()
    {
        static Manager instance;
        return instance;
    }

    Manager::Manager()
        : m_stopping(false), m_paused(false), m_inFlight(0), m_nextRequestId(1),
          m_accepted(0), m_completed(0), m_failed(0), m_dropped(0),
          m_suppressedErrors(0), m_started(false)
    {
    }

    Manager::~Manager()
    {
        Stop();
    }

    void Manager::Start()
    {
        Stop();
        m_config = std::make_shared<Config const>(Config::Load());
        m_paused = false;

        if (!m_config->enabled)
        {
            sLog.outString("[AzerothVoices] Module loaded but disabled.");
            return;
        }

        if (m_config->ResolveApiKey().empty())
            sLog.outError("[AzerothVoices] API key resolved empty; provider requests will omit the Authorization header.");
        if (m_config->naturalCommandsEnabled && PlayerbotBridge::CommandPrefix().empty())
            sLog.outError("[AzerothVoices][NaturalCommands] AiPlayerbot.CommandPrefix is empty; native PlayerBots may process unprefixed commands before Azeroth Voices. Recommended operator setting: AiPlayerbot.CommandPrefix = !");

        std::string tlsError;
        if (!Provider::InitializeTls(tlsError))
        {
            sLog.outError("[AzerothVoices] TLS initialization failed: %s", tlsError.c_str());
            return;
        }

        InitializeDatabaseStorage();
        LoadRag();
        m_stopping = false;
        m_started = true;
        m_lastErrorLog = Clock::time_point();
        m_suppressedErrors = 0;
        m_telemetryWindowStarted = Clock::now();
        m_telemetryApiCalls = 0;
        m_telemetrySuccessfulResults = 0;
        m_telemetryFailedResults = 0;
        m_telemetryGeneratedMessages = 0;
        m_naturalClassified = 0;
        m_naturalDispatched = 0;
        m_naturalRejected = 0;
        m_naturalExpired = 0;
        m_naturalConsidered = 0;
        m_naturalLocalFastPath = 0;
        m_naturalClassifierQueued = 0;
        m_naturalClassifierResults = 0;
        m_naturalClassifierLatencyMilliseconds = 0;
        m_naturalShortlistActions = 0;
        m_naturalPromptCharacters = 0;
        m_naturalTelemetry = {};
        m_naturalActionUsage.clear();
        m_naturalCommandAudit.clear();
        m_naturalLastFailure.clear();
        m_preflightRejections.fill(0);
        m_nextHistoryPrune = Clock::now() + std::chrono::minutes(1);
        m_nextDatabaseFlush = Clock::now() + std::chrono::seconds(m_config->historyDatabaseFlushSeconds);
        m_nextDatabaseCleanup = Clock::now() + std::chrono::hours(1);
        for (uint32_t i = 0; i < m_config->workerThreads; ++i)
            m_workers.emplace_back(&Manager::WorkerLoop, this);
        ScheduleNextAmbient();

        std::string const commandModel = m_config->naturalCommandsModel.empty()
            ? m_config->model : m_config->naturalCommandsModel;
        sLog.outString("[AzerothVoices] Started with %u workers, endpoint %s, model %s, natural-command model %s.",
            m_config->workerThreads, SanitizeEndpoint(m_config->endpoint).c_str(),
            m_config->model.c_str(), commandModel.c_str());
    }

    void Manager::Reload()
    {
        sLog.outString("[AzerothVoices][INIT] Reloading configuration and restarting workers.");
        Start();
    }

    void Manager::Stop()
    {
        if (m_config)
            FlushDatabaseWrites(true);
        m_started = false;
        m_stopping = true;
        m_queueReady.notify_all();
        for (std::thread& worker : m_workers)
            if (worker.joinable())
                worker.join();
        m_workers.clear();

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto& queue : m_queues)
                queue.clear();
            m_latestRequestByActor.clear();
            m_pendingNaturalCommandsByActor.clear();
            m_requestBudget.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_completionMutex);
            m_completions.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_ingressMutex);
            m_ingress.clear();
        }
        m_scheduled.clear();
        m_actorCooldowns.clear();
        m_speakerCooldowns.clear();
        m_eventCooldowns.clear();
        m_pendingNaturalConfirmations.clear();
        m_naturalActionUsage.clear();
        m_naturalCommandAudit.clear();
        m_history.clear();
        m_surroundingChat.clear();
        m_snapshotHistory.clear();
        m_personalities.clear();
        m_personalityCacheOrder.clear();
        m_databaseLoadedHistoryKeys.clear();
        m_databaseLoadedSnapshotKeys.clear();
        m_databaseLoadedPersonalityGuids.clear();
        m_pendingPersonalityRequests.clear();
        m_personalityRetryAfter.clear();
        m_personalityGenerationStatus.clear();
        m_personalityGenerationStatusOrder.clear();
        m_pendingHistoryWrites.clear();
        m_pendingSnapshotWrites.clear();
        m_historyDatabaseAvailable = false;
        m_snapshotDatabaseAvailable = false;
        m_personalityDatabaseAvailable = false;
        m_telemetryApiCalls = 0;
        m_telemetrySuccessfulResults = 0;
        m_telemetryFailedResults = 0;
        m_telemetryGeneratedMessages = 0;
        m_naturalTelemetry = {};
        m_preflightRejections.fill(0);
        m_inFlight = 0;
    }

    void Manager::Update(uint32_t /*diff*/)
    {
        if (!m_started || !m_config || !m_config->enabled)
            return;
        DrainIngress();
        DrainCompletions();
        auto const confirmationNow = Clock::now();
        for (auto pending = m_pendingNaturalConfirmations.begin();
             pending != m_pendingNaturalConfirmations.end();)
        {
            if (confirmationNow <= pending->second.expires)
            {
                ++pending;
                continue;
            }
            Player* speaker = ObjectAccessor::FindPlayer(ObjectGuid(pending->second.speakerGuid));
            SendNaturalCommandFeedback(speaker, "Pending natural-command confirmation expired.");
            ++m_naturalExpired;
            ++m_naturalTelemetry.confirmationExpired;
            for (uint64_t botGuid : pending->second.botGuids)
                for (PendingNaturalConfirmation::Action const& action : pending->second.actions)
                    RecordNaturalCommandAudit(pending->second.speakerGuid, botGuid,
                        action.action, action.arguments, pending->second.source, "confirmation-expired",
                        action.confidence, pending->second.requestId,
                        pending->second.latencyMilliseconds);
            pending = m_pendingNaturalConfirmations.erase(pending);
        }
        ReportTelemetry();
        DeliverScheduled();
        FlushDatabaseWrites();
        if (Clock::now() >= m_nextHistoryPrune)
        {
            PruneHistory();
            m_nextHistoryPrune = Clock::now() + std::chrono::minutes(1);
        }
        if ((m_historyDatabaseAvailable || m_snapshotDatabaseAvailable) && Clock::now() >= m_nextDatabaseCleanup)
        {
            CleanupDatabase();
            m_nextDatabaseCleanup = Clock::now() + std::chrono::hours(1);
        }
        if (!m_paused && m_config->randomChatterEnabled && Clock::now() >= m_nextAmbient)
        {
            RunAmbient();
            ScheduleNextAmbient();
        }
    }

    void Manager::ScheduleNextAmbient()
    {
        uint32_t seconds = m_config ? RandomUInt(m_config->randomMinimumIntervalSeconds,
                                                  m_config->randomMaximumIntervalSeconds) : 120;
        m_nextAmbient = Clock::now() + std::chrono::seconds(seconds);
    }

    bool Manager::PopRequest(ChatRequest& request)
    {
        for (;;)
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueReady.wait(lock, [&]() {
                if (m_stopping)
                    return true;
                for (auto const& queue : m_queues)
                    for (ChatRequest const& queued : queue)
                    {
                        if (queued.kind != RequestKind::NaturalCommand)
                            return true;
                        if (NaturalCommandPendingForAll(
                            m_pendingNaturalCommandsByActor, queued, true))
                            return true;
                    }
                return false;
            });
            if (m_stopping)
                return false;

            for (int priority = 3; priority >= 0; --priority)
            {
                auto& queue = m_queues[static_cast<size_t>(priority)];
                for (auto current = queue.begin(); current != queue.end();)
                {
                    if (current->kind == RequestKind::NaturalCommand)
                    {
                        if (!NaturalCommandPendingForAll(
                            m_pendingNaturalCommandsByActor, *current, false))
                        {
                            current = queue.erase(current);
                            ++m_dropped;
                            continue;
                        }
                        if (!NaturalCommandPendingForAll(
                            m_pendingNaturalCommandsByActor, *current, true))
                        {
                            ++current;
                            continue;
                        }
                    }
                    request = std::move(*current);
                    queue.erase(current);
                    if (request.kind == RequestKind::PersonalityGeneration)
                        return true;
                    if (request.kind == RequestKind::NaturalCommand)
                        return true;
                    auto latest = m_latestRequestByActor.find(request.actor.guid);
                    if (latest != m_latestRequestByActor.end() && latest->second == request.id)
                        return true;
                    ++m_dropped;
                }
            }
        }
    }

    void Manager::WorkerLoop()
    {
        ChatRequest request;
        while (PopRequest(request))
        {
            if (Clock::now() > request.expires)
            {
                if (request.kind == RequestKind::PersonalityGeneration ||
                    request.kind == RequestKind::NaturalCommand)
                {
                    ChatCompletion completion;
                    completion.request = std::move(request);
                    completion.error = completion.request.kind == RequestKind::NaturalCommand
                        ? "natural-command request expired in the request queue"
                        : "personality generation expired in the request queue";
                    std::lock_guard<std::mutex> lock(m_completionMutex);
                    m_completions.push_back(std::move(completion));
                    continue;
                }
                ++m_dropped;
                continue;
            }

            ++m_inFlight;
            ChatCompletion completion;
            completion.request = request;
            uint32_t retries = 0;
            uint32_t httpAttempts = 0;
            do
            {
                if (request.kind == RequestKind::NaturalCommand)
                {
                    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        request.expires - Clock::now()).count();
                    if (remaining <= 0)
                    {
                        completion.error = "natural-command request expired before provider execution";
                        break;
                    }
                    request.requestTimeoutMillisecondsOverride = static_cast<uint32_t>(
                        std::min<int64_t>(remaining, std::numeric_limits<uint32_t>::max()));
                    request.requestTimeoutSecondsOverride = static_cast<uint32_t>(
                        std::max<int64_t>(1, (remaining + 999) / 1000));
                }
                completion = Provider::Execute(*m_config, request);
                httpAttempts += completion.httpAttemptCount;
                bool retryable = !completion.success &&
                    (completion.httpStatus == 0 || completion.httpStatus == 429 || completion.httpStatus >= 500);
                uint32_t const retryMaximum = request.kind == RequestKind::NaturalCommand
                    ? m_config->naturalCommandsRetryMaximum : m_config->retryMaximum;
                if (!retryable || retries >= retryMaximum || m_stopping)
                    break;
                ++retries;
                auto const backoff = std::chrono::milliseconds(m_config->retryBackoffMilliseconds * retries);
                if (request.kind == RequestKind::NaturalCommand && Clock::now() + backoff >= request.expires)
                    break;
                std::this_thread::sleep_for(backoff);
            } while (Clock::now() <= request.expires);
            completion.httpAttemptCount = httpAttempts;
            --m_inFlight;

            {
                std::lock_guard<std::mutex> lock(m_completionMutex);
                m_completions.push_back(std::move(completion));
            }
        }
    }

    void Manager::RecordPreflightRejection(PreflightReason reason)
    {
        if (!m_config || !m_config->consoleApiCallStats)
            return;
        size_t const index = static_cast<size_t>(reason);
        if (index < m_preflightRejections.size())
            ++m_preflightRejections[index];
    }

    bool Manager::CanEnqueueDialogue(ActorSnapshot const& actor, RequestPriority priority,
                                     bool ambient)
    {
        if (!m_started || m_stopping || m_paused || !m_config)
        {
            RecordPreflightRejection(PreflightReason::Unavailable);
            return false;
        }

        auto const now = Clock::now();
        uint32_t const cooldownSeconds = ambient
            ? m_config->ambientActorCooldownSeconds : m_config->actorCooldownSeconds;
        auto cooldown = m_actorCooldowns.find(actor.guid);
        if (priority != RequestPriority::Direct && cooldown != m_actorCooldowns.end() &&
            cooldown->second > now)
        {
            RecordPreflightRejection(PreflightReason::Cooldown);
            return false;
        }

        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (!m_requestBudget.empty() && now - m_requestBudget.front() >= std::chrono::minutes(1))
            m_requestBudget.pop_front();
        if (m_requestBudget.size() >= m_config->globalRequestsPerMinute)
        {
            RecordPreflightRejection(PreflightReason::RateLimit);
            return false;
        }

        auto latest = m_latestRequestByActor.find(actor.guid);
        if (latest != m_latestRequestByActor.end())
        {
            RequestPriority existing = RequestPriority::Ambient;
            bool found = false;
            for (size_t i = 0; i < m_queues.size() && !found; ++i)
                for (ChatRequest const& queued : m_queues[i])
                    if (queued.kind == RequestKind::Dialogue && queued.id == latest->second)
                    {
                        existing = queued.priority;
                        found = true;
                        break;
                    }
            if (found && static_cast<uint8_t>(priority) <= static_cast<uint8_t>(existing))
            {
                RecordPreflightRejection(PreflightReason::Superseded);
                return false;
            }
        }

        size_t queuedCount = 0;
        for (auto const& queue : m_queues)
            queuedCount += queue.size();
        size_t const normalLimit = m_config->queueMaximum - m_config->highPriorityReserve;
        bool const highPriority = priority == RequestPriority::Direct ||
            priority == RequestPriority::Group;
        bool const canDisplaceAmbient = highPriority && !m_queues[0].empty();
        if (((!highPriority && queuedCount >= normalLimit) ||
             queuedCount >= m_config->queueMaximum) && !canDisplaceAmbient)
        {
            RecordPreflightRejection(PreflightReason::QueueFull);
            return false;
        }
        return true;
    }

    bool Manager::PreflightDialogue(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                    ChatScope scope, std::string const& channelName,
                                    std::string const& trigger, RequestPriority priority,
                                    bool ambient)
    {
        if (!m_config || !IsScopeEnabled(*m_config, scope))
        {
            RecordPreflightRejection(PreflightReason::InvalidScope);
            return false;
        }

        if (actor.kind == ActorKind::Creature)
        {
            bool const sayChatReaction = scope == ChatScope::Say &&
                (trigger == "direct-chat" || trigger == "overheard-chat" ||
                 trigger.compare(0, 13, "targeted-npc-") == 0);
            bool const legalNpcTrigger = ambient || trigger.compare(0, 6, "event:") == 0 ||
                sayChatReaction;
            if (!m_config->npcReplies || scope != ChatScope::Say || !legalNpcTrigger)
            {
                RecordPreflightRejection(PreflightReason::InvalidScope);
                return false;
            }

            Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(actor.anchorPlayerGuid));
            Creature* creature = anchor && anchor->IsInWorld() && anchor->GetMapId() == actor.mapId
                ? ObjectAccessor::GetCreature(*anchor, ObjectGuid(actor.guid)) : nullptr;
            if (!creature || creature->GetName() != actor.name)
            {
                RecordPreflightRejection(PreflightReason::InvalidActor);
                return false;
            }

            NpcEligibilityResult const eligibility = EvaluateNpcSpeaker(
                creature, m_config->sayDistance, *m_config);
            if (eligibility != NpcEligibilityResult::Eligible)
            {
                switch (eligibility)
                {
                    case NpcEligibilityResult::Temporary:
                        RecordPreflightRejection(PreflightReason::NpcTemporary);
                        break;
                    case NpcEligibilityResult::Neutral:
                        RecordPreflightRejection(PreflightReason::NpcNeutral);
                        break;
                    case NpcEligibilityResult::Hostile:
                        RecordPreflightRejection(PreflightReason::NpcHostile);
                        break;
                    case NpcEligibilityResult::NoHumanNearby:
                        RecordPreflightRejection(PreflightReason::NoHumanNearby);
                        break;
                    default:
                        RecordPreflightRejection(PreflightReason::InvalidActor);
                        break;
                }
                return false;
            }
            if (m_config->disableRepliesInCombat && creature->IsInCombat())
            {
                RecordPreflightRejection(PreflightReason::Combat);
                return false;
            }
        }
        else
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(actor.guid));
            if (!bot || !bot->IsInWorld() || !bot->IsAlive() ||
                !Script_IsAIControlled(bot) || bot->GetName() != actor.name)
            {
                RecordPreflightRejection(PreflightReason::InvalidActor);
                return false;
            }
            if (m_config->disableRepliesInCombat && bot->IsInCombat())
            {
                RecordPreflightRejection(PreflightReason::Combat);
                return false;
            }

            if (scope == ChatScope::Say || scope == ChatScope::Yell)
            {
                float const observerDistance = scope == ChatScope::Yell
                    ? m_config->yellDistance : m_config->sayDistance;
                if (!HasNearbyRealPlayer(bot, observerDistance))
                {
                    RecordPreflightRejection(PreflightReason::NoHumanNearby);
                    return false;
                }
            }
            else if (scope == ChatScope::Whisper)
            {
                Player* receiver = ObjectAccessor::FindPlayer(ObjectGuid(speaker.guid));
                if (!IsOnlineRealPlayer(receiver))
                {
                    RecordPreflightRejection(PreflightReason::NoAudience);
                    return false;
                }
            }
            else
            {
                std::string const audienceChannel = channelName.empty()
                    ? m_config->worldChannelName : channelName;
                if (!HasRealPlayerAudience(bot, scope, audienceChannel, 0.0f))
                {
                    RecordPreflightRejection(PreflightReason::NoAudience);
                    return false;
                }
            }
        }

        return CanEnqueueDialogue(actor, priority, ambient);
    }

    bool Manager::QueueDialogue(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                ChatScope scope, std::string const& channelName,
                                std::string const& trigger, std::string const& message,
                                RequestPriority priority, bool ambient, bool allowFollowup,
                                uint32_t conversationDepth)
    {
        if (!PreflightDialogue(actor, speaker, scope, channelName, trigger, priority, ambient))
            return false;

        ChatRequest request = BuildRequest(actor, speaker, scope, channelName, trigger,
            message, priority, ambient, allowFollowup);
        request.conversationDepth = conversationDepth;
        if (Enqueue(std::move(request)))
            return true;

        // Enqueue repeats the queue invariants for safety. This should only be
        // reachable if lifecycle state changed after the world-thread preflight.
        RecordPreflightRejection(PreflightReason::Unavailable);
        return false;
    }

    bool Manager::Enqueue(ChatRequest request)
    {
        if (!m_started || m_stopping || m_paused || !m_config)
            return false;

        auto const now = Clock::now();
        bool const personalityRequest = request.kind == RequestKind::PersonalityGeneration;
        bool const naturalCommandRequest = request.kind == RequestKind::NaturalCommand;
        uint32_t cooldownSeconds = request.ambient
            ? m_config->ambientActorCooldownSeconds : m_config->actorCooldownSeconds;
        auto cooldown = m_actorCooldowns.find(request.actor.guid);
        if (!personalityRequest && !naturalCommandRequest &&
            request.priority != RequestPriority::Direct &&
            cooldown != m_actorCooldowns.end() && cooldown->second > now)
        {
            ++m_dropped;
            return false;
        }

        ActorSnapshot personalityActor;
        bool queueMissingPersonality = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            while (!m_requestBudget.empty() && now - m_requestBudget.front() >= std::chrono::minutes(1))
                m_requestBudget.pop_front();
            if (m_requestBudget.size() >= m_config->globalRequestsPerMinute)
            {
                ++m_dropped;
                return false;
            }

            if (naturalCommandRequest)
            {
                for (uint64_t guid : NaturalCommandRecipientGuids(request))
                {
                    auto pending = m_pendingNaturalCommandsByActor.find(guid);
                    if (pending != m_pendingNaturalCommandsByActor.end() &&
                        pending->second.size() >= m_config->naturalCommandsMaximumPendingPerBot)
                    {
                        ++m_dropped;
                        return false;
                    }
                }
            }

            if (!personalityRequest && !naturalCommandRequest)
            {
                auto latest = m_latestRequestByActor.find(request.actor.guid);
                if (latest != m_latestRequestByActor.end())
                {
                    RequestPriority existing = RequestPriority::Ambient;
                    bool found = false;
                    for (size_t i = 0; i < m_queues.size() && !found; ++i)
                        for (ChatRequest const& queued : m_queues[i])
                            if (queued.kind == RequestKind::Dialogue && queued.id == latest->second)
                            {
                                existing = queued.priority;
                                found = true;
                                break;
                            }
                    if (found && static_cast<uint8_t>(request.priority) <= static_cast<uint8_t>(existing))
                    {
                        ++m_dropped;
                        return false;
                    }
                }
            }

            size_t queuedCount = 0;
            for (auto const& queue : m_queues)
                queuedCount += queue.size();
            size_t normalLimit = m_config->queueMaximum - m_config->highPriorityReserve;
            bool highPriority = request.priority == RequestPriority::Direct ||
                request.priority == RequestPriority::Group;
            if ((!highPriority && queuedCount >= normalLimit) || queuedCount >= m_config->queueMaximum)
            {
                if (highPriority && !m_queues[0].empty())
                {
                    ChatRequest dropped = std::move(m_queues[0].back());
                    m_queues[0].pop_back();
                    if (dropped.kind == RequestKind::PersonalityGeneration)
                    {
                        auto pending = m_pendingPersonalityRequests.find(dropped.actor.guid);
                        if (pending != m_pendingPersonalityRequests.end() && pending->second == dropped.id)
                            m_pendingPersonalityRequests.erase(pending);
                        RecordPersonalityGenerationStatus(dropped.actor, "dropped", dropped.id,
                            "The queued personality request was displaced by higher-priority work.");
                    }
                    else
                    {
                        if (dropped.kind == RequestKind::NaturalCommand)
                            RemoveNaturalCommandPending(
                                m_pendingNaturalCommandsByActor, dropped);
                        else
                        {
                            auto oldLatest = m_latestRequestByActor.find(dropped.actor.guid);
                            if (oldLatest != m_latestRequestByActor.end() && oldLatest->second == dropped.id)
                                m_latestRequestByActor.erase(oldLatest);
                        }
                    }
                    ++m_dropped;
                }
                else
                {
                    ++m_dropped;
                    return false;
                }
            }

            if (!request.id)
                request.id = m_nextRequestId++;
            request.created = now;
            request.expires = now + std::chrono::seconds(naturalCommandRequest
                ? m_config->naturalCommandsRequestTtlSeconds : m_config->requestTtlSeconds);
            if (naturalCommandRequest)
                for (uint64_t guid : NaturalCommandRecipientGuids(request))
                    m_pendingNaturalCommandsByActor[guid].push_back(request.id);
            else if (!personalityRequest)
                m_latestRequestByActor[request.actor.guid] = request.id;
            size_t const priorityIndex = static_cast<size_t>(request.priority);
            queueMissingPersonality = !personalityRequest && !naturalCommandRequest &&
                request.personalityGenerationNeeded;
            if (queueMissingPersonality)
                personalityActor = request.actor;
            m_queues[priorityIndex].push_back(std::move(request));
            m_requestBudget.push_back(now);
            if (!personalityRequest && !naturalCommandRequest)
                m_actorCooldowns[m_queues[priorityIndex].back().actor.guid] =
                    now + std::chrono::seconds(cooldownSeconds);
            ++m_accepted;
            m_queueReady.notify_one();
        }

        if (queueMissingPersonality)
            QueuePersonalityGeneration(personalityActor, false);
        return true;
    }

    ChatRequest Manager::BuildRequest(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                      ChatScope scope, std::string const& channelName,
                                      std::string const& trigger, std::string const& message,
                                      RequestPriority priority, bool ambient, bool allowFollowup)
    {
        ChatRequest request;
        request.id = m_nextRequestId++;
        request.priority = priority;
        request.actor = actor;
        if (!m_config->personalityEnabled)
            request.actor.talentBuild.clear();
        request.speaker = speaker;
        request.scope = scope;
        request.channelName = channelName;
        request.trigger = trigger;
        request.incomingMessage = message;
        request.ambient = ambient;
        request.allowFollowup = allowFollowup;
        request.historyKey = HistoryKey(*m_config, actor, speaker, scope, channelName);
        request.scopeKey = ScopeKey(scope, channelName, actor, speaker);

        if (actor.kind == ActorKind::PlayerBot && m_config->personalityEnabled)
        {
            BotPersonality personality;
            if (LoadPersonality(actor, personality))
            {
                request.personality = std::move(personality);
                if (request.personality.backgroundMode != m_config->personalityBackgroundMode)
                    request.personality.background.clear();
                request.personalityBlock = BuildPersonalityPromptBlock(*m_config, request.personality);
            }
            else if (m_config->personalityGenerateOnDemand &&
                     !m_pendingPersonalityRequests.count(actor.guid))
            {
                auto retry = m_personalityRetryAfter.find(actor.guid);
                request.personalityGenerationNeeded = retry == m_personalityRetryAfter.end() ||
                    retry->second <= Clock::now();
            }
        }

        std::string rolePrompt = actor.kind == ActorKind::Creature ? m_config->rpgPrompt : m_config->prePrompt;
        bool const personalityPlaceholder = m_config->globalPrompt.find("<bot personality block>") != std::string::npos ||
            rolePrompt.find("<bot personality block>") != std::string::npos;
        bool const specializationPlaceholder = m_config->globalPrompt.find("<bot specialization>") != std::string::npos ||
            rolePrompt.find("<bot specialization>") != std::string::npos;
        request.systemPrompt = m_config->globalPrompt;
        if (!rolePrompt.empty())
            request.systemPrompt += "\n" + rolePrompt;
        if (actor.kind == ActorKind::Creature)
        {
            if (actor.disposition == "friendly")
                request.systemPrompt += "\nYour disposition for this line is friendly. Speak warmly, cooperatively, or respectfully while remaining in character.";
            else if (actor.disposition == "hostile")
                request.systemPrompt += "\nYour disposition for this line is hostile. Speak in an unfriendly, suspicious, dismissive, or threatening way while remaining in character. Express hostility through dialogue only; do not narrate or invent combat actions.";
            else
                request.systemPrompt += "\nYour disposition for this line is neutral. Speak in a reserved, matter-of-fact way, neither warm nor threatening, while remaining in character.";
        }
        request.systemPrompt += "\nThe reply will be sent through " +
            (channelName.empty() ? ScopeName(scope) : channelName) +
            ". Keep it concise and return dialogue only. Treat the current message and current live environment "
            "as authoritative. Older conversation, snapshot history, and retrieved lore are optional context: "
            "use only details directly relevant to the current reply, do not assume old state is still true, and "
            "do not invent facts to reconcile stale or conflicting context.";

        if (trigger == "generated-followup")
            request.userPrompt = "Reply naturally to this line: " + message;
        else if (ambient)
            request.userPrompt = "Create one natural line now. Situation or topic: " + message;
        else if (trigger.compare(0, 6, "event:") == 0)
            request.userPrompt = "React naturally to this in-game event: " + message;
        else
            request.userPrompt = m_config->prompt;
        if (!m_config->postPrompt.empty())
            request.userPrompt += "\n" + m_config->postPrompt;

        request.systemPrompt = Expand(request.systemPrompt, request);
        request.userPrompt = Expand(request.userPrompt, request);
        if (actor.kind == ActorKind::PlayerBot && !request.personalityBlock.empty() && !personalityPlaceholder)
            request.systemPrompt += "\n" + request.personalityBlock;
        if (m_config->personalityEnabled && actor.kind == ActorKind::PlayerBot &&
            !request.actor.talentBuild.empty() && !specializationPlaceholder)
            request.systemPrompt += "\nCurrent talent specialization: " + request.actor.talentBuild + '.';
        std::string const currentEnvironment = BuildEnvironmentContext(request);
        std::string const history = BuildHistoryContext(request);
        std::string const surrounding = BuildSurroundingContext(request);
        request.currentSnapshot = BuildCurrentSnapshotContext(request);
        std::string const snapshotHistory = BuildSnapshotHistoryContext(request);
        std::string rag = SelectRag(request);
        if (!rag.empty())
        {
            std::string block = m_config->ragPromptTemplate;
            ReplaceAll(block, "{rag_info}", rag);
            ReplaceAll(block, "\\n", "\n");
            rag = std::move(block);
        }

        // Allocate the fixed context budget by importance. The final ordering
        // keeps the authoritative live snapshot closest to the new user turn.
        size_t remaining = m_config->contextLength > request.personalityBlock.size()
            ? m_config->contextLength - request.personalityBlock.size() : 0;
        auto reserveHead = [&](std::string const& value) {
            std::string selected = HeadBounded(value, remaining);
            remaining -= selected.size();
            return selected;
        };
        auto reserveTail = [&](std::string const& value) {
            std::string selected = TailBounded(value, remaining);
            remaining -= selected.size();
            return selected;
        };
        std::string keptCurrent = reserveHead(currentEnvironment);
        std::string keptSnapshot = reserveHead(request.currentSnapshot);
        std::string keptHistory = reserveTail(history);
        std::string keptSurrounding = reserveTail(surrounding);
        std::string keptSnapshotHistory = reserveTail(snapshotHistory);
        std::string keptRag = reserveHead(rag);
        auto appendBlock = [&](std::string const& block) {
            if (!block.empty())
                request.context += (request.context.empty() ? "" : "\n\n") + block;
        };
        appendBlock(keptHistory);
        appendBlock(keptSurrounding);
        appendBlock(keptSnapshotHistory);
        appendBlock(keptRag);
        appendBlock(keptCurrent);
        appendBlock(keptSnapshot);
        if (request.context.size() > m_config->contextLength)
            request.context.erase(0, request.context.size() - m_config->contextLength);
        return request;
    }

    std::vector<Manager::Candidate> Manager::CollectCandidates(Player* speaker, ChatScope scope,
        std::string const& targetName, std::string const& message, bool ambient, bool allowNpcs,
        uint64_t excludedActor, uint32_t guildIdOverride,
        WorldObject const* dispositionTarget)
    {
        std::vector<Candidate> result;
        if (!speaker || !speaker->IsInWorld() || !m_config)
            return result;

        bool const speakerIsBot = Script_IsAIControlled(speaker);
        ObjectGuid const selected = speaker->GetSelectionGuid();
        std::string const targetLower = Lower(targetName);
        std::string const messageLower = Lower(message);
        float const nearbyDistance = scope == ChatScope::Yell
            ? m_config->yellDistance : m_config->sayDistance;
        WorldObject const* npcDispositionTarget = dispositionTarget && dispositionTarget->IsInWorld() &&
            dispositionTarget->GetMapId() == speaker->GetMapId()
                ? dispositionTarget : static_cast<WorldObject const*>(speaker);

        // A real player's Say while explicitly selecting an eligible NPC uses
        // a separate, tightly local selection policy. The selected NPC is
        // considered first; other NPCs and PlayerBots may join using their own
        // configured chances, but every participant starts within NPC.Distance
        // of the human speaker.
        Creature* selectedNpc = nullptr;
        bool targetedNpcConversation = false;
        if (!ambient && allowNpcs && m_config->npcReplies && scope == ChatScope::Say &&
            IsOnlineRealPlayer(speaker) && selected.IsCreature())
        {
            selectedNpc = ObjectAccessor::GetCreature(*speaker, selected);
            targetedNpcConversation = selectedNpc &&
                speaker->IsWithinDist(selectedNpc, m_config->npcDistance, false) &&
                EvaluateNpcSpeaker(selectedNpc, m_config->sayDistance, *m_config) ==
                    NpcEligibilityResult::Eligible &&
                (!m_config->disableRepliesInCombat || !selectedNpc->IsInCombat());
        }

        auto chanceFor = [&](std::string const& actorName, ObjectGuid actorGuid, bool npc) -> std::pair<uint32_t, int>
        {
            bool const direct = scope == ChatScope::Whisper || (!selected.IsEmpty() && selected == actorGuid);
            bool const mentioned = !actorName.empty() && messageLower.find(Lower(actorName)) != std::string::npos;
            if (ambient)
                return { 100, 10 };
            if (targetedNpcConversation)
            {
                if (npc && actorGuid == selected)
                    return { m_config->targetedNpcReplyChance, 1000 };
                return { npc ? m_config->targetedNpcJoinChance
                             : m_config->targetedNpcPlayerBotJoinChance, 20 };
            }

            uint32_t chance = m_config->overhearChance;
            int score = 10;
            if (direct)
            {
                chance = m_config->directAddressChance;
                score = 100;
            }
            else if (mentioned)
            {
                chance = m_config->nameMentionChance;
                score = 80;
            }
            else if (npc)
            {
                chance = m_config->overhearChance;
            }
            else
            {
                switch (scope)
                {
                    case ChatScope::Say:
                    case ChatScope::Yell:
                        chance = speakerIsBot ? m_config->botReplyChanceSay : m_config->playerReplyChanceSay;
                        break;
                    case ChatScope::Party:
                    case ChatScope::Raid:
                        chance = speakerIsBot ? m_config->botReplyChanceParty : m_config->playerReplyChanceParty;
                        break;
                    case ChatScope::Guild:
                    case ChatScope::Officer:
                        chance = speakerIsBot ? m_config->botReplyChanceGuild : m_config->playerReplyChanceGuild;
                        break;
                    case ChatScope::Channel:
                    case ChatScope::World:
                        chance = speakerIsBot ? m_config->botReplyChanceChannel : m_config->playerReplyChanceChannel;
                        break;
                    case ChatScope::Whisper:
                        chance = 100;
                        break;
                }
            }

            if (speakerIsBot)
                chance = std::min(chance, npc
                    ? m_config->rpgAiChatChance : m_config->botToBotChatChance);
            return { chance, score };
        };

        std::vector<Player*> onlinePlayers;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
                if (entry.second)
                    onlinePlayers.push_back(entry.second);
        }
        for (Player* bot : onlinePlayers)
        {
            if (bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                !Script_IsAIControlled(bot) || bot->GetObjectGuid().GetRawValue() == excludedActor)
                continue;
            if (m_config->disableRepliesInCombat && bot->IsInCombat())
            {
                RecordPreflightRejection(PreflightReason::Combat);
                continue;
            }

            bool eligible = false;
            switch (scope)
            {
                case ChatScope::Whisper:
                    eligible = !targetLower.empty() && Lower(bot->GetName()) == targetLower;
                    break;
                case ChatScope::Say:
                case ChatScope::Yell:
                {
                    float const candidateDistance = targetedNpcConversation
                        ? m_config->npcDistance : nearbyDistance;
                    eligible = bot->GetMapId() == speaker->GetMapId() &&
                        speaker->IsWithinDist(bot, candidateDistance, false);
                    break;
                }
                case ChatScope::Party:
                    eligible = speaker->GetGroup() && bot->GetGroup() == speaker->GetGroup() &&
                        speaker->GetGroup()->SameSubGroup(speaker, bot);
                    break;
                case ChatScope::Raid:
                    eligible = speaker->GetGroup() && bot->GetGroup() == speaker->GetGroup();
                    break;
                case ChatScope::Guild:
                {
                    uint32_t const guildId = guildIdOverride
                        ? guildIdOverride : speaker->GetGuildId();
                    eligible = guildId && bot->GetGuildId() == guildId;
                    break;
                }
                case ChatScope::Officer:
                {
                    uint32_t const guildId = guildIdOverride
                        ? guildIdOverride : speaker->GetGuildId();
                    Guild* guild = guildId ? sGuildMgr.GetGuildById(guildId) : nullptr;
                    eligible = guild && bot->GetGuildId() == guildId &&
                        guild->HasRankRight(bot->GetRank(), GR_RIGHT_OFFCHATSPEAK);
                    break;
                }
                case ChatScope::Channel:
                case ChatScope::World:
                    eligible = true;
                    break;
            }
            if (!eligible)
                continue;

            if (scope == ChatScope::Say || scope == ChatScope::Yell)
            {
                float const observerDistance = scope == ChatScope::Yell
                    ? m_config->yellDistance : m_config->sayDistance;
                if (!HasNearbyRealPlayer(bot, observerDistance))
                {
                    RecordPreflightRejection(PreflightReason::NoHumanNearby);
                    continue;
                }
            }

            auto chanceAndScore = chanceFor(bot->GetName(), bot->GetObjectGuid(), false);
            if (!Roll(chanceAndScore.first))
                continue;
            Candidate candidate;
            candidate.actor = SnapshotBot(bot);
            candidate.chance = chanceAndScore.first;
            candidate.score = chanceAndScore.second + static_cast<int>(RandomUInt(0, 9));
            candidate.targetedNpcConversation = targetedNpcConversation;
            result.push_back(std::move(candidate));
        }

        if (allowNpcs && m_config->npcReplies && scope == ChatScope::Say)
        {
            float const distance = m_config->npcDistance;
            MaNGOS::AllCreaturesInRange check(speaker, distance);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
            Cell::VisitGridObjects(speaker, searcher, distance);

            for (Creature* creature : creatures)
            {
                if (!creature || creature->GetObjectGuid().GetRawValue() == excludedActor)
                    continue;
                NpcEligibilityResult const eligibility = EvaluateNpcSpeaker(
                    creature, m_config->sayDistance, *m_config);
                if (eligibility != NpcEligibilityResult::Eligible)
                {
                    switch (eligibility)
                    {
                        case NpcEligibilityResult::Temporary:
                            RecordPreflightRejection(PreflightReason::NpcTemporary);
                            break;
                        case NpcEligibilityResult::Neutral:
                            RecordPreflightRejection(PreflightReason::NpcNeutral);
                            break;
                        case NpcEligibilityResult::Hostile:
                            RecordPreflightRejection(PreflightReason::NpcHostile);
                            break;
                        case NpcEligibilityResult::NoHumanNearby:
                            RecordPreflightRejection(PreflightReason::NoHumanNearby);
                            break;
                        default:
                            RecordPreflightRejection(PreflightReason::InvalidActor);
                            break;
                    }
                    continue;
                }
                if (m_config->disableRepliesInCombat && creature->IsInCombat())
                {
                    RecordPreflightRejection(PreflightReason::Combat);
                    continue;
                }

                auto chanceAndScore = chanceFor(creature->GetName(), creature->GetObjectGuid(), true);
                NpcDisposition const disposition = ClassifyNpcDisposition(
                    creature, npcDispositionTarget);
                uint32_t const dispositionChance = NpcDispositionReplyChance(
                    disposition, *m_config);
                if (!Roll(chanceAndScore.first) || !Roll(dispositionChance))
                    continue;
                Candidate candidate;
                candidate.actor = SnapshotCreature(creature, speaker, disposition);
                candidate.chance = static_cast<uint32_t>(
                    (static_cast<uint64_t>(chanceAndScore.first) * dispositionChance) / 100);
                candidate.score = chanceAndScore.second + static_cast<int>(RandomUInt(0, 9));
                candidate.targetedNpcConversation = targetedNpcConversation;
                candidate.selectedNpcTarget = targetedNpcConversation &&
                    creature->GetObjectGuid() == selected;
                result.push_back(std::move(candidate));
            }
        }

        std::sort(result.begin(), result.end(), [](Candidate const& left, Candidate const& right) {
            return left.score > right.score;
        });
        return result;
    }

    void Manager::HandleChat(Player* speaker, ChatScope scope, std::string const& message,
                             std::string const& targetName, std::string const& channelName)
    {
        if (!m_started || !speaker || message.empty())
            return;
        InboundSignal signal;
        signal.kind = InboundSignal::Kind::Chat;
        signal.playerGuid = speaker->GetObjectGuid().GetRawValue();
        signal.scope = scope;
        signal.message = message;
        signal.targetName = targetName;
        signal.channelName = channelName;
        std::lock_guard<std::mutex> lock(m_ingressMutex);
        if (m_ingress.size() >= 2048)
        {
            ++m_dropped;
            if (m_config)
                sLog.outError("[AzerothVoices] World-thread ingress queue is full; chat signal dropped.");
            return;
        }
        m_ingress.push_back(std::move(signal));
    }

    bool Manager::NaturalCommandTargetValid(Player* speaker, Player* bot, ChatScope scope,
                                            std::string const& addressing) const
    {
        if (!m_config || !IsOnlineRealPlayer(speaker) || !bot || bot == speaker ||
            !bot->IsInWorld() || !PlayerbotBridge::IsControlled(bot))
            return false;
        if (m_config->naturalCommandsMasterOnly && PlayerbotBridge::Master(bot) != speaker)
            return false;

        switch (scope)
        {
            case ChatScope::Whisper:
                return true;
            case ChatScope::Say:
                return bot->GetMapId() == speaker->GetMapId() &&
                    speaker->IsWithinDist(bot, m_config->sayDistance, false) &&
                    (addressing != "selected" || speaker->GetSelectionGuid() == bot->GetObjectGuid());
            case ChatScope::Yell:
                return addressing == "named" && bot->GetMapId() == speaker->GetMapId() &&
                    speaker->IsWithinDist(bot, m_config->yellDistance, false);
            case ChatScope::Party:
                return (addressing == "named" || addressing == "party-single") &&
                    speaker->GetGroup() &&
                    bot->GetGroup() == speaker->GetGroup() &&
                    speaker->GetGroup()->SameSubGroup(speaker, bot);
            case ChatScope::Raid:
                return addressing == "named" && speaker->GetGroup() &&
                    bot->GetGroup() == speaker->GetGroup();
            case ChatScope::Guild:
                return addressing == "named" && speaker->GetGuildId() &&
                    bot->GetGuildId() == speaker->GetGuildId();
            case ChatScope::Officer:
            {
                Guild* guild = speaker->GetGuildId()
                    ? sGuildMgr.GetGuildById(speaker->GetGuildId()) : nullptr;
                return addressing == "named" && guild &&
                    bot->GetGuildId() == speaker->GetGuildId() &&
                    guild->HasRankRight(bot->GetRank(), GR_RIGHT_OFFCHATLISTEN);
            }
            case ChatScope::World:
            case ChatScope::Channel:
                return addressing == "named";
        }
        return false;
    }

    std::vector<Player*> Manager::ResolveNaturalCommandBots(Player* speaker, ChatScope scope,
                                                            std::string const& message,
                                                            std::string const& targetName,
                                                            std::string& addressing,
                                                            std::string& addressedMessage) const
    {
        std::vector<Player*> result;
        addressing.clear();
        addressedMessage = message;
        if (!m_config || !IsOnlineRealPlayer(speaker))
            return result;

        if (scope == ChatScope::Whisper)
        {
            Player* bot = targetName.empty()
                ? nullptr : ObjectAccessor::FindPlayerByName(targetName.c_str());
            if (!NaturalCommandTargetValid(speaker, bot, scope, "whisper"))
            {
                if (bot && PlayerbotBridge::IsControlled(bot))
                    addressing = "unauthorized";
                return result;
            }
            addressing = "whisper";
            if (ContainsExplicitName(message, bot->GetName()))
                addressedMessage = RemoveExplicitName(message, bot->GetName());
            result.push_back(bot);
            return result;
        }

        std::vector<Player*> mentionedBots;
        std::set<uint64_t> seen;
        std::string token;
        auto considerToken = [&]() {
            if (token.size() >= 2 && token.size() <= 32)
            {
                Player* bot = ObjectAccessor::FindPlayerByName(token.c_str());
                if (bot && bot != speaker && bot->IsInWorld() &&
                    PlayerbotBridge::IsControlled(bot) &&
                    ContainsExplicitName(message, bot->GetName()) &&
                    seen.insert(bot->GetObjectGuid().GetRawValue()).second)
                    mentionedBots.push_back(bot);
            }
            token.clear();
        };
        for (unsigned char c : message)
        {
            if (std::isalnum(c) || c >= 0x80)
                token.push_back(static_cast<char>(c));
            else
                considerToken();
        }
        considerToken();

        if (!mentionedBots.empty())
        {
            addressedMessage = message;
            for (Player* bot : mentionedBots)
                addressedMessage = RemoveExplicitName(addressedMessage, bot->GetName());
            addressedMessage = Trim(addressedMessage);
            while (Lower(addressedMessage).compare(0, 4, "and ") == 0)
                addressedMessage = Trim(addressedMessage.substr(4));
            while (!addressedMessage.empty() && (addressedMessage.front() == ',' ||
                addressedMessage.front() == ':' || addressedMessage.front() == '-' ||
                addressedMessage.front() == '@'))
                addressedMessage = Trim(addressedMessage.substr(1));
            if (mentionedBots.size() > m_config->naturalCommandsMaximumRecipients)
            {
                addressing = "too-many";
                return result;
            }
            for (Player* bot : mentionedBots)
            {
                if (!NaturalCommandTargetValid(speaker, bot, scope, "named"))
                {
                    addressing = "unauthorized";
                    return {};
                }
            }
            addressing = "named";
            return mentionedBots;
        }

        if (scope == ChatScope::Party && speaker->GetGroup())
        {
            for (GroupReference* reference = speaker->GetGroup()->GetFirstMember();
                reference; reference = reference->next())
            {
                Player* bot = reference->getSource();
                if (!NaturalCommandTargetValid(speaker, bot, scope, "party-single"))
                    continue;
                result.push_back(bot);
                if (result.size() > 1)
                {
                    addressing = "ambiguous-party";
                    return {};
                }
            }
            if (result.size() == 1)
            {
                addressing = "party-single";
                return result;
            }
        }

        if (scope == ChatScope::Say)
        {
            Player* selected = speaker->GetSelectedPlayer();
            if (NaturalCommandTargetValid(speaker, selected, scope, "selected"))
            {
                addressing = "selected";
                result.push_back(selected);
                return result;
            }
            if (selected && PlayerbotBridge::IsControlled(selected))
                addressing = "unauthorized";
        }
        return result;
    }

    void Manager::SendNaturalCommandFeedback(Player* speaker, std::string const& message) const
    {
        if (!m_config || !m_config->naturalCommandsFeedbackEnabled || !IsOnlineRealPlayer(speaker))
            return;
        std::string const safe = HeadBounded(SanitizeLogText(message), 500);
        ChatHandler(speaker).SendSysMessage(("[AzerothVoices] " + safe).c_str());
    }

    void Manager::RejectNaturalCommand(Player* speaker, std::string const& reason)
    {
        ++m_naturalRejected;
        ++m_naturalTelemetry.rejected;
        m_naturalLastFailure = HeadBounded(SanitizeLogText(reason), 120);
        SendNaturalCommandFeedback(speaker, reason);
    }

    void Manager::RecordNaturalCommandAudit(uint64_t playerGuid, uint64_t botGuid,
        std::string const& action, std::string const& arguments,
        std::string const& source, std::string const& result,
        double confidence, uint64_t requestId, uint32_t latencyMilliseconds)
    {
        if (!m_config || !m_config->naturalCommandsAuditEnabled)
            return;
        NaturalCommandAuditRecord record;
        record.createdUnix = static_cast<uint64_t>(std::time(nullptr));
        record.requestId = requestId;
        record.playerGuid = playerGuid;
        record.botGuid = botGuid;
        record.action = HeadBounded(SanitizeLogText(action), 80);
        if (m_config->naturalCommandsAuditIncludeArguments)
            record.arguments = HeadBounded(SanitizeLogText(arguments), 300);
        record.source = HeadBounded(SanitizeLogText(source), 24);
        record.result = HeadBounded(SanitizeLogText(result), 80);
        record.confidence = std::max(0.0, std::min(1.0, confidence));
        record.latencyMilliseconds = latencyMilliseconds;
        m_naturalCommandAudit.push_back(std::move(record));
        while (m_naturalCommandAudit.size() > m_config->naturalCommandsAuditMaximumRecords)
            m_naturalCommandAudit.pop_front();
    }

    std::string Manager::NaturalCommandMostUsedActions(size_t maximum) const
    {
        std::vector<std::pair<std::string, uint64_t>> ranked(
            m_naturalActionUsage.begin(), m_naturalActionUsage.end());
        std::sort(ranked.begin(), ranked.end(), [](auto const& left, auto const& right) {
            if (left.second != right.second)
                return left.second > right.second;
            return left.first < right.first;
        });
        std::ostringstream result;
        for (size_t i = 0; i < ranked.size() && i < maximum; ++i)
        {
            if (i)
                result << ',';
            result << ranked[i].first << ':' << ranked[i].second;
        }
        return result.str();
    }

    void Manager::ScheduleNaturalCommandAcknowledgement(
        ChatRequest const& request, std::string const& acknowledgement)
    {
        if (!m_config || m_config->naturalCommandsAcknowledgementMode != "generated")
            return;
        std::string const text = Trim(acknowledgement);
        if (text.empty())
            return;
        ScheduledLine line;
        line.request = request;
        line.request.kind = RequestKind::NaturalCommand;
        line.request.allowFollowup = false;
        line.request.ambient = false;
        line.request.expires = Clock::now() + std::chrono::seconds(5);
        line.text = HeadBounded(text, 240);
        line.due = Clock::now();
        line.firstLine = true;
        m_scheduled.push_back(std::move(line));
    }

    bool Manager::ExecuteNaturalCommand(Player* speaker, Player* bot,
                                        std::string const& action,
                                        std::string const& arguments,
                                        ChatScope scope,
                                        std::string const& addressing,
                                        bool confirmed,
                                        bool sendFeedback,
                                        std::string const& source,
                                        double confidence,
                                        uint64_t requestId,
                                        uint32_t latencyMilliseconds)
    {
        if (!m_config || !speaker || !bot)
            return false;
        NaturalCommandAction const* definition = FindNaturalCommandAction(action);
        std::string const normalizedAction = NormalizeNaturalCommandAction(action);
        uint64_t const speakerGuid = speaker->GetObjectGuid().GetRawValue();
        uint64_t const botGuid = bot->GetObjectGuid().GetRawValue();
        auto audit = [&](std::string const& result) {
            RecordNaturalCommandAudit(speakerGuid, botGuid, normalizedAction, arguments,
                source, result, confidence, requestId, latencyMilliseconds);
        };
        if (!definition || definition->forbidden ||
            !m_config->naturalCommandsAllowedActions.count(normalizedAction))
        {
            audit("not-allowed");
            RejectNaturalCommand(speaker, "Action is not allowed by the module.");
            return false;
        }

        if (!NaturalCommandTargetValid(speaker, bot, scope, addressing))
        {
            audit("recipient-ineligible");
            RejectNaturalCommand(speaker, "The PlayerBot is no longer an eligible command recipient.");
            return false;
        }

        std::string safeArguments;
        std::string validationError;
        if (!ValidateNaturalCommandArguments(normalizedAction, Trim(arguments),
            !speaker->GetSelectionGuid().IsEmpty(), {}, safeArguments, validationError))
        {
            audit("argument-rejected");
            RejectNaturalCommand(speaker, "Command rejected: " + validationError + ".");
            return false;
        }

        std::string command = normalizedAction;
        if (!safeArguments.empty())
            command += ' ' + safeArguments;
        std::string const separator = PlayerbotBridge::CommandSeparator();
        if (!separator.empty() && command.find(separator) != std::string::npos)
        {
            audit("separator-rejected");
            RejectNaturalCommand(speaker, "Command rejected because it contains the PlayerBots command separator.");
            return false;
        }

        NaturalCommandMetadata const metadata = GetNaturalCommandMetadata(*definition);
        if (metadata.confirmationRequired && m_config->naturalCommandsConfirmationEnabled && !confirmed)
        {
            PendingNaturalConfirmation pending;
            pending.speakerGuid = speakerGuid;
            pending.botGuids.push_back(bot->GetObjectGuid().GetRawValue());
            pending.actions.push_back({ normalizedAction, safeArguments });
            pending.scope = scope;
            pending.addressing = addressing;
            pending.source = source;
            pending.requestId = requestId;
            pending.latencyMilliseconds = latencyMilliseconds;
            pending.expires = Clock::now() +
                std::chrono::seconds(m_config->naturalCommandsConfirmationTtlSeconds);
            m_pendingNaturalConfirmations[speakerGuid] = std::move(pending);
            std::string exact = normalizedAction;
            if (!safeArguments.empty())
                exact += " " + safeArguments;
            SendNaturalCommandFeedback(speaker, "Confirmation required for '" +
                HeadBounded(SanitizeLogText(exact), 300) + "'. Address the same bot with 'confirm' within " +
                std::to_string(m_config->naturalCommandsConfirmationTtlSeconds) + " seconds, or use 'cancel'.");
            ++m_naturalTelemetry.confirmationRequired;
            audit("confirmation-required");
            return true;
        }

        if (!PlayerbotBridge::Dispatch(speaker, bot, command))
        {
            audit("dispatch-boundary-rejected");
            RejectNaturalCommand(speaker, "PlayerBots rejected the dispatch boundary.");
            return false;
        }
        ++m_naturalDispatched;
        ++m_naturalActionUsage[normalizedAction];
        audit(confirmed && metadata.confirmationRequired ? "dispatched-confirmed" : "dispatched");
        m_naturalLastFailure.clear();
        if (sendFeedback)
            SendNaturalCommandFeedback(speaker, "Dispatched '" +
                HeadBounded(SanitizeLogText(command), 300) + "' to " +
                SanitizeLogText(bot->GetName()) + ". PlayerBots will report whether the action succeeds.");
        if (m_config->debug)
            sLog.outDebug("[AzerothVoices][NaturalCommands] Dispatched action '%s' to %s for %s.",
                normalizedAction.c_str(), SanitizeLogText(bot->GetName()).c_str(),
                SanitizeLogText(speaker->GetName()).c_str());
        return true;
    }

    bool Manager::ExecuteNaturalCommandBatch(Player* speaker,
        std::vector<Player*> const& bots,
        std::vector<PendingNaturalConfirmation::Action> const& actions,
        ChatScope scope, std::string const& addressing, bool confirmed,
        std::string const& source, uint64_t requestId,
        uint32_t latencyMilliseconds, std::string const& acknowledgement,
        ChatRequest const* acknowledgementRequest)
    {
        if (!m_config || !speaker || bots.empty() || actions.empty() ||
            bots.size() > m_config->naturalCommandsMaximumRecipients ||
            actions.size() > m_config->naturalCommandsMaximumActions)
            return false;

        bool needsConfirmation = false;
        for (Player* bot : bots)
            if (!NaturalCommandTargetValid(speaker, bot, scope, addressing))
            {
                RejectNaturalCommand(speaker, "A PlayerBot is no longer an eligible command recipient.");
                return false;
            }
        for (PendingNaturalConfirmation::Action const& item : actions)
        {
            NaturalCommandAction const* definition = FindNaturalCommandAction(item.action);
            if (!definition || definition->forbidden ||
                !m_config->naturalCommandsAllowedActions.count(
                    NormalizeNaturalCommandAction(item.action)))
            {
                RejectNaturalCommand(speaker, "An action is not allowed by the module.");
                return false;
            }
            needsConfirmation = needsConfirmation ||
                GetNaturalCommandMetadata(*definition).confirmationRequired;
        }

        if (needsConfirmation && m_config->naturalCommandsConfirmationEnabled && !confirmed)
        {
            PendingNaturalConfirmation pending;
            pending.speakerGuid = speaker->GetObjectGuid().GetRawValue();
            for (Player* bot : bots)
                pending.botGuids.push_back(bot->GetObjectGuid().GetRawValue());
            pending.actions = actions;
            pending.scope = scope;
            pending.addressing = addressing;
            pending.source = source;
            pending.requestId = requestId;
            pending.latencyMilliseconds = latencyMilliseconds;
            pending.acknowledgement = acknowledgement;
            if (acknowledgementRequest)
                pending.acknowledgementRequest = *acknowledgementRequest;
            pending.expires = Clock::now() +
                std::chrono::seconds(m_config->naturalCommandsConfirmationTtlSeconds);
            m_pendingNaturalConfirmations[pending.speakerGuid] = std::move(pending);
            ++m_naturalTelemetry.confirmationRequired;
            for (Player* bot : bots)
                for (PendingNaturalConfirmation::Action const& action : actions)
                    RecordNaturalCommandAudit(speaker->GetObjectGuid().GetRawValue(),
                        bot->GetObjectGuid().GetRawValue(), action.action, action.arguments,
                        source, "confirmation-required", action.confidence, requestId,
                        latencyMilliseconds);
            SendNaturalCommandFeedback(speaker, "Confirmation required for a batch of " +
                std::to_string(actions.size()) + " action(s) across " +
                std::to_string(bots.size()) + " PlayerBot(s). Address the same bot(s) with 'confirm' within " +
                std::to_string(m_config->naturalCommandsConfirmationTtlSeconds) +
                " seconds, or use 'cancel'.");
            return true;
        }

        bool const generatedAcknowledgement =
            m_config->naturalCommandsAcknowledgementMode == "generated" &&
            source == "llm" && !acknowledgement.empty() && acknowledgementRequest;
        bool const localAcknowledgement =
            m_config->naturalCommandsAcknowledgementMode == "local" ||
            (m_config->naturalCommandsAcknowledgementMode == "generated" && source != "llm");
        bool const individualFeedback = localAcknowledgement &&
            bots.size() == 1 && actions.size() == 1;
        for (PendingNaturalConfirmation::Action const& item : actions)
            for (Player* bot : bots)
                if (!ExecuteNaturalCommand(speaker, bot, item.action, item.arguments,
                    scope, addressing, true, individualFeedback, source, item.confidence,
                    requestId, latencyMilliseconds))
                    return false;
        if (localAcknowledgement && !individualFeedback)
            SendNaturalCommandFeedback(speaker, "Dispatched " +
                std::to_string(actions.size() * bots.size()) + " PlayerBots command(s): " +
                std::to_string(actions.size()) + " action(s) across " +
                std::to_string(bots.size()) +
                " recipient(s). PlayerBots will report whether each action succeeds.");
        if (generatedAcknowledgement)
            ScheduleNaturalCommandAcknowledgement(*acknowledgementRequest, acknowledgement);
        return true;
    }

    bool Manager::QueueNaturalCommandInterpretation(Player* speaker,
                                                    std::vector<Player*> const& bots,
                                                    ChatScope scope,
                                                    std::string const& channelName,
                                                    std::string const& addressing,
                                                    std::string const& message)
    {
        if (!m_config || !speaker || bots.empty())
            return false;

        std::vector<std::string> links;
        std::string const protectedMessage = PreserveNaturalCommandLinks(message, links);
        std::string shortlistInput = protectedMessage;
        if (!speaker->GetSelectionGuid().IsEmpty())
            shortlistInput += " target";
        if (scope == ChatScope::Guild || scope == ChatScope::Officer)
            shortlistInput += " guild";
        else if (scope == ChatScope::Party || scope == ChatScope::Raid)
            shortlistInput += " group";
        std::set<std::string> const shortlist = ShortlistNaturalCommandActions(
            m_config->naturalCommandsAllowedActions, shortlistInput,
            m_config->naturalCommandsShortlistMaximum,
            m_config->naturalCommandsPromoteFrequentlyUsedActions
                ? &m_naturalActionUsage : nullptr);
        if (shortlist.empty())
            return false;

        ChatRequest request;
        request.kind = RequestKind::NaturalCommand;
        request.priority = RequestPriority::Direct;
        request.actor = SnapshotBot(bots.front());
        request.speaker = SnapshotSpeaker(speaker);
        request.scope = scope;
        request.channelName = channelName;
        request.trigger = "natural-command-" + addressing;
        request.incomingMessage = message;
        request.naturalPreservedLinks = links;
        request.naturalAllowedActions.assign(shortlist.begin(), shortlist.end());
        for (Player* bot : bots)
        {
            request.naturalRecipientGuids.push_back(bot->GetObjectGuid().GetRawValue());
            request.naturalRecipientNames.push_back(bot->GetName());
        }
        request.naturalMaximumActions = LooksLikeMultipleNaturalCommands(
            message, PlayerbotBridge::CommandSeparator())
            ? m_config->naturalCommandsMaximumActions : 1;
        bool const generatedAcknowledgement =
            m_config->naturalCommandsAcknowledgementMode == "generated";
        request.systemPrompt = BuildNaturalCommandClassifierPrompt(shortlist,
            request.naturalMaximumActions, generatedAcknowledgement);
        std::ostringstream recipientNames;
        for (size_t i = 0; i < request.naturalRecipientNames.size(); ++i)
        {
            if (i)
                recipientNames << ", ";
            recipientNames << request.naturalRecipientNames[i];
        }
        request.userPrompt = std::string("Addressed PlayerBots: ") + recipientNames.str() +
            "\nChat scope: " + ScopeName(scope) +
            "\nPlayer message: " + protectedMessage;
        request.modelOverride = m_config->naturalCommandsModel;
        request.maxTokensOverride = request.naturalMaximumActions > 1 ? 256 :
            (protectedMessage.size() > 300 ? 256 :
            (!links.empty() || protectedMessage.size() > 120 ? 160 : 96));
        request.requestTimeoutSecondsOverride = m_config->naturalCommandsRequestTtlSeconds;
        request.temperatureOverride = 0.0f;
        size_t const promptCharacters = request.systemPrompt.size() + request.userPrompt.size();
        if (m_config->debug)
            sLog.outDebug("[AzerothVoices][NaturalCommands] Classifier shortlist=%u prompt-characters=%u.",
                static_cast<unsigned>(shortlist.size()),
                static_cast<unsigned>(promptCharacters));
        if (!Enqueue(std::move(request)))
        {
            RejectNaturalCommand(speaker, "Natural-command queue is full.");
            return false;
        }
        ++m_naturalClassifierQueued;
        ++m_naturalTelemetry.classifierQueued;
        m_naturalShortlistActions += shortlist.size();
        m_naturalTelemetry.shortlistActions += shortlist.size();
        m_naturalPromptCharacters += promptCharacters;
        m_naturalTelemetry.promptCharacters += promptCharacters;
        return true;
    }

    bool Manager::TryHandleNaturalCommand(Player* speaker, ChatScope scope,
                                          std::string const& message,
                                          std::string const& targetName,
                                          std::string const& channelName)
    {
        if (!m_config || !IsOnlineRealPlayer(speaker) || message.empty())
            return false;

        NaturalCommandAction const* rawAction = FindNaturalCommandPrefix(message);
        std::string const configuredPrefix = PlayerbotBridge::CommandPrefix();
        bool const startsConfiguredPrefix = !configuredPrefix.empty() &&
            message.compare(0, configuredPrefix.size(), configuredPrefix) == 0;
        // With no PlayerBots prefix, native syntax has already been delivered
        // by PlayerbotMgr. With a prefix, a prefixed native command has also
        // already been delivered. Consume it here so Azeroth Voices neither
        // duplicates the command nor turns it into LLM dialogue.
        if (rawAction && startsConfiguredPrefix)
            return true;

        if (rawAction && configuredPrefix.empty())
        {
            if (m_config->naturalCommandsEnabled)
                SendNaturalCommandFeedback(speaker,
                    "Native PlayerBots may already have processed this unprefixed command. "
                    "Azeroth Voices did not dispatch it; set AiPlayerbot.CommandPrefix = ! for reliable separation.");
            return true;
        }

        if (!m_config->naturalCommandsEnabled || m_config->naturalCommandsAllowedActions.empty())
            return false;

        std::string addressing;
        std::string addressedMessage;
        std::vector<Player*> bots = ResolveNaturalCommandBots(speaker, scope, message, targetName,
            addressing, addressedMessage);
        if (bots.empty())
        {
            if ((addressing == "too-many" || addressing == "unauthorized" ||
                    addressing == "ambiguous-party") &&
                (LooksLikeNaturalCommand(addressedMessage) ||
                    LooksLikeMultipleNaturalCommands(addressedMessage,
                        PlayerbotBridge::CommandSeparator())))
            {
                RejectNaturalCommand(speaker, addressing == "too-many"
                    ? "Command rejected because the number of named PlayerBots exceeds NaturalCommands.MaximumRecipients."
                    : (addressing == "ambiguous-party"
                        ? "Command rejected because more than one eligible PlayerBot is in the party subgroup; name the recipient."
                        : "Command rejected because the named PlayerBot is not an eligible owned recipient."));
                return true;
            }
            return false;
        }
        if (addressedMessage.empty())
            return false;

        bool const commandCandidate = FindNaturalCommandPrefix(addressedMessage) ||
            LooksLikeNaturalCommand(addressedMessage) ||
            ConservativeMultilingualCommandCandidate(addressedMessage, addressing);
        if (commandCandidate)
        {
            ++m_naturalConsidered;
            ++m_naturalTelemetry.considered;
        }
        if (NaturalCommandChannelExcluded(*m_config, scope, channelName))
        {
            if (commandCandidate)
            {
                RejectNaturalCommand(speaker, "Natural commands are excluded from this channel or scope.");
                return true;
            }
            return false;
        }
        if (message.size() > 500)
        {
            if (commandCandidate)
            {
                RejectNaturalCommand(speaker, "Natural command exceeds the 500-character input limit.");
                return true;
            }
            return false;
        }

        if (configuredPrefix.empty() && commandCandidate)
        {
            RejectNaturalCommand(speaker,
                "Natural command was not dispatched because the native PlayerBots prefix is empty. "
                "Set AiPlayerbot.CommandPrefix = ! to prevent native and interpreted commands from overlapping.");
            return true;
        }

        std::string const normalizedMessage = NormalizeNaturalPhrase(addressedMessage);
        uint64_t const speakerGuid = speaker->GetObjectGuid().GetRawValue();
        if (normalizedMessage == "cancel")
        {
            auto pending = m_pendingNaturalConfirmations.find(speakerGuid);
            if (pending != m_pendingNaturalConfirmations.end())
            {
                for (uint64_t botGuid : pending->second.botGuids)
                    for (PendingNaturalConfirmation::Action const& action : pending->second.actions)
                        RecordNaturalCommandAudit(speakerGuid, botGuid, action.action,
                            action.arguments, pending->second.source, "confirmation-cancelled",
                            action.confidence, pending->second.requestId,
                            pending->second.latencyMilliseconds);
                m_pendingNaturalConfirmations.erase(pending);
                ++m_naturalTelemetry.confirmationCancelled;
                SendNaturalCommandFeedback(speaker, "Pending natural command cancelled.");
            }
            else
                RejectNaturalCommand(speaker, "There is no pending natural command to cancel.");
            return true;
        }
        if (normalizedMessage == "confirm")
        {
            auto pending = m_pendingNaturalConfirmations.find(speakerGuid);
            if (pending == m_pendingNaturalConfirmations.end())
            {
                RejectNaturalCommand(speaker, "There is no pending natural command to confirm.");
                return true;
            }
            PendingNaturalConfirmation confirmation = pending->second;
            m_pendingNaturalConfirmations.erase(pending);
            if (Clock::now() > confirmation.expires)
            {
                ++m_naturalExpired;
                ++m_naturalTelemetry.confirmationExpired;
                for (uint64_t botGuid : confirmation.botGuids)
                    for (PendingNaturalConfirmation::Action const& action : confirmation.actions)
                        RecordNaturalCommandAudit(speakerGuid, botGuid, action.action,
                            action.arguments, confirmation.source, "confirmation-expired",
                            action.confidence, confirmation.requestId,
                            confirmation.latencyMilliseconds);
                RejectNaturalCommand(speaker, "Natural-command confirmation expired.");
                return true;
            }
            std::vector<uint64_t> resolvedGuids;
            for (Player* bot : bots)
                resolvedGuids.push_back(bot->GetObjectGuid().GetRawValue());
            std::sort(resolvedGuids.begin(), resolvedGuids.end());
            std::vector<uint64_t> confirmedGuids = confirmation.botGuids;
            std::sort(confirmedGuids.begin(), confirmedGuids.end());
            if (confirmedGuids != resolvedGuids)
            {
                RejectNaturalCommand(speaker, "Confirmation does not match the same PlayerBot recipient set.");
                return true;
            }
            ++m_naturalTelemetry.confirmationConfirmed;
            ChatRequest const* acknowledgementRequest = confirmation.acknowledgement.empty()
                ? nullptr : &confirmation.acknowledgementRequest;
            ExecuteNaturalCommandBatch(speaker, bots, confirmation.actions,
                confirmation.scope, confirmation.addressing, true,
                confirmation.source, confirmation.requestId,
                confirmation.latencyMilliseconds, confirmation.acknowledgement,
                acknowledgementRequest);
            return true;
        }

        bool const multipleActions = LooksLikeMultipleNaturalCommands(
            addressedMessage, PlayerbotBridge::CommandSeparator());
        if (multipleActions && m_config->naturalCommandsMaximumActions <= 1)
        {
            RejectNaturalCommand(speaker, "Use one natural-language action per message.");
            return true;
        }

        std::string localAction;
        std::string localArguments;
        if (!multipleActions && m_config->naturalCommandsLocalFastPath &&
            FindLocalNaturalCommand(addressedMessage,
                m_config->naturalCommandsAllowedActions, localAction, localArguments))
        {
            ++m_naturalLocalFastPath;
            ++m_naturalTelemetry.localFastPath;
            ExecuteNaturalCommandBatch(speaker, bots, { { localAction, localArguments } },
                scope, addressing, false, "local");
            return true;
        }

        NaturalCommandAction const* addressedAction = FindNaturalCommandPrefix(addressedMessage);
        if (addressedAction && addressedAction->forbidden)
        {
            ++m_naturalLocalFastPath;
            ++m_naturalTelemetry.localFastPath;
            RejectNaturalCommand(speaker, "That action is permanently forbidden for interpreted commands.");
            return true;
        }
        if (addressedAction && !m_config->naturalCommandsAllowedActions.count(addressedAction->name))
        {
            RejectNaturalCommand(speaker, "That action is not present in NaturalCommands.AllowedActions.");
            return true;
        }
        if (!multipleActions && m_config->naturalCommandsLocalFastPath && addressedAction &&
            m_config->naturalCommandsAllowedActions.count(addressedAction->name))
        {
            std::string commandText = Trim(addressedMessage);
            if (!configuredPrefix.empty() &&
                commandText.compare(0, configuredPrefix.size(), configuredPrefix) == 0)
                commandText = Trim(commandText.substr(configuredPrefix.size()));
            size_t const actionLength = std::char_traits<char>::length(addressedAction->name);
            std::string arguments = commandText.size() > actionLength
                ? Trim(commandText.substr(actionLength)) : "";
            ExecuteNaturalCommandBatch(speaker, bots,
                { { addressedAction->name, arguments } }, scope, addressing,
                false, "local");
            return true;
        }

        if (!commandCandidate)
            return false;
        if (m_config->naturalCommandsLlmFallback)
        {
            if (!QueueNaturalCommandInterpretation(speaker, bots, scope, channelName,
                addressing, addressedMessage))
                return true;
        }
        else
            RejectNaturalCommand(speaker, "Natural-command LLM fallback is disabled.");
        // Command-looking addressed input is never offered to NPC responders,
        // even if provider work is disabled, rate-limited, or unavailable.
        return true;
    }

    void Manager::HandleNaturalCommandCompletion(ChatCompletion const& completion)
    {
        Player* speaker = ObjectAccessor::FindPlayer(ObjectGuid(completion.request.speaker.guid));
        std::string const prefix = "natural-command-";
        std::string addressing = completion.request.trigger.compare(0, prefix.size(), prefix) == 0
            ? completion.request.trigger.substr(prefix.size()) : "";
        std::vector<uint64_t> recipientGuids = completion.request.naturalRecipientGuids;
        if (recipientGuids.empty())
            recipientGuids.push_back(completion.request.actor.guid);
        std::vector<Player*> bots;
        for (uint64_t guid : recipientGuids)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            if (!NaturalCommandTargetValid(speaker, bot, completion.request.scope, addressing))
            {
                if (IsOnlineRealPlayer(speaker))
                    RejectNaturalCommand(speaker,
                        "Natural command was dropped because a PlayerBot recipient is no longer eligible.");
                ++m_dropped;
                return;
            }
            bots.push_back(bot);
        }
        if (bots.empty())
        {
            ++m_dropped;
            return;
        }
        auto auditForBots = [&](std::string const& action, std::string const& arguments,
                                std::string const& result, double confidence) {
            for (Player* bot : bots)
                RecordNaturalCommandAudit(completion.request.speaker.guid,
                    bot->GetObjectGuid().GetRawValue(), action, arguments, "llm", result,
                    confidence, completion.request.id, completion.elapsedMilliseconds);
        };

        try
        {
            Json const decision = Json::parse(completion.responseText);
            uint32_t const maximumActions = std::max<uint32_t>(1,
                std::min<uint32_t>(3, completion.request.naturalMaximumActions));
            if (!decision.is_object() || !decision.count("kind") ||
                !decision["kind"].is_string())
                throw std::runtime_error("decision must contain a string kind");
            std::string const kind = Lower(Trim(decision["kind"].get<std::string>()));
            std::vector<PendingNaturalConfirmation::Action> actions;
            std::vector<double> confidences;
            bool const generatedAcknowledgement =
                m_config->naturalCommandsAcknowledgementMode == "generated";
            std::string acknowledgement;
            if (maximumActions == 1)
            {
                size_t const expectedFields = generatedAcknowledgement ? 5 : 4;
                if (decision.size() != expectedFields ||
                    !decision.count("action") || !decision["action"].is_string() ||
                    !decision.count("arguments") || !decision["arguments"].is_string() ||
                    !decision.count("confidence") || !decision["confidence"].is_number() ||
                    (generatedAcknowledgement && (!decision.count("acknowledgment") ||
                        !decision["acknowledgment"].is_string())))
                    throw std::runtime_error("decision contains an invalid single-action schema");
                actions.push_back({ NormalizeNaturalCommandAction(
                    decision["action"].get<std::string>()),
                    decision["arguments"].get<std::string>() });
                confidences.push_back(decision["confidence"].get<double>());
                if (generatedAcknowledgement)
                    acknowledgement = Trim(decision["acknowledgment"].get<std::string>());
            }
            else
            {
                size_t const expectedFields = generatedAcknowledgement ? 3 : 2;
                if (decision.size() != expectedFields || !decision.count("commands") ||
                    !decision["commands"].is_array() ||
                    decision["commands"].size() > maximumActions ||
                    (generatedAcknowledgement && (!decision.count("acknowledgment") ||
                        !decision["acknowledgment"].is_string())))
                    throw std::runtime_error("decision contains an invalid batch schema");
                for (Json const& item : decision["commands"])
                {
                    if (!item.is_object() || item.size() != 3 ||
                        !item.count("action") || !item["action"].is_string() ||
                        !item.count("arguments") || !item["arguments"].is_string() ||
                        !item.count("confidence") || !item["confidence"].is_number())
                        throw std::runtime_error("each command must contain exactly action, arguments, confidence");
                    actions.push_back({ NormalizeNaturalCommandAction(
                        item["action"].get<std::string>()),
                        item["arguments"].get<std::string>() });
                    confidences.push_back(item["confidence"].get<double>());
                }
                if (generatedAcknowledgement)
                    acknowledgement = Trim(decision["acknowledgment"].get<std::string>());
            }
            for (double confidence : confidences)
                if (!std::isfinite(confidence) || confidence < 0.0 || confidence > 1.0)
                    throw std::runtime_error("confidence must be finite and between zero and one");
            if (generatedAcknowledgement &&
                (acknowledgement.size() > 240 || acknowledgement.find('\r') != std::string::npos ||
                 acknowledgement.find('\n') != std::string::npos ||
                 acknowledgement.find('\0') != std::string::npos ||
                 acknowledgement.find("|H") != std::string::npos))
                throw std::runtime_error("acknowledgment exceeded its safe single-line bound");
            ++m_naturalClassified;

            if (kind == "conversation")
            {
                bool const empty = maximumActions == 1
                    ? actions.size() == 1 && actions.front().action.empty() &&
                        actions.front().arguments.empty()
                    : actions.empty();
                if (!empty)
                    throw std::runtime_error("conversation decision contained commands");
                if (generatedAcknowledgement && !acknowledgement.empty())
                    throw std::runtime_error("conversation decision contained an acknowledgment");
                ++m_naturalTelemetry.conversation;
                auditForBots("", "", "conversation", 0.0);
                QueueDialogue(completion.request.actor, completion.request.speaker,
                    completion.request.scope, completion.request.channelName,
                    "natural-command-conversation", completion.request.incomingMessage,
                    RequestPriority::Direct, false, false);
                ++m_completed;
                return;
            }
            if (kind == "unsupported")
            {
                bool const empty = maximumActions == 1
                    ? actions.size() == 1 && actions.front().action.empty() &&
                        actions.front().arguments.empty()
                    : actions.empty();
                if (!empty)
                    throw std::runtime_error("unsupported decision contained commands");
                if (generatedAcknowledgement && !acknowledgement.empty())
                    throw std::runtime_error("unsupported decision contained an acknowledgment");
                ++m_naturalTelemetry.unsupported;
                auditForBots("", "", "unsupported", 0.0);
                RejectNaturalCommand(speaker, "The instruction is unsupported or cannot be represented safely.");
                ++m_completed;
                return;
            }
            if (kind != "command")
                throw std::runtime_error("unknown command decision kind");
            if (actions.empty() || actions.size() > maximumActions)
                throw std::runtime_error("command decision contained no actions or exceeded its limit");
            if (generatedAcknowledgement && acknowledgement.empty())
                throw std::runtime_error("command decision omitted its acknowledgment");
            std::vector<std::string> sourceLinks;
            std::string const protectedSource = PreserveNaturalCommandLinks(
                completion.request.incomingMessage, sourceLinks);
            if (sourceLinks != completion.request.naturalPreservedLinks)
                throw std::runtime_error("source link placeholders changed");
            std::set<std::string> uniqueActions;
            for (size_t i = 0; i < actions.size(); ++i)
            {
                std::string const& action = actions[i].action;
                std::string const& arguments = actions[i].arguments;
                if (confidences[i] < m_config->naturalCommandsMinimumConfidence)
                {
                    ++m_naturalTelemetry.lowConfidence;
                    auditForBots(action, arguments, "low-confidence", confidences[i]);
                    RejectNaturalCommand(speaker, "Natural-command classification confidence was too low.");
                    return;
                }
                if (!uniqueActions.insert(action).second ||
                    std::find(completion.request.naturalAllowedActions.begin(),
                        completion.request.naturalAllowedActions.end(), action) ==
                        completion.request.naturalAllowedActions.end() ||
                    !m_config->naturalCommandsAllowedActions.count(action))
                {
                    auditForBots(action, arguments, "outside-shortlist", confidences[i]);
                    RejectNaturalCommand(speaker,
                        "Classifier returned a duplicate action or an action outside the authorized shortlist.");
                    return;
                }
                if (arguments.find("|H") != std::string::npos ||
                    LooksLikeMultipleNaturalCommands(action + " " + arguments,
                        PlayerbotBridge::CommandSeparator()))
                {
                    auditForBots(action, arguments, "embedded-command-rejected", confidences[i]);
                    RejectNaturalCommand(speaker,
                        "Classifier returned an invented link or embedded command chain.");
                    return;
                }
                if (!NaturalArgumentsComeFromMessage(arguments, protectedSource))
                {
                    auditForBots(action, arguments, "invented-argument", confidences[i]);
                    RejectNaturalCommand(speaker,
                        "Classifier returned an argument that was not present in the player's instruction.");
                    return;
                }
                std::string restoredArguments;
                std::string validationError;
                if (!ValidateNaturalCommandArguments(action, arguments,
                    !speaker->GetSelectionGuid().IsEmpty(),
                    completion.request.naturalPreservedLinks,
                    restoredArguments, validationError))
                {
                    auditForBots(action, arguments, "argument-rejected", confidences[i]);
                    RejectNaturalCommand(speaker, "Command rejected: " + validationError + ".");
                    return;
                }
                actions[i].arguments = std::move(restoredArguments);
                actions[i].confidence = confidences[i];
            }
            ChatRequest const* acknowledgementRequest = acknowledgement.empty()
                ? nullptr : &completion.request;
            if (!ExecuteNaturalCommandBatch(speaker, bots, actions,
                completion.request.scope, addressing, false, "llm",
                completion.request.id, completion.elapsedMilliseconds,
                acknowledgement, acknowledgementRequest))
                return;
            ++m_completed;
        }
        catch (std::exception const& exception)
        {
            ++m_failed;
            ++m_naturalTelemetry.invalidDecision;
            auditForBots("", "", "invalid-decision", 0.0);
            RejectNaturalCommand(speaker, "Provider returned an invalid natural-command decision.");
            if (m_config->debug)
                sLog.outError("[AzerothVoices][NaturalCommands] Request %llu returned an invalid decision: %s",
                    static_cast<unsigned long long>(completion.request.id), exception.what());
        }
    }

    void Manager::ProcessChat(Player* speaker, ChatScope scope, std::string const& message,
                              std::string const& targetName, std::string const& channelName)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !speaker ||
            message.empty())
            return;
        if (TryHandleNaturalCommand(speaker, scope, message, targetName, channelName))
            return;
        if (!IsScopeEnabled(*m_config, scope) ||
            IsBlockedChannel(*m_config, scope, channelName) || IsBlacklisted(*m_config, message))
            return;

        if (Script_IsAIControlled(speaker))
        {
            float const localDistance = scope == ChatScope::Yell
                ? m_config->yellDistance : m_config->sayDistance;
            std::string const audienceChannel = channelName.empty()
                ? m_config->worldChannelName : channelName;
            if (!HasRealPlayerAudience(speaker, scope, audienceChannel, localDistance))
            {
                RecordPreflightRejection(scope == ChatScope::Say || scope == ChatScope::Yell
                    ? PreflightReason::NoHumanNearby : PreflightReason::NoAudience);
                return;
            }
        }

        SpeakerSnapshot speakerSnapshot = SnapshotSpeaker(speaker);
        if (scope != ChatScope::Whisper)
            RecordSurroundingChat(scope, channelName, SnapshotBot(speaker), speakerSnapshot, message);

        auto now = Clock::now();
        if (scope != ChatScope::Whisper && m_config->speakerCooldownSeconds)
        {
            auto cooldown = m_speakerCooldowns.find(speaker->GetObjectGuid().GetRawValue());
            if (cooldown != m_speakerCooldowns.end() && cooldown->second > now)
                return;
        }

        bool const speakerIsBot = Script_IsAIControlled(speaker);
        // NPCs can answer both real players and PlayerBots in Say. NPC.Distance
        // limits candidate discovery; the per-NPC preflight also requires a
        // real human observer within SayDistance before prompt creation.
        bool const allowNpcReply = scope == ChatScope::Say;
        // Bot-originated dialogue may continue in every supported public/group
        // scope. Whisper remains human-directed and never starts AI-to-AI
        // follow-ups. MaybeQueueFollowup repeats the scope audience rules.
        bool const allowAiFollowup = speakerIsBot && scope != ChatScope::Whisper;
        std::vector<Candidate> candidates = CollectCandidates(
            speaker, scope, targetName, message, false, allowNpcReply);
        if (candidates.empty())
            return;
        if (scope == ChatScope::Whisper)
            RecordSurroundingChat(scope, channelName, candidates.front().actor, speakerSnapshot, message);

        bool accepted = false;
        uint32_t maximum = scope == ChatScope::Whisper ? 1 : m_config->maxResponders;
        for (size_t i = 0; i < candidates.size() && i < maximum; ++i)
        {
            bool direct = scope == ChatScope::Whisper || candidates[i].score >= 80 ||
                candidates[i].selectedNpcTarget;
            RequestPriority priority = direct ? RequestPriority::Direct :
                ((scope == ChatScope::Party || scope == ChatScope::Raid || scope == ChatScope::Guild || scope == ChatScope::Officer)
                    ? RequestPriority::Group : RequestPriority::Nearby);
            std::string trigger = direct ? "direct-chat" : "overheard-chat";
            if (candidates[i].targetedNpcConversation)
                trigger = candidates[i].selectedNpcTarget
                    ? "targeted-npc-direct" : "targeted-npc-join";
            accepted = QueueDialogue(candidates[i].actor, speakerSnapshot, scope, channelName,
                trigger, message, priority, false, allowAiFollowup) || accepted;
        }
        if (accepted && scope != ChatScope::Whisper && m_config->speakerCooldownSeconds)
            m_speakerCooldowns[speaker->GetObjectGuid().GetRawValue()] =
                now + std::chrono::seconds(m_config->speakerCooldownSeconds);
    }

    void Manager::HandleEvent(Player* subject, std::string const& eventName,
                              std::string const& detail, uint32_t guildId)
    {
        if (!m_started || !subject)
            return;
        InboundSignal signal;
        signal.kind = InboundSignal::Kind::Event;
        signal.playerGuid = subject->GetObjectGuid().GetRawValue();
        signal.eventName = eventName;
        signal.message = detail;
        signal.guildId = guildId;
        std::lock_guard<std::mutex> lock(m_ingressMutex);
        if (m_ingress.size() >= 2048)
        {
            ++m_dropped;
            return;
        }
        m_ingress.push_back(std::move(signal));
    }

    void Manager::DrainIngress()
    {
        std::deque<InboundSignal> signals;
        {
            std::lock_guard<std::mutex> lock(m_ingressMutex);
            signals.swap(m_ingress);
        }
        for (InboundSignal const& signal : signals)
        {
            Player* player = ObjectAccessor::FindPlayer(ObjectGuid(signal.playerGuid));
            if (!player || !player->IsInWorld())
                continue;
            if (signal.kind == InboundSignal::Kind::Chat)
                ProcessChat(player, signal.scope, signal.message, signal.targetName, signal.channelName);
            else
                ProcessEvent(player, signal.eventName, signal.message, signal.guildId);
        }
    }

    void Manager::ProcessEvent(Player* subject, std::string const& eventName,
                               std::string const& detail, uint32_t guildId)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !m_config->eventChatterEnabled ||
            !subject || !subject->IsInWorld())
            return;

        std::string event = Lower(eventName);
        auto eventChance = m_config->eventChances.find(event);
        if (eventChance == m_config->eventChances.end() || !Roll(eventChance->second))
            return;
        std::string cooldownKey = event + ':' + std::to_string(subject->GetObjectGuid().GetRawValue());
        auto now = Clock::now();
        auto cooldown = m_eventCooldowns.find(cooldownKey);
        if (cooldown != m_eventCooldowns.end() && cooldown->second > now)
            return;
        m_eventCooldowns[cooldownKey] = now + std::chrono::seconds(m_config->eventCooldownSeconds);

        std::string description = event;
        ReplaceAll(description, "_", " ");
        if (!detail.empty())
            description += ": " + detail;
        SpeakerSnapshot subjectSnapshot = SnapshotSpeaker(subject);

        bool const subjectIsBot = Script_IsAIControlled(subject);
        uint32_t const eventGuildId = guildId ? guildId : subject->GetGuildId();
        static std::set<std::string> const guildEvents = {
            "guild_demotion", "guild_promotion", "guild_login", "guild_leave",
            "guild_join", "level_up"
        };
        bool guildEvent = guildEvents.count(event) != 0;
        if (event == "level_up" && !eventGuildId)
            guildEvent = false;

        if (guildEvent)
        {
            // Guild membership hooks copy the guild id into ingress so a
            // guild-leave reaction still targets the guild after the core has
            // cleared the subject's membership. No real guild listener means
            // no prompt and therefore no provider call.
            if (!eventGuildId || !IsScopeEnabled(*m_config, ChatScope::Guild) ||
                !FindOnlineRealGuildAudience(eventGuildId, ChatScope::Guild))
            {
                RecordPreflightRejection(PreflightReason::NoAudience);
                return;
            }

            if (subjectIsBot && subject->GetGuildId() == eventGuildId &&
                Roll(m_config->eventSelfCommentChance))
            {
                QueueDialogue(SnapshotBot(subject), subjectSnapshot, ChatScope::Guild, "",
                    "event:" + event, description, RequestPriority::Group, false, true);
            }

            if (!Roll(m_config->eventResponderChance))
                return;
            std::vector<Candidate> candidates = CollectCandidates(subject, ChatScope::Guild, "",
                description, true, false, subject->GetObjectGuid().GetRawValue(), eventGuildId);
            uint32_t count = 0;
            for (Candidate const& candidate : candidates)
            {
                if (count >= m_config->eventMaximumResponders)
                    break;
                if (QueueDialogue(candidate.actor, subjectSnapshot, ChatScope::Guild, "",
                    "event:" + event, description, RequestPriority::Group, false, true))
                    ++count;
            }
            return;
        }

        // Non-guild events prefer Party for PlayerBots in the subject's party
        // subgroup. Say is the local fallback and is also the only event scope
        // available to NPC responders.
        if (subjectIsBot && Roll(m_config->eventSelfCommentChance))
        {
            bool const hasPartyAudience = IsScopeEnabled(*m_config, ChatScope::Party) &&
                FindOnlineRealGroupAudience(subject, ChatScope::Party);
            ChatScope const selfScope = hasPartyAudience ? ChatScope::Party : ChatScope::Say;
            QueueDialogue(SnapshotBot(subject), subjectSnapshot, selfScope, "",
                "event:" + event, description,
                hasPartyAudience ? RequestPriority::Group : RequestPriority::Nearby,
                false, true);
        }

        if (!Roll(m_config->eventResponderChance))
            return;

        uint32_t count = 0;
        std::set<uint64_t> selectedActors;
        if (IsScopeEnabled(*m_config, ChatScope::Party) && subject->GetGroup())
        {
            std::vector<Candidate> partyCandidates = CollectCandidates(subject, ChatScope::Party,
                "", description, true, false, subject->GetObjectGuid().GetRawValue());
            for (Candidate const& candidate : partyCandidates)
            {
                if (count >= m_config->eventMaximumResponders)
                    break;
                if (QueueDialogue(candidate.actor, subjectSnapshot, ChatScope::Party, "",
                    "event:" + event, description, RequestPriority::Group, false, true))
                {
                    selectedActors.insert(candidate.actor.guid);
                    ++count;
                }
            }
        }

        if (count >= m_config->eventMaximumResponders ||
            !IsScopeEnabled(*m_config, ChatScope::Say))
            return;
        std::vector<Candidate> sayCandidates = CollectCandidates(subject, ChatScope::Say, "",
            description, true, true, subject->GetObjectGuid().GetRawValue());
        for (Candidate const& candidate : sayCandidates)
        {
            if (count >= m_config->eventMaximumResponders)
                break;
            if (selectedActors.count(candidate.actor.guid))
                continue;
            if (QueueDialogue(candidate.actor, subjectSnapshot, ChatScope::Say, "",
                "event:" + event, description, RequestPriority::Nearby, false, true))
                ++count;
        }
    }

    bool Manager::ForceAmbient(Player* anchor, std::string const& instruction)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !m_config->randomChatterEnabled)
            return false;

        if (!anchor)
        {
            std::vector<Player*> realPlayers;
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (player && player->IsInWorld() && player->IsAlive() && !Script_IsAIControlled(player))
                    realPlayers.push_back(player);
            }
            if (realPlayers.empty())
                return false;
            anchor = realPlayers[RandomUInt(0, static_cast<uint32_t>(realPlayers.size() - 1))];
        }
        if (!IsOnlineRealPlayer(anchor) || !anchor->IsAlive())
        {
            RecordPreflightRejection(PreflightReason::NoAudience);
            return false;
        }

        ChatScope scope = ChatScope::Say;
        if (!m_config->randomScopes.empty())
            scope = ParseScope(Pick(m_config->randomScopes));
        if ((scope == ChatScope::Guild || scope == ChatScope::Officer) && !anchor->GetGuildId())
            scope = ChatScope::Say;
        if ((scope == ChatScope::Party || scope == ChatScope::Raid) && !anchor->GetGroup())
            scope = ChatScope::Say;
        if (scope == ChatScope::Raid && !anchor->GetGroup()->IsRaidGroup())
            scope = ChatScope::Say;
        if (!IsScopeEnabled(*m_config, scope))
            scope = ChatScope::Say;

        std::string topic = instruction;
        if (topic.empty())
        {
            uint32_t category = RandomUInt(0, 2);
            if (scope == ChatScope::Guild)
                topic = Pick(m_config->guildPrompts);
            else if (scope == ChatScope::World || scope == ChatScope::Channel)
                topic = Pick(m_config->worldPrompts);
            else if (category == 0)
                topic = Pick(m_config->randomPrompts);
            else if (category == 1)
                topic = Pick(m_config->randomQuestions);
            else
                topic = Pick(m_config->environmentPrompts);
        }
        if (topic.empty())
            topic = "Make a brief natural comment about the current situation.";

        std::vector<Candidate> candidates = CollectCandidates(anchor, scope, "", topic, true,
            true);
        if (candidates.empty() && scope != ChatScope::Say)
        {
            scope = ChatScope::Say;
            candidates = CollectCandidates(anchor, scope, "", topic, true, true);
        }
        if (candidates.empty())
            return false;

        // NPC ambient speech is intentionally started only when a second nearby
        // actor exists, so a lone quest giver does not continually monologue.
        if (candidates.front().actor.kind == ActorKind::Creature && candidates.size() < 2)
        {
            auto bot = std::find_if(candidates.begin(), candidates.end(), [](Candidate const& candidate) {
                return candidate.actor.kind == ActorKind::PlayerBot;
            });
            if (bot == candidates.end())
                return false;
            std::iter_swap(candidates.begin(), bot);
        }

        std::string channel = (scope == ChatScope::World || scope == ChatScope::Channel)
            ? m_config->worldChannelName : "";
        return QueueDialogue(candidates.front().actor, SnapshotSpeaker(anchor), scope, channel,
            "ambient", topic, RequestPriority::Ambient, true, true);
    }

    void Manager::RunAmbient()
    {
        if (ForceAmbient(nullptr) && m_config->debug)
            sLog.outDebug("[AzerothVoices] Queued ambient chatter.");
    }

    bool Manager::QueueTest(Player* requester, std::string const& actorName, std::string const& instruction)
    {
        if (!requester || !m_started || !m_config || !m_config->enabled)
            return false;

        ActorSnapshot actor;
        if (!actorName.empty())
        {
            Player* bot = ObjectAccessor::FindPlayerByName(actorName.c_str());
            if (!bot || !bot->IsInWorld() || !Script_IsAIControlled(bot))
                return false;
            actor = SnapshotBot(bot);
        }
        else
        {
            std::vector<Candidate> candidates = CollectCandidates(requester, ChatScope::Say, "",
                instruction, true, false);
            if (candidates.empty())
                return false;
            actor = candidates.front().actor;
        }

        std::string prompt = instruction.empty() ? "Reply exactly: Azeroth Voices test successful." : instruction;
        return QueueDialogue(actor, SnapshotSpeaker(requester), ChatScope::Whisper, "",
            "gm-test", prompt, RequestPriority::Direct, false, false);
    }

    bool Manager::IsPersonalityCurrent(BotPersonality const& personality) const
    {
        if (!m_config || !personality.characterGuid ||
            personality.generationVersion != PersonalityGenerationVersion ||
            personality.traits.size() != m_config->personalityTraitCount ||
            personality.backgroundMode > 1)
            return false;
        if (m_config->personalityGenerateTone && personality.tone.empty())
            return false;
        if (m_config->personalityGenerateBackground &&
            (personality.background.empty() ||
             personality.backgroundMode != m_config->personalityBackgroundMode))
            return false;
        return true;
    }

    void Manager::CachePersonality(BotPersonality personality)
    {
        uint64_t const guid = personality.characterGuid;
        if (!guid)
            return;
        m_personalityCacheOrder.erase(std::remove(m_personalityCacheOrder.begin(),
            m_personalityCacheOrder.end(), guid), m_personalityCacheOrder.end());
        m_personalityCacheOrder.push_back(guid);
        m_personalities[guid] = std::move(personality);
        while (m_personalities.size() > MaximumPersonalityCacheEntries && !m_personalityCacheOrder.empty())
        {
            uint64_t const expired = m_personalityCacheOrder.front();
            m_personalityCacheOrder.pop_front();
            m_personalities.erase(expired);
            m_databaseLoadedPersonalityGuids.erase(expired);
        }
    }

    bool Manager::LoadPersonality(ActorSnapshot const& actor, BotPersonality& personality,
                                  bool requireCurrent)
    {
        if (!m_config || actor.kind != ActorKind::PlayerBot || !actor.guid)
            return false;

        auto cached = m_personalities.find(actor.guid);
        if (cached != m_personalities.end())
        {
            if (!requireCurrent || IsPersonalityCurrent(cached->second))
            {
                personality = cached->second;
                CachePersonality(personality);
                return true;
            }
            m_personalities.erase(cached);
            m_personalityCacheOrder.erase(std::remove(m_personalityCacheOrder.begin(),
                m_personalityCacheOrder.end(), actor.guid), m_personalityCacheOrder.end());
        }

        if (m_databaseLoadedPersonalityGuids.count(actor.guid))
            return false;
        m_databaseLoadedPersonalityGuids.insert(actor.guid);
        while (m_databaseLoadedPersonalityGuids.size() > MaximumPersonalityCacheEntries)
            m_databaseLoadedPersonalityGuids.erase(m_databaseLoadedPersonalityGuids.begin());
        if (!m_personalityDatabaseAvailable)
            return false;

        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `bot_name`,`traits_json`,`tone`,`background`,`background_mode`,`generation_version`,"
            "UNIX_TIMESTAMP(`created_at`),UNIX_TIMESTAMP(`updated_at`) "
            "FROM `azeroth_voices_bot_personality` WHERE `character_guid`='%llu' LIMIT 1",
            static_cast<unsigned long long>(actor.guid)));
        if (!result)
            return false;

        Field* fields = result->Fetch();
        BotPersonality loaded;
        loaded.characterGuid = actor.guid;
        loaded.botName = fields[0].GetCppString();
        if (!ParseStoredPersonalityTraits(fields[1].GetCppString(), loaded.traits))
        {
            sLog.outError("[AzerothVoices][PERSONALITY] Stored traits for %s are invalid; regeneration is required.",
                SanitizeLogText(actor.name).c_str());
            return false;
        }
        loaded.tone = fields[2].GetCppString();
        loaded.background = fields[3].GetCppString();
        loaded.backgroundMode = fields[4].GetUInt32();
        loaded.generationVersion = fields[5].GetUInt32();
        loaded.createdUnix = fields[6].GetUInt64();
        loaded.updatedUnix = fields[7].GetUInt64();
        if (requireCurrent && !IsPersonalityCurrent(loaded))
            return false;
        CachePersonality(loaded);
        personality = std::move(loaded);
        return true;
    }

    bool Manager::QueuePersonalityGeneration(ActorSnapshot const& actor, bool forced)
    {
        if (!m_started || !m_config || !m_config->personalityEnabled ||
            actor.kind != ActorKind::PlayerBot || !actor.guid ||
            (!forced && !m_config->personalityGenerateOnDemand) ||
            m_pendingPersonalityRequests.count(actor.guid))
            return false;
        auto const now = Clock::now();
        auto retry = m_personalityRetryAfter.find(actor.guid);
        if (!forced && retry != m_personalityRetryAfter.end() && retry->second > now)
            return false;

        ChatRequest request;
        request.id = m_nextRequestId++;
        request.kind = RequestKind::PersonalityGeneration;
        request.priority = forced ? RequestPriority::Direct : RequestPriority::Ambient;
        request.actor = actor;
        request.trigger = "personality-generation";
        request.systemPrompt = BuildPersonalityGenerationSystemPrompt(*m_config);
        request.userPrompt = BuildPersonalityGenerationUserPrompt(*m_config, actor);
        request.maxTokensOverride = PersonalityGenerationTokenBudget(*m_config);
        uint64_t const requestId = request.id;
        if (!Enqueue(std::move(request)))
        {
            RecordPersonalityGenerationStatus(actor, "rejected", requestId,
                "The personality request could not be queued; check pause state, queue capacity, and the global request limit.");
            return false;
        }
        m_pendingPersonalityRequests[actor.guid] = requestId;
        RecordPersonalityGenerationStatus(actor, "pending", requestId,
            forced ? "GM-requested personality replacement is running."
                   : "On-demand personality generation is running.");
        return true;
    }

    void Manager::PersistPersonality(BotPersonality const& personality)
    {
        if (!m_personalityDatabaseAvailable)
            return;
        std::string botName = personality.botName;
        std::string traits = SerializePersonalityTraits(personality.traits);
        std::string tone = personality.tone;
        std::string background = personality.background;
        CharacterDatabase.escape_string(botName);
        CharacterDatabase.escape_string(traits);
        CharacterDatabase.escape_string(tone);
        CharacterDatabase.escape_string(background);
        CharacterDatabase.PExecute(
            "INSERT INTO `azeroth_voices_bot_personality` "
            "(`character_guid`,`bot_name`,`traits_json`,`tone`,`background`,`background_mode`,`generation_version`) "
            "VALUES ('%llu','%s','%s','%s','%s','%u','%u') "
            "ON DUPLICATE KEY UPDATE `bot_name`=VALUES(`bot_name`),`traits_json`=VALUES(`traits_json`),"
            "`tone`=VALUES(`tone`),`background`=VALUES(`background`),"
            "`background_mode`=VALUES(`background_mode`),`generation_version`=VALUES(`generation_version`),"
            "`updated_at`=CURRENT_TIMESTAMP",
            static_cast<unsigned long long>(personality.characterGuid), botName.c_str(), traits.c_str(),
            tone.c_str(), background.c_str(), personality.backgroundMode, personality.generationVersion);
    }

    void Manager::HandlePersonalityCompletion(ChatCompletion const& completion)
    {
        uint64_t const guid = completion.request.actor.guid;
        auto pending = m_pendingPersonalityRequests.find(guid);
        if (pending == m_pendingPersonalityRequests.end() || pending->second != completion.request.id)
        {
            ++m_dropped;
            return;
        }
        m_pendingPersonalityRequests.erase(pending);
        auto const now = Clock::now();
        auto fail = [&](std::string const& error) {
            m_personalityRetryAfter[guid] = now +
                std::chrono::seconds(m_config->personalityGenerationRetrySeconds);
            while (m_personalityRetryAfter.size() > MaximumPersonalityCacheEntries)
                m_personalityRetryAfter.erase(m_personalityRetryAfter.begin());
            ++m_failed;
            std::string const safeError = SanitizeLogText(RedactSecrets(*m_config, error));
            RecordPersonalityGenerationStatus(completion.request.actor, "failed",
                completion.request.id, safeError);
            sLog.outError("[AzerothVoices][PERSONALITY] Generation for %s failed; retry allowed in %u seconds: %s",
                SanitizeLogText(completion.request.actor.name).c_str(),
                m_config->personalityGenerationRetrySeconds,
                safeError.c_str());
        };

        if (now > completion.request.expires)
        {
            fail("personality generation expired before completion processing");
            return;
        }
        if (!completion.success)
        {
            fail(completion.error);
            return;
        }

        BotPersonality personality;
        std::string error;
        if (!ParsePersonalityResponse(*m_config, completion.request.actor,
                                      completion.responseText, personality, error))
        {
            fail(error);
            return;
        }
        personality.createdUnix = UnixNow();
        personality.updatedUnix = personality.createdUnix;
        m_personalityRetryAfter.erase(guid);
        m_databaseLoadedPersonalityGuids.insert(guid);
        CachePersonality(personality);
        PersistPersonality(personality);
        RecordPersonalityGenerationStatus(completion.request.actor, "succeeded",
            completion.request.id, m_personalityDatabaseAvailable
                ? "Personality generated; the SQL upsert was queued."
                : "Personality generated in RAM; the SQL table is unavailable.");
        ++m_completed;
        if (m_config->debug)
            sLog.outDebug("[AzerothVoices][PERSONALITY] Generated persistent identity for %s.",
                SanitizeLogText(personality.botName).c_str());
    }

    bool Manager::ResolvePersonalityActor(std::string const& actorName, ActorSnapshot& actor,
                                          std::string& message) const
    {
        if (actorName.empty())
        {
            message = "An exact online PlayerBot name is required.";
            return false;
        }
        Player* bot = ObjectAccessor::FindPlayerByName(actorName.c_str());
        if (!bot || !bot->IsInWorld() || !Script_IsAIControlled(bot))
        {
            message = "No online AI-controlled PlayerBot with that exact name was found.";
            return false;
        }
        actor = SnapshotBot(bot);
        return true;
    }

    void Manager::RecordPersonalityGenerationStatus(ActorSnapshot const& actor, std::string state,
                                                     uint64_t requestId, std::string detail)
    {
        if (!actor.guid)
            return;

        PersonalityGenerationRecord record;
        record.botName = actor.name;
        record.state = std::move(state);
        record.detail = HeadBounded(SanitizeLogText(detail), 500);
        record.requestId = requestId;
        record.updatedUnix = UnixNow();
        m_personalityGenerationStatusOrder.erase(std::remove(m_personalityGenerationStatusOrder.begin(),
            m_personalityGenerationStatusOrder.end(), actor.guid), m_personalityGenerationStatusOrder.end());
        m_personalityGenerationStatusOrder.push_back(actor.guid);
        m_personalityGenerationStatus[actor.guid] = std::move(record);
        while (m_personalityGenerationStatus.size() > MaximumPersonalityCacheEntries &&
               !m_personalityGenerationStatusOrder.empty())
        {
            uint64_t const expired = m_personalityGenerationStatusOrder.front();
            m_personalityGenerationStatusOrder.pop_front();
            m_personalityGenerationStatus.erase(expired);
        }
    }

    void Manager::CancelPersonalityGeneration(uint64_t characterGuid)
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto& queue : m_queues)
            {
                size_t const before = queue.size();
                queue.erase(std::remove_if(queue.begin(), queue.end(), [characterGuid](ChatRequest const& request) {
                    return request.kind == RequestKind::PersonalityGeneration &&
                        request.actor.guid == characterGuid;
                }), queue.end());
                m_dropped.fetch_add(before - queue.size());
            }
        }
        m_pendingPersonalityRequests.erase(characterGuid);
    }

    void Manager::DeletePersonalityRecord(uint64_t characterGuid)
    {
        CancelPersonalityGeneration(characterGuid);
        m_personalities.erase(characterGuid);
        m_personalityCacheOrder.erase(std::remove(m_personalityCacheOrder.begin(),
            m_personalityCacheOrder.end(), characterGuid), m_personalityCacheOrder.end());
        m_databaseLoadedPersonalityGuids.erase(characterGuid);
        m_personalityRetryAfter.erase(characterGuid);
        m_personalityGenerationStatus.erase(characterGuid);
        m_personalityGenerationStatusOrder.erase(std::remove(m_personalityGenerationStatusOrder.begin(),
            m_personalityGenerationStatusOrder.end(), characterGuid), m_personalityGenerationStatusOrder.end());
        if (m_personalityDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_bot_personality` WHERE `character_guid`='%llu'",
                static_cast<unsigned long long>(characterGuid));
    }

    bool Manager::GetPersonality(std::string const& actorName, BotPersonality& personality,
                                 std::string& message)
    {
        ActorSnapshot actor;
        if (!ResolvePersonalityActor(actorName, actor, message))
            return false;
        if (LoadPersonality(actor, personality, false))
            return true;
        if (m_pendingPersonalityRequests.count(actor.guid))
            message = "Personality generation is still pending for " + actor.name + '.';
        else
            message = "No current personality is stored for " + actor.name + ". Use regenerate to create one.";
        return false;
    }

    bool Manager::GetPersonalityGenerationStatus(std::string const& actorName, std::string& message)
    {
        ActorSnapshot actor;
        if (!ResolvePersonalityActor(actorName, actor, message))
            return false;

        auto found = m_personalityGenerationStatus.find(actor.guid);
        if (found == m_personalityGenerationStatus.end())
        {
            message = m_pendingPersonalityRequests.count(actor.guid)
                ? "Personality generation is pending for " + actor.name + "."
                : "No personality generation result is recorded for " + actor.name + " in this server session.";
            return true;
        }

        PersonalityGenerationRecord const& record = found->second;
        std::ostringstream text;
        text << "Personality generation for " << actor.name << ": " << record.state;
        if (record.requestId)
            text << " (request " << record.requestId << ')';
        if (!record.detail.empty())
            text << ". " << record.detail;
        text << " Updated unix=" << record.updatedUnix << '.';
        message = text.str();
        return true;
    }

    bool Manager::RegeneratePersonality(std::string const& actorName, std::string& message)
    {
        if (!m_config || !m_config->personalityEnabled)
        {
            message = "Persistent personalities are disabled by configuration.";
            return false;
        }
        ActorSnapshot actor;
        if (!ResolvePersonalityActor(actorName, actor, message))
            return false;
        BotPersonality previous;
        bool const replacing = LoadPersonality(actor, previous, false);
        CancelPersonalityGeneration(actor.guid);
        if (!QueuePersonalityGeneration(actor, true))
        {
            message = replacing
                ? "Personality replacement could not be queued; the current personality was preserved. Use personality status for details."
                : "Personality generation could not be queued. Use personality status for details.";
            return false;
        }
        message = replacing
            ? "Personality replacement queued for " + actor.name + "; the current personality remains active until the replacement succeeds."
            : "Personality generation queued for " + actor.name + ".";
        return true;
    }

    bool Manager::DeletePersonality(std::string const& actorName, std::string& message)
    {
        ActorSnapshot actor;
        if (!ResolvePersonalityActor(actorName, actor, message))
            return false;
        DeletePersonalityRecord(actor.guid);
        message = "Personality cache and pending work deleted for " + actor.name +
            (m_personalityDatabaseAvailable ? "; the persistent row was queued for deletion. " :
             "; the SQL table is unavailable, so persistent deletion could not be confirmed. ") +
            "History, snapshots, environment, and RAG were unchanged.";
        return true;
    }

    bool Manager::DeleteAllPersonalities(std::string& message)
    {
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto& queue : m_queues)
            {
                size_t const before = queue.size();
                queue.erase(std::remove_if(queue.begin(), queue.end(), [](ChatRequest const& request) {
                    return request.kind == RequestKind::PersonalityGeneration;
                }), queue.end());
                m_dropped.fetch_add(before - queue.size());
            }
        }
        m_pendingPersonalityRequests.clear();
        m_personalityRetryAfter.clear();
        m_personalityGenerationStatus.clear();
        m_personalityGenerationStatusOrder.clear();
        m_personalities.clear();
        m_personalityCacheOrder.clear();
        m_databaseLoadedPersonalityGuids.clear();
        if (m_personalityDatabaseAvailable)
            CharacterDatabase.PExecute("DELETE FROM `azeroth_voices_bot_personality`");
        message = m_personalityDatabaseAvailable
            ? "All Azeroth Voices personality records were queued for deletion; no history, snapshot, environment, RAG, character, or PlayerBot data was changed."
            : "All cached personalities and pending generation jobs were deleted, but the SQL table is unavailable so persistent deletion could not be confirmed; unrelated data was unchanged.";
        return true;
    }

    void Manager::MaybeQueueFollowup(ChatRequest const& request, std::string const& reply)
    {
        if (!request.allowFollowup || !m_config->randomChatterEnabled ||
            request.conversationDepth + 1 >= m_config->randomMaximumActors ||
            !Roll(m_config->randomFollowupChance))
            return;

        Player* actor = request.actor.kind == ActorKind::PlayerBot
            ? ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid)) : nullptr;
        Creature* actorCreature = nullptr;
        Player* anchor = nullptr;
        if (request.scope == ChatScope::World || request.scope == ChatScope::Channel)
            anchor = FindOnlineRealPlayer();
        else if (request.scope == ChatScope::Guild || request.scope == ChatScope::Officer)
            anchor = FindOnlineRealGuildAudience(actor, request.scope);
        else if (request.scope == ChatScope::Party || request.scope == ChatScope::Raid)
            anchor = FindOnlineRealGroupAudience(actor, request.scope);
        else
        {
            anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
            if (!IsOnlineRealPlayer(anchor))
                anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
            if (!IsOnlineRealPlayer(anchor))
            {
                float const observerDistance = request.scope == ChatScope::Yell
                    ? m_config->yellDistance : m_config->sayDistance;
                if (actor)
                    anchor = FindNearbyRealPlayer(actor, observerDistance);
                else
                {
                    Player* lookupAnchor = ObjectAccessor::FindPlayer(
                        ObjectGuid(request.actor.anchorPlayerGuid));
                    actorCreature = lookupAnchor && lookupAnchor->IsInWorld() &&
                        lookupAnchor->GetMapId() == request.actor.mapId
                            ? ObjectAccessor::GetCreature(*lookupAnchor,
                                ObjectGuid(request.actor.guid)) : nullptr;
                    anchor = FindNearbyRealPlayer(actorCreature, observerDistance);
                }
            }
        }
        if (!IsOnlineRealPlayer(anchor))
            return;

        if (!actor && request.actor.kind == ActorKind::Creature && !actorCreature &&
            anchor->GetMapId() == request.actor.mapId)
        {
            actorCreature = ObjectAccessor::GetCreature(*anchor,
                ObjectGuid(request.actor.guid));
        }
        WorldObject* previousActor = actor
            ? static_cast<WorldObject*>(actor)
            : static_cast<WorldObject*>(actorCreature);
        std::vector<Candidate> candidates = CollectCandidates(anchor, request.scope, "", reply,
            true, true, request.actor.guid, 0, previousActor);

        // Any follow-up pair containing an NPC must remain close enough to be
        // a plausible local exchange. Both actors must also be observable by
        // the same real-human anchor. NPC.Distance is 10 yards by default and
        // SayDistance is 25 yards by default.
        if (request.scope == ChatScope::Say)
        {
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [&](Candidate const& candidate) {
                    bool const npcPair = request.actor.kind == ActorKind::Creature ||
                        candidate.actor.kind == ActorKind::Creature;
                    if (!npcPair)
                        return false;
                    if (!previousActor || !previousActor->IsInWorld() ||
                        previousActor->GetMapId() != anchor->GetMapId() ||
                        !previousActor->IsWithinDist(anchor, m_config->sayDistance, false))
                        return true;

                    WorldObject* nextActor = nullptr;
                    if (candidate.actor.kind == ActorKind::PlayerBot)
                        nextActor = ObjectAccessor::FindPlayer(ObjectGuid(candidate.actor.guid));
                    else
                        nextActor = ObjectAccessor::GetCreature(*anchor,
                            ObjectGuid(candidate.actor.guid));
                    return !nextActor || !nextActor->IsInWorld() ||
                        nextActor->GetMapId() != anchor->GetMapId() ||
                        !nextActor->IsWithinDist(anchor, m_config->sayDistance, false) ||
                        !previousActor->IsWithinDist(nextActor, m_config->npcDistance, false);
                }), candidates.end());
        }
        if (candidates.empty())
            return;

        // Prefer the AI that caused the previous line when it remains an
        // eligible candidate. This makes bot-to-bot, bot-to-NPC, NPC-to-bot,
        // and PlayerBot-subject event exchanges answer the triggering actor
        // before introducing another participant. Real humans are never in the
        // candidate list, so human-triggered lines cannot generate a human.
        auto triggeringActor = std::find_if(candidates.begin(), candidates.end(),
            [&](Candidate const& candidate) {
                return request.speaker.guid && candidate.actor.guid == request.speaker.guid;
            });
        if (triggeringActor != candidates.end())
            std::rotate(candidates.begin(), triggeringActor, triggeringActor + 1);
        SpeakerSnapshot previous;
        previous.guid = request.actor.guid;
        previous.name = request.actor.name;
        previous.race = request.actor.race;
        previous.className = request.actor.className;
        previous.gender = request.actor.gender;
        previous.faction = request.actor.faction;
        previous.guild = request.actor.guild;
        previous.groupStatus = request.actor.groupStatus;
        previous.level = request.actor.level;
        previous.isBot = request.actor.kind == ActorKind::PlayerBot;

        for (Candidate const& candidate : candidates)
        {
            uint32_t const interactionChance =
                request.actor.kind == ActorKind::Creature ||
                candidate.actor.kind == ActorKind::Creature
                    ? m_config->rpgAiChatChance : m_config->botToBotChatChance;
            if (!Roll(interactionChance))
                continue;
            if (QueueDialogue(candidate.actor, previous, request.scope,
                request.channelName, "generated-followup", reply, RequestPriority::Ambient,
                true, true, request.conversationDepth + 1))
                return;
        }
    }

    void Manager::DrainCompletions()
    {
        std::deque<ChatCompletion> completions;
        {
            std::lock_guard<std::mutex> lock(m_completionMutex);
            completions.swap(m_completions);
        }

        auto const now = Clock::now();
        for (ChatCompletion& completion : completions)
        {
            RecordApiResult(completion);
            if (completion.request.kind == RequestKind::NaturalCommand)
            {
                ++m_naturalClassifierResults;
                m_naturalClassifierLatencyMilliseconds += completion.elapsedMilliseconds;
                ++m_naturalTelemetry.classifierResults;
                m_naturalTelemetry.classifierLatencyMilliseconds += completion.elapsedMilliseconds;
            }
            if (completion.request.kind == RequestKind::PersonalityGeneration)
            {
                HandlePersonalityCompletion(completion);
                continue;
            }
            bool current = false;
            bool wakeQueue = false;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                if (completion.request.kind == RequestKind::NaturalCommand)
                {
                    current = NaturalCommandPendingForAll(
                        m_pendingNaturalCommandsByActor, completion.request, true);
                    wakeQueue = RemoveNaturalCommandPending(
                        m_pendingNaturalCommandsByActor, completion.request);
                }
                else
                {
                    auto latest = m_latestRequestByActor.find(completion.request.actor.guid);
                    current = latest != m_latestRequestByActor.end() &&
                        latest->second == completion.request.id;
                    if (current)
                        m_latestRequestByActor.erase(latest);
                }
            }
            if (wakeQueue)
                m_queueReady.notify_all();
            if (!current || now > completion.request.expires)
            {
                if (completion.request.kind == RequestKind::NaturalCommand && current &&
                    now > completion.request.expires)
                {
                    ++m_naturalExpired;
                    ++m_naturalTelemetry.confirmationExpired;
                    m_naturalLastFailure = "request expired";
                    Player* speaker = ObjectAccessor::FindPlayer(
                        ObjectGuid(completion.request.speaker.guid));
                    SendNaturalCommandFeedback(speaker, "Natural-command request expired before dispatch.");
                    for (uint64_t botGuid : NaturalCommandRecipientGuids(completion.request))
                        RecordNaturalCommandAudit(completion.request.speaker.guid, botGuid,
                            "", "", "llm", "request-expired", 0.0,
                            completion.request.id, completion.elapsedMilliseconds);
                }
                ++m_dropped;
                continue;
            }

            if (!completion.success)
            {
                ++m_failed;
                if (m_config->debug || m_lastErrorLog == Clock::time_point() ||
                    now - m_lastErrorLog >= std::chrono::seconds(30))
                {
                    if (m_suppressedErrors)
                        sLog.outError("[AzerothVoices] %u additional provider errors were suppressed.", m_suppressedErrors);
                    sLog.outError("[AzerothVoices] Request %llu for %s failed: %s",
                        static_cast<unsigned long long>(completion.request.id),
                        SanitizeLogText(completion.request.actor.name).c_str(),
                        RedactSecrets(*m_config, completion.error).c_str());
                    m_lastErrorLog = now;
                    m_suppressedErrors = 0;
                }
                else
                    ++m_suppressedErrors;
                if (completion.request.kind == RequestKind::NaturalCommand)
                {
                    Player* speaker = ObjectAccessor::FindPlayer(
                        ObjectGuid(completion.request.speaker.guid));
                    RejectNaturalCommand(speaker, "Natural-command provider request failed.");
                    for (uint64_t botGuid : NaturalCommandRecipientGuids(completion.request))
                        RecordNaturalCommandAudit(completion.request.speaker.guid, botGuid,
                            "", "", "llm", "provider-failed", 0.0,
                            completion.request.id, completion.elapsedMilliseconds);
                }
                continue;
            }

            if (completion.request.kind == RequestKind::NaturalCommand)
            {
                HandleNaturalCommandCompletion(completion);
                continue;
            }

            std::vector<std::string> lines = Provider::SplitReply(*m_config, completion.responseText);
            if (lines.empty())
            {
                ++m_failed;
                continue;
            }

            RecordGeneratedMessage(completion, lines);

            uint64_t cumulativeDelay = m_config->typingSimulationEnabled
                ? m_config->typingBaseDelayMilliseconds : 0;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                uint64_t lineDelay = m_config->typingSimulationEnabled
                    ? static_cast<uint64_t>(lines[i].size()) * m_config->typingDelayPerCharacterMilliseconds
                    : 0;
                cumulativeDelay += lineDelay;
                uint64_t effectiveDelay = cumulativeDelay;
                if (m_config->typingSimulationEnabled && m_config->subtractGenerationTime)
                    effectiveDelay = effectiveDelay > completion.elapsedMilliseconds
                        ? effectiveDelay - completion.elapsedMilliseconds : 0;

                ScheduledLine line;
                line.request = completion.request;
                line.text = lines[i];
                line.due = now + std::chrono::milliseconds(effectiveDelay);
                line.firstLine = i == 0;
                m_scheduled.push_back(std::move(line));
                if (m_config->typingSimulationEnabled)
                    cumulativeDelay += 500;
            }
            ++m_completed;
        }
    }

    void Manager::RecordApiResult(ChatCompletion const& completion)
    {
        if (!m_config || !m_config->consoleApiCallStats)
            return;

        m_telemetryApiCalls += completion.httpAttemptCount;
        if (completion.success)
            ++m_telemetrySuccessfulResults;
        else
            ++m_telemetryFailedResults;
    }

    void Manager::RecordGeneratedMessage(ChatCompletion const& completion,
                                         std::vector<std::string> const& lines)
    {
        if (!m_config || (!m_config->consoleGeneratedMessages &&
                          !m_config->consoleApiCallStats))
            return;

        if (m_config->consoleApiCallStats)
            ++m_telemetryGeneratedMessages;
        if (!m_config->consoleGeneratedMessages)
            return;

        std::string speakerName = SanitizeLogText(completion.request.speaker.name);
        if (speakerName.empty())
            speakerName = "system";
        sLog.outString("[AzerothVoices][Generated] request=%llu actor=\"%s\" guid=%llu kind=%s scope=%s channel=\"%s\" trigger=%s speaker=\"%s\" model=\"%s\" http=%d attempts=%u latency=%u ms text=\"%s\"",
            static_cast<unsigned long long>(completion.request.id),
            SanitizeLogText(completion.request.actor.name).c_str(),
            static_cast<unsigned long long>(completion.request.actor.guid),
            ActorKindName(completion.request.actor.kind).c_str(),
            ScopeName(completion.request.scope).c_str(),
            SanitizeLogText(completion.request.channelName).c_str(),
            SanitizeLogText(completion.request.trigger).c_str(), speakerName.c_str(),
            SanitizeLogText(completion.request.modelOverride.empty()
                ? m_config->model : completion.request.modelOverride).c_str(), completion.httpStatus,
            completion.httpAttemptCount, completion.elapsedMilliseconds,
            JoinReplyLines(lines).c_str());
    }

    void Manager::ReportTelemetry()
    {
        if (!m_config || (!m_config->consoleApiCallStats &&
                          !m_config->naturalCommandsTelemetryEnabled))
            return;

        auto const now = Clock::now();
        if (m_telemetryWindowStarted == Clock::time_point())
            m_telemetryWindowStarted = now;
        auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_telemetryWindowStarted).count();
        if (elapsed < m_config->consoleApiCallStatsIntervalSeconds)
            return;

        if (m_config->consoleApiCallStats)
        {
            uint64_t preflightRejected = 0;
            for (uint64_t count : m_preflightRejections)
                preflightRejected += count;
            sLog.outString("[AzerothVoices][Telemetry] past %lld seconds: API calls=%llu, successful results=%llu, failed results=%llu, generated messages=%llu, preflight rejected=%llu.",
                static_cast<long long>(elapsed),
                static_cast<unsigned long long>(m_telemetryApiCalls),
                static_cast<unsigned long long>(m_telemetrySuccessfulResults),
                static_cast<unsigned long long>(m_telemetryFailedResults),
                static_cast<unsigned long long>(m_telemetryGeneratedMessages),
                static_cast<unsigned long long>(preflightRejected));

            static std::array<char const*, static_cast<size_t>(PreflightReason::Count)> const reasonNames = {
                "no-human-nearby", "no-real-audience", "npc-neutral", "npc-hostile",
                "npc-temporary", "invalid-actor", "invalid-scope", "combat",
                "unavailable", "cooldown", "rate-limit", "queue-full", "superseded"
            };
            std::ostringstream reasons;
            for (size_t i = 0; i < m_preflightRejections.size(); ++i)
            {
                if (!m_preflightRejections[i])
                    continue;
                if (reasons.tellp() > 0)
                    reasons << ", ";
                reasons << reasonNames[i] << '=' << m_preflightRejections[i];
            }
            if (reasons.tellp() > 0)
                sLog.outString("[AzerothVoices][Telemetry] preflight reasons: %s.", reasons.str().c_str());
        }

        if (m_config->naturalCommandsTelemetryEnabled)
        {
            uint64_t const averageShortlist = m_naturalTelemetry.classifierQueued
                ? m_naturalTelemetry.shortlistActions / m_naturalTelemetry.classifierQueued : 0;
            uint64_t const averagePrompt = m_naturalTelemetry.classifierQueued
                ? m_naturalTelemetry.promptCharacters / m_naturalTelemetry.classifierQueued : 0;
            uint64_t const averageLatency = m_naturalTelemetry.classifierResults
                ? m_naturalTelemetry.classifierLatencyMilliseconds /
                    m_naturalTelemetry.classifierResults : 0;
            std::string const mostUsed = NaturalCommandMostUsedActions(5);
            sLog.outString("[AzerothVoices][NaturalCommands][Telemetry] past %lld seconds: considered=%llu, local=%llu, classifier=%llu, avg-shortlist=%llu, avg-prompt-chars=%llu, avg-latency-ms=%llu, conversation=%llu, unsupported=%llu, low-confidence=%llu, invalid=%llu, rejected=%llu, confirm-required=%llu, confirmed=%llu, cancelled=%llu, expired=%llu, top-actions=%s.",
                static_cast<long long>(elapsed),
                static_cast<unsigned long long>(m_naturalTelemetry.considered),
                static_cast<unsigned long long>(m_naturalTelemetry.localFastPath),
                static_cast<unsigned long long>(m_naturalTelemetry.classifierQueued),
                static_cast<unsigned long long>(averageShortlist),
                static_cast<unsigned long long>(averagePrompt),
                static_cast<unsigned long long>(averageLatency),
                static_cast<unsigned long long>(m_naturalTelemetry.conversation),
                static_cast<unsigned long long>(m_naturalTelemetry.unsupported),
                static_cast<unsigned long long>(m_naturalTelemetry.lowConfidence),
                static_cast<unsigned long long>(m_naturalTelemetry.invalidDecision),
                static_cast<unsigned long long>(m_naturalTelemetry.rejected),
                static_cast<unsigned long long>(m_naturalTelemetry.confirmationRequired),
                static_cast<unsigned long long>(m_naturalTelemetry.confirmationConfirmed),
                static_cast<unsigned long long>(m_naturalTelemetry.confirmationCancelled),
                static_cast<unsigned long long>(m_naturalTelemetry.confirmationExpired),
                mostUsed.empty() ? "none" : mostUsed.c_str());
        }

        m_telemetryWindowStarted = now;
        if (m_config->consoleApiCallStats)
        {
            m_telemetryApiCalls = 0;
            m_telemetrySuccessfulResults = 0;
            m_telemetryFailedResults = 0;
            m_telemetryGeneratedMessages = 0;
            m_preflightRejections.fill(0);
        }
        if (m_config->naturalCommandsTelemetryEnabled)
            m_naturalTelemetry = {};
    }

    void Manager::DeliverScheduled()
    {
        auto const now = Clock::now();
        for (auto it = m_scheduled.begin(); it != m_scheduled.end(); )
        {
            if (it->due > now)
            {
                ++it;
                continue;
            }

            bool delivered = Deliver(*it);
            if (delivered && it->firstLine && it->request.kind != RequestKind::NaturalCommand)
            {
                AddHistory(it->request, it->text);
                AddSnapshotHistory(it->request, it->request.currentSnapshot);
                SpeakerSnapshot actorSpeaker;
                actorSpeaker.guid = it->request.actor.guid;
                actorSpeaker.name = it->request.actor.name;
                actorSpeaker.race = it->request.actor.race;
                actorSpeaker.className = it->request.actor.className;
                actorSpeaker.gender = it->request.actor.gender;
                actorSpeaker.faction = it->request.actor.faction;
                actorSpeaker.guild = it->request.actor.guild;
                actorSpeaker.groupStatus = it->request.actor.groupStatus;
                actorSpeaker.level = it->request.actor.level;
                actorSpeaker.groupId = it->request.speaker.groupId;
                actorSpeaker.guildId = it->request.speaker.guildId;
                actorSpeaker.isBot = true;
                ActorSnapshot replyLocation = it->request.actor;
                if (it->request.scope == ChatScope::Whisper)
                    replyLocation.guid = it->request.speaker.guid;
                RecordSurroundingChat(it->request.scope, it->request.channelName,
                    replyLocation, actorSpeaker, it->text);
                MaybeQueueFollowup(it->request, it->text);
            }
            if (!delivered && m_config->debug)
                sLog.outDebug("[AzerothVoices] Reply discarded because actor %s is unavailable.",
                    SanitizeLogText(it->request.actor.name).c_str());
            else if (delivered && m_config->debug)
                sLog.outDebug("[AzerothVoices] %s replied through %s.",
                    SanitizeLogText(it->request.actor.name).c_str(), ScopeName(it->request.scope).c_str());
            it = m_scheduled.erase(it);
        }
    }

    bool Manager::Deliver(ScheduledLine const& line)
    {
        ChatRequest const& request = line.request;
        if (!m_started || m_stopping || !m_config || Clock::now() > request.expires)
            return false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (request.kind != RequestKind::NaturalCommand)
            {
                auto latest = m_latestRequestByActor.find(request.actor.guid);
                if (latest != m_latestRequestByActor.end() && latest->second != request.id)
                    return false;
            }
        }

        // PlayerBots joining a real player's explicitly targeted NPC
        // conversation must still be within NPC.Distance of that same player
        // after provider latency. NPC actors receive this pair check in the
        // shared NPC exchange validation immediately below.
        bool const targetedNpcConversation = request.scope == ChatScope::Say &&
            request.trigger.compare(0, 13, "targeted-npc-") == 0;
        if (targetedNpcConversation && request.actor.kind == ActorKind::PlayerBot)
        {
            Player* human = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            if (!IsOnlineRealPlayer(human) || !bot || !bot->IsInWorld() ||
                bot->GetMapId() != human->GetMapId() ||
                !bot->IsWithinDist(human, m_config->npcDistance, false))
                return false;
        }

        // Revalidate local NPC exchanges after provider latency. Any pair that
        // contains an NPC must still be within NPC.Distance, and one real
        // human must simultaneously be within SayDistance of both speakers.
        if (request.scope == ChatScope::Say && request.speaker.guid)
        {
            ObjectGuid const previousGuid(request.speaker.guid);
            bool const npcPair = request.actor.kind == ActorKind::Creature ||
                previousGuid.IsCreature();
            if (npcPair)
            {
                WorldObject* currentActor = nullptr;
                if (request.actor.kind == ActorKind::PlayerBot)
                    currentActor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
                else
                {
                    Player* lookupAnchor = ObjectAccessor::FindPlayer(
                        ObjectGuid(request.actor.anchorPlayerGuid));
                    if (lookupAnchor && lookupAnchor->IsInWorld() &&
                        lookupAnchor->GetMapId() == request.actor.mapId)
                    {
                        currentActor = ObjectAccessor::GetCreature(*lookupAnchor,
                            ObjectGuid(request.actor.guid));
                    }
                }

                WorldObject* previousActor = nullptr;
                if (previousGuid.IsPlayer())
                    previousActor = ObjectAccessor::FindPlayer(previousGuid);
                else if (previousGuid.IsCreature() && currentActor)
                    previousActor = ObjectAccessor::GetCreature(*currentActor, previousGuid);

                if (!currentActor || !previousActor || !currentActor->IsInWorld() ||
                    !previousActor->IsInWorld() ||
                    currentActor->GetMapId() != previousActor->GetMapId() ||
                    !currentActor->IsWithinDist(previousActor, m_config->npcDistance, false))
                    return false;

                bool sharedHumanObserver = false;
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& entry : sObjectAccessor.GetPlayers())
                {
                    Player* player = entry.second;
                    if (!IsOnlineRealPlayer(player) ||
                        player->GetMapId() != currentActor->GetMapId())
                        continue;
                    if (currentActor->IsWithinDist(player, m_config->sayDistance, false) &&
                        previousActor->IsWithinDist(player, m_config->sayDistance, false))
                    {
                        sharedHumanObserver = true;
                        break;
                    }
                }
                if (!sharedHumanObserver)
                    return false;
            }
        }

        if (request.actor.kind == ActorKind::PlayerBot)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            if (!bot || !bot->IsInWorld() || !bot->IsAlive() ||
                !Script_IsAIControlled(bot) || bot->GetName() != request.actor.name ||
                (m_config->disableRepliesInCombat && bot->IsInCombat()))
                return false;

            if (request.scope == ChatScope::Say || request.scope == ChatScope::Yell)
            {
                float const observerDistance = request.scope == ChatScope::Yell
                    ? m_config->yellDistance : m_config->sayDistance;
                if (!HasNearbyRealPlayer(bot, observerDistance))
                    return false;
            }

            switch (request.scope)
            {
                case ChatScope::Say:
                    bot->Say(line.text, LANG_UNIVERSAL);
                    return true;
                case ChatScope::Yell:
                    bot->Yell(line.text, LANG_UNIVERSAL);
                    return true;
                case ChatScope::Whisper:
                {
                    Player* receiver = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
                    if (!IsOnlineRealPlayer(receiver))
                        return false;
                    bot->Whisper(line.text, LANG_UNIVERSAL, receiver->GetObjectGuid());
                    return true;
                }
                case ChatScope::Party:
                case ChatScope::Raid:
                {
                    Group* group = bot->GetGroup();
                    if (!group || !HasRealPlayerAudience(bot, request.scope, "", 0.0f))
                        return false;
                    WorldPacket packet;
                    ChatMsg type = request.scope == ChatScope::Raid ? CHAT_MSG_RAID : CHAT_MSG_PARTY;
                    ChatHandler::BuildChatPacket(packet, type, line.text, LANG_UNIVERSAL,
                        bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
                    int subgroup = request.scope == ChatScope::Party
                        ? group->GetMemberGroup(bot->GetObjectGuid()) : -1;
                    group->BroadcastPacket(&packet, false, subgroup);
                    return true;
                }
                case ChatScope::Guild:
                case ChatScope::Officer:
                {
                    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
                    if (!guild || !bot->GetSession() ||
                        !HasRealPlayerAudience(bot, request.scope, "", 0.0f))
                        return false;
                    if (request.scope == ChatScope::Officer)
                        guild->BroadcastToOfficers(bot->GetSession(), line.text, LANG_UNIVERSAL);
                    else
                        guild->BroadcastToGuild(bot->GetSession(), line.text, LANG_UNIVERSAL);
                    return true;
                }
                case ChatScope::Channel:
                case ChatScope::World:
                {
                    std::string channelName = request.channelName.empty()
                        ? m_config->worldChannelName : request.channelName;
                    if (!HasRealPlayerAudience(bot, request.scope, channelName, 0.0f))
                        return false;
                    ChannelMgr* manager = channelMgr(bot->GetTeam());
                    Channel* channel = manager ? manager->GetOrCreateChannel(channelName) : nullptr;
                    if (!channel)
                        return false;
                    channel->AsyncSay(bot->GetObjectGuid(), line.text.c_str(), LANG_UNIVERSAL, true);
                    return true;
                }
            }
        }

        if (request.scope != ChatScope::Say)
            return false;
        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
        if (!anchor || !anchor->IsInWorld() || anchor->GetMapId() != request.actor.mapId)
            return false;
        Creature* creature = ObjectAccessor::GetCreature(*anchor, ObjectGuid(request.actor.guid));
        if (!creature || creature->GetName() != request.actor.name ||
            EvaluateNpcSpeaker(creature, m_config->sayDistance, *m_config) !=
                NpcEligibilityResult::Eligible ||
            (m_config->disableRepliesInCombat && creature->IsInCombat()))
            return false;

        Player* receiver = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
        creature->MonsterSay(line.text, LANG_UNIVERSAL,
            receiver && receiver->IsInWorld() ? receiver : nullptr);
        return true;
    }

    std::string Manager::BuildEnvironmentContext(ChatRequest const& request) const
    {
        if (!m_config->environmentContextEnabled)
            return "";

        Player* bot = nullptr;
        Player* anchor = nullptr;
        WorldObject* center = nullptr;
        if (request.actor.kind == ActorKind::PlayerBot)
        {
            bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
            center = bot && bot->IsInWorld() ? static_cast<WorldObject*>(bot) : nullptr;
            anchor = bot;
        }
        else
        {
            anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
            if (anchor && anchor->IsInWorld())
                center = ObjectAccessor::GetCreature(*anchor, ObjectGuid(request.actor.guid));
        }

        std::ostringstream context;
        context << "Current environment: map=" << request.actor.map
                << ", zone=" << request.actor.zone
                << ", area=" << request.actor.area
                << ", level=" << request.actor.level;
        bool const detailedPlayerBot = m_config->snapshotEnabled && request.actor.kind == ActorKind::PlayerBot;
        if (!detailedPlayerBot || !m_config->snapshotIncludeCombat)
            context << ", state=" << (request.actor.inCombat ? "in combat" : "out of combat");
        if (!detailedPlayerBot || !m_config->snapshotIncludeGroup)
            context << ", group=" << request.actor.groupStatus;
        if (!request.actor.guild.empty())
            context << ", guild=" << request.actor.guild;
        if (center && center->FindMap() && center->FindMap()->IsDungeon())
            context << ", inside a dungeon";

        if (center && m_config->environmentMaximumCreatures &&
            (!detailedPlayerBot || !m_config->snapshotIncludeLineOfSight))
        {
            size_t const candidateLimit = static_cast<size_t>(m_config->environmentMaximumCreatures) * 4 + 16;
            BoundedCreatureRangeCheck check(center, m_config->environmentContextDistance, candidateLimit);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<BoundedCreatureRangeCheck> searcher(creatures, check);
            Cell::VisitGridObjects(center, searcher, m_config->environmentContextDistance);
            uint32_t count = 0;
            for (Creature* creature : creatures)
            {
                if (!creature || creature->GetObjectGuid().GetRawValue() == request.actor.guid ||
                    !creature->IsAlive() || creature->IsPet() || creature->IsTotem())
                    continue;
                context << (count++ == 0 ? "\nNearby creatures: " : ", ") << creature->GetName();
                if (count >= m_config->environmentMaximumCreatures)
                    break;
            }
        }

        if (bot && m_config->environmentMaximumItems &&
            (m_config->environmentIncludeEquipment || m_config->environmentIncludeBackpack))
        {
            uint32_t count = 0;
            auto appendItem = [&](Item* item)
            {
                if (!item || !item->GetProto() || count >= m_config->environmentMaximumItems)
                    return;
                context << (count++ == 0 ? "\nVisible/relevant items: " : ", ") << item->GetProto()->Name1;
            };
            if (m_config->environmentIncludeEquipment)
                for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END && count < m_config->environmentMaximumItems; ++slot)
                    appendItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            if (m_config->environmentIncludeBackpack)
                for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && count < m_config->environmentMaximumItems; ++slot)
                    appendItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        }
        return context.str();
    }

    std::string Manager::BuildCurrentSnapshotContext(ChatRequest const& request) const
    {
        if (!m_config->snapshotEnabled || request.actor.kind != ActorKind::PlayerBot)
            return "";

        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.guid));
        if (!bot || !bot->IsInWorld())
            return "";

        auto healthText = [](Unit const* unit) {
            std::ostringstream value;
            uint32_t const maximum = unit ? unit->GetMaxHealth() : 0;
            uint32_t const current = unit ? unit->GetHealth() : 0;
            uint32_t const percent = maximum ? static_cast<uint32_t>((static_cast<uint64_t>(current) * 100) / maximum) : 0;
            value << current << '/' << maximum << " (" << percent << "%)";
            return value.str();
        };
        auto distanceText = [bot](WorldObject const* object) {
            return std::to_string(static_cast<uint32_t>(bot->GetDistance(object) + 0.5f)) + " yd";
        };
        auto appendSectionLine = [](std::string& section, std::string const& heading, std::string const& line) {
            if (line.empty())
                return;
            if (section.empty())
                section = heading + ":\n";
            section += "- " + line + "\n";
        };

        std::string combat;
        if (m_config->snapshotIncludeCombat)
        {
            std::ostringstream line;
            line << "Bot health=" << healthText(bot)
                 << ", state=" << (bot->IsInCombat() ? "in combat" : "out of combat");
            Powers const power = bot->GetPowerType();
            char const* powerName = "power";
            switch (power)
            {
                case POWER_MANA: powerName = "mana"; break;
                case POWER_RAGE: powerName = "rage"; break;
                case POWER_ENERGY: powerName = "energy"; break;
                default: break;
            }
            uint32_t currentPower = static_cast<uint32_t>(bot->GetPower(power));
            uint32_t maximumPower = static_cast<uint32_t>(bot->GetMaxPower(power));
            if (power == POWER_RAGE)
            {
                currentPower /= 10;
                maximumPower /= 10;
            }
            line << ", " << powerName << '=' << currentPower << '/' << maximumPower;
            Unit* target = bot->GetVictim();
            char const* targetKind = "victim";
            if (!target && !bot->GetSelectionGuid().IsEmpty())
            {
                target = ObjectAccessor::GetUnit(*bot, bot->GetSelectionGuid());
                targetKind = "selected target";
            }
            if (target)
                line << ", " << targetKind << '=' << target->GetName() << " (level " << target->GetLevel()
                     << ", health " << healthText(target) << ')';
            combat = "Combat and resources:\n- " + line.str() + "\n";
        }

        std::string group;
        if (m_config->snapshotIncludeGroup && m_config->snapshotMaximumGroupMembers)
        {
            if (Group* botGroup = bot->GetGroup())
            {
                uint32_t count = 0;
                for (GroupReference* reference = botGroup->GetFirstMember(); reference &&
                     count < m_config->snapshotMaximumGroupMembers; reference = reference->next())
                {
                    Player* member = reference->getSource();
                    if (!member || member == bot || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
                        continue;
                    std::ostringstream line;
                    line << member->GetName() << ", level " << member->GetLevel() << ' '
                         << RaceName(member->GetRace()) << ' ' << ClassName(member->GetClass())
                         << ", health " << healthText(member) << ", distance " << distanceText(member);
                    if (member->IsInCombat())
                    {
                        line << ", fighting";
                        if (member->GetVictim())
                            line << ' ' << member->GetVictim()->GetName();
                    }
                    appendSectionLine(group, "Nearby group members", line.str());
                    ++count;
                }
            }
        }

        std::string spells;
        if (m_config->snapshotIncludeSpells && m_config->snapshotMaximumSpells)
        {
            struct KnownSpell { uint8_t rank = 0; uint32_t id = 0; SpellEntry const* info = nullptr; };
            std::map<std::string, KnownSpell> highestByName;
            static std::set<std::string> const ignored = {
                "attack", "opening", "closing", "stuck", "remove insignia", "opening - no text",
                "grovel", "duel", "honorless target"
            };
            for (auto const& learned : bot->GetSpellMap())
            {
                uint32_t const spellId = learned.first;
                if (learned.second.state == PLAYERSPELL_REMOVED || learned.second.disabled)
                    continue;
                SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
                if (!info || info->IsPassiveSpell() || info->SpellName[0].empty())
                    continue;
                std::string const name = info->SpellName[0];
                uint8_t const rank = sSpellMgr.GetSpellRank(spellId);
                if (ignored.count(Lower(name)) || (!rank && info->SpellFamilyName == SPELLFAMILY_GENERIC))
                    continue;
                KnownSpell& selected = highestByName[name];
                if (!selected.info || rank > selected.rank || (rank == selected.rank && spellId > selected.id))
                    selected = { rank, spellId, info };
            }
            uint32_t count = 0;
            for (auto const& selected : highestByName)
            {
                if (count++ >= m_config->snapshotMaximumSpells)
                    break;
                std::ostringstream line;
                line << selected.first;
                KnownSpell const& spell = selected.second;
                if (spell.rank)
                    line << " (rank " << static_cast<uint32_t>(spell.rank) << ')';
                if (spell.info->manaCost)
                {
                    char const* costName = spell.info->powerType == POWER_RAGE ? "rage" :
                        (spell.info->powerType == POWER_ENERGY ? "energy" : "mana");
                    uint32_t cost = spell.info->manaCost;
                    if (spell.info->powerType == POWER_RAGE)
                        cost /= 10;
                    line << ", cost " << cost << ' ' << costName;
                }
                appendSectionLine(spells, "Known usable spells (highest known rank per name)", line.str());
            }
        }

        std::string quests;
        if (m_config->snapshotIncludeQuests && m_config->snapshotMaximumQuests)
        {
            uint32_t count = 0;
            for (uint8_t slot = 0; slot < MAX_QUEST_LOG_SIZE && count < m_config->snapshotMaximumQuests; ++slot)
            {
                uint32_t const questId = bot->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (!quest)
                    continue;
                QuestStatus const status = bot->GetQuestStatus(questId);
                std::string statusName = status == QUEST_STATUS_COMPLETE ? "complete" :
                    (status == QUEST_STATUS_FAILED ? "failed" : "in progress");
                appendSectionLine(quests, "Active quests", quest->GetTitle() + " (" + statusName + ")");
                ++count;
            }
        }

        std::string lineOfSight;
        if (m_config->snapshotIncludeLineOfSight)
        {
            if (m_config->snapshotMaximumCreatures)
            {
                size_t const candidateLimit = static_cast<size_t>(m_config->snapshotMaximumCreatures) * 4 + 16;
                BoundedCreatureRangeCheck check(bot, m_config->snapshotDistance, candidateLimit);
                std::list<Creature*> creatures;
                MaNGOS::CreatureListSearcher<BoundedCreatureRangeCheck> searcher(creatures, check);
                Cell::VisitGridObjects(bot, searcher, m_config->snapshotDistance);
                std::vector<std::pair<float, std::string>> visible;
                for (Creature* creature : creatures)
                {
                    if (!creature || !creature->IsInWorld() || creature->IsPet() || creature->IsTotem() ||
                        creature->IsCritter() || !bot->IsWithinLOSInMap(creature))
                        continue;
                    std::ostringstream line;
                    line << creature->GetName() << ", level " << creature->GetLevel() << ", "
                         << (creature->IsHostileTo(bot) ? "hostile" : (creature->IsFriendlyTo(bot) ? "friendly" : "neutral"))
                         << ", " << distanceText(creature);
                    if (creature->IsInCombat() || creature->IsHostileTo(bot))
                        line << ", health " << healthText(creature);
                    if (creature->IsInCombat())
                        line << ", in combat";
                    visible.emplace_back(bot->GetDistance(creature), line.str());
                }
                std::stable_sort(visible.begin(), visible.end(), [](auto const& left, auto const& right) {
                    return left.first < right.first;
                });
                std::set<std::string> seen;
                uint32_t count = 0;
                for (auto const& value : visible)
                    if (seen.insert(value.second).second && count++ < m_config->snapshotMaximumCreatures)
                        appendSectionLine(lineOfSight, "Visible creatures and game objects", value.second);
            }

            if (m_config->snapshotMaximumGameObjects)
            {
                size_t const candidateLimit = static_cast<size_t>(m_config->snapshotMaximumGameObjects) * 4 + 16;
                BoundedGameObjectRangeCheck check(bot, m_config->snapshotDistance, candidateLimit);
                std::list<GameObject*> gameObjects;
                MaNGOS::GameObjectListSearcher<BoundedGameObjectRangeCheck> searcher(gameObjects, check);
                Cell::VisitGridObjects(bot, searcher, m_config->snapshotDistance);
                std::vector<std::pair<float, std::string>> visible;
                for (GameObject* object : gameObjects)
                {
                    if (!object || !object->IsInWorld() || !object->IsSpawned() || !object->GetGOInfo() ||
                        object->GetGOInfo()->name.empty() || !bot->IsWithinLOSInMap(object))
                        continue;
                    visible.emplace_back(bot->GetDistance(object), object->GetGOInfo()->name +
                        " (" + GameObjectTypeName(object->GetGoType()) + ", " + distanceText(object) + ")");
                }
                std::stable_sort(visible.begin(), visible.end(), [](auto const& left, auto const& right) {
                    return left.first < right.first;
                });
                std::set<std::string> seen;
                uint32_t count = 0;
                for (auto const& value : visible)
                    if (seen.insert(value.second).second && count++ < m_config->snapshotMaximumGameObjects)
                        appendSectionLine(lineOfSight, "Visible creatures and game objects", value.second);
            }
        }

        std::string nearbyPlayers;
        if (m_config->snapshotIncludeNearbyPlayers && m_config->snapshotMaximumPlayers)
        {
            size_t const candidateLimit = static_cast<size_t>(m_config->snapshotMaximumPlayers) * 4 + 16;
            BoundedPlayerRangeCheck check(bot, m_config->snapshotDistance, candidateLimit);
            std::list<Player*> players;
            MaNGOS::PlayerListSearcher<BoundedPlayerRangeCheck> searcher(players, check);
            Cell::VisitWorldObjects(bot, searcher, m_config->snapshotDistance);
            std::vector<std::pair<float, std::string>> visible;
            for (Player* player : players)
            {
                if (!player || player == bot || !player->IsInWorld() || player->IsGameMaster() ||
                    !bot->IsWithinLOSInMap(player))
                    continue;
                std::ostringstream line;
                line << player->GetName() << ", level " << player->GetLevel() << ' '
                     << RaceName(player->GetRace()) << ' ' << ClassName(player->GetClass())
                     << ", " << TeamName(player->GetTeam())
                     << ", " << (Script_IsAIControlled(player) ? "playerbot" : "player")
                     << ", " << distanceText(player);
                visible.emplace_back(bot->GetDistance(player), line.str());
            }
            std::stable_sort(visible.begin(), visible.end(), [](auto const& left, auto const& right) {
                return left.first < right.first;
            });
            for (size_t i = 0; i < visible.size() && i < m_config->snapshotMaximumPlayers; ++i)
                appendSectionLine(nearbyPlayers, "Nearby visible players", visible[i].second);
        }

        if (combat.empty() && group.empty() && spells.empty() && quests.empty() &&
            lineOfSight.empty() && nearbyPlayers.empty())
            return "";

        std::string result = m_config->snapshotPromptTemplate;
        ReplaceAll(result, "{combat}", Trim(combat));
        ReplaceAll(result, "{group}", Trim(group));
        ReplaceAll(result, "{spells}", Trim(spells));
        ReplaceAll(result, "{quests}", Trim(quests));
        ReplaceAll(result, "{line_of_sight}", Trim(lineOfSight));
        ReplaceAll(result, "{nearby_players}", Trim(nearbyPlayers));
        ReplaceAll(result, "\\n", "\n");
        while (result.find("\n\n\n") != std::string::npos)
            ReplaceAll(result, "\n\n\n", "\n\n");
        return HeadBounded(Trim(result), m_config->snapshotMaximumCharacters);
    }

    std::string Manager::BuildHistoryContext(ChatRequest const& request)
    {
        if (!m_config->historyStorageMode)
            return "";
        if (m_config->historyStorageMode == 2)
            LoadDatabaseHistory(request);
        auto found = m_history.find(request.historyKey);
        if (found == m_history.end() || found->second.empty())
            return "";

        auto cutoff = Clock::now() - std::chrono::minutes(m_config->historyTtlMinutes);
        std::string body;
        size_t const maximumTurns = m_config->historyStorageMode == 2
            ? m_config->historyDatabaseMaximumTurns : m_config->historyRamMaximumTurns;
        size_t included = 0;
        for (auto it = found->second.rbegin(); it != found->second.rend() && included < maximumTurns; ++it)
        {
            if (it->created < cutoff)
                continue;
            std::string line = m_config->historyLineTemplate;
            ReplaceAll(line, "<sender message>", it->speakerMessage);
            ReplaceAll(line, "<bot reply>", it->actorReply);
            line = Expand(line, request);
            ReplaceAll(line, "\\n", "\n");
            if (!body.empty() && body.size() + line.size() > m_config->historyMaximumCharacters)
                break;
            body.insert(0, line);
            ++included;
        }
        if (body.empty())
            return "";
        std::string context = Expand(m_config->historyHeaderTemplate, request) + "\n" + body;
        context += Expand(m_config->historyFooterTemplate, request);
        ReplaceAll(context, "\\n", "\n");
        return TailBounded(context, m_config->historyMaximumCharacters);
    }

    std::string Manager::BuildSurroundingContext(ChatRequest const& request) const
    {
        if (!m_config->surroundingChatEnabled || !m_config->surroundingChatMaximumLines)
            return "";
        auto found = m_surroundingChat.find(request.scopeKey);
        if (found == m_surroundingChat.end())
            return "";

        auto cutoff = Clock::now() - std::chrono::minutes(m_config->surroundingChatTtlMinutes);
        std::vector<std::string> lines;
        for (auto it = found->second.rbegin(); it != found->second.rend() &&
             lines.size() < m_config->surroundingChatMaximumLines; ++it)
        {
            if (it->created < cutoff)
                continue;
            if (it == found->second.rbegin() && it->speakerGuid == request.speaker.guid &&
                it->message == request.incomingMessage)
                continue;
            lines.push_back(it->speakerName + ": " + it->message);
        }
        if (lines.empty())
            return "";
        std::string result = "Recent relevant chat in this same scope (older context only):\n";
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            result += *it + "\n";
        return TailBounded(result, m_config->surroundingChatMaximumCharacters);
    }

    std::string Manager::BuildSnapshotHistoryContext(ChatRequest const& request)
    {
        if (!m_config->snapshotEnabled || !m_config->snapshotStorageMode || request.actor.kind != ActorKind::PlayerBot)
            return "";
        if (m_config->snapshotStorageMode == 2)
            LoadDatabaseSnapshot(request);
        auto found = m_snapshotHistory.find(ActorKey(request.actor));
        if (found == m_snapshotHistory.end())
            return "";

        auto cutoff = Clock::now() - std::chrono::minutes(m_config->snapshotHistoryTtlMinutes);
        size_t const maximum = m_config->snapshotStorageMode == 2
            ? m_config->snapshotDatabaseMaximumSnapshots : m_config->snapshotRamMaximumSnapshots;
        std::vector<std::string> snapshots;
        for (auto it = found->second.rbegin(); it != found->second.rend() && snapshots.size() < maximum; ++it)
            if (it->created >= cutoff)
                snapshots.push_back(it->text);
        if (snapshots.empty())
            return "";
        std::string const header = "Earlier playerbot snapshots (possibly stale; the current snapshot below wins):\n";
        std::string body;
        for (auto it = snapshots.rbegin(); it != snapshots.rend(); ++it)
            body += "- " + *it + "\n";
        if (header.size() >= m_config->snapshotHistoryMaximumCharacters)
            return header.substr(0, m_config->snapshotHistoryMaximumCharacters);
        return header + TailBounded(body, m_config->snapshotHistoryMaximumCharacters - header.size());
    }

    void Manager::AddHistory(ChatRequest const& request, std::string const& reply)
    {
        if (!m_config->historyStorageMode)
            return;
        size_t const maximum = m_config->historyStorageMode == 2
            ? m_config->historyDatabaseMaximumTurns : m_config->historyRamMaximumTurns;
        if (!maximum)
            return;
        HistoryTurn turn;
        turn.speakerMessage = request.incomingMessage;
        turn.actorReply = reply;
        turn.created = Clock::now();
        turn.createdUnix = UnixNow();
        auto& history = m_history[request.historyKey];
        history.push_back(std::move(turn));
        while (history.size() > maximum)
            history.pop_front();

        if (m_config->historyStorageMode == 2 && m_historyDatabaseAvailable)
        {
            PendingHistoryWrite write;
            write.historyKey = request.historyKey;
            write.request = request;
            write.reply = reply;
            write.createdUnix = UnixNow();
            m_pendingHistoryWrites.push_back(std::move(write));
        }
    }

    void Manager::AddSnapshotHistory(ChatRequest const& request, std::string const& snapshotText)
    {
        if (!m_config->snapshotEnabled || !m_config->snapshotStorageMode ||
            request.actor.kind != ActorKind::PlayerBot || snapshotText.empty())
            return;
        size_t const maximum = m_config->snapshotStorageMode == 2
            ? m_config->snapshotDatabaseMaximumSnapshots : m_config->snapshotRamMaximumSnapshots;
        if (!maximum)
            return;
        std::string const key = ActorKey(request.actor);
        auto& snapshots = m_snapshotHistory[key];
        if (!snapshots.empty() && snapshots.back().text == snapshotText)
            return;
        SnapshotRecord snapshot;
        snapshot.text = snapshotText;
        snapshot.created = Clock::now();
        snapshot.createdUnix = UnixNow();
        snapshots.push_back(snapshot);
        while (snapshots.size() > maximum)
            snapshots.pop_front();

        if (m_config->snapshotStorageMode == 2 && m_snapshotDatabaseAvailable)
        {
            PendingSnapshotWrite write;
            write.actorKey = key;
            write.request = request;
            write.snapshot = snapshotText;
            write.createdUnix = snapshot.createdUnix;
            m_pendingSnapshotWrites.push_back(std::move(write));
        }
    }

    void Manager::RecordSurroundingChat(ChatScope scope, std::string const& channelName,
                                        ActorSnapshot const& location, SpeakerSnapshot const& speaker,
                                        std::string const& message)
    {
        if (!m_config->surroundingChatEnabled || !m_config->surroundingChatMaximumLines || message.empty())
            return;
        std::string const key = ScopeKey(scope, channelName, location, speaker);
        auto& lines = m_surroundingChat[key];
        if (!lines.empty() && lines.back().speakerGuid == speaker.guid && lines.back().message == message)
            return;
        RecentChatLine line;
        line.speakerGuid = speaker.guid;
        line.speakerName = speaker.name.empty() ? "unknown" : speaker.name;
        line.message = HeadBounded(message, 500);
        line.created = Clock::now();
        lines.push_back(std::move(line));
        while (lines.size() > m_config->surroundingChatMaximumLines)
            lines.pop_front();
        while (m_surroundingChat.size() > m_config->surroundingChatMaximumScopes)
            m_surroundingChat.erase(m_surroundingChat.begin());
    }

    void Manager::PruneHistory()
    {
        if (!m_config)
            return;
        auto const now = Clock::now();
        auto cutoff = now - std::chrono::minutes(m_config->historyTtlMinutes);
        for (auto mapIt = m_history.begin(); mapIt != m_history.end(); )
        {
            auto& turns = mapIt->second;
            while (!turns.empty() && turns.front().created < cutoff)
                turns.pop_front();
            if (turns.empty())
            {
                m_databaseLoadedHistoryKeys.erase(mapIt->first);
                mapIt = m_history.erase(mapIt);
            }
            else
                ++mapIt;
        }
        while (m_history.size() > m_config->historyMaximumConversations)
        {
            m_databaseLoadedHistoryKeys.erase(m_history.begin()->first);
            m_history.erase(m_history.begin());
        }
        while (m_databaseLoadedHistoryKeys.size() > m_config->historyMaximumConversations)
            m_databaseLoadedHistoryKeys.erase(m_databaseLoadedHistoryKeys.begin());

        auto surroundingCutoff = now - std::chrono::minutes(m_config->surroundingChatTtlMinutes);
        for (auto mapIt = m_surroundingChat.begin(); mapIt != m_surroundingChat.end(); )
        {
            while (!mapIt->second.empty() && mapIt->second.front().created < surroundingCutoff)
                mapIt->second.pop_front();
            if (mapIt->second.empty())
                mapIt = m_surroundingChat.erase(mapIt);
            else
                ++mapIt;
        }

        auto snapshotCutoff = now - std::chrono::minutes(m_config->snapshotHistoryTtlMinutes);
        for (auto mapIt = m_snapshotHistory.begin(); mapIt != m_snapshotHistory.end(); )
        {
            while (!mapIt->second.empty() && mapIt->second.front().created < snapshotCutoff)
                mapIt->second.pop_front();
            if (mapIt->second.empty())
            {
                m_databaseLoadedSnapshotKeys.erase(mapIt->first);
                mapIt = m_snapshotHistory.erase(mapIt);
            }
            else
                ++mapIt;
        }
        while (m_snapshotHistory.size() > m_config->snapshotHistoryMaximumActors)
        {
            m_databaseLoadedSnapshotKeys.erase(m_snapshotHistory.begin()->first);
            m_snapshotHistory.erase(m_snapshotHistory.begin());
        }
        while (m_databaseLoadedSnapshotKeys.size() > m_config->snapshotHistoryMaximumActors)
            m_databaseLoadedSnapshotKeys.erase(m_databaseLoadedSnapshotKeys.begin());
    }

    void Manager::ClearHistory()
    {
        m_history.clear();
        m_surroundingChat.clear();
        m_snapshotHistory.clear();
        m_databaseLoadedHistoryKeys.clear();
        m_databaseLoadedSnapshotKeys.clear();
        m_pendingHistoryWrites.clear();
        m_pendingSnapshotWrites.clear();
        if (m_historyDatabaseAvailable)
        {
            CharacterDatabase.PExecute("DELETE FROM `azeroth_voices_chat_history`");
            sLog.outString("[AzerothVoices][HISTORY][SQL] Cached and persistent conversation history clear was queued.");
        }
        if (m_snapshotDatabaseAvailable)
        {
            CharacterDatabase.PExecute("DELETE FROM `azeroth_voices_environment_history`");
            sLog.outString("[AzerothVoices][SNAPSHOT][SQL] Cached and persistent snapshot history clear was queued.");
        }
    }

    void Manager::InitializeDatabaseStorage()
    {
        m_historyDatabaseAvailable = false;
        m_snapshotDatabaseAvailable = false;
        m_personalityDatabaseAvailable = false;
        if (!m_config)
            return;

        if (m_config->historyStorageMode == 2)
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_chat_history'"));
            m_historyDatabaseAvailable = table != nullptr;
            if (!m_historyDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL conversation history table is missing; falling back to bounded RAM. Install data/sql/character/20260827_01_azeroth_voices_history.sql.");
            else
                sLog.outString("[AzerothVoices][HISTORY][SQL] Persistent conversation history storage is available.");
        }
        if (m_config->snapshotStorageMode == 2)
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_environment_history'"));
            m_snapshotDatabaseAvailable = table != nullptr;
            if (!m_snapshotDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL snapshot history table is missing; falling back to bounded RAM. Install data/sql/character/20260827_01_azeroth_voices_history.sql.");
            else
                sLog.outString("[AzerothVoices][SNAPSHOT][SQL] Persistent snapshot history storage is available.");
        }
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_bot_personality'"));
            m_personalityDatabaseAvailable = table != nullptr;
            if (m_config->personalityEnabled && !m_personalityDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL personality table is missing; using a bounded non-persistent RAM cache. Install data/sql/character/20260829_01_azeroth_voices_personality.sql.");
            else if (m_config->personalityEnabled)
                sLog.outString("[AzerothVoices][PERSONALITY][SQL] Persistent PlayerBot personality storage is available.");
        }
        if (m_historyDatabaseAvailable || m_snapshotDatabaseAvailable)
            CleanupDatabase();
    }

    void Manager::LoadDatabaseHistory(ChatRequest const& request)
    {
        if (!m_historyDatabaseAvailable || m_databaseLoadedHistoryKeys.count(request.historyKey))
            return;
        m_databaseLoadedHistoryKeys.insert(request.historyKey);
        if (!m_config->historyDatabaseMaximumTurns)
            return;

        std::string key = request.historyKey;
        CharacterDatabase.escape_string(key);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `speaker_message`, `actor_reply`, UNIX_TIMESTAMP(`created_at`) "
            "FROM `azeroth_voices_chat_history` WHERE `history_key`='%s' "
            "AND `created_at` >= DATE_SUB(NOW(), INTERVAL %u MINUTE) "
            "ORDER BY `id` DESC LIMIT %u",
            key.c_str(), m_config->historyDatabaseTtlMinutes,
            m_config->historyDatabaseMaximumTurns));
        if (!result)
            return;

        uint64_t const nowUnix = UnixNow();
        auto& history = m_history[request.historyKey];
        do
        {
            Field* fields = result->Fetch();
            HistoryTurn turn;
            turn.speakerMessage = fields[0].GetCppString();
            turn.actorReply = fields[1].GetCppString();
            turn.createdUnix = fields[2].GetUInt64();
            uint64_t age = nowUnix > turn.createdUnix ? nowUnix - turn.createdUnix : 0;
            turn.created = Clock::now() - std::chrono::seconds(age);
            history.push_front(std::move(turn));
        } while (result->NextRow());
    }

    void Manager::LoadDatabaseSnapshot(ChatRequest const& request)
    {
        std::string const actorKey = ActorKey(request.actor);
        if (!m_snapshotDatabaseAvailable || m_databaseLoadedSnapshotKeys.count(actorKey))
            return;
        m_databaseLoadedSnapshotKeys.insert(actorKey);
        if (!m_config->snapshotDatabaseMaximumSnapshots)
            return;

        std::string key = actorKey;
        CharacterDatabase.escape_string(key);
        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `snapshot`, UNIX_TIMESTAMP(`created_at`) "
            "FROM `azeroth_voices_environment_history` WHERE `actor_key`='%s' "
            "AND `created_at` >= DATE_SUB(NOW(), INTERVAL %u MINUTE) "
            "ORDER BY `id` DESC LIMIT %u",
            key.c_str(), m_config->snapshotDatabaseTtlMinutes,
            m_config->snapshotDatabaseMaximumSnapshots));
        if (!result)
            return;

        uint64_t const nowUnix = UnixNow();
        auto& snapshots = m_snapshotHistory[actorKey];
        do
        {
            Field* fields = result->Fetch();
            SnapshotRecord snapshot;
            snapshot.text = fields[0].GetCppString();
            snapshot.createdUnix = fields[1].GetUInt64();
            uint64_t age = nowUnix > snapshot.createdUnix ? nowUnix - snapshot.createdUnix : 0;
            snapshot.created = Clock::now() - std::chrono::seconds(age);
            snapshots.push_front(std::move(snapshot));
        } while (result->NextRow());
    }

    void Manager::FlushDatabaseWrites(bool force)
    {
        if (!m_config || (!m_historyDatabaseAvailable && !m_snapshotDatabaseAvailable))
            return;
        auto const now = Clock::now();
        size_t pending = m_pendingHistoryWrites.size() + m_pendingSnapshotWrites.size();
        if (!pending)
            return;
        if (!force && pending < m_config->historyDatabaseFlushBatchSize && now < m_nextDatabaseFlush)
            return;

        size_t const batch = force ? pending : std::min<size_t>(pending, m_config->historyDatabaseFlushBatchSize);
        if (!CharacterDatabase.BeginTransaction())
        {
            sLog.outError("[AzerothVoices] Could not begin the asynchronous history transaction; retaining RAM history.");
            m_historyDatabaseAvailable = false;
            m_snapshotDatabaseAvailable = false;
            return;
        }

        std::set<std::string> touchedHistoryKeys;
        std::set<std::string> touchedActorKeys;
        size_t written = 0;
        while (written < batch && !m_pendingHistoryWrites.empty())
        {
            PendingHistoryWrite write = std::move(m_pendingHistoryWrites.front());
            m_pendingHistoryWrites.pop_front();
            std::string key = write.historyKey;
            std::string channel = write.request.channelName;
            std::string speakerName = write.request.speaker.name;
            std::string speakerMessage = HeadBounded(write.request.incomingMessage, 4000);
            std::string actorName = write.request.actor.name;
            std::string reply = HeadBounded(write.reply, 4000);
            CharacterDatabase.escape_string(key);
            CharacterDatabase.escape_string(channel);
            CharacterDatabase.escape_string(speakerName);
            CharacterDatabase.escape_string(speakerMessage);
            CharacterDatabase.escape_string(actorName);
            CharacterDatabase.escape_string(reply);
            CharacterDatabase.PExecute(
                "INSERT INTO `azeroth_voices_chat_history` "
                "(`history_key`,`actor_guid`,`speaker_guid`,`actor_kind`,`scope`,`channel_name`,"
                "`speaker_name`,`speaker_message`,`actor_name`,`actor_reply`,`created_at`) "
                "VALUES ('%s','%llu','%llu','%u','%u','%s','%s','%s','%s','%s',FROM_UNIXTIME('%llu'))",
                key.c_str(), static_cast<unsigned long long>(write.request.actor.guid),
                static_cast<unsigned long long>(write.request.speaker.guid),
                static_cast<unsigned>(write.request.actor.kind), static_cast<unsigned>(write.request.scope),
                channel.c_str(), speakerName.c_str(), speakerMessage.c_str(), actorName.c_str(), reply.c_str(),
                static_cast<unsigned long long>(write.createdUnix));
            touchedHistoryKeys.insert(write.historyKey);
            ++written;
        }
        while (written < batch && !m_pendingSnapshotWrites.empty())
        {
            PendingSnapshotWrite write = std::move(m_pendingSnapshotWrites.front());
            m_pendingSnapshotWrites.pop_front();
            std::string key = write.actorKey;
            std::string actorName = write.request.actor.name;
            std::string snapshot = HeadBounded(write.snapshot, 8000);
            CharacterDatabase.escape_string(key);
            CharacterDatabase.escape_string(actorName);
            CharacterDatabase.escape_string(snapshot);
            CharacterDatabase.PExecute(
                "INSERT INTO `azeroth_voices_environment_history` "
                "(`actor_key`,`actor_guid`,`actor_kind`,`actor_name`,`map_id`,`zone_id`,`area_id`,`snapshot`,`created_at`) "
                "VALUES ('%s','%llu','%u','%s','%u','%u','%u','%s',FROM_UNIXTIME('%llu'))",
                key.c_str(), static_cast<unsigned long long>(write.request.actor.guid),
                static_cast<unsigned>(write.request.actor.kind), actorName.c_str(),
                write.request.actor.mapId, write.request.actor.zoneId, write.request.actor.areaId,
                snapshot.c_str(), static_cast<unsigned long long>(write.createdUnix));
            touchedActorKeys.insert(write.actorKey);
            ++written;
        }

        for (std::string key : touchedHistoryKeys)
        {
            CharacterDatabase.escape_string(key);
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_chat_history` WHERE `history_key`='%s' AND `id` NOT IN "
                "(SELECT `id` FROM (SELECT `id` FROM `azeroth_voices_chat_history` "
                "WHERE `history_key`='%s' ORDER BY `id` DESC LIMIT %u) AS `av_keep`)",
                key.c_str(), key.c_str(), m_config->historyDatabaseMaximumTurns);
        }
        for (std::string key : touchedActorKeys)
        {
            CharacterDatabase.escape_string(key);
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_environment_history` WHERE `actor_key`='%s' AND `id` NOT IN "
                "(SELECT `id` FROM (SELECT `id` FROM `azeroth_voices_environment_history` "
                "WHERE `actor_key`='%s' ORDER BY `id` DESC LIMIT %u) AS `av_keep`)",
                key.c_str(), key.c_str(), m_config->snapshotDatabaseMaximumSnapshots);
        }
        CharacterDatabase.CommitTransaction();
        m_nextDatabaseFlush = now + std::chrono::seconds(m_config->historyDatabaseFlushSeconds);
    }

    void Manager::CleanupDatabase()
    {
        if (!m_config || (!m_historyDatabaseAvailable && !m_snapshotDatabaseAvailable))
            return;
        if (m_historyDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_chat_history` WHERE `created_at` < DATE_SUB(NOW(), INTERVAL %u MINUTE)",
                m_config->historyDatabaseTtlMinutes);
        if (m_snapshotDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_environment_history` WHERE `created_at` < DATE_SUB(NOW(), INTERVAL %u MINUTE)",
                m_config->snapshotDatabaseTtlMinutes);
    }

    void Manager::LoadRag()
    {
        if (!m_config->ragEnabled)
        {
            m_rag.clear();
            m_ragFiles = 0;
            m_ragParseFailures = 0;
            return;
        }
        if (!m_config->ragReloadOnRestart && !m_rag.empty())
            return;
        m_ragFiles = 0;
        m_ragParseFailures = 0;
        m_rag.clear();

        try
        {
        fs::path directory(m_config->ragDirectory);
        std::vector<fs::path> candidates = { directory, fs::path("modules/mod-azeroth-voices/data/rag") };
#ifdef TW_SOURCE_MODULES_DIR
        candidates.push_back(fs::path(TW_SOURCE_MODULES_DIR) / "mod-azeroth-voices" / "data" / "rag");
#endif
        for (fs::path const& candidate : candidates)
            if (fs::exists(candidate) && fs::is_directory(candidate))
            {
                directory = candidate;
                break;
            }
        if (!fs::exists(directory) || !fs::is_directory(directory))
        {
            sLog.outError("[AzerothVoices] RAG directory was not found: %s", m_config->ragDirectory.c_str());
            return;
        }

        auto appendItem = [&](Json const& value, std::string const& fileName) {
            if (!value.is_object())
                return;
            RagItem item;
            auto stringField = [&](char const* name) -> std::string {
                return value.count(name) && value[name].is_string() ? value[name].get<std::string>() : "";
            };
            item.id = stringField("id");
            item.title = stringField("title");
            item.category = stringField("category");
            item.source = stringField("source");
            item.text = stringField("text");
            if (item.text.empty())
                item.text = stringField("content");
            auto appendKeywords = [&](char const* name) {
                if (!value.count(name))
                    return;
                Json const& field = value[name];
                if (field.is_array())
                    for (Json const& keyword : field)
                        if (keyword.is_string())
                            item.keywords.push_back(Lower(Trim(keyword.get<std::string>())));
                else if (field.is_string())
                {
                    std::vector<std::string> values = Split(field.get<std::string>(), ',');
                    item.keywords.insert(item.keywords.end(), values.begin(), values.end());
                }
            };
            appendKeywords("keywords");
            appendKeywords("tags");
            if (item.keywords.empty())
                item.keywords = Words(item.title + " " + item.category + " " + item.text);
            if (!item.text.empty())
            {
                for (std::string const& keyword : item.keywords)
                    for (std::string const& word : Words(keyword))
                        item.keywordWords.insert(word);
                for (std::string const& word : Words(item.title + " " + item.category))
                    item.headingWords.insert(word);
                for (std::string const& word : Words(item.text))
                    item.contentWords.insert(word);
                if (item.source.empty())
                    item.source = fileName;
                m_rag.push_back(std::move(item));
            }
        };

        std::vector<fs::path> ragFiles;
        for (fs::directory_entry const& entry : fs::directory_iterator(directory))
            if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json")
                ragFiles.push_back(entry.path());
        std::sort(ragFiles.begin(), ragFiles.end());

        size_t loadedFiles = 0;
        for (fs::path const& path : ragFiles)
        {
            try
            {
                std::ifstream input(path);
                Json root;
                input >> root;
                if (root.is_array())
                    for (Json const& value : root)
                        appendItem(value, path.filename().string());
                else if (root.is_object() && root.count("entries") && root["entries"].is_array())
                    for (Json const& value : root["entries"])
                        appendItem(value, path.filename().string());
                else if (root.is_object() && root.count("items") && root["items"].is_array())
                    for (Json const& value : root["items"])
                        appendItem(value, path.filename().string());
                else
                    appendItem(root, path.filename().string());
                ++loadedFiles;
            }
            catch (std::exception const& exception)
            {
                ++m_ragParseFailures;
                sLog.outError("[AzerothVoices] Could not load RAG file %s: %s",
                    path.string().c_str(), exception.what());
            }
        }
        m_ragFiles = loadedFiles;
        sLog.outString("[AzerothVoices][RAG] Loaded %u structured RAG entries from %u JSON files in %s; parse failures=%u.",
            static_cast<unsigned>(m_rag.size()), static_cast<unsigned>(loadedFiles), directory.string().c_str(),
            static_cast<unsigned>(m_ragParseFailures));
        }
        catch (std::exception const& exception)
        {
            ++m_ragParseFailures;
            sLog.outError("[AzerothVoices] RAG directory scan failed for %s: %s",
                m_config->ragDirectory.c_str(), exception.what());
        }
    }

    std::string Manager::SelectRag(ChatRequest const& request) const
    {
        if (!m_config->ragEnabled || m_rag.empty())
            return "";
        std::set<std::string> inputWords;
        for (std::string const& word : Words(request.incomingMessage + " " + request.trigger + " " +
                                              request.actor.area + " " + request.actor.zone + " " + request.actor.map))
            inputWords.insert(word);
        if (inputWords.empty())
            return "";

        std::vector<std::pair<float, size_t>> scores;
        for (size_t i = 0; i < m_rag.size(); ++i)
        {
            std::vector<float> matchedWeights;
            matchedWeights.reserve(inputWords.size());
            for (std::string const& word : inputWords)
            {
                if (m_rag[i].keywordWords.count(word))
                    matchedWeights.push_back(1.0f);
                else if (m_rag[i].headingWords.count(word))
                    matchedWeights.push_back(0.8f);
                else if (m_rag[i].contentWords.count(word))
                    matchedWeights.push_back(0.35f);
            }
            std::sort(matchedWeights.begin(), matchedWeights.end(), std::greater<float>());
            size_t const comparisonTerms = std::min<size_t>(3, inputWords.size());
            float similarity = 0.0f;
            for (size_t match = 0; match < std::min(comparisonTerms, matchedWeights.size()); ++match)
                similarity += matchedWeights[match];
            similarity /= static_cast<float>(comparisonTerms);
            if (similarity >= m_config->ragSimilarityThreshold)
                scores.emplace_back(similarity, i);
        }
        std::stable_sort(scores.begin(), scores.end(), [](auto const& left, auto const& right) {
            return left.first > right.first;
        });

        std::vector<std::pair<float, size_t>> selected;
        selected.reserve(std::min<size_t>(scores.size(), m_config->ragMaximumItems));
        std::set<std::string> selectedTitles;
        for (std::pair<float, size_t> const& score : scores)
        {
            RagItem const& item = m_rag[score.second];
            std::string const identity = Lower(Trim(item.title.empty() ? item.text : item.title));
            if (!identity.empty() && !selectedTitles.insert(identity).second)
                continue;
            selected.push_back(score);
            if (selected.size() >= m_config->ragMaximumItems)
                break;
        }
        if (m_config->debug)
            sLog.outDebug("[AzerothVoices] RAG selected %u of %u loaded entries.",
                static_cast<unsigned>(selected.size()), static_cast<unsigned>(m_rag.size()));

        std::string result;
        for (size_t i = 0; i < selected.size(); ++i)
        {
            RagItem const& item = m_rag[selected[i].second];
            std::string line = "- ";
            if (!item.title.empty())
                line += item.title + ": ";
            line += item.text;
            if (!result.empty())
                line = "\n" + line;
            if (result.size() + line.size() > m_config->ragMaximumCharacters)
            {
                if (result.empty())
                    result = HeadBounded(line, m_config->ragMaximumCharacters);
                break;
            }
            result += line;
        }
        return result;
    }

    void Manager::SetPaused(bool paused)
    {
        m_paused = paused;
    }

    bool Manager::IsPaused() const
    {
        return m_paused;
    }

    std::vector<NaturalCommandAuditRecord> Manager::GetNaturalCommandAudit(size_t maximum) const
    {
        std::vector<NaturalCommandAuditRecord> result;
        if (!maximum)
            return result;
        maximum = std::min(maximum, m_naturalCommandAudit.size());
        result.reserve(maximum);
        for (auto entry = m_naturalCommandAudit.rbegin();
             entry != m_naturalCommandAudit.rend() && result.size() < maximum; ++entry)
            result.push_back(*entry);
        return result;
    }

    void Manager::ClearNaturalCommandAudit()
    {
        m_naturalCommandAudit.clear();
    }

    StatusSnapshot Manager::GetStatus() const
    {
        StatusSnapshot status;
        status.configurationLoaded = m_config != nullptr;
        status.enabled = m_started && m_config && m_config->enabled;
        status.paused = m_paused;
        status.workers = static_cast<uint32_t>(m_workers.size());
        status.inFlight = m_inFlight;
        status.accepted = m_accepted;
        status.completed = m_completed;
        status.failed = m_failed;
        status.dropped = m_dropped;
        status.conversations = m_history.size();
        status.surroundingScopes = m_surroundingChat.size();
        status.snapshotHistories = m_snapshotHistory.size();
        status.historyDatabaseAvailable = m_historyDatabaseAvailable;
        status.snapshotDatabaseAvailable = m_snapshotDatabaseAvailable;
        status.personalityDatabaseAvailable = m_personalityDatabaseAvailable;
        status.personalities = m_personalities.size();
        status.personalityGenerationsPending = m_pendingPersonalityRequests.size();
        status.ragEntries = m_rag.size();
        status.ragFiles = m_ragFiles;
        status.ragParseFailures = m_ragParseFailures;
        status.scheduledLines = m_scheduled.size();
        if (m_config)
        {
            status.endpoint = SanitizeEndpoint(m_config->endpoint);
            status.model = m_config->model;
            status.worldChannelName = m_config->worldChannelName;
            status.historyStorageMode = m_config->historyStorageMode;
            status.snapshotStorageMode = m_config->snapshotStorageMode;
            status.personalityEnabled = m_config->personalityEnabled;
            status.ragEnabled = m_config->ragEnabled;
            status.environmentEnabled = m_config->environmentContextEnabled;
            status.snapshotEnabled = m_config->snapshotEnabled;
            status.naturalCommandsEnabled = m_config->naturalCommandsEnabled;
            status.naturalCommandActions = m_config->naturalCommandsAllowedActions.size();
            status.naturalCommandShortlistMaximum = m_config->naturalCommandsShortlistMaximum;
            status.naturalCommandEffectiveShortlistMaximum =
                m_config->naturalCommandsShortlistMaximum == 0
                ? status.naturalCommandActions
                : std::min<size_t>(m_config->naturalCommandsShortlistMaximum,
                    status.naturalCommandActions);
            status.naturalCommandMaximumRecipients = m_config->naturalCommandsMaximumRecipients;
            status.naturalCommandMaximumActions = m_config->naturalCommandsMaximumActions;
            status.naturalCommandPrefixConfigured = !PlayerbotBridge::CommandPrefix().empty();
            status.naturalConfirmationsPending = m_pendingNaturalConfirmations.size();
            status.naturalClassified = m_naturalClassified;
            status.naturalDispatched = m_naturalDispatched;
            status.naturalRejected = m_naturalRejected;
            status.naturalExpired = m_naturalExpired;
            status.naturalConsidered = m_naturalConsidered;
            status.naturalLocalFastPath = m_naturalLocalFastPath;
            status.naturalClassifierQueued = m_naturalClassifierQueued;
            status.naturalAverageShortlist = m_naturalClassifierQueued
                ? static_cast<size_t>(m_naturalShortlistActions / m_naturalClassifierQueued) : 0;
            status.naturalAveragePromptCharacters = m_naturalClassifierQueued
                ? static_cast<size_t>(m_naturalPromptCharacters / m_naturalClassifierQueued) : 0;
            status.naturalAverageClassifierLatencyMilliseconds = m_naturalClassifierResults
                ? static_cast<uint32_t>(m_naturalClassifierLatencyMilliseconds /
                    m_naturalClassifierResults) : 0;
            status.naturalCommandModel = m_config->naturalCommandsModel.empty()
                ? m_config->model : m_config->naturalCommandsModel;
            status.naturalAcknowledgementMode = m_config->naturalCommandsAcknowledgementMode;
            status.naturalMostUsedActions = NaturalCommandMostUsedActions(5);
            status.naturalAuditEnabled = m_config->naturalCommandsAuditEnabled;
            status.naturalAuditRecords = m_naturalCommandAudit.size();
            status.naturalLastFailure = m_naturalLastFailure;
            status.apiConfigured = !m_config->endpoint.empty() &&
                (!m_config->model.empty() || !m_config->apiJsonTemplate.empty()) &&
                (!m_config->ResolveApiKey().empty() || IsLocalEndpoint(m_config->endpoint));
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto const& queue : m_queues)
                status.queued += queue.size();
            std::set<uint64_t> pendingRequestIds;
            for (auto const& pending : m_pendingNaturalCommandsByActor)
                pendingRequestIds.insert(pending.second.begin(), pending.second.end());
            status.naturalCommandsPending = pendingRequestIds.size();
        }
        return status;
    }
}
