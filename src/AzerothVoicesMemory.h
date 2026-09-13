#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    // Deterministic per-bot/player memory ledger. Summaries are always written
    // by C++ from verified facts: no provider call and no player-written text.
    enum class MemoryType : uint8_t
    {
        FirstMet,
        PartyMember,
        QuestCompleted,
        DungeonCompleted,
        LevelUp,
        BossKill,
        PvpKill,
        Wipe,
        Count
    };

    char const* MemoryTypeName(MemoryType type);
    bool ParseMemoryType(std::string const& name, MemoryType& type);
    uint32_t MemoryTypeImportance(MemoryType type);

    struct MemoryRecord
    {
        uint64_t id = 0;
        uint64_t botGuid = 0;
        uint64_t playerGuid = 0;
        MemoryType type = MemoryType::FirstMet;
        std::string summary;
        uint32_t importance = 0;
        uint64_t createdUnix = 0;
        uint64_t updatedUnix = 0;
    };

    struct MemoryFacts
    {
        std::string playerName;
        std::string zone;
        std::string instance;
        std::string quest;
        std::string boss;
        std::string victim;
        uint32_t level = 0;
    };

    // Fixed-template summary, cleaned and bounded to <= 180 characters.
    // Returns false when the facts needed for this type are missing.
    bool BuildMemorySummary(MemoryType type, MemoryFacts const& facts, std::string& summary);

    // Importance first, then newest. Bounded by item count and total characters.
    std::vector<MemoryRecord const*> SelectMemoriesForPrompt(
        std::vector<MemoryRecord> const& records, size_t maximumItems,
        size_t maximumCharacters);

    std::string BuildMemoryPromptBlock(std::string const& playerName,
                                       std::vector<MemoryRecord const*> const& selected,
                                       size_t maximumCharacters);

    // Keeps the newest `maximumPerPair` records and reports how many must be
    // deleted from SQL.
    size_t PruneMemoryRecords(std::vector<MemoryRecord>& records, size_t maximumPerPair);
}
