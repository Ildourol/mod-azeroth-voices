#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    // Ordered speaker policy for ordinary proximity Say chatter. The decision
    // function is pure so the precedence below can be tested without a server:
    //   1. universal safety exclusions
    //   2. speaker denylist (wins over everything else)
    //   3. boss exclusion
    //   4. guard / interactive service role
    //   5. allow-list gate
    //   6. humanoid
    //   7. explicit non-humanoid allow-list
    //   8. rejection
    enum class ProximityRejection : uint8_t
    {
        None,
        SafetyExclusion,
        Denied,
        Boss,
        ServiceRole,
        NotAllowed,
        NonHumanoid,
        Count
    };

    struct ProximitySpeakerInput
    {
        bool safetyExcluded = false;
        bool denied = false;
        bool boss = false;
        bool guardOrService = false;
        bool creatureTypeKnown = false;
        bool humanoid = false;
        bool entryAllowed = false;
        bool allowListEmpty = true;
        bool entryAllowedAsNonHumanoid = false;
    };

    struct ProximitySpeakerDecision
    {
        bool eligible = false;
        ProximityRejection reason = ProximityRejection::None;
    };

    ProximitySpeakerDecision EvaluateProximitySpeaker(ProximitySpeakerInput const& input);
    char const* ProximityRejectionName(ProximityRejection reason);

    // Proximity scenes never run in battlegrounds or arenas. Dungeons and
    // raids are optional.
    bool IsProximityMapEligible(bool battleground, bool arena, bool dungeon, bool allowInstances);

    // Zone fatigue: the first `threshold` scenes in a zone keep the configured
    // trigger chance; every later scene decays it by `decayPercent`, floored at
    // one percent so an eligible zone never goes permanently silent.
    uint32_t ApplyZoneFatigue(uint32_t baseChance, uint32_t scenes,
                              uint32_t threshold = 3, uint32_t decayPercent = 20);

    // Zone fatigue decays once per scan in which the zone produced no scene.
    uint32_t DecayZoneScenes(double scenes, uint32_t decayPercent = 20);

    enum class NameMatchKind : uint8_t
    {
        None,
        FullName,
        UniqueFirstName,
        Ambiguous
    };

    struct NpcNameMatch
    {
        NameMatchKind kind = NameMatchKind::None;
        size_t index = 0;
        bool found = false;
    };

    // Full-name matches beat unique first-token matches. Ambiguous first-token
    // matches (two nearby creatures share a first name) never select anything,
    // and a matched candidate is only returned when the whole-word match is
    // unambiguous.
    NpcNameMatch SelectNamedNpc(std::vector<std::string> const& candidateNames,
                                std::string const& message);
}
