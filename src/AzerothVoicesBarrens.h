#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AzerothVoices
{
    // Tagged level selector for tone axes: either Auto (sampled from distribution)
    // or Fixed (a fixed level 0..3).
    enum class BarrensSelectorMode : uint8_t
    {
        Auto,
        Fixed
    };

    struct BarrensTaggedLevel
    {
        BarrensSelectorMode mode = BarrensSelectorMode::Auto;
        uint8_t level = 0; // 0..3 when mode == Fixed

        bool IsAuto() const { return mode == BarrensSelectorMode::Auto; }
        uint8_t GetFixed() const { return level; }

        static BarrensTaggedLevel FromString(std::string const& str,
                                             BarrensTaggedLevel const& def = BarrensTaggedLevel{},
                                             bool* warned = nullptr);
        std::string ToString() const;

        bool operator==(BarrensTaggedLevel const& other) const
        {
            return mode == other.mode && (mode == BarrensSelectorMode::Auto || level == other.level);
        }
    };

    // Four discrete levels (0, 1, 2, 3) with integer weights (0..1000 each, positive sum).
    struct BarrensLevelWeights
    {
        uint32_t weights[4] = { 0, 0, 0, 0 };

        static BarrensLevelWeights DefaultSalt()     { return { { 20, 30, 35, 15 } }; }
        static BarrensLevelWeights DefaultSarcasm()  { return { { 15, 30, 35, 20 } }; }
        static BarrensLevelWeights DefaultToxicity() { return { { 30, 35, 25, 10 } }; }

        static bool Parse(std::string const& str, BarrensLevelWeights& out, std::string* error = nullptr);
        uint32_t TotalWeight() const;
        uint8_t Sample(uint32_t rollVal) const; // rollVal in [1, TotalWeight()]

        bool operator==(BarrensLevelWeights const& other) const
        {
            return weights[0] == other.weights[0] &&
                   weights[1] == other.weights[1] &&
                   weights[2] == other.weights[2] &&
                   weights[3] == other.weights[3];
        }
    };

    // Concrete resolved tone tuple (salt, sarcasm, toxicity)
    struct BarrensToneTuple
    {
        uint8_t salt = 0;     // 0..3
        uint8_t sarcasm = 0;  // 0..3
        uint8_t toxicity = 0; // 0..3

        bool operator==(BarrensToneTuple const& o) const
        {
            return salt == o.salt && sarcasm == o.sarcasm && toxicity == o.toxicity;
        }
    };

    enum class BarrensTopicSource : uint8_t
    {
        Hybrid,
        Curated,
        AI
    };

    BarrensTopicSource ParseBarrensTopicSource(std::string const& str,
                                               BarrensTopicSource def = BarrensTopicSource::Hybrid);
    char const* BarrensTopicSourceName(BarrensTopicSource source);

    enum class BarrensBeat : uint8_t
    {
        Reply,
        Reaction,
        Derail,
        Interruption
    };

    BarrensBeat ParseBarrensBeat(std::string const& str, BarrensBeat def = BarrensBeat::Reply);
    char const* BarrensBeatName(BarrensBeat beat);

    enum class BarrensBeatMode : uint8_t
    {
        Auto,
        Reply
    };

    BarrensBeatMode ParseBarrensBeatMode(std::string const& str, BarrensBeatMode def = BarrensBeatMode::Auto);
    char const* BarrensBeatModeName(BarrensBeatMode mode);

    enum class BarrensProfanityMode : uint8_t
    {
        Auto,
        Off
    };

    BarrensProfanityMode ParseBarrensProfanityMode(std::string const& str, BarrensProfanityMode def = BarrensProfanityMode::Auto);
    char const* BarrensProfanityModeName(BarrensProfanityMode mode);

    enum class BarrensScenePhase : uint8_t
    {
        TopicPending,       // Waiting for AI topic proposal (pre-opening)
        OpeningPending,     // Opener request queued/in-flight
        Waiting,            // Inter-line interval delay
        DerailTopicPending, // Waiting for AI derail topic proposal
        ReplyPending,       // Reply request queued/in-flight
        Finished,           // Scene successfully concluded
        Cancelled           // Scene terminated early
    };

    char const* BarrensScenePhaseName(BarrensScenePhase phase);

    // Curated topic definition
    struct BarrensCuratedTopic
    {
        std::string id;
        std::string family;
        std::string premise;
        std::vector<std::string> variants; // 3 variants per topic
    };

    // Immutable per-turn plan constructed on the world thread
    struct BarrensLinePlan
    {
        uint64_t sceneId = 0;
        uint32_t turnIndex = 0;
        uint64_t speakerGuid = 0;
        std::string speakerRole;
        BarrensBeat beat = BarrensBeat::Reply;
        std::string topicFamily;
        std::string topicPremise;
        std::string openingIntent;
        std::string seedId;
        uint32_t variantIndex = 0;
        BarrensToneTuple tone;
        bool profanityAllowed = false;
    };

    struct BarrensTranscriptEntry
    {
        uint64_t speakerGuid = 0;
        std::string speakerName;
        std::string text;
        BarrensBeat beat = BarrensBeat::Reply;
    };

    struct BarrensScene
    {
        uint64_t sceneId = 0;
        uint32_t epoch = 0;
        std::string audienceKey;
        BarrensScenePhase phase = BarrensScenePhase::OpeningPending;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point nextTurnTime;
        uint64_t pendingRequestId = 0;
        std::vector<uint64_t> participantGuids;
        std::unordered_map<uint64_t, std::string> participantRoles;
        std::unordered_map<uint64_t, BarrensToneTuple> participantBaselines;
        std::unordered_map<uint64_t, uint32_t> participantDeliveredCount;
        uint64_t previousSpeakerGuid = 0;
        std::string currentTopicFamily;
        std::string currentTopicPremise;
        std::string currentOpeningIntent;
        std::string currentSeedId;
        uint32_t targetMessageCount = 0;
        uint32_t deliveredCount = 0;
        uint32_t derailCount = 0;
        bool interruptionUsed = false;
        BarrensLinePlan pendingPlan;
        std::vector<BarrensTranscriptEntry> transcript;
        uint32_t proposalAttempts = 0;
        bool isAITopic = false;
    };

    // Pure parsing functions
    bool ParseBarrensTopicWeights(std::string const& input,
                                  std::vector<std::pair<std::string, uint32_t>>& out,
                                  std::string* error = nullptr);

    bool ParseBarrensBeatWeights(std::string const& input,
                                 std::vector<std::pair<BarrensBeat, uint32_t>>& out,
                                 std::string* error = nullptr);

    std::vector<std::pair<std::string, uint32_t>> DefaultBarrensTopicWeights();
    std::vector<std::pair<BarrensBeat, uint32_t>> DefaultBarrensBeatWeights();

    // Curated bank catalog
    std::vector<BarrensCuratedTopic> const& GetBarrensCuratedTopics();
    std::vector<std::string> const& GetBarrensSupportedTopicFamilies();
    bool IsValidBarrensTopicFamily(std::string const& family);

    // Topic selection
    std::string SampleBarrensTopicFamily(std::vector<std::pair<std::string, uint32_t>> const& topicWeights,
                                         std::string const& currentFamily,
                                         bool requireAlternate,
                                         uint32_t rollVal);

    BarrensCuratedTopic const* FindBarrensCuratedTopic(std::string const& topicId);
    BarrensCuratedTopic const* SelectBarrensCuratedTopic(std::string const& family,
                                                         std::set<std::string> const& activeCooldownIds,
                                                         uint32_t rollVal);

    // Tone sampling & rerolls
    BarrensToneTuple SampleBarrensBaselineTone(BarrensTaggedLevel const& salt,
                                               BarrensTaggedLevel const& sarcasm,
                                               BarrensTaggedLevel const& toxicity,
                                               BarrensLevelWeights const& saltWeights,
                                               BarrensLevelWeights const& sarcasmWeights,
                                               BarrensLevelWeights const& toxicityWeights,
                                               uint32_t rollSalt,
                                               uint32_t rollSarcasm,
                                               uint32_t rollToxicity);

    BarrensToneTuple ResolveBarrensLineTone(BarrensToneTuple const& baseline,
                                            BarrensTaggedLevel const& salt,
                                            BarrensTaggedLevel const& sarcasm,
                                            BarrensTaggedLevel const& toxicity,
                                            BarrensLevelWeights const& saltWeights,
                                            BarrensLevelWeights const& sarcasmWeights,
                                            BarrensLevelWeights const& toxicityWeights,
                                            uint32_t rerollRoll,
                                            uint32_t rerollChance,
                                            uint32_t rollSalt,
                                            uint32_t rollSarcasm,
                                            uint32_t rollToxicity);

    bool ResolveBarrensProfanityPermission(BarrensProfanityMode mode,
                                           uint32_t profanityChance,
                                           BarrensToneTuple const& tone,
                                           uint32_t rollVal);

    // Beat selection
    std::vector<BarrensBeat> FilterEligibleBarrensBeats(BarrensBeatMode mode,
                                                        uint32_t turnIndex,
                                                        uint32_t targetMessages,
                                                        uint32_t derailCount,
                                                        bool interruptionUsed,
                                                        bool alternateTopicAvailable);

    BarrensBeat SampleBarrensBeat(std::vector<std::pair<BarrensBeat, uint32_t>> const& beatWeights,
                                  std::vector<BarrensBeat> const& eligibleBeats,
                                  uint32_t rollVal);

    // Proposal prompt & validation
    std::string BuildBarrensTopicProposalPrompt(std::string const& family,
                                                std::vector<std::string> const& recentPremises);

    bool ValidateBarrensTopicProposal(std::string const& rawResponse,
                                      std::string const& expectedFamily,
                                      std::string& outPremise,
                                      std::string& outOpeningIntent,
                                      std::string& outError);

    // Normalization & novelty
    std::string NormalizeBarrensPremise(std::string const& text);
    double CalculateBarrensLexicalSimilarity(std::string const& normA, std::string const& normB);

    struct BarrensNoveltyEntry
    {
        std::string normalizedPremise;
        std::chrono::steady_clock::time_point timestamp;
    };

    class BarrensNoveltyRing
    {
    public:
        explicit BarrensNoveltyRing(size_t maxEntries = 100) : m_maxEntries(maxEntries) {}

        void Prune(std::chrono::steady_clock::time_point now, uint32_t windowSeconds);
        bool IsNovel(std::string const& normalizedPremise,
                     std::chrono::steady_clock::time_point now,
                     uint32_t windowSeconds,
                     double similarityThreshold = 0.60) const;
        void Record(std::string const& normalizedPremise, std::chrono::steady_clock::time_point now);
        std::vector<std::string> GetRecentPremises(size_t maxCount) const;
        size_t Size() const { return m_entries.size(); }

    private:
        size_t m_maxEntries = 100;
        std::vector<BarrensNoveltyEntry> m_entries;
    };

    // Prompt construction & output validation
    std::string BuildBarrensSystemPromptExtension(BarrensLinePlan const& plan,
                                                  uint32_t maxCharacters);

    std::string BuildBarrensContextBlock(std::vector<BarrensTranscriptEntry> const& transcript,
                                         uint32_t maxContextMessages,
                                         std::string const& topicPremise,
                                         std::string const& openingIntent,
                                         BarrensBeat beat,
                                         uint32_t remainingTurns);

    bool ValidateBarrensMessage(std::string const& rawText,
                                uint32_t maxCharacters,
                                std::string& cleaned);
}
