// M4a task 2.2: the warp row of the clip inspector (clip-inspector and clip-warp specs), driven by synthetic events.
#include "ViewHarness.h"
#include "WarpTestSupport.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;
using namespace sampler::test;
namespace cmd = sampler::commands;
namespace ui = sampler::ui;

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

juce::String placeClip(ViewHarness& h, const juce::String& sound, double beat, const WarpSettings& w)
{
    cmd::CreateClipsFromSources command({sound}, {0, false, beat}, {w});
    h.f.commands->apply(command);
    REQUIRE(command.resultIds().size() == 1);
    return command.resultIds().front();
}

/** Applies a command that is made on the spot. */
template <typename Command>
void run(ViewHarness& h, Command command)
{
    h.f.commands->apply(command);
}

void select(ViewHarness& h, const juce::String& clip)
{
    h.selection.setClips({clip});
    h.sync();
}

/** Presses the WARP toggle like a click does: the state flips, then onClick runs. */
void clickWarp(ViewHarness& h)
{
    h.inspector->warpToggle.triggerClick();
    h.sync();
}
} // namespace

TEST_CASE("the inspector shows the warp settings of a Beats clip", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-show");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    select(h, clip);

    auto& in = *h.inspector;
    CHECK(in.warpToggle.getToggleState());
    CHECK(in.warpToggle.isEnabled());
    CHECK(in.mode.selected() == ui::InspectorLogic::modeBeats);
    CHECK(in.mode.isEnabled());
    CHECK(in.bpm.text() == "90.00");
    CHECK(in.transpose.text() == "0 st");
    CHECK(in.bpm.isVisible());
    CHECK(in.transpose.isEnabled());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("choosing Tones is one step and Undo returns to Beats", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-mode");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    select(h, clip);

    auto& in = *h.inspector;
    in.mode.setSelected(ui::InspectorLogic::modeTones, true);
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.mode == WarpMode::tones);
    CHECK(in.mode.selected() == ui::InspectorLogic::modeTones);
    CHECK(h.f.commands->undoName() == "Set clip warp");

    h.f.commands->undo();
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.mode == WarpMode::beats);
    CHECK(in.mode.selected() == ui::InspectorLogic::modeBeats);
    CHECK(h.f.violations.isEmpty());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("Texture is shown in MODE and cannot be chosen", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-texture");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    select(h, clip);

    auto& in = *h.inspector;
    CHECK_FALSE(in.mode.isItemEnabled(ui::InspectorLogic::modeTexture));
    CHECK(in.mode.isItemEnabled(ui::InspectorLogic::modeBeats));
    CHECK(in.mode.isItemEnabled(ui::InspectorLogic::modeTones));
    CHECK(in.mode.isItemEnabled(ui::InspectorLogic::modeRepitch));

    // A click on the Texture segment changes nothing.
    const auto x = static_cast<float>(in.mode.getWidth()) * 0.625f; // the middle of the third of four segments
    REQUIRE(in.mode.itemAt({static_cast<int>(x), 5}) == ui::InspectorLogic::modeTexture);
    h.click(in.mode, {x, 5.0f});
    h.sync();
    CHECK(in.mode.selected() == ui::InspectorLogic::modeBeats);
    CHECK(h.f.arrangement().clip(clip)->warp.mode == WarpMode::beats);
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("WARP off greys MODE, BPM and TRANSPOSE and keeps their values; Undo brings the warp back", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-off");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::tones, 90.0, 7.0));
    select(h, clip);

    auto& in = *h.inspector;
    clickWarp(h);
    const auto info = *h.f.arrangement().clip(clip);
    CHECK_FALSE(info.warp.on);
    CHECK_FALSE(in.warpToggle.getToggleState());
    CHECK(in.warpToggle.isEnabled()); // the tempo is known, so it can go back on
    CHECK_FALSE(in.mode.isEnabled());
    CHECK_FALSE(in.bpm.isEnabled());
    CHECK_FALSE(in.halveButton.isEnabled());
    CHECK_FALSE(in.doubleButton.isEnabled());
    CHECK_FALSE(in.transpose.isEnabled());
    CHECK(in.mode.selected() == ui::InspectorLogic::modeTones);
    CHECK(in.bpm.text() == "90.00");
    CHECK(in.transpose.text() == "+7 st");

    h.f.commands->undo();
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.on);
    CHECK(in.warpToggle.getToggleState());
    CHECK(in.mode.isEnabled());
    CHECK(in.transpose.isEnabled());
    CHECK(h.f.violations.isEmpty());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("turning WARP on takes the sound's tempo", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-on");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = h.f.place(sound, 0, 0.0); // WARP off, no tempo of its own
    select(h, clip);

    auto& in = *h.inspector;
    REQUIRE_FALSE(h.f.arrangement().clip(clip)->warp.on);
    CHECK(in.warpToggle.isEnabled());
    CHECK(in.bpm.text() == "90.00"); // shown greyed: what WARP will use
    clickWarp(h);
    const auto info = *h.f.arrangement().clip(clip);
    CHECK(info.warp.on);
    CHECK(info.warp.sourceBpm == Approx(90.0));
    CHECK(info.length == Approx(16.0));
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("a sound without a tempo shows WARP disabled with the hint", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-notempo");
    h.soundBpm = 0.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = h.f.place(sound, 0, 0.0);
    select(h, clip);

    auto& in = *h.inspector;
    CHECK_FALSE(in.warpToggle.isEnabled());
    CHECK(in.warpState().warpHint == "Tempo not known");
    CHECK(in.warpToggle.getTooltip() == "Tempo not known");
    CHECK_FALSE(in.bpm.isEnabled());
    CHECK(in.bpm.text() == juce::String::fromUTF8("\xe2\x80\x93"));

    // Even a click that reaches the command changes nothing: WARP needs a tempo.
    in.warpToggle.onClick();
    h.sync();
    CHECK_FALSE(h.f.arrangement().clip(clip)->warp.on);
    CHECK_FALSE(in.warpToggle.getToggleState());
}

// A sound of fixed length holds twice as many beats at twice the tempo, so x2 doubles the clip's bars (the clip-warp scenario
// "Double a wrong tempo" says the clip gets shorter; see the note in the task 2.2 report). Same as warp_commands.test.cpp.
TEST_CASE("x2 doubles the tempo to 180 and the clip's bars with it; Undo restores 90 and 4 bars", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-double");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    select(h, clip);

    auto& in = *h.inspector;
    in.doubleButton.onClick();
    h.sync();
    auto info = *h.f.arrangement().clip(clip);
    CHECK(info.warp.sourceBpm == Approx(180.0));
    CHECK(info.length == Approx(32.0)); // 10.67 s at 180 BPM
    CHECK(in.bpm.text() == "180.00");
    CHECK(h.f.commands->undoName() == "Set clip warp");

    h.f.commands->undo();
    h.sync();
    info = *h.f.arrangement().clip(clip);
    CHECK(info.warp.sourceBpm == Approx(90.0));
    CHECK(info.length == Approx(16.0));
    CHECK(in.bpm.text() == "90.00");

    in.halveButton.onClick();
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.sourceBpm == Approx(45.0));
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("a typed tempo is clamped to 20..999 and changes only this clip", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-bpm");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto a = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    const auto b = placeClip(h, sound, 32.0, warpOn(WarpMode::beats, 90.0));
    select(h, a);

    auto& in = *h.inspector;
    REQUIRE(in.bpm.commitText("92"));
    h.sync();
    CHECK(h.f.arrangement().clip(a)->warp.sourceBpm == Approx(92.0));
    CHECK(h.f.arrangement().clip(b)->warp.sourceBpm == Approx(90.0));
    CHECK(in.bpm.text() == "92.00");

    REQUIRE(in.bpm.commitText("5000"));
    h.sync();
    CHECK(h.f.arrangement().clip(a)->warp.sourceBpm == Approx(999.0));
    CHECK_FALSE(in.bpm.commitText("fast"));
    CHECK(h.f.violations.isEmpty());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("TRANSPOSE is typed or dragged in whole semitones and greys out in Re-pitch keeping its value", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-transpose");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    select(h, clip);

    auto& in = *h.inspector;
    REQUIRE(in.transpose.commitText("+7"));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.transpose == Approx(7.0));
    CHECK(in.transpose.text() == "+7 st");
    CHECK(h.f.commands->undoName() == "Set clip transpose");

    // A drag is one step, however many values it passes through.
    const auto mid = static_cast<float>(in.transpose.getHeight()) * 0.5f;
    h.drag(in.transpose, {20.0f, mid}, {20.0f, mid - 20.0f}, {20.0f, mid - 40.0f});
    h.sync();
    const auto dragged = h.f.arrangement().clip(clip)->warp.transpose;
    CHECK(dragged > 7.0);
    CHECK(dragged == Approx(std::round(dragged)));
    h.f.commands->undo(); // one Undo takes the whole drag back
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->warp.transpose == Approx(7.0));

    // Re-pitch ignores transpose: the field is greyed and still shows +7.
    in.mode.setSelected(ui::InspectorLogic::modeRepitch, true);
    h.sync();
    CHECK_FALSE(in.transpose.isEnabled());
    CHECK(in.transpose.text() == "+7 st");
    CHECK(h.f.arrangement().clip(clip)->warp.transpose == Approx(7.0));
    CHECK(h.f.violations.isEmpty());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("the warp row is hidden when no clip or several clips are selected", "[arrange][warp][inspector]")
{
    ViewHarness h("wi-hidden");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto a = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    const auto b = placeClip(h, sound, 32.0, warpOn(WarpMode::beats, 90.0));
    h.sync();
    CHECK_FALSE(h.inspector->warpToggle.isVisible());
    h.selection.setClips({a, b});
    CHECK_FALSE(h.inspector->warpToggle.isVisible());
    CHECK_FALSE(h.inspector->mode.isVisible());
    h.selection.setClips({a});
    CHECK(h.inspector->warpToggle.isVisible());
    CHECK(h.inspector->mode.isVisible());
    CHECK(waitForBeatsRenders(h.f));
}

//==============================================================================
// Task 3.2: LENGTH, LOOP and the fades.

namespace
{
/** The second second of a 4 s ramp as a 2-beat clip at beat 2 (as RampSecond in warp_loop.test.cpp). */
juce::String placeBar(ViewHarness& h)
{
    const auto sound = h.f.addSound("ramp", 4.0, "ramp");
    const auto clip = h.f.place(sound, 0, 0.0);
    run(h, cmd::TrimClip(clip, 2.0, 4.0, 0.01)); // beats 2 to 4: one second of the ramp
    return clip;
}
} // namespace

TEST_CASE("LENGTH shows bars and beats and a typed length changes the clip in one step", "[arrange][warp][inspector][length]")
{
    ViewHarness h("wi-length");
    h.soundBpm = 90.0;
    const auto sound = addFixtureSound(h.f, "break-90.wav");
    const auto clip = placeClip(h, sound, 0.0, warpOn(WarpMode::beats, 90.0)); // 4 bars
    select(h, clip);

    auto& in = *h.inspector;
    CHECK(in.length.text() == "4 bars");
    CHECK(in.length.isVisible());

    REQUIRE(in.length.commitText("2"));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->length == Approx(8.0));
    CHECK(in.length.text() == "2 bars");
    CHECK(h.f.commands->undoName() == "Set clip length");

    REQUIRE(in.length.commitText("1.2"));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->length == Approx(6.0));
    CHECK(in.length.text() == "1 bar 2 beats");
    CHECK_FALSE(in.length.commitText("long"));

    h.f.commands->undo();
    h.sync();
    CHECK(in.length.text() == "2 bars");
    CHECK(h.f.violations.isEmpty());
    CHECK(waitForBeatsRenders(h.f));
}

TEST_CASE("without LOOP a typed length stops at the sound; with LOOP it can repeat", "[arrange][warp][inspector][length]")
{
    ViewHarness h("wi-loop");
    const auto clip = placeBar(h); // 2 beats of a 4 s ramp (8 beats): from beat 2, offset 1 s
    select(h, clip);

    auto& in = *h.inspector;
    CHECK_FALSE(in.loopButton.getToggleState());
    REQUIRE(in.length.commitText("8"));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->length == Approx(6.0)); // the rest of the sound: beats 2 to 8
    CHECK(in.length.text() == "1 bar 2 beats");

    h.f.commands->undo();
    h.sync();
    REQUIRE(h.f.arrangement().clip(clip)->length == Approx(2.0));

    in.loopButton.triggerClick();
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->loop);
    CHECK(in.loopButton.getToggleState());
    CHECK(h.f.commands->undoName() == "Set clip loop");

    REQUIRE(in.length.commitText("8"));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->length == Approx(32.0)); // 8 bars, the same bar eight times
    CHECK(in.length.text() == "8 bars");

    in.loopButton.triggerClick(); // LOOP off: back to one pass
    h.sync();
    CHECK_FALSE(h.f.arrangement().clip(clip)->loop);
    CHECK(h.f.arrangement().clip(clip)->length == Approx(2.0));
    CHECK(in.length.text() == "2 beats");
    CHECK_FALSE(in.loopButton.getToggleState());

    h.f.commands->undo();
    h.sync();
    CHECK(in.loopButton.getToggleState());
    CHECK(in.length.text() == "8 bars");
    CHECK(h.f.violations.isEmpty());
}

TEST_CASE("FADE IN and FADE OUT are typed in ms, cut to fit, and a drag is one step", "[arrange][warp][inspector][fades]")
{
    ViewHarness h("wi-fades");
    const auto sound = h.f.addSound("one-second", 1.0, "tone");
    const auto clip = h.f.place(sound, 0, 0.0); // 1 s
    select(h, clip);

    auto& in = *h.inspector;
    CHECK(in.fadeIn.isEnabled());
    CHECK(in.fadeOut.isEnabled());
    CHECK(in.fadeIn.text() == "0 ms");

    REQUIRE(in.fadeOut.commitText("600"));
    h.sync();
    REQUIRE(in.fadeIn.commitText("800"));
    h.sync();
    const auto info = *h.f.arrangement().clip(clip);
    CHECK(info.fadeOut == Approx(0.6));
    CHECK(info.fadeIn == Approx(0.4)); // 800 does not fit next to 600 in 1000
    CHECK(in.fadeIn.text() == "400 ms");
    CHECK(in.fadeOut.text() == "600 ms");
    CHECK(h.f.commands->undoName() == "Set clip fades");
    CHECK(h.f.violations.isEmpty());

    // A drag is one undo step.
    const auto mid = static_cast<float>(in.fadeIn.getHeight()) * 0.5f;
    h.drag(in.fadeIn, {20.0f, mid}, {20.0f, mid + 10.0f}, {20.0f, mid + 20.0f}); // down: shorter
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->fadeIn < 0.3); // the drag changed it
    CHECK(h.f.arrangement().clip(clip)->fadeOut == Approx(0.6));
    h.f.commands->undo(); // one Undo takes the whole drag back
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->fadeIn == Approx(0.4));
    CHECK(in.fadeIn.text() == "400 ms");
}

TEST_CASE("the loop passes and the fades are drawn on the inspector waveform", "[arrange][warp][inspector][fades]")
{
    ViewHarness h("wi-paint");
    const auto clip = placeBar(h);
    select(h, clip);
    run(h, cmd::SetClipLoop(clip, true));
    run(h, cmd::SetClipLength(clip, 8.0));
    run(h, cmd::SetClipFades(clip, 0.5, 0.25));
    h.sync();

    // Painting a looped, faded clip must work (no thumbnail in this harness: the marks and fades are still drawn).
    juce::Image image(juce::Image::ARGB, h.inspector->getWidth(), h.inspector->getHeight(), true);
    juce::Graphics g(image);
    h.inspector->paintEntireComponent(g, false);
    CHECK(image.getWidth() > 0);
    const auto pieces = sampler::ui::waveformPieces(*h.f.arrangement().clip(clip));
    CHECK(pieces.size() == 4);
}

//==============================================================================
// Task 3.3: the timeline.

TEST_CASE("dragging the top-right handle one bar left makes a one-bar fade-out and does not move the clip", "[arrange][warp][fades][view]")
{
    ViewHarness h("wt-fade");
    const auto sound = h.f.addSound("two-bars", 4.0, "tone"); // 8 beats = 2 bars at 120 BPM
    const auto clip = h.f.place(sound, 0, 0.0);
    h.sync();
    auto& lanes = h.view->lanes();
    const auto before = *h.f.arrangement().clip(clip);
    REQUIRE(before.length == Approx(8.0));

    const auto top = 11.0f; // inside the top 8 px of the clip
    lanes.mouseMove(h.event(lanes, {h.xOfBeat(4.0), h.yInTrack(0, top)}, {h.xOfBeat(4.0), h.yInTrack(0, top)}, false));
    CHECK(lanes.hoveredClip() == clip); // the handles are drawn for the clip under the mouse

    h.drag(lanes, {h.xOfBeat(8.0) - 2.0f, h.yInTrack(0, top)}, {h.xOfBeat(6.0), h.yInTrack(0, top)}, {h.xOfBeat(4.0), h.yInTrack(0, top)});
    h.sync();
    const auto after = *h.f.arrangement().clip(clip);
    CHECK(after.fadeOut == Approx(2.0).margin(0.1)); // one bar = 2 s, not snapped, so within a pixel or two
    CHECK(after.fadeIn == 0.0);
    CHECK(after.start == Approx(before.start));
    CHECK(after.length == Approx(before.length));
    CHECK(after.trackId == before.trackId);
    CHECK(h.f.commands->undoName() == "Set clip fades");
    CHECK(h.f.violations.isEmpty());

    h.f.commands->undo(); // one Undo takes the whole drag back
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->fadeOut == 0.0);
}

TEST_CASE("the fade-in handle drags right, is cut by the fade out, and Escape cancels", "[arrange][warp][fades][view]")
{
    ViewHarness h("wt-fade2");
    const auto sound = h.f.addSound("two-bars", 4.0, "tone");
    const auto clip = h.f.place(sound, 0, 0.0);
    run(h, cmd::SetClipFades(clip, 0.0, 3.0));
    h.sync();
    auto& lanes = h.view->lanes();
    const auto top = 11.0f;

    h.drag(lanes, {h.xOfBeat(0.0) + 2.0f, h.yInTrack(0, top)}, {h.xOfBeat(4.0), h.yInTrack(0, top)}, {h.xOfBeat(7.0), h.yInTrack(0, top)});
    h.sync();
    auto info = *h.f.arrangement().clip(clip);
    CHECK(info.fadeOut == Approx(3.0));
    CHECK(info.fadeIn == Approx(1.0).margin(0.05)); // 4 s clip: at most 1 s next to the 3 s fade out
    CHECK(h.f.violations.isEmpty());

    // Escape in the middle of a drag leaves no step and no change.
    const auto name = h.f.commands->undoName();
    const juce::Point<float> from{h.xOfBeat(info.fadeIn * 2.0), h.yInTrack(0, top)};
    lanes.mouseDown(h.event(lanes, from, from, false));
    lanes.mouseDrag(h.event(lanes, {h.xOfBeat(0.5), h.yInTrack(0, top)}, from, true));
    CHECK(lanes.isDragging());
    CHECK(lanes.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    lanes.mouseUp(h.event(lanes, {h.xOfBeat(0.5), h.yInTrack(0, top)}, from, true));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->fadeIn == Approx(info.fadeIn));
    CHECK(h.f.commands->undoName() == name);
    CHECK_FALSE(h.f.commands->isGestureOpen());
}

TEST_CASE("a 1-bar loop dragged to 4 bars shows marks at bars 2, 3 and 4", "[arrange][warp][loop][view]")
{
    ViewHarness h("wt-marks");
    const auto sound = h.f.addSound("ramp", 2.0, "ramp"); // 4 beats = one bar
    const auto clip = h.f.place(sound, 0, 0.0);
    run(h, cmd::SetClipLoop(clip, true));
    h.sync();
    auto& lanes = h.view->lanes();

    // The right edge, below the handle band, dragged to the end of bar 4 (beat 16).
    const auto y = h.yInTrack(0, 50.0f);
    h.drag(lanes, {h.xOfBeat(4.0) - 2.0f, y}, {h.xOfBeat(10.0), y}, {h.xOfBeat(16.0) + 1.0f, y});
    h.sync();
    const auto info = *h.f.arrangement().clip(clip);
    CHECK(info.length == Approx(16.0)); // past the sound, because it loops
    CHECK(h.f.commands->undoName() == "Trim clip");
    CHECK(h.f.violations.isEmpty());

    const auto pieces = sampler::ui::waveformPieces(info);
    REQUIRE(pieces.size() == 4);
    for (size_t i = 1; i < pieces.size(); ++i)
        CHECK(pieces[i].from * info.length == Approx(4.0 * static_cast<double>(i))); // marks at bars 2, 3 and 4

    // And it paints, with its marks, without a thumbnail too.
    juce::Image image(juce::Image::ARGB, lanes.getWidth(), lanes.getHeight(), true);
    juce::Graphics g(image);
    lanes.paintEntireComponent(g, false);
    CHECK(lanes.clipsDrawnInLastPaint() == 1);
}

TEST_CASE("without LOOP the right edge stops at the end of the sound", "[arrange][warp][loop][view]")
{
    ViewHarness h("wt-noloop");
    const auto sound = h.f.addSound("ramp", 2.0, "ramp");
    const auto clip = h.f.place(sound, 0, 0.0);
    h.sync();
    const auto y = h.yInTrack(0, 50.0f);
    h.drag(h.view->lanes(), {h.xOfBeat(4.0) - 2.0f, y}, {h.xOfBeat(10.0), y}, {h.xOfBeat(16.0) + 1.0f, y});
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->length == Approx(4.0));
}
