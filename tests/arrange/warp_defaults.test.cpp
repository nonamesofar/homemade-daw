// M4a task 1.8: the callers choose the warp settings of new clips (browser drop, Explorer drop, Slices to new track).
#include "ViewHarness.h"
#include "WarpTestSupport.h"

#include "commands/SliceCommands.h"
#include "io/WarpDefaults.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::engine::WarpMode;
using namespace sampler::test;
namespace cmd = sampler::commands;
namespace io = sampler::io;

namespace
{
io::SoundAnalysis analysisAt(double bpm)
{
    io::SoundAnalysis a;
    if (bpm > 0.0)
        a.tempo.push_back({bpm, 1.0f});
    return a;
}

sampler::model::Source sourceOf(ArrangeFixture& f, const juce::String& id)
{
    return sampler::model::SamplerState(f.project->samplerState()).findSourceById(id);
}
} // namespace

TEST_CASE("the default warp is Beats at the sound's tempo for a loop with a known tempo, else off", "[arrange][warp][defaults]")
{
    ArrangeFixture f("wd-rules");
    const auto loop = addFixtureSound(f, "break-90.wav");
    const auto kick = addFixtureSound(f, "kick.wav", sampler::model::Category::oneShot);
    const auto take = addFixtureSound(f, "tone-440-100.wav", sampler::model::Category::recording);

    const auto ninety = analysisAt(90.0);
    const auto w = io::defaultWarpFor(sourceOf(f, loop), &ninety);
    CHECK(w.on);
    CHECK(w.mode == WarpMode::beats);
    CHECK(w.sourceBpm == 90.0);

    CHECK_FALSE(io::defaultWarpFor(sourceOf(f, loop), nullptr).on);                  // analysis not ready
    const auto none = analysisAt(0.0);
    CHECK_FALSE(io::defaultWarpFor(sourceOf(f, loop), &none).on);                    // no tempo found
    CHECK_FALSE(io::defaultWarpFor(sourceOf(f, kick), &ninety).on);                  // one-shot
    CHECK_FALSE(io::defaultWarpFor(sourceOf(f, take), &ninety).on);                  // a take
    CHECK_FALSE(io::defaultWarpFor(sampler::model::Source(), &ninety).on);           // no such sound

    // The user's correction wins over the analysis, and gives a tempo where the analysis found none.
    f.commands->apply(std::make_unique<cmd::SetSourceTempo>(loop, 180.0));
    CHECK(io::defaultWarpFor(sourceOf(f, loop), &ninety).sourceBpm == 180.0);
    CHECK(io::defaultWarpFor(sourceOf(f, loop), &none).sourceBpm == 180.0);
}

TEST_CASE("a loop dropped from the browser at 120 BPM gets Beats at its tempo; a kick gets WARP off", "[arrange][warp][defaults]")
{
    sampler::test::ViewHarness h("wd-drop");
    const auto loop = addFixtureSound(h.f, "break-90.wav");
    const auto kick = addFixtureSound(h.f, "kick.wav", sampler::model::Category::oneShot);
    h.defaultWarp = [&](const juce::String& id) {
        const auto analysis = analysisAt(90.0);
        return io::defaultWarpFor(sourceOf(h.f, id), &analysis);
    };
    h.sync();

    auto& lanes = h.view->lanes();
    const auto drop = [&](const juce::String& id, double beat) {
        juce::DragAndDropTarget::SourceDetails details("sample:" + id, nullptr, {static_cast<int>(h.xOfBeat(beat)), static_cast<int>(h.yInTrack(0))});
        lanes.itemDropped(details);
        h.sync();
    };
    drop(loop, 0.0);
    drop(kick, 32.0);

    const auto clips = h.f.arrangement().clipsOnTrack(h.f.trackId(0));
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].warp.on);
    CHECK(clips[0].warp.mode == WarpMode::beats);
    CHECK(clips[0].warp.sourceBpm == 90.0);
    CHECK(clips[0].length == Approx(16.0));
    CHECK_FALSE(clips[1].warp.on);
    CHECK(h.f.violations.isEmpty());
}

TEST_CASE("sounds placed by an Explorer drop before their analysis have WARP off", "[arrange][warp][defaults]")
{
    ArrangeFixture f("wd-explorer");
    const auto loop = addFixtureSound(f, "break-90.wav");
    cmd::ImportAndPlace command({}, {loop}, {0, false, 0.0}); // no warp settings: the analysis did not exist when it was dropped
    f.commands->apply(command);
    REQUIRE(command.resultIds().size() == 1);
    CHECK_FALSE(f.arrangement().clip(command.resultIds().front())->warp.on);
}

TEST_CASE("16 quarter-note slices of break-90 at 120 BPM cover bars 1 to 4, each on a beat; a slice pitch is the transpose",
          "[arrange][warp][defaults][slices]")
{
    ArrangeFixture f("wd-slices");
    const auto loop = addFixtureSound(f, "break-90.wav");
    for (int k = 1; k < 16; ++k)
        f.commands->apply(std::make_unique<cmd::AddSlicePoint>(loop, static_cast<juce::int64>(k) * 29400));
    f.commands->apply(std::make_unique<cmd::SetSliceParam>(loop, 2, cmd::SetSliceParam::Param::pitch, 5.0)); // "slice 3"

    const auto analysis = analysisAt(90.0);
    cmd::SlicesToTrack command(loop, 0.0, io::defaultWarpFor(sourceOf(f, loop), &analysis));
    f.commands->apply(command);
    REQUIRE(command.resultIds().size() == 16);
    const auto clips = f.arrangement().clipsOnTrack(command.trackId());
    REQUIRE(clips.size() == 16);
    for (size_t i = 0; i < clips.size(); ++i)
    {
        INFO("slice " << i);
        CHECK(clips[i].warp.active());
        CHECK(clips[i].warp.mode == WarpMode::beats);
        CHECK(clips[i].start == Approx(static_cast<double>(i)));
        CHECK(clips[i].length == Approx(1.0));
        CHECK(clips[i].offsetSeconds == Approx(static_cast<double>(i) * 29400.0 / 44100.0));
        CHECK(clips[i].originSlice == static_cast<int>(i));
        CHECK(clips[i].warp.transpose == (i == 2 ? 5.0 : 0.0));
    }
    CHECK(f.arrangement().endBeat() == Approx(16.0)); // bars 1 to 4
    CHECK(f.violations.isEmpty());

    // The slices keep their bars when the tempo changes.
    f.commands->apply(std::make_unique<cmd::SetTempo>(100.0));
    CHECK(f.arrangement().endBeat() == Approx(16.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("slices without warp settings keep the M3 behaviour", "[arrange][warp][defaults][slices]")
{
    ArrangeFixture f("wd-slices-off");
    const auto loop = addFixtureSound(f, "break-90.wav");
    f.commands->apply(std::make_unique<cmd::AddSlicePoint>(loop, 29400));
    cmd::SlicesToTrack command(loop, 0.0);
    f.commands->apply(command);
    const auto clips = f.arrangement().clipsOnTrack(command.trackId());
    REQUIRE(clips.size() == 2);
    CHECK_FALSE(clips[0].warp.on);
    CHECK(clips[0].lengthSeconds == Approx(29400.0 / 44100.0));
    CHECK(clips[0].length == Approx(29400.0 / 44100.0 * 2.0));
}
