// M2: the arrangement view, its header and the clip inspector driven by synthetic events on a headless engine.
#include "ViewHarness.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::test::ViewHarness;
namespace cmd = sampler::commands;
namespace ui = sampler::ui;

TEST_CASE("the view shows 4 tracks and 14 clips where the geometry puts them", "[arrange][view]")
{
    ViewHarness h("view-14");
    const auto bar = h.f.addSound("bar", 2.0);
    for (int i = 0; i < 3; ++i)
        h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    const int perTrack[] = {5, 3, 4, 2};
    for (int t = 0; t < 4; ++t)
        for (int c = 0; c < perTrack[t]; ++c)
            h.f.place(bar, t, c * 8.0);
    h.sync();
    REQUIRE(h.view->trackList().size() == 4);
    REQUIRE(h.view->laneClips().size() == 14);
    CHECK(h.view->headers().numHeaders() == 4);
    for (const auto& clip : h.f.arrangement().clips())
    {
        const auto bounds = h.view->lanes().boundsOfClip(clip.id);
        const auto track = h.f.arrangement().trackIndex(clip.trackId);
        CHECK(bounds.getX() == juce::roundToInt(clip.start * 20.0));
        CHECK(bounds.getWidth() == juce::roundToInt(clip.length * 20.0));
        CHECK(bounds.getY() == track * 112 + 8);
    }
    // Painting the whole view works and draws every visible clip.
    juce::Image image(juce::Image::RGB, h.view->lanes().getWidth(), h.view->lanes().getHeight(), true);
    juce::Graphics g(image);
    h.view->lanes().paintEntireComponent(g, false);
    CHECK(h.view->lanes().clipsDrawnInLastPaint() == 14);
}

TEST_CASE("dropping a browser sound on a lane and below the last lane", "[arrange][view][drop]")
{
    ViewHarness h("view-drop");
    const auto loop = h.f.addSound("drums-loop-120", 8.0);
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.sync();

    // Track 2 at about bar 5 with 1-bar snap: beat 16.6 snaps to 16.
    h.dropSample(loop, {h.xOfBeat(16.6), h.yInTrack(1)});
    h.sync();
    auto clips = h.f.clipsOn(1);
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == Approx(16.0));
    CHECK(clips[0].length == Approx(16.0));
    CHECK(h.selection.isSelected(clips[0].id)); // the new clip is selected

    // Below the last lane: a new track with the clip; one undo removes both.
    h.dropSample(loop, {h.xOfBeat(0.0), h.yInTrack(2, 20.0f)});
    h.sync();
    REQUIRE(h.f.arrangement().numTracks() == 3);
    CHECK(h.f.clipsOn(2).size() == 1);
    h.f.commands->undo();
    h.sync();
    CHECK(h.f.arrangement().numTracks() == 2);
    CHECK(h.f.violations.isEmpty());
}

TEST_CASE("header: Play lights only when the transport really plays; typing a position; ruler click locates",
          "[arrange][view][transport]")
{
    ViewHarness h("view-transport");
    auto& transport = h.f.project->transport();

    // The headless engine has no output, so a pressed Play stays pending and the button stays dark.
    h.headerBar->playButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    h.headerBar->tick(16.0);
    CHECK(transport.isPlayPending());
    CHECK_FALSE(transport.isPlaying());
    CHECK_FALSE(h.headerBar->playButton.isLit());
    h.headerBar->stopButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK_FALSE(transport.isPlayPending());

    // Typing "5.2" moves the playhead to bar 5 beat 2; bad text is refused.
    h.headerBar->position.startEditing();
    REQUIRE(h.headerBar->position.textEditor() != nullptr);
    CHECK_FALSE(h.headerBar->position.commit("abc"));
    CHECK(h.headerBar->position.commit("5.2"));
    CHECK(transport.positionBeats() == Approx(17.0));
    h.headerBar->tick(16.0);
    CHECK(h.headerBar->position.text() == "5 . 2 . 1");

    // A ruler click at about bar 3 locates there (snapped to the bar).
    auto& ruler = h.view->ruler();
    h.click(ruler, {h.xOfBeat(8.4), 25.0f});
    CHECK(transport.positionBeats() == Approx(8.0));
    // Second stop goes back to the start (looping off).
    h.headerBar->stopPressed();
    CHECK(transport.positionBeats() == Approx(0.0));
}

TEST_CASE("header: a second Stop while a recording runs does not try to move the locked playhead", "[arrange][view][transport]")
{
    ViewHarness h("view-stop-recording");
    auto& transport = h.f.project->transport();
    int refused = 0;
    transport.onLocateRefused = [&refused] { ++refused; };
    transport.setLocateLocked(true);
    bool recording = true;
    ui::HeaderContext header{h.f.project->arrangement(), transport, h.f.project->levelMeters(), *h.f.commands, h.selection,
                             [&h] { return h.snap; }, [&h](ui::SnapValue v) { h.snap = v; }, [] {}, [&recording] { return recording; }};
    ui::HeaderBar bar(header);
    bar.stopPressed();
    CHECK(refused == 0);
    recording = false;
    transport.setLocateLocked(false);
    bar.stopPressed();
    CHECK(refused == 0);
}

TEST_CASE("selecting with clicks and a marquee across two tracks", "[arrange][view][selection]")
{
    ViewHarness h("view-select");
    const auto bar = h.f.addSound("bar", 2.0);
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto a = h.f.place(bar, 0, 8.0);   // bar 3
    const auto b = h.f.place(bar, 0, 20.0);  // bar 6
    const auto c = h.f.place(bar, 1, 12.0);  // bar 4
    const auto d = h.f.place(bar, 1, 40.0);  // bar 11
    h.sync();
    auto& lanes = h.view->lanes();

    h.click(lanes, {h.xOfBeat(9.0), h.yInTrack(0)});
    CHECK(h.selection.clips() == std::vector<juce::String>{a});
    h.click(lanes, {h.xOfBeat(41.0), h.yInTrack(1)}, juce::ModifierKeys::shiftModifier);
    CHECK(h.selection.clips().size() == 2);
    h.click(lanes, {h.xOfBeat(9.0), h.yInTrack(0)}, juce::ModifierKeys::commandModifier);
    CHECK(h.selection.clips() == std::vector<juce::String>{d});

    // A box from an empty spot on track 1 across bars 3 to 6 down to track 2.
    h.drag(lanes, {h.xOfBeat(8.0), h.yInTrack(0, 4.0f)}, {h.xOfBeat(14.0), h.yInTrack(1)}, {h.xOfBeat(21.0), h.yInTrack(1)});
    CHECK(h.selection.isSelected(a));
    CHECK(h.selection.isSelected(b));
    CHECK(h.selection.isSelected(c));
    CHECK_FALSE(h.selection.isSelected(d));
    CHECK(h.f.commands->undoName() == "Add clip"); // selecting is not an undo step

    h.click(lanes, {h.xOfBeat(60.0), h.yInTrack(0)}); // empty lane space clears
    CHECK(h.selection.isEmpty());
}

TEST_CASE("mouse editing: move across tracks, left trim, cancelled drag, Alt bypasses snap", "[arrange][view][drag]")
{
    ViewHarness h("view-drag");
    const auto loop = h.f.addSound("loop4", 8.0, "ramp");
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto clip = h.f.place(loop, 0, 0.0);
    h.sync();
    auto& lanes = h.view->lanes();

    // Move from track 1 to track 3, about two bars right.
    h.drag(lanes, {h.xOfBeat(2.0), h.yInTrack(0)}, {h.xOfBeat(5.0), h.yInTrack(1)}, {h.xOfBeat(10.3), h.yInTrack(2)});
    h.sync();
    auto info = h.f.arrangement().clip(clip);
    CHECK(info->trackId == h.f.trackId(2));
    CHECK(info->start == Approx(8.0));
    CHECK(h.f.commands->undoName() == "Move clip");

    // Left trim to bar 2 keeps the audio in place.
    h.drag(lanes, {h.xOfBeat(8.0) + 2.0f, h.yInTrack(2)}, {h.xOfBeat(10.0), h.yInTrack(2)}, {h.xOfBeat(12.2), h.yInTrack(2)});
    h.sync();
    info = h.f.arrangement().clip(clip);
    CHECK(info->start == Approx(12.0));
    CHECK(info->offsetSeconds == Approx(2.0));
    CHECK(h.f.commands->undoName() == "Trim clip");

    // A drag cancelled with Escape changes nothing and leaves no undo step.
    const auto undoBefore = h.f.project->undoManager().getUndoDescriptions().size();
    const juce::Point<float> from{h.xOfBeat(16.0), h.yInTrack(2)};
    lanes.mouseDown(h.event(lanes, from, from, false));
    lanes.mouseDrag(h.event(lanes, {h.xOfBeat(30.0), h.yInTrack(0)}, from, true));
    CHECK(lanes.isDragging());
    CHECK(lanes.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    lanes.mouseUp(h.event(lanes, {h.xOfBeat(30.0), h.yInTrack(0)}, from, true));
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->start == Approx(12.0));
    CHECK(h.f.project->undoManager().getUndoDescriptions().size() == undoBefore);

    // Alt: the clip lands exactly where it was released.
    h.drag(lanes, {h.xOfBeat(16.0), h.yInTrack(2)}, {h.xOfBeat(17.0), h.yInTrack(2)}, {h.xOfBeat(17.5), h.yInTrack(2)},
           juce::ModifierKeys::altModifier);
    h.sync();
    CHECK(h.f.arrangement().clip(clip)->start == Approx(13.5));
    CHECK(h.f.violations.isEmpty());
}

TEST_CASE("snap 1/8T puts a moved clip on thirds of a beat", "[arrange][view][snap]")
{
    ViewHarness h("view-snap");
    const auto bar = h.f.addSound("bar", 2.0);
    const auto clip = h.f.place(bar, 0, 0.0);
    h.snap = ui::SnapValue::eighthTriplet;
    h.sync();
    h.drag(h.view->lanes(), {h.xOfBeat(1.0), h.yInTrack(0)}, {h.xOfBeat(2.0), h.yInTrack(0)}, {h.xOfBeat(2.4), h.yInTrack(0)});
    h.sync();
    const auto start = h.f.arrangement().clip(clip)->start;
    CHECK(std::abs(start * 3.0 - std::round(start * 3.0)) < 1.0e-6);
    CHECK(start == Approx(4.0 / 3.0));
}

TEST_CASE("zoom: Mod+wheel around the mouse, wheel scrolls, Shift+Z shows a song ending at bar 40", "[arrange][view][zoom]")
{
    ViewHarness h("view-zoom");
    const auto bar = h.f.addSound("bar", 2.0);
    for (int i = 0; i < 5; ++i) // more lanes than fit, so the wheel can scroll
        h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.f.place(bar, 0, 156.0); // ends at the end of bar 40
    h.sync();
    auto& lanes = h.view->lanes();
    auto& vp = h.view->viewport();

    const auto beatUnder = vp.xToBeat(300.0);
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.5f;
    lanes.mouseWheelMove(h.event(lanes, {300.0f, 50.0f}, {300.0f, 50.0f}, false, juce::ModifierKeys::commandModifier), wheel);
    CHECK(vp.pixelsPerBeat > 20.0);
    CHECK(vp.xToBeat(300.0) == Approx(beatUnder));

    wheel.deltaY = -0.5f;
    lanes.mouseWheelMove(h.event(lanes, {300.0f, 50.0f}, {300.0f, 50.0f}, false), wheel);
    CHECK(vp.scrollY > 0.0); // plain wheel scrolls vertically

    h.view->zoomToSong();
    CHECK(vp.firstBeat == Approx(0.0));
    CHECK(vp.beatToX(160.0) == Approx(vp.widthPx));

    // The scroll bar's end shows the last bars with room after the song.
    auto& bar2 = h.view->horizontalScrollBar();
    vp.pixelsPerBeat = 20.0;
    h.view->viewChanged();
    bar2.setCurrentRangeStart(bar2.getMaximumRangeLimit(), juce::sendNotificationSync);
    CHECK(vp.lastBeat() == Approx(192.0).margin(0.5)); // 8 bars past the song's end
}

TEST_CASE("track headers: reorder drag, small movement is a click, rename keeps letters", "[arrange][view][tracks]")
{
    ViewHarness h("view-headers");
    for (int i = 0; i < 2; ++i)
        h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.sync();
    auto& list = h.view->headers();
    const auto third = h.f.trackId(2);

    // Drag the third header above the first.
    auto* header = list.header(2);
    REQUIRE(header != nullptr);
    header->mouseDown(h.event(*header, {60.0f, 20.0f}, {60.0f, 20.0f}, false));
    list.headerDragged({61, 2 * 112 + 22});
    CHECK_FALSE(list.isReordering()); // under 4 px
    list.headerDragged({60, 5});
    CHECK(list.isReordering());
    CHECK(list.reorderTargetIndex() == 0);
    list.headerReleased();
    h.sync();
    CHECK(h.f.trackId(0) == third);
    CHECK(h.f.commands->undoName() == "Move track");

    // A press with a tiny movement only selects.
    header = list.header(1);
    header->mouseDown(h.event(*header, {60.0f, 20.0f}, {60.0f, 20.0f}, false));
    list.headerDragged({62, 112 + 21});
    list.headerReleased();
    h.sync();
    CHECK(h.selection.track() == h.f.trackId(1));
    CHECK(h.f.trackId(0) == third);

    // Rename: letters go into the name, nothing is muted or soloed.
    header = list.header(0);
    header->startRename();
    REQUIRE(header->renameEditor() != nullptr);
    header->renameEditor()->setText("ms drums", false);
    header->renameEditor()->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    h.sync();
    CHECK(h.f.arrangement().tracks()[0].name == "ms drums");
    CHECK_FALSE(h.f.arrangement().tracks()[0].mute);
    CHECK_FALSE(h.f.arrangement().tracks()[0].solo);
    CHECK(h.f.commands->undoName() == "Rename track");
}

TEST_CASE("header controls: volume drag is one step, double-click resets, Mod+click solo is exclusive", "[arrange][view][mixer]")
{
    ViewHarness h("view-mixer");
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.sync();
    auto* header = h.view->headers().header(0);
    REQUIRE(header != nullptr);
    auto& volume = header->volume;

    // Drag down through many values: one undo step back to 0 dB.
    volume.mouseDown(h.event(volume, {20.0f, 10.0f}, {20.0f, 10.0f}, false));
    for (int y = 12; y <= 60; y += 4)
        volume.mouseDrag(h.event(volume, {20.0f, static_cast<float>(y)}, {20.0f, 10.0f}, true));
    volume.mouseUp(h.event(volume, {20.0f, 60.0f}, {20.0f, 10.0f}, true));
    h.sync();
    const auto dragged = h.f.arrangement().tracks()[0].volumeDb;
    CHECK(dragged < -5.0);
    CHECK(h.f.commands->undoName() == "Set volume");
    h.f.commands->undo();
    h.sync();
    CHECK(h.f.arrangement().tracks()[0].volumeDb == Approx(0.0).margin(0.05));

    // Past +6 it stays at +6.
    volume.mouseDown(h.event(volume, {20.0f, 10.0f}, {20.0f, 10.0f}, false));
    volume.mouseDrag(h.event(volume, {20.0f, -500.0f}, {20.0f, 10.0f}, true));
    volume.mouseUp(h.event(volume, {20.0f, -500.0f}, {20.0f, 10.0f}, true));
    h.sync();
    CHECK(h.f.arrangement().tracks()[0].volumeDb == Approx(6.0).margin(0.05));
    volume.mouseDoubleClick(h.event(volume, {20.0f, 10.0f}, {20.0f, 10.0f}, false, {}, 2));
    h.sync();
    CHECK(h.f.arrangement().tracks()[0].volumeDb == Approx(0.0).margin(0.05));

    // Solo tracks 1 and 2, then Mod+click solo on track 3: only track 3.
    h.view->headers().header(0)->soloButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    h.view->headers().header(1)->soloButton.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    h.sync();
    CHECK(h.f.arrangement().tracks()[0].solo);
    CHECK(h.f.arrangement().tracks()[1].solo);
    auto& solo3 = h.view->headers().header(2)->soloButton;
    juce::ModifierKeys::currentModifiers = juce::ModifierKeys::commandModifier;
    solo3.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    juce::ModifierKeys::currentModifiers = {};
    h.sync();
    CHECK_FALSE(h.f.arrangement().tracks()[0].solo);
    CHECK_FALSE(h.f.arrangement().tracks()[1].solo);
    CHECK(h.f.arrangement().tracks()[2].solo);

    // R arms: see the arm tests below.
}

TEST_CASE("loop bar: draw a range, drag its end, one undo step each", "[arrange][view][loop]")
{
    ViewHarness h("view-loop");
    h.sync();
    auto& ruler = h.view->ruler();
    // Draw across the empty loop bar from bar 5 to bar 9: bars 5 to 8.
    h.drag(ruler, {h.xOfBeat(16.2), 5.0f}, {h.xOfBeat(24.0), 5.0f}, {h.xOfBeat(31.8), 5.0f});
    auto loop = h.f.arrangement().loopRange();
    CHECK(loop.start == Approx(16.0));
    CHECK(loop.end == Approx(32.0));
    CHECK(h.f.commands->undoName() == "Set loop");
    // Drag the end edge right by about two bars: bars 5 to 10.
    h.drag(ruler, {h.xOfBeat(32.0), 5.0f}, {h.xOfBeat(36.0), 5.0f}, {h.xOfBeat(39.7), 5.0f});
    loop = h.f.arrangement().loopRange();
    CHECK(loop.end == Approx(40.0));
    h.f.commands->undo();
    CHECK(h.f.arrangement().loopRange().end == Approx(32.0));
}

TEST_CASE("clip inspector: one, none and several clips, and it follows edits", "[arrange][view][inspector]")
{
    ViewHarness h("view-inspector");
    const auto loop = h.f.addSound("Break 90", 8.0);
    h.f.commands->apply(std::make_unique<cmd::RenameTrack>(h.f.trackId(0), "Drums"));
    const auto a = h.f.place(loop, 0, 16.0); // bars 5 to 8
    const auto b = h.f.place(loop, 0, 48.0);
    h.sync();
    CHECK(h.inspector->titleText() == "No clip selected");
    CHECK_FALSE(h.inspector->showsClip());

    h.selection.setClips({a});
    CHECK(h.inspector->titleText() == "Break 90");
    CHECK(h.inspector->detailText() == juce::String::fromUTF8("Drums \xc2\xb7 Bars 5\xe2\x80\x93" "8"));
    CHECK(h.inspector->gain.isVisible());
    CHECK_FALSE(h.inspector->fadeIn.isEnabled());
    CHECK(h.inspector->openEditor.isEnabled()); // the sound's file is there

    // Moved two bars in the timeline: the panel follows at once.
    h.f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{a, 0, 24.0}}));
    h.sync();
    CHECK(h.inspector->detailText().endsWith(juce::String::fromUTF8("Bars 7\xe2\x80\x93" "10")));
    CHECK(h.inspector->start.text() == "7 . 1 . 1");

    // Typing a start moves the clip.
    REQUIRE(h.inspector->start.commitText("9.1.1"));
    h.sync();
    CHECK(h.f.arrangement().clip(a)->start == Approx(32.0));
    CHECK(h.f.commands->undoName() == "Set clip start");
    CHECK_FALSE(h.inspector->start.commitText("bar nine"));

    // Gain typed.
    REQUIRE(h.inspector->gain.commitText("-6"));
    h.sync();
    CHECK(h.f.arrangement().clip(a)->gainDb == Approx(-6.0));
    CHECK(h.f.commands->undoName() == "Set clip gain");

    h.selection.setClips({a, b});
    CHECK(h.inspector->titleText() == "2 clips selected");
    CHECK_FALSE(h.inspector->gain.isVisible());
}

TEST_CASE("bars text", "[arrange][view][inspector]")
{
    CHECK(ui::ClipInspector::barsText(0.0, 16.0, 4) == juce::String::fromUTF8("Bars 1\xe2\x80\x93" "4"));
    CHECK(ui::ClipInspector::barsText(16.0, 17.0, 4) == juce::String::fromUTF8("Bars 5\xe2\x80\x93" "5"));
    CHECK(ui::ClipInspector::barsText(3.0, 9.0, 3) == juce::String::fromUTF8("Bars 2\xe2\x80\x93" "3"));
}
