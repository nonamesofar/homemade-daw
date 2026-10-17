// M4a task 1.7: the warp commands and what the older commands do with warped clips (clip-warp and transport specs).
#include "WarpTestSupport.h"

#include <catch2/catch_approx.hpp>

#include <algorithm>

using Catch::Approx;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;
using namespace sampler::test;
namespace cmd = sampler::commands;

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

/** Places a sound with the given warp settings; returns the clip id. */
juce::String placeWarped(ArrangeFixture& f, const juce::String& sound, int track, double beat, const WarpSettings& w)
{
    cmd::CreateClipsFromSources command({sound}, {track, false, beat}, {w});
    f.commands->apply(command);
    REQUIRE(command.resultIds().size() == 1);
    return command.resultIds().front();
}
} // namespace

TEST_CASE("a clip placed with warp settings is warped and covers the sound in bars", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-place");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto warped = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::beats, 90.0));
    const auto plain = f.place(sound, 0, 32.0);
    const auto a = *f.arrangement().clip(warped);
    CHECK(a.warp == warpOn(WarpMode::beats, 90.0));
    CHECK(a.start == Approx(0.0));
    CHECK(a.length == Approx(16.0)); // bars 1 to 4
    CHECK(a.lengthSeconds == Approx(8.0));
    CHECK_FALSE(f.arrangement().clip(plain)->warp.on);
    CHECK(f.arrangement().clip(plain)->length == Approx(470400.0 / 44100.0 * 2.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a warped clip keeps bars 1 to 4 when the tempo goes from 120 to 100 BPM; undo needs no render",
          "[arrange][warp][commands][transport]")
{
    ArrangeFixture f("wc-tempo");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::beats, 90.0));
    REQUIRE(waitForBeatsRenders(f));

    f.commands->apply(std::make_unique<cmd::SetTempo>(100.0));
    auto info = *f.arrangement().clip(clip);
    CHECK(info.start == Approx(0.0));
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(9.6).margin(1.0e-6)); // longer in seconds
    REQUIRE(waitForBeatsRenders(f));
    CHECK(numBeatsFiles(f) == 2);

    f.commands->undo();
    info = *f.arrangement().clip(clip);
    CHECK(f.arrangement().tempo() == Approx(120.0));
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(8.0).margin(1.0e-6));
    CHECK_FALSE(info.rendering); // the old render is still in the cache
    CHECK(f.arrangement().allRendersReady());
    f.commands->redo();
    CHECK_FALSE(f.arrangement().clip(clip)->rendering);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("an unwarped clip keeps its seconds when the tempo changes, a warped one next to it keeps its bars",
          "[arrange][warp][commands][transport]")
{
    ArrangeFixture f("wc-tempo-mixed");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto warped = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::repitch, 100.0));
    const auto plain = f.place(sound, 1, 0.0);
    f.commands->apply(std::make_unique<cmd::SetTempo>(90.0));
    const auto w = *f.arrangement().clip(warped);
    const auto p = *f.arrangement().clip(plain);
    CHECK(w.length == Approx(16.0));
    CHECK(w.lengthSeconds == Approx(16.0 * 60.0 / 90.0).margin(1.0e-6));
    CHECK(p.lengthSeconds == Approx(9.6).margin(1.0e-6));
    CHECK(p.length == Approx(9.6 * 90.0 / 60.0));
    f.commands->undo();
    CHECK(f.arrangement().clip(warped)->lengthSeconds == Approx(8.0).margin(1.0e-6));
    CHECK(f.arrangement().clip(plain)->lengthSeconds == Approx(9.6).margin(1.0e-6));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a tempo drag asks for the renders of the final tempo only", "[arrange][warp][commands][transport]")
{
    ArrangeFixture f("wc-tempo-drag");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::beats, 90.0));
    REQUIRE(waitForBeatsRenders(f));
    REQUIRE(numBeatsFiles(f) == 1);
    const auto stepsBefore = f.project->undoManager().getUndoDescriptions().size();

    f.commands->begin("Set tempo");
    cmd::SetTempo gesture(120.0);
    gesture.setDeferRenders(true);
    for (const double bpm : {110.0, 105.0, 101.0, 100.0})
    {
        gesture.setBpm(bpm);
        f.commands->update(gesture);
        ArrangeFixture::pump(30);
        CHECK(f.arrangement().missingBeatsRenders().empty()); // the clip keeps the render it plays
        CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
        CHECK_FALSE(f.arrangement().clip(clip)->rendering);
    }
    REQUIRE(numBeatsFiles(f) == 1);
    gesture.setDeferRenders(false);
    f.commands->update(gesture);
    CHECK(f.arrangement().clip(clip)->rendering); // the render for 100 BPM is asked for now
    f.commands->commit();
    REQUIRE(waitForBeatsRenders(f));
    CHECK(numBeatsFiles(f) == 2);
    CHECK(f.project->undoManager().getUndoDescriptions().size() == stepsBefore + 1);

    f.commands->undo();
    CHECK(f.arrangement().tempo() == Approx(120.0));
    CHECK_FALSE(f.arrangement().clip(clip)->rendering);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("WARP off gives 5.33 bars and shortens the next clip in the same step", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-warp-off");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto kick = addFixtureSound(f, "kick.wav", sampler::model::Category::oneShot);
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::beats, 90.0));
    const auto next = f.place(kick, 0, 21.0);
    const auto nextBefore = *f.arrangement().clip(next);
    REQUIRE(nextBefore.start == Approx(21.0));

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, false, WarpMode::beats, 90.0));
    const auto info = *f.arrangement().clip(clip);
    CHECK_FALSE(info.warp.on);
    CHECK(info.warp.sourceBpm == 90.0); // kept
    CHECK(info.start == Approx(0.0));
    CHECK(info.lengthSeconds == Approx(470400.0 / 44100.0));
    CHECK(info.length == Approx(470400.0 / 44100.0 * 2.0)); // 5.33 bars
    const auto after = *f.arrangement().clip(next);
    CHECK(after.start == Approx(info.end()));    // shortened from its start
    CHECK(after.end() == Approx(nextBefore.end()));
    CHECK(after.offsetSeconds > 0.0);
    CHECK(f.violations.isEmpty());
    CHECK(f.commands->undoName() == "Set clip warp");

    f.commands->undo(); // one step brings back both
    CHECK(f.arrangement().clip(clip)->warp.active());
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    CHECK(f.arrangement().clip(next)->start == Approx(21.0));
    CHECK(f.arrangement().clip(next)->offsetSeconds == Approx(0.0));
}

TEST_CASE("doubling the clip tempo halves its bars, undo brings them back, and other clips of the sound are not changed",
          "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-double");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    // The sound was detected at 50 BPM, so a clip covers 8 beats; the user presses x2.
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::tones, 50.0));
    const auto other = placeWarped(f, sound, 1, 0.0, warpOn(WarpMode::tones, 50.0));
    REQUIRE(f.arrangement().clip(clip)->length == Approx(8.0));

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::tones, 100.0));
    CHECK(f.arrangement().clip(clip)->warp.sourceBpm == 100.0);
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    CHECK(f.arrangement().clip(other)->warp.sourceBpm == 50.0);
    CHECK(f.arrangement().clip(other)->length == Approx(8.0));

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::tones, 25.0));
    CHECK(f.arrangement().clip(clip)->length == Approx(4.0));
    f.commands->undo();
    f.commands->undo();
    CHECK(f.arrangement().clip(clip)->warp.sourceBpm == 50.0);
    CHECK(f.arrangement().clip(clip)->length == Approx(8.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("turning WARP on gives the clip its bars; without a tempo it does nothing", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-warp-on");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto clip = f.place(sound, 0, 4.0);
    const auto steps = f.project->undoManager().getUndoDescriptions().size();

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::beats, 0.0));
    CHECK_FALSE(f.arrangement().clip(clip)->warp.on);
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps); // no undo step

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::beats, 90.0));
    const auto info = *f.arrangement().clip(clip);
    CHECK(info.warp.active());
    CHECK(info.start == Approx(4.0));
    CHECK(info.length == Approx(16.0));
    CHECK(info.lengthSeconds == Approx(8.0));

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::repitch, 90.0)); // another mode, same bars
    CHECK(f.arrangement().clip(clip)->warp.mode == WarpMode::repitch);
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::repitch, 90.0)); // unchanged: nothing
    CHECK(f.project->undoManager().getUndoDescriptions().size() == steps + 2);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("transpose is one undo step, clamped, and kept when the mode ignores it", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-transpose");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::beats, 90.0));
    f.commands->apply(std::make_unique<cmd::SetClipTranspose>(clip, 7.0));
    CHECK(f.arrangement().clip(clip)->warp.transpose == 7.0);
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    f.commands->apply(std::make_unique<cmd::SetClipTranspose>(clip, 40.0));
    CHECK(f.arrangement().clip(clip)->warp.transpose == 24.0);
    f.commands->apply(std::make_unique<cmd::SetClipWarp>(clip, true, WarpMode::repitch, 90.0));
    CHECK(f.arrangement().clip(clip)->warp.transpose == 24.0); // greyed out in the panel, value kept
    f.commands->undo();
    f.commands->undo();
    CHECK(f.arrangement().clip(clip)->warp.transpose == 7.0);
    f.commands->undo();
    CHECK(f.arrangement().clip(clip)->warp.transpose == 0.0);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("duplicate, paste and split carry the warp settings of a clip", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-carry");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto original = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::tones, 100.0, 2.0));

    SECTION("duplicate")
    {
        cmd::DuplicateClips duplicate({original});
        f.commands->apply(duplicate);
        REQUIRE(duplicate.resultIds().size() == 1);
        const auto copy = *f.arrangement().clip(duplicate.resultIds().front());
        CHECK(copy.warp == warpOn(WarpMode::tones, 100.0, 2.0));
        CHECK(copy.start == Approx(16.0));
        CHECK(copy.length == Approx(16.0));
    }

    SECTION("paste")
    {
        const auto info = *f.arrangement().clip(original);
        std::vector<cmd::ClipRecord> records(1);
        records[0].sourceId = info.sourceId;
        records[0].name = info.name;
        records[0].lengthSeconds = info.lengthSeconds;
        records[0].offsetSeconds = info.offsetSeconds;
        records[0].warp = info.warp;
        records[0].lengthBeats = info.length;
        cmd::PasteClips paste(records, 1, 8.0);
        f.commands->apply(paste);
        REQUIRE(paste.resultIds().size() == 1);
        const auto copy = *f.arrangement().clip(paste.resultIds().front());
        CHECK(copy.warp == info.warp);
        CHECK(copy.start == Approx(8.0));
        CHECK(copy.length == Approx(16.0));
        // After a tempo change the pasted clip still covers its bars.
        f.commands->apply(std::make_unique<cmd::SetTempo>(150.0));
        CHECK(f.arrangement().clip(paste.resultIds().front())->length == Approx(16.0));
    }

    SECTION("split")
    {
        cmd::SplitClips split({original}, 6.0);
        f.commands->apply(split);
        const auto clips = f.arrangement().clipsOnTrack(f.trackId(0));
        REQUIRE(clips.size() == 2);
        CHECK(clips[0].warp == warpOn(WarpMode::tones, 100.0, 2.0));
        CHECK(clips[1].warp == clips[0].warp);
        CHECK(clips[0].length == Approx(6.0));
        CHECK(clips[1].start == Approx(6.0));
        CHECK(clips[1].length == Approx(10.0));
        CHECK(clips[0].offsetSeconds == Approx(0.0));
        CHECK(clips[1].offsetSeconds == Approx(6.0 * 60.0 / 100.0)); // the part of the sound the first clip played
        CHECK(clips[0].coveredSeconds() + clips[1].coveredSeconds() == Approx(9.6));
        f.commands->undo();
        CHECK(f.arrangement().clipsOnTrack(f.trackId(0)).size() == 1);
    }
    CHECK(f.violations.isEmpty());
}

TEST_CASE("pasting a WARP-off clip keeps its remembered tempo, and WARP can be turned on after", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-paste-off");
    const auto sound = addFixtureSound(f, "break-90.wav");
    const auto original = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::tones, 90.0, 3.0));
    f.commands->apply(std::make_unique<cmd::SetClipWarp>(original, false, WarpMode::tones, 90.0));
    const auto info = *f.arrangement().clip(original);
    REQUIRE_FALSE(info.warp.on);
    REQUIRE(info.warp.sourceBpm == 90.0);

    cmd::ClipRecord record;
    record.sourceId = info.sourceId;
    record.name = info.name;
    record.lengthSeconds = info.lengthSeconds;
    record.warp = info.warp;
    record.lengthBeats = info.length;
    cmd::PasteClips paste({record}, 0, 40.0);
    f.commands->apply(paste);
    REQUIRE(paste.resultIds().size() == 1);
    const auto copyId = paste.resultIds().front();
    const auto copy = *f.arrangement().clip(copyId);
    CHECK_FALSE(copy.warp.on);
    CHECK(copy.warp.sourceBpm == 90.0);
    CHECK(copy.warp.mode == WarpMode::tones);
    CHECK(copy.warp.transpose == 3.0);

    f.commands->apply(std::make_unique<cmd::SetClipWarp>(copyId, true, WarpMode::tones, 90.0));
    CHECK(f.arrangement().clip(copyId)->warp.active());

    // A warped record without a length in beats still gets a length (from its seconds).
    record.warp = warpOn(WarpMode::beats, 90.0);
    record.lengthBeats = 0.0;
    record.lengthSeconds = 4.0; // timeline seconds: 8 beats at 120 BPM, which is 5.33 s of a 90 BPM sound
    cmd::PasteClips pasteNoBeats({record}, 0, 80.0);
    f.commands->apply(pasteNoBeats);
    REQUIRE(pasteNoBeats.resultIds().size() == 1);
    // The record's seconds are timeline seconds (at 120 BPM here), whatever the clip's own tempo.
    CHECK(f.arrangement().clip(pasteNoBeats.resultIds().front())->length == Approx(8.0));
    CHECK(f.violations.isEmpty());
    CHECK(waitForBeatsRenders(f));
}

TEST_CASE("a clip dropped inside another splits it like a split: the fade in stays left, the fade out goes right", "[arrange][commands][fades]")
{
    ArrangeFixture f("wc-cut-fades");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto longSound = f.addSound("long", 8.0, "tone"); // 16 beats at 120 BPM
    const auto shortSound = f.addSound("short", 1.0, "tone"); // 2 beats
    const auto a = f.place(longSound, 0, 0.0);
    const auto b = f.place(shortSound, 1, 0.0);
    f.commands->apply(std::make_unique<cmd::SetClipFades>(a, 2.0, 2.0));

    f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{b, 0, 6.0}}));
    auto clips = f.arrangement().clipsOnTrack(f.trackId(0));
    REQUIRE(clips.size() == 3);
    std::sort(clips.begin(), clips.end(), [](const auto& x, const auto& y) { return x.start < y.start; });
    CHECK(clips[0].id == a);
    CHECK(clips[0].fadeIn == Approx(2.0));
    CHECK(clips[0].fadeOut == Approx(0.0));
    CHECK(clips[1].id == b);
    CHECK(clips[2].fadeIn == Approx(0.0));
    CHECK(clips[2].fadeOut == Approx(2.0));

    f.commands->undo();
    CHECK(f.arrangement().clip(a)->fadeIn == Approx(2.0));
    CHECK(f.arrangement().clip(a)->fadeOut == Approx(2.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("two clips dropped inside one long clip in one command: the fade in stays left, the fade out goes to the end piece",
          "[arrange][commands][fades]")
{
    ArrangeFixture f("wc-cut-fades-two");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto longSound = f.addSound("long", 8.0, "tone"); // 16 beats at 120 BPM
    const auto shortSound = f.addSound("short", 1.0, "tone"); // 2 beats
    const auto a = f.place(longSound, 0, 0.0);
    const auto b = f.place(shortSound, 1, 0.0);
    const auto c = f.place(shortSound, 1, 4.0);
    f.commands->apply(std::make_unique<cmd::SetClipFades>(a, 2.0, 2.0));
    const auto before = f.arrangement().serialise();

    f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{b, 0, 4.0}, {c, 0, 10.0}}));
    auto clips = f.arrangement().clipsOnTrack(f.trackId(0));
    REQUIRE(clips.size() == 5);
    std::sort(clips.begin(), clips.end(), [](const auto& x, const auto& y) { return x.start < y.start; });
    CHECK(clips[0].id == a);
    CHECK(clips[0].fadeIn == Approx(2.0));
    CHECK(clips[0].fadeOut == Approx(0.0));
    CHECK(clips[1].id == b);
    CHECK(clips[2].fadeIn == Approx(0.0)); // the piece between the two clips
    CHECK(clips[2].fadeOut == Approx(0.0));
    CHECK(clips[3].id == c);
    CHECK(clips[4].fadeIn == Approx(0.0)); // the piece that holds the end of the long clip
    CHECK(clips[4].fadeOut == Approx(2.0));

    f.commands->undo();
    CHECK(firstDifference(f.arrangement().serialise(), before) == "");
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a Re-pitch clip past Tracktion's speed limit trims by the sound it really plays", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-trim-clamped");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto clip = placeWarped(f, sound, 0, 0.0, warpOn(WarpMode::repitch, 20.0));
    f.commands->apply(std::make_unique<cmd::SetTempo>(800.0)); // 800 / 20 = 40, Tracktion plays 20
    auto info = *f.arrangement().clip(clip);
    REQUIRE(info.clampedSpeed == Approx(20.0));
    const auto offsetBefore = info.offsetSeconds;
    const auto end = info.end();

    f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 1.0, end, 0.0625));
    const auto trimmed = *f.arrangement().clip(clip);
    CHECK(trimmed.start == Approx(1.0));
    CHECK(trimmed.end() == Approx(end));
    // One beat is 60 / 800 s of the timeline, and the clip plays 20 s of sound per second of it.
    CHECK(trimmed.offsetSeconds == Approx(offsetBefore + 20.0 * 60.0 / 800.0).margin(1.0e-6));
    CHECK(trimmed.offsetSeconds + trimmed.coveredSeconds() == Approx(offsetBefore + info.coveredSeconds()).margin(1.0e-6));
    CHECK(trimmed.offsetSeconds + trimmed.coveredSeconds() <= trimmed.sourceSeconds + 1.0e-6);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("trimming and moving a warped clip converts the offset through its own tempo", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-trim");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto clip = placeWarped(f, sound, 0, 8.0, warpOn(WarpMode::repitch, 100.0));

    f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 10.0, 24.0, 0.25)); // 2 beats off the left edge
    auto info = *f.arrangement().clip(clip);
    CHECK(info.start == Approx(10.0));
    CHECK(info.end() == Approx(24.0));
    CHECK(info.offsetSeconds == Approx(2.0 * 60.0 / 100.0)); // 2 beats of a 100 BPM sound, whatever the project tempo
    // The right edge cannot pass the end of the sound: 16 beats of sound, 2 used up on the left.
    f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 10.0, 40.0, 0.25));
    info = *f.arrangement().clip(clip);
    CHECK(info.end() == Approx(10.0 + 14.0));

    f.commands->apply(std::make_unique<cmd::SetTempo>(60.0));
    f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{clip, 1, 3.0}}));
    info = *f.arrangement().clip(clip);
    CHECK(info.start == Approx(3.0));
    CHECK(info.length == Approx(14.0));
    CHECK(info.offsetSeconds == Approx(0.0 + 2.0 * 60.0 / 100.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("the project check names a warped clip without a usable tempo", "[arrange][warp][commands]")
{
    ArrangeFixture f("wc-check");
    const auto sound = addFixtureSound(f, "tone-440-100.wav");
    const auto clip = f.place(sound, 0, 0.0);
    CHECK_FALSE(cmd::findArrangementViolation(f.arrangement(), f.project->samplerState()).has_value());
    WarpSettings broken;
    broken.on = true;
    broken.sourceBpm = 0.0;
    f.project->undoManager().beginNewTransaction("break");
    f.arrangement().setClipWarp(clip, broken);
    const auto problem = cmd::findArrangementViolation(f.arrangement(), f.project->samplerState());
    REQUIRE(problem.has_value());
    CHECK(problem->contains("without a tempo"));
}
