#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    enum class ActorKind : uint8_t
    {
        PlayerBot,
        Creature
    };

    enum class ChatScope : uint8_t
    {
        Say,
        Yell,
        Whisper,
        Party,
        Raid,
        Guild,
        Officer,
        Channel,
        World
    };

    enum class RequestPriority : uint8_t
    {
        Ambient = 0,
        Nearby = 1,
        Group = 2,
        Direct = 3
    };

    enum class RequestKind : uint8_t
    {
        Dialogue,
        PersonalityGeneration
    };

    struct BotPersonality
    {
        uint64_t characterGuid = 0;
        std::string botName;
        std::vector<std::string> traits;
        std::string tone;
        std::string background;
        uint32_t backgroundMode = 0;
        uint32_t generationVersion = 0;
        uint64_t createdUnix = 0;
        uint64_t updatedUnix = 0;
    };

    struct ActorSnapshot
    {
        ActorKind kind = ActorKind::PlayerBot;
        uint64_t guid = 0;
        uint64_t anchorPlayerGuid = 0;
        std::string name;
        std::string race;
        std::string className;
        std::string gender;
        std::string faction;
        std::string disposition;
        std::string guild;
        std::string groupStatus;
        std::string area;
        std::string zone;
        std::string map;
        std::string talentBuild;
        uint32_t level = 0;
        uint32_t mapId = 0;
        uint32_t areaId = 0;
        uint32_t zoneId = 0;
        bool inCombat = false;
    };

    struct SpeakerSnapshot
    {
        uint64_t guid = 0;
        std::string name;
        std::string race;
        std::string className;
        std::string gender;
        std::string faction;
        std::string guild;
        std::string groupStatus;
        uint32_t level = 0;
        uint32_t groupId = 0;
        uint32_t guildId = 0;
        bool isBot = false;
    };

    struct HistoryTurn
    {
        std::string speakerMessage;
        std::string actorReply;
        std::chrono::steady_clock::time_point created;
        uint64_t createdUnix = 0;
    };

    struct RecentChatLine
    {
        uint64_t speakerGuid = 0;
        std::string speakerName;
        std::string message;
        std::chrono::steady_clock::time_point created;
    };

    struct SnapshotRecord
    {
        std::string text;
        std::chrono::steady_clock::time_point created;
        uint64_t createdUnix = 0;
    };

    struct ChatRequest
    {
        uint64_t id = 0;
        RequestKind kind = RequestKind::Dialogue;
        RequestPriority priority = RequestPriority::Ambient;
        ActorSnapshot actor;
        SpeakerSnapshot speaker;
        BotPersonality personality;
        ChatScope scope = ChatScope::Say;
        std::string channelName;
        std::string trigger;
        std::string incomingMessage;
        std::string systemPrompt;
        std::string userPrompt;
        std::string context;
        std::string currentSnapshot;
        std::string personalityBlock;
        std::string historyKey;
        std::string scopeKey;
        uint32_t maxTokensOverride = 0;
        bool personalityGenerationNeeded = false;
        bool ambient = false;
        bool allowFollowup = false;
        uint32_t conversationDepth = 1;
        std::chrono::steady_clock::time_point created;
        std::chrono::steady_clock::time_point expires;
    };

    struct ChatCompletion
    {
        ChatRequest request;
        bool success = false;
        int httpStatus = 0;
        std::string responseText;
        std::string rawResponse;
        std::string error;
        uint32_t elapsedMilliseconds = 0;
        uint32_t httpAttemptCount = 0;
    };

    struct ScheduledLine
    {
        ChatRequest request;
        std::string text;
        std::chrono::steady_clock::time_point due;
        bool firstLine = false;
    };
}
