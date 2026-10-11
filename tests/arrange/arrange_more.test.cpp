// M2: save and reopen, missing files, tracks, mixer, gestures, loop, metronome, tempo, time signature and clip settings
// on a real (headless) engine, with offline renders where the sound itself is the thing to check.
#include "ArrangeFixture.h"

#include "engine/OfflineRender.h"

#include <catch2/catch_approx.hpp>

#include <limits>

#include <juce_cryptography/juce_cryptography.h>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;
namespace test = sampler::test;

namespace
{
juce::AudioBuffer<float> render(ArrangeFixture& f, const juce::String& name, double* rate = nullptr)
{
    const auto out = f.root.getChildFile(name + ".wav");
    juce::String error;
    sampler::engine::RenderOptions options;
    REQUIRE(sampler::engine::renderToWav(*f.project, out, options, error));
    INFO(error);
    return test::readWav(out, rate);
}

/** Peak of a channel over the middle of [startSec, endSec) (away from the declick fades). */
float peakBetween(const juce::AudioBuffer<float>& b, int channel, double startSec, double endSec, double rate = 48000.0)
{
    const auto a = static_cast<int>((startSec + 0.02) * rate);
    const auto e = static_cast<int>((endSec - 0.02) * rate);
    return test::peakOf(b, channel, a, e - a);
}
} // namespace

TEST_CASE("save and reopen keep tracks and clips; a missing decoded file keeps its clip, silent and reported",
          "[arrange][project]")
{
    ArrangeFixture f("more-roundtrip");
    const auto kick = f.addSound("kick", 1.0);
    const auto pad = f.addSound("pad", 2.0);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::RenameTrack>(f.trackId(1), "Pads"));
    f.place(kick, 0, 0.0);
    f.place(kick, 0, 4.0);
    const auto padClip = f.place(pad, 1, 8.0);
    f.commands->apply(std::make_unique<cmd::SetClipGain>(padClip, -6.0));
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), cmd::SetTrackParam::Control::volume, -3.0));
    f.commands->apply(std::make_unique<cmd::SetTempo>(100.0));
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{4.0, 12.0}));
    f.project->transport().setLooping(true);
    f.project->transport().setClickOn(true);
    const auto bundle = f.root.getChildFile("Trip.sdaw");
    juce::String error;
    REQUIRE(f.project->saveAs(bundle, error));
    const auto before = f.arrangement().serialise();
    f.commands.reset();
    f.project.reset();

    {
        auto reopened = sampler::ProjectEdit::open(f.host, bundle, error);
        REQUIRE(reopened != nullptr);
        auto& a = reopened->arrangement();
        REQUIRE(a.numTracks() == 2);
        CHECK(a.tracks()[1].name == "Pads");
        CHECK(a.tracks()[1].volumeDb == Approx(-3.0).margin(0.05));
        REQUIRE(a.numClips() == 3);
        CHECK(a.tempo() == Approx(100.0));
        CHECK(a.loopRange().start == Approx(4.0));
        CHECK(reopened->transport().isLooping());
        CHECK(reopened->transport().isClickOn());
        const auto pads = a.clipsOnTrack(a.tracks()[1].id);
        REQUIRE(pads.size() == 1);
        CHECK(pads[0].start == Approx(8.0));
        CHECK(pads[0].gainDb == Approx(-6.0));
        CHECK_FALSE(pads[0].missing);
        CHECK(reopened->missingFiles().isEmpty());
        CHECK(a.serialise().length() > before.length() / 2);
    }

    // The kick's working copy goes: its clips are kept, drawn as missing, and the open reports the file.
    REQUIRE(bundle.getChildFile("audio/kick.wav").deleteFile());
    auto reopened = sampler::ProjectEdit::open(f.host, bundle, error);
    REQUIRE(reopened != nullptr);
    auto& a = reopened->arrangement();
    REQUIRE(a.numClips() == 3);
    int missing = 0;
    for (const auto& c : a.clips())
        missing += c.missing ? 1 : 0;
    CHECK(missing == 2);
    CHECK(reopened->missingFiles() == juce::StringArray{"kick.wav"});
    // The other clips still play: a render has the pad and no kick.
    juce::String renderError;
    const auto out = f.root.getChildFile("missing.wav");
    REQUIRE(sampler::engine::renderToWav(*reopened, out, {}, renderError));
    const auto audio = test::readWav(out);
    const auto spb = 60.0 / 100.0;
    CHECK(peakBetween(audio, 0, 0.0, 4.0 * spb) < 0.001f);     // where the kicks were: silence
    CHECK(peakBetween(audio, 0, 8.0 * spb, 9.0 * spb) > 0.05f); // the pad
}

TEST_CASE("track commands: add after the selected one, delete with undo restoring clips and mixer, move, rename, colour",
          "[arrange][commands][tracks]")
{
    ArrangeFixture f("more-tracks");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    // "Audio 1" selected, Mod+T: "Audio 3" between them with a new colour.
    cmd::AddTrack between(f.trackId(0));
    f.commands->apply(between);
    REQUIRE(a.numTracks() == 3);
    CHECK(a.tracks()[1].name == "Audio 3");
    CHECK(a.tracks()[1].id == between.resultIds().front());
    CHECK(a.tracks()[1].colour != a.tracks()[2].colour);

    // A track with three clips and mixer settings, deleted and brought back.
    const auto id = f.trackId(1);
    for (int i = 0; i < 3; ++i)
        f.place(bar, 1, i * 8.0);
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(id, cmd::SetTrackParam::Control::volume, -7.0));
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(id, cmd::SetTrackParam::Control::pan, -0.5));
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(id, cmd::SetTrackParam::Control::mute, 1.0));
    f.commands->apply(std::make_unique<cmd::DeleteTracks>(std::vector<juce::String>{id}));
    CHECK(f.commands->undoName() == "Delete track");
    CHECK(a.numTracks() == 2);
    CHECK(a.numClips() == 0);
    f.commands->undo();
    REQUIRE(a.numTracks() == 3);
    CHECK(a.tracks()[1].id == id);
    CHECK(a.tracks()[1].name == "Audio 3");
    CHECK(a.tracks()[1].volumeDb == Approx(-7.0).margin(0.05));
    CHECK(a.tracks()[1].pan == Approx(-0.5));
    CHECK(a.tracks()[1].mute);
    CHECK(a.clipsOnTrack(id).size() == 3);

    f.commands->apply(std::make_unique<cmd::ReorderTrack>(f.trackId(2), 0));
    CHECK(f.commands->undoName() == "Move track");
    CHECK(a.tracks()[0].name == "Audio 2");
    f.commands->undo();
    CHECK(a.tracks()[2].name == "Audio 2");

    f.commands->apply(std::make_unique<cmd::RenameTrack>(id, "  Bass "));
    CHECK(a.tracks()[1].name == "Bass");
    CHECK(f.commands->undoName() == "Rename track");
    const auto steps = f.project->undoManager().getUndoDescriptions().size();
    f.commands->apply(std::make_unique<cmd::RenameTrack>(id, "   ")); // empty: refused
    CHECK(a.tracks()[1].name == "Bass");
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps);

    f.commands->apply(std::make_unique<cmd::SetTrackColour>(id, sampler::engine::trackPaletteColour(1)));
    CHECK(a.tracks()[1].colour == sampler::engine::trackPaletteColour(1));
    CHECK(f.commands->undoName() == "Set track colour");
    CHECK(f.violations.isEmpty());
}

TEST_CASE("gestures: one step, cancel leaves none, undo is off while open", "[arrange][commands][gesture]")
{
    ArrangeFixture f("more-gesture");
    const auto track = f.trackId(0);
    const auto steps = [&] { return f.project->undoManager().getUndoDescriptions().size(); };
    const auto before = steps();

    f.commands->begin("Set volume");
    for (int i = 1; i <= 12; ++i)
        f.commands->update(std::make_unique<cmd::SetTrackParam>(track, cmd::SetTrackParam::Control::volume, -1.0 * i));
    CHECK_FALSE(f.commands->canUndo());
    CHECK(f.commands->isGestureOpen());
    f.commands->commit();
    CHECK(steps() == before + 1);
    CHECK(f.commands->undoName() == "Set volume");
    f.commands->undo();
    CHECK(f.arrangement().tracks()[0].volumeDb == Approx(0.0).margin(0.05));
    f.commands->redo();
    CHECK(f.arrangement().tracks()[0].volumeDb == Approx(-12.0).margin(0.05));

    // Cancel: the value goes back and no step is left; redo still works.
    f.commands->undo();
    const auto token = f.commands->historyToken();
    f.commands->begin("Set volume");
    f.commands->update(std::make_unique<cmd::SetTrackParam>(track, cmd::SetTrackParam::Control::volume, -20.0));
    f.commands->cancel();
    CHECK(f.arrangement().tracks()[0].volumeDb == Approx(0.0).margin(0.05));
    CHECK(f.commands->historyToken() == token);
    CHECK(f.commands->canRedo());
    f.commands->redo();
    CHECK(f.arrangement().tracks()[0].volumeDb == Approx(-12.0).margin(0.05));
}

TEST_CASE("repeated mixer changes within 500 ms join one step; another command or the window breaks it",
          "[arrange][commands][gesture]")
{
    ArrangeFixture f("more-coalesce");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    juce::uint32 now = 1000;
    f.commands->clock = [&now] { return now; };
    const auto track = f.trackId(0);
    const auto mute = [&](bool on) {
        f.commands->apply(std::make_unique<cmd::SetTrackParam>(track, cmd::SetTrackParam::Control::mute, on ? 1.0 : 0.0));
    };
    const auto steps = [&] { return f.project->undoManager().getUndoDescriptions().size(); };
    const auto before = steps();

    mute(true);
    now += 150;
    mute(false);
    now += 150;
    mute(true);
    CHECK(steps() == before + 1); // three quick clicks, one step
    f.commands->undo();
    CHECK_FALSE(f.arrangement().tracks()[0].mute); // as before the first click

    mute(true);
    now += 600; // out of the window
    mute(false);
    CHECK(steps() == before + 2);

    now += 100;
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), cmd::SetTrackParam::Control::mute, 1.0)); // other track
    now += 100;
    mute(true); // same control again, but something came in between
    CHECK(steps() == before + 4);
}

TEST_CASE("mute and solo decide what is heard; exclusive solo is one step", "[arrange][commands][mixer][render]")
{
    ArrangeFixture f("more-mute-solo");
    const auto left = f.addSound("left", 2.0, "dc", 1.0f, 0.0f);
    const auto right = f.addSound("right", 2.0, "dc", 0.0f, 1.0f);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.place(left, 0, 0.0);
    f.place(right, 1, 0.0);
    const auto t0 = f.trackId(0), t1 = f.trackId(1), t2 = f.trackId(2);
    using C = cmd::SetTrackParam::Control;

    auto audio = render(f, "both");
    CHECK(peakBetween(audio, 0, 0.0, 2.0) > 0.3f);
    CHECK(peakBetween(audio, 1, 0.0, 2.0) > 0.3f);

    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t0, C::mute, 1.0));
    audio = render(f, "muted");
    CHECK(peakBetween(audio, 0, 0.0, 2.0) < 0.001f);
    CHECK(peakBetween(audio, 1, 0.0, 2.0) > 0.3f);
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t0, C::mute, 0.0));

    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t1, C::solo, 1.0));
    audio = render(f, "solo");
    CHECK(peakBetween(audio, 0, 0.0, 2.0) < 0.001f);
    CHECK(peakBetween(audio, 1, 0.0, 2.0) > 0.3f);

    // Mute wins over solo on the same track.
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t1, C::mute, 1.0));
    audio = render(f, "mute-wins");
    CHECK(peakBetween(audio, 1, 0.0, 2.0) < 0.001f);
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t1, C::mute, 0.0));

    // Tracks 1 and 2 soloed, then exclusive solo on 3: one step, only 3 soloed.
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t0, C::solo, 1.0));
    const auto steps = f.project->undoManager().getUndoDescriptions().size();
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t2, C::exclusiveSolo, 1.0));
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps + 1);
    const auto tracks = f.arrangement().tracks();
    CHECK_FALSE(tracks[0].solo);
    CHECK_FALSE(tracks[1].solo);
    CHECK(tracks[2].solo);

    // Un-soloing the only soloed track: everything unmuted is heard again.
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t2, C::solo, 0.0));
    audio = render(f, "unsolo");
    CHECK(peakBetween(audio, 0, 0.0, 2.0) > 0.3f);
    CHECK(peakBetween(audio, 1, 0.0, 2.0) > 0.3f);
}

TEST_CASE("volume, pan and master volume change the render; undo of a volume drag is heard", "[arrange][commands][mixer][render]")
{
    ArrangeFixture f("more-volume");
    const auto dc = f.addSound("dc", 2.0, "dc");
    f.place(dc, 0, 0.0);
    const auto t0 = f.trackId(0);
    using C = cmd::SetTrackParam::Control;
    const auto reference = peakBetween(render(f, "ref"), 0, 0.0, 2.0);
    REQUIRE(reference > 0.1f);

    f.commands->begin("Set volume");
    f.commands->update(std::make_unique<cmd::SetTrackParam>(t0, C::volume, -6.0));
    f.commands->update(std::make_unique<cmd::SetTrackParam>(t0, C::volume, -12.0));
    f.commands->commit();
    ArrangeFixture::pump();
    CHECK(juce::Decibels::gainToDecibels(peakBetween(render(f, "minus12"), 0, 0.0, 2.0) / reference) == Approx(-12.0).margin(0.3));
    f.commands->undo();
    ArrangeFixture::pump(); // the plugin parameters follow the undone value
    CHECK(peakBetween(render(f, "undone"), 0, 0.0, 2.0) == Approx(reference).margin(0.01));

    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t0, C::pan, -1.0));
    ArrangeFixture::pump();
    auto audio = render(f, "panned");
    CHECK(peakBetween(audio, 1, 0.0, 2.0) < 0.001f); // only the left channel
    CHECK(peakBetween(audio, 0, 0.0, 2.0) > 0.1f);
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(t0, C::pan, 0.0));

    f.commands->apply(std::make_unique<cmd::SetTrackParam>(juce::String(), C::volume, -6.0));
    ArrangeFixture::pump();
    CHECK(f.arrangement().masterVolumeDb() == Approx(-6.0).margin(0.05));
    CHECK(juce::Decibels::gainToDecibels(peakBetween(render(f, "master"), 0, 0.0, 2.0) / reference) == Approx(-6.0).margin(0.3));
}

TEST_CASE("SetLoop is one undo step; the metronome setting is saved, not undoable and never rendered",
          "[arrange][commands][loop][click]")
{
    ArrangeFixture f("more-loop-click");
    const auto dc = f.addSound("quiet", 4.0, "dc", 0.0f, 0.0f); // silent sound: anything in the render is a click
    f.place(dc, 0, 0.0);
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{16.0, 32.0}));
    CHECK(f.commands->undoName() == "Set loop");
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{40.0, 36.0})); // drawn backwards
    CHECK(f.arrangement().loopRange().start == Approx(36.0));
    f.commands->undo();
    CHECK(f.arrangement().loopRange().start == Approx(16.0));

    const auto token = f.commands->historyToken();
    const auto steps = f.project->undoManager().getUndoDescriptions().size();
    f.project->transport().setClickOn(true);
    CHECK(f.commands->historyToken() == token);
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps);
    const auto audio = render(f, "click");
    CHECK(test::peakOf(audio, 0, 0, audio.getNumSamples()) < 0.0001f);
    CHECK(f.project->transport().isClickOn()); // still on after the render
}

TEST_CASE("tempo: clips keep their bars; a faster tempo shortens the clip that runs into the next one", "[arrange][commands][tempo]")
{
    ArrangeFixture f("more-tempo");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0); // one bar at 120 BPM
    const auto first = f.place(bar, 0, 0.0);
    const auto second = f.place(bar, 0, 4.0);
    const auto later = f.place(bar, 0, 16.0); // bar 5

    f.commands->apply(std::make_unique<cmd::SetTempo>(90.0));
    CHECK(a.tempo() == Approx(90.0));
    CHECK(a.clip(later)->start == Approx(16.0));
    CHECK(a.clip(later)->startSeconds == Approx(16.0 * 60.0 / 90.0)); // later in seconds
    f.commands->undo();
    CHECK(a.tempo() == Approx(120.0));

    f.commands->apply(std::make_unique<cmd::SetTempo>(140.0));
    CHECK(f.commands->undoName() == "Set tempo");
    CHECK(a.clip(first)->end() == Approx(4.0)); // shortened to end at bar 2
    CHECK(a.clip(second)->start == Approx(4.0));
    CHECK(f.violations.isEmpty());
    f.commands->undo();
    CHECK(a.tempo() == Approx(120.0));
    CHECK(a.clip(first)->lengthSeconds == Approx(2.0)); // length restored too

    f.commands->apply(std::make_unique<cmd::SetTempo>(5000.0)); // clamped
    CHECK(a.tempo() == Approx(999.0));
    f.commands->apply(std::make_unique<cmd::SetTempo>(1.0));
    CHECK(a.tempo() == Approx(20.0));
}

TEST_CASE("tempo: NaN and infinity are refused, 999 BPM keeps the clips on their beats", "[arrange][commands][tempo]")
{
    ArrangeFixture f("more-tempo-bad");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0);
    const auto first = f.place(bar, 0, 0.0);
    const auto second = f.place(bar, 0, 4.0);
    const auto steps = f.project->undoManager().getUndoDescriptions().size();
    const auto saved = a.serialise();

    for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()})
    {
        f.commands->apply(std::make_unique<cmd::SetTempo>(bad));
        CHECK(a.tempo() == Approx(120.0));
        CHECK(f.project->undoManager().getUndoDescriptions().size() == steps);
        CHECK(a.serialise() == saved);
    }
    a.setTempo(std::numeric_limits<double>::quiet_NaN()); // the engine layer ignores it too
    CHECK(a.tempo() == Approx(120.0));

    f.commands->apply(std::make_unique<cmd::SetTempo>(999.0));
    CHECK(a.tempo() == Approx(999.0));
    CHECK(a.clip(first)->start == Approx(0.0));
    CHECK(a.clip(second)->start == Approx(4.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("tempo drag 120 -> 200 -> 120 in one gesture shortens nothing for good", "[arrange][commands][tempo]")
{
    ArrangeFixture f("more-tempo-drag");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0); // one bar at 120 BPM
    const auto first = f.place(bar, 0, 0.0);
    const auto second = f.place(bar, 0, 4.0);
    const auto before = a.clips();
    const auto steps = f.project->undoManager().getUndoDescriptions().size();

    f.commands->begin("Set tempo");
    cmd::SetTempo drag(200.0); // one command for the drag, as the tempo field does
    f.commands->update(drag);
    CHECK(a.clip(first)->end() == Approx(4.0)); // shortened at 200 BPM: 2 s are 6.67 beats
    drag.setBpm(160.0);
    f.commands->update(drag);
    drag.setBpm(120.0);
    f.commands->update(drag);
    f.commands->commit();

    CHECK(a.tempo() == Approx(120.0));
    CHECK(a.clip(first)->lengthSeconds == Approx(2.0));
    CHECK(a.clip(first)->length == Approx(4.0));
    CHECK(a.clip(second)->start == Approx(4.0));
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps + 1);
    CHECK(f.violations.isEmpty());
    const auto after = a.clips();
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].start == Approx(before[i].start));
        CHECK(after[i].lengthSeconds == Approx(before[i].lengthSeconds));
    }
    f.commands->undo();
    CHECK(a.tempo() == Approx(120.0));
    CHECK(a.clips().size() == before.size());
    CHECK(a.clip(first)->lengthSeconds == Approx(2.0));
}

TEST_CASE("time signature: undoable, clips keep their beats", "[arrange][commands][tempo]")
{
    ArrangeFixture f("more-timesig");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0);
    const auto clip = f.place(bar, 0, 12.0);
    f.commands->apply(std::make_unique<cmd::SetTimeSig>(3, 4));
    CHECK(f.commands->undoName() == "Set time signature");
    CHECK(a.timeSig().numerator == 3);
    CHECK(a.timeSig().denominator == 4);
    CHECK(a.clip(clip)->start == Approx(12.0));
    f.commands->apply(std::make_unique<cmd::SetTimeSig>(7, 8));
    CHECK(a.timeSig().denominator == 8);
    CHECK(a.clip(clip)->start == Approx(12.0));
    f.commands->apply(std::make_unique<cmd::SetTimeSig>(40, 5)); // clamped / unknown denominator
    CHECK(a.timeSig().numerator == 32);
    CHECK(a.timeSig().denominator == 4);
    f.commands->undo();
    f.commands->undo();
    CHECK(a.timeSig().numerator == 3);
    f.commands->undo();
    CHECK(a.timeSig().numerator == 4);
}

TEST_CASE("clip gain, start and reverse: undoable; the reversed render is the sound backwards; files untouched",
          "[arrange][commands][clip][render]")
{
    ArrangeFixture f("more-clip");
    auto& a = f.arrangement();
    const auto ramp = f.addSound("ramp", 2.0, "ramp");
    const auto decoded = f.project->bundleDir().getChildFile("audio/ramp.wav");
    const auto hashBefore = juce::SHA256(decoded).toHexString();
    const auto clip = f.place(ramp, 0, 0.0);

    f.commands->apply(std::make_unique<cmd::SetClipGain>(clip, -6.0));
    CHECK(f.commands->undoName() == "Set clip gain");
    CHECK(a.clip(clip)->gainDb == Approx(-6.0));
    f.commands->undo();
    CHECK(a.clip(clip)->gainDb == Approx(0.0));

    f.commands->apply(std::make_unique<cmd::SetClipStart>(clip, 32.0));
    CHECK(f.commands->undoName() == "Set clip start");
    CHECK(a.clip(clip)->start == Approx(32.0));
    f.commands->undo();
    CHECK(a.clip(clip)->start == Approx(0.0));

    f.commands->apply(std::make_unique<cmd::SetClipReversed>(clip, true));
    CHECK(f.commands->undoName() == "Reverse clip");
    CHECK(a.clip(clip)->reversed);
    // The render waits for the reverse proxy: the ramp now falls instead of rising.
    const auto audio = render(f, "reversed");
    const auto early = audio.getSample(0, static_cast<int>(0.2 * 48000.0));
    const auto late = audio.getSample(0, static_cast<int>(1.8 * 48000.0));
    CHECK(early > late);
    CHECK(early == Approx(0.9f).margin(0.03f));
    CHECK(late == Approx(0.1f).margin(0.03f));
    REQUIRE(f.project->save());
    CHECK(juce::SHA256(decoded).toHexString() == hashBefore);
    for (const auto& file : f.project->bundleDir().getChildFile("audio").findChildFiles(juce::File::findFiles, false))
        CHECK_FALSE(file.getFileName().startsWith("render_")); // derived audio lives in cache/
    f.commands->undo();
    CHECK_FALSE(a.clip(clip)->reversed);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("paste: at the playhead on the selected track, spacing kept, clamped to the last track, overlap rule",
          "[arrange][commands][paste]")
{
    ArrangeFixture f("more-paste");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto c1 = f.place(bar, 0, 0.0);
    const auto c2 = f.place(bar, 1, 4.0);
    std::vector<cmd::ClipRecord> records;
    for (const auto& id : {c1, c2})
    {
        const auto info = *a.clip(id);
        cmd::ClipRecord r;
        r.sourceId = info.sourceId;
        r.name = info.name;
        r.trackOffset = a.trackIndex(info.trackId);
        r.startOffset = info.start;
        r.lengthSeconds = info.lengthSeconds;
        records.push_back(r);
    }
    cmd::PasteClips paste(records, 2, 32.0); // track 3, bar 9: the second clip would go below the last track
    f.commands->apply(paste);
    CHECK(f.commands->undoName() == "Paste 2 clips");
    REQUIRE(paste.resultIds().size() == 2);
    const auto onLast = f.clipsOn(2);
    REQUIRE(onLast.size() == 2);
    CHECK(onLast[0].start == Approx(32.0));
    CHECK(onLast[1].start == Approx(36.0));
    CHECK(a.clip(c1)->start == Approx(0.0)); // the original is unchanged

    // Pasting over existing clips follows the no-overlap rule.
    cmd::PasteClips over({records[0]}, 2, 34.0);
    f.commands->apply(over);
    CHECK(f.violations.isEmpty());
    f.commands->undo();
    f.commands->undo();
    CHECK(f.clipsOn(2).empty());
}

TEST_CASE("a placement follows its track by id: reordered, or deleted while the import was pending", "[arrange][commands][placement]")
{
    ArrangeFixture f("more-placement-id");
    auto& a = f.arrangement();
    const auto bar = f.addSound("bar", 2.0);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto first = f.trackId(0), second = f.trackId(1), third = f.trackId(2);

    // The target was track 2 ("second") when the files were dropped; then the tracks were reordered.
    f.commands->apply(std::make_unique<cmd::ReorderTrack>(second, 0));
    REQUIRE(a.trackIndex(second) == 0);
    cmd::CreateClipsFromSources onSecond({bar}, cmd::PlacementTarget{1, false, 8.0, second});
    f.commands->apply(onSecond);
    REQUIRE(onSecond.resultIds().size() == 1);
    CHECK(a.clip(onSecond.resultIds().front())->trackId == second);

    // The target track is deleted before the import finishes: the clips land on a new track at the end.
    f.commands->apply(std::make_unique<cmd::DeleteTracks>(std::vector<juce::String>{third}));
    const auto tracksBefore = a.numTracks();
    cmd::CreateClipsFromSources onGone({bar}, cmd::PlacementTarget{2, false, 0.0, third});
    f.commands->apply(onGone);
    REQUIRE(onGone.resultIds().size() == 1);
    CHECK(a.numTracks() == tracksBefore + 1);
    CHECK(a.clip(onGone.resultIds().front())->trackId == a.tracks().back().id);
    CHECK(a.clip(onGone.resultIds().front())->trackId != first);
    CHECK(f.violations.isEmpty());
}
