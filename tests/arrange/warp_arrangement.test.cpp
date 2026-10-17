// M4a task 1.5: clip warp in the arrangement facade (WARP off, Re-pitch, Tones, Beats), checked by offline renders.
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

void setTempo(ArrangeFixture& f, double bpm) { f.commands->apply(std::make_unique<sampler::commands::SetTempo>(bpm)); }
} // namespace

TEST_CASE("an unwarped clip leaves no warp properties and reads as WARP off", "[arrange][warp]")
{
    ArrangeFixture f("warp-off");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto clipId = f.place(sound, 0, 0.0);
    const auto info = f.arrangement().clip(clipId);
    REQUIRE(info.has_value());
    CHECK_FALSE(info->warp.on);
    CHECK(info->warp.mode == WarpMode::beats);
    CHECK(info->warp.transpose == 0.0);
    CHECK_FALSE(info->warp.active());
    CHECK(info->lengthSeconds == Approx(9.6));
    CHECK(info->coveredSeconds() == Approx(9.6));
    CHECK_FALSE(info->rendering);
    REQUIRE(f.project->save());
    const auto text = f.project->editFile().loadFileAsString();
    CHECK_FALSE(text.contains("sampler_warp"));
    CHECK_FALSE(text.contains("sampler_sourceBpm"));
    CHECK_FALSE(text.contains("cache/beats"));
}

TEST_CASE("Re-pitch: a 440 Hz tone at 100 BPM in a 200 BPM project is 880 Hz, half as long, and starts on its sample",
          "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-repitch");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    setTempo(f, 200.0);
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 4.0, warpOn(WarpMode::repitch, 100.0)));
    REQUIRE(clipId.isNotEmpty());

    const auto info = *f.arrangement().clip(clipId);
    CHECK(info.warp.active());
    CHECK(info.start == Approx(4.0));
    CHECK(info.length == Approx(16.0)); // 4 bars of the sound at its own tempo
    CHECK(info.lengthSeconds == Approx(4.8));
    CHECK(info.offsetSeconds == Approx(0.0));
    CHECK_FALSE(info.rendering);

    double rate = 0.0;
    const auto out = renderProject(f, "repitch", &rate);
    REQUIRE(rate == 48000.0);
    const auto first = firstAbove(out, 1.0e-4f);
    CHECK(std::abs(first - 57600) <= 2); // beat 4 at 200 BPM = 1.2 s
    CHECK(std::abs(lastAbove(out, 1.0e-4f) - (57600 + 230400)) <= 2);
    CHECK(frequencyBetween(out, rate, 2.0, 5.0) == Approx(880.0).margin(1.0));
}

TEST_CASE("Re-pitch with an offset into the sound", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-repitch-offset");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    setTempo(f, 200.0);
    f.project->undoManager().beginNewTransaction("create");
    auto spec = specFor(f, sound, 0.0, warpOn(WarpMode::repitch, 100.0));
    spec.offsetSeconds = 2.4;   // one bar in
    spec.lengthSeconds = 4.8;   // two bars of the sound
    const auto clipId = f.arrangement().createClip(f.trackId(0), spec);
    const auto info = *f.arrangement().clip(clipId);
    CHECK(info.offsetSeconds == Approx(2.4));
    CHECK(info.length == Approx(8.0));
    CHECK(info.lengthSeconds == Approx(2.4));
    // The offset is the same sound seconds after a tempo change.
    setTempo(f, 100.0);
    const auto later = *f.arrangement().clip(clipId);
    CHECK(later.offsetSeconds == Approx(2.4));
    CHECK(later.length == Approx(8.0));
    CHECK(later.lengthSeconds == Approx(4.8));
}

TEST_CASE("Tones: a 440 Hz tone at 100 BPM keeps its pitch and fills 4 bars at 130 BPM", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-tones");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    setTempo(f, 130.0);
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::tones, 100.0)));
    REQUIRE(clipId.isNotEmpty());

    const auto info = *f.arrangement().clip(clipId);
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(16.0 * 60.0 / 130.0).margin(1.0e-6));

    double rate = 0.0;
    const auto out = renderProject(f, "tones", &rate);
    CHECK(frequencyBetween(out, rate, 1.0, 6.0) == Approx(440.0).margin(1.0));
    CHECK(lastAbove(out, 0.02f) / rate == Approx(16.0 * 60.0 / 130.0).margin(0.05));
}

TEST_CASE("Tones: transpose +7 gives about 659 Hz and the same length; the length in bars survives a tempo change",
          "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-tones-transpose");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    setTempo(f, 130.0);
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::tones, 100.0, 7.0)));
    double rate = 0.0;
    const auto out = renderProject(f, "tones7", &rate);
    CHECK(frequencyBetween(out, rate, 1.0, 6.0) == Approx(659.26).margin(2.5));
    CHECK(lastAbove(out, 0.02f) / rate == Approx(16.0 * 60.0 / 130.0).margin(0.05));

    setTempo(f, 100.0);
    const auto info = *f.arrangement().clip(clipId);
    CHECK(info.start == Approx(0.0));
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(9.6).margin(1.0e-6));
}

TEST_CASE("Tones with an offset keeps the same part of the sound across a tempo change", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-tones-offset");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    f.project->undoManager().beginNewTransaction("create");
    auto spec = specFor(f, sound, 0.0, warpOn(WarpMode::tones, 100.0));
    spec.offsetSeconds = 2.4;
    spec.lengthSeconds = 4.8;
    const auto clipId = f.arrangement().createClip(f.trackId(0), spec);
    CHECK(f.arrangement().clip(clipId)->offsetSeconds == Approx(2.4));
    setTempo(f, 150.0);
    const auto info = *f.arrangement().clip(clipId);
    CHECK(info.offsetSeconds == Approx(2.4).margin(1.0e-6));
    CHECK(info.length == Approx(8.0));
    CHECK(info.lengthSeconds == Approx(8.0 * 60.0 / 150.0).margin(1.0e-6));
}

TEST_CASE("Beats: break-90 at 120 BPM covers bars 1 to 4, silent until its render exists", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-beats");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::beats, 90.0)));
    REQUIRE(clipId.isNotEmpty());

    auto info = *f.arrangement().clip(clipId);
    CHECK(info.length == Approx(16.0)); // 4 bars
    CHECK(info.lengthSeconds == Approx(8.0));
    CHECK_FALSE(info.missing);
    CHECK(info.rendering);
    CHECK_FALSE(f.arrangement().allRendersReady());
    const auto requests = f.arrangement().missingBeatsRenders();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].clipId == clipId);
    CHECK(requests[0].destination.getParentDirectory().getFullPathName().replace("\\", "/").endsWith("cache/beats"));
    CHECK(requests[0].params.sourceBpm == 90.0);
    CHECK(requests[0].params.projectBpm == 120.0);

    REQUIRE(waitForBeatsRenders(f));
    CHECK(numBeatsFiles(f) == 1);
    info = *f.arrangement().clip(clipId);
    CHECK_FALSE(info.rendering);
    CHECK(f.arrangement().allRendersReady());
    CHECK(f.arrangement().missingBeatsRenders().empty());

    double rate = 0.0;
    const auto out = renderProject(f, "beats", &rate);
    // The loop's last hit starts at beat 15 of the sound (the sound's 100 ms lead scaled to 75 ms); the render ends with bar 4.
    CHECK(lastAbove(out, 0.01f) / rate > 7.5);
    CHECK(lastAbove(out, 0.01f) / rate < 8.0 + 0.05);
    // The kick on the fifth beat of the sound is on the fifth beat of the song (2 s), 75 ms late like all hits.
    const auto kick = firstAbove(out, 0.05f, static_cast<int>(2.0 * rate));
    CHECK(std::abs(kick / rate - (2.0 + 0.075)) < 0.002);
}

TEST_CASE("a Beats clip is not missing when its render is, and is missing when its sound is", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-beats-missing");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::beats, 90.0)));
    REQUIRE(waitForBeatsRenders(f));
    REQUIRE_FALSE(f.arrangement().clip(clipId)->rendering);

    const auto requests = f.arrangement().missingBeatsRenders();
    CHECK(requests.empty());
    // Delete the render: still not missing, but rendering and requested again.
    auto renders = f.project->bundleDir().getChildFile("cache/beats").findChildFiles(juce::File::findFiles, false, "*.wav");
    REQUIRE(renders.size() == 1);
    REQUIRE(renders.getFirst().deleteFile());
    auto info = *f.arrangement().clip(clipId);
    CHECK_FALSE(info.missing);
    CHECK(info.rendering);
    CHECK(f.arrangement().missingBeatsRenders().size() == 1);

    // Delete the sound: missing.
    REQUIRE(f.project->bundleDir().getChildFile("audio/break-90.wav").deleteFile());
    info = *f.arrangement().clip(clipId);
    CHECK(info.missing);
    CHECK(f.arrangement().missingBeatsRenders().empty()); // nothing to render from
}

TEST_CASE("a tempo change keeps a warped clip's bars and gives a Beats clip another render; undo needs no new render",
          "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-beats-tempo");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 8.0, warpOn(WarpMode::beats, 90.0)));
    REQUIRE(waitForBeatsRenders(f));
    const auto before = f.arrangement().serialise();

    setTempo(f, 100.0);
    auto info = *f.arrangement().clip(clipId);
    CHECK(info.start == Approx(8.0));
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(9.6).margin(1.0e-6));
    CHECK(info.rendering); // the render for 100 BPM is not there yet
    REQUIRE(waitForBeatsRenders(f));
    CHECK(numBeatsFiles(f) == 2);
    CHECK_FALSE(f.arrangement().clip(clipId)->rendering);

    f.commands->undo();
    info = *f.arrangement().clip(clipId);
    CHECK(info.lengthSeconds == Approx(8.0));
    CHECK_FALSE(info.rendering); // the 120 BPM render is still in the cache
    CHECK(f.arrangement().missingBeatsRenders().empty());
    CHECK(firstDifference(f.arrangement().serialise(), before) == "");
}

TEST_CASE("setClipWarp keeps the start and the part of the sound; WARP off gives 5.33 bars, and undo restores the edit",
          "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-toggle");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 8.0, warpOn(WarpMode::beats, 90.0)));
    REQUIRE(waitForBeatsRenders(f));
    const auto warped = f.arrangement().serialise();

    f.project->undoManager().beginNewTransaction("warp off");
    auto w = f.arrangement().clip(clipId)->warp;
    w.on = false;
    f.arrangement().setClipWarp(clipId, w);
    auto info = *f.arrangement().clip(clipId);
    CHECK_FALSE(info.warp.active());
    CHECK(info.warp.mode == WarpMode::beats);   // kept for when it is turned on again
    CHECK(info.warp.sourceBpm == 90.0);
    CHECK(info.start == Approx(8.0));
    CHECK(info.lengthSeconds == Approx(470400.0 / 44100.0));
    CHECK(info.length == Approx(470400.0 / 44100.0 * 2.0)); // 21.33 beats = 5.33 bars
    CHECK_FALSE(info.rendering);
    const auto off = f.arrangement().serialise();
    CHECK(off != warped);

    for (const auto mode : {WarpMode::tones, WarpMode::repitch, WarpMode::beats})
    {
        f.project->undoManager().beginNewTransaction("mode");
        w.on = true;
        w.mode = mode;
        f.arrangement().setClipWarp(clipId, w);
        info = *f.arrangement().clip(clipId);
        CHECK(info.warp.mode == mode);
        CHECK(info.start == Approx(8.0));
        CHECK(info.length == Approx(16.0));
    }
    f.project->undoManager().beginNewTransaction("transpose");
    f.arrangement().setClipTranspose(clipId, 5.0);
    CHECK(f.arrangement().clip(clipId)->warp.transpose == 5.0);
    CHECK(f.arrangement().clip(clipId)->rendering); // another render (+5 st)
    f.arrangement().setClipTranspose(clipId, 99.0);
    CHECK(f.arrangement().clip(clipId)->warp.transpose == 24.0);

    // Undo everything back to the first warped state.
    while (f.project->undoManager().canUndo() && f.arrangement().serialise() != warped)
        f.project->undoManager().undo();
    CHECK(firstDifference(f.arrangement().serialise(), warped) == "");
    f.project->undoManager().redo(); // warp off again
    CHECK(firstDifference(f.arrangement().serialise(), off) == "");
}

TEST_CASE("reversing a Beats clip plays a render of the reversed sound", "[arrange][warp][engine]")
{
    ArrangeFixture f("warp-beats-reverse");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, warpOn(WarpMode::beats, 90.0)));
    const auto forward = f.arrangement().serialise();
    f.project->undoManager().beginNewTransaction("reverse");
    f.arrangement().setClipReversed(clipId, true);
    auto info = *f.arrangement().clip(clipId);
    CHECK(info.reversed);
    CHECK(info.rendering);
    CHECK(info.offsetSeconds == Approx(0.0).margin(1.0e-9));
    CHECK(f.arrangement().missingBeatsRenders().size() == 1);
    CHECK(f.arrangement().missingBeatsRenders()[0].params.reverse);
    f.project->undoManager().undo();
    CHECK(firstDifference(f.arrangement().serialise(), forward) == "");
    CHECK_FALSE(f.arrangement().clip(clipId)->reversed);
}
