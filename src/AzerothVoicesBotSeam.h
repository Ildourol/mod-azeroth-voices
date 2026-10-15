#ifndef AZEROTH_VOICES_BOT_SEAM_H
#define AZEROTH_VOICES_BOT_SEAM_H

#include "Player.h"
#include "WorldSession.h"
#include "ScriptMgr.h"
#include "ObjectAccessor.h"
#include "GameObject.h"
#include "Creature.h"

namespace AzerothVoices
{
    /**
     * Non-intrusive PlayerBot detection helper for TortoiseWoW and TortoiseBots.
     * Replaces legacy core function hacks without editing tortoise-wow or TortoiseBots.
     */
    inline bool Script_IsAIControlled(Player const* player)
    {
        if (!player)
            return false;

        // Check 1: Headless session transport (standard TortoiseBots marker)
        if (player->GetSession() && player->GetSession()->IsHeadless())
            return true;

        // Check 2: Core ScriptMgr managed bot hook (answered by BotPlayerAdapter)
        if (sScriptMgr.IsBotManaged(const_cast<Player*>(player)))
            return true;

        return false;
    }

    // Engine compatibility shims for TortoiseWoW / vMaNGOS
    inline Creature* GetCreatureFromAnchor(WorldObject const& anchor, ObjectGuid guid)
    {
        Unit* u = ObjectAccessor::GetUnit(anchor, guid);
        return u ? u->ToCreature() : nullptr;
    }

    inline bool IsCreatureCritter(Creature const* creature)
    {
        return creature && creature->GetCreatureType() == CREATURE_TYPE_CRITTER;
    }

    inline bool IsGameObjectSpawned(GameObject const* go)
    {
        return go && go->isSpawned();
    }

    inline uint32_t GetPlayerQuestSlotQuestId(Player const* player, uint16_t slot)
    {
        return player ? player->GetUInt32Value(PLAYER_QUEST_LOG_1_1 + slot * MAX_QUEST_OFFSET + QUEST_ID_OFFSET) : 0;
    }

    inline void HoldBotMovementAndFace(Player* bot, WorldObject const* target)
    {
        if (!bot || !target)
            return;
        bot->SetFacingToObject(const_cast<WorldObject*>(target));
        bot->StopMoving();
    }

    inline void HoldForReply(Player* bot, WorldObject const* target, uint32_t /*durationMs*/ = 0)
    {
        HoldBotMovementAndFace(bot, target);
    }

    inline void BotWhisperPlayer(Player* bot, Player* receiver, std::string const& text)
    {
        if (!bot || !receiver)
            return;
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
        receiver->GetSession()->SendPacket(&data);
        data.clear();
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER_INFORM, text.c_str(), LANG_UNIVERSAL, receiver->GetChatTag(), receiver->GetObjectGuid(), receiver->GetName());
        if (bot->GetSession())
            bot->GetSession()->SendPacket(&data);
    }
}

#endif // AZEROTH_VOICES_BOT_SEAM_H
