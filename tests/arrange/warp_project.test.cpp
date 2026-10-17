// M4a task 1.4: the warp settings of a clip in the saved project, and projects from before warping.
#include "WarpTestSupport.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;
using namespace sampler::test;

namespace
{
WarpSettings warpOn(WarpMode mode, double bpm, double transpose = 0.0)
{
    WarpSettings w;
    w.on = true;
    w.mode = mode;
    w.sourceBpm = bpm;
    w.transpose = transpose;
    return w;
}

std::unique_ptr<sampler::ProjectEdit> reopen(ArrangeFixture& f)
{
    const auto bundle = f.project->bundleDir();
    f.commands.reset();
    f.project.reset();
    juce::String error;
    auto project = sampler::ProjectEdit::open(f.host, bundle, error);
    REQUIRE(project != nullptr);
    return project;
}
} // namespace

TEST_CASE("a project saved before warping opens with every clip on WARP off and is not changed by opening",
          "[arrange][warp][project]")
{
    ArrangeFixture f("warp-m3-project");
    const auto sound = addFixtureSound(f, "break-90.wav");
    // What M3 wrote: an ordinary clip, and a slice copy with the saved pitch of its slice (sampler_pitch) and no warp properties.
    f.project->undoManager().beginNewTransaction("clips");
    f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0));
    auto slice = specFor(f, sound, 16.0);
    slice.lengthSeconds = 0.5;
    slice.originSourceId = sound;
    slice.originSlice = 3;
    slice.pitch = 5.0;
    f.arrangement().createClip(f.trackId(0), slice);
    REQUIRE(f.project->save());
    const auto fileBefore = f.project->editFile().loadFileAsString();
    CHECK_FALSE(fileBefore.contains("sampler_warp"));

    auto project = reopen(f);
    const auto clips = project->arrangement().clips();
    REQUIRE(clips.size() == 2);
    for (const auto& clip : clips)
    {
        CHECK_FALSE(clip.warp.on);
        CHECK_FALSE(clip.warp.active());
        CHECK_FALSE(clip.rendering);
        CHECK_FALSE(clip.loop);
        CHECK(clip.fadeIn == 0.0);
        CHECK(clip.fadeOut == 0.0);
        CHECK(clip.coveredSeconds() == Approx(clip.lengthSeconds));
    }
    CHECK(clips[0].warp.transpose == 0.0);
    CHECK(clips[1].warp.transpose == 5.0); // the saved slice pitch is the initial transpose
    CHECK(clips[1].pitch == 5.0);
    CHECK(clips[0].lengthSeconds == Approx(470400.0 / 44100.0));

    // Opening changed nothing: no undo step, and saving gives the same project file.
    CHECK_FALSE(project->undoManager().canUndo());
    CHECK(project->undoManager().getNumActionsInCurrentTransaction() == 0);
    REQUIRE(project->save());
    CHECK(project->editFile().loadFileAsString().contains("sampler_pitch=\"5.0\""));
    CHECK_FALSE(project->editFile().loadFileAsString().contains("sampler_warp"));
    CHECK_FALSE(project->editFile().loadFileAsString().contains("sampler_transpose"));
}

TEST_CASE("warp settings round-trip through save and reopen", "[arrange][warp][project]")
{
    ArrangeFixture f("warp-roundtrip");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("clips");
    auto& a = f.arrangement();
    const auto beats = a.createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::beats, 90.0, 3.0)));
    const auto tones = a.createClip(f.trackId(0), specFor(f, sound, 16.0, warpOn(WarpMode::tones, 92.5, -7.0)));
    const auto repitch = a.createClip(f.trackId(0), specFor(f, sound, 32.0, warpOn(WarpMode::repitch, 88.0)));
    auto off = warpOn(WarpMode::tones, 90.0, 2.0);
    off.on = false; // kept values with WARP off
    const auto unwarped = a.createClip(f.trackId(0), specFor(f, sound, 48.0));
    a.setClipWarp(unwarped, off);
    const auto before = a.clips();
    REQUIRE(before.size() == 4);
    REQUIRE(f.project->save());

    auto project = reopen(f);
    const auto after = project->arrangement().clips();
    REQUIRE(after.size() == 4);
    for (size_t i = 0; i < after.size(); ++i)
    {
        INFO("clip " << i);
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].warp == before[i].warp);
        CHECK(after[i].start == Approx(before[i].start));
        CHECK(after[i].length == Approx(before[i].length));
        CHECK(after[i].offsetSeconds == Approx(before[i].offsetSeconds));
    }
    const auto find = [&](const juce::String& id) { return *project->arrangement().clip(id); };
    CHECK(find(beats).warp == warpOn(WarpMode::beats, 90.0, 3.0));
    CHECK(find(tones).warp == warpOn(WarpMode::tones, 92.5, -7.0));
    CHECK(find(repitch).warp.mode == WarpMode::repitch);
    CHECK_FALSE(find(unwarped).warp.on);
    CHECK(find(unwarped).warp.mode == WarpMode::tones);
    CHECK(find(unwarped).warp.sourceBpm == 90.0);
    CHECK(find(unwarped).warp.transpose == 2.0);
    // The Beats clip points at its render in the cache by a path relative to the project.
    const auto text = project->editFile().loadFileAsString();
    CHECK(text.replace("\\", "/").contains("source=\"cache/beats/"));
    CHECK_FALSE(text.contains(project->bundleDir().getFullPathName()));
    CHECK_FALSE(project->undoManager().canUndo());
}
