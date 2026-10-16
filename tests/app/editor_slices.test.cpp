// M3 2.4: chopping in the sample editor: modes, preview, slice points, the strip and the slice panel.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "EditorFixture.h"
#include "commands/SliceCommands.h"

using namespace sampler::test;

namespace
{
struct Chop : Session
{
    explicit Chop(const juce::String& name) : Session(name)
    {
        main.browser().onRowOpened(id);
        pump();
    }

    sampler::ui::SampleEditorView& ed() { return editor(); }

    juce::Point<float> pointAt(juce::int64 frame, float y = 80.0f)
    {
        return {static_cast<float>(ed().waveform().viewport().frameToX(static_cast<double>(frame))), y};
    }

    void chooseMode(int index)
    {
        ed().modeControl.setSelected(index, true);
        pump(20);
    }

    void undo()
    {
        main.invoke(sampler::app::undoCommand);
        pump(20);
    }

    juce::MouseEvent withMods(juce::Component& target, juce::Point<float> pos, juce::Point<float> downPos, bool dragged, juce::ModifierKeys mods)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent(source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), downPos,
                                juce::Time::getCurrentTime(), 1, dragged);
    }
};
} // namespace

TEST_CASE("Beat mode: a quarter-note grid and a finer one, each one undo step, with Sensitivity greyed out", "[app][editor][slices]")
{
    Chop c("chop-beat");
    CHECK(c.ed().counterText() == "1 slice");
    CHECK_FALSE(c.ed().sensitivityField.isEnabled());
    CHECK_FALSE(c.ed().divisionControl.isEnabled()); // Manual: both greyed out
    CHECK_FALSE(c.ed().autoSliceButton.isEnabled());

    c.chooseMode(0);
    CHECK(c.controller.commandService().undoName() == "Slice by beat");
    CHECK(c.ed().counterText() == "17 slices"); // 16 beats from the first kick, plus the 100 ms before it
    CHECK(c.ed().divisionControl.isEnabled());
    CHECK_FALSE(c.ed().sensitivityField.isEnabled());
    CHECK(c.ed().waveform().markers().size() == 16);
    const auto firstBeat = c.ed().currentTempo().firstBeatFrame; // the first kick, to within a sample
    CHECK(std::abs(firstBeat - 4410) <= 2);
    CHECK(c.ed().waveform().markers()[0] == firstBeat);

    c.ed().divisionControl.setSelected(2, true); // 1/16
    pump(20);
    CHECK(c.ed().counterText() == "65 slices");
    CHECK(c.controller.commandService().undoName() == "Slice by beat");
    c.undo();
    CHECK(c.ed().counterText() == "17 slices");
    CHECK(c.ed().divisionControl.selected() == 0);
}

TEST_CASE("Transient mode: the Sensitivity preview leaves the project alone and Auto-slice keeps it as one step", "[app][editor][slices]")
{
    Chop c("chop-transient");
    c.chooseMode(1);
    CHECK(c.controller.commandService().undoName() == "Slice by transient");
    CHECK(c.ed().counterText() == "17 slices"); // the 16 hits of the break
    CHECK(c.ed().sensitivityField.isEnabled());
    CHECK(c.ed().autoSliceButton.isEnabled());
    CHECK_FALSE(c.ed().divisionControl.isEnabled());

    const auto token = c.controller.commandService().historyToken();
    const auto name = c.controller.commandService().undoName();
    const auto markersBefore = c.ed().waveform().markers();
    c.ed().sensitivityField.setValue(5, true);
    const auto lowCount = c.ed().counterText();
    c.ed().sensitivityField.setValue(100, true);
    CHECK(c.ed().isPreviewing());
    CHECK(c.ed().counterText() != lowCount);
    CHECK(c.controller.commandService().historyToken() == token); // nothing was applied
    CHECK(c.controller.commandService().undoName() == name);
    CHECK(c.ed().waveform().markers() == markersBefore);

    c.ed().autoSliceButton.onClick();
    pump(20);
    CHECK_FALSE(c.ed().isPreviewing());
    CHECK(c.controller.commandService().undoName() == "Auto-slice");
    CHECK(c.controller.commandService().historyToken() != token);
    CHECK(c.ed().waveform().markers().size() >= markersBefore.size());
    c.undo();
    CHECK(c.ed().waveform().markers() == markersBefore);
}

TEST_CASE("Manual mode: double-click adds a point, double-click on it removes it, two Undos", "[app][editor][slices]")
{
    Chop c("chop-manual");
    c.chooseMode(0); // start from the beat grid so the switch to Manual is visible
    REQUIRE(c.ed().waveform().markers().size() == 16);
    auto& wave = c.ed().waveform();

    const juce::int64 middle = c.ed().currentTempo().firstBeatFrame + 29400 * 2 + 10000; // inside slice 3 of the grid
    const auto at = c.pointAt(middle);
    wave.mouseDoubleClick(c.event(wave, at, at, false));
    pump(20);
    CHECK(wave.markers().size() == 17);
    CHECK(c.controller.commandService().undoName() == "Add slice point");
    CHECK(c.ed().modeControl.selected() == 2); // the mode now says Manual
    CHECK(c.ed().counterText() == "18 slices");

    // The new point is within a few samples of the click (it snapped to a zero crossing).
    juce::int64 added = 0;
    for (const auto m : wave.markers())
        if (std::abs(m - middle) < 100)
            added = m;
    REQUIRE(added != 0);
    const auto onPoint = c.pointAt(added);
    wave.mouseDoubleClick(c.event(wave, onPoint, onPoint, false));
    pump(20);
    CHECK(wave.markers().size() == 16);
    CHECK(c.controller.commandService().undoName() == "Remove slice point");
    c.undo();
    CHECK(c.controller.commandService().undoName() == "Add slice point");
    c.undo();
    CHECK(c.controller.commandService().undoName() == "Slice by beat");
    CHECK(wave.markers().size() == 16);
}

TEST_CASE("dragging a slice point snaps to a zero crossing, Alt drops it exactly, and the drag is one step", "[app][editor][slices]")
{
    Chop c("chop-drag");
    c.chooseMode(0);
    auto& wave = c.ed().waveform();
    const auto original = wave.markers()[3];
    const auto start = c.pointAt(original);

    // Drag three pixels to the right.
    const auto target = juce::Point<float>(start.x + 3.0f, start.y);
    wave.mouseDown(c.event(wave, start, start, false));
    wave.mouseDrag(c.event(wave, {start.x + 1.0f, start.y}, start, true));
    wave.mouseDrag(c.event(wave, target, start, true));
    wave.mouseUp(c.event(wave, target, start, true));
    pump(20);
    CHECK(c.controller.commandService().undoName() == "Move slice point");
    CHECK(wave.markers().size() == 16);
    const auto moved = wave.markers()[3];
    CHECK(moved > original);
    CHECK(c.ed().modeControl.selected() == 2);

    // The point sits on a zero crossing of the sound (the mono mix).
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(fixture("break-90.wav")));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> samples(2, 4);
    reader->read(&samples, 0, 4, moved - 2, true, true);
    const auto mono = [&](int i) { return samples.getSample(0, i) + samples.getSample(1, i); };
    bool crossing = false;
    for (int i = 1; i < 4; ++i)
        crossing = crossing || (mono(i - 1) <= 0.0f && mono(i) > 0.0f) || (mono(i - 1) >= 0.0f && mono(i) < 0.0f);
    CHECK(crossing);

    c.undo();
    CHECK(wave.markers()[3] == original);
    CHECK(c.ed().modeControl.selected() == 0); // Beat mode is back with the point

    // Alt: exactly under the mouse.
    const auto here = c.pointAt(wave.markers()[3]);
    const auto there = juce::Point<float>(here.x + 2.0f, here.y);
    const juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
    wave.mouseDown(c.withMods(wave, here, here, false, alt));
    wave.mouseDrag(c.withMods(wave, {here.x + 1.0f, here.y}, here, true, alt));
    wave.mouseDrag(c.withMods(wave, there, here, true, alt));
    wave.mouseUp(c.withMods(wave, there, here, true, alt));
    pump(20);
    CHECK(wave.markers()[3] == wave.viewport().xToFrame(static_cast<double>(juce::roundToInt(there.x)))); // the mouse event holds whole pixels
}

TEST_CASE("a point dragged onto its neighbour stops 5 ms before it, and Delete removes the selected point", "[app][editor][slices]")
{
    Chop c("chop-limits");
    c.chooseMode(0);
    auto& wave = c.ed().waveform();
    const auto neighbour = wave.markers()[4];
    const auto start = c.pointAt(wave.markers()[3]);
    const auto onto = c.pointAt(neighbour);
    const juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
    wave.mouseDown(c.withMods(wave, start, start, false, alt));
    wave.mouseDrag(c.withMods(wave, {start.x + 2.0f, start.y}, start, true, alt));
    wave.mouseDrag(c.withMods(wave, {onto.x + 40.0f, onto.y}, start, true, alt)); // well past the neighbour
    wave.mouseUp(c.withMods(wave, {onto.x + 40.0f, onto.y}, start, true, alt));
    pump(20);
    CHECK(wave.markers()[3] == neighbour - 221);

    // The point is selected by the click; Delete removes it.
    CHECK(wave.selectedMarker() == 3);
    const auto count = wave.markers().size();
    CHECK(wave.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    pump(20);
    CHECK(wave.markers().size() == count - 1);
    CHECK(c.controller.commandService().undoName() == "Remove slice point");
}

TEST_CASE("the slice strip selects a slice, shades it and fills the panel", "[app][editor][slices]")
{
    Chop c("chop-strip");
    c.chooseMode(0);
    auto& strip = c.ed().sliceStrip();
    REQUIRE(strip.getWidth() > 100);

    const auto span = strip.cellSpan(2); // cell 3
    REQUIRE(span.second > span.first);
    const auto x = static_cast<float>((span.first + span.second) / 2.0);
    strip.mouseDown(c.event(strip, {x, 10.0f}, {x, 10.0f}, false));
    CHECK(c.ed().selectedSlice() == 2);
    CHECK(c.ed().waveform().selectedSlice() == 2);
    CHECK(strip.selectedSlice() == 2);
    CHECK(c.ed().slicePanel().slice().index == 2);
    CHECK(c.ed().slicePanel().slice().startFrame == c.ed().waveform().markers()[1]);
    CHECK(c.ed().slicePanel().slice().endFrame == c.ed().waveform().markers()[2]);
    CHECK(c.controller.commandService().undoName() == "Slice by beat"); // selecting is not an undo step

    // A click on the waveform inside slice 5 selects it too.
    auto& wave = c.ed().waveform();
    const auto inside = c.pointAt((wave.markers()[3] + wave.markers()[4]) / 2);
    wave.mouseDown(c.event(wave, inside, inside, false));
    wave.mouseUp(c.event(wave, inside, inside, false));
    CHECK(c.ed().selectedSlice() == 4);
}

TEST_CASE("typing an END moves the point, in bars, seconds or samples; a refused value comes back", "[app][editor][slices]")
{
    Chop c("chop-bounds");
    c.chooseMode(0);
    c.ed().selectSlice(1); // 1.1.1 to 1.2.1
    auto& panel = c.ed().slicePanel();
    CHECK(panel.startField.text() == "1.1.1");
    CHECK(panel.endField.text() == "1.2.1");

    REQUIRE(panel.endField.commitText("1.2.3"));
    pump(20);
    CHECK(c.controller.commandService().undoName() == "Set slice end");
    CHECK(c.ed().waveform().markers()[1] == c.ed().currentTempo().firstBeatFrame + 29400 + 2 * 7350);
    CHECK(panel.endField.text() == "1.2.3");
    CHECK(c.ed().modeControl.selected() == 2);

    // Past the next point: refused, the old value is shown again, no step.
    const auto name = c.controller.commandService().undoName();
    REQUIRE(panel.endField.commitText("1.4.1"));
    pump(20);
    CHECK(panel.endField.text() == "1.2.3");
    CHECK(c.controller.commandService().undoName() == name);
    CHECK(c.ed().noticeText().isNotEmpty());

    // The first slice's start and the last slice's end are the ends of the sound.
    c.ed().selectSlice(0);
    CHECK_FALSE(panel.startField.isEnabled());
    c.ed().selectSlice(c.ed().sliceCount() - 1);
    CHECK_FALSE(panel.endField.isEnabled());

    // Units: samples show sample numbers of the sound.
    c.ed().selectSlice(1);
    panel.unitsControl.setSelected(2, true);
    CHECK(panel.startField.text() == juce::String(c.ed().currentTempo().firstBeatFrame));
    CHECK(c.ed().units() == sampler::ui::editor::Units::samples);
    REQUIRE(panel.startField.commitText("4900"));
    pump(20);
    CHECK(c.ed().waveform().markers()[0] == 4900);
    panel.unitsControl.setSelected(1, true);
    CHECK(panel.startField.text().endsWith(" s"));
}

TEST_CASE("pressing Enter in START or END without typing leaves the point where it is", "[app][editor][slices]")
{
    Chop c("chop-enter");
    c.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(c.id, 50000));
    c.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(c.id, 123457));
    pump(20);
    c.ed().selectSlice(1);
    auto& panel = c.ed().slicePanel();
    const auto name = c.controller.commandService().undoName();
    const auto token = c.controller.commandService().historyToken();

    for (const int units : {0, 1, 2})
    {
        panel.unitsControl.setSelected(units, true);
        REQUIRE(panel.startField.commitText(panel.startField.text()));
        REQUIRE(panel.endField.commitText(panel.endField.text()));
        pump(20);
        CHECK(c.ed().waveform().markers() == std::vector<juce::int64>{50000, 123457});
    }
    CHECK(c.controller.commandService().undoName() == name);
    CHECK(c.controller.commandService().historyToken() == token);
}

TEST_CASE("a selected slice point does not carry over to another sound or survive an added point", "[app][editor][slices]")
{
    Chop c("chop-point-select");
    c.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(c.id, 50000));
    pump(20);
    auto& wave = c.ed().waveform();
    wave.mouseDown(c.event(wave, c.pointAt(50000), c.pointAt(50000), false));
    wave.mouseUp(c.event(wave, c.pointAt(50000), c.pointAt(50000), false));
    CHECK(wave.selectedMarker() == 0);

    c.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(c.id, 20000));
    pump(20);
    CHECK(wave.selectedMarker() == -1); // the indices moved

    wave.mouseDown(c.event(wave, c.pointAt(50000), c.pointAt(50000), false));
    wave.mouseUp(c.event(wave, c.pointAt(50000), c.pointAt(50000), false));
    CHECK(wave.selectedMarker() == 1);
    c.ed().close();
    c.ed().open(c.id);
    CHECK(wave.selectedMarker() == -1);
}

TEST_CASE("an unanalysed sound cannot be sliced by beat or transient yet", "[app][editor][slices]")
{
    juce::File target;
    {
        Session first("chop-wait-a");
        target = first.controller.project().bundleDir().getParentDirectory().getChildFile("Saved.sdaw");
        first.controller.chooseSaveLocation = [&target](std::function<void(juce::File)> answer) { answer(target); };
        first.controller.requestSave();
    }
    REQUIRE(target.getChildFile("cache").deleteRecursively());
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(Session::options("chop-wait-b"));
    sampler::app::MainComponent main(controller);
    REQUIRE(controller.openProject(target));
    const auto id = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0).id();
    main.openEditor(id);
    REQUIRE(main.sampleEditor()->isAnalysing());
    CHECK_FALSE(main.sampleEditor()->tempoField.isEnabled());
    main.sampleEditor()->modeControl.setSelected(0, true);
    CHECK(main.sampleEditor()->noticeText().isNotEmpty());
    CHECK(main.sampleEditor()->modeControl.selected() == 2); // stays on Manual
    CHECK_FALSE(controller.commandService().canUndo());
}
