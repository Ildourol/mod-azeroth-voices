#include "AzerothVoicesBossDialogue.h"

#include <algorithm>
#include <iterator>

namespace AzerothVoices
{
    namespace
    {
        // Curated 1.12 boss registry.
        //
        // Provenance: the union of the audited DC_BOSS_ENTRIES_1121 curated
        // Kith boss list (classic instances plus Molten Core and Onyxia) and
        // the DC_BOSS_ORDER_1121 door bosses, exactly as shipped by the
        // tortoise-wow-extended checkout at
        // modules/mod-dungeon-clear/src/Ai/Dungeon/DungeonClear/Data/DcBossEntries1121.h.
        // Those entries also cover Turtle-specific bosses that exist only in
        // this world database, which is why the numbers were derived from the
        // checkout rather than from a general expansion list.
        //
        // Creature rank cannot stand in for this table: Deadmines has VanCleef
        // and his trash both at rank 1. The world-boss rank and instance-bind
        // flags are separate dynamic fallbacks evaluated in ClassifyBoss.
        //
        // The array is sorted so lookups stay a binary search; keep it sorted
        // when adding entries.
        constexpr uint32_t CuratedBossEntries[] = {
            639, 642, 644, 645, 646, 647, 1663, 1666,
            1696, 1716, 1717, 1763, 1853, 2748, 3653, 3654,
            3669, 3670, 3671, 3673, 3674, 3886, 3887, 3914,
            3927, 3974, 3975, 4274, 4275, 4278, 4279, 4421,
            4424, 4543, 4829, 4830, 4831, 4832, 4842, 4854,
            4887, 5709, 5710, 5712, 5715, 5719, 5720, 5721,
            5722, 5775, 6228, 6229, 6235, 6243, 6487, 6488,
            6910, 7023, 7206, 7228, 7267, 7271, 7291, 7355,
            7356, 7357, 7358, 7604, 7800, 8127, 8443, 8580,
            8983, 9016, 9017, 9024, 9030, 9033, 9156, 9196,
            9218, 9236, 9237, 9568, 9816, 9938, 10184, 10220,
            10264, 10339, 10363, 10429, 10430, 10432, 10433, 10435,
            10437, 10440, 10504, 10507, 10508, 10516, 10558, 10584,
            10596, 10811, 10812, 10813, 10901, 10997, 11143, 11488,
            11489, 11490, 11492, 11496, 11502, 11517, 11518, 11519,
            11520, 11622, 11982, 12018, 12056, 12057, 12098, 12118,
            12119, 12129, 12201, 12203, 12236, 12237, 12258, 12259,
            12264, 13280, 13282, 13601, 14321, 14323, 14325, 14326,
            14327, 14354, 40068, 61961, 61963, 61965, 61968, 61969,
            62037, 62038, 62056, 62057, 62067, 62069, 62070, 62071,
            62072, 62530, 63129, 63130, 63131, 63132, 63133, 2000092
        };
    }

    bool IsCuratedBossEntry(uint32_t entry)
    {
        return entry != 0 &&
            std::binary_search(std::begin(CuratedBossEntries), std::end(CuratedBossEntries), entry);
    }

    size_t CuratedBossEntryCount()
    {
        return sizeof(CuratedBossEntries) / sizeof(CuratedBossEntries[0]);
    }
}
