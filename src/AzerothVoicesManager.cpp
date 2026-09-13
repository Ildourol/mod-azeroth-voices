#include "AzerothVoicesManager.h"

#include "AzerothVoicesAddon.h"
#include "AzerothVoicesBossDialogue.h"
#include "AzerothVoicesPersonality.h"
#include "AzerothVoicesProximity.h"
#include "AzerothVoicesProvider.h"
#include "AzerothVoicesReasoning.h"
#include "AzerothVoicesSentiment.h"
#include "AzerothVoicesSocial.h"

#include "Cell.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCStores.h"
#include "GameEventMgr.h"
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
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <list>
#include <memory>
#include <random>
#include <set>
#include <sstream>

namespace AzerothVoices
{
    struct Manager::Candidate
    {
        ActorSnapshot actor;
        uint32_t chance = 0;
        int score = 0;
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
        constexpr uint64_t SecondsPerDay = 24 * 60 * 60;
        constexpr uint32_t SentimentNeutralRetentionDays = 90;

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

        bool IsDirectSentimentConversation(Player const* actor, Player const* target,
                                           ChatScope scope, std::string const& message)
        {
            if (!actor || !actor->IsInWorld() || !Script_IsAIControlled(actor) ||
                !IsOnlineRealPlayer(target))
                return false;

            if (scope == ChatScope::Whisper)
                return true;

            bool const named = IsExplicitPlayerBotNameMention(message, actor->GetName());
            if (scope == ChatScope::Say || scope == ChatScope::World)
                return named;
            if (scope != ChatScope::Party)
                return false;

            Group const* group = target->GetGroup();
            if (!group || actor->GetGroup() != group || !group->SameSubGroup(target, actor))
                return false;

            uint32_t playerBotsInSubgroup = 0;
            uint8_t const subgroup = group->GetMemberGroup(target->GetObjectGuid());
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                if (slot.group != subgroup)
                    continue;
                Player* member = ObjectAccessor::FindPlayer(slot.guid);
                if (!member || !member->IsInWorld() || !Script_IsAIControlled(member))
                    continue;
                if (++playerBotsInSubgroup > 1)
                    break;
            }
            return playerBotsInSubgroup == 1 || named;
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
            NoHumanNearby,
            Boss
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
                if (creatureFaction->IsHostileTo(*playableFaction))
                    hostileToPlayableFaction = true;
                else if (creatureFaction->IsFriendlyTo(*playableFaction) ||
                         playableFaction->IsFriendlyTo(*creatureFaction))
                    return REP_FRIENDLY;
            }
            return hostileToPlayableFaction ? REP_HOSTILE : REP_NEUTRAL;
        }

        BossClassification ClassifyCreatureBoss(Creature const* creature, Config const& config)
        {
            if (!creature || !creature->GetCreatureInfo())
                return BossClassification();
            CreatureInfo const* info = creature->GetCreatureInfo();
            Map const* map = creature->FindMap();
            bool const inDungeonOrRaid = map && map->IsDungeon();
            return ClassifyBoss(info->entry, info->rank, info->flags_extra, inDungeonOrRaid,
                config.bossDialogueAllowEntries.count(info->entry) != 0,
                config.bossDialogueDenyEntries.count(info->entry) != 0);
        }

        NpcEligibilityResult EvaluateNpcSpeaker(Creature const* creature, float observerDistance,
                                                Config const& config)
        {
            if (!creature || !creature->IsInWorld() || !creature->IsAlive() ||
                !creature->GetCreatureInfo())
                return NpcEligibilityResult::Invalid;
            if (creature->IsPet() || creature->IsTotem() || creature->IsTemporarySummon() ||
                creature->IsCharmed() || !creature->GetCharmerOrOwnerGuid().IsEmpty() ||
                creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_POSSESSED) ||
                creature->IsTrigger() || creature->IsCritter() ||
                !creature->HasStaticDBSpawnData())
                return NpcEligibilityResult::Temporary;

            if (!config.npcAllowedTypes.count(creature->GetCreatureInfo()->type))
                return NpcEligibilityResult::Filtered;

            // A classified boss has exactly one owner: the pre-aggro boss
            // subsystem. Ordinary ambient, event, combat-start, and proximity
            // paths must stay silent for it even though it is a static,
            // allowed-type creature.
            if (ClassifyCreatureBoss(creature, config).boss)
                return NpcEligibilityResult::Boss;

            ReputationRank const reaction = NpcPlayableFactionReaction(creature);
            if (reaction == REP_NEUTRAL && !config.npcAllowNeutralAndHostile)
                return NpcEligibilityResult::Neutral;
            if (reaction < REP_NEUTRAL && !config.npcAllowNeutralAndHostile)
                return NpcEligibilityResult::Hostile;
            if (!HasNearbyRealPlayer(creature, observerDistance))
                return NpcEligibilityResult::NoHumanNearby;
            return NpcEligibilityResult::Eligible;
        }

        std::string CreatureRoleName(Creature const* creature)
        {
            if (!creature)
                return "";
            if (creature->IsGuard())
                return "guard";
            if (creature->IsVendor())
                return "vendor";
            if (creature->IsTrainer())
                return "trainer";
            if (creature->IsQuestGiver())
                return "quest giver";
            if (creature->IsInnkeeper())
                return "innkeeper";
            if (creature->IsTaxi())
                return "flight master";
            if (creature->IsBanker())
                return "banker";
            if (creature->IsBattleMaster())
                return "battlemaster";
            if (creature->IsSpiritService())
                return "spirit healer";
            if (creature->IsAuctioner())
                return "auctioneer";
            if (creature->IsArmorer())
                return "repairer";
            if (creature->IsGuildMaster())
                return "guild master";
            if (creature->IsGossip())
                return "gossip";
            return "ordinary";
        }

        bool IsGuardOrInteractiveRole(Creature const* creature)
        {
            if (!creature)
                return true;
            return creature->IsGuard() || creature->IsServiceProvider() ||
                creature->IsGossip() || creature->IsQuestGiver() ||
                creature->IsSpiritService() || creature->IsArmorer() ||
                creature->IsAuctioner() || creature->IsGuildMaster() ||
                creature->IsTabardDesigner();
        }

        std::string InstanceIdentity(uint32_t mapId, uint32_t instanceId)
        {
            return std::to_string(mapId) + ':' + std::to_string(instanceId);
        }

        uint32_t CurrentInstanceId(WorldObject const* object)
        {
            if (!object)
                return 0;
            Map const* map = object->FindMap();
            return map ? map->GetInstanceId() : 0;
        }

        // Ordered proximity policy for one live creature. Every rejection reason
        // is returned so telemetry can explain which rule won.
        ProximitySpeakerDecision EvaluateProximityCreature(Creature const* creature,
                                                           Config const& config)
        {
            ProximitySpeakerInput input;
            CreatureInfo const* info = creature ? creature->GetCreatureInfo() : nullptr;
            input.safetyExcluded = !creature || !info || !creature->IsInWorld() ||
                !creature->IsAlive() || creature->IsPet() || creature->IsTotem() ||
                creature->IsTemporarySummon() || creature->IsCharmed() ||
                !creature->GetCharmerOrOwnerGuid().IsEmpty() ||
                creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_POSSESSED) ||
                creature->IsTrigger() || creature->IsCritter() || !creature->HasStaticDBSpawnData();
            if (input.safetyExcluded)
                return EvaluateProximitySpeaker(input);

            input.denied = config.proximitySpeakerDenyEntries.count(info->entry) != 0;
            input.boss = ClassifyCreatureBoss(creature, config).boss;
            input.guardOrService = IsGuardOrInteractiveRole(creature);
            input.creatureTypeKnown = true;
            input.humanoid = info->type == CREATURE_TYPE_HUMANOID;
            input.allowListEmpty = config.proximitySpeakerAllowEntries.empty();
            input.entryAllowed = config.proximitySpeakerAllowEntries.count(info->entry) != 0;
            input.entryAllowedAsNonHumanoid =
                config.proximitySpeakerNonHumanoidAllowEntries.count(info->entry) != 0;
            return EvaluateProximitySpeaker(input);
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
            return IsCommandIgnored(message, config.commandBlacklist);
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
            result.instanceId = CurrentInstanceId(player);
            if (Map const* map = player->FindMap())
                result.instance = map->IsDungeon();
            FillLocation(player, result.area, result.zone, result.map,
                         result.mapId, result.areaId, result.zoneId);
            return result;
        }

        ActorSnapshot SnapshotCreature(Creature const* creature, Player const* anchor,
                                       NpcDisposition disposition,
                                       std::string qualification = "",
                                       bool boss = false)
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
            if (CreatureInfo const* info = creature->GetCreatureInfo())
            {
                result.creatureEntry = info->entry;
                result.creatureType = info->type;
                result.creatureRank = info->rank;
            }
            result.instanceId = CurrentInstanceId(creature);
            if (Map const* map = creature->FindMap())
                result.instance = map->IsDungeon();
            result.role = CreatureRoleName(creature);
            result.qualification = std::move(qualification);
            result.boss = boss;
            result.staticSpawn = creature->HasStaticDBSpawnData();
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
            ReplaceAll(value, "<bot memory block>", request.memoryBlock);
            ReplaceAll(value, "<bot memory player>", request.memoryTargetName);
            ReplaceAll(value, "<bot type>", request.actor.kind == ActorKind::Creature ? "NPC" : "playerbot");
            ReplaceAll(value, "<bot creature type>", request.actor.creatureType
                ? std::to_string(request.actor.creatureType) : "");
            ReplaceAll(value, "<bot rank>", request.actor.creatureRank
                ? std::to_string(request.actor.creatureRank) : "");
            ReplaceAll(value, "<bot role>", request.actor.role);
            ReplaceAll(value, "<bot qualification>", request.actor.qualification);
            ReplaceAll(value, "<instance name>", request.actor.map);
            ReplaceAll(value, "<instance area>", request.actor.area);
            ReplaceAll(value, "<instance lore>", request.bossLoreContext);
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

        std::string tlsError;
        if (!Provider::InitializeTls(tlsError))
        {
            sLog.outError("[AzerothVoices] TLS initialization failed: %s", tlsError.c_str());
            return;
        }

        InitializeDatabaseStorage();
        LoadRag();
        LoadInstanceLore();
        m_stopping = false;
        m_started = true;
        m_lastErrorLog = Clock::time_point();
        m_suppressedErrors = 0;
        m_telemetryWindowStarted = Clock::now();
        m_telemetryApiCalls = 0;
        m_telemetrySuccessfulResults = 0;
        m_telemetryFailedResults = 0;
        m_telemetryGeneratedMessages = 0;
        m_thinkingFallbacks = 0;
        m_preflightRejections.fill(0);
        m_nextHistoryPrune = Clock::now() + std::chrono::minutes(1);
        m_nextDatabaseFlush = Clock::now() + std::chrono::seconds(m_config->historyDatabaseFlushSeconds);
        m_nextSentimentDatabaseFlush = Clock::now() +
            std::chrono::seconds(m_config->sentimentDatabaseFlushSeconds);
        m_nextDatabaseCleanup = Clock::now() + std::chrono::hours(1);
        m_nextProximityOutdoorScan = Clock::now() +
            std::chrono::seconds(m_config->proximityOutdoorScanSeconds);
        m_nextProximityInstanceScan = Clock::now() +
            std::chrono::seconds(m_config->proximityInstanceScanSeconds);
        m_nextBossScan = Clock::now() + std::chrono::seconds(m_config->bossDialogueScanSeconds);
        m_nextGroupScan = Clock::now() + std::chrono::seconds(m_config->groupChatterScanSeconds);
        m_nextGuildScan = Clock::now() + std::chrono::seconds(m_config->guildChatterScanSeconds);
        m_nextMemoryFlush = Clock::now() + std::chrono::seconds(m_config->memoryDatabaseFlushSeconds);
        for (uint32_t i = 0; i < m_config->workerThreads; ++i)
            m_workers.emplace_back(&Manager::WorkerLoop, this);
        ScheduleNextAmbient();
        ScheduleNextGeneralTrigger();

        if (m_config->playerbotsLlmEnabled)
        {
            sLog.outError("[AzerothVoices][COMPATIBILITY] AiPlayerbot.LLMEnabled is enabled! mod-azeroth-voices is the sole LLM-chat owner. Disable AiPlayerbot.LLMEnabled (set AiPlayerbot.LLMEnabled = 0 in aiplayerbot.conf) to prevent duplicate generation and conflicting chat.");
        }

        sLog.outString("[AzerothVoices] Started with %u workers, endpoint %s, model %s, proximity %s, boss-dialogue %s, addon %s.",
            m_config->workerThreads, SanitizeEndpoint(m_config->endpoint).c_str(), m_config->model.c_str(),
            m_config->proximityEnabled ? "enabled" : "disabled",
            m_config->bossDialogueEnabled ? "enabled" : "disabled",
            m_config->addonEnabled ? "enabled" : "disabled");
    }

    void Manager::Reload()
    {
        sLog.outString("[AzerothVoices][INIT] Reloading configuration and restarting workers.");
        Start();
    }

    void Manager::Stop()
    {
        if (m_config)
        {
            FlushDatabaseWrites(true);
            FlushSentimentWrites(true);
        }
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
        m_history.clear();
        m_surroundingChat.clear();
        m_snapshotHistory.clear();
        m_personalities.clear();
        m_personalityCacheOrder.clear();
        m_sentiments.clear();
        m_sentimentCacheOrder.clear();
        m_databaseLoadedHistoryKeys.clear();
        m_databaseLoadedSnapshotKeys.clear();
        m_databaseLoadedPersonalityGuids.clear();
        m_databaseLoadedSentimentPairs.clear();
        m_pendingPersonalityRequests.clear();
        m_personalityRetryAfter.clear();
        m_personalityGenerationStatus.clear();
        m_personalityGenerationStatusOrder.clear();
        m_pendingHistoryWrites.clear();
        m_pendingSnapshotWrites.clear();
        m_pendingSentimentWrites.clear();
        m_proximityScenes.clear();
        m_proximityZoneScenes.clear();
        m_proximitySpeakerCooldowns.clear();
        m_bossPresences.clear();
        m_memories.clear();
        m_memoryCacheOrder.clear();
        m_databaseLoadedMemoryPairs.clear();
        m_pendingMemoryWrites.clear();
        m_groupConversations.clear();
        m_groupStates.clear();
        m_groupTriggerCooldowns.clear();
        m_guildGreetingCooldowns.clear();
        m_guildReplyDebounce.clear();
        m_guildLastSpeaker.clear();
        m_nextGroupConversationId = 1;
        m_generalPacing.Clear();
        m_instanceLore.clear();
        m_instanceLoreLoaded = false;
        m_nextProximitySceneId = 1;
        m_historyDatabaseAvailable = false;
        m_snapshotDatabaseAvailable = false;
        m_personalityDatabaseAvailable = false;
        m_sentimentDatabaseAvailable = false;
        m_addonDatabaseAvailable = false;
        m_memoryDatabaseAvailable = false;
        m_telemetryApiCalls = 0;
        m_telemetrySuccessfulResults = 0;
        m_telemetryFailedResults = 0;
        m_telemetryGeneratedMessages = 0;
        m_preflightRejections.fill(0);
        m_inFlight = 0;
    }

    void Manager::Update(uint32_t /*diff*/)
    {
        if (!m_started || !m_config || !m_config->enabled)
            return;
        DrainIngress();
        DrainCompletions();
        ReportTelemetry();
        DeliverScheduled();
        FlushDatabaseWrites();
        FlushSentimentWrites();
        FlushMemoryWrites();
        PruneGeneralPacing();
        PrunePartyPacing();
        PruneGeneralChatter();
        PruneProximityScenes();
        PruneBossPresences();
        PruneGroupAndGuildState();
        ProcessPendingGuildGreetings();
        m_addonReassembler.ExpireStale(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()));
        if (!m_paused && (m_config->groupChatterEnabled || m_config->raidChatterEnabled) &&
            Clock::now() >= m_nextGroupScan)
        {
            RunGroupChatter();
            m_nextGroupScan = Clock::now() +
                std::chrono::seconds(m_config->groupChatterScanSeconds);
        }
        if (!m_paused && m_config->guildChatterEnabled && Clock::now() >= m_nextGuildScan)
        {
            RunGuildChatter();
            m_nextGuildScan = Clock::now() +
                std::chrono::seconds(m_config->guildChatterScanSeconds);
        }
        if (!m_paused && m_config->proximityEnabled &&
            Clock::now() >= m_nextProximityOutdoorScan)
        {
            RunProximity();
            m_nextProximityOutdoorScan = Clock::now() +
                std::chrono::seconds(m_config->proximityOutdoorScanSeconds);
        }
        if (!m_paused && m_config->bossDialogueEnabled &&
            Clock::now() >= m_nextBossScan)
        {
            RunBossDialogue();
            m_nextBossScan = Clock::now() +
                std::chrono::seconds(m_config->bossDialogueScanSeconds);
        }
        if (Clock::now() >= m_nextHistoryPrune)
        {
            PruneHistory();
            m_nextHistoryPrune = Clock::now() + std::chrono::minutes(1);
        }
        if ((m_historyDatabaseAvailable || m_snapshotDatabaseAvailable ||
             (m_config->sentimentEnabled && m_sentimentDatabaseAvailable) ||
             (m_config->memoryEnabled && m_memoryDatabaseAvailable)) &&
            Clock::now() >= m_nextDatabaseCleanup)
        {
            CleanupDatabase();
            m_nextDatabaseCleanup = Clock::now() + std::chrono::hours(1);
        }
        if (!m_paused && m_config->randomChatterEnabled && Clock::now() >= m_nextAmbient)
        {
            RunAmbient();
            ScheduleNextAmbient();
        }
        if (!m_paused && m_config->generalChatterEnabled && Clock::now() >= m_nextGeneralTrigger)
        {
            RunGeneralChatter();
            ScheduleNextGeneralTrigger();
        }
    }

    void Manager::ScheduleNextAmbient()
    {
        uint32_t seconds = m_config ? RandomUInt(m_config->randomMinimumIntervalSeconds,
                                                  m_config->randomMaximumIntervalSeconds) : 120;
        m_nextAmbient = Clock::now() + std::chrono::seconds(seconds);
    }

    void Manager::ScheduleNextGeneralTrigger()
    {
        uint32_t const seconds = (m_config && m_config->generalTriggerIntervalSeconds > 0)
            ? m_config->generalTriggerIntervalSeconds : 30;
        m_nextGeneralTrigger = Clock::now() + std::chrono::seconds(seconds);
    }

    void Manager::PruneGeneralChatter()
    {
        if (!m_config)
            return;
        uint32_t const nowSec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now().time_since_epoch()).count());
        m_generalSpeakerTracker.Prune(nowSec, m_config->generalBotSpeakerCooldownSeconds);
        m_gossipTargetTracker.Prune(nowSec, m_config->generalGossipTargetCooldownSeconds);
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
                    if (!queue.empty())
                        return true;
                return false;
            });
            if (m_stopping)
                return false;

            for (int priority = 3; priority >= 0; --priority)
            {
                auto& queue = m_queues[static_cast<size_t>(priority)];
                while (!queue.empty())
                {
                    request = std::move(queue.front());
                    queue.pop_front();
                    if (request.kind == RequestKind::PersonalityGeneration)
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
                if (request.kind == RequestKind::PersonalityGeneration)
                {
                    ChatCompletion completion;
                    completion.request = std::move(request);
                    completion.error = "personality generation expired in the request queue";
                    std::lock_guard<std::mutex> lock(m_completionMutex);
                    m_completions.push_back(std::move(completion));
                    continue;
                }
                ++m_dropped;
                continue;
            }

            ++m_inFlight;
            ChatCompletion completion;
            uint32_t retries = 0;
            uint32_t httpAttempts = 0;
            bool thinkingFallbackTried = false;
            do
            {
                completion = Provider::Execute(*m_config, request);
                httpAttempts += completion.httpAttemptCount;

                // V0.7: an official provider may reject the thinking control
                // itself. Mark that provider/model unsupported for this process
                // and retry the same request once without any reasoning field.
                if (!completion.success && !thinkingFallbackTried &&
                    !request.suppressReasoning &&
                    Reasoning::DetectProvider(m_config->endpoint) != Reasoning::Provider::None &&
                    Reasoning::IsParameterReasoningRejection(completion.httpStatus,
                        completion.rawResponse))
                {
                    Reasoning::MarkUnsupported(m_config->endpoint, m_config->model);
                    request.suppressReasoning = true;
                    thinkingFallbackTried = true;
                    ++m_thinkingFallbacks;
                    if (m_config->debug)
                        sLog.outDebug("[AzerothVoices][THINKING] Provider rejected the reasoning control; "
                            "retrying request %llu without it and disabling it for %s.",
                            static_cast<unsigned long long>(request.id),
                            SanitizeLogText(m_config->model).c_str());
                    continue;
                }

                bool retryable = !completion.success &&
                    (completion.httpStatus == 0 || completion.httpStatus == 429 || completion.httpStatus >= 500);
                if (!retryable || retries >= m_config->retryMaximum || m_stopping)
                    break;
                ++retries;
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    m_config->retryBackoffMilliseconds * retries));
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
            bool const proximityTrigger = trigger == "proximity" ||
                trigger == "proximity-followup";
            bool const bossTrigger = trigger.compare(0, 5, "boss-") == 0;
            bool const sayChatReaction = scope == ChatScope::Say &&
                (trigger == "direct-chat" || trigger == "overheard-chat" ||
                 trigger.compare(0, 13, "targeted-npc-") == 0);
            bool const legalNpcTrigger = ambient || trigger.compare(0, 6, "event:") == 0 ||
                sayChatReaction || proximityTrigger || bossTrigger;
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

            if (bossTrigger)
            {
                BossClassification const classification = ClassifyCreatureBoss(creature, *m_config);
                if (!classification.boss || !creature->IsAlive() || creature->IsInCombat() ||
                    NpcPlayableFactionReaction(creature) >= REP_NEUTRAL)
                {
                    RecordPreflightRejection(PreflightReason::InvalidActor);
                    return false;
                }
            }
            else
            {
                ProximitySpeakerDecision const decision = proximityTrigger
                    ? EvaluateProximityCreature(creature, *m_config)
                    : ProximitySpeakerDecision { true, ProximityRejection::None };
                NpcEligibilityResult const eligibility = proximityTrigger
                    ? (decision.eligible ? NpcEligibilityResult::Eligible : NpcEligibilityResult::Filtered)
                    : EvaluateNpcSpeaker(creature, m_config->sayDistance, *m_config);
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
                        case NpcEligibilityResult::Boss:
                            RecordPreflightRejection(PreflightReason::NpcBoss);
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
            // Group chatter is exempt: in-combat callouts are its purpose.
            if (m_config->disableRepliesInCombat && bot->IsInCombat() &&
                trigger.compare(0, 6, "group:") != 0 && trigger.compare(0, 5, "raid:") != 0)
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
                                uint32_t conversationDepth, ProximityScene const* scene,
                                BossPresence const* boss, bool bossDirected,
                                GroupConversation const* groupScene,
                                ActorSnapshot const* targetedNpc)
    {
        if (!PreflightDialogue(actor, speaker, scope, channelName, trigger, priority, ambient))
            return false;

        ChatRequest request = BuildRequest(actor, speaker, scope, channelName, trigger,
            message, priority, ambient, allowFollowup, scene, boss, bossDirected, groupScene, targetedNpc);
        request.conversationDepth = conversationDepth;
        if (Enqueue(std::move(request)))
            return true;
        RecordPreflightRejection(PreflightReason::Unavailable);
        return false;
    }

    bool Manager::QueueGuildPlayerReply(ActorSnapshot const& actor, SpeakerSnapshot const& speaker,
                                        std::string const& message, bool addressByName,
                                        std::string const& callbackTopic, bool followupQuestion,
                                        uint32_t initialDelaySeconds)
    {
        if (!PreflightDialogue(actor, speaker, ChatScope::Guild, "", "guild:player-reply", RequestPriority::Group, false))
            return false;

        ChatRequest request = BuildRequest(actor, speaker, ChatScope::Guild, "", "guild:player-reply",
            message, RequestPriority::Group, false, false);
        request.guildPlayerReply = true;
        request.guildAddressByName = addressByName;
        request.guildPlayerName = speaker.name;
        request.guildCallbackTopic = callbackTopic;
        request.guildFollowupQuestion = followupQuestion;
        request.guildInitialDelaySeconds = initialDelaySeconds;

        if (addressByName && !speaker.name.empty())
            request.systemPrompt += "\nAddress " + speaker.name + " by name in your reply naturally.";
        if (!callbackTopic.empty())
            request.systemPrompt += "\nEarlier, " + speaker.name + " mentioned: \"" + callbackTopic + "\". If relevant, naturally reference this.";
        if (followupQuestion)
            request.systemPrompt += "\nInclude a relevant question in your reply to keep the conversation flowing.";

        if (Enqueue(std::move(request)))
            return true;
        RecordPreflightRejection(PreflightReason::Unavailable);
        return false;
    }

    bool Manager::Enqueue(ChatRequest request)
    {
        if (!m_started || m_stopping || m_paused || !m_config)
            return false;

        auto const now = Clock::now();
        bool const personalityRequest = request.kind == RequestKind::PersonalityGeneration;
        uint32_t cooldownSeconds = request.ambient
            ? m_config->ambientActorCooldownSeconds : m_config->actorCooldownSeconds;
        auto cooldown = m_actorCooldowns.find(request.actor.guid);
        if (!personalityRequest && request.priority != RequestPriority::Direct &&
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

            if (!personalityRequest)
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
                        auto oldLatest = m_latestRequestByActor.find(dropped.actor.guid);
                        if (oldLatest != m_latestRequestByActor.end() && oldLatest->second == dropped.id)
                            m_latestRequestByActor.erase(oldLatest);
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
            request.expires = now + std::chrono::seconds(m_config->requestTtlSeconds);
            if (!personalityRequest)
                m_latestRequestByActor[request.actor.guid] = request.id;
            size_t const priorityIndex = static_cast<size_t>(request.priority);
            queueMissingPersonality = !personalityRequest && request.personalityGenerationNeeded;
            if (queueMissingPersonality)
                personalityActor = request.actor;
            m_queues[priorityIndex].push_back(std::move(request));
            m_requestBudget.push_back(now);
            if (!personalityRequest)
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
                                      RequestPriority priority, bool ambient, bool allowFollowup,
                                      ProximityScene const* scene, BossPresence const* boss,
                                      bool bossDirected, GroupConversation const* groupScene,
                                      ActorSnapshot const* targetedNpc)
    {
        ChatRequest request;
        request.id = m_nextRequestId++;
        request.priority = priority;
        request.actor = actor;
        if (targetedNpc)
        {
            request.targetedNpcObserver = true;
            request.targetedNpc = *targetedNpc;
        }
        bool const eventTrigger = trigger.compare(0, 6, "event:") == 0;
        bool const usePersonality = m_config->personalityEnabled &&
            (!ambient || m_config->personalityUseInRandom) &&
            (!eventTrigger || m_config->personalityUseInEvents);
        if (!usePersonality)
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
        request.pacingKey = NormalizePacingKey(scope, channelName, actor.mapId,
            actor.instanceId, actor.zoneId, actor.instance);
        if (scene)
        {
            request.proximityScene = true;
            request.proximityConversation = scene->conversation;
            request.sceneId = scene->id;
            request.sceneLineIndex = scene->deliveredLines;
            if (actor.kind == ActorKind::Creature && actor.instance)
                request.bossLoreContext = BuildInstanceLoreContext(actor.mapId, actor.map,
                    actor.area, m_instanceLore);
        }
        if (boss)
        {
            request.bossLine = true;
            request.bossAutomatic = !bossDirected;
            request.bossDirected = bossDirected;
            request.bossKey = boss->key;
            request.bossName = boss->name;
            request.bossSubName = boss->subName;
            request.bossLoreContext = boss->loreContext;
        }
        bool const groupTrigger = trigger.compare(0, 6, "group:") == 0;
        bool const raidTrigger = trigger.compare(0, 5, "raid:") == 0;
        request.groupChatter = groupTrigger || raidTrigger;
        if (groupScene)
        {
            request.groupConversation = groupScene->conversation;
            request.groupConversationId = groupScene->id;
            request.groupId = groupScene->groupId;
        }
        else if (request.groupChatter || scope == ChatScope::Party || scope == ChatScope::Raid)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(actor.guid));
            if (bot && bot->GetGroup())
            {
                request.groupId = bot->GetGroup()->GetId();
                if (bot->GetGroup()->isRaidGroup())
                    request.groupSubgroup = bot->GetSubGroup();
            }
            else if (speaker.groupId)
                request.groupId = speaker.groupId;
        }

        if (actor.kind == ActorKind::PlayerBot && usePersonality)
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

        if (m_config->sentimentEnabled && actor.kind == ActorKind::PlayerBot &&
            trigger != "gm-test")
        {
            Player* sentimentActor = ObjectAccessor::FindPlayer(ObjectGuid(actor.guid));
            Player* target = ObjectAccessor::FindPlayer(ObjectGuid(speaker.guid));
            bool const directConversation = !ambient && !eventTrigger && !speaker.isBot &&
                (trigger != "targeted-npc-observer") &&
                IsDirectSentimentConversation(sentimentActor, target, scope, message);
            bool const readOnlyContext = (ambient && m_config->sentimentUseInRandom) ||
                (eventTrigger && m_config->sentimentUseInEvents) ||
                (trigger == "targeted-npc-observer");
            if (IsOnlineRealPlayer(target) && (directConversation || readOnlyContext))
            {
                SentimentKey const key { actor.guid, target->GetObjectGuid().GetRawValue() };
                SentimentRecord sentiment;
                LoadSentiment(key, sentiment);
                request.sentiment = sentiment;
                request.sentimentTargetName = target->GetName();
                request.sentimentTracked = true;
                request.sentimentDeltaLimit = directConversation
                    ? m_config->sentimentConversationMaximumDelta : 0;
                request.sentimentBlock = BuildSentimentPromptBlock(
                    request.sentimentTargetName, sentiment.score, request.sentimentDeltaLimit);
            }
        }

        if (actor.kind == ActorKind::PlayerBot && trigger != "gm-test")
        {
            request.memoryBlock = BuildMemoryContext(request);
            if (!request.memoryBlock.empty())
                request.memoryTargetName = request.sentimentTargetName;
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
        if (actor.kind == ActorKind::Creature && (scene || boss))
        {
            request.systemPrompt += "\nObserved creature: " + actor.name +
                ", creature type " + std::to_string(actor.creatureType) +
                ", rank " + std::to_string(actor.creatureRank) +
                ", role " + (actor.role.empty() ? std::string("ordinary") : actor.role) +
                ", disposition " + (actor.disposition.empty() ? std::string("neutral") : actor.disposition) +
                ", map " + (actor.map.empty() ? std::string("unknown") : actor.map) +
                ", zone " + (actor.zone.empty() ? std::string("unknown") : actor.zone) +
                ", area " + (actor.area.empty() ? std::string("unknown") : actor.area) + '.';
        }
        if (scene)
        {
            request.systemPrompt += "\nThis is ordinary nearby NPC chatter witnessed by a passing adventurer, "
                "not a conversation with that player. Never address the player, never ask them for anything, and "
                "never mention that another actor is being generated. ";
            request.systemPrompt += scene->conversation
                ? "Say one short line that a different nearby speaker could plausibly answer."
                : "Say one short, self-contained line about the situation, your work, or what is around you.";
            if (!request.bossLoreContext.empty())
                request.systemPrompt += ' ' + request.bossLoreContext;
            request.systemPrompt += " Return one line of at most 180 characters with no speaker name prefix.";
        }
        if (boss)
        {
            request.systemPrompt += "\nYou are " + boss->name +
                (boss->subName.empty() ? std::string() : ", " + boss->subName) + '.';
            request.systemPrompt += " This is pre-aggro dialogue: the adventurers can see you and you can see them, "
                "but combat has not started and you are not yet aware of them as a threat target. Speak one "
                "original line of your own words. Never narrate or promise attacks, spells, movement, or any "
                "change in threat, aggro, facing, or position. ";
            if (!boss->loreContext.empty())
                request.systemPrompt += boss->loreContext + ' ';
            if (request.bossDirected)
                request.systemPrompt += "The player just spoke to you; answer their actual meaning in character. ";
            request.systemPrompt += "Keep the line between " + std::to_string(m_config->bossDialogueMinimumWords) +
                " and " + std::to_string(m_config->bossDialogueMaximumWords) + " words and at most " +
                std::to_string(m_config->bossDialogueMaximumCharacters) + " characters.";
            if (!boss->recentLines.empty())
            {
                request.systemPrompt += " Do not repeat or paraphrase these lines you already delivered:";
                for (std::string const& recent : boss->recentLines)
                    request.systemPrompt += " \"" + recent + "\"";
                request.systemPrompt += '.';
            }
        }
        request.systemPrompt += "\nThe reply will be sent through " +
            (channelName.empty() ? ScopeName(scope) : channelName) +
            ". Keep it concise and return dialogue only. Treat the current message and current live environment "
            "as authoritative. Older conversation, snapshot history, and retrieved lore are optional context: "
            "use only details directly relevant to the current reply, do not assume old state is still true, and "
            "do not invent facts to reconcile stale or conflicting context.";

        if (trigger == "boss-directed")
            request.userPrompt = "Respond in character to this player line: " + message;
        else if (trigger == "boss-automatic")
            request.userPrompt = "Speak one short pre-aggro line to the adventurers you can see approaching.";
        else if (trigger == "proximity-followup")
            request.userPrompt = "Reply naturally to what the other nearby speaker just said: " + message;
        else if (trigger == "generated-followup")
            request.userPrompt = "Reply naturally to this line: " + message;
        else if (trigger == "proximity")
            request.userPrompt = "Say one short line now. Situation, place, or topic: " + message;
        else if (ambient)
            request.userPrompt = "Create one natural line now. Situation or topic: " + message;
        else if (trigger.compare(0, 6, "event:") == 0)
            request.userPrompt = "React naturally to this in-game event: " + message;
        else if (trigger == "targeted-npc-observer")
        {
            TargetedNpcObserverPromptInput promptInput;
            promptInput.playerName = speaker.name;
            promptInput.npcName = request.targetedNpc.name;
            promptInput.npcRole = request.targetedNpc.role;
            promptInput.playerMessage = message;
            promptInput.zoneOrArea = actor.area.empty() ? actor.zone : actor.area;
            promptInput.botName = actor.name;

            TargetedNpcObserverPrompt observerPrompt = BuildTargetedNpcObserverPrompt(promptInput);
            request.systemPrompt += "\n" + observerPrompt.systemPromptExtension;
            request.userPrompt = observerPrompt.userPrompt;
        }
        else if (trigger == "guild:player-reply")
            request.userPrompt = "Respond naturally in guild chat to what your guildmate said: " + message;
        else if (trigger == "guild:login-greeting")
            request.userPrompt = "Respond naturally in guild chat to greet your guildmate: " + message;
        else
            request.userPrompt = m_config->prompt;
        if (!m_config->postPrompt.empty() && trigger != "targeted-npc-observer")
            request.userPrompt += "\n" + m_config->postPrompt;

        request.systemPrompt = Expand(request.systemPrompt, request);
        request.userPrompt = Expand(request.userPrompt, request);
        if (request.guildPlayerReply)
        {
            std::string const pName = !request.guildPlayerName.empty() ? request.guildPlayerName : speaker.name;
            if (request.guildAddressByName && !pName.empty())
                request.systemPrompt += "\nAddress " + pName + " by name in your reply naturally.";
            if (!request.guildCallbackTopic.empty())
                request.systemPrompt += "\nEarlier, " + pName + " mentioned: \"" + request.guildCallbackTopic + "\". If relevant, naturally reference this.";
            if (request.guildFollowupQuestion)
                request.systemPrompt += "\nInclude a relevant question in your reply to keep the conversation flowing.";
        }
        if (actor.kind == ActorKind::PlayerBot && !request.personalityBlock.empty() && !personalityPlaceholder)
            request.systemPrompt += "\n" + request.personalityBlock;
        if (!request.sentimentBlock.empty())
            request.systemPrompt += "\n" + request.sentimentBlock;
        if (!request.memoryBlock.empty())
            request.systemPrompt += "\n" + request.memoryBlock;
        if (usePersonality && actor.kind == ActorKind::PlayerBot &&
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

        size_t const fixedContext = request.personalityBlock.size() + request.sentimentBlock.size() +
            request.memoryBlock.size();
        size_t remaining = m_config->contextLength > fixedContext
            ? m_config->contextLength - fixedContext : 0;
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
        float const nearbyDistance = scope == ChatScope::Yell
            ? m_config->yellDistance : m_config->sayDistance;
        WorldObject const* npcDispositionTarget = dispositionTarget && dispositionTarget->IsInWorld() &&
            dispositionTarget->GetMapId() == speaker->GetMapId()
                ? dispositionTarget : static_cast<WorldObject const*>(speaker);

        auto chanceFor = [&](std::string const& actorName, ObjectGuid actorGuid, bool npc) -> std::pair<uint32_t, int>
        {
            bool const direct = scope == ChatScope::Whisper || (!selected.IsEmpty() && selected == actorGuid);
            bool const mentioned = IsExplicitPlayerBotNameMention(message, actorName);
            if (ambient)
                return { 100, 10 };

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

        std::set<uint64_t> explicitlyMentionedPlayerBots;
        if (!ambient && scope != ChatScope::Whisper &&
            m_config->exclusiveNameMentionResponder)
        {
            for (Player* bot : onlinePlayers)
            {
                if (bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                    !Script_IsAIControlled(bot) ||
                    bot->GetObjectGuid().GetRawValue() == excludedActor)
                    continue;
                if (IsExplicitPlayerBotNameMention(message, bot->GetName()))
                    explicitlyMentionedPlayerBots.insert(bot->GetObjectGuid().GetRawValue());
            }
        }
        bool const exclusivePlayerBotMention = !explicitlyMentionedPlayerBots.empty();

        for (Player* bot : onlinePlayers)
        {
            if (bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                !Script_IsAIControlled(bot) || bot->GetObjectGuid().GetRawValue() == excludedActor)
                continue;
            if (exclusivePlayerBotMention &&
                !explicitlyMentionedPlayerBots.count(bot->GetObjectGuid().GetRawValue()))
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
                    eligible = bot->GetMapId() == speaker->GetMapId() &&
                        speaker->IsWithinDist(bot, nearbyDistance, false);
                    break;
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
            result.push_back(std::move(candidate));
        }

        if (!exclusivePlayerBotMention && allowNpcs && m_config->npcReplies &&
            scope == ChatScope::Say)
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
        if (m_config && IsBlacklisted(*m_config, message))
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

    void Manager::ProcessChat(Player* speaker, ChatScope scope, std::string const& message,
                              std::string const& targetName, std::string const& channelName)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !speaker ||
            message.empty() || !IsScopeEnabled(*m_config, scope) ||
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

        // V0.8 guild reply gating: a short per-speaker debounce, plus suppression
        // of the bot that spoke most recently in this guild.
        bool const guildScope = scope == ChatScope::Guild || scope == ChatScope::Officer;
        uint64_t const speakerGuid = speaker->GetObjectGuid().GetRawValue();
        if (guildScope && !Script_IsAIControlled(speaker) && m_config->guildChatterEnabled)
        {
            CancelPendingGuildGreeting(speakerGuid);

            if (m_config->guildPlayerRepliesEnabled)
            {
                if (m_config->guildPlayerRepliesDebounceSeconds)
                {
                    auto debounce = m_guildReplyDebounce.find(speakerGuid);
                    if (debounce != m_guildReplyDebounce.end() && debounce->second > now)
                        return;
                }

                uint32_t const guildId = speaker->GetGuildId();
                if (guildId)
                    m_guildPlayerConversationUntil[guildId] = now + std::chrono::seconds(m_config->guildPlayerRepliesIdleSuppressionSeconds);

                GuildSessionKey sessionKey{ speakerGuid, guildId };
                std::string callbackTopic;
                if (m_config->guildPlayerRepliesCallbackChance && Roll(m_config->guildPlayerRepliesCallbackChance))
                {
                    auto hist = m_guildSessionHistory.find(sessionKey);
                    if (hist != m_guildSessionHistory.end())
                        callbackTopic = hist->second.FindRelevantCallback(message);
                }

                std::vector<GuildReplyCandidate> candidates;
                uint32_t namedCount = 0;
                {
                    HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                    for (auto const& entry : sObjectAccessor.GetPlayers())
                    {
                        Player* candidate = entry.second;
                        if (!candidate || !candidate->IsInWorld() || !candidate->IsAlive() ||
                            candidate == speaker || candidate->GetGuildId() != guildId ||
                            !Script_IsAIControlled(candidate))
                            continue;

                        bool const named = IsExplicitPlayerBotNameMention(message, candidate->GetName());
                        if (named)
                            ++namedCount;

                        auto const& recent = m_guildRecentSpeakers[guildId];
                        uint64_t const candGuid = candidate->GetObjectGuid().GetRawValue();
                        bool const spokeRecently = std::find(recent.begin(), recent.end(), candGuid) != recent.end();
                        uint32_t const weight = CalculateGuildBotWeight(named, spokeRecently, m_config->guildPlayerRepliesRecentSpeakerPenalty);

                        GuildReplyCandidate c;
                        c.guid = candGuid;
                        c.name = candidate->GetName();
                        c.explicitlyNamed = named;
                        c.spokeRecently = spokeRecently;
                        c.weight = weight;
                        candidates.push_back(std::move(c));

                        if (candidates.size() >= m_config->guildPlayerRepliesMaxCandidates)
                            break;
                    }
                }

                if (candidates.empty())
                    return;

                candidates = SelectWeightedGuildCandidates(std::move(candidates), m_config->guildPlayerRepliesMaxCandidates);

                GuildReplyMode const mode = DecideGuildReplyMode(
                    static_cast<uint32_t>(candidates.size()),
                    m_config->guildConversationChance > 0,
                    m_config->guildPlayerRepliesConversationChance,
                    m_config->guildPlayerRepliesMultiReplyChance,
                    m_config->guildPlayerRepliesMultiAddressedBonus,
                    namedCount >= 2,
                    RandomUInt(1, 100),
                    RandomUInt(1, 100));

                if (mode == GuildReplyMode::Conversation)
                {
                    GroupConversation scene;
                    scene.id = m_nextGroupConversationId++;
                    scene.groupId = guildId;
                    scene.mapId = speaker->GetMapId();
                    scene.instanceId = CurrentInstanceId(speaker);
                    scene.anchorPlayerGuid = speakerGuid;
                    scene.maximumLines = std::max<uint32_t>(1, m_config->guildMaximumLines);
                    scene.conversation = true;
                    scene.guild = true;
                    scene.nextTurn = now;
                    scene.expires = now + std::chrono::seconds(
                        m_config->groupConversationReplyWindowSeconds +
                        static_cast<int64_t>(m_config->groupConversationTurnGapSeconds) * scene.maximumLines);
                    uint32_t const participants = std::min<uint32_t>(
                        std::max<uint32_t>(2, m_config->guildMaximumParticipants),
                        static_cast<uint32_t>(candidates.size()));
                    for (uint32_t i = 0; i < participants; ++i)
                        scene.speakers.push_back(candidates[i].guid);
                    m_groupConversations[std::to_string(scene.id)] = scene;

                    Player* opener = ObjectAccessor::FindPlayer(ObjectGuid(candidates.front().guid));
                    if (opener)
                    {
                        ActorSnapshot actor = SnapshotBot(opener);
                        actor.anchorPlayerGuid = speakerGuid;
                        GroupConversation const* scenePtr = &m_groupConversations[std::to_string(scene.id)];
                        if (!QueueDialogue(actor, speakerSnapshot, scope, channelName,
                                "guild:player-reply", message, RequestPriority::Group, false, false, 0,
                                nullptr, nullptr, false, scenePtr))
                        {
                            m_groupConversations.erase(std::to_string(scene.id));
                        }
                        else
                        {
                            auto& recent = m_guildRecentSpeakers[guildId];
                            recent.push_back(candidates.front().guid);
                            if (recent.size() > 10)
                                recent.pop_front();
                            m_guildLastSpeaker[guildId] = std::make_pair(candidates.front().guid, now);
                        }
                    }
                }
                else
                {
                    uint32_t const maxResponders = (mode == GuildReplyMode::MultiReply)
                        ? std::min<uint32_t>(m_config->guildPlayerRepliesMaxResponders, static_cast<uint32_t>(candidates.size()))
                        : 1;

                    for (uint32_t i = 0; i < maxResponders; ++i)
                    {
                        uint64_t const botGuid = candidates[i].guid;
                        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(botGuid));
                        if (!bot)
                            continue;

                        uint32_t delay = (m_config->guildPlayerRepliesFirstDelayMaxSeconds >= m_config->guildPlayerRepliesFirstDelayMinSeconds)
                            ? RandomUInt(m_config->guildPlayerRepliesFirstDelayMinSeconds, m_config->guildPlayerRepliesFirstDelayMaxSeconds)
                            : m_config->guildPlayerRepliesFirstDelayMinSeconds;
                        if (i > 0)
                            delay += i * 2;

                        bool const addressByName = m_config->guildPlayerRepliesPlayerNameChance &&
                            Roll(m_config->guildPlayerRepliesPlayerNameChance);
                        bool const followupQuestion = m_config->guildPlayerRepliesFollowupQuestionChance &&
                            Roll(m_config->guildPlayerRepliesFollowupQuestionChance);

                        ActorSnapshot actor = SnapshotBot(bot);
                        actor.anchorPlayerGuid = speakerGuid;
                        if (QueueGuildPlayerReply(actor, speakerSnapshot, message, addressByName,
                                callbackTopic, followupQuestion, delay))
                        {
                            auto& recent = m_guildRecentSpeakers[guildId];
                            recent.push_back(botGuid);
                            if (recent.size() > 10)
                                recent.pop_front();
                            m_guildLastSpeaker[guildId] = std::make_pair(botGuid, now);
                        }
                    }
                }

                m_guildSessionHistory[sessionKey].AddTurn(message, "", static_cast<uint32_t>(time(nullptr)));
                if (m_config->guildPlayerRepliesDebounceSeconds)
                    m_guildReplyDebounce[speakerGuid] = now + std::chrono::seconds(m_config->guildPlayerRepliesDebounceSeconds);
                if (m_config->speakerCooldownSeconds)
                    m_speakerCooldowns[speakerGuid] = now + std::chrono::seconds(m_config->speakerCooldownSeconds);
                return;
            }
            else if (m_config->guildReplyDebounceSeconds)
            {
                auto debounce = m_guildReplyDebounce.find(speakerGuid);
                if (debounce != m_guildReplyDebounce.end() && debounce->second > now)
                    return;
            }
        }

        bool const speakerIsBot = Script_IsAIControlled(speaker);
        bool const allowNpcReply = scope == ChatScope::Say;
        bool const allowAiFollowup = speakerIsBot && scope != ChatScope::Whisper;
        bool npcHandled = false;
        bool selectedIsNpc = false;
        bool explicitBotMention = false;

        if (scope == ChatScope::Say && !speakerIsBot)
        {
            ObjectGuid const selected = speaker->GetSelectionGuid();
            Creature* selectedNpc = nullptr;
            if (selected.IsCreature())
            {
                selectedNpc = ObjectAccessor::GetCreature(*speaker, selected);
                if (selectedNpc && selectedNpc->IsInWorld() && selectedNpc->IsAlive())
                    selectedIsNpc = true;
            }

            if (selectedIsNpc)
            {
                {
                    HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                    for (auto const& entry : sObjectAccessor.GetPlayers())
                    {
                        Player* bot = entry.second;
                        if (!bot || bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                            !Script_IsAIControlled(bot))
                            continue;
                        if (bot->GetMapId() == speaker->GetMapId() &&
                            bot->IsWithinDist(speaker, m_config->sayDistance, false) &&
                            IsExplicitPlayerBotNameMention(message, bot->GetName()))
                        {
                            explicitBotMention = true;
                            break;
                        }
                    }
                }

                if (explicitBotMention)
                {
                    npcHandled = ProcessPlayerSay(speaker, message, targetName, channelName);
                }
                else
                {
                    npcHandled = ProcessPlayerSay(speaker, message, targetName, channelName);
                    MaybeQueueTargetedNpcObserverComment(speaker, selectedNpc, message, channelName);
                    return;
                }
            }
            else
            {
                // Real-player /say has an ordered NPC priority chain that owns the
                // NPC side of the reply; PlayerBots stay eligible through the
                // generic candidate path below.
                npcHandled = ProcessPlayerSay(speaker, message, targetName, channelName);
            }
        }
        std::vector<Candidate> candidates = CollectCandidates(
            speaker, scope, targetName, message, false, allowNpcReply && !npcHandled);
        if (selectedIsNpc && explicitBotMention)
        {
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [&message](Candidate const& candidate) {
                    return !IsExplicitPlayerBotNameMention(message, candidate.actor.name);
                }), candidates.end());
        }
        if (guildScope && m_config->guildChatterEnabled)
        {
            auto last = m_guildLastSpeaker.find(speaker->GetGuildId());
            if (last != m_guildLastSpeaker.end() &&
                now - last->second.second <
                    std::chrono::seconds(m_config->guildRecentSpeakerSuppressionSeconds))
            {
                uint64_t const suppressed = last->second.first;
                candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                    [suppressed](Candidate const& candidate) {
                        return candidate.actor.guid == suppressed;
                    }), candidates.end());
            }
        }
        if (candidates.empty())
            return;
        if (scope == ChatScope::Whisper)
            RecordSurroundingChat(scope, channelName, candidates.front().actor, speakerSnapshot, message);

        bool accepted = false;
        uint64_t acceptedActorGuid = 0;
        uint32_t acceptedResponders = 0;
        size_t candidateIndex = 0;
        uint32_t const maximum = scope == ChatScope::Whisper
            ? 1 : m_config->maxResponders.maximum;
        while (candidateIndex < candidates.size() && acceptedResponders < maximum)
        {
            if (scope != ChatScope::Whisper && m_config->maxResponders.falloffEnabled &&
                acceptedResponders > 0)
            {
                uint64_t const reduction =
                    static_cast<uint64_t>(m_config->maxResponders.chanceDelta) *
                    (acceptedResponders - 1);
                uint32_t const nextChance = reduction >= m_config->maxResponders.secondChance
                    ? 0
                    : m_config->maxResponders.secondChance - static_cast<uint32_t>(reduction);
                if (!Roll(nextChance))
                    break;
            }

            bool responderAccepted = false;
            while (candidateIndex < candidates.size())
            {
                Candidate const& candidate = candidates[candidateIndex++];
                bool const direct = scope == ChatScope::Whisper || candidate.score >= 80;
                RequestPriority const priority = direct ? RequestPriority::Direct :
                    ((scope == ChatScope::Party || scope == ChatScope::Raid ||
                      scope == ChatScope::Guild || scope == ChatScope::Officer)
                        ? RequestPriority::Group : RequestPriority::Nearby);
                std::string trigger = direct ? "direct-chat" : "overheard-chat";
                if (!QueueDialogue(candidate.actor, speakerSnapshot, scope, channelName,
                    trigger, message, priority, false, allowAiFollowup))
                    continue;

                accepted = true;
                acceptedActorGuid = candidate.actor.guid;
                responderAccepted = true;
                ++acceptedResponders;
                break;
            }

            if (!responderAccepted)
                break;
        }
        if (accepted && scope != ChatScope::Whisper && m_config->speakerCooldownSeconds)
            m_speakerCooldowns[speaker->GetObjectGuid().GetRawValue()] =
                now + std::chrono::seconds(m_config->speakerCooldownSeconds);
        if (accepted && guildScope && m_config->guildChatterEnabled)
        {
            if (acceptedActorGuid && speaker->GetGuildId())
                m_guildLastSpeaker[speaker->GetGuildId()] =
                    std::make_pair(acceptedActorGuid, now);
            if (m_config->guildReplyDebounceSeconds && !Script_IsAIControlled(speaker))
                m_guildReplyDebounce[speakerGuid] = now +
                    std::chrono::seconds(m_config->guildReplyDebounceSeconds);
        }
    }

    void Manager::HandleEvent(Player* subject, std::string const& eventName,
                              std::string const& detail, uint32_t guildId,
                              uint32_t creatureEntry, uint32_t creatureRank,
                              uint32_t itemQuality)
    {
        if (!m_started || !subject)
            return;
        InboundSignal signal;
        signal.kind = InboundSignal::Kind::Event;
        signal.playerGuid = subject->GetObjectGuid().GetRawValue();
        signal.eventName = eventName;
        signal.message = detail;
        signal.guildId = guildId;
        signal.creatureEntry = creatureEntry;
        signal.creatureRank = creatureRank;
        signal.itemQuality = itemQuality;
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
                ProcessEvent(player, signal.eventName, signal.message, signal.guildId,
                    signal.creatureEntry, signal.creatureRank, signal.itemQuality);
        }
    }

    void Manager::ProcessEvent(Player* subject, std::string const& eventName,
                               std::string const& detail, uint32_t guildId,
                               uint32_t creatureEntry, uint32_t creatureRank,
                               uint32_t itemQuality)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused ||
            !subject || !subject->IsInWorld())
            return;

        std::string event = Lower(eventName);

        if (event == "player_logout")
        {
            uint64_t const logoutGuid = subject->GetObjectGuid().GetRawValue();
            m_lastPlayerMap.erase(logoutGuid);
            m_guildReplyDebounce.erase(logoutGuid);
            return;
        }

        // Deterministic memories are recorded before any responder path, and the
        // group/raid subsystem claims its own situations so one event cannot
        // produce two unrelated lines.
        HandleEventMemories(subject, event, detail, creatureEntry, creatureRank);
        if (TryClaimGroupTrigger(subject, event, detail, creatureEntry, creatureRank, itemQuality))
            return;
        if (event == "guild_login" && HandleGuildLoginGreeting(subject))
            return;
        if (event == "guild_leave")
            CancelPendingGuildGreeting(subject->GetObjectGuid().GetRawValue());

        if (!m_config->eventChatterEnabled)
            return;
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

        // Ordinary Say chatter is owned by the proximity subsystem. A legacy
        // configuration that still lists `say` is routed through that gate so
        // the same NPC cannot speak twice for one Random roll.
        if (scope == ChatScope::Say)
            return QueueProximityScene(anchor, true);

        std::vector<Candidate> candidates = CollectCandidates(anchor, scope, "", topic, true,
            true);
        if (candidates.empty() && scope != ChatScope::Say)
        {
            scope = ChatScope::Say;
            candidates = CollectCandidates(anchor, scope, "", topic, true, true);
        }
        if (candidates.empty())
            return false;

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
        if ((scope == ChatScope::World || scope == ChatScope::Channel) &&
            !ReserveGeneralPacing(scope, channel, SnapshotBot(anchor),
                std::max<size_t>(1, m_config->maximumReplyLines)))
            return false;
        return QueueDialogue(candidates.front().actor, SnapshotSpeaker(anchor), scope, channel,
            "ambient", topic, RequestPriority::Ambient, true, true);
    }

    void Manager::RunAmbient()
    {
        if (ForceAmbient(nullptr) && m_config->debug)
            sLog.outDebug("[AzerothVoices] Queued ambient chatter.");
    }

    void Manager::RunGeneralChatter()
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !m_config->generalChatterEnabled)
            return;

        std::set<uint32_t> realPlayerZones;
        std::unordered_map<uint32_t, std::vector<Player*>> botsByZone;
        std::unordered_map<uint32_t, Player*> realPlayerAnchorByZone;

        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (!player || !player->IsInWorld() || !player->IsAlive())
                    continue;

                uint32_t const zoneId = player->GetZoneId();
                if (!zoneId)
                    continue;

                if (Script_IsAIControlled(player))
                {
                    botsByZone[zoneId].push_back(player);
                }
                else
                {
                    realPlayerZones.insert(zoneId);
                    if (realPlayerAnchorByZone.find(zoneId) == realPlayerAnchorByZone.end())
                        realPlayerAnchorByZone[zoneId] = player;
                }
            }
        }

        if (realPlayerZones.empty())
            return;

        uint32_t const nowSec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now().time_since_epoch()).count());

        for (uint32_t zoneId : realPlayerZones)
        {
            bool const isCity = IsVanillaCapitalCityZone(zoneId);
            uint32_t const effectiveChance = CalculateGeneralTriggerChance(
                m_config->generalTriggerChance, m_config->generalCityMultiplier, isCity);

            if (!Roll(effectiveChance))
                continue;

            auto botIt = botsByZone.find(zoneId);
            if (botIt == botsByZone.end() || botIt->second.empty())
                continue;

            std::vector<Player*> eligibleBots;
            for (Player* bot : botIt->second)
            {
                uint64_t const guid = bot->GetObjectGuid().GetRawValue();
                if (!m_generalSpeakerTracker.IsOnCooldown(guid, nowSec, m_config->generalBotSpeakerCooldownSeconds))
                    eligibleBots.push_back(bot);
            }

            if (eligibleBots.empty())
                continue;

            std::shuffle(eligibleBots.begin(), eligibleBots.end(), RandomEngine());

            bool const isConversation = (eligibleBots.size() >= 2) && Roll(m_config->generalConversationChance);
            Player* speaker1 = eligibleBots[0];
            Player* speaker2 = isConversation ? eligibleBots[1] : nullptr;
            Player* anchor = realPlayerAnchorByZone[zoneId];
            if (!anchor)
                anchor = speaker1;

            std::string const channel = m_config->worldChannelName;
            ChatScope const scope = ChatScope::World;
            if (!ReserveGeneralPacing(scope, channel, SnapshotBot(speaker1), isConversation ? 2 : 1))
                continue;

            std::string area;
            std::string zone;
            std::string mapName;
            uint32_t mapId = 0;
            uint32_t areaId = 0;
            uint32_t dummyZoneId = 0;
            FillLocation(speaker1, area, zone, mapName, mapId, areaId, dummyZoneId);
            std::string const effectiveZone = zone.empty() ? (area.empty() ? mapName : area) : zone;

            uint32_t const roll100 = RandomUInt(1, 100);
            GeneralSubjectType subjectType = SelectGeneralSubjectType(
                m_config->generalNpcGossipChance, m_config->generalBotGossipChance, roll100);

            std::string topic;

            if (subjectType == GeneralSubjectType::NpcGossip)
            {
                std::string npcName;
                float const distance = 80.0f;
                MaNGOS::AllCreaturesInRange check(speaker1, distance);
                std::list<Creature*> creatures;
                MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
                Cell::VisitGridObjects(speaker1, searcher, distance);

                for (Creature* creature : creatures)
                {
                    if (!creature || !creature->IsAlive())
                        continue;
                    std::string const name = creature->GetName();
                    if (name.empty())
                        continue;
                    std::string gossipKey = "npc:" + std::to_string(zoneId) + ":" + name;
                    if (!m_gossipTargetTracker.IsOnCooldown(gossipKey, nowSec, m_config->generalGossipTargetCooldownSeconds))
                    {
                        npcName = name;
                        m_gossipTargetTracker.RecordTarget(gossipKey, nowSec);
                        break;
                    }
                }

                if (!npcName.empty())
                    topic = "Casually talk or gossip about " + npcName + " in " + effectiveZone + ".";
                else
                    subjectType = GeneralSubjectType::Plain;
            }

            if (subjectType == GeneralSubjectType::BotGossip)
            {
                std::string otherBotName;
                for (Player* bot : botIt->second)
                {
                    if (bot == speaker1 || bot == speaker2)
                        continue;
                    std::string gossipKey = "bot:" + std::to_string(zoneId) + ":" + bot->GetName();
                    if (!m_gossipTargetTracker.IsOnCooldown(gossipKey, nowSec, m_config->generalGossipTargetCooldownSeconds))
                    {
                        otherBotName = bot->GetName();
                        m_gossipTargetTracker.RecordTarget(gossipKey, nowSec);
                        break;
                    }
                }

                if (!otherBotName.empty())
                    topic = "Casually talk or gossip about another adventurer named " + otherBotName + " in " + effectiveZone + ".";
                else
                    subjectType = GeneralSubjectType::Plain;
            }

            if (subjectType == GeneralSubjectType::Plain || topic.empty())
            {
                if (!m_config->worldPrompts.empty())
                    topic = Pick(m_config->worldPrompts);
                else
                    topic = "Make a short public observation or comment about " + effectiveZone + " that could start a conversation.";
            }

            m_generalSpeakerTracker.RecordSpeech(speaker1->GetObjectGuid().GetRawValue(), nowSec);
            if (isConversation && speaker2)
                m_generalSpeakerTracker.RecordSpeech(speaker2->GetObjectGuid().GetRawValue(), nowSec);

            SpeakerSnapshot audienceSnapshot = SnapshotSpeaker(anchor);
            if (!isConversation || !speaker2)
            {
                QueueDialogue(SnapshotBot(speaker1), audienceSnapshot, scope, channel,
                    "ambient", topic, RequestPriority::Ambient, true, true);
            }
            else
            {
                QueueDialogue(SnapshotBot(speaker1), audienceSnapshot, scope, channel,
                    "ambient", topic, RequestPriority::Ambient, true, true);
                std::string replyInstruction = "Respond naturally to the previous comment about " + effectiveZone + ".";
                QueueDialogue(SnapshotBot(speaker2), SnapshotSpeaker(speaker1), scope, channel,
                    "conversation", replyInstruction, RequestPriority::Ambient, false, true);
            }

            if (m_config->debug)
            {
                sLog.outDebug("[AzerothVoices] Queued General channel %s in %s (speaker: %s%s).",
                    isConversation ? "conversation" : "statement",
                    effectiveZone.c_str(),
                    speaker1->GetName(),
                    speaker2 ? (", reply: " + std::string(speaker2->GetName())).c_str() : "");
            }
        }
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
            personality.traits.size() != PersonalityTraitCount ||
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

    bool Manager::QueuePersonalityGeneration(ActorSnapshot const& actor, bool forced,
                                             PersonalityGenerationMode mode,
                                             BotPersonality const& fixed)
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
        request.personalityMode = mode;
        request.personalityFixed = fixed;
        request.addonRequest = mode != PersonalityGenerationMode::Full;
        request.systemPrompt = BuildPersonalityGenerationSystemPrompt(*m_config, mode);
        request.userPrompt = BuildPersonalityGenerationUserPrompt(*m_config, actor, mode, fixed);
        request.maxTokensOverride = PersonalityGenerationTokenBudget(*m_config, mode);
        uint64_t const requestId = request.id;
        if (!Enqueue(std::move(request)))
        {
            RecordPersonalityGenerationStatus(actor, "rejected", requestId,
                "The personality request could not be queued; check pause state, queue capacity, and the global request limit.");
            return false;
        }
        m_pendingPersonalityRequests[actor.guid] = requestId;
        RecordPersonalityGenerationStatus(actor, "pending", requestId,
            mode == PersonalityGenerationMode::ReplaceTraits
                ? "Trait replacement is running; tone and background are being regenerated."
                : mode == PersonalityGenerationMode::BackgroundOnly
                    ? "Background regeneration is running."
                    : forced ? "GM-requested personality replacement is running."
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
                                      completion.request.personalityMode,
                                      completion.request.personalityFixed,
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

    void Manager::CacheSentiment(SentimentRecord sentiment)
    {
        if (!m_config || !sentiment.key.actorGuid || !sentiment.key.targetGuid)
            return;

        SentimentKey const key = sentiment.key;
        m_sentimentCacheOrder.erase(std::remove(m_sentimentCacheOrder.begin(),
            m_sentimentCacheOrder.end(), key), m_sentimentCacheOrder.end());
        m_sentimentCacheOrder.push_back(key);
        m_sentiments[key] = std::move(sentiment);
        while (m_sentiments.size() > m_config->sentimentCacheMaximumEntries &&
               !m_sentimentCacheOrder.empty())
        {
            SentimentKey const expired = m_sentimentCacheOrder.front();
            m_sentimentCacheOrder.pop_front();
            m_sentiments.erase(expired);
            m_databaseLoadedSentimentPairs.erase(expired);
        }
    }

    bool Manager::ApplySentimentDecay(SentimentRecord& sentiment)
    {
        if (!m_config || !sentiment.exists || !sentiment.score ||
            (!m_config->sentimentPositiveDecayPerDay &&
             !m_config->sentimentNegativeDecayPerDay))
            return false;

        uint64_t const now = UnixNow();
        uint64_t const graceEnd = sentiment.lastInteractionUnix +
            static_cast<uint64_t>(m_config->sentimentInactivityGraceDays) * SecondsPerDay;
        uint64_t const decayBase = std::max(sentiment.lastDecayUnix, graceEnd);
        if (!decayBase || now <= decayBase)
            return false;

        uint64_t const elapsedDays = (now - decayBase) / SecondsPerDay;
        if (!elapsedDays)
            return false;

        uint32_t const rate = sentiment.score > 0
            ? m_config->sentimentPositiveDecayPerDay
            : m_config->sentimentNegativeDecayPerDay;
        if (!rate)
            return false;

        int64_t const movement = static_cast<int64_t>(elapsedDays) * rate;
        int32_t const previous = sentiment.score;
        if (sentiment.score > 0)
            sentiment.score = static_cast<int32_t>(std::max<int64_t>(0,
                static_cast<int64_t>(sentiment.score) - movement));
        else
            sentiment.score = static_cast<int32_t>(std::min<int64_t>(0,
                static_cast<int64_t>(sentiment.score) + movement));
        sentiment.lastDecayUnix = decayBase + elapsedDays * SecondsPerDay;
        sentiment.updatedUnix = now;
        return sentiment.score != previous;
    }

    bool Manager::LoadSentiment(SentimentKey const& key, SentimentRecord& sentiment)
    {
        if (!key.actorGuid || !key.targetGuid)
            return false;

        auto dirty = m_pendingSentimentWrites.find(key);
        if (dirty != m_pendingSentimentWrites.end())
            sentiment = dirty->second;
        else
        {
            auto cached = m_sentiments.find(key);
            if (cached != m_sentiments.end())
                sentiment = cached->second;
            else
            {
                sentiment = SentimentRecord();
                sentiment.key = key;
                if (m_sentimentDatabaseAvailable &&
                    !m_databaseLoadedSentimentPairs.count(key))
                {
                    m_databaseLoadedSentimentPairs.insert(key);
                    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                        "SELECT `score`,UNIX_TIMESTAMP(`created_at`),"
                        "UNIX_TIMESTAMP(`updated_at`),UNIX_TIMESTAMP(`last_interaction_at`),"
                        "UNIX_TIMESTAMP(`last_decay_at`) FROM `azeroth_voices_sentiment` "
                        "WHERE `actor_guid`='%llu' AND `target_guid`='%llu' LIMIT 1",
                        static_cast<unsigned long long>(key.actorGuid),
                        static_cast<unsigned long long>(key.targetGuid)));
                    if (result)
                    {
                        Field* fields = result->Fetch();
                        sentiment.score = ClampSentimentScore(fields[0].GetInt32());
                        sentiment.createdUnix = fields[1].GetUInt64();
                        sentiment.updatedUnix = fields[2].GetUInt64();
                        sentiment.lastInteractionUnix = fields[3].GetUInt64();
                        sentiment.lastDecayUnix = fields[4].GetUInt64();
                        sentiment.exists = true;
                    }
                }
            }
        }

        if (ApplySentimentDecay(sentiment))
            QueueSentimentWrite(sentiment);
        CacheSentiment(sentiment);
        return sentiment.exists;
    }

    void Manager::QueueSentimentWrite(SentimentRecord const& sentiment)
    {
        if (!m_config || !m_sentimentDatabaseAvailable || !sentiment.exists)
            return;

        if (!m_pendingSentimentWrites.count(sentiment.key) &&
            m_pendingSentimentWrites.size() >= m_config->sentimentPendingWriteMaximum)
            FlushSentimentWrites(false, true);
        if (!m_sentimentDatabaseAvailable)
            return;
        m_pendingSentimentWrites[sentiment.key] = sentiment;
    }

    void Manager::ApplyDeliveredSentiment(ChatRequest const& request)
    {
        if (!m_config || !m_config->sentimentEnabled || !request.sentimentTracked ||
            !request.sentimentDeltaAvailable || request.actor.kind != ActorKind::PlayerBot)
            return;

        Player* actor = ObjectAccessor::FindPlayer(ObjectGuid(request.sentiment.key.actorGuid));
        Player* target = ObjectAccessor::FindPlayer(ObjectGuid(request.sentiment.key.targetGuid));
        if (!actor || !actor->IsInWorld() || !Script_IsAIControlled(actor) ||
            !IsOnlineRealPlayer(target) || actor->GetName() != request.actor.name ||
            target->GetName() != request.sentimentTargetName)
            return;

        SentimentRecord sentiment;
        LoadSentiment(request.sentiment.key, sentiment);
        uint64_t const now = UnixNow();
        sentiment.key = request.sentiment.key;
        sentiment.score = ClampSentimentScore(sentiment.score + request.sentimentDelta);
        sentiment.createdUnix = sentiment.createdUnix ? sentiment.createdUnix : now;
        sentiment.updatedUnix = now;
        sentiment.lastInteractionUnix = now;
        sentiment.lastDecayUnix = now;
        sentiment.exists = true;
        CacheSentiment(sentiment);
        QueueSentimentWrite(sentiment);
    }

    bool Manager::ResolveSentimentPair(std::string const& actorName,
                                       std::string const& targetName,
                                       SentimentKey& key,
                                       std::string& resolvedActorName,
                                       std::string& resolvedTargetName,
                                       std::string& message) const
    {
        if (actorName.empty() || targetName.empty())
        {
            message = "Exact online PlayerBot and real-player names are required.";
            return false;
        }

        Player* actor = ObjectAccessor::FindPlayerByName(actorName.c_str());
        if (!actor || !actor->IsInWorld() || !Script_IsAIControlled(actor) ||
            actor->GetName() != actorName)
        {
            message = "No online AI-controlled PlayerBot with that exact name was found.";
            return false;
        }
        Player* target = ObjectAccessor::FindPlayerByName(targetName.c_str());
        if (!IsOnlineRealPlayer(target) || target->GetName() != targetName)
        {
            message = "No online real player with that exact name was found; PlayerBots are not valid sentiment targets.";
            return false;
        }

        key = { actor->GetObjectGuid().GetRawValue(), target->GetObjectGuid().GetRawValue() };
        resolvedActorName = actor->GetName();
        resolvedTargetName = target->GetName();
        return true;
    }

    bool Manager::InspectSentiment(std::string const& actorName, std::string const& targetName,
                                   std::string& message)
    {
        SentimentKey key;
        std::string actor;
        std::string target;
        if (!ResolveSentimentPair(actorName, targetName, key, actor, target, message))
            return false;

        SentimentRecord sentiment;
        bool const exists = LoadSentiment(key, sentiment);
        std::ostringstream text;
        text << "Sentiment " << actor << " -> " << target
             << ": score=" << sentiment.score
             << ", tier=" << SentimentTierForScore(sentiment.score)
             << ", stored=" << (exists ? "yes" : "no")
             << ", actor_guid=" << key.actorGuid
             << ", target_guid=" << key.targetGuid;
        if (exists)
            text << ", last_interaction_unix=" << sentiment.lastInteractionUnix
                 << ", last_decay_unix=" << sentiment.lastDecayUnix;
        text << '.';
        message = text.str();
        return true;
    }

    bool Manager::SetSentiment(std::string const& actorName, std::string const& targetName,
                               int32_t score, std::string& message)
    {
        if (score < SentimentMinimumScore || score > SentimentMaximumScore)
        {
            message = "Sentiment score must be an exact integer from -100 through 100.";
            return false;
        }

        SentimentKey key;
        std::string actor;
        std::string target;
        if (!ResolveSentimentPair(actorName, targetName, key, actor, target, message))
            return false;

        SentimentRecord sentiment;
        LoadSentiment(key, sentiment);
        uint64_t const now = UnixNow();
        sentiment.key = key;
        sentiment.score = score;
        sentiment.createdUnix = sentiment.createdUnix ? sentiment.createdUnix : now;
        sentiment.updatedUnix = now;
        sentiment.lastInteractionUnix = now;
        sentiment.lastDecayUnix = now;
        sentiment.exists = true;
        CacheSentiment(sentiment);
        QueueSentimentWrite(sentiment);
        message = "Sentiment " + actor + " -> " + target + " set to " +
            std::to_string(score) + " (" + SentimentTierForScore(score) + ").";
        return true;
    }

    bool Manager::ResetSentiment(std::string const& actorName, std::string const& targetName,
                                 std::string& message)
    {
        SentimentKey key;
        std::string actor;
        std::string target;
        if (!ResolveSentimentPair(actorName, targetName, key, actor, target, message))
            return false;

        m_sentiments.erase(key);
        m_sentimentCacheOrder.erase(std::remove(m_sentimentCacheOrder.begin(),
            m_sentimentCacheOrder.end(), key), m_sentimentCacheOrder.end());
        m_pendingSentimentWrites.erase(key);
        m_databaseLoadedSentimentPairs.insert(key);
        SentimentRecord neutral;
        neutral.key = key;
        CacheSentiment(neutral);
        if (m_sentimentDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_sentiment` WHERE `actor_guid`='%llu' AND `target_guid`='%llu'",
                static_cast<unsigned long long>(key.actorGuid),
                static_cast<unsigned long long>(key.targetGuid));
        message = "Sentiment " + actor + " -> " + target +
            (m_sentimentDatabaseAvailable
                ? " reset to neutral; the persistent pair deletion was queued."
                : " reset to neutral in RAM; the SQL table is unavailable.");
        return true;
    }

    bool Manager::ResetAllSentiments(std::string& message)
    {
        m_sentiments.clear();
        m_sentimentCacheOrder.clear();
        m_databaseLoadedSentimentPairs.clear();
        m_pendingSentimentWrites.clear();
        if (m_sentimentDatabaseAvailable)
            CharacterDatabase.PExecute("DELETE FROM `azeroth_voices_sentiment`");
        message = m_sentimentDatabaseAvailable
            ? "All Azeroth Voices sentiment pairs were reset; the explicit persistent reset-all deletion was queued. Personalities, history, snapshots, characters, and PlayerBots data were unchanged."
            : "All cached sentiment pairs were reset, but the SQL table is unavailable so persistent reset-all could not be confirmed. Unrelated data was unchanged.";
        return true;
    }

    void Manager::FlushSentimentWrites(bool force, bool ignoreDeadline)
    {
        if (!m_config || !m_sentimentDatabaseAvailable || m_pendingSentimentWrites.empty())
            return;

        auto const now = Clock::now();
        if (!force && !ignoreDeadline &&
            m_pendingSentimentWrites.size() < m_config->sentimentDatabaseFlushBatchSize &&
            now < m_nextSentimentDatabaseFlush)
            return;

        size_t const batch = force ? m_pendingSentimentWrites.size() :
            std::min<size_t>(m_pendingSentimentWrites.size(),
                             m_config->sentimentDatabaseFlushBatchSize);
        if (!CharacterDatabase.BeginTransaction())
        {
            sLog.outError("[AzerothVoices] Could not begin the asynchronous sentiment transaction; continuing with bounded RAM sentiment.");
            m_sentimentDatabaseAvailable = false;
            m_pendingSentimentWrites.clear();
            return;
        }

        size_t written = 0;
        while (written < batch && !m_pendingSentimentWrites.empty())
        {
            auto found = m_pendingSentimentWrites.begin();
            SentimentRecord const sentiment = found->second;
            m_pendingSentimentWrites.erase(found);
            CharacterDatabase.PExecute(
                "INSERT INTO `azeroth_voices_sentiment` "
                "(`actor_guid`,`target_guid`,`score`,`last_interaction_at`,`last_decay_at`,`created_at`,`updated_at`) "
                "VALUES ('%llu','%llu','%d',FROM_UNIXTIME('%llu'),FROM_UNIXTIME('%llu'),"
                "FROM_UNIXTIME('%llu'),FROM_UNIXTIME('%llu')) "
                "ON DUPLICATE KEY UPDATE `score`=VALUES(`score`),"
                "`last_interaction_at`=VALUES(`last_interaction_at`),"
                "`last_decay_at`=VALUES(`last_decay_at`),`updated_at`=VALUES(`updated_at`)",
                static_cast<unsigned long long>(sentiment.key.actorGuid),
                static_cast<unsigned long long>(sentiment.key.targetGuid), sentiment.score,
                static_cast<unsigned long long>(sentiment.lastInteractionUnix),
                static_cast<unsigned long long>(sentiment.lastDecayUnix),
                static_cast<unsigned long long>(sentiment.createdUnix),
                static_cast<unsigned long long>(sentiment.updatedUnix));
            ++written;
        }
        CharacterDatabase.CommitTransaction();
        m_nextSentimentDatabaseFlush = now +
            std::chrono::seconds(m_config->sentimentDatabaseFlushSeconds);
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
            if (completion.request.kind == RequestKind::PersonalityGeneration)
            {
                HandlePersonalityCompletion(completion);
                continue;
            }
            bool current = false;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                auto latest = m_latestRequestByActor.find(completion.request.actor.guid);
                current = latest != m_latestRequestByActor.end() && latest->second == completion.request.id;
                if (current)
                    m_latestRequestByActor.erase(latest);
            }
            if (!current || now > completion.request.expires)
            {
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
                continue;
            }

            int32_t sentimentDelta = 0;
            bool const hasSentimentDelta = ExtractSentimentDelta(completion.responseText,
                completion.request.sentimentDeltaLimit, sentimentDelta);
            if (completion.request.sentimentTracked && completion.request.sentimentDeltaLimit)
            {
                completion.request.sentimentDeltaAvailable = hasSentimentDelta;
                completion.request.sentimentDelta = sentimentDelta;
            }
            std::vector<std::string> lines = Provider::SplitReply(*m_config, completion.responseText);
            if (completion.request.bossLine && !lines.empty())
            {
                // One yell per opportunity, and only if it satisfies the
                // configured word and character bounds.
                std::vector<std::string> validated;
                for (std::string const& candidate : lines)
                {
                    std::string cleaned;
                    if (!ValidateBossLine(candidate, m_config->bossDialogueMinimumWords,
                            m_config->bossDialogueMaximumWords,
                            m_config->bossDialogueMaximumCharacters, cleaned))
                    {
                        if (m_config->debug)
                            sLog.outDebug("[AzerothVoices] Boss line discarded by the word/character bounds.");
                        continue;
                    }
                    validated.push_back(std::move(cleaned));
                    break;
                }
                lines = std::move(validated);
            }
            if (lines.empty())
            {
                ++m_failed;
                continue;
            }

            RecordGeneratedMessage(completion, lines);

            uint64_t cumulativeDelay = m_config->typingSimulationEnabled
                ? m_config->typingBaseDelayMilliseconds : 0;
            if (completion.request.trigger == "targeted-npc-observer")
                cumulativeDelay += 2000;
            if (completion.request.guildInitialDelaySeconds > 0)
                cumulativeDelay += static_cast<uint64_t>(completion.request.guildInitialDelaySeconds) * 1000;
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
                // A proximity conversation keeps its configured turn gap: the
                // next line is never delivered before the scene's next turn.
                if (completion.request.proximityScene && completion.request.sceneId)
                {
                    auto scene = m_proximityScenes.find(
                        std::to_string(completion.request.sceneId));
                    if (scene != m_proximityScenes.end() && line.due < scene->second.nextTurn)
                        line.due = scene->second.nextTurn;
                }
                line.firstLine = i == 0;
                m_scheduled.push_back(std::move(line));
                if (m_config->typingSimulationEnabled)
                    cumulativeDelay += 500;
            }

            size_t const linesAdded = lines.size();
            if (m_config->partyGateEnabled && linesAdded > 0 &&
                (completion.request.scope == ChatScope::Party || completion.request.scope == ChatScope::Raid || completion.request.groupChatter) &&
                completion.request.groupId != 0)
            {
                PartyGatePolicy const policy = PartyGatePolicyForTrigger(completion.request.trigger);
                if (policy != PartyGatePolicy::Bypass)
                {
                    std::string const partyKey = PartyPacingKey(completion.request.groupId,
                        completion.request.groupSubgroup ? static_cast<int32_t>(completion.request.groupSubgroup) : -1);
                    uint32_t const gapSeconds = PartyGateGapSeconds(policy, *m_config);
                    size_t const firstIndex = m_scheduled.size() - linesAdded;
                    auto const requestedTime = m_scheduled[firstIndex].due;
                    auto const scheduledTime = CalculatePartyScheduledTime(
                        m_partyPacing, partyKey, policy, requestedTime, now, m_config->partyGateMaxFillerDelaySeconds);
                    auto const shift = scheduledTime - requestedTime;
                    if (shift != std::chrono::milliseconds(0))
                    {
                        for (size_t k = firstIndex; k < m_scheduled.size(); ++k)
                            m_scheduled[k].due += shift;
                    }

                    uint32_t const exchangeDuration = EstimateExchangeMilliseconds(
                        linesAdded,
                        m_config->typingSimulationEnabled,
                        m_config->typingBaseDelayMilliseconds,
                        m_config->typingDelayPerCharacterMilliseconds,
                        60,
                        500
                    );
                    m_partyPacing.Reserve(partyKey, scheduledTime, exchangeDuration, gapSeconds * 1000);
                    if (m_config->partyGateDebugLog)
                    {
                        sLog.outString("[AzerothVoices] PartyGate paced %s message for %s: policy=%s, shift=%lld ms, exchange=%u ms, gap=%u s",
                            completion.request.trigger.c_str(), partyKey.c_str(), PartyGatePolicyName(policy),
                            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(shift).count(),
                            exchangeDuration, gapSeconds);
                    }
                }
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
            SanitizeLogText(m_config->model).c_str(), completion.httpStatus,
            completion.httpAttemptCount, completion.elapsedMilliseconds,
            JoinReplyLines(lines).c_str());
    }

    void Manager::ReportTelemetry()
    {
        if (!m_config || !m_config->consoleApiCallStats)
            return;

        auto const now = Clock::now();
        if (m_telemetryWindowStarted == Clock::time_point())
            m_telemetryWindowStarted = now;
        auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_telemetryWindowStarted).count();
        if (elapsed < m_config->consoleApiCallStatsIntervalSeconds)
            return;

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
            "npc-boss", "unavailable", "cooldown", "rate-limit", "queue-full", "superseded"
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

        m_telemetryWindowStarted = now;
        m_telemetryApiCalls = 0;
        m_telemetrySuccessfulResults = 0;
        m_telemetryFailedResults = 0;
        m_telemetryGeneratedMessages = 0;
        m_preflightRejections.fill(0);
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
            if (delivered && it->firstLine)
            {
                ApplyDeliveredSentiment(it->request);
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
                if (it->request.bossLine)
                    NoteBossLineDelivered(it->request, it->text);
                if (it->request.proximityScene)
                    MaybeQueueProximityTurn(it->request, it->text);
                if (it->request.groupConversation)
                    MaybeQueueGroupTurn(it->request, it->text);
                else if (!it->request.groupChatter &&
                    (it->request.scope == ChatScope::Party || it->request.scope == ChatScope::Raid) &&
                    it->request.actor.kind == ActorKind::PlayerBot && !it->request.speaker.isBot)
                    MaybeQueueGroupPlayerFollowup(it->request, it->text);
                if (it->request.actor.kind == ActorKind::PlayerBot && it->request.speaker.guid &&
                    !it->request.speaker.isBot)
                {
                    Player* observer = ObjectAccessor::FindPlayer(ObjectGuid(it->request.speaker.guid));
                    if (IsOnlineRealPlayer(observer))
                        RecordAddonContact(observer->GetObjectGuid().GetRawValue(),
                            it->request.actor.guid, it->request.actor.name);
                }
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
            auto latest = m_latestRequestByActor.find(request.actor.guid);
            if (latest != m_latestRequestByActor.end() && latest->second != request.id)
                return false;
        }

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
                (m_config->disableRepliesInCombat && bot->IsInCombat() &&
                 !request.groupChatter))
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
        if (!creature || creature->GetName() != request.actor.name)
            return false;

        if (request.bossLine)
        {
            // Boss lines are revalidated immediately before delivery: the boss
            // must still be an unaggroed, living, hostile classified boss that
            // this player can see from a safe distance. Nothing here ever
            // touches threat, aggro, facing, or movement.
            if (!ClassifyCreatureBoss(creature, *m_config).boss || !creature->IsAlive() ||
                creature->IsInCombat() || NpcPlayableFactionReaction(creature) >= REP_NEUTRAL)
                return false;
            if (creature->GetMapId() != anchor->GetMapId() ||
                CurrentInstanceId(creature) != CurrentInstanceId(anchor))
                return false;
            if (!anchor->IsAlive() ||
                creature->IsWithinDist(anchor, creature->GetAttackDistance(anchor) +
                    m_config->bossDialogueAggroMargin, false))
                return false;
            if (!creature->IsWithinLOSInMap(anchor) ||
                !creature->IsVisibleForOrDetect(anchor, anchor, true))
                return false;
            creature->MonsterYell(line.text, LANG_UNIVERSAL, anchor);
            return true;
        }

        if (request.proximityScene)
        {
            // Every proximity speaker must still be visible, alive, out of
            // combat, in the same map and instance, within line of sight, and
            // audible to the observing real player.
            if (!anchor->IsAlive() || CurrentInstanceId(creature) != CurrentInstanceId(anchor) ||
                creature->IsInCombat() ||
                !creature->IsWithinDist(anchor, m_config->sayDistance, false) ||
                !creature->IsWithinLOSInMap(anchor) ||
                !creature->IsVisibleForOrDetect(anchor, anchor, true) ||
                !EvaluateProximityCreature(creature, *m_config).eligible)
                return false;
            creature->MonsterSay(line.text, LANG_UNIVERSAL, anchor);
            return true;
        }

        if (EvaluateNpcSpeaker(creature, m_config->sayDistance, *m_config) !=
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
        m_sentimentDatabaseAvailable = false;
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
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_sentiment'"));
            m_sentimentDatabaseAvailable = table != nullptr;
            if (m_config->sentimentEnabled && !m_sentimentDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL sentiment table is missing; using bounded non-persistent RAM sentiment. Install data/sql/character/20260902_01_azeroth_voices_sentiment.sql.");
            else if (m_config->sentimentEnabled)
                sLog.outString("[AzerothVoices][SENTIMENT][SQL] Persistent PlayerBot-to-player sentiment storage is available.");
        }
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_addon_contacts'"));
            m_addonDatabaseAvailable = table != nullptr;
            if (m_config->addonEnabled && !m_addonDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL addon contact table is missing; the Chatter roster stays empty until data/sql/character/20260912_01_azeroth_voices_addon_contacts.sql is installed. Dialogue is unaffected.");
            else if (m_config->addonEnabled)
                sLog.outString("[AzerothVoices][ADDON][SQL] Chatter Companion contact storage is available.");
        }
        {
            std::unique_ptr<QueryResult> table(CharacterDatabase.Query(
                "SHOW TABLES LIKE 'azeroth_voices_bot_memory'"));
            m_memoryDatabaseAvailable = table != nullptr;
            if (m_config->memoryEnabled && !m_memoryDatabaseAvailable)
                sLog.outError("[AzerothVoices] SQL memory table is missing; memories stay in bounded RAM. Install data/sql/character/20260912_02_azeroth_voices_memories.sql.");
            else if (m_config->memoryEnabled)
                sLog.outString("[AzerothVoices][MEMORY][SQL] Persistent bot memory storage is available.");
        }
        if (m_historyDatabaseAvailable || m_snapshotDatabaseAvailable ||
            (m_config->sentimentEnabled && m_sentimentDatabaseAvailable))
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
        if (!m_config || (!m_historyDatabaseAvailable && !m_snapshotDatabaseAvailable &&
                          (!m_config->sentimentEnabled || !m_sentimentDatabaseAvailable)))
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
        if (!m_config)
            return;
        if (m_historyDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_chat_history` WHERE `created_at` < DATE_SUB(NOW(), INTERVAL %u MINUTE)",
                m_config->historyDatabaseTtlMinutes);
        if (m_snapshotDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_environment_history` WHERE `created_at` < DATE_SUB(NOW(), INTERVAL %u MINUTE)",
                m_config->snapshotDatabaseTtlMinutes);
        if (m_config->sentimentEnabled && m_sentimentDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_sentiment` WHERE `score`=0 "
                "AND `updated_at` < DATE_SUB(NOW(), INTERVAL %u DAY)",
                SentimentNeutralRetentionDays);
        if (m_config->memoryEnabled && m_memoryDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_bot_memory` WHERE `updated_at` < "
                "DATE_SUB(NOW(), INTERVAL %u DAY)",
                m_config->memoryRetentionDays);
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

    bool Manager::ResolveAddonContact(uint64_t playerGuid, uint64_t botGuid, std::string& botName,
                                      std::string& message) const
    {
        botName.clear();
        if (!playerGuid || !botGuid)
        {
            message = "A player and bot GUID are required.";
            return false;
        }
        if (!m_addonDatabaseAvailable)
        {
            message = "Addon contact storage is unavailable on this realm.";
            return false;
        }

        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `bot_name` FROM `azeroth_voices_addon_contacts` "
            "WHERE `player_guid`='%llu' AND `bot_guid`='%llu' LIMIT 1",
            static_cast<unsigned long long>(playerGuid),
            static_cast<unsigned long long>(botGuid)));
        if (!result)
        {
            message = "That bot is not in your known roster. Use Refresh after meeting it in game.";
            return false;
        }
        botName = result->Fetch()[0].GetCppString();
        if (botName.empty())
            botName = "PlayerBot " + std::to_string(botGuid);
        return true;
    }

    void Manager::RecordAddonContact(uint64_t playerGuid, uint64_t botGuid, std::string const& botName)
    {
        if (!m_config || !m_config->addonEnabled || !m_addonDatabaseAvailable ||
            !playerGuid || !botGuid || botName.empty())
            return;
        std::string name = botName;
        CharacterDatabase.escape_string(name);
        CharacterDatabase.PExecute(
            "INSERT INTO `azeroth_voices_addon_contacts` (`player_guid`,`bot_guid`,`bot_name`) "
            "VALUES ('%llu','%llu','%s') ON DUPLICATE KEY UPDATE `bot_name`=VALUES(`bot_name`),"
            "`updated_at`=CURRENT_TIMESTAMP",
            static_cast<unsigned long long>(playerGuid),
            static_cast<unsigned long long>(botGuid), name.c_str());
    }

    void Manager::ForgetAddonContact(uint64_t playerGuid, uint64_t botGuid)
    {
        if (!playerGuid || !botGuid)
            return;

        std::string const prefix = "0:" + std::to_string(botGuid) + ':' +
            std::to_string(playerGuid) + ':';
        for (auto it = m_history.begin(); it != m_history.end(); )
            it = it->first.compare(0, prefix.size(), prefix) == 0
                ? m_history.erase(it) : std::next(it);
        for (auto it = m_databaseLoadedHistoryKeys.begin();
             it != m_databaseLoadedHistoryKeys.end(); )
            it = it->compare(0, prefix.size(), prefix) == 0
                ? m_databaseLoadedHistoryKeys.erase(it) : std::next(it);

        bool const sharedContext = m_config && m_config->globalContext;
        if (sharedContext)
        {
            std::string const sharedKey = "0:" + std::to_string(botGuid);
            m_history.erase(sharedKey);
            m_databaseLoadedHistoryKeys.erase(sharedKey);
        }

        if (m_addonDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_addon_contacts` "
                "WHERE `player_guid`='%llu' AND `bot_guid`='%llu'",
                static_cast<unsigned long long>(playerGuid),
                static_cast<unsigned long long>(botGuid));

        if (m_historyDatabaseAvailable)
        {
            std::string pattern = prefix;
            CharacterDatabase.escape_string(pattern);
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_chat_history` WHERE `history_key` LIKE '%s%%'",
                pattern.c_str());
            if (sharedContext)
            {
                std::string sharedKey = "0:" + std::to_string(botGuid);
                CharacterDatabase.escape_string(sharedKey);
                CharacterDatabase.PExecute(
                    "DELETE FROM `azeroth_voices_chat_history` WHERE `history_key`='%s'",
                    sharedKey.c_str());
            }
        }
    }

    bool Manager::LoadPersonalityForGuid(uint64_t botGuid, std::string const& botName,
                                         BotPersonality& personality, bool requireCurrent)
    {
        ActorSnapshot actor;
        actor.kind = ActorKind::PlayerBot;
        actor.guid = botGuid;
        actor.name = botName;
        return LoadPersonality(actor, personality, requireCurrent);
    }

    bool Manager::HandleAddonCommandLogical(Player* player, std::string const& arguments,
                                            std::string const& channel,
                                            std::vector<std::string>& logicalPayloads)
    {
        logicalPayloads.clear();
        if (!player || !player->IsInWorld() || !player->GetSession())
            return false;
        if (!m_config || !m_config->enabled || !m_config->addonEnabled)
        {
            logicalPayloads.push_back(Addon::LogicalError(channel, "The addon interface is disabled on this server."));
            return false;
        }

        Addon::Command command;
        std::string error;
        if (!Addon::ParseCommand(arguments, command, error))
        {
            logicalPayloads.push_back(Addon::LogicalError(channel, error));
            return false;
        }

        uint64_t const playerGuid = player->GetObjectGuid().GetRawValue();
        switch (command.kind)
        {
            case Addon::CommandKind::Roster:
            {
                // A missing contact table degrades to an empty roster; dialogue
                // and every other subsystem keep working.
                logicalPayloads.push_back(Addon::LogicalRosterBegin());
                if (m_addonDatabaseAvailable)
                {
                    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
                        "SELECT `bot_guid`,`bot_name` FROM `azeroth_voices_addon_contacts` "
                        "WHERE `player_guid`='%llu' ORDER BY `bot_name` LIMIT %u",
                        static_cast<unsigned long long>(playerGuid),
                        m_config->addonMaximumRosterEntries));
                    if (result)
                    {
                        do
                        {
                            Field* fields = result->Fetch();
                            logicalPayloads.push_back(Addon::LogicalRosterEntry(
                                fields[0].GetUInt64(), fields[1].GetCppString()));
                        } while (result->NextRow());
                    }
                }
                logicalPayloads.push_back(Addon::LogicalRosterEnd());
                return true;
            }

            case Addon::CommandKind::Get:
            {
                std::string botName;
                std::string message;
                if (!ResolveAddonContact(playerGuid, command.guid, botName, message))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel, message));
                    return false;
                }
                BotPersonality personality;
                if (!LoadPersonalityForGuid(command.guid, botName, personality, false))
                {
                    logicalPayloads.push_back(Addon::LogicalProfile(command.guid, botName, {}, ""));
                    logicalPayloads.push_back(Addon::LogicalBackstory(command.guid, ""));
                    return true;
                }
                logicalPayloads.push_back(Addon::LogicalProfile(command.guid, botName,
                    personality.traits, personality.tone));
                logicalPayloads.push_back(Addon::LogicalBackstory(command.guid, personality.background));
                return true;
            }

            case Addon::CommandKind::Set:
            {
                if (!m_config->personalityEnabled)
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel, "Bot personalities are disabled on this server."));
                    return false;
                }
                std::string botName;
                std::string message;
                if (!ResolveAddonContact(playerGuid, command.guid, botName, message))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel, message));
                    return false;
                }

                BotPersonality stored;
                bool const hadStored = LoadPersonalityForGuid(command.guid, botName, stored, false);
                bool unchanged = hadStored && stored.traits.size() == command.traits.size();
                for (size_t i = 0; unchanged && i < command.traits.size(); ++i)
                    unchanged = Lower(stored.traits[i]) == Lower(command.traits[i]);
                if (unchanged)
                {
                    logicalPayloads.push_back(Addon::LogicalUpdated(command.guid, botName, false));
                    logicalPayloads.push_back(Addon::LogicalProfile(command.guid, botName,
                        stored.traits, stored.tone));
                    logicalPayloads.push_back(Addon::LogicalBackstory(command.guid, stored.background));
                    return true;
                }

                BotPersonality replacement;
                replacement.characterGuid = command.guid;
                replacement.botName = botName;
                replacement.traits = command.traits;
                replacement.backgroundMode = m_config->personalityBackgroundMode;
                replacement.generationVersion = PersonalityGenerationVersion;
                replacement.createdUnix = hadStored && stored.createdUnix
                    ? stored.createdUnix : UnixNow();
                replacement.updatedUnix = UnixNow();
                CachePersonality(replacement);
                PersistPersonality(replacement);
                CancelPersonalityGeneration(command.guid);

                ActorSnapshot actor;
                actor.kind = ActorKind::PlayerBot;
                actor.guid = command.guid;
                actor.name = botName;
                if (!QueuePersonalityGeneration(actor, true,
                        PersonalityGenerationMode::ReplaceTraits, replacement))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel,
                        "The traits were saved, but tone and background regeneration could not be queued."));
                }
                logicalPayloads.push_back(Addon::LogicalUpdated(command.guid, botName, true));
                logicalPayloads.push_back(Addon::LogicalProfile(command.guid, botName, command.traits, ""));
                return true;
            }

            case Addon::CommandKind::RegenBackstory:
            {
                if (!m_config->personalityGenerateBackground)
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel,
                        "Background generation is disabled on this server."));
                    return false;
                }
                std::string botName;
                std::string message;
                if (!ResolveAddonContact(playerGuid, command.guid, botName, message))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel, message));
                    return false;
                }
                BotPersonality stored;
                if (!LoadPersonalityForGuid(command.guid, botName, stored, false) ||
                    stored.traits.size() != PersonalityTraitCount)
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel,
                        "No stored personality is available for this bot yet."));
                    return false;
                }

                stored.background.clear();
                stored.updatedUnix = UnixNow();
                CachePersonality(stored);
                PersistPersonality(stored);
                CancelPersonalityGeneration(command.guid);

                ActorSnapshot actor;
                actor.kind = ActorKind::PlayerBot;
                actor.guid = command.guid;
                actor.name = botName;
                if (!QueuePersonalityGeneration(actor, true,
                        PersonalityGenerationMode::BackgroundOnly, stored))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel,
                        "Background regeneration could not be queued; try again shortly."));
                    return false;
                }
                logicalPayloads.push_back(Addon::LogicalBackstoryRegen(command.guid, botName));
                return true;
            }

            case Addon::CommandKind::Forget:
            {
                std::string botName;
                std::string message;
                if (!ResolveAddonContact(playerGuid, command.guid, botName, message))
                {
                    logicalPayloads.push_back(Addon::LogicalError(channel, message));
                    return false;
                }
                // Forgetting removes only this player/bot contact and their
                // conversation history. The bot's global personality and
                // sentiment are preserved.
                ForgetAddonContact(playerGuid, command.guid);
                DeleteMemoryPair(MemoryKey { command.guid, playerGuid });
                logicalPayloads.push_back(Addon::LogicalForgotten(command.guid, botName));
                return true;
            }

            case Addon::CommandKind::Invalid:
            default:
                break;
        }
        logicalPayloads.push_back(Addon::LogicalError(channel, "Unsupported command."));
        return false;
    }

    bool Manager::HandleAddonCommand(Player* player, std::string const& arguments,
                                     std::vector<std::string>& responses)
    {
        responses.clear();
        std::vector<std::string> logicalPayloads;
        bool const success = HandleAddonCommandLogical(player, arguments, "llmc", logicalPayloads);
        for (std::string const& payload : logicalPayloads)
            responses.push_back(Addon::Response(payload));
        return success;
    }

    void Manager::HandleAvaddonSay(Player* player, std::string const& message)
    {
        if (!player || !player->IsInWorld() || !player->GetSession())
            return;

        uint64_t const playerGuid = player->GetObjectGuid().GetRawValue();
        uint32_t const nowMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        Addon::ReassembledRequest request;
        Addon::FrameStatus const status = m_addonReassembler.Feed(playerGuid, message, nowMs, request);
        if (status == Addon::FrameStatus::Complete)
        {
            HandleNativeAddonCommand(player, request.requestId, request.command);
        }
    }

    void Manager::HandleNativeAddonCommand(Player* player, std::string const& requestId,
                                          std::string const& command)
    {
        if (!player || !player->IsInWorld() || !player->GetSession())
            return;

        std::vector<std::string> logicalPayloads;
        HandleAddonCommandLogical(player, command, "avaddon", logicalPayloads);
        std::vector<std::string> frames = Addon::BuildNativeFrames(requestId, logicalPayloads);
        for (std::string const& frame : frames)
            player->SendAddonMessage(Addon::NativePrefix, frame);
    }

    void Manager::OnPlayerLogout(Player* player)
    {
        if (player)
        {
            uint64_t const guid = player->GetObjectGuid().GetRawValue();
            m_addonReassembler.ClearPlayer(guid);
            CancelPendingGuildGreeting(guid);
        }
    }

    uint32_t Manager::MemoryGenerationChance(MemoryType type) const
    {
        if (!m_config)
            return 0;
        switch (type)
        {
            case MemoryType::FirstMet: return m_config->memoryFirstMetChance;
            case MemoryType::PartyMember: return m_config->memoryPartyMemberChance;
            case MemoryType::QuestCompleted: return m_config->memoryQuestCompletedChance;
            case MemoryType::DungeonCompleted: return m_config->memoryDungeonCompletedChance;
            case MemoryType::LevelUp: return m_config->memoryLevelUpChance;
            case MemoryType::BossKill: return m_config->memoryBossKillChance;
            case MemoryType::PvpKill: return m_config->memoryPvpKillChance;
            case MemoryType::Wipe: return m_config->memoryWipeChance;
            case MemoryType::Count: break;
        }
        return 0;
    }

    bool Manager::LoadMemories(MemoryKey const& key)
    {
        if (m_memories.count(key))
            return true;
        if (m_databaseLoadedMemoryPairs.count(key))
            return false;
        m_databaseLoadedMemoryPairs.insert(key);
        while (m_databaseLoadedMemoryPairs.size() > m_config->memoryCacheMaximumPairs)
            m_databaseLoadedMemoryPairs.erase(m_databaseLoadedMemoryPairs.begin());
        if (!m_memoryDatabaseAvailable)
            return false;

        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT `id`,`memory_type`,`summary`,`importance`,UNIX_TIMESTAMP(`created_at`),"
            "UNIX_TIMESTAMP(`updated_at`) FROM `azeroth_voices_bot_memory` "
            "WHERE `bot_guid`='%llu' AND `player_guid`='%llu' "
            "ORDER BY `importance` DESC, `created_at` DESC LIMIT %u",
            static_cast<unsigned long long>(key.botGuid),
            static_cast<unsigned long long>(key.playerGuid),
            m_config->memoryMaximumPerPair));
        if (!result)
            return false;

        std::vector<MemoryRecord> records;
        do
        {
            Field* fields = result->Fetch();
            MemoryRecord record;
            record.id = fields[0].GetUInt64();
            record.botGuid = key.botGuid;
            record.playerGuid = key.playerGuid;
            MemoryType type = MemoryType::FirstMet;
            if (!ParseMemoryType(fields[1].GetCppString(), type))
                continue;
            record.type = type;
            record.summary = fields[2].GetCppString();
            record.importance = fields[3].GetUInt32();
            record.createdUnix = fields[4].GetUInt64();
            record.updatedUnix = fields[5].GetUInt64();
            if (!record.summary.empty())
                records.push_back(std::move(record));
        } while (result->NextRow());

        CacheMemories(key, std::move(records));
        return true;
    }

    void Manager::CacheMemories(MemoryKey const& key, std::vector<MemoryRecord> records)
    {
        if (!m_config || !key.botGuid || !key.playerGuid)
            return;
        m_memoryCacheOrder.erase(std::remove(m_memoryCacheOrder.begin(),
            m_memoryCacheOrder.end(), key), m_memoryCacheOrder.end());
        m_memoryCacheOrder.push_back(key);
        m_memories[key] = std::move(records);
        while (m_memories.size() > m_config->memoryCacheMaximumPairs && !m_memoryCacheOrder.empty())
        {
            MemoryKey const expired = m_memoryCacheOrder.front();
            m_memoryCacheOrder.pop_front();
            m_memories.erase(expired);
            m_databaseLoadedMemoryPairs.erase(expired);
        }
    }

    bool Manager::AppendMemory(MemoryKey const& key, MemoryRecord record)
    {
        if (!m_config || !m_config->memoryEnabled || !key.botGuid || !key.playerGuid ||
            record.summary.empty())
            return false;

        if (!m_memories.count(key))
            LoadMemories(key);
        std::vector<MemoryRecord>& records = m_memories[key];
        // Coalesce an identical memory instead of storing the same fact twice.
        for (MemoryRecord& existing : records)
        {
            if (existing.type == record.type && existing.summary == record.summary)
            {
                existing.updatedUnix = record.updatedUnix;
                return false;
            }
        }

        records.push_back(record);
        if (records.size() > m_config->memoryMaximumPerPair)
            PruneMemoryRecords(records, m_config->memoryMaximumPerPair);
        if (!m_memoryCacheOrder.empty() || m_memories.size() > 1)
        {
            m_memoryCacheOrder.erase(std::remove(m_memoryCacheOrder.begin(),
                m_memoryCacheOrder.end(), key), m_memoryCacheOrder.end());
            m_memoryCacheOrder.push_back(key);
        }
        QueueMemoryWrite(key, record);
        return true;
    }

    void Manager::RecordMemory(uint64_t botGuid, uint64_t playerGuid, MemoryType type,
                               MemoryFacts const& facts)
    {
        if (!m_config || !m_config->memoryEnabled || !botGuid || !playerGuid)
            return;
        if (!Roll(MemoryGenerationChance(type)))
            return;

        MemoryRecord record;
        record.botGuid = botGuid;
        record.playerGuid = playerGuid;
        record.type = type;
        if (!BuildMemorySummary(type, facts, record.summary))
            return;
        record.importance = MemoryTypeImportance(type);
        record.createdUnix = UnixNow();
        record.updatedUnix = record.createdUnix;
        AppendMemory(MemoryKey { botGuid, playerGuid }, std::move(record));
    }

    void Manager::QueueMemoryWrite(MemoryKey const& key, MemoryRecord const& record)
    {
        if (!m_memoryDatabaseAvailable)
            return;
        while (m_pendingMemoryWrites.size() >= static_cast<size_t>(m_config->memoryCacheMaximumPairs))
            m_pendingMemoryWrites.pop_front();
        PendingMemoryWrite write;
        write.key = key;
        write.record = record;
        write.createdUnix = record.createdUnix;
        m_pendingMemoryWrites.push_back(std::move(write));
    }

    void Manager::FlushMemoryWrites(bool force)
    {
        if (!m_config || !m_config->memoryEnabled || !m_memoryDatabaseAvailable)
            return;
        if (m_pendingMemoryWrites.empty())
            return;
        auto const now = Clock::now();
        if (!force && m_pendingMemoryWrites.size() < m_config->memoryDatabaseFlushBatchSize &&
            now < m_nextMemoryFlush)
            return;

        size_t const batch = force ? m_pendingMemoryWrites.size()
            : std::min<size_t>(m_pendingMemoryWrites.size(),
                m_config->memoryDatabaseFlushBatchSize);
        if (!CharacterDatabase.BeginTransaction())
        {
            sLog.outError("[AzerothVoices] Could not begin the memory transaction; keeping %u memories in RAM.",
                static_cast<unsigned>(m_pendingMemoryWrites.size()));
            m_memoryDatabaseAvailable = false;
            return;
        }

        std::set<MemoryKey> touched;
        size_t written = 0;
        while (written < batch && !m_pendingMemoryWrites.empty())
        {
            PendingMemoryWrite write = std::move(m_pendingMemoryWrites.front());
            m_pendingMemoryWrites.pop_front();
            std::string type = MemoryTypeName(write.record.type);
            std::string summary = HeadBounded(write.record.summary, 500);
            CharacterDatabase.escape_string(type);
            CharacterDatabase.escape_string(summary);
            CharacterDatabase.PExecute(
                "INSERT INTO `azeroth_voices_bot_memory` "
                "(`bot_guid`,`player_guid`,`memory_type`,`summary`,`importance`,`created_at`) "
                "VALUES ('%llu','%llu','%s','%s','%u',FROM_UNIXTIME('%llu'))",
                static_cast<unsigned long long>(write.key.botGuid),
                static_cast<unsigned long long>(write.key.playerGuid),
                type.c_str(), summary.c_str(), write.record.importance,
                static_cast<unsigned long long>(write.createdUnix));
            touched.insert(write.key);
            ++written;
        }

        for (MemoryKey const& key : touched)
        {
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_bot_memory` WHERE `bot_guid`='%llu' "
                "AND `player_guid`='%llu' AND `id` NOT IN "
                "(SELECT `id` FROM (SELECT `id` FROM `azeroth_voices_bot_memory` "
                "WHERE `bot_guid`='%llu' AND `player_guid`='%llu' "
                "ORDER BY `id` DESC LIMIT %u) AS `av_keep`)",
                static_cast<unsigned long long>(key.botGuid),
                static_cast<unsigned long long>(key.playerGuid),
                static_cast<unsigned long long>(key.botGuid),
                static_cast<unsigned long long>(key.playerGuid),
                m_config->memoryMaximumPerPair);
        }
        CharacterDatabase.CommitTransaction();
        m_nextMemoryFlush = now + std::chrono::seconds(m_config->memoryDatabaseFlushSeconds);
    }

    void Manager::DeleteMemoryPair(MemoryKey const& key)
    {
        m_memories.erase(key);
        m_memoryCacheOrder.erase(std::remove(m_memoryCacheOrder.begin(),
            m_memoryCacheOrder.end(), key), m_memoryCacheOrder.end());
        m_databaseLoadedMemoryPairs.erase(key);
        if (m_memoryDatabaseAvailable)
            CharacterDatabase.PExecute(
                "DELETE FROM `azeroth_voices_bot_memory` "
                "WHERE `bot_guid`='%llu' AND `player_guid`='%llu'",
                static_cast<unsigned long long>(key.botGuid),
                static_cast<unsigned long long>(key.playerGuid));
    }

    void Manager::DeleteAllMemories()
    {
        m_memories.clear();
        m_memoryCacheOrder.clear();
        m_databaseLoadedMemoryPairs.clear();
        m_pendingMemoryWrites.clear();
        if (m_memoryDatabaseAvailable)
            CharacterDatabase.PExecute("DELETE FROM `azeroth_voices_bot_memory`");
    }

    std::string Manager::BuildMemoryContext(ChatRequest const& request)
    {
        if (!m_config || !m_config->memoryEnabled || request.actor.kind != ActorKind::PlayerBot ||
            !request.actor.guid)
            return "";

        uint64_t playerGuid = 0;
        std::string playerName;
        if (!request.speaker.isBot && request.speaker.guid)
        {
            Player* speaker = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
            if (IsOnlineRealPlayer(speaker))
            {
                playerGuid = speaker->GetObjectGuid().GetRawValue();
                playerName = speaker->GetName();
            }
        }
        if (!playerGuid && request.actor.anchorPlayerGuid)
        {
            Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.actor.anchorPlayerGuid));
            if (IsOnlineRealPlayer(anchor))
            {
                playerGuid = anchor->GetObjectGuid().GetRawValue();
                playerName = anchor->GetName();
            }
        }
        if (!playerGuid)
            return "";

        // Ambient and group chatter only sometimes reach for the past; a direct
        // conversation always may.
        if ((request.groupChatter || request.ambient) && !Roll(m_config->memoryRecallChance))
            return "";

        MemoryKey const key { request.actor.guid, playerGuid };
        LoadMemories(key);
        auto found = m_memories.find(key);
        if (found == m_memories.end() || found->second.empty())
            return "";
        std::vector<MemoryRecord const*> const selected = SelectMemoriesForPrompt(
            found->second, m_config->memoryMaximumPromptItems,
            m_config->memoryMaximumPromptCharacters);
        if (selected.empty())
            return "";
        return BuildMemoryPromptBlock(playerName, selected,
            m_config->memoryMaximumPromptCharacters);
    }

    bool Manager::ResolveMemoryPair(std::string const& actorName, std::string const& targetName,
                                    MemoryKey& key, std::string& message) const
    {
        ActorSnapshot actor;
        if (!ResolvePersonalityActor(actorName, actor, message))
            return false;
        Player* target = ObjectAccessor::FindPlayerByName(targetName.c_str());
        if (!target || !IsOnlineRealPlayer(target))
        {
            message = "No online real player with that exact name was found.";
            return false;
        }
        key.botGuid = actor.guid;
        key.playerGuid = target->GetObjectGuid().GetRawValue();
        return true;
    }

    bool Manager::InspectMemories(std::string const& actorName, std::string const& targetName,
                                  std::string& message)
    {
        MemoryKey key;
        if (!ResolveMemoryPair(actorName, targetName, key, message))
            return false;
        LoadMemories(key);
        auto found = m_memories.find(key);
        size_t const count = found == m_memories.end() ? 0 : found->second.size();
        std::ostringstream text;
        text << "Memories for " << targetName << " held by " << actorName << ": " << count;
        if (count)
        {
            size_t shown = 0;
            for (MemoryRecord const& record : found->second)
            {
                if (shown++ >= 5)
                    break;
                text << "\n- [" << MemoryTypeName(record.type) << "] " << record.summary;
            }
        }
        text << (m_memoryDatabaseAvailable
            ? "\nStorage: SQL persistent."
            : "\nStorage: RAM only; the memory table is unavailable.");
        message = text.str();
        return true;
    }

    bool Manager::ForgetMemories(std::string const& actorName, std::string const& targetName,
                                 std::string& message)
    {
        MemoryKey key;
        if (!ResolveMemoryPair(actorName, targetName, key, message))
            return false;
        LoadMemories(key);
        auto found = m_memories.find(key);
        size_t const count = found == m_memories.end() ? 0 : found->second.size();
        DeleteMemoryPair(key);
        message = "Deleted " + std::to_string(count) + " memories between " + targetName +
            " and " + actorName + (m_memoryDatabaseAvailable
                ? "; the SQL rows were queued for deletion. "
                : "; the memory table is unavailable, so only RAM was cleared. ") +
            "Personality, sentiment, and conversation history were not changed.";
        return true;
    }

    bool Manager::ForgetAllMemories(std::string& message)
    {
        size_t const pairs = m_memories.size();
        DeleteAllMemories();
        message = "Cleared " + std::to_string(pairs) + " cached memory pair(s)" +
            (m_memoryDatabaseAvailable
                ? " and queued the whole memory table for deletion."
                : "; the memory table is unavailable, so persistent rows could not be confirmed.") +
            " Personality, sentiment, and conversation history were not changed.";
        return true;
    }

    std::vector<Player*> Manager::CollectGroupBots(Player* realPlayer, bool raid,
                                                   uint32_t& groupId) const
    {
        std::vector<Player*> bots;
        groupId = 0;
        if (!realPlayer)
            return bots;
        Group* group = realPlayer->GetGroup();
        if (!group || (raid && !group->IsRaidGroup()))
            return bots;
        groupId = group->GetId();
        uint8_t const subgroup = group->GetMemberGroup(realPlayer->GetObjectGuid());
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            if (!raid && slot.group != subgroup)
                continue;
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || !member->IsInWorld() || !member->IsAlive() ||
                !Script_IsAIControlled(member))
                continue;
            bots.push_back(member);
        }
        return bots;
    }

    namespace
    {
        // A real player who shares the subject's party subgroup (or raid) and can
        // act as the anchor and audience for generated group chatter.
        Player* FindGroupRealPlayer(Player* member, bool raid, uint32_t& groupId)
        {
            groupId = 0;
            if (!member)
                return nullptr;
            Group* group = member->GetGroup();
            if (!group || (raid && !group->IsRaidGroup()))
                return nullptr;
            groupId = group->GetId();
            uint8_t const subgroup = group->GetMemberGroup(member->GetObjectGuid());
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                if (!raid && slot.group != subgroup)
                    continue;
                Player* candidate = ObjectAccessor::FindPlayer(slot.guid);
                if (candidate && IsOnlineRealPlayer(candidate))
                    return candidate;
            }
            return nullptr;
        }
    }

    bool Manager::GroupTriggerReady(uint32_t groupId, GroupTrigger trigger, uint32_t cooldownSeconds)
    {
        if (!cooldownSeconds)
            return true;
        std::string const key = std::to_string(groupId) + ':' + GroupTriggerName(trigger);
        auto found = m_groupTriggerCooldowns.find(key);
        return found == m_groupTriggerCooldowns.end() || found->second <= Clock::now();
    }

    void Manager::NoteGroupTrigger(uint32_t groupId, GroupTrigger trigger, uint32_t cooldownSeconds)
    {
        if (!cooldownSeconds)
            return;
        std::string const key = std::to_string(groupId) + ':' + GroupTriggerName(trigger);
        m_groupTriggerCooldowns[key] = Clock::now() + std::chrono::seconds(cooldownSeconds);
    }

    bool Manager::QueueGroupChatter(GroupTrigger trigger, Player* anchor,
                                    std::string const& detail, bool conversation,
                                    uint64_t excludedSpeaker)
    {
        if (!m_started || m_stopping || m_paused || !m_config || !anchor || !anchor->IsInWorld())
            return false;
        Map const* map = anchor->FindMap();
        if (!map)
            return false;
        bool const raid = map->IsRaid();
        if (IsRaidOnlyTrigger(trigger) &&
            (!raid || !m_config->raidChatterEnabled))
            return false;
        if (!m_config->groupChatterEnabled)
            return false;
        ChatScope const scope = raid ? ChatScope::Raid : ChatScope::Party;
        if (!IsScopeEnabled(*m_config, scope))
            return false;

        uint32_t groupId = 0;
        std::vector<Player*> bots = CollectGroupBots(anchor, raid, groupId);
        if (bots.empty())
            return false;
        std::shuffle(bots.begin(), bots.end(), RandomEngine());

        Player* speaker = nullptr;
        for (Player* bot : bots)
        {
            if (bot->GetObjectGuid().GetRawValue() == excludedSpeaker)
                continue;
            speaker = bot;
            break;
        }
        if (!speaker)
            return false;
        // Keep the shuffled order but put the chosen speaker first so a
        // conversation can include the actor that opens it.
        if (bots.front() != speaker)
            std::swap(bots.front(), *std::find(bots.begin(), bots.end(), speaker));

        std::string const triggerName = std::string(raid ? "raid:" : "group:") +
            GroupTriggerName(trigger);
        if (m_config->partyGateEnabled && groupId != 0)
        {
            PartyGatePolicy const policy = PartyGatePolicyForTrigger(triggerName);
            if (policy == PartyGatePolicy::Filler)
            {
                std::string const partyKey = PartyPacingKey(groupId, -1);
                auto const now = Clock::now();
                uint32_t waitSeconds = 0;
                if (ShouldDeferPartyFiller(m_partyPacing, partyKey, policy, m_config->partyGatePreLLMDeferThresholdSeconds, now, &waitSeconds))
                {
                    if (m_config->partyGateDebugLog)
                    {
                        sLog.outString("[AzerothVoices] PartyGate deferring pre-LLM filler for group %u (waitTime=%u s > threshold=%u s)",
                            groupId, waitSeconds, m_config->partyGatePreLLMDeferThresholdSeconds);
                    }
                    return false;
                }
            }
        }

        GroupConversation scene;
        bool const wantsConversation = conversation && m_config->groupConversationChance &&
            Roll(m_config->groupConversationChance) &&
            GroupConversationBudget { m_config->groupConversationMaximumLines,
                m_config->groupConversationMaximumParticipants,
                m_config->groupConversationMaximumReplyTurns,
                m_config->groupConversationTurnGapSeconds,
                m_config->groupConversationReplyWindowSeconds }
                .CanStartConversation(static_cast<uint32_t>(bots.size()));
        if (wantsConversation)
        {
            auto const now = Clock::now();
            scene.id = m_nextGroupConversationId++;
            scene.groupId = groupId;
            scene.mapId = anchor->GetMapId();
            scene.instanceId = CurrentInstanceId(anchor);
            scene.anchorPlayerGuid = anchor->GetObjectGuid().GetRawValue();
            scene.maximumLines = std::max<uint32_t>(1, m_config->groupConversationMaximumLines);
            scene.conversation = true;
            scene.raid = raid;
            scene.nextTurn = now;
            scene.expires = now + std::chrono::seconds(
                m_config->groupConversationReplyWindowSeconds +
                static_cast<int64_t>(m_config->groupConversationTurnGapSeconds) * scene.maximumLines);
            uint32_t const participants = std::min<uint32_t>(
                std::max<uint32_t>(1, m_config->groupConversationMaximumParticipants),
                static_cast<uint32_t>(bots.size()));
            for (uint32_t i = 0; i < participants; ++i)
                scene.speakers.push_back(bots[i]->GetObjectGuid().GetRawValue());
            m_groupConversations[std::to_string(scene.id)] = scene;
        }

        ActorSnapshot actor = SnapshotBot(speaker);
        actor.anchorPlayerGuid = anchor->GetObjectGuid().GetRawValue();
        GroupConversation const* scenePtr = nullptr;
        if (wantsConversation)
            scenePtr = &m_groupConversations[std::to_string(scene.id)];
        if (!QueueDialogue(actor, SnapshotSpeaker(anchor), scope, "", triggerName, detail,
                RequestPriority::Group, false, false, 0, nullptr, nullptr, false, scenePtr))
        {
            if (scenePtr)
                m_groupConversations.erase(std::to_string(scene.id));
            return false;
        }
        NoteGroupTrigger(groupId, trigger, 0);
        return true;
    }

    void Manager::MaybeQueueGroupTurn(ChatRequest const& request, std::string const& reply)
    {
        if (!m_config || !request.groupConversationId)
            return;
        auto found = m_groupConversations.find(std::to_string(request.groupConversationId));
        if (found == m_groupConversations.end())
            return;
        GroupConversation& scene = found->second;
        if (scene.guild && !m_config->guildChatterEnabled)
            return;
        if (!scene.guild && !m_config->groupChatterEnabled)
            return;
        ++scene.deliveredLines;
        GroupConversationBudget const budget {
            scene.guild ? m_config->guildMaximumLines : m_config->groupConversationMaximumLines,
            scene.guild ? m_config->guildMaximumParticipants
                        : m_config->groupConversationMaximumParticipants,
            m_config->groupConversationMaximumReplyTurns,
            m_config->groupConversationTurnGapSeconds,
            m_config->groupConversationReplyWindowSeconds };
        if (!budget.CanContinue(scene.deliveredLines, scene.replyTurns))
            return;

        auto const now = Clock::now();
        if (now > scene.expires)
            return;

        if (scene.guild)
        {
            if (!FindOnlineRealGuildAudience(scene.groupId, ChatScope::Guild))
                return;
            std::vector<Player*> candidates;
            for (uint64_t guid : scene.speakers)
            {
                Player* member = ObjectAccessor::FindPlayer(ObjectGuid(guid));
                if (!member || !member->IsInWorld() || !member->IsAlive() ||
                    !Script_IsAIControlled(member) || member->GetGuildId() != scene.groupId ||
                    member->GetObjectGuid().GetRawValue() == request.actor.guid)
                    continue;
                candidates.push_back(member);
            }
            if (candidates.empty())
                return;
            Player* nextSpeaker = candidates[RandomUInt(0,
                static_cast<uint32_t>(candidates.size() - 1))];
            Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(scene.anchorPlayerGuid));
            if (!anchor)
                return;

            ActorSnapshot guildActor = SnapshotBot(nextSpeaker);
            guildActor.anchorPlayerGuid = scene.anchorPlayerGuid;
            SpeakerSnapshot guildPrevious;
            guildPrevious.guid = request.actor.guid;
            guildPrevious.name = request.actor.name;
            guildPrevious.race = request.actor.race;
            guildPrevious.className = request.actor.className;
            guildPrevious.gender = request.actor.gender;
            guildPrevious.faction = request.actor.faction;
            guildPrevious.guild = request.actor.guild;
            guildPrevious.groupStatus = request.actor.groupStatus;
            guildPrevious.level = request.actor.level;
            guildPrevious.isBot = true;
            scene.nextTurn = now + std::chrono::seconds(m_config->groupConversationTurnGapSeconds);
            std::string const guildTrigger = std::string("guild:") +
                GroupTriggerName(GroupTrigger::PlayerFollowup);
            if (QueueDialogue(guildActor, guildPrevious, ChatScope::Guild, "", guildTrigger,
                    reply, RequestPriority::Group, false, false, request.conversationDepth + 1,
                    nullptr, nullptr, false, &scene))
                ++scene.replyTurns;
            return;
        }

        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(scene.anchorPlayerGuid));
        if (!anchor || !anchor->IsInWorld() || anchor->GetMapId() != scene.mapId ||
            CurrentInstanceId(anchor) != scene.instanceId)
            return;

        std::vector<Player*> candidates;
        for (uint64_t guid : scene.speakers)
        {
            Player* member = ObjectAccessor::FindPlayer(ObjectGuid(guid));
            if (!member || !member->IsInWorld() || !member->IsAlive() ||
                !Script_IsAIControlled(member) ||
                member->GetObjectGuid().GetRawValue() == request.actor.guid)
                continue;
            if (!member->GetGroup() || member->GetGroup()->GetId() != request.groupId)
                continue;
            candidates.push_back(member);
        }
        if (candidates.empty())
            return;

        Player* next = candidates[RandomUInt(0, static_cast<uint32_t>(candidates.size() - 1))];
        ActorSnapshot actor = SnapshotBot(next);
        actor.anchorPlayerGuid = scene.anchorPlayerGuid;

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
        previous.isBot = true;

        scene.nextTurn = now + std::chrono::seconds(m_config->groupConversationTurnGapSeconds);
        std::string const triggerName = std::string(scene.raid ? "raid:" : "group:") +
            GroupTriggerName(GroupTrigger::PlayerFollowup);
        if (QueueDialogue(actor, previous, scene.raid ? ChatScope::Raid : ChatScope::Party, "",
                triggerName, reply, RequestPriority::Group, false, false,
                request.conversationDepth + 1, nullptr, nullptr, false, &scene))
            ++scene.replyTurns;
    }

    void Manager::MaybeQueueGroupPlayerFollowup(ChatRequest const& request, std::string const& reply)
    {
        if (!m_config || !m_config->groupChatterEnabled || !m_config->groupPlayerFollowupChance ||
            request.speaker.isBot || !request.speaker.guid)
            return;
        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(request.speaker.guid));
        if (!IsOnlineRealPlayer(anchor))
            return;
        Map const* map = anchor->FindMap();
        if (!map)
            return;
        bool const raid = map->IsRaid();
        if (raid && !m_config->raidChatterEnabled)
            return;

        uint32_t groupId = 0;
        std::vector<Player*> bots = CollectGroupBots(anchor, raid, groupId);
        bots.erase(std::remove_if(bots.begin(), bots.end(), [&](Player* bot) {
            return bot->GetObjectGuid().GetRawValue() == request.actor.guid;
        }), bots.end());
        if (bots.empty())
            return;
        if (!GroupTriggerReady(groupId, GroupTrigger::PlayerFollowup,
                m_config->groupPlayerFollowupCooldownSeconds) ||
            !Roll(m_config->groupPlayerFollowupChance))
            return;

        Player* speaker = bots[RandomUInt(0, static_cast<uint32_t>(bots.size() - 1))];
        ActorSnapshot actor = SnapshotBot(speaker);
        actor.anchorPlayerGuid = anchor->GetObjectGuid().GetRawValue();
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
        previous.isBot = true;
        std::string const triggerName = std::string(raid ? "raid:" : "group:") +
            GroupTriggerName(GroupTrigger::PlayerFollowup);
        if (QueueDialogue(actor, previous, raid ? ChatScope::Raid : ChatScope::Party, "",
                triggerName, reply, RequestPriority::Group, false, false,
                request.conversationDepth + 1))
            NoteGroupTrigger(groupId, GroupTrigger::PlayerFollowup,
                m_config->groupPlayerFollowupCooldownSeconds);
    }

    void Manager::ScanGroupBots(GroupRuntimeState& state, Player* anchor,
                                std::vector<Player*> const& bots, bool raid)
    {
        size_t alive = 0;
        bool anyInCombat = false;
        for (Player* bot : bots)
        {
            if (bot->IsAlive())
                ++alive;
            anyInCombat = anyInCombat || bot->IsInCombat();
        }
        bool const anchorAlive = anchor->IsAlive();
        size_t const members = bots.size() + 1;
        size_t const deadMembers = (bots.size() - alive) + (anchorAlive ? 0 : 1);
        anyInCombat = anyInCombat || anchor->IsInCombat();

        if (state.wipe.Update(members, deadMembers, anyInCombat))
        {
            if (GroupTriggerReady(state.groupId, GroupTrigger::Wipe,
                    m_config->groupWipeCooldownSeconds) &&
                Roll(m_config->groupWipeChance) &&
                QueueGroupChatter(GroupTrigger::Wipe, anchor, "the group just wiped", false))
                NoteGroupTrigger(state.groupId, GroupTrigger::Wipe,
                    m_config->groupWipeCooldownSeconds);
            std::string const instance = anchor->FindMap() ? anchor->FindMap()->GetMapName() : "";
            std::string wipeArea;
            std::string wipeZone;
            std::string wipeMap;
            uint32_t wipeMapId = 0;
            uint32_t wipeAreaId = 0;
            uint32_t wipeZoneId = 0;
            FillLocation(anchor, wipeArea, wipeZone, wipeMap, wipeMapId, wipeAreaId, wipeZoneId);
            for (Player* bot : bots)
            {
                MemoryFacts facts;
                facts.instance = instance;
                facts.zone = wipeZone.empty() ? wipeArea : wipeZone;
                RecordMemory(bot->GetObjectGuid().GetRawValue(),
                    anchor->GetObjectGuid().GetRawValue(), MemoryType::Wipe, facts);
            }
        }

        for (Player* bot : bots)
        {
            uint64_t const guid = bot->GetObjectGuid().GetRawValue();

            // Resurrect: a member that was dead in the previous scan is alive now.
            auto aliveIt = state.aliveLastScan.find(guid);
            bool const wasAlive = aliveIt == state.aliveLastScan.end() ? true : aliveIt->second;
            if (!wasAlive && bot->IsAlive() &&
                GroupTriggerReady(state.groupId, GroupTrigger::Resurrect,
                    m_config->groupResurrectCooldownSeconds) &&
                Roll(m_config->groupResurrectChance))
            {
                if (QueueGroupChatter(GroupTrigger::Resurrect, anchor,
                        bot->GetName() + std::string(" is back on their feet"), false, guid))
                    NoteGroupTrigger(state.groupId, GroupTrigger::Resurrect,
                        m_config->groupResurrectCooldownSeconds);
            }
            state.aliveLastScan[guid] = bot->IsAlive();

            // Quest log diff: accept / objective progress / completion.
            QuestLogSnapshot snapshot;
            snapshot.known = true;
            QuestStatusMap& quests = bot->GetQuestStatusMap();
            for (auto const& entry : quests)
            {
                if (entry.second.m_status == QUEST_STATUS_INCOMPLETE)
                    ++snapshot.entries;
                snapshot.hash = MixQuestLogHash(snapshot.hash, entry.first);
                snapshot.hash = MixQuestLogHash(snapshot.hash,
                    static_cast<uint64_t>(entry.second.m_status));
                for (uint32_t count : entry.second.m_itemcount)
                    snapshot.hash = MixQuestLogHash(snapshot.hash, count);
                for (uint32_t count : entry.second.m_creatureOrGOcount)
                    snapshot.hash = MixQuestLogHash(snapshot.hash, count);
            }
            QuestLogSnapshot& previous = state.questLogs[guid];
            QuestLogEvent const questEvent = DiffQuestLog(previous, snapshot);
            previous = snapshot;
            if (questEvent == QuestLogEvent::Accepted &&
                GroupTriggerReady(state.groupId, GroupTrigger::QuestAccept,
                    m_config->groupQuestCooldownSeconds) &&
                Roll(m_config->groupQuestAcceptChance))
            {
                if (QueueGroupChatter(GroupTrigger::QuestAccept, anchor,
                        bot->GetName() + std::string(" picked up a new quest"), false, guid))
                    NoteGroupTrigger(state.groupId, GroupTrigger::QuestAccept,
                        m_config->groupQuestCooldownSeconds);
            }
            else if (questEvent == QuestLogEvent::ObjectiveProgressed &&
                GroupTriggerReady(state.groupId, GroupTrigger::QuestObjective,
                    m_config->groupQuestObjectiveDebounceSeconds) &&
                Roll(m_config->groupQuestObjectiveChance))
            {
                if (QueueGroupChatter(GroupTrigger::QuestObjective, anchor,
                        "that quest objective just moved forward", false, guid))
                    NoteGroupTrigger(state.groupId, GroupTrigger::QuestObjective,
                        m_config->groupQuestObjectiveDebounceSeconds);
            }

            // Low health / low mana callouts with recovery hysteresis.
            uint32_t const maxHealth = bot->GetMaxHealth();
            uint32_t const maxMana = bot->GetMaxPower(POWER_MANA);
            if (maxHealth)
            {
                uint32_t const percent = bot->GetHealth() * 100 / maxHealth;
                uint32_t const threshold = m_config->groupLowHealthThresholdPercent;
                bool const fired = state.healthLatches[guid].Update(
                    percent <= threshold, percent >= std::min<uint32_t>(100, threshold + 15));
                if (fired && GroupTriggerReady(state.groupId, GroupTrigger::LowHealth,
                        m_config->groupLowHealthCooldownSeconds) &&
                    Roll(m_config->groupLowHealthChance) &&
                    QueueGroupChatter(GroupTrigger::LowHealth, anchor,
                        bot->GetName() + std::string(" is low on health"), false, guid))
                    NoteGroupTrigger(state.groupId, GroupTrigger::LowHealth,
                        m_config->groupLowHealthCooldownSeconds);
            }
            if (maxMana)
            {
                uint32_t const percent = bot->GetPower(POWER_MANA) * 100 / maxMana;
                uint32_t const threshold = m_config->groupLowManaThresholdPercent;
                bool const fired = state.manaLatches[guid].Update(
                    percent <= threshold, percent >= std::min<uint32_t>(100, threshold + 10));
                if (fired && GroupTriggerReady(state.groupId, GroupTrigger::LowMana,
                        m_config->groupLowManaCooldownSeconds) &&
                    Roll(m_config->groupLowManaChance) &&
                    QueueGroupChatter(GroupTrigger::LowMana, anchor,
                        bot->GetName() + std::string(" is nearly out of mana"), false, guid))
                    NoteGroupTrigger(state.groupId, GroupTrigger::LowMana,
                        m_config->groupLowManaCooldownSeconds);
            }
        }
    }

    void Manager::RunGroupChatter()
    {
        if (!m_config || (!m_config->groupChatterEnabled && !m_config->raidChatterEnabled))
            return;
        auto const now = Clock::now();

        std::vector<Player*> realPlayers;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (IsOnlineRealPlayer(player) && player->IsAlive())
                    realPlayers.push_back(player);
                if (realPlayers.size() >= 64)
                    break;
            }
        }

        for (Player* player : realPlayers)
        {
            Map const* map = player->FindMap();
            if (!map || !player->GetGroup())
                continue;
            bool const raid = map->IsRaid();
            if (raid && !m_config->raidChatterEnabled)
                continue;

            uint32_t groupId = 0;
            std::vector<Player*> bots = CollectGroupBots(player, raid, groupId);
            if (bots.empty())
                continue;

            GroupRuntimeState& state = m_groupStates[groupId];
            if (!state.groupId)
            {
                state.groupId = groupId;
                state.nextIdle = now + std::chrono::seconds(m_config->groupChatterScanSeconds);
                state.nextObject = now + std::chrono::seconds(m_config->groupNearbyObjectScanSeconds);
                state.nextQuestion = now + std::chrono::seconds(m_config->groupBotQuestionScanSeconds);
                state.nextMorale = now + std::chrono::seconds(m_config->raidMoraleScanSeconds);

                // First meeting and party membership are deterministic memories:
                // "first met" only when this bot holds no memory of this player yet.
                std::string area;
                std::string zone;
                std::string mapName;
                uint32_t mapId = 0;
                uint32_t areaId = 0;
                uint32_t zoneId = 0;
                FillLocation(player, area, zone, mapName, mapId, areaId, zoneId);
                MemoryFacts facts;
                facts.playerName = player->GetName();
                facts.zone = zone.empty() ? area : zone;
                facts.instance = mapName;
                for (Player* bot : bots)
                {
                    MemoryKey const key { bot->GetObjectGuid().GetRawValue(),
                        player->GetObjectGuid().GetRawValue() };
                    LoadMemories(key);
                    auto found = m_memories.find(key);
                    bool const firstEver = found == m_memories.end() || found->second.empty();
                    if (firstEver)
                        RecordMemory(key.botGuid, key.playerGuid, MemoryType::FirstMet, facts);
                    RecordMemory(key.botGuid, key.playerGuid, MemoryType::PartyMember, facts);
                }
            }
            state.lastSeen = now;

            std::string const instanceKey = std::to_string(player->GetMapId()) + ':' +
                std::to_string(CurrentInstanceId(player));
            uint64_t const instanceHash = static_cast<uint64_t>(player->GetMapId()) * 1000003ull +
                CurrentInstanceId(player);
            if (state.instanceKey != instanceHash)
            {
                state.instanceKey = instanceHash;
                state.bossesKilledInInstance = 0;
                state.questLogs.clear();
            }

            ScanGroupBots(state, player, bots, raid);

            if (now >= state.nextIdle)
            {
                state.nextIdle = now + std::chrono::seconds(raid
                    ? m_config->raidIdleScanSeconds : m_config->groupChatterScanSeconds);
                uint32_t const chance = raid ? m_config->raidIdleChance : m_config->groupIdleChance;
                uint32_t const cooldown = raid ? m_config->raidIdleCooldownSeconds
                                               : m_config->groupIdleCooldownSeconds;
                if (GroupTriggerReady(groupId, GroupTrigger::Idle, cooldown) && Roll(chance) &&
                    QueueGroupChatter(GroupTrigger::Idle, player,
                        "make a short casual remark to the group about what you are doing", true))
                    NoteGroupTrigger(groupId, GroupTrigger::Idle, cooldown);
            }
            if (raid && now >= state.nextMorale)
            {
                state.nextMorale = now + std::chrono::seconds(m_config->raidMoraleScanSeconds);
                if (GroupTriggerReady(groupId, GroupTrigger::Morale,
                        m_config->raidMoraleCooldownSeconds) &&
                    Roll(m_config->raidMoraleChance) &&
                    QueueGroupChatter(GroupTrigger::Morale, player,
                        "say something that keeps the raid's spirits up", true))
                    NoteGroupTrigger(groupId, GroupTrigger::Morale,
                        m_config->raidMoraleCooldownSeconds);
            }
            if (now >= state.nextQuestion)
            {
                state.nextQuestion = now + std::chrono::seconds(m_config->groupBotQuestionScanSeconds);
                if (GroupTriggerReady(groupId, GroupTrigger::BotQuestion,
                        m_config->groupBotQuestionCooldownSeconds) &&
                    Roll(m_config->groupBotQuestionChance) &&
                    QueueGroupChatter(GroupTrigger::BotQuestion, player,
                        "ask the group one short practical question about what you are doing", true))
                    NoteGroupTrigger(groupId, GroupTrigger::BotQuestion,
                        m_config->groupBotQuestionCooldownSeconds);
            }
            if (now >= state.nextObject)
            {
                state.nextObject = now + std::chrono::seconds(m_config->groupNearbyObjectScanSeconds);
                if (GroupTriggerReady(groupId, GroupTrigger::NearbyObject,
                        m_config->groupNearbyObjectCooldownSeconds) &&
                    Roll(m_config->groupNearbyObjectChance))
                {
                    float const distance = m_config->environmentContextDistance;
                    BoundedGameObjectRangeCheck check(player, distance, 24);
                    std::list<GameObject*> objects;
                    MaNGOS::GameObjectListSearcher<BoundedGameObjectRangeCheck> searcher(objects, check);
                    Cell::VisitGridObjects(player, searcher, distance);
                    std::vector<GameObject*> visible;
                    for (GameObject* object : objects)
                    {
                        if (!object || !object->IsInWorld() || !object->IsSpawned() ||
                            !object->GetGOInfo() || object->GetGOInfo()->name.empty() ||
                            !player->IsWithinLOSInMap(object))
                            continue;
                        visible.push_back(object);
                    }
                    if (!visible.empty())
                    {
                        GameObject* object = visible[RandomUInt(0,
                            static_cast<uint32_t>(visible.size() - 1))];
                        std::string const subject = object->GetGOInfo()->name;
                        if (QueueGroupChatter(GroupTrigger::NearbyObject, player,
                                "say something short about " + subject, false))
                            NoteGroupTrigger(groupId, GroupTrigger::NearbyObject,
                                m_config->groupNearbyObjectCooldownSeconds);
                    }
                }
            }
        }
    }

    namespace
    {
        struct GroupTriggerSettings
        {
            uint32_t chance = 0;
            uint32_t cooldownSeconds = 0;
        };
    }

    std::vector<Player*> Manager::GroupBotsForEvent(Player* subject, Player*& anchor,
                                                    bool& raid) const
    {
        std::vector<Player*> bots;
        anchor = nullptr;
        raid = false;
        if (!subject)
            return bots;
        Map const* map = subject->FindMap();
        if (!map)
            return bots;
        raid = map->IsRaid();
        uint32_t groupId = 0;
        if (IsOnlineRealPlayer(subject))
        {
            anchor = subject;
            bots = CollectGroupBots(subject, raid, groupId);
        }
        else
        {
            anchor = FindGroupRealPlayer(subject, raid, groupId);
            if (anchor)
                bots = CollectGroupBots(anchor, raid, groupId);
        }
        return bots;
    }

    void Manager::HandleEventMemories(Player* subject, std::string const& event,
                                      std::string const& detail, uint32_t creatureEntry,
                                      uint32_t creatureRank)
    {
        if (!m_config || !m_config->memoryEnabled || !subject)
            return;

        Player* anchor = nullptr;
        bool raid = false;
        std::vector<Player*> const bots = GroupBotsForEvent(subject, anchor, raid);
        if (!anchor || bots.empty())
            return;
        uint64_t const playerGuid = anchor->GetObjectGuid().GetRawValue();

        MemoryType type = MemoryType::Count;
        if (event == "quest_completed")
            type = MemoryType::QuestCompleted;
        else if (event == "level_up")
            type = MemoryType::LevelUp;
        else if (event == "creature_defeated")
        {
            // Only a classified boss becomes a durable memory; ordinary trash
            // kills are not worth remembering.
            bool const boss = (creatureEntry && IsCuratedBossEntry(creatureEntry)) ||
                creatureRank == CREATURE_ELITE_WORLDBOSS;
            if (!boss)
                return;
            type = MemoryType::BossKill;
        }
        else if (event == "player_defeated")
            type = MemoryType::PvpKill;
        else if (event == "dungeon_completed")
            type = MemoryType::DungeonCompleted;
        else
            return;

        std::string area;
        std::string zone;
        std::string mapName;
        uint32_t mapId = 0;
        uint32_t areaId = 0;
        uint32_t zoneId = 0;
        FillLocation(anchor, area, zone, mapName, mapId, areaId, zoneId);

        MemoryFacts facts;
        facts.playerName = anchor->GetName();
        facts.zone = zone.empty() ? area : zone;
        facts.instance = mapName;
        switch (type)
        {
            case MemoryType::QuestCompleted:
                facts.quest = detail;
                break;
            case MemoryType::LevelUp:
                try { facts.level = static_cast<uint32_t>(std::stoul(detail)); }
                catch (...) { facts.level = anchor->GetLevel(); }
                break;
            case MemoryType::BossKill:
                facts.boss = detail;
                break;
            case MemoryType::PvpKill:
                facts.victim = detail;
                break;
            case MemoryType::DungeonCompleted:
                if (!detail.empty())
                    facts.instance = detail;
                break;
            default:
                break;
        }
        for (Player* bot : bots)
            RecordMemory(bot->GetObjectGuid().GetRawValue(), playerGuid, type, facts);
    }

    bool Manager::HandleGuildLoginGreeting(Player* member)
    {
        if (!m_config || !m_config->guildChatterEnabled ||
            !m_config->guildLoginGreetingEnabled || !IsOnlineRealPlayer(member) ||
            !member->GetGuildId())
            return false;

        uint64_t const playerGuid = member->GetObjectGuid().GetRawValue();
        auto const now = Clock::now();
        auto cooldown = m_guildGreetingCooldowns.find(playerGuid);
        if (cooldown != m_guildGreetingCooldowns.end() && cooldown->second > now)
            return false;

        if (!Roll(m_config->guildLoginGreetingChance))
            return true;      // Claimed event

        LoginGreetingBand const band = SelectLoginGreetingBand(
            m_config->guildLoginGreetingQuickChance,
            m_config->guildLoginGreetingBusyChance,
            RandomUInt(1, 100));
        uint32_t const delaySeconds = PickLoginGreetingDelaySeconds(band, RandomUInt(0, 100));

        PendingGuildGreeting greeting;
        greeting.playerGuid = playerGuid;
        greeting.guildId = member->GetGuildId();
        greeting.playerName = member->GetName();
        greeting.delaySeconds = delaySeconds;
        greeting.scheduledAt = now + std::chrono::seconds(delaySeconds);
        greeting.nextRetry = greeting.scheduledAt;
        greeting.deadline = now + std::chrono::seconds(m_config->guildLoginGreetingReadinessTimeoutSeconds);
        m_pendingGuildGreetings[playerGuid] = greeting;
        m_guildGreetingCooldowns[playerGuid] = now +
            std::chrono::seconds(m_config->guildLoginGreetingCooldownSeconds);
        return true;
    }

    void Manager::CancelPendingGuildGreeting(uint64_t playerGuid)
    {
        m_pendingGuildGreetings.erase(playerGuid);
    }

    void Manager::ProcessPendingGuildGreetings()
    {
        if (m_pendingGuildGreetings.empty())
            return;

        auto const now = Clock::now();
        for (auto it = m_pendingGuildGreetings.begin(); it != m_pendingGuildGreetings.end(); )
        {
            PendingGuildGreeting& greeting = it->second;
            if (now < greeting.nextRetry)
            {
                ++it;
                continue;
            }

            if (now > greeting.deadline)
            {
                it = m_pendingGuildGreetings.erase(it);
                continue;
            }

            Player* member = ObjectAccessor::FindPlayer(ObjectGuid(greeting.playerGuid));
            if (!member || !member->IsInWorld() || member->GetGuildId() != greeting.guildId)
            {
                it = m_pendingGuildGreetings.erase(it);
                continue;
            }

            std::vector<Player*> botCandidates;
            {
                HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
                for (auto const& entry : sObjectAccessor.GetPlayers())
                {
                    Player* candidate = entry.second;
                    if (!candidate || !candidate->IsInWorld() || !candidate->IsAlive() ||
                        candidate == member || candidate->GetGuildId() != greeting.guildId ||
                        !Script_IsAIControlled(candidate))
                        continue;
                    botCandidates.push_back(candidate);
                    if (botCandidates.size() >= m_config->guildLoginGreetingMaxCandidates)
                        break;
                }
            }

            if (botCandidates.empty())
            {
                greeting.nextRetry = now + std::chrono::seconds(m_config->guildLoginGreetingRetryIntervalSeconds);
                ++it;
                continue;
            }

            std::shuffle(botCandidates.begin(), botCandidates.end(), RandomEngine());
            bool const multi = m_config->guildLoginGreetingMultiReplyChance &&
                Roll(m_config->guildLoginGreetingMultiReplyChance);
            uint32_t const responderCount = multi
                ? std::min<uint32_t>(m_config->guildLoginGreetingMaxResponders, static_cast<uint32_t>(botCandidates.size()))
                : 1;

            SpeakerSnapshot speakerSnapshot = SnapshotSpeaker(member);
            bool const addressByName = m_config->guildLoginGreetingPlayerNameChance &&
                Roll(m_config->guildLoginGreetingPlayerNameChance);
            std::string const topic = addressByName
                ? ("greet " + greeting.playerName + ", a guild member who just logged in, by name in one short friendly line")
                : "greet a guild member who just logged in, in one short friendly line";

            for (uint32_t i = 0; i < responderCount; ++i)
            {
                Player* bot = botCandidates[i];
                ActorSnapshot actor = SnapshotBot(bot);
                actor.anchorPlayerGuid = greeting.playerGuid;
                QueueDialogue(actor, speakerSnapshot, ChatScope::Guild, "",
                    "guild:login-greeting", topic, RequestPriority::Group, false, false);
            }

            it = m_pendingGuildGreetings.erase(it);
        }
    }

    bool Manager::TryClaimGroupTrigger(Player* subject, std::string const& event,
                                       std::string const& detail, uint32_t creatureEntry,
                                       uint32_t creatureRank, uint32_t itemQuality)
    {
        if (!m_config || !m_config->groupChatterEnabled || !subject)
            return false;
        Map const* map = subject->FindMap();
        if (!map)
            return false;
        bool const raid = map->IsRaid();
        if (raid && !m_config->raidChatterEnabled)
            return false;
        if (!IsScopeEnabled(*m_config, raid ? ChatScope::Raid : ChatScope::Party))
            return false;

        // Events the generic event system also owns must be claimed even when
        // this roll produces nothing, so one situation never yields two lines.
        bool const legacyShared = event == "died" || event == "creature_defeated" ||
            event == "quest_completed" || event == "item_looted" || event == "rare_item" ||
            event == "epic_item";

        GroupTrigger trigger = GroupTrigger::Idle;
        GroupTriggerSettings settings;
        bool conversation = false;
        if (event == "died")
        {
            trigger = GroupTrigger::Death;
            settings = { m_config->groupDeathChance, m_config->groupDeathCooldownSeconds };
        }
        else if (event == "creature_defeated")
        {
            bool const boss = (creatureEntry && IsCuratedBossEntry(creatureEntry)) ||
                creatureRank == CREATURE_ELITE_WORLDBOSS;
            trigger = boss ? GroupTrigger::BossKill : GroupTrigger::TrashKill;
            settings = boss
                ? GroupTriggerSettings { m_config->groupBossKillChance,
                    m_config->groupBossKillCooldownSeconds }
                : GroupTriggerSettings { m_config->groupKillChance,
                    m_config->groupKillCooldownSeconds };
            conversation = boss;
        }
        else if (event == "quest_completed")
        {
            trigger = GroupTrigger::QuestComplete;
            settings = { m_config->groupQuestCompleteChance,
                m_config->groupQuestCooldownSeconds };
        }
        else if (event == "item_looted" || event == "rare_item" || event == "epic_item")
        {
            LootChanceTable const table { m_config->groupLootUncommonChance,
                m_config->groupLootRareChance, m_config->groupLootEpicChance,
                m_config->groupLootLegendaryChance };
            uint32_t const chance = LootChanceForQuality(itemQuality, table);
            if (!chance)
                return false;     // nothing worth a group reaction; leave the event path alone
            trigger = GroupTrigger::Loot;
            settings = { chance, m_config->groupLootCooldownSeconds };
        }
        else if (event == "released_ghost")
        {
            trigger = GroupTrigger::CorpseRun;
            settings = { m_config->groupCorpseRunChance,
                m_config->groupCorpseRunCooldownSeconds };
        }
        else if (event == "spell_cast")
        {
            // Instances of this hook are hot; short-circuit before touching groups.
            if (!Roll(m_config->groupSpellCastChance))
                return false;
            trigger = GroupTrigger::SpellCast;
            settings = { 100, m_config->groupSpellCastCooldownSeconds };
        }
        else if (event == "zone_changed")
        {
            if (map->IsDungeon())
                return false;     // instance transitions belong to the map-change path
            trigger = GroupTrigger::ZoneChange;
            settings = { m_config->groupZoneChangeChance,
                m_config->groupZoneChangeCooldownSeconds };
        }
        else
            return false;

        if (!legacyShared && trigger != GroupTrigger::SpellCast &&
            !Roll(settings.chance))
            return false;

        uint32_t groupId = 0;
        Player* anchor = nullptr;
        if (IsOnlineRealPlayer(subject))
        {
            anchor = subject;
            if (CollectGroupBots(anchor, raid, groupId).empty())
                return false;
        }
        else
        {
            anchor = FindGroupRealPlayer(subject, raid, groupId);
            if (!anchor)
                return false;
        }
        if (!anchor || !anchor->IsAlive())
            return false;

        if (!GroupTriggerReady(groupId, trigger, settings.cooldownSeconds))
            return legacyShared;  // still on cooldown; only legacy-shared events are owned
        if (trigger == GroupTrigger::BossKill)
        {
            auto state = m_groupStates.find(groupId);
            if (state != m_groupStates.end())
                ++state->second.bossesKilledInInstance;
        }
        if (legacyShared && trigger != GroupTrigger::SpellCast && !Roll(settings.chance))
            return true;      // owned by the group system, this roll produced nothing
        if (QueueGroupChatter(trigger, anchor, detail, conversation))
            NoteGroupTrigger(groupId, trigger, settings.cooldownSeconds);
        return legacyShared;
    }

    void Manager::RunGuildChatter()
    {
        if (!m_config || !m_config->guildChatterEnabled || !IsScopeEnabled(*m_config, ChatScope::Guild))
            return;

        struct GuildCandidate
        {
            uint32_t guildId = 0;
            Player* member = nullptr;
            std::vector<Player*> bots;
        };
        std::vector<GuildCandidate> candidates;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (!player || !player->IsInWorld() || !player->GetGuildId())
                    continue;
                size_t index = candidates.size();
                for (size_t i = 0; i < candidates.size(); ++i)
                    if (candidates[i].guildId == player->GetGuildId())
                    {
                        index = i;
                        break;
                    }
                if (index == candidates.size())
                {
                    candidates.push_back(GuildCandidate { player->GetGuildId(), nullptr, {} });
                    index = candidates.size() - 1;
                }
                GuildCandidate& candidate = candidates[index];
                if (IsOnlineRealPlayer(player))
                {
                    if (!candidate.member)
                        candidate.member = player;
                }
                else if (Script_IsAIControlled(player) && player->IsAlive())
                    candidate.bots.push_back(player);
                if (candidates.size() >= 8)
                    break;
            }
        }

        auto const now = Clock::now();
        for (GuildCandidate const& candidate : candidates)
        {
            if (!candidate.member || candidate.bots.empty())
                continue;
            if (!FindOnlineRealGuildAudience(candidate.guildId, ChatScope::Guild))
                continue;
            std::string const key = "guild:" + std::to_string(candidate.guildId) + ":ambient";
            auto cooldown = m_groupTriggerCooldowns.find(key);
            if (cooldown != m_groupTriggerCooldowns.end() && cooldown->second > now)
                continue;
            auto humanActive = m_guildPlayerConversationUntil.find(candidate.guildId);
            if (humanActive != m_guildPlayerConversationUntil.end() && humanActive->second > now)
                continue;
            if (!Roll(m_config->guildAmbientChance))
                continue;

            std::vector<Player*> shuffled = candidate.bots;
            std::shuffle(shuffled.begin(), shuffled.end(), RandomEngine());
            Player* speaker = shuffled.front();
            auto lastSpeaker = m_guildLastSpeaker.find(candidate.guildId);
            if (lastSpeaker != m_guildLastSpeaker.end() &&
                lastSpeaker->second.first == speaker->GetObjectGuid().GetRawValue() &&
                now - lastSpeaker->second.second <
                    std::chrono::seconds(m_config->guildRecentSpeakerSuppressionSeconds) &&
                shuffled.size() > 1)
                speaker = shuffled[1];

            std::string topic = "start a short casual guild conversation";
            if (Roll(m_config->guildParticipantReferenceChance) && shuffled.size() > 1)
                topic += ", mentioning another guild member who is online";
            if (Roll(m_config->guildZoneNameChance))
            {
                std::string area;
                std::string zone;
                std::string mapName;
                uint32_t mapId = 0, areaId = 0, zoneId = 0;
                FillLocation(candidate.member, area, zone, mapName, mapId, areaId, zoneId);
                if (!zone.empty())
                    topic += ", referring to " + zone;
            }
            if (Roll(m_config->guildHistoryContextChance))
                topic += ", referencing something the guild said recently";

            bool const conversation = candidate.bots.size() >= 2 &&
                m_config->guildConversationChance && Roll(m_config->guildConversationChance);
            ActorSnapshot actor = SnapshotBot(speaker);
            actor.anchorPlayerGuid = candidate.member->GetObjectGuid().GetRawValue();
            GroupConversation scene;
            if (conversation)
            {
                scene.id = m_nextGroupConversationId++;
                scene.groupId = candidate.guildId;
                scene.mapId = speaker->GetMapId();
                scene.instanceId = CurrentInstanceId(speaker);
                scene.anchorPlayerGuid = candidate.member->GetObjectGuid().GetRawValue();
                scene.maximumLines = std::max<uint32_t>(1, m_config->guildMaximumLines);
                scene.conversation = true;
                scene.guild = true;
                scene.nextTurn = now;
                scene.expires = now + std::chrono::seconds(
                    m_config->groupConversationReplyWindowSeconds +
                    static_cast<int64_t>(m_config->groupConversationTurnGapSeconds) *
                        scene.maximumLines);
                uint32_t const participants = std::min<uint32_t>(
                    std::max<uint32_t>(1, m_config->guildMaximumParticipants),
                    static_cast<uint32_t>(shuffled.size()));
                for (uint32_t i = 0; i < participants; ++i)
                    scene.speakers.push_back(shuffled[i]->GetObjectGuid().GetRawValue());
                if (shuffled.front() != speaker)
                    std::swap(shuffled.front(), *std::find(shuffled.begin(), shuffled.end(), speaker));
                m_groupConversations[std::to_string(scene.id)] = scene;
            }
            if (QueueDialogue(actor, SnapshotSpeaker(candidate.member), ChatScope::Guild, "",
                    "guild:ambient", topic, RequestPriority::Group, false, false, 0,
                    nullptr, nullptr, false,
                    conversation ? &m_groupConversations[std::to_string(scene.id)] : nullptr))
            {
                m_groupTriggerCooldowns[key] = now +
                    std::chrono::seconds(m_config->guildAmbientCooldownSeconds);
                m_guildLastSpeaker[candidate.guildId] =
                    std::make_pair(speaker->GetObjectGuid().GetRawValue(), now);
            }
            else if (conversation)
                m_groupConversations.erase(std::to_string(scene.id));
        }
    }

    void Manager::PruneGroupAndGuildState()
    {
        auto const now = Clock::now();
        for (auto it = m_groupStates.begin(); it != m_groupStates.end(); )
        {
            bool const stale = it->second.lastSeen.time_since_epoch().count() != 0 &&
                now - it->second.lastSeen > std::chrono::minutes(30);
            it = stale ? m_groupStates.erase(it) : std::next(it);
        }
        for (auto it = m_groupTriggerCooldowns.begin(); it != m_groupTriggerCooldowns.end(); )
            it = it->second <= now ? m_groupTriggerCooldowns.erase(it) : std::next(it);
        for (auto it = m_groupConversations.begin(); it != m_groupConversations.end(); )
            it = it->second.expires <= now ? m_groupConversations.erase(it) : std::next(it);
        for (auto it = m_guildGreetingCooldowns.begin(); it != m_guildGreetingCooldowns.end(); )
            it = it->second <= now ? m_guildGreetingCooldowns.erase(it) : std::next(it);
        for (auto it = m_guildReplyDebounce.begin(); it != m_guildReplyDebounce.end(); )
            it = it->second <= now ? m_guildReplyDebounce.erase(it) : std::next(it);

        uint32_t const sessionTtlSeconds = 86400;
        uint32_t const nowSeconds = static_cast<uint32_t>(time(nullptr));
        for (auto it = m_guildSessionHistory.begin(); it != m_guildSessionHistory.end(); )
        {
            auto const& turns = it->second.Turns();
            bool const stale = turns.empty() ||
                (nowSeconds > turns.back().timestampSeconds && (nowSeconds - turns.back().timestampSeconds) > sessionTtlSeconds);
            it = stale ? m_guildSessionHistory.erase(it) : std::next(it);
        }
        while (m_guildSessionHistory.size() > 4096)
            m_guildSessionHistory.erase(m_guildSessionHistory.begin());

        while (m_lastPlayerMap.size() > 4096)
            m_lastPlayerMap.erase(m_lastPlayerMap.begin());
    }

    void Manager::HandlePlayerMapChanged(Player* player)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !player ||
            !player->IsInWorld())
            return;

        uint64_t const guid = player->GetObjectGuid().GetRawValue();
        uint32_t const currentMap = player->GetMapId();
        Map const* map = player->FindMap();
        std::string const currentName = map ? map->GetMapName() : "";
        auto found = m_lastPlayerMap.find(guid);
        std::pair<uint32_t, std::string> const previous = found == m_lastPlayerMap.end()
            ? std::make_pair(currentMap, currentName) : found->second;
        m_lastPlayerMap[guid] = std::make_pair(currentMap, currentName);
        if (previous.first == currentMap)
            return;

        bool const enteredInstance = map && map->IsDungeon();
        bool const leftInstance = !enteredInstance && previous.first != 0;

        Player* anchor = nullptr;
        bool raid = false;
        std::vector<Player*> bots = GroupBotsForEvent(player, anchor, raid);
        if (!anchor || bots.empty())
            return;

        if (enteredInstance && m_config->groupChatterEnabled)
        {
            uint32_t groupId = 0;
            CollectGroupBots(anchor, raid, groupId);
            if (GroupTriggerReady(groupId, GroupTrigger::DungeonEntry,
                    m_config->groupDungeonEntryCooldownSeconds) &&
                Roll(m_config->groupDungeonEntryChance))
            {
                if (QueueGroupChatter(GroupTrigger::DungeonEntry, anchor,
                        "the group has just entered " + currentName, true))
                    NoteGroupTrigger(groupId, GroupTrigger::DungeonEntry,
                        m_config->groupDungeonEntryCooldownSeconds);
            }
        }
        else if (leftInstance)
        {
            // Leaving an instance after at least one boss kill is the
            // deterministic "we cleared this place" signal.
            uint32_t groupId = 0;
            std::vector<Player*> const groupBots = CollectGroupBots(anchor, raid, groupId);
            auto state = m_groupStates.find(groupId);
            uint32_t const bosses = state == m_groupStates.end()
                ? 0 : state->second.bossesKilledInInstance;
            if (bosses)
            {
                MemoryFacts facts;
                facts.playerName = anchor->GetName();
                facts.instance = previous.second;
                for (Player* bot : groupBots)
                    RecordMemory(bot->GetObjectGuid().GetRawValue(),
                        anchor->GetObjectGuid().GetRawValue(), MemoryType::DungeonCompleted, facts);
                if (state != m_groupStates.end())
                {
                    state->second.bossesKilledInInstance = 0;
                    state->second.instanceKey = 0;
                }
                HandleEvent(anchor, "dungeon_completed", previous.second);
            }
        }
        else if (m_config->groupChatterEnabled)
        {
            std::string area;
            std::string zone;
            std::string mapName;
            uint32_t mapId = 0;
            uint32_t areaId = 0;
            uint32_t zoneId = 0;
            FillLocation(anchor, area, zone, mapName, mapId, areaId, zoneId);
            uint32_t groupId = 0;
            CollectGroupBots(anchor, raid, groupId);
            if (GroupTriggerReady(groupId, GroupTrigger::ZoneChange,
                    m_config->groupZoneChangeCooldownSeconds) &&
                Roll(m_config->groupZoneChangeChance) &&
                QueueGroupChatter(GroupTrigger::ZoneChange, anchor,
                    "the group has just arrived in " + (zone.empty() ? mapName : zone), true))
                NoteGroupTrigger(groupId, GroupTrigger::ZoneChange,
                    m_config->groupZoneChangeCooldownSeconds);
        }
    }

    void Manager::HandleGameEventState(uint16_t eventId, bool started, std::string const& description)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused)
            return;

        std::string desc = description;
        if (desc.empty())
        {
            auto const& events = sGameEventMgr.GetEventMap();
            if (eventId < events.size())
                desc = events[eventId].description;
        }
        if (desc.empty())
            desc = "event " + std::to_string(eventId);

        std::vector<Player*> targets;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (player && player->IsInWorld() && player->IsAlive())
                {
                    if (!Script_IsAIControlled(player))
                        targets.push_back(player);
                }
            }
            if (targets.empty())
            {
                for (auto const& entry : sObjectAccessor.GetPlayers())
                {
                    Player* player = entry.second;
                    if (player && player->IsInWorld() && player->IsAlive())
                    {
                        targets.push_back(player);
                        break;
                    }
                }
            }
        }

        std::string const eventName = started ? "game_event_started" : "game_event_stopped";
        for (Player* target : targets)
            HandleEvent(target, eventName, desc);
    }

    void Manager::LoadInstanceLore()
    {
        m_instanceLore.clear();
        m_instanceLoreLoaded = false;
        if (!m_config)
            return;

        std::vector<fs::path> candidates = {
            fs::path("modules/mod-azeroth-voices/data/instance_lore.json"),
            fs::path("data/instance_lore.json")
        };
        if (!m_config->ragDirectory.empty())
            candidates.push_back(fs::path(m_config->ragDirectory).parent_path() / "instance_lore.json");
#ifdef TW_SOURCE_MODULES_DIR
        candidates.push_back(fs::path(TW_SOURCE_MODULES_DIR) / "mod-azeroth-voices" / "data" /
            "instance_lore.json");
#endif

        for (fs::path const& candidate : candidates)
        {
            std::error_code errorCode;
            if (!fs::exists(candidate, errorCode) || fs::is_directory(candidate, errorCode))
                continue;
            std::ifstream stream(candidate);
            if (!stream)
                continue;

            std::stringstream buffer;
            buffer << stream.rdbuf();
            std::string error;
            if (!ParseInstanceLore(buffer.str(), m_instanceLore, error))
            {
                sLog.outError("[AzerothVoices][LORE] %s could not be parsed: %s",
                    candidate.string().c_str(), error.c_str());
                m_instanceLore.clear();
                continue;
            }
            m_instanceLoreLoaded = true;
            if (!error.empty())
                sLog.outError("[AzerothVoices][LORE] %s loaded %u entries with warnings: %s",
                    candidate.string().c_str(), static_cast<unsigned>(m_instanceLore.size()), error.c_str());
            else
                sLog.outString("[AzerothVoices][LORE] Loaded %u curated instance lore entries from %s.",
                    static_cast<unsigned>(m_instanceLore.size()), candidate.string().c_str());
            return;
        }

        sLog.outString("[AzerothVoices][LORE] No instance_lore.json was found; instances fall back to the "
            "engine map name and the current area.");
    }

    void Manager::PruneGeneralPacing()
    {
        m_generalPacing.Prune(Clock::now());
    }

    void Manager::PrunePartyPacing()
    {
        m_partyPacing.Prune(Clock::now());
    }

    bool Manager::ReserveGeneralPacing(ChatScope scope, std::string const& channelName,
                                       ActorSnapshot const& location, size_t lines)
    {
        if (!m_config || !m_config->generalChatPacingEnabled)
            return true;
        std::string const key = NormalizePacingKey(scope, channelName, location.mapId,
            location.instanceId, location.zoneId, location.instance);
        if (key.empty())
            return true;

        auto const now = Clock::now();
        if (!m_generalPacing.CanDeliver(key, now))
            return false;

        uint32_t const assumedLineCharacters = m_config->maximumReplyLines
            ? std::max<uint32_t>(24, m_config->maximumReplyCharacters / m_config->maximumReplyLines)
            : 80;
        uint32_t const duration = EstimateExchangeMilliseconds(
            std::max<size_t>(1, lines), m_config->typingSimulationEnabled,
            m_config->typingBaseDelayMilliseconds,
            m_config->typingDelayPerCharacterMilliseconds,
            assumedLineCharacters, 500);
        m_generalPacing.Reserve(key, now, duration,
            m_config->generalChatMinimumGapSeconds * 1000);
        return true;
    }

    void Manager::RunProximity()
    {
        if (!m_config || !m_config->proximityEnabled)
            return;

        auto const now = Clock::now();
        bool const instanceScanDue = now >= m_nextProximityInstanceScan;
        if (instanceScanDue)
            m_nextProximityInstanceScan = now +
                std::chrono::seconds(m_config->proximityInstanceScanSeconds);

        std::vector<Player*> players;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (IsOnlineRealPlayer(player) && player->IsAlive())
                    players.push_back(player);
                if (players.size() >= 64)
                    break;
            }
        }

        for (Player* player : players)
        {
            Map const* map = player->FindMap();
            if (!map)
                continue;
            bool const dungeon = map->IsDungeon();
            // Battlegrounds and arenas are always excluded. Dungeons and raids
            // are optional and use their own scan cadence.
            if (!IsProximityMapEligible(map->IsBattleGround(), false, dungeon,
                    m_config->proximityIncludeInstances))
                continue;
            if (dungeon && !instanceScanDue)
                continue;

            std::string const zoneKey = dungeon
                ? "map:" + InstanceIdentity(player->GetMapId(), CurrentInstanceId(player))
                : "zone:" + std::to_string(player->GetZoneId());
            uint32_t const baseChance = dungeon
                ? m_config->proximityInstanceChance : m_config->proximityOutdoorChance;
            auto counter = m_proximityZoneScenes.find(zoneKey);
            uint32_t const scenes = counter == m_proximityZoneScenes.end() ? 0 : counter->second;
            uint32_t const chance = ApplyZoneFatigue(baseChance, scenes,
                m_config->proximityZoneFatigueScenes,
                m_config->proximityZoneFatigueDecayPercent);

            bool queued = false;
            if (Roll(chance))
                queued = QueueProximityScene(player, false);

            if (queued)
            {
                if (m_proximityZoneScenes.size() < 512 ||
                    m_proximityZoneScenes.count(zoneKey))
                    m_proximityZoneScenes[zoneKey] = scenes + 1;
            }
            else
            {
                uint32_t const decayed = DecayZoneScenes(scenes,
                    m_config->proximityZoneFatigueDecayPercent);
                if (decayed)
                    m_proximityZoneScenes[zoneKey] = decayed;
                else
                    m_proximityZoneScenes.erase(zoneKey);
            }
        }
    }

    bool Manager::QueueProximityScene(Player* anchor, bool forced)
    {
        if (!m_started || m_stopping || m_paused || !m_config || !m_config->proximityEnabled ||
            !anchor || !anchor->IsInWorld() || !anchor->IsAlive())
            return false;
        Map const* map = anchor->FindMap();
        if (!map)
            return false;

        bool const dungeon = map->IsDungeon();
        if (!IsProximityMapEligible(map->IsBattleGround(), false, dungeon,
                m_config->proximityIncludeInstances))
            return false;

        uint32_t const mapId = anchor->GetMapId();
        uint32_t const instanceId = CurrentInstanceId(anchor);
        float const distance = m_config->sayDistance;
        MaNGOS::AllCreaturesInRange check(anchor, distance);
        std::list<Creature*> creatures;
        MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
        Cell::VisitGridObjects(anchor, searcher, distance);

        auto const now = Clock::now();
        std::vector<Creature*> eligible;
        for (Creature* creature : creatures)
        {
            if (!creature || creature->GetMapId() != mapId)
                continue;
            if (CurrentInstanceId(creature) != instanceId || creature->IsInCombat())
                continue;
            if (!creature->IsWithinLOSInMap(anchor) ||
                !creature->IsVisibleForOrDetect(anchor, anchor, true))
                continue;
            auto cooldown = m_proximitySpeakerCooldowns.find(
                creature->GetObjectGuid().GetRawValue());
            if (cooldown != m_proximitySpeakerCooldowns.end() && cooldown->second > now)
                continue;
            if (!EvaluateProximityCreature(creature, *m_config).eligible)
                continue;
            eligible.push_back(creature);
        }
        if (eligible.empty())
        {
            RecordPreflightRejection(PreflightReason::InvalidActor);
            return false;
        }

        uint32_t const minimum = std::min<uint32_t>(
            std::max<uint32_t>(1, m_config->proximityMinimumSpeakers),
            static_cast<uint32_t>(eligible.size()));
        uint32_t const maximum = std::min<uint32_t>(
            std::max<uint32_t>(minimum, m_config->proximityMaximumSpeakers),
            static_cast<uint32_t>(eligible.size()));
        uint32_t const speakerCount = RandomUInt(minimum, maximum);
        std::shuffle(eligible.begin(), eligible.end(), RandomEngine());
        eligible.resize(speakerCount);

        ProximityScene scene;
        scene.id = m_nextProximitySceneId++;
        if (!scene.id)
            scene.id = m_nextProximitySceneId++;
        scene.anchorPlayerGuid = anchor->GetObjectGuid().GetRawValue();
        scene.mapId = mapId;
        scene.instanceId = instanceId;
        scene.zoneId = anchor->GetZoneId();
        scene.instance = dungeon;
        scene.zoneKey = dungeon
            ? "map:" + InstanceIdentity(mapId, instanceId)
            : "zone:" + std::to_string(scene.zoneId);
        scene.maximumLines = std::max<uint32_t>(1, m_config->proximityMaximumLines);
        scene.conversation = eligible.size() > 1 && Roll(m_config->proximityConversationChance);
        scene.nextTurn = now;
        scene.expires = now + std::chrono::seconds(
            m_config->proximityReplyWindowSeconds +
            (scene.conversation
                ? static_cast<int64_t>(m_config->proximityTurnGapSeconds) * scene.maximumLines : 0));
        for (Creature* creature : eligible)
            scene.speakers.push_back(creature->GetObjectGuid().GetRawValue());

        Creature* first = eligible.front();
        NpcDisposition const disposition = ClassifyNpcDisposition(first, anchor);
        bool const firstIsBoss = ClassifyCreatureBoss(first, *m_config).boss;
        ActorSnapshot actor = SnapshotCreature(first, anchor, disposition,
            "proximity:" + std::string(ProximityRejectionName(ProximityRejection::None)), firstIsBoss);
        std::string const topic = dungeon
            ? "this place and what is waiting further inside"
            : "the area around them";

        std::string const sceneKey = std::to_string(scene.id);
        m_proximityScenes[sceneKey] = scene;
        ProximityScene& stored = m_proximityScenes[sceneKey];
        if (!QueueDialogue(actor, SnapshotSpeaker(anchor), ChatScope::Say, "", "proximity",
                topic, RequestPriority::Ambient, true, false, 0, &stored))
        {
            m_proximityScenes.erase(sceneKey);
            return false;
        }

        m_proximitySpeakerCooldowns[first->GetObjectGuid().GetRawValue()] = now +
            std::chrono::seconds(m_config->proximityEntityCooldownSeconds);
        if (m_config->debug)
            sLog.outDebug("[AzerothVoices] Proximity scene %llu queued with %u speaker(s) on map %u:%u.",
                static_cast<unsigned long long>(scene.id),
                static_cast<unsigned>(scene.speakers.size()), mapId, instanceId);
        return true;
    }

    void Manager::MaybeQueueProximityTurn(ChatRequest const& request, std::string const& reply)
    {
        if (!m_config || !request.sceneId)
            return;
        auto found = m_proximityScenes.find(std::to_string(request.sceneId));
        if (found == m_proximityScenes.end())
            return;
        ProximityScene& scene = found->second;
        ++scene.deliveredLines;
        if (!scene.conversation || scene.deliveredLines >= scene.maximumLines ||
            scene.replyTurns >= m_config->proximityMaximumReplyTurns)
            return;

        auto const now = Clock::now();
        if (now > scene.expires)
            return;

        Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(scene.anchorPlayerGuid));
        if (!anchor || !anchor->IsInWorld() || !anchor->IsAlive() ||
            anchor->GetMapId() != scene.mapId ||
            CurrentInstanceId(anchor) != scene.instanceId)
            return;

        std::vector<Creature*> candidates;
        for (uint64_t guid : scene.speakers)
        {
            Creature* creature = ObjectAccessor::GetCreature(*anchor, ObjectGuid(guid));
            if (!creature || creature->GetObjectGuid().GetRawValue() == request.actor.guid)
                continue;
            if (creature->IsInCombat() || !creature->IsWithinDist(anchor, m_config->sayDistance, false))
                continue;
            if (!creature->IsWithinLOSInMap(anchor) ||
                !creature->IsVisibleForOrDetect(anchor, anchor, true))
                continue;
            auto cooldown = m_proximitySpeakerCooldowns.find(guid);
            if (cooldown != m_proximitySpeakerCooldowns.end() && cooldown->second > now)
                continue;
            if (!EvaluateProximityCreature(creature, *m_config).eligible)
                continue;
            candidates.push_back(creature);
        }
        if (candidates.empty())
            return;

        Creature* next = candidates[RandomUInt(0, static_cast<uint32_t>(candidates.size() - 1))];
        NpcDisposition const disposition = ClassifyNpcDisposition(next, anchor);
        ActorSnapshot actor = SnapshotCreature(next, anchor, disposition,
            "proximity-conversation", ClassifyCreatureBoss(next, *m_config).boss);

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
        previous.isBot = false;

        scene.nextTurn = now + std::chrono::seconds(m_config->proximityTurnGapSeconds);
        if (QueueDialogue(actor, previous, ChatScope::Say, "", "proximity-followup", reply,
                RequestPriority::Ambient, true, false, request.conversationDepth + 1, &scene))
        {
            ++scene.replyTurns;
            m_proximitySpeakerCooldowns[next->GetObjectGuid().GetRawValue()] = now +
                std::chrono::seconds(m_config->proximityEntityCooldownSeconds);
        }
    }

    void Manager::PruneProximityScenes()
    {
        auto const now = Clock::now();
        for (auto it = m_proximityScenes.begin(); it != m_proximityScenes.end(); )
            it = it->second.expires <= now ? m_proximityScenes.erase(it) : std::next(it);
        for (auto it = m_proximitySpeakerCooldowns.begin(); it != m_proximitySpeakerCooldowns.end(); )
            it = it->second <= now ? m_proximitySpeakerCooldowns.erase(it) : std::next(it);
    }

    void Manager::RunBossDialogue()
    {
        if (!m_config || !m_config->bossDialogueEnabled)
            return;

        auto const now = Clock::now();
        std::vector<Player*> players;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
            {
                Player* player = entry.second;
                if (IsOnlineRealPlayer(player) && player->IsAlive())
                    players.push_back(player);
                if (players.size() >= 64)
                    break;
            }
        }

        for (Player* player : players)
        {
            Map const* map = player->FindMap();
            if (!map || !map->IsDungeon() || map->IsBattleGround())
                continue;

            uint32_t const mapId = player->GetMapId();
            uint32_t const instanceId = CurrentInstanceId(player);
            float const distance = m_config->bossDialogueMaximumDistance;
            MaNGOS::AllCreaturesInRange check(player, distance);
            std::list<Creature*> creatures;
            MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
            Cell::VisitGridObjects(player, searcher, distance);

            for (Creature* creature : creatures)
            {
                if (!creature || creature->GetMapId() != mapId ||
                    CurrentInstanceId(creature) != instanceId)
                    continue;
                if (!ClassifyCreatureBoss(creature, *m_config).boss)
                    continue;
                if (!creature->IsAlive() || creature->IsInCombat())
                    continue;
                if (NpcPlayableFactionReaction(creature) >= REP_NEUTRAL)
                    continue;
                if (!creature->IsWithinLOSInMap(player) ||
                    !creature->IsVisibleForOrDetect(player, player, true))
                    continue;
                float const safeDistance = creature->GetAttackDistance(player) +
                    m_config->bossDialogueAggroMargin;
                if (creature->IsWithinDist(player, safeDistance, false))
                    continue;

                std::string const key = InstanceIdentity(mapId, instanceId) + ':' +
                    std::to_string(creature->GetObjectGuid().GetRawValue());
                auto inserted = m_bossPresences.emplace(key, BossPresence());
                BossPresence& presence = inserted.first->second;
                if (inserted.second)
                {
                    presence.key = key;
                    presence.bossGuid = creature->GetObjectGuid().GetRawValue();
                    presence.entry = creature->GetCreatureInfo() ? creature->GetCreatureInfo()->entry : 0;
                    presence.mapId = mapId;
                    presence.instanceId = instanceId;
                    presence.anchorPlayerGuid = player->GetObjectGuid().GetRawValue();
                    presence.name = creature->GetName();
                    presence.subName = creature->GetSubName();
                    std::string area;
                    std::string zone;
                    std::string mapName;
                    uint32_t filledMapId = 0;
                    uint32_t areaId = 0;
                    uint32_t zoneId = 0;
                    FillLocation(creature, area, zone, mapName, filledMapId, areaId, zoneId);
                    presence.mapName = mapName.empty() ? map->GetMapName() : mapName;
                    presence.area = area.empty() ? zone : area;
                    presence.loreContext = BuildInstanceLoreContext(mapId, presence.mapName,
                        presence.area, m_instanceLore);
                    presence.chance = m_config->bossDialogueRepeatChance;
                    presence.lastSeen = now;
                    presence.nextAutomatic = now + std::chrono::seconds(RandomUInt(
                        m_config->bossDialogueInitialDelayMinimumSeconds,
                        m_config->bossDialogueInitialDelayMaximumSeconds));
                    continue;
                }

                presence.lastSeen = now;
                presence.anchorPlayerGuid = player->GetObjectGuid().GetRawValue();
                if (now < presence.nextAutomatic)
                    continue;

                bool queued = false;
                if (Roll(presence.chance))
                {
                    NpcDisposition const disposition = ClassifyNpcDisposition(creature, player);
                    ActorSnapshot actor = SnapshotCreature(creature, player, disposition,
                        "boss:" + std::string(BossClassificationSourceName(
                            ClassifyCreatureBoss(creature, *m_config).source)), true);
                    queued = QueueDialogue(actor, SnapshotSpeaker(player), ChatScope::Say, "",
                        "boss-automatic", presence.area, RequestPriority::Nearby, false, false,
                        0, nullptr, &presence);
                }

                if (queued)
                {
                    ++presence.automaticLines;
                    uint32_t const decay = m_config->bossDialogueRepeatChanceDecayPercent;
                    presence.chance = std::max<uint32_t>(m_config->bossDialogueRepeatChanceFloor,
                        static_cast<uint32_t>(static_cast<uint64_t>(presence.chance) *
                            (100 - std::min<uint32_t>(100, decay)) / 100));
                }
                presence.nextAutomatic = now + std::chrono::seconds(RandomUInt(
                    m_config->bossDialogueRepeatDelayMinimumSeconds,
                    m_config->bossDialogueRepeatDelayMaximumSeconds));
            }
        }
    }

    void Manager::PruneBossPresences()
    {
        if (!m_config)
            return;
        auto const now = Clock::now();
        for (auto it = m_bossPresences.begin(); it != m_bossPresences.end(); )
        {
            auto const age = now - it->second.lastSeen;
            it = age > std::chrono::seconds(m_config->bossDialoguePresenceResetSeconds)
                ? m_bossPresences.erase(it) : std::next(it);
        }
    }

    void Manager::NoteBossLineDelivered(ChatRequest const& request, std::string const& text)
    {
        if (!m_config || request.bossKey.empty())
            return;
        auto found = m_bossPresences.find(request.bossKey);
        if (found == m_bossPresences.end())
            return;
        BossPresence& presence = found->second;
        presence.recentLines.push_back(text);
        while (presence.recentLines.size() > 5)
            presence.recentLines.pop_front();
        presence.lastSeen = Clock::now();
        if (request.bossDirected && request.speaker.guid)
            presence.directedCooldowns[request.speaker.guid] = Clock::now() +
                std::chrono::seconds(m_config->bossDialogueDirectedReplyCooldownSeconds);
    }

    bool Manager::QueueBossDirectedReply(Player* speaker, std::string const& message)
    {
        if (!m_config || !m_config->bossDialogueEnabled || !speaker)
            return false;

        auto const now = Clock::now();
        uint64_t const playerGuid = speaker->GetObjectGuid().GetRawValue();
        uint32_t const mapId = speaker->GetMapId();
        uint32_t const instanceId = CurrentInstanceId(speaker);

        for (auto& entry : m_bossPresences)
        {
            BossPresence& presence = entry.second;
            if (presence.mapId != mapId || presence.instanceId != instanceId)
                continue;
            Creature* boss = ObjectAccessor::GetCreature(*speaker, ObjectGuid(presence.bossGuid));
            if (!boss || !boss->IsAlive() || boss->IsInCombat())
                continue;
            if (NpcPlayableFactionReaction(boss) >= REP_NEUTRAL)
                continue;
            if (!boss->IsWithinDist(speaker, m_config->bossDialogueMaximumDistance, false))
                continue;
            float const safeDistance = boss->GetAttackDistance(speaker) +
                m_config->bossDialogueAggroMargin;
            if (boss->IsWithinDist(speaker, safeDistance, false))
                continue;
            if (!boss->IsWithinLOSInMap(speaker) ||
                !boss->IsVisibleForOrDetect(speaker, speaker, true))
                continue;

            auto cooldown = presence.directedCooldowns.find(playerGuid);
            if (cooldown != presence.directedCooldowns.end() && cooldown->second > now)
                continue;

            NpcDisposition const disposition = ClassifyNpcDisposition(boss, speaker);
            ActorSnapshot actor = SnapshotCreature(boss, speaker, disposition, "boss-directed", true);
            presence.lastSeen = now;
            presence.anchorPlayerGuid = playerGuid;
            if (QueueDialogue(actor, SnapshotSpeaker(speaker), ChatScope::Say, "",
                    "boss-directed", message, RequestPriority::Direct, false, false,
                    0, nullptr, &presence, true))
                return true;
        }
        return false;
    }

    bool Manager::ProcessPlayerSay(Player* speaker, std::string const& message,
                                   std::string const& /*targetName*/,
                                   std::string const& /*channelName*/)
    {
        if (!m_started || !m_config || !speaker || !m_config->npcReplies)
            return false;

        float const distance = m_config->npcDistance;
        MaNGOS::AllCreaturesInRange check(speaker, distance);
        std::list<Creature*> creatures;
        MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesInRange> searcher(creatures, check);
        Cell::VisitGridObjects(speaker, searcher, distance);

        std::vector<Creature*> candidates;
        std::vector<std::string> names;
        for (Creature* creature : creatures)
        {
            if (!creature || !creature->IsInWorld() || !creature->IsAlive() ||
                !creature->GetCreatureInfo())
                continue;
            if (!creature->IsWithinLOSInMap(speaker))
                continue;
            bool const boss = ClassifyCreatureBoss(creature, *m_config).boss;
            if (boss)
            {
                if (creature->IsInCombat() || NpcPlayableFactionReaction(creature) >= REP_NEUTRAL)
                    continue;
            }
            else if (EvaluateNpcSpeaker(creature, m_config->sayDistance, *m_config) !=
                     NpcEligibilityResult::Eligible)
                continue;
            candidates.push_back(creature);
            names.push_back(creature->GetName());
        }

        // 1. An eligible named NPC. A full-name match beats a unique
        //    first-token match, and an ambiguous name selects nothing.
        NpcNameMatch const match = SelectNamedNpc(names, message);
        if (match.found && match.index < candidates.size())
        {
            Creature* chosen = candidates[match.index];
            if (ClassifyCreatureBoss(chosen, *m_config).boss)
                return QueueBossDirectedReply(speaker, message);
            NpcDisposition const disposition = ClassifyNpcDisposition(chosen, speaker);
            ActorSnapshot actor = SnapshotCreature(chosen, speaker, disposition,
                match.kind == NameMatchKind::FullName ? "named-full" : "named-first");
            if (QueueDialogue(actor, SnapshotSpeaker(speaker), ChatScope::Say, "",
                    "targeted-npc-named", message, RequestPriority::Direct, false, false))
                return true;
        }

        // 2. The explicitly selected target.
        ObjectGuid const selected = speaker->GetSelectionGuid();
        if (selected.IsCreature())
        {
            Creature* selectedNpc = ObjectAccessor::GetCreature(*speaker, selected);
            if (selectedNpc && selectedNpc->IsInWorld() && selectedNpc->IsAlive() &&
                selectedNpc->IsWithinLOSInMap(speaker) &&
                EvaluateNpcSpeaker(selectedNpc, m_config->sayDistance, *m_config) ==
                    NpcEligibilityResult::Eligible &&
                (!m_config->disableRepliesInCombat || !selectedNpc->IsInCombat()))
            {
                NpcDisposition const disposition = ClassifyNpcDisposition(selectedNpc, speaker);
                ActorSnapshot actor = SnapshotCreature(selectedNpc, speaker, disposition, "selected");
                if (QueueDialogue(actor, SnapshotSpeaker(speaker), ChatScope::Say, "",
                        "targeted-npc-selected", message, RequestPriority::Direct, false, false))
                    return true;
            }
        }

        // 3. The last active scene participant, if the scene is still open.
        auto const now = Clock::now();
        for (auto& entry : m_proximityScenes)
        {
            ProximityScene& scene = entry.second;
            if (scene.anchorPlayerGuid != speaker->GetObjectGuid().GetRawValue() ||
                scene.expires <= now || scene.mapId != speaker->GetMapId() ||
                scene.instanceId != CurrentInstanceId(speaker))
                continue;
            for (uint64_t guid : scene.speakers)
            {
                Creature* participant = ObjectAccessor::GetCreature(*speaker, ObjectGuid(guid));
                if (!participant || !participant->IsAlive() || participant->IsInCombat() ||
                    !participant->IsWithinDist(speaker, m_config->sayDistance, false) ||
                    !participant->IsWithinLOSInMap(speaker) ||
                    !EvaluateProximityCreature(participant, *m_config).eligible)
                    continue;
                NpcDisposition const disposition = ClassifyNpcDisposition(participant, speaker);
                ActorSnapshot actor = SnapshotCreature(participant, speaker, disposition,
                    "scene-participant");
                if (QueueDialogue(actor, SnapshotSpeaker(speaker), ChatScope::Say, "",
                        "targeted-npc-scene", message, RequestPriority::Direct, false, false))
                    return true;
            }
            break;
        }

        // 4. Otherwise open a fresh proximity scene around the speaker.
        return QueueProximityScene(speaker, true);
    }

    void Manager::MaybeQueueTargetedNpcObserverComment(Player* speaker, Creature* targetedNpc,
                                                      std::string const& message,
                                                      std::string const& channelName)
    {
        if (!m_started || !m_config || !m_config->enabled || m_paused || !speaker || !targetedNpc)
            return;
        if (!m_config->targetedNpcBotCommentsEnabled || m_config->targetedNpcMaxBotComments == 0)
            return;

        // Roll once for the interaction whether an observer comment should happen.
        if (!Roll(m_config->targetedNpcBotCommentChance))
            return;

        std::vector<Player*> onlinePlayers;
        {
            HashMapHolder<Player>::ReadGuard guard(HashMapHolder<Player>::GetLock());
            for (auto const& entry : sObjectAccessor.GetPlayers())
                if (entry.second)
                    onlinePlayers.push_back(entry.second);
        }

        std::vector<ObserverCandidate> eligibleBots;
        std::unordered_map<uint64_t, Player*> botMap;
        auto const now = Clock::now();
        float const sayDist = m_config->sayDistance;

        for (Player* bot : onlinePlayers)
        {
            if (!bot || bot == speaker || !bot->IsInWorld() || !bot->IsAlive() ||
                !Script_IsAIControlled(bot))
                continue;

            uint64_t const botGuid = bot->GetObjectGuid().GetRawValue();

            if (bot->GetMapId() != speaker->GetMapId() ||
                CurrentInstanceId(bot) != CurrentInstanceId(speaker))
                continue;

            if (m_config->disableRepliesInCombat && bot->IsInCombat())
                continue;

            // Must be within say range of the interaction
            if (!bot->IsWithinDist(speaker, sayDist, false))
                continue;

            // Line of sight to the speaker
            if (!bot->IsWithinLOSInMap(speaker))
                continue;

            // Respect actor cooldown
            auto cd = m_actorCooldowns.find(botGuid);
            if (cd != m_actorCooldowns.end() && cd->second > now)
                continue;

            // Standard real player audience check
            if (!HasNearbyRealPlayer(bot, sayDist))
                continue;

            float const dist = bot->GetDistance(speaker);
            eligibleBots.push_back({ botGuid, bot->GetName(), dist });
            botMap[botGuid] = bot;
        }

        if (eligibleBots.empty())
            return;

        std::vector<ObserverCandidate> chosen = SelectObserverCandidates(
            eligibleBots, m_config->targetedNpcMaxBotComments);

        ActorSnapshot npcSnapshot = SnapshotCreature(targetedNpc, speaker,
            ClassifyNpcDisposition(targetedNpc, speaker), "targeted-npc");
        SpeakerSnapshot speakerSnapshot = SnapshotSpeaker(speaker);

        for (ObserverCandidate const& candidate : chosen)
        {
            Player* bot = botMap[candidate.guid];
            if (!bot)
                continue;

            ActorSnapshot botActor = SnapshotBot(bot);
            QueueDialogue(botActor, speakerSnapshot, ChatScope::Say, channelName,
                "targeted-npc-observer", message, RequestPriority::Nearby,
                false, false, 0, nullptr, nullptr, false, nullptr, &npcSnapshot);

            if (m_config->debug)
            {
                sLog.outDebug("[AzerothVoices] Queued targeted-npc-observer comment for bot %s overhearing %s speaking to %s",
                    botActor.name.c_str(), speaker->GetName(), npcSnapshot.name.c_str());
            }
        }
    }

    void Manager::SetPaused(bool paused)
    {
        m_paused = paused;
    }

    bool Manager::IsPaused() const
    {
        return m_paused;
    }

    StatusSnapshot Manager::GetStatus() const
    {
        StatusSnapshot status;
        status.configurationLoaded = m_config != nullptr;
        status.enabled = m_started && m_config && m_config->enabled;
        status.playerbotsLlmEnabled = m_config ? m_config->playerbotsLlmEnabled : false;
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
        status.sentimentDatabaseAvailable = m_sentimentDatabaseAvailable;
        status.sentiments = m_sentiments.size();
        status.sentimentWritesPending = m_pendingSentimentWrites.size();
        status.addonDatabaseAvailable = m_addonDatabaseAvailable;
        status.groupConversations = m_groupConversations.size();
        status.trackedGroups = m_groupStates.size();
        status.memoryDatabaseAvailable = m_memoryDatabaseAvailable;
        status.memoryPairs = m_memories.size();
        status.memoryWritesPending = m_pendingMemoryWrites.size();
        status.thinkingFallbacks = m_thinkingFallbacks;
        status.proximityScenes = m_proximityScenes.size();
        status.proximityZoneCounters = m_proximityZoneScenes.size();
        status.bossPresences = m_bossPresences.size();
        status.instanceLoreEntries = m_instanceLore.size();
        status.generalPacingWindows = m_generalPacing.Size();
        status.partyPacingWindows = m_partyPacing.Size();
        status.pendingGuildGreetings = m_pendingGuildGreetings.size();
        status.guildSessionHistories = m_guildSessionHistory.size();
        status.generalSpeakersOnCooldown = m_generalSpeakerTracker.Size();
        status.gossipTargetsOnCooldown = m_gossipTargetTracker.Size();
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
            status.sentimentEnabled = m_config->sentimentEnabled;
            status.addonEnabled = m_config->addonEnabled;
            status.proximityEnabled = m_config->proximityEnabled;
            status.bossDialogueEnabled = m_config->bossDialogueEnabled;
            status.groupChatterEnabled = m_config->groupChatterEnabled;
            status.raidChatterEnabled = m_config->raidChatterEnabled;
            status.guildChatterEnabled = m_config->guildChatterEnabled;
            status.generalChatterEnabled = m_config->generalChatterEnabled;
            status.targetedNpcBotCommentsEnabled = m_config->targetedNpcBotCommentsEnabled;
            status.targetedNpcBotCommentChance = m_config->targetedNpcBotCommentChance;
            status.memoryEnabled = m_config->memoryEnabled;
            status.thinkingMode = m_config->thinkingMode;
            status.thinkingProvider = Reasoning::ProviderName(
                Reasoning::DetectProvider(m_config->endpoint));
            status.thinkingCapability = Reasoning::CapabilityName(
                Reasoning::DetectCapability(Reasoning::DetectProvider(m_config->endpoint),
                    m_config->model));
            status.thinkingEffort = m_config->thinkingEffort;
            status.thinkingAutoKinds = m_config->thinkingAutoKinds;
            status.ragEnabled = m_config->ragEnabled;
            status.environmentEnabled = m_config->environmentContextEnabled;
            status.snapshotEnabled = m_config->snapshotEnabled;
            status.apiConfigured = !m_config->endpoint.empty() &&
                (!m_config->model.empty() || !m_config->apiJsonTemplate.empty()) &&
                (!m_config->ResolveApiKey().empty() || IsLocalEndpoint(m_config->endpoint));
        }
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (auto const& queue : m_queues)
                status.queued += queue.size();
        }
        return status;
    }

    bool Manager::IsWorldChannel(std::string const& channelName) const
    {
        if (!m_config)
            return false;
        std::string a = channelName;
        std::string b = m_config->worldChannelName;
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return a == b;
    }
}

