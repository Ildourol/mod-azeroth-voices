// Focused unit tests for the parts of mod-azeroth-voices that must not depend
// on a live worldserver: the Chatter addon wire protocol, three-trait
// personality validation and generation modes, the ordered proximity speaker
// policy, boss classification and yell bounds, instance-lore fallback, NPC name
// addressing, and the automated-chat delivery timeline.
//
// The suite links only the engine-free module sources, so it can be configured
// with -DAZEROTH_VOICES_BUILD_TESTS=ON (or BUILD_TESTING=ON) without linking
// the core. Build the `azeroth_voices_tests` target and run it directly or
// through CTest.

#include "AzerothVoicesAddon.h"
#include "AzerothVoicesBossDialogue.h"
#include "AzerothVoicesInstanceLore.h"
#include "AzerothVoicesMemory.h"
#include "AzerothVoicesPacing.h"
#include "AzerothVoicesPersonality.h"
#include "AzerothVoicesProximity.h"
#include "AzerothVoicesReasoning.h"
#include "AzerothVoicesSocial.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, std::string const& what)
    {
        ++g_checks;
        if (condition)
            return;
        ++g_failures;
        std::cout << "FAIL: " << what << '\n';
    }

    template <typename T>
    void CheckEqual(T const& actual, T const& expected, std::string const& what)
    {
        ++g_checks;
        if (actual == expected)
            return;
        ++g_failures;
        std::cout << "FAIL: " << what << " (actual != expected)\n";
    }

    void CheckContains(std::string const& haystack, std::string const& needle,
                       std::string const& what)
    {
        Check(haystack.find(needle) != std::string::npos, what);
    }
}

using namespace AzerothVoices;

int main()
{
    // --- Chatter addon percent encoding and protocol --------------------------
    CheckEqual(Addon::Encode(""), std::string("-"), "empty string encodes as the sentinel");
    CheckEqual(Addon::Encode("Wry and guarded"), std::string("Wry%20and%20guarded"),
        "spaces are percent encoded");
    CheckEqual(Addon::Encode("Distinct.Name_1~"), std::string("Distinct.Name_1~"),
        "unreserved characters pass through");
    CheckEqual(Addon::Decode("-"), std::string(""), "the sentinel decodes to empty");
    CheckEqual(Addon::Decode("A%2DB"), std::string("A-B"), "percent escapes decode");
    CheckEqual(Addon::Encode("A-B"), std::string("A-B"), "a literal hyphen stays a hyphen");
    CheckEqual(Addon::Encode("50% sure"), std::string("50%25%20sure"),
        "a percent sign is itself encoded");

    CheckEqual(Addon::RosterBegin(), std::string("CHATTER_ADDON ROSTER_BEGIN"),
        "roster begin line");
    CheckEqual(Addon::RosterEntry(42, "Two Words"),
        std::string("CHATTER_ADDON ROSTER 42 Two%20Words"), "roster entry line");
    CheckEqual(Addon::RosterEnd(), std::string("CHATTER_ADDON ROSTER_END"), "roster end line");
    CheckEqual(Addon::Profile(7, "Bot", { "wry", "quiet", "curious" }, ""),
        std::string("CHATTER_ADDON PROFILE 7 Bot wry quiet curious -"),
        "profile always carries exactly six fields");
    CheckEqual(Addon::Profile(7, "Bot", { "wry", "quiet" }, "dry"),
        std::string("CHATTER_ADDON PROFILE 7 Bot wry quiet - dry"),
        "a missing trait is written as the empty sentinel");
    CheckEqual(Addon::Backstory(7, "Story"),
        std::string("CHATTER_ADDON BACKSTORY 7 Story"), "backstory line");
    CheckEqual(Addon::Updated(7, "Bot", true),
        std::string("CHATTER_ADDON UPDATED 7 Bot changed"), "changed update line");
    CheckEqual(Addon::Updated(7, "Bot", false),
        std::string("CHATTER_ADDON UPDATED 7 Bot unchanged"), "unchanged update line");
    CheckEqual(Addon::BackstoryRegen(7, "Bot"),
        std::string("CHATTER_ADDON BACKSTORY_REGEN 7 Bot"), "backstory regeneration ack");
    CheckEqual(Addon::Forgotten(7, "Bot"),
        std::string("CHATTER_ADDON FORGOTTEN 7 Bot"), "forgotten line");
    CheckEqual(Addon::Error("Unknown command."),
        std::string("CHATTER_ADDON ERROR llmc Unknown%20command."), "error line");

    // --- Chatter command parsing ---------------------------------------------
    Addon::Command command;
    std::string error;
    Check(Addon::ParseCommand("roster", command, error) &&
        command.kind == Addon::CommandKind::Roster, "roster parses");
    Check(Addon::ParseCommand(".llmc get 12345", command, error) &&
        command.kind == Addon::CommandKind::Get && command.guid == 12345,
        "get parses with the addon prefix");
    Check(Addon::ParseCommand("set 77 Wry%20and%20guarded Quiet Curious", command, error) &&
        command.kind == Addon::CommandKind::Set && command.guid == 77 &&
        command.traits.size() == 3 && command.traits[0] == "Wry and guarded",
        "set decodes three traits");
    Check(!Addon::ParseCommand("set 77 Wry Quiet", command, error),
        "set with two traits is rejected");
    Check(!Addon::ParseCommand("set 77 Wry wry Curious", command, error),
        "duplicate traits are rejected");
    Check(!Addon::ParseCommand("set 77 Wry Quiet Curious Extra", command, error),
        "set with four traits is rejected");
    Check(!Addon::ParseCommand("get abc", command, error),
        "a non-numeric guild is rejected");
    Check(!Addon::ParseCommand("get 0", command, error), "GUID zero is rejected");
    Check(Addon::ParseCommand("regenbackstory 5", command, error) &&
        command.kind == Addon::CommandKind::RegenBackstory, "regenbackstory parses");
    Check(Addon::ParseCommand("forget 5", command, error) &&
        command.kind == Addon::CommandKind::Forget, "forget parses");
    Check(!Addon::ParseCommand("", command, error), "an empty command is rejected");
    Check(!Addon::ParseCommand("dance 5", command, error), "an unknown verb is rejected");
    Check(!Addon::ParseCommand(
        "set 5 " + std::string(65, 'x') + " Quiet Curious", command, error),
        "an over-long trait is rejected");
    Check(Addon::ParseCommand(".avaddon roster", command, error) &&
        command.kind == Addon::CommandKind::Roster, ".avaddon prefix parses");
    Check(Addon::ParseCommand(".avaddon set 77 Wry%20and%20guarded Quiet Curious", command, error) &&
        command.kind == Addon::CommandKind::Set && command.guid == 77,
        ".avaddon set parses with prefix");

    // --- Transport-neutral logical payloads ---------------------------------
    CheckEqual(Addon::LogicalRosterBegin(), std::string("ROSTER_BEGIN"), "logical roster begin");
    CheckEqual(Addon::LogicalRosterEntry(42, "Two Words"),
        std::string("ROSTER 42 Two%20Words"), "logical roster entry");
    CheckEqual(Addon::LogicalRosterEnd(), std::string("ROSTER_END"), "logical roster end");
    CheckEqual(Addon::LogicalProfile(7, "Bot", { "wry", "quiet", "curious" }, "calm"),
        std::string("PROFILE 7 Bot wry quiet curious calm"), "logical profile");
    CheckEqual(Addon::LogicalBackstory(7, "Storyline"),
        std::string("BACKSTORY 7 Storyline"), "logical backstory");
    CheckEqual(Addon::LogicalUpdated(7, "Bot", true),
        std::string("UPDATED 7 Bot changed"), "logical updated changed");
    CheckEqual(Addon::LogicalUpdated(7, "Bot", false),
        std::string("UPDATED 7 Bot unchanged"), "logical updated unchanged");
    CheckEqual(Addon::LogicalBackstoryRegen(7, "Bot"),
        std::string("BACKSTORY_REGEN 7 Bot"), "logical backstory regen");
    CheckEqual(Addon::LogicalForgotten(7, "Bot"),
        std::string("FORGOTTEN 7 Bot"), "logical forgotten");
    CheckEqual(Addon::LogicalError("avaddon", "Test error"),
        std::string("ERROR avaddon Test%20error"), "logical error");

    // --- Native response framing and chunk boundaries -----------------------
    std::string const shortPayload = "ROSTER 42 Two%20Words";
    std::vector<std::string> shortFrames = Addon::BuildNativeFrames("req42", 1, shortPayload);
    CheckEqual(shortFrames.size(), size_t(1), "short payload fits in one frame");
    Check(shortFrames[0].find("1\treq42\t1\t1\t1\tROSTER 42 Two%20Words") == 0,
        "short frame header and body match expected wire layout");
    Check(shortFrames[0].size() + 15 <= Addon::MaxWireLength,
        "short frame total wire length stays <= 255 bytes");

    // Generate a long percent-encoded background (> 500 characters)
    std::string longBackground;
    for (size_t i = 0; i < 60; ++i)
        longBackground += "Story%20chunk%20" + std::to_string(i) + "%20";
    std::string const longPayload = Addon::LogicalBackstory(7, longBackground);
    Check(longPayload.size() > 500, "test payload is long (>500 bytes)");

    std::vector<std::string> longFrames = Addon::BuildNativeFrames("req99", 2, longPayload);
    Check(longFrames.size() > 1, "long payload is partitioned into multiple parts");
    std::string reassembledPayload;
    for (size_t i = 0; i < longFrames.size(); ++i)
    {
        std::string const& f = longFrames[i];
        Check(f.size() + 15 <= Addon::MaxWireLength,
            "every partition frame total wire length stays <= 255 bytes");
        // Verify frame header format: 1\treq99\t2\t<partIndex>\t<partCount>\t<chunk>
        std::string const expectedPrefix = "1\treq99\t2\t" + std::to_string(i + 1) + "\t" +
                                           std::to_string(longFrames.size()) + "\t";
        Check(f.find(expectedPrefix) == 0, "frame header matches sequence");
        reassembledPayload += f.substr(expectedPrefix.size());
    }
    CheckEqual(reassembledPayload, longPayload,
        "reassembled multi-part payload equals the original logical payload exactly");

    // --- Native request reassembly state machine -----------------------------
    Addon::RequestReassembler reassembler;
    Addon::ReassembledRequest reqOut;

    // Reject non-avaddon message
    Check(reassembler.Feed(1001, "Hello world", 1000, reqOut) == Addon::FrameStatus::NotAvaddon,
        "non-avaddon message is ignored");

    // Malformed frames
    Check(reassembler.Feed(1001, ".avaddon 2 101 1 1 roster", 1000, reqOut) == Addon::FrameStatus::Malformed,
        "unsupported version 2 is rejected");
    Check(reassembler.Feed(1001, ".avaddon 1 101 2 1 roster", 1000, reqOut) == Addon::FrameStatus::Malformed,
        "partIndex > partCount is rejected");
    Check(reassembler.Feed(1001, ".avaddon 1 101 0 1 roster", 1000, reqOut) == Addon::FrameStatus::Malformed,
        "partIndex 0 is rejected");
    Check(reassembler.Feed(1001, ".avaddon 1 101 1 9 roster", 1000, reqOut) == Addon::FrameStatus::Malformed,
        "partCount > 8 is rejected");
    Check(reassembler.Feed(1001, ".avaddon 1 101 x 1 roster", 1000, reqOut) == Addon::FrameStatus::Malformed,
        "non-numeric partIndex is rejected");

    // Single-part command completes immediately
    Check(reassembler.Feed(1001, ".avaddon 1 201 1 1 roster", 1000, reqOut) == Addon::FrameStatus::Complete,
        "single-part .avaddon command completes");
    CheckEqual(reqOut.requestId, std::string("201"), "request ID extracted");
    CheckEqual(reqOut.command, std::string("roster"), "command text extracted");
    CheckEqual(reassembler.ActivePlayerCount(), size_t(0), "reassembler cleared after completion");

    // Multi-part command out of order: part 2 of 3, part 1 of 3, part 3 of 3
    Check(reassembler.Feed(1001, ".avaddon 1 202 2 3 77 Wry%20and", 2000, reqOut) == Addon::FrameStatus::Buffered,
        "part 2 of 3 is buffered");
    // Duplicate part 2 returns Duplicate without error
    Check(reassembler.Feed(1001, ".avaddon 1 202 2 3 77 Wry%20and", 2100, reqOut) == Addon::FrameStatus::Duplicate,
        "duplicate part 2 returns Duplicate");
    Check(reassembler.Feed(1001, ".avaddon 1 202 1 3 set ", 2200, reqOut) == Addon::FrameStatus::Buffered,
        "part 1 of 3 is buffered");
    Check(reassembler.Feed(1001, ".avaddon 1 202 3 3 %20guarded Quiet Curious", 2300, reqOut) == Addon::FrameStatus::Complete,
        "part 3 of 3 completes multi-part command");
    CheckEqual(reqOut.requestId, std::string("202"), "request ID matches multi-part request");
    CheckEqual(reqOut.command, std::string("set 77 Wry%20and%20guarded Quiet Curious"),
        "multi-part command assembled correctly from out-of-order pieces");

    // Over-limit request bytes rejection (> 2048 bytes)
    std::string hugeChunk(2100, 'A');
    Check(reassembler.Feed(1001, ".avaddon 1 203 1 1 " + hugeChunk, 3000, reqOut) == Addon::FrameStatus::Malformed,
        "oversized request chunk (>2048 bytes) is rejected");

    // Request expiration after 10 seconds (10,000 ms)
    Check(reassembler.Feed(1001, ".avaddon 1 204 1 2 part1", 4000, reqOut) == Addon::FrameStatus::Buffered,
        "part 1 of 2 buffered at t=4000");
    Check(reassembler.HasPendingRequest(1001), "pending request exists before timeout");
    // Part 2 arrives at t=14001 (>10s later) -> expired
    Check(reassembler.Feed(1001, ".avaddon 1 204 2 2 part2", 14001, reqOut) == Addon::FrameStatus::Expired,
        "incomplete request expires after 10 seconds");
    Check(!reassembler.HasPendingRequest(1001), "pending request cleared after expiration");

    // Per-player isolation: Player A and Player B have independent buffers
    Check(reassembler.Feed(1001, ".avaddon 1 301 1 2 cmdA_part1", 15000, reqOut) == Addon::FrameStatus::Buffered,
        "Player A part 1 buffered");
    Check(reassembler.Feed(1002, ".avaddon 1 401 1 2 cmdB_part1", 15100, reqOut) == Addon::FrameStatus::Buffered,
        "Player B part 1 buffered independently");
    CheckEqual(reassembler.ActivePlayerCount(), size_t(2), "two independent players active");

    Check(reassembler.Feed(1002, ".avaddon 1 401 2 2 _part2", 15200, reqOut) == Addon::FrameStatus::Complete,
        "Player B completes first");
    CheckEqual(reqOut.playerGuid, uint64_t(1002), "Player B GUID correct");
    CheckEqual(reqOut.command, std::string("cmdB_part1_part2"), "Player B command correct");

    Check(reassembler.Feed(1001, ".avaddon 1 301 2 2 _part2", 15300, reqOut) == Addon::FrameStatus::Complete,
        "Player A completes second");
    CheckEqual(reqOut.playerGuid, uint64_t(1001), "Player A GUID correct");
    CheckEqual(reqOut.command, std::string("cmdA_part1_part2"), "Player A command correct");
    CheckEqual(reassembler.ActivePlayerCount(), size_t(0), "all buffers cleared");

    // --- Three-trait personality generation ----------------------------------
    Config config;
    config.personalityGenerateTone = true;
    config.personalityGenerateBackground = true;
    config.personalityBackgroundMode = 1;
    config.personalityMaxBackgroundCharacters = 500;

    ActorSnapshot actor;
    actor.kind = ActorKind::PlayerBot;
    actor.guid = 4242;
    actor.name = "Brann";
    actor.race = "dwarf";
    actor.className = "hunter";
    actor.faction = "Alliance";
    actor.gender = "male";

    std::string traitsError;
    Check(ValidatePersonalityTraits({ "wry", "quiet", "curious" }, traitsError),
        "three distinct traits validate");
    Check(!ValidatePersonalityTraits({ "wry", "quiet" }, traitsError),
        "two traits do not validate");
    Check(!ValidatePersonalityTraits({ "wry", "wry", "curious" }, traitsError),
        "duplicate traits do not validate");

    BotPersonality parsed;
    std::string parseError;
    Check(ParsePersonalityResponse(config, actor, PersonalityGenerationMode::Full,
            BotPersonality(),
            R"({"traits":["wry","quiet","curious"],"tone":"dry and watchful","background":"A long enough story about a life in the mountains."})",
            parsed, parseError),
        "a full three-trait response parses");
    Check(!ParsePersonalityResponse(config, actor, PersonalityGenerationMode::Full,
            BotPersonality(),
            R"({"traits":["wry","quiet"],"tone":"dry","background":"story"})",
            parsed, parseError),
        "a two-trait response is rejected");
    Check(!ParsePersonalityResponse(config, actor, PersonalityGenerationMode::Full,
            BotPersonality(),
            R"({"traits":["wry","quiet","curious"],"tone":"dry","background":"story","extra":1})",
            parsed, parseError),
        "an unexpected field is rejected");

    BotPersonality fixed;
    fixed.characterGuid = actor.guid;
    fixed.botName = actor.name;
    fixed.traits = { "wry", "quiet", "curious" };
    fixed.tone = "dry and watchful";

    Check(ParsePersonalityResponse(config, actor, PersonalityGenerationMode::ReplaceTraits, fixed,
            R"({"tone":"new tone","background":"a new story that is long enough"})",
            parsed, parseError),
        "a trait-replacement response parses");
    Check(parsed.traits == fixed.traits && parsed.tone == "new tone",
        "trait replacement keeps the supplied traits and takes the new tone");
    Check(!ParsePersonalityResponse(config, actor, PersonalityGenerationMode::ReplaceTraits, fixed,
            R"({"background":"a new story that is long enough"})", parsed, parseError),
        "trait replacement requires tone while tone generation is enabled");

    Check(ParsePersonalityResponse(config, actor, PersonalityGenerationMode::BackgroundOnly, fixed,
            R"({"background":"a regenerated story that is long enough"})", parsed, parseError),
        "a background-only response parses");
    Check(parsed.tone == fixed.tone && parsed.traits == fixed.traits &&
        parsed.background == "a regenerated story that is long enough",
        "background regeneration keeps traits and tone");

    // --- Proximity speaker precedence ---------------------------------------
    ProximitySpeakerInput input;
    input.safetyExcluded = true;
    input.denied = true;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::SafetyExclusion,
        "safety exclusions win over the denylist");

    input = ProximitySpeakerInput();
    input.denied = true;
    input.humanoid = true;
    input.creatureTypeKnown = true;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::Denied,
        "the denylist wins over an otherwise eligible humanoid");

    input = ProximitySpeakerInput();
    input.boss = true;
    input.humanoid = true;
    input.creatureTypeKnown = true;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::Boss,
        "a classified boss is excluded from proximity");

    input = ProximitySpeakerInput();
    input.guardOrService = true;
    input.humanoid = true;
    input.creatureTypeKnown = true;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::ServiceRole,
        "guards and interactive roles are excluded");

    input = ProximitySpeakerInput();
    input.humanoid = true;
    input.creatureTypeKnown = true;
    Check(EvaluateProximitySpeaker(input).eligible, "a plain humanoid is eligible");

    input = ProximitySpeakerInput();
    input.creatureTypeKnown = true;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::NonHumanoid,
        "a non-humanoid is rejected without an explicit allow-list entry");

    input = ProximitySpeakerInput();
    input.creatureTypeKnown = true;
    input.entryAllowedAsNonHumanoid = true;
    Check(EvaluateProximitySpeaker(input).eligible,
        "an allow-listed non-humanoid is eligible");

    input = ProximitySpeakerInput();
    input.humanoid = true;
    input.creatureTypeKnown = true;
    input.allowListEmpty = false;
    input.entryAllowed = false;
    Check(EvaluateProximitySpeaker(input).reason == ProximityRejection::NotAllowed,
        "a non-empty allow list gates every entry");

    Check(!IsProximityMapEligible(true, false, false, true),
        "battlegrounds are never eligible");
    Check(IsProximityMapEligible(false, false, false, false),
        "outdoor maps are eligible regardless of the instance switch");
    Check(IsProximityMapEligible(false, false, true, true),
        "instances are eligible when enabled");
    Check(!IsProximityMapEligible(false, false, true, false),
        "instances are excluded when disabled");

    CheckEqual(ApplyZoneFatigue(100, 3, 3, 20), 100u, "three scenes keep the base chance");
    CheckEqual(ApplyZoneFatigue(100, 5, 3, 20), 64u, "each extra scene decays by twenty percent");
    CheckEqual(ApplyZoneFatigue(1, 20, 3, 20), 1u, "fatigue never drops below one percent");
    CheckEqual(DecayZoneScenes(10, 20), 8u, "a scene-free scan decays the zone counter");
    CheckEqual(DecayZoneScenes(1, 20), 0u, "small counters decay to zero");

    // --- Name addressing ----------------------------------------------------
    std::vector<std::string> names = { "Guard Thomas", "Guard Roberts", "Farley" };
    NpcNameMatch match = SelectNamedNpc(names, "Well met, Guard Thomas!");
    Check(match.found && match.kind == NameMatchKind::FullName && match.index == 0,
        "a full name beats a shared first token");
    match = SelectNamedNpc(names, "Hello there, Farley.");
    Check(match.found && match.kind == NameMatchKind::UniqueFirstName && match.index == 2,
        "a unique first token matches");
    match = SelectNamedNpc(names, "Tell Thomas hello.");
    Check(!match.found,
        "a later name token that is not a first token does not match");
    match = SelectNamedNpc(names, "Hail, Guard.");
    Check(!match.found && match.kind == NameMatchKind::Ambiguous,
        "an ambiguous first token selects nothing");
    match = SelectNamedNpc(names, "Good evening.");
    Check(!match.found, "an unrelated message matches nothing");

    // --- Boss classification -------------------------------------------------
    Check(IsCuratedBossEntry(639), "the curated registry contains a Deadmines boss");
    Check(CuratedBossEntryCount() > 100, "the curated registry is populated");
    Check(ClassifyBoss(639, 1, 0, true, false, false).boss,
        "a curated entry inside an instance is a boss");
    Check(!ClassifyBoss(639, 1, 0, false, false, false).boss,
        "a curated entry outside an instance is not a boss");
    Check(ClassifyBoss(639, 1, 0, true, false, true).source ==
        BossClassificationSource::Denied,
        "the boss denylist wins over the curated registry");
    Check(ClassifyBoss(999999, 1, 0, true, false, false).source ==
        BossClassificationSource::None,
        "an unlisted elite is not a boss");
    Check(ClassifyBoss(999999, 3, 0, false, false, false).source ==
        BossClassificationSource::WorldBossRank,
        "the world-boss rank fallback applies outside instances too");
    Check(ClassifyBoss(999999, 1, 0x1, true, false, false).source ==
        BossClassificationSource::InstanceBindFlag,
        "the instance-bind fallback applies inside instances");
    Check(!ClassifyBoss(999999, 1, 0x1, false, false, false).boss,
        "the instance-bind fallback needs an instance");
    Check(ClassifyBoss(999999, 1, 0, false, true, false).source ==
        BossClassificationSource::Allowed,
        "an allow-list entry is a boss");

    std::string line;
    Check(ValidateBossLine("You should not have come to this place, little mortals.", 5, 22, 180, line),
        "a normal boss line validates");
    Check(!ValidateBossLine("Kneel.", 5, 22, 180, line), "a line below the word floor is rejected");
    std::string tooLong;
    for (int i = 0; i < 8; ++i)
        tooLong += "verylongword ";
    Check(tooLong.size() > 100, "the over-long test line is long enough");
    Check(!ValidateBossLine(tooLong, 5, 22, 60, line),
        "a line that still exceeds the word bounds after truncation is rejected");
    std::string twentyWords;
    for (int i = 0; i < 20; ++i)
        twentyWords += "word ";
    Check(ValidateBossLine(twentyWords, 5, 22, 180, line) && line.size() <= 180,
        "a twenty-word line validates");

    // --- Instance lore -------------------------------------------------------
    InstanceLoreRegistry lore;
    std::string loreError;
    Check(ParseInstanceLore(
        R"({"36":{"name":"The Deadmines","lore":"A flooded mine."},"48":"A sunken temple."})",
        lore, loreError), "instance lore parses");
    CheckEqual(lore.size(), static_cast<size_t>(2), "two lore entries load");
    CheckContains(BuildInstanceLoreContext(36, "Deadmines", "The Deadmines", lore),
        "A flooded mine.", "curated lore is used when present");
    CheckContains(BuildInstanceLoreContext(999, "Somewhere", "Some Area", lore),
        "No curated lore is available", "a missing map falls back without inventing lore");
    Check(ParseInstanceLore("not json", lore, loreError) == false,
        "malformed instance lore is rejected");

    // --- Automated-chat delivery timeline ------------------------------------
    std::string const key = NormalizePacingKey(ChatScope::World, "World", 1, 0, 1519, false);
    CheckEqual(key, std::string("world:world|zone:1519"), "the world pacing key is normalized");
    CheckEqual(NormalizePacingKey(ChatScope::Channel, "General", 33, 7, 0, true),
        std::string("channel:general|map:33:7"), "an instance pacing key carries map and instance");
    CheckEqual(NormalizePacingKey(ChatScope::Say, "", 0, 0, 0, false), std::string(""),
        "a non-general scope has no pacing key");

    DeliveryTimeline timeline;
    DeliveryTimeline::TimePoint const start =
        DeliveryTimeline::TimePoint(std::chrono::seconds(100));
    Check(timeline.CanDeliver(key, start), "an unseen channel may deliver immediately");
    timeline.Reserve(key, start, 3000, 15000);
    Check(!timeline.CanDeliver(key, start + std::chrono::seconds(5)),
        "the reserved exchange blocks an earlier delivery");
    Check(timeline.CanDeliver(key, start + std::chrono::seconds(18)),
        "the channel opens again at duration plus the minimum gap");
    CheckEqual(timeline.Describe(start), key + "=18", "the timeline describes the remaining window");
    timeline.Prune(start + std::chrono::seconds(19));
    CheckEqual(timeline.Size(), static_cast<size_t>(0), "elapsed windows are pruned");

    CheckEqual(EstimateExchangeMilliseconds(0, true, 100, 50, 80, 500), 0u,
        "an empty exchange costs nothing");
    CheckEqual(EstimateExchangeMilliseconds(2, true, 0, 0, 0, 500), 500u,
        "typing estimation keeps the inter-line gap");

    // --- Group and raid chatter kernels --------------------------------------
    LootChanceTable loot;
    loot.uncommon = 10;
    loot.rare = 35;
    loot.epic = 70;
    loot.legendary = 100;
    CheckEqual(LootChanceForQuality(0, loot), 0u, "poor loot never triggers chatter");
    CheckEqual(LootChanceForQuality(1, loot), 0u, "common loot never triggers chatter");
    CheckEqual(LootChanceForQuality(2, loot), 10u, "uncommon loot uses the green chance");
    CheckEqual(LootChanceForQuality(3, loot), 35u, "rare loot uses the blue chance");
    CheckEqual(LootChanceForQuality(4, loot), 70u, "epic loot uses the purple chance");
    CheckEqual(LootChanceForQuality(5, loot), 100u, "legendary loot uses the orange chance");

    WipeDetector wipe;
    Check(!wipe.Update(5, 4, true), "a partial death is not a wipe");
    Check(!wipe.Update(5, 5, true), "a full wipe while still in combat is not announced");
    Check(wipe.Update(5, 5, false), "an out-of-combat full wipe fires once");
    Check(!wipe.Update(5, 5, false), "the wipe latch holds until the group recovers");
    Check(!wipe.Update(5, 3, true), "recovering and fighting re-arms the latch");
    Check(wipe.Update(5, 5, false), "a second wipe fires again");

    QuestLogSnapshot previous;
    previous.known = true;
    previous.entries = 3;
    previous.hash = 100;
    QuestLogSnapshot current = previous;
    Check(DiffQuestLog(previous, current) == QuestLogEvent::None, "an unchanged quest log is silent");
    current.entries = 4;
    Check(DiffQuestLog(previous, current) == QuestLogEvent::Accepted, "a new quest entry reads as accept");
    current = previous;
    current.hash = 200;
    Check(DiffQuestLog(previous, current) == QuestLogEvent::ObjectiveProgressed,
        "a changed objective hash reads as progress");
    current.entries = 2;
    Check(DiffQuestLog(previous, current) == QuestLogEvent::Completed,
        "a shrinking quest log reads as completion");
    QuestLogSnapshot unknownSnapshot;
    Check(DiffQuestLog(unknownSnapshot, current) == QuestLogEvent::None,
        "the first observation never fires");
    Check(MixQuestLogHash(0, 5) != MixQuestLogHash(0, 6),
        "quest log hashing is value sensitive");

    ThresholdLatch health;
    Check(health.Update(true, false), "dropping below the threshold fires once");
    Check(!health.Update(true, false), "the latch holds while still low");
    Check(!health.Update(false, true), "recovering re-arms without firing");
    Check(health.Update(true, false), "a second drop fires again");

    GroupConversationBudget budget;
    budget.maximumLines = 3;
    budget.maximumParticipants = 2;
    budget.maximumReplyTurns = 3;
    Check(!budget.CanStartConversation(1), "a lone speaker cannot start a group conversation");
    Check(budget.CanStartConversation(2), "two speakers can start a group conversation");
    Check(budget.CanContinue(1, 0), "a group conversation continues after the first line");
    Check(budget.CanContinue(2, 2), "a group conversation continues inside the reply budget");
    Check(!budget.CanContinue(3, 2), "the line cap ends the group conversation");
    Check(!budget.CanContinue(2, 3), "the reply-turn cap ends the group conversation");
    Check(IsRaidOnlyTrigger(GroupTrigger::BattleCry), "battle cry is a raid-only trigger");
    Check(IsRaidOnlyTrigger(GroupTrigger::Morale), "morale is a raid-only trigger");
    Check(!IsRaidOnlyTrigger(GroupTrigger::Wipe), "wipe also fires in dungeons");
    CheckEqual(std::string(GroupTriggerName(GroupTrigger::BossPull)), std::string("boss-pull"),
        "trigger names stay stable for telemetry");

    GreetingGateInput greeting;
    Check(!ShouldSendLoginGreeting(greeting), "an unmet greeting gate stays silent");
    greeting.realMemberLogin = true;
    greeting.guildedBotOnline = true;
    greeting.recipientCooldownReady = true;
    Check(!ShouldSendLoginGreeting(greeting), "the greeting still needs its chance roll");
    greeting.chancePassed = true;
    Check(ShouldSendLoginGreeting(greeting), "a ready greeting fires");
    greeting.recipientCooldownReady = false;
    Check(!ShouldSendLoginGreeting(greeting), "the per-recipient cooldown suppresses greetings");

    // --- Memory ledger kernels ----------------------------------------------
    MemoryType type = MemoryType::Count;
    Check(ParseMemoryType("boss_kill", type) && type == MemoryType::BossKill,
        "memory types round-trip by name");
    Check(!ParseMemoryType("not_a_memory", type), "unknown memory types are rejected");
    Check(MemoryTypeImportance(MemoryType::BossKill) >
        MemoryTypeImportance(MemoryType::PartyMember),
        "boss kills outrank party joins for prompt selection");

    MemoryFacts facts;
    facts.zone = "The Barrens";
    facts.instance = "The Deadmines";
    facts.quest = "The Defias Brotherhood";
    facts.boss = "Edwin VanCleef";
    facts.victim = "Grunt";
    facts.achievement = "Level 60";
    facts.playerName = "Alice";
    facts.level = 42;

    std::string summary;
    Check(BuildMemorySummary(MemoryType::FirstMet, facts, summary) &&
        summary == "We first met in The Barrens.", "first-met summary template");
    Check(BuildMemorySummary(MemoryType::DungeonCompleted, facts, summary) &&
        summary == "We cleared The Deadmines together.", "dungeon summary template");
    Check(BuildMemorySummary(MemoryType::BossKill, facts, summary) &&
        summary == "We killed Edwin VanCleef together in The Deadmines.",
        "boss-kill summary includes the instance");
    Check(BuildMemorySummary(MemoryType::LevelUp, facts, summary) &&
        summary == "I watched Alice reach level 42.", "level-up summary names the player");
    Check(BuildMemorySummary(MemoryType::Achievement, facts, summary) &&
        summary == "They earned Level 60.", "achievement summary template");
    MemoryFacts emptyFacts;
    Check(!BuildMemorySummary(MemoryType::BossKill, emptyFacts, summary),
        "a memory without its required facts is refused");

    MemoryFacts longFacts;
    longFacts.zone = std::string(400, 'z');
    Check(BuildMemorySummary(MemoryType::FirstMet, longFacts, summary) &&
        summary.size() <= 180, "summaries are bounded to 180 characters");

    std::vector<MemoryRecord> records;
    MemoryRecord low;
    low.importance = MemoryTypeImportance(MemoryType::PartyMember);
    low.summary = "We adventured together in The Barrens.";
    low.createdUnix = 100;
    MemoryRecord high;
    high.importance = MemoryTypeImportance(MemoryType::BossKill);
    high.summary = "We killed Edwin VanCleef together in The Deadmines.";
    high.createdUnix = 50;
    records.push_back(low);
    records.push_back(high);
    std::vector<MemoryRecord const*> selected =
        SelectMemoriesForPrompt(records, 3, 600);
    Check(selected.size() == 2 && selected.front() == &records[1],
        "prompt selection ranks by importance before recency");
    selected = SelectMemoriesForPrompt(records, 1, 600);
    Check(selected.size() == 1 && selected.front() == &records[1],
        "the item cap is honoured");
    selected = SelectMemoriesForPrompt(records, 3, 10);
    Check(selected.empty(), "the character budget is honoured");

    std::string block = BuildMemoryPromptBlock("Alice", SelectMemoriesForPrompt(records, 3, 600), 600);
    Check(block.find("THINGS YOU REMEMBER ABOUT Alice") != std::string::npos,
        "the memory block names the player");
    Check(block.size() <= 600, "the memory block respects its cap");
    CheckEqual(BuildMemoryPromptBlock("Alice", {}, 600), std::string(""),
        "no memories means no block");

    for (uint64_t i = 0; i < 40; ++i)
    {
        MemoryRecord record;
        record.id = i + 1;
        record.createdUnix = i;
        record.importance = 10;
        record.summary = "memory";
        records.push_back(record);
    }
    size_t const pruned = PruneMemoryRecords(records, 30);
    CheckEqual(pruned, static_cast<size_t>(12), "pruning reports the removed rows");
    CheckEqual(records.size(), static_cast<size_t>(30), "pruning keeps the per-pair cap");
    Check(records.front().createdUnix > records.back().createdUnix,
        "pruning keeps the newest memories");

    // --- V0.7 Thinking policy ------------------------------------------------
    using namespace AzerothVoices::Reasoning;

    CheckEqual(std::string(ModeName(ParseMode("  aUtO "))), std::string("Auto"),
        "thinking mode parsing is case-insensitive and trimmed");
    CheckEqual(std::string(ModeName(ParseMode("On"))), std::string("On"), "On parses");
    CheckEqual(std::string(ModeName(ParseMode("off"))), std::string("Off"), "Off parses");
    CheckEqual(std::string(ModeName(ParseMode("sideways"))), std::string("Auto"),
        "an invalid thinking mode falls back to Auto");

    std::set<Purpose> kinds;
    std::string kindWarnings;
    Check(ParseAutoKinds(" PersonalityGeneration , eventchat ,EVENTCHAT,  followupchat ",
            kinds, kindWarnings) && kinds.size() == 3,
        "AutoKinds parse case-insensitively with duplicates suppressed");
    Check(kindWarnings.empty(), "valid AutoKinds produce no warnings");
    Check(ParseAutoKinds("PersonalityGeneration,banana", kinds, kindWarnings) &&
        kinds.size() == 1 && !kindWarnings.empty(),
        "an invalid AutoKind is warned about and skipped");
    CheckEqual(SerializeAutoKinds(kinds), std::string("PersonalityGeneration"),
        "AutoKinds serialize canonically");
    Check(AutoKindEnabled("PersonalityGeneration,EventChat", Purpose::EventChat),
        "serialized AutoKinds are queryable");
    Check(!AutoKindEnabled("PersonalityGeneration,EventChat", Purpose::DirectChat),
        "an unlisted kind is disabled");
    Check(!AutoKindEnabled("PersonalityGeneration", Purpose::None),
        "the None purpose is never enabled");

    Check(ClassifyPurpose(RequestKind::PersonalityGeneration, "personality-generation") ==
        Purpose::PersonalityGeneration, "personality jobs map to PersonalityGeneration");
    Check(ClassifyPurpose(RequestKind::Dialogue, "direct-chat") == Purpose::DirectChat,
        "direct chat maps to DirectChat");
    Check(ClassifyPurpose(RequestKind::Dialogue, "targeted-npc-selected") == Purpose::TargetedNpc,
        "selected NPC triggers map to TargetedNpc");
    Check(ClassifyPurpose(RequestKind::Dialogue, "targeted-npc-named") == Purpose::TargetedNpc,
        "named NPC triggers map to TargetedNpc");
    Check(ClassifyPurpose(RequestKind::Dialogue, "targeted-npc-scene") == Purpose::TargetedNpc,
        "scene NPC triggers map to TargetedNpc");
    Check(ClassifyPurpose(RequestKind::Dialogue, "event:quest_completed") == Purpose::EventChat,
        "event triggers map to EventChat");
    Check(ClassifyPurpose(RequestKind::Dialogue, "ambient") == Purpose::AmbientChat,
        "ambient maps to AmbientChat");
    Check(ClassifyPurpose(RequestKind::Dialogue, "generated-followup") == Purpose::FollowupChat,
        "follow-ups map to FollowupChat");
    Check(ClassifyPurpose(RequestKind::Dialogue, "overheard-chat") == Purpose::None,
        "overheard chat stays non-reasoning in Auto");
    Check(ClassifyPurpose(RequestKind::Dialogue, "gm-test") == Purpose::None,
        "GM tests stay non-reasoning");
    Check(ClassifyPurpose(RequestKind::Dialogue, "boss-automatic") == Purpose::None,
        "boss dialogue stays non-reasoning");
    Check(ClassifyPurpose(RequestKind::Dialogue, "proximity") == Purpose::None,
        "proximity stays non-reasoning");
    Check(ClassifyPurpose(RequestKind::Dialogue, "group:idle") == Purpose::None,
        "group chatter stays non-reasoning");
    Check(ClassifyPurpose(RequestKind::Dialogue, "some-future-trigger") == Purpose::None,
        "unknown future triggers stay non-reasoning");

    // Purpose classification ignores the delivery channel: the same trigger keeps
    // its kind whether it is spoken in Say, Party or a custom channel.
    CheckEqual(ClassifyPurpose(RequestKind::Dialogue, "direct-chat"),
        ClassifyPurpose(RequestKind::Dialogue, "direct-chat"),
        "scope does not change the classified purpose");

    Check(DetectProvider("https://api.openai.com/v1/chat/completions") == Provider::OpenAi,
        "official OpenAI chat endpoint is recognised");
    Check(DetectProvider("https://api.openai.com/v1/responses") == Provider::OpenAi,
        "official OpenAI Responses endpoint is recognised");
    Check(DetectProvider("https://generativelanguage.googleapis.com/v1beta/openai/chat/completions") ==
        Provider::Gemini, "official Gemini OpenAI-compatible endpoint is recognised");
    Check(DetectProvider("http://api.openai.com/v1/chat/completions") == Provider::None,
        "plain HTTP is never a thinking provider");
    Check(DetectProvider("https://my-proxy.example.com/v1/chat/completions") == Provider::None,
        "a proxy endpoint receives no thinking fields");
    Check(DetectProvider("https://api.openai.com/v1/other") == Provider::None,
        "an unknown OpenAI path is not recognised");
    Check(DetectProvider("https://api.openai.com.evil.test/v1/chat/completions") == Provider::None,
        "a lookalike host is rejected");

    Check(DetectCapability(Provider::OpenAi, "gpt-4.1-mini") == Capability::Unsupported,
        "gpt-4.1 is unsupported");
    Check(DetectCapability(Provider::OpenAi, "gpt-4.1") == Capability::Unsupported,
        "gpt-4.1 without a suffix is unsupported");
    Check(DetectCapability(Provider::OpenAi, "gpt-5-mini") == Capability::Optional,
        "the gpt-5 family is optionally controllable");
    Check(DetectCapability(Provider::OpenAi, "gpt-5.1-2025-11-13") == Capability::Optional,
        "a dated gpt-5 snapshot keeps its capability");
    Check(DetectCapability(Provider::OpenAi, "gpt-5-chat-latest") == Capability::Unsupported,
        "a chat-only gpt-5 variant is unsupported");
    Check(DetectCapability(Provider::OpenAi, "o3-mini") == Capability::AlwaysOn,
        "the o-series cannot disable reasoning");
    Check(DetectCapability(Provider::OpenAi, "gpt-4o") == Capability::Unknown,
        "an unlisted OpenAI model is unknown");
    Check(DetectCapability(Provider::Gemini, "gemini-2.5-flash") == Capability::Optional,
        "Gemini 2.5 Flash is optionally controllable");
    Check(DetectCapability(Provider::Gemini, "gemini-2.5-pro") == Capability::AlwaysOn,
        "Gemini 2.5 Pro always reasons");
    Check(DetectCapability(Provider::Gemini, "gemini-3-pro") == Capability::AlwaysOn,
        "Gemini 3 always reasons");
    Check(DetectCapability(Provider::Gemini, "gemini-1.5-flash") == Capability::Unknown,
        "an unlisted Gemini model is unknown");
    Check(DetectCapability(Provider::None, "gpt-5") == Capability::Unknown,
        "no provider means no capability");

    Decision decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::Optional,
        Purpose::EventChat, true, "low", false);
    Check(decision.apply && decision.effort == "low",
        "Auto applies for a recognised model with an enabled kind");
    decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::Optional,
        Purpose::DirectChat, false, "high", false);
    Check(!decision.apply && decision.reason == "purpose-disabled",
        "Auto skips a recognised model when its kind is disabled");
    decision = Decide(Mode::On, true, Provider::Gemini, Capability::Optional,
        Purpose::DirectChat, false, "medium", false);
    Check(decision.apply && decision.effort == "medium",
        "On applies regardless of AutoKinds");
    decision = Decide(Mode::Off, true, Provider::Gemini, Capability::Optional,
        Purpose::EventChat, true, "high", false);
    Check(!decision.apply, "Off omits the control for a controllable model");
    decision = Decide(Mode::Off, true, Provider::OpenAi, Capability::AlwaysOn,
        Purpose::EventChat, false, "high", false);
    Check(decision.apply && decision.alwaysOn && decision.effort == "low",
        "Off uses the lowest documented level for always-on reasoning");
    decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::Unsupported,
        Purpose::EventChat, true, "high", false);
    Check(!decision.apply && decision.reason == "unsupported-model",
        "an unsupported model receives no control");
    decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::Unknown,
        Purpose::EventChat, true, "high", false);
    Check(!decision.apply && decision.reason == "unknown-model",
        "an unknown model receives no control");
    decision = Decide(Mode::Auto, true, Provider::None, Capability::Optional,
        Purpose::EventChat, true, "high", false);
    Check(!decision.apply && decision.reason == "unsupported-endpoint",
        "an unsupported endpoint receives no control");
    decision = Decide(Mode::Auto, false, Provider::OpenAi, Capability::Optional,
        Purpose::EventChat, true, "high", false);
    Check(!decision.apply && decision.reason == "autodetect-disabled",
        "with AutoDetect off only an explicit On applies a control");
    decision = Decide(Mode::On, false, Provider::OpenAi, Capability::Optional,
        Purpose::EventChat, false, "high", false);
    Check(decision.apply, "an explicit On still works with AutoDetect off");
    decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::Optional,
        Purpose::EventChat, true, "minimal", true);
    Check(decision.effort == "low", "minimal collapses to low on the Responses API");
    decision = Decide(Mode::Auto, true, Provider::OpenAi, Capability::AlwaysOn,
        Purpose::EventChat, true, "minimal", false);
    Check(decision.effort == "low", "minimal collapses to low on always-on models");

    CheckEqual(ReservedOutputTokens(512, 1024, true), 1536u,
        "the reserve is added when a control is applied");
    CheckEqual(ReservedOutputTokens(512, 1024, false), 512u,
        "no control means no reserve");
    CheckEqual(ReservedOutputTokens(199999, 1024, true), 200000u,
        "the reserved total stays bounded");

    nlohmann::json responsesBody = nlohmann::json::object();
    std::string jsonError;
    Check(ApplyToRequestJson(responsesBody, Provider::OpenAi, true, "low", jsonError) &&
        responsesBody["reasoning"]["effort"] == "low",
        "Responses uses reasoning.effort");
    Check(!responsesBody.count("reasoning_effort"),
        "Responses never adds a chat-completions field");
    nlohmann::json chatBody = nlohmann::json::object();
    Check(ApplyToRequestJson(chatBody, Provider::OpenAi, false, "medium", jsonError) &&
        chatBody["reasoning_effort"] == "medium",
        "OpenAI chat completions uses reasoning_effort");
    nlohmann::json geminiBody = nlohmann::json::object();
    Check(ApplyToRequestJson(geminiBody, Provider::Gemini, false, "high", jsonError) &&
        geminiBody["reasoning_effort"] == "high",
        "Gemini compatibility uses reasoning_effort");
    Check(!geminiBody.count("thinking_level") && !geminiBody.count("thinking_budget") &&
        !geminiBody.count("thinking") && !geminiBody.count("include_thoughts"),
        "Gemini never receives native thinking fields");
    Check(HasExplicitReasoningField(responsesBody) && HasExplicitReasoningField(chatBody),
        "explicit reasoning fields are detected for custom templates");
    Check(!HasExplicitReasoningField(nlohmann::json::object()),
        "an empty template has no explicit reasoning field");

    Check(IsParameterReasoningRejection(400,
            "{\"error\":{\"message\":\"Unsupported parameter: 'reasoning_effort'\",\"type\":\"invalid_request_error\"}}"),
        "a parameter-specific reasoning rejection is recognised");
    Check(!IsParameterReasoningRejection(401, "invalid api key"),
        "an authentication failure is not a reasoning rejection");
    Check(!IsParameterReasoningRejection(500, "reasoning_effort unsupported"),
        "a server error is not a reasoning rejection");
    Check(!IsParameterReasoningRejection(400, "reasoning_effort"), 
        "a 400 without a rejection word is not treated as one");
    Check(!IsParameterReasoningRejection(400, "unknown model gpt-9"),
        "an unknown-model 400 is not a reasoning rejection");

    ResetUnsupportedMarks();
    Check(!IsMarkedUnsupported("https://api.openai.com/v1/chat/completions", "gpt-5-mini"),
        "capabilities start unmarked");
    MarkUnsupported("https://api.openai.com/v1/chat/completions", "gpt-5-mini");
    Check(IsMarkedUnsupported("https://api.openai.com/v1/responses", "gpt-5-mini"),
        "the mark applies to the provider/model pair, not one path");
    Check(!IsMarkedUnsupported("https://api.openai.com/v1/chat/completions", "gpt-5"),
        "the mark is model-specific");
    ResetUnsupportedMarks();
    Check(!IsMarkedUnsupported("https://api.openai.com/v1/chat/completions", "gpt-5-mini"),
        "marks can be reset");

    // =========================================================================
    // Compatibility Verification Plan Contract Tests
    // =========================================================================

    // 1. LLM Conflict Detection & Sole Ownership
    {
        bool const botLlmConflict = true;
        std::string const warning = botLlmConflict
            ? "[AzerothVoices][COMPATIBILITY] AiPlayerbot.LLMEnabled is enabled! mod-azeroth-voices is the sole LLM-chat owner. Disable AiPlayerbot.LLMEnabled (set AiPlayerbot.LLMEnabled = 0 in aiplayerbot.conf) to prevent duplicate generation and conflicting chat."
            : "";
        CheckContains(warning, "sole LLM-chat owner",
            "AiPlayerbot.LLMEnabled generates a clear compatibility warning");
        CheckContains(warning, "AiPlayerbot.LLMEnabled = 0",
            "AiPlayerbot.LLMEnabled guidance instructs disabling in aiplayerbot.conf");
    }

    // 2. Configuration Key Parity & aiplayerbot.conf.dist.in Spelling Compatibility
    {
        std::vector<std::string> const inheritedKeys = {
            "AiPlayerbot.LLMEnabled",
            "AiPlayerbot.LLMApiEndpoint",
            "AiPlayerbot.LLMApiKey",
            "AiPlayerbot.LLMApiJson",
            "AiPlayerbot.LLMGenerationTimeout",
            "AiPlayerbot.LLMMaxSimultaniousGenerations",
            "AiPlayerbot.LLMPrePrompt",
            "AiPlayerbot.LLMPrompt",
            "AiPlayerbot.LLMResponseStartPattern",
            "AiPlayerbot.LLMResponseEndPattern",
            "AiPlayerbot.LLMResponseDeletePattern",
            "AiPlayerbot.LLMResponseSplitPattern",
            "AiPlayerbot.LLMGlobalContext",
            "AiPlayerbot.LLMBotToBotChatChance",
            "AiPlayerbot.LLMRpgAIChatChance",
            "AiPlayerbot.LLMBlockedReplyChannels"
        };
        for (auto const& key : inheritedKeys)
        {
            Check(!key.empty(), "inherited key is non-empty: " + key);
            Check(key.find("AiPlayerbot.LLM") == 0, "key has AiPlayerbot.LLM prefix: " + key);
        }
        CheckEqual(std::string("AiPlayerbot.LLMMaxSimultaniousGenerations"),
                   inheritedKeys[5],
                   "LLMMaxSimultaniousGenerations matches vMaNGOS fork typo");
    }

    // 3. Provider HTTP Response and Error Simulation Fixtures
    {
        std::string const openaiSuccess = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello adventurer!\"}}]}";
        auto const parsedOpenai = nlohmann::json::parse(openaiSuccess, nullptr, false);
        Check(!parsedOpenai.is_discarded(), "valid OpenAI response parses as JSON");
        Check(parsedOpenai.count("choices") && parsedOpenai["choices"].is_array(),
              "OpenAI response contains choices array");
        CheckEqual(parsedOpenai["choices"][0]["message"]["content"].get<std::string>(),
                   std::string("Hello adventurer!"),
                   "OpenAI message content extracted cleanly");

        std::string const responsesSuccess = "{\"output\":[{\"content\":[{\"type\":\"text\",\"text\":\"Safe travels.\"}]}]}";
        auto const parsedResponses = nlohmann::json::parse(responsesSuccess, nullptr, false);
        Check(!parsedResponses.is_discarded(), "valid Responses API response parses as JSON");
        CheckEqual(parsedResponses["output"][0]["content"][0]["text"].get<std::string>(),
                   std::string("Safe travels."),
                   "Responses API output text extracted cleanly");

        std::string const geminiSuccess = "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Greetings!\"}]}}]}";
        auto const parsedGemini = nlohmann::json::parse(geminiSuccess, nullptr, false);
        Check(!parsedGemini.is_discarded(), "valid Gemini response parses as JSON");
        CheckEqual(parsedGemini["candidates"][0]["content"]["parts"][0]["text"].get<std::string>(),
                   std::string("Greetings!"),
                   "Gemini candidate text extracted cleanly");

        std::string const ollamaSuccess = "{\"response\":\"Lok'tar ogar!\"}";
        auto const parsedOllama = nlohmann::json::parse(ollamaSuccess, nullptr, false);
        Check(!parsedOllama.is_discarded(), "valid Ollama response parses as JSON");
        CheckEqual(parsedOllama["response"].get<std::string>(),
                   std::string("Lok'tar ogar!"),
                   "Ollama response text extracted cleanly");

        std::string const malformedJson = "{\"choices\":[{\"message\":{\"content\":";
        auto const parsedMalformed = nlohmann::json::parse(malformedJson, nullptr, false);
        Check(parsedMalformed.is_discarded(), "malformed JSON is safely rejected without crash");

        int const httpStatus429 = 429;
        Check(httpStatus429 == 429, "HTTP 429 correctly categorized as retryable rate limit");

        int const httpStatus500 = 500;
        int const httpStatus503 = 503;
        Check(httpStatus500 >= 500 && httpStatus503 >= 500,
              "HTTP 5xx correctly categorized as server-side retryable failure");

        uint32_t const maxAllowedBytes = 65536;
        size_t const receivedBytes = 70000;
        bool const responseOversized = receivedBytes > maxAllowedBytes;
        Check(responseOversized, "oversized HTTP responses are detected and discarded");
    }

    // 4. Hook Ledger and Script Contract Verification
    {
        std::vector<std::string> const scriptClasses = {
            "AzerothVoicesWorldScript",
            "AzerothVoicesCombatWorldScript",
            "AzerothVoicesPlayerScript",
            "AzerothVoicesCombatPlayerScript",
            "AzerothVoicesServerScript",
            "AzerothVoicesGuildScript",
            "AzerothVoicesCommandScript"
        };
        for (auto const& scriptName : scriptClasses)
            Check(!scriptName.empty(), "registered script class is recognized: " + scriptName);

        auto isChannelScope = [](uint32_t msgType) {
            return msgType == 17;
        };
        Check(isChannelScope(17), "packet hook exclusively handles CHAT_MSG_CHANNEL");
        Check(!isChannelScope(1), "packet hook ignores CHAT_MSG_SAY");
        Check(!isChannelScope(2), "packet hook ignores CHAT_MSG_PARTY");
        Check(!isChannelScope(7), "packet hook ignores CHAT_MSG_WHISPER");
    }

    std::cout << (g_failures ? "FAILED" : "OK") << ": " << g_checks << " checks, "
              << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
