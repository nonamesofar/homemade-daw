// M3 3.6: the pads in the sample editor: grid, banks, keys, selection, per-slice settings and the render pipeline.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "EditorFixture.h"
#include "commands/SliceCommands.h"

using namespace sampler::test;

namespace
{
constexpr double padRate = 44100.0;

struct Pads : Session
{
    explicit Pads(const juce::String& name, double rate = padRate) : Session(name, true, rate)
    {
        main.browser().onRowOpened(id);
        main.editorFocusProbe = [] { return true; };
        pump();
    }

    sampler::ui::SampleEditorView& ed() { return editor(); }

    void chooseBeat(int division)
    {
        ed().modeControl.setSelected(0, true);
        pump(20);
        if (division != 0)
        {
            ed().divisionControl.setSelected(division, true);
            pump(20);
        }
        waitForRenders();
    }

    void waitForRenders()
    {
        pumpUntil([this] { return controller.pads().isIdle(); });
        pump(40); // the results are handed to the message thread
        pumpUntil([this] { return controller.pads().isIdle(); });
    }

    bool press(char c, juce::ModifierKeys mods = {})
    {
        return main.keyPressed(juce::KeyPress(static_cast<int>(c), mods, c));
    }

    juce::Point<int> centreOfPad(int pad) { return ed().padGrid().padArea(pad).getCentre(); }

    void mouseDownOnPad(int pad)
    {
        auto& grid = ed().padGrid();
        const auto at = centreOfPad(pad).toFloat();
        grid.mouseDown(event(grid, at, at, false));
    }

    void mouseUpOnPad(int pad)
    {
        auto& grid = ed().padGrid();
        const auto at = centreOfPad(pad).toFloat();
        grid.mouseUp(event(grid, at, at, false));
    }
};
} // namespace

TEST_CASE("with fewer slices than pads the others are empty and play nothing", "[app][editor][pads]")
{
    Pads p("pads-empty");
    auto& grid = p.ed().padGrid();
    CHECK(grid.numSlices() == 1);
    CHECK(grid.sliceOfPad(0) == 0);
    for (int pad = 1; pad < 16; ++pad)
        CHECK(grid.sliceOfPad(pad) == -1);
    CHECK(grid.bankCount() == 1);

    // Five slice points make six slices: pads 1 to 6 are active.
    for (const juce::int64 at : {50000, 100000, 150000, 200000, 250000})
        p.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(p.id, at));
    pump(20);
    CHECK(grid.numSlices() == 6);
    for (int pad = 0; pad < 6; ++pad)
        CHECK(grid.sliceOfPad(pad) == pad);
    for (int pad = 6; pad < 16; ++pad)
        CHECK(grid.sliceOfPad(pad) == -1);

    CHECK(p.ed().padKeyDown(0));
    CHECK_FALSE(p.ed().padKeyDown(9)); // an empty pad plays nothing
    CHECK(p.ed().selectedSlice() == 0);
    CHECK(p.controller.pads().lastPlayedSlot() == 0);

    // Clicking an empty pad does nothing either.
    p.mouseDownOnPad(12);
    CHECK(p.ed().selectedSlice() == 0);
    p.mouseUpOnPad(12);
}

TEST_CASE("pads are laid out like an MPC and bank buttons appear above 16 slices", "[app][editor][pads]")
{
    Pads p("pads-layout");
    p.chooseBeat(3); // 1/32: 128 slices
    auto& grid = p.ed().padGrid();
    CHECK(grid.numSlices() > 100);
    CHECK(grid.bankCount() == (grid.numSlices() + 15) / 16);
    CHECK(grid.bankCount() >= 8); // banks A to H and more

    // Pad 1 bottom left, pad 4 bottom right, pad 13 top left.
    CHECK(grid.padArea(0).getY() > grid.padArea(12).getY());
    CHECK(grid.padArea(0).getX() == grid.padArea(12).getX());
    CHECK(grid.padArea(3).getX() > grid.padArea(0).getX());
    CHECK(grid.padArea(3).getY() == grid.padArea(0).getY());
    CHECK(grid.padAt(grid.padArea(5).getCentre()) == 5);
}

TEST_CASE("Z plays pad 1 of bank C as slice 33, and the key selects it", "[app][editor][pads]")
{
    Pads p("pads-bank-c");
    p.chooseBeat(3);
    auto& grid = p.ed().padGrid();
    REQUIRE(grid.bankCount() >= 3);

    // Choosing bank C is a click on its button: not an undo step.
    const auto depth = p.controller.commandService().undoName();
    const auto button = grid.bankButtonArea(2).getCentre().toFloat();
    grid.mouseDown(p.event(grid, button, button, false));
    grid.mouseUp(p.event(grid, button, button, false));
    CHECK(grid.bank() == 2);
    CHECK(p.controller.commandService().undoName() == depth);

    CHECK(p.press('z'));
    CHECK(p.controller.pads().lastPlayedSlot() == 32);
    CHECK(p.ed().selectedSlice() == 32);
    CHECK(p.main.router().anyPadHeld());
}

TEST_CASE("clicking pad 3 plays and selects slice 3 and playing leaves the project clean", "[app][editor][pads]")
{
    Pads p("pads-click");
    p.chooseBeat(0);
    const auto token = p.controller.commandService().historyToken();
    const bool dirty = p.controller.isDirty();
    const auto name = p.controller.commandService().undoName();

    p.mouseDownOnPad(2);
    CHECK(p.ed().selectedSlice() == 2);
    CHECK(p.controller.pads().lastPlayedSlot() == 2);
    p.mouseUpOnPad(2);
    for (const char c : {'z', 'x', 'c', 'a', 'q', '1'})
    {
        p.press(c);
        p.main.keyStateChanged(false);
    }
    p.ed().padGrid().setBank(0);
    p.main.tick(16.0);

    CHECK(p.controller.commandService().historyToken() == token);
    CHECK(p.controller.isDirty() == dirty);
    CHECK(p.controller.commandService().undoName() == name);
}

TEST_CASE("pad keys are ignored when the editor does not have focus, and in the arrangement", "[app][editor][pads]")
{
    Pads p("pads-focus");
    p.chooseBeat(0);
    p.main.editorFocusProbe = [] { return false; };
    p.press('z');
    CHECK(p.controller.pads().lastPlayedSlot() == -1);
    CHECK_FALSE(p.main.router().anyPadHeld());

    p.main.editorFocusProbe = [] { return true; };
    p.main.closeEditor();
    p.main.editorFocusProbe = nullptr; // the real probe: no editor is open, so it has no focus
    p.press('z');
    CHECK(p.controller.pads().lastPlayedSlot() == -1);
}

TEST_CASE("every pad is let go when focus leaves, the editor closes or the app goes to the background", "[app][editor][pads]")
{
    Pads p("pads-release");
    p.chooseBeat(0);
    std::set<int> down;
    p.main.router().isKeyDown = [&down](int code) { return down.count(code) != 0; };

    down.insert('Z');
    p.press('z');
    down.insert('X');
    p.press('x');
    CHECK(p.main.router().anyPadHeld());

    // The key-up of Z arrives normally.
    down.erase('Z');
    p.main.keyStateChanged(false);
    CHECK(p.main.router().anyPadHeld()); // X is still held

    // Focus leaves the editor: the missed key-up of X does not matter.
    p.main.releaseAllPads();
    CHECK_FALSE(p.main.router().anyPadHeld());

    // The app goes to the background while a key is held.
    down.insert('C');
    p.press('c');
    bool active = true;
    p.main.appActiveProbe = [&active] { return active; };
    p.main.tick(16.0);
    CHECK(p.main.router().anyPadHeld());
    active = false;
    p.main.tick(16.0);
    CHECK_FALSE(p.main.router().anyPadHeld());

    // The editor is closed while a key is held.
    active = true;
    p.main.tick(16.0);
    down.insert('V');
    p.press('v');
    CHECK(p.main.router().anyPadHeld());
    p.main.closeEditor();
    CHECK_FALSE(p.main.router().anyPadHeld());
}

TEST_CASE("slices are rendered for the pads and a change is rendered again while the old sound stays", "[app][editor][pads]")
{
    Pads p("pads-render");
    p.chooseBeat(0);
    auto& pads = p.controller.pads();
    const int count = p.ed().padGrid().numSlices();
    CHECK(pads.renderedCount() == count);
    CHECK(pads.renderRate() == padRate);
    for (int slot = 0; slot < count; ++slot)
        CHECK(pads.hasBuffer(slot));

    // Pitch of slice 3 up an octave: the project changes at once, the render follows, the old buffer stays until then.
    p.ed().selectSlice(2);
    p.ed().slicePanel().pitchField.setValue(12, true);
    CHECK(p.controller.commandService().undoName() == "Set slice pitch");
    CHECK(pads.hasBuffer(2)); // still the old one
    p.waitForRenders();
    CHECK(pads.hasBuffer(2));
    CHECK(pads.renderedCount() == count);

    // Gain, reverse and the play mode: one named step each.
    p.ed().slicePanel().gainField.setValue(-6, true);
    CHECK(p.controller.commandService().undoName() == "Set slice gain");
    pump(600); // out of the 500 ms window of the gain change
    p.ed().slicePanel().reverseButton.setToggleState(true, juce::dontSendNotification);
    p.ed().slicePanel().reverseButton.onClick();
    CHECK(p.controller.commandService().undoName() == "Reverse slice");
    p.ed().slicePanel().playModeControl.setSelected(2, true);
    CHECK(p.controller.commandService().undoName() == "Set play mode");

    const auto settings = sampler::model::SamplerState(p.controller.project().samplerState()).sourceAt(0).sliceSet().slices[2];
    CHECK(settings.pitch == 12.0);
    CHECK(settings.gainDb == Catch::Approx(-6.0));
    CHECK(settings.reverse);
    CHECK(settings.playMode == sampler::model::PlayMode::loop);
    p.waitForRenders();

    // The panel shows the selected slice's settings, and selecting another slice shows its own.
    p.ed().selectSlice(0);
    CHECK(p.ed().slicePanel().pitchField.getValue() == 0.0);
    CHECK_FALSE(p.ed().slicePanel().reverseButton.getToggleState());
    p.ed().selectSlice(2);
    CHECK(p.ed().slicePanel().pitchField.getValue() == 12.0);
    CHECK(p.ed().slicePanel().playModeControl.selected() == 2);
}

TEST_CASE("a drag on the pitch field is one undo step", "[app][editor][pads]")
{
    Pads p("pads-pitch-drag");
    p.chooseBeat(0);
    p.ed().selectSlice(1);
    auto& field = p.ed().slicePanel().pitchField;
    field.onDragStart();
    for (const double v : {1.0, 3.0, 5.0, 7.0})
        field.setValue(v, true);
    field.onDragEnd();
    CHECK(p.controller.commandService().undoName() == "Set slice pitch");
    p.main.invoke(sampler::app::undoCommand);
    pump(20);
    CHECK(p.controller.commandService().undoName() == "Slice by beat");
}

TEST_CASE("the pads are cleared when the sound is closed", "[app][editor][pads]")
{
    Pads p("pads-close");
    p.chooseBeat(0);
    CHECK(p.controller.pads().renderedCount() > 1);
    p.main.closeEditor();
    CHECK(p.controller.pads().renderedCount() == 0);
    CHECK(p.controller.pads().openId().isEmpty());
}

TEST_CASE("pads play a Gate slice while the key is held, on the real device", "[app][editor][pads][hardware]")
{
    Pads p("pads-gate", 0.0); // the device's own rate
    pumpUntil([&] { return p.controller.engine().isAudioReady(); }, 15000);
    if (! p.controller.engine().isAudioReady())
        SKIP("no audio output on this machine");

    p.ed().modeControl.setSelected(0, true);
    pump(20);
    p.ed().selectSlice(1);
    p.ed().slicePanel().playModeControl.setSelected(1, true); // Gate
    p.waitForRenders();
    REQUIRE(p.controller.pads().hasBuffer(1));

    std::set<int> down{'X'};
    p.main.router().isKeyDown = [&down](int code) { return down.count(code) != 0; };
    p.press('x'); // pad 2 = slice 2 (index 1)
    pump(300);
    CHECK(p.controller.pads().isSounding(1));
    down.clear();
    p.main.keyStateChanged(false); // key up
    pump(300);
    CHECK_FALSE(p.controller.pads().isSounding(1)); // let go: it faded out within 10 ms
}

TEST_CASE("changing the slices keeps the selected slice and the other pads' renders", "[app][editor][pads]")
{
    Pads p("pads-keep-selection");
    p.chooseBeat(1); // 1/8: 32 slices
    p.waitForRenders();
    auto& pads = p.controller.pads();
    REQUIRE(p.ed().sliceCount() >= 30);
    p.ed().selectSlice(7);
    p.waitForRenders();
    const auto rendered = pads.renderedCount();
    REQUIRE(rendered >= 30);

    const auto markers = p.ed().waveform().markers();
    const auto inLater = (markers[19] + markers[20]) / 2; // inside slice 20: slices 0 to 19 are untouched
    p.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(p.id, inLater));
    p.ed().flushPendingRefresh();
    CHECK(p.ed().selectedSlice() == 7);
    for (int slot = 0; slot < 20; ++slot)
        CHECK(pads.hasBuffer(slot));

    p.waitForRenders();
    CHECK(p.ed().selectedSlice() == 7);
    CHECK(pads.renderedCount() >= rendered);

    // The same through undo and redo.
    p.main.invoke(sampler::app::undoCommand);
    p.ed().flushPendingRefresh();
    CHECK(p.ed().selectedSlice() == 7);
    for (int slot = 0; slot < 20; ++slot)
        CHECK(pads.hasBuffer(slot));
    p.waitForRenders();
}

TEST_CASE("a pad is let go as the slice it started, whatever the bank shows by then", "[app][editor][pads]")
{
    Pads p("pads-release-slice");
    p.chooseBeat(1); // 1/8: 32 slices, banks A and B
    sampler::ui::UiState state;
    sampler::ui::EditorContext ctx{p.controller.project().samplerState(), p.controller.commandService(), p.controller.analyser(),
                                   [&p] { return p.controller.project().bundleDir(); }};
    std::vector<int> downs, ups;
    ctx.padDown = [&downs](int slice) { downs.push_back(slice); };
    ctx.padUp = [&ups](int slice) { ups.push_back(slice); };
    sampler::ui::SampleEditorView view(ctx, state);
    view.setSize(1440, 900);
    view.open(p.id);
    REQUIRE(view.padGrid().bankCount() >= 2);

    // A key held across a bank change.
    REQUIRE(view.padKeyDown(2));
    view.padGrid().setBank(1);
    view.padKeyUp(2);
    // And a mouse press held across one.
    auto& grid = view.padGrid();
    grid.setBank(0);
    const auto at = grid.padArea(4).getCentre().toFloat();
    grid.mouseDown(p.event(grid, at, at, false));
    grid.setBank(1);
    grid.mouseUp(p.event(grid, at, at, false));

    CHECK(downs == std::vector<int>{2, 4});
    CHECK(ups == std::vector<int>{2, 4});
    view.close();
}

TEST_CASE("every bank button lies inside the pad grid, even with 256 slices", "[app][editor][pads]")
{
    sampler::ui::editor::PadGrid grid;
    grid.setSize(420, 240);
    for (const int slices : {17, 100, 208, 256})
    {
        grid.setSliceCount(slices);
        const auto bounds = grid.getLocalBounds();
        for (int bank = 0; bank < grid.bankCount(); ++bank)
        {
            const auto r = grid.bankButtonArea(bank);
            INFO(slices << " slices, bank " << bank);
            CHECK(bounds.contains(r));
            CHECK(r.getWidth() >= 8);
            if (bank > 0)
                CHECK(r.getX() > grid.bankButtonArea(bank - 1).getRight() - 1);
        }
    }
}
