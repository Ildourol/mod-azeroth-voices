#include "AzerothVoicesManager.h"

#include "Creature.h"
#include "Item.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptObjects.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace AzerothVoices
{
    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool ToScope(uint32 type, ChatScope& scope)
        {
            switch (type)
            {
                case CHAT_MSG_SAY: scope = ChatScope::Say; return true;
                case CHAT_MSG_YELL: scope = ChatScope::Yell; return true;
                case CHAT_MSG_WHISPER: scope = ChatScope::Whisper; return true;
                case CHAT_MSG_PARTY: scope = ChatScope::Party; return true;
                case CHAT_MSG_RAID:
                case CHAT_MSG_RAID_LEADER:
                case CHAT_MSG_RAID_WARNING: scope = ChatScope::Raid; return true;
                case CHAT_MSG_GUILD: scope = ChatScope::Guild; return true;
                case CHAT_MSG_OFFICER: scope = ChatScope::Officer; return true;
                case CHAT_MSG_CHANNEL: scope = ChatScope::Channel; return true;
                default: return false;
            }
        }

        class AzerothVoicesWorldScript final : public WorldScript
        {
        public:
            AzerothVoicesWorldScript()
                : WorldScript("AzerothVoicesWorldScript", {
                    WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                    WORLDHOOK_ON_UPDATE,
                    WORLDHOOK_ON_STARTUP,
                    WORLDHOOK_ON_SHUTDOWN })
            {
            }

            void OnStartup() override
            {
                Manager::Instance().Start();
            }

            void OnUpdate(uint32 diff) override
            {
                Manager::Instance().Update(diff);
            }

            void OnAfterConfigLoad(bool reload) override
            {
                if (reload)
                    Manager::Instance().Reload();
            }

            void OnShutdown() override
            {
                Manager::Instance().Stop();
            }
        };

        class AzerothVoicesPlayerScript final : public PlayerScript
        {
        public:
            AzerothVoicesPlayerScript()
                : PlayerScript("AzerothVoicesPlayerScript", {
                    PLAYERHOOK_ON_PLAYER_JUST_DIED,
                    PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
                    PLAYERHOOK_ON_PVP_KILL,
                    PLAYERHOOK_ON_CREATURE_KILL,
                    PLAYERHOOK_ON_LEVEL_CHANGED,
                    PLAYERHOOK_ON_LEARN_SPELL,
                    PLAYERHOOK_ON_DUEL_REQUEST,
                    PLAYERHOOK_ON_DUEL_START,
                    PLAYERHOOK_ON_DUEL_END,
                    PLAYERHOOK_ON_LOGIN,
                    PLAYERHOOK_ON_LOOT_ITEM,
                    PLAYERHOOK_ON_CHAT_COMMAND })
            {
            }

            void OnChatCommand(Player* player, uint32 type, std::string const& message,
                               uint32 language, std::string const& target) override
            {
                if (!player || language == LANG_ADDON || type == CHAT_MSG_CHANNEL)
                    return;
                ChatScope scope;
                if (ToScope(type, scope))
                    Manager::Instance().HandleChat(player, scope, message, target);
            }

            void OnPlayerJustDied(Player* player) override
            {
                Manager::Instance().HandleEvent(player, "died");
            }

            void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
            {
                Manager::Instance().HandleEvent(player, "quest_completed", quest ? quest->GetTitle() : "");
            }

            void OnPVPKill(Player* killer, Player* killed) override
            {
                Manager::Instance().HandleEvent(killer, "player_defeated", killed ? killed->GetName() : "");
            }

            void OnCreatureKill(Player* killer, Creature* killed) override
            {
                Manager::Instance().HandleEvent(killer, "creature_defeated", killed ? killed->GetName() : "");
            }

            void OnLevelChanged(Player* player, uint8 oldLevel) override
            {
                if (player && player->GetLevel() > oldLevel)
                    Manager::Instance().HandleEvent(player, "level_up", std::to_string(player->GetLevel()));
            }

            void OnLearnSpell(Player* player, uint32 spellId) override
            {
                Manager::Instance().HandleEvent(player, "spell_learned", std::to_string(spellId));
            }

            void OnDuelRequest(Player* target, Player* challenger) override
            {
                Manager::Instance().HandleEvent(challenger, "duel_requested", target ? target->GetName() : "");
            }

            void OnDuelStart(Player* player1, Player* player2) override
            {
                Manager::Instance().HandleEvent(player1, "duel_started", player2 ? player2->GetName() : "");
            }

            void OnDuelEnd(Player* winner, Player* loser, uint32 /*type*/) override
            {
                Manager::Instance().HandleEvent(winner, "duel_won", loser ? loser->GetName() : "");
            }

            void OnLogin(Player* player) override
            {
                if (player && player->GetGuildId())
                    Manager::Instance().HandleEvent(player, "guild_login");
            }

            void OnLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootGuid*/) override
            {
                std::string detail;
                std::string eventName = "item_looted";
                if (item && item->GetProto())
                {
                    detail = item->GetProto()->Name1 + " x" + std::to_string(count);
                    if (item->GetProto()->Quality >= ITEM_QUALITY_EPIC)
                        eventName = "epic_item";
                    else if (item->GetProto()->Quality >= ITEM_QUALITY_RARE)
                        eventName = "rare_item";
                }
                Manager::Instance().HandleEvent(player, eventName, detail);
            }
        };

        class AzerothVoicesServerScript final : public ServerScript
        {
        public:
            AzerothVoicesServerScript()
                : ServerScript("AzerothVoicesServerScript", { SERVERHOOK_ON_PACKET_HANDLED })
            {
            }

            void OnPacketHandled(WorldSession* session, WorldPacket const& original) override
            {
                if (!session || !session->GetPlayer() || original.GetOpcode() != CMSG_MESSAGECHAT)
                    return;
                try
                {
                    WorldPacket packet(original);
                    packet.rpos(0);
                    uint32 type = 0;
                    uint32 language = 0;
                    packet >> type >> language;
                    if (type != CHAT_MSG_CHANNEL || language == LANG_ADDON)
                        return;
                    std::string channel;
                    std::string message;
                    packet >> channel >> message;
                    if (message.empty())
                        return;

                    StatusSnapshot status = Manager::Instance().GetStatus();
                    ChatScope scope = Lower(channel) == Lower(status.worldChannelName) ? ChatScope::World : ChatScope::Channel;
                    Manager::Instance().HandleChat(session->GetPlayer(), scope, message, "", channel);
                }
                catch (ByteBufferException const&)
                {
                    // Malformed chat was already rejected by the core handler.
                }
            }
        };

        class AzerothVoicesGuildScript final : public GuildScript
        {
        public:
            AzerothVoicesGuildScript() : GuildScript("AzerothVoicesGuildScript") {}

            void OnAddMember(Guild* /*guild*/, Player* player, uint8& /*rank*/) override
            {
                Manager::Instance().HandleEvent(player, "guild_join");
            }

            void OnRemoveMember(Guild* /*guild*/, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
            {
                Manager::Instance().HandleEvent(player, "guild_leave");
            }
        };
    }

    void RegisterAzerothVoicesCommand();

    void RegisterAzerothVoicesScripts()
    {
        new AzerothVoicesWorldScript();
        new AzerothVoicesPlayerScript();
        new AzerothVoicesServerScript();
        new AzerothVoicesGuildScript();
    }
}

void Addmod_azeroth_voicesScripts()
{
    AzerothVoices::RegisterAzerothVoicesScripts();
    AzerothVoices::RegisterAzerothVoicesCommand();
}
