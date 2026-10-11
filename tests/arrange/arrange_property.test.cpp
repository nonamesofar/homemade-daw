// Task 2.3: random sequences of arrangement edits, undo and redo on a real engine. After every step the invariants hold,
// and undoing an edit returns the saved form of the project to exactly what it was before it (undo-redo spec).
#include "ArrangeFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;

namespace
{
/** One random edit, or nothing when it does not apply (no clips yet). */
std::unique_ptr<cmd::Command> randomEdit(std::mt19937& rng, sampler::engine::Arrangement& a, const std::vector<juce::String>& sources)
{
    const auto clips = a.clips();
    const auto tracks = a.numTracks();
    const auto pick = [&rng](int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); };
    const auto beat = [&rng] { return std::uniform_int_distribution<int>(0, 48)(rng) * 0.5; };
    const auto anyClip = [&]() { return clips[static_cast<size_t>(pick(static_cast<int>(clips.size())))]; };

    const auto kind = clips.empty() ? 0 : pick(8);
    switch (kind)
    {
        case 0:
            return std::make_unique<cmd::CreateClipsFromSources>(
                std::vector<juce::String>{sources[static_cast<size_t>(pick(static_cast<int>(sources.size())))]},
                cmd::PlacementTarget{pick(tracks), pick(6) == 0, beat()});
        case 1:
        {
            std::vector<cmd::ClipMove> moves;
            const auto c = anyClip();
            moves.push_back({c.id, pick(tracks), beat()});
            if (clips.size() > 1 && pick(2) == 0)
            {
                const auto d = anyClip();
                if (d.id != c.id)
                    moves.push_back({d.id, pick(tracks), beat()});
            }
            return std::make_unique<cmd::MoveClips>(moves);
        }
        case 2:
        {
            const auto c = anyClip();
            return std::make_unique<cmd::TrimClip>(c.id, c.start + (pick(5) - 2) * 0.5, c.end() + (pick(5) - 2) * 0.5, 0.5);
        }
        case 3:
        {
            const auto c = anyClip();
            return std::make_unique<cmd::SplitClips>(std::vector<juce::String>{c.id}, c.start + c.length * 0.4);
        }
        case 4: return std::make_unique<cmd::DuplicateClips>(std::vector<juce::String>{anyClip().id});
        case 5: return std::make_unique<cmd::DeleteClips>(std::vector<juce::String>{anyClip().id});
        case 6: return std::make_unique<cmd::SetClipStart>(anyClip().id, beat());
        default:
        {
            const auto c = anyClip();
            std::vector<cmd::ClipRecord> records(1);
            records[0].sourceId = c.sourceId;
            records[0].name = c.name;
            records[0].lengthSeconds = c.lengthSeconds;
            records[0].offsetSeconds = c.offsetSeconds;
            return std::make_unique<cmd::PasteClips>(records, pick(tracks), beat());
        }
    }
}
} // namespace

TEST_CASE("random arrangement edits keep the invariants and undo restores the saved form", "[arrange][commands][property]")
{
    ArrangeFixture f("property");
    auto& a = f.arrangement();
    const std::vector<juce::String> sources{f.addSound("one", 0.5), f.addSound("two", 2.0, "ramp"), f.addSound("three", 3.25)};
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::AddTrack>());

    // 300 in CI; SAMPLER_PROPERTY_SEQUENCES changes it for a quick local run.
    const int sequences = juce::SystemStats::getEnvironmentVariable("SAMPLER_PROPERTY_SEQUENCES", "300").getIntValue();
    constexpr int stepsPerSequence = 6;
    std::mt19937 rng(20261011);
    int edits = 0, undoChecks = 0;
    const bool verbose = juce::SystemStats::getEnvironmentVariable("SAMPLER_PROPERTY_VERBOSE", {}).isNotEmpty();
    for (int sequence = 0; sequence < sequences; ++sequence)
    {
        if (verbose)
        {
            std::printf("sequence %d: %d clips", sequence, a.numClips());
            std::puts("");
            std::fflush(stdout);
        }
        // Non-dyadic tempos make clip edges read back with float noise: every 25 sequences use another one.
        if (sequence % 25 == 0)
        {
            static constexpr double tempos[] = {120.0, 90.0, 97.3};
            f.commands->apply(std::make_unique<cmd::SetTempo>(tempos[(sequence / 25) % 3]));
            REQUIRE(f.violations.isEmpty());
        }
        for (int step = 0; step < stepsPerSequence; ++step)
        {
            const auto roll = std::uniform_int_distribution<int>(0, 9)(rng);
            if (roll == 0 && f.commands->canUndo())
            {
                f.commands->undo();
                continue;
            }
            if (roll == 1 && f.commands->canRedo())
            {
                f.commands->redo();
                continue;
            }
            auto edit = randomEdit(rng, a, sources);
            if (verbose)
            {
                std::printf("  %s", edit->name().toRawUTF8());
                std::puts("");
                std::fflush(stdout);
            }
            const auto before = a.serialise();
            const auto stepsBefore = f.project->undoManager().getUndoDescriptions().size();
            f.commands->apply(*edit);
            ++edits;
            INFO("sequence " << sequence << " step " << step << ": " << edit->name());
            REQUIRE(f.violations.isEmpty());
            if (f.project->undoManager().getUndoDescriptions().size() == stepsBefore)
                continue; // the edit changed nothing (for example a trim against its limits)
            const auto after = a.serialise();
            f.commands->undo();
            REQUIRE(a.serialise() == before);
            f.commands->redo();
            REQUIRE(a.serialise() == after);
            REQUIRE(f.violations.isEmpty());
            ++undoChecks;
        }
        // Keep the history short so the serialised forms stay small: start over from an empty arrangement now and then.
        if (sequence % 25 == 24)
            for (const auto& c : a.clips())
                f.commands->apply(std::make_unique<cmd::DeleteClips>(std::vector<juce::String>{c.id}));
    }
    std::printf("property test: %d edits, %d undo/redo round trips\n", edits, undoChecks);
    CHECK(undoChecks > sequences / 2);
}
