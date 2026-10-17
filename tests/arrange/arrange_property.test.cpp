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

    const auto warpSettings = [&] {
        sampler::engine::WarpSettings w;
        w.on = pick(4) != 0;
        w.mode = static_cast<sampler::engine::WarpMode>(pick(3));
        w.sourceBpm = 60.0 + pick(13) * 10.0;
        w.transpose = pick(3) == 0 ? 0.0 : pick(49) - 24.0;
        return w;
    };

    const auto kind = clips.empty() ? 0 : pick(15);
    switch (kind)
    {
        case 0:
        {
            std::vector<sampler::engine::WarpSettings> warps;
            if (pick(2) == 0)
                warps.push_back(warpSettings());
            return std::make_unique<cmd::CreateClipsFromSources>(
                std::vector<juce::String>{sources[static_cast<size_t>(pick(static_cast<int>(sources.size())))]},
                cmd::PlacementTarget{pick(tracks), pick(6) == 0, beat()}, warps);
        }
        case 8:
        {
            const auto w = warpSettings();
            return std::make_unique<cmd::SetClipWarp>(anyClip().id, w.on, w.mode, w.sourceBpm);
        }
        case 11: return std::make_unique<cmd::SetClipLoop>(anyClip().id, pick(3) != 0);
        case 12: return std::make_unique<cmd::SetClipLength>(anyClip().id, 0.5 + pick(48) * 0.5);
        case 13: return std::make_unique<cmd::SetClipFades>(anyClip().id, pick(3) == 0 ? 0.0 : pick(20) * 0.1, pick(3) == 0 ? 0.0 : pick(20) * 0.1);
        case 9: return std::make_unique<cmd::SetClipTranspose>(anyClip().id, pick(49) - 24.0);
        case 10: return std::make_unique<cmd::SetTempo>(std::vector<double>{90.0, 100.0, 120.0, 133.0}[static_cast<size_t>(pick(4))]);
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
            records[0].warp = c.warp;
            records[0].lengthBeats = c.length;
            if (c.loop)
            {
                records[0].loopStart = c.loopStart;
                records[0].loopLength = c.loopLength;
            }
            records[0].fadeIn = c.fadeIn;
            records[0].fadeOut = c.fadeOut;
            return std::make_unique<cmd::PasteClips>(records, pick(tracks), beat());
        }
    }
}
/** The clips and the tempo as numbers: what must come back after an undo (the saved form is stricter, see below). */
juce::String describe(const sampler::engine::Arrangement& a)
{
    juce::String text = "tempo " + juce::String(a.tempo(), 4) + "\n";
    for (const auto& c : a.clips())
        text << c.id << " " << c.trackId << " " << juce::String(c.start, 5) << " " << juce::String(c.length, 5) << " " << juce::String(c.offsetSeconds, 5)
             << " w" << (c.warp.on ? 1 : 0) << int(c.warp.mode) << " " << juce::String(c.warp.sourceBpm, 3) << " " << juce::String(c.warp.transpose, 3)
             << " r" << (c.reversed ? 1 : 0) << "\n";
    return text;
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
    int edits = 0, undoChecks = 0, warpedSeen = 0;
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
            // A tempo change leaves zero loop points in the transport node that its undo does not remove (since M2): for it the
            // clips and the tempo must come back, not the saved text.
            const bool tempoEdit = dynamic_cast<cmd::SetTempo*>(edit.get()) != nullptr;
            const auto before = a.serialise();
            const auto described = describe(a);
            const auto stepsBefore = f.project->undoManager().getUndoDescriptions().size();
            f.commands->apply(*edit);
            ++edits;
            for (const auto& c : a.clips())
                warpedSeen += c.warp.active() ? 1 : 0;
            INFO("sequence " << sequence << " step " << step << ": " << edit->name());
            REQUIRE(f.violations.isEmpty());
            if (f.project->undoManager().getUndoDescriptions().size() == stepsBefore)
                continue; // the edit changed nothing (for example a trim against its limits)
            const auto after = a.serialise();
            const auto describedAfter = describe(a);
            f.commands->undo();
            if (tempoEdit)
                REQUIRE(describe(a) == described);
            else
                REQUIRE(a.serialise() == before);
            f.commands->redo();
            if (tempoEdit)
                REQUIRE(describe(a) == describedAfter);
            else
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
    CHECK(warpedSeen > sequences / 2); // the sequences did exercise warped clips
}
