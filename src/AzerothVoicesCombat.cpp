#include "AzerothVoicesManager.h"

#include "Creature.h"
#include "Database/DBCStores.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptObjects.h"
#include "SharedDefines.h"

#include <chrono>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace AzerothVoices
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::unordered_set<uint64_t> s_activePlayerCombats;
        std::unordered_map<uint64_t, Clock::time_point> s_combatStartCooldowns;

        std::mt19937& RandomEngine()
        {
            static thread_local std::mt19937 engine(std::random_device{}());
            return engine;
        }

        bool Roll(uint32_t chance)
        {
            if (!chance)
                return false;
            if (chance >= 100)
                return true;
            return std::uniform_int_distribution<uint32_t>(1, 100)(RandomEngine()) <= chance;
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

        std::string DispositionName(Creature const* creature, Player const* player)
        {
            ReputationRank const reaction = creature && player
                ? creature->GetReactionTo(player) : REP_NEUTRAL;
            if (reaction >= REP_FRIENDLY)
                return "friendly";
            if (reaction == REP_NEUTRAL)
                return "neutral";
            return "hostile";
        }

        void FillLocation(WorldObject const* object, ActorSnapshot& result)
        {
            if (!object)
                return;

            result.mapId = object->GetMapId();
            result.areaId = object->GetAreaId();
            result.zoneId = object->GetZoneId();
            if (Map const* map = object->FindMap())
                result.map = map->GetMapName();
            if (AreaEntry const* area = AreaEntry::GetById(result.areaId))
                result.area = area->Name ? area->Name : "";
            if (AreaEntry const* zone = AreaEntry::GetById(result.zoneId))
                result.zone = zone->Name ? zone->Name : "";
            if (result.zone.empty())
                result.zone = result.area;
        }

        ActorSnapshot SnapshotCombatCreature(Creature const* creature, Player const* player)
        {
            ActorSnapshot result;
            result.kind = ActorKind::Creature;
            if (!creature)
                return result;

            result.guid = creature->GetObjectGuid().GetRawValue();
            result.anchorPlayerGuid = player ? player->GetObjectGuid().GetRawValue() : 0;
            result.name = creature->GetName();
            result.race = "NPC";
            result.className = "NPC";
            result.gender = GenderName(creature->GetGender());
            result.disposition = DispositionName(creature, player);
            result.faction = result.disposition + " NPC";
            result.groupStatus = "in combat";
            result.level = creature->GetLevel();
            result.inCombat = creature->IsInCombat();
            FillLocation(creature, result);
            return result;
        }

        SpeakerSnapshot SnapshotCombatPlayer(Player const* player)
        {
            SpeakerSnapshot result;
            if (!player)
                return result;

            // The creature's anchorPlayerGuid retains the live attacker identity for
            // world-thread re-resolution. Keeping this snapshot GUID at zero avoids
            // the normal NPC-conversation 10-yard participant rule; combat-openers
            // instead use the configured SayDistance checked before queueing.
            result.guid = 0;
            result.name = player->GetName();
            result.race = RaceName(player->GetRace());
            result.className = ClassName(player->GetClass());
            result.gender = GenderName(player->GetGender());
            result.faction = TeamName(player->GetTeam());
            result.groupStatus = player->GetGroup() ? "in a group" : "solo";
            result.level = player->GetLevel();
            result.guildId = player->GetGuildId();
            result.isBot = false;
            return result;
        }

        void ResetCombatState()
        {
            s_activePlayerCombats.clear();
            s_combatStartCooldowns.clear();
        }

        class AzerothVoicesCombatPlayerScript final : public PlayerScript
        {
        public:
            AzerothVoicesCombatPlayerScript()
                : PlayerScript("AzerothVoicesCombatPlayerScript", {
                    PLAYERHOOK_ON_UPDATE,
                    PLAYERHOOK_ON_LOGOUT })
            {
            }

            void OnUpdate(Player* player, uint32 /*diff*/) override
            {
                if (!player || !player->IsInWorld() || !player->GetSession() ||
                    Script_IsAIControlled(player))
                    return;

                uint64_t const playerGuid = player->GetObjectGuid().GetRawValue();
                if (!player->IsInCombat())
                {
                    s_activePlayerCombats.erase(playerGuid);
                    return;
                }

                // Latch the entire combat before inspecting the victim. This is
                // intentional: body aggro or PvP cannot later become a qualifying
                // creature opener merely because the player attacks something else.
                if (!s_activePlayerCombats.insert(playerGuid).second)
                    return;

                Unit* victim = player->GetVictim();
                if (!victim || victim->GetTypeId() != TYPEID_UNIT)
                    return;

                Manager::Instance().HandleCombatStart(player, static_cast<Creature*>(victim));
            }

            void OnLogout(Player* player) override
            {
                if (!player)
                    return;
                uint64_t const playerGuid = player->GetObjectGuid().GetRawValue();
                s_activePlayerCombats.erase(playerGuid);
                s_combatStartCooldowns.erase(playerGuid);
            }
        };

        class AzerothVoicesCombatWorldScript final : public WorldScript
        {
        public:
            AzerothVoicesCombatWorldScript()
                : WorldScript("AzerothVoicesCombatWorldScript", { WORLDHOOK_ON_SHUTDOWN })
            {
            }

            void OnShutdown() override
            {
                ResetCombatState();
            }
        };
    }

    void Manager::HandleCombatStart(Player* player, Creature* creature)
    {
        if (!m_started || m_stopping || m_paused || !m_config || !m_config->enabled ||
            !m_config->npcCombatStartEnabled || !m_config->npcReplies || !m_config->sayReplies ||
            !player || !creature || !player->IsInWorld() || !creature->IsInWorld() ||
            !player->GetSession() || Script_IsAIControlled(player) || !creature->IsAlive())
            return;

        // The feature represents the player's opening attack, not ambient aggro.
        if (!player->IsInCombat() || player->GetVictim() != creature)
            return;

        if (player->GetMapId() != creature->GetMapId() ||
            !player->IsWithinDist(creature, m_config->sayDistance, false))
            return;

        uint64_t const playerGuid = player->GetObjectGuid().GetRawValue();
        auto const now = Clock::now();
        auto const cooldown = s_combatStartCooldowns.find(playerGuid);
        if (cooldown != s_combatStartCooldowns.end() && cooldown->second > now)
            return;

        if (!Roll(m_config->npcCombatStartChance))
            return;

        ActorSnapshot const actor = SnapshotCombatCreature(creature, player);
        SpeakerSnapshot const speaker = SnapshotCombatPlayer(player);
        std::string const instruction =
            std::string("The real player ") + player->GetName() +
            " has just initiated a fresh combat by attacking you. React with one short, random, "
            "in-character spoken line about being attacked. You may threaten, protest, taunt, warn, "
            "or challenge the attacker as appropriate for this creature. Do not narrate actions.";

        if (QueueDialogue(actor, speaker, ChatScope::Say, "", "event:combat_started",
                          instruction, RequestPriority::Direct, false, false))
        {
            s_combatStartCooldowns[playerGuid] =
                now + std::chrono::seconds(m_config->npcCombatStartCooldownSeconds);
        }
    }

    void RegisterAzerothVoicesCombatScripts()
    {
        new AzerothVoicesCombatPlayerScript();
        new AzerothVoicesCombatWorldScript();
    }
}
