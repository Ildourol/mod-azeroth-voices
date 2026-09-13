#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace AzerothVoices
{
    // Boss and miniboss pre-aggro dialogue. Classification is pure so the
    // curated registry, the allow/deny configuration, and the two dynamic
    // fallbacks can be tested without a running server.
    enum class BossClassificationSource : uint8_t
    {
        None,
        Denied,
        Allowed,
        CuratedRegistry,
        InstanceBindFlag,
        WorldBossRank,
        Count
    };

    struct BossClassification
    {
        bool boss = false;
        BossClassificationSource source = BossClassificationSource::None;
    };

    BossClassification ClassifyBoss(uint32_t entry, uint32_t rank, uint32_t flagsExtra,
                                    bool inDungeonOrRaid, bool allowListHit, bool denyListHit);
    char const* BossClassificationSourceName(BossClassificationSource source);

    bool IsCuratedBossEntry(uint32_t entry);
    size_t CuratedBossEntryCount();

    // Cleans one generated boss line and enforces the configured word and
    // character bounds. Over-long text is cut back to the last word boundary at
    // or below the character cap; a line that still has too few or too many
    // words is rejected rather than spoken.
    bool ValidateBossLine(std::string const& line, uint32_t minimumWords,
                          uint32_t maximumWords, uint32_t maximumCharacters,
                          std::string& cleaned);
}
