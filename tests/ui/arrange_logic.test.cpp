// Plain arrangement UI logic: position text, transport logic, snap, selection, clipboard, meter ballistics, keyboard routing.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/KeyboardRouter.h"
#include "ui/arrange/ArrangeLogic.h"

#include <map>

using Catch::Approx;
using namespace sampler::ui;
using sampler::engine::TimeSig;

TEST_CASE("position text formats bar . beat . sixteenth from 1", "[ui][position]")
{
    const TimeSig fourFour{4, 4};
    CHECK(PositionText::format(0.0, fourFour) == "1 . 1 . 1");
    CHECK(PositionText::format(18.0, fourFour) == "5 . 3 . 1"); // 9 s at 120 BPM
    CHECK(PositionText::format(17.25, fourFour) == "5 . 2 . 2");
    CHECK(PositionText::format(2.9999999, fourFour) == "1 . 4 . 1");
    CHECK(PositionText::format(6.0, {3, 4}) == "3 . 1 . 1");
    CHECK(PositionText::format(7.5, {7, 8}) == "2 . 1 . 2"); // eighth-note beats, two sixteenths each
}

TEST_CASE("position text parses typed positions and refuses bad text", "[ui][position]")
{
    const TimeSig fourFour{4, 4};
    CHECK(PositionText::parse("5.2", fourFour) == Approx(17.0));
    CHECK(PositionText::parse("5 . 2 . 1", fourFour) == Approx(17.0));
    CHECK(PositionText::parse("9", fourFour) == Approx(32.0));
    CHECK(PositionText::parse("1.1.3", fourFour) == Approx(0.5));
    CHECK(PositionText::parse("2.3", {3, 4}) == Approx(5.0));
    for (const auto* bad : {"", "abc", "0.1", "5.5", "1.1.5", "1..2", "1.2.3.4", "-2", "5,2"})
        CHECK_FALSE(PositionText::parse(bad, fourFour).has_value());
}

TEST_CASE("transport logic: second stop, Home and Shift+Space", "[ui][transport]")
{
    CHECK(TransportLogic::returnPosition(false, {16.0, 32.0}) == Approx(0.0));
    CHECK(TransportLogic::returnPosition(true, {16.0, 32.0}) == Approx(16.0));
    CHECK(TransportLogic::returnPosition(true, {}) == Approx(0.0));
    CHECK(TransportLogic::playFromSelection(24.0, 0.0) == Approx(24.0));
    CHECK(TransportLogic::playFromSelection(std::nullopt, 9.0) == Approx(9.0));
}

TEST_CASE("snap grid in 4/4 and 3/4, triplets and Alt", "[ui][snap]")
{
    const TimeSig four{4, 4}, three{3, 4};
    CHECK(Snap::gridBeats(SnapValue::off, four) == 0.0);
    CHECK(Snap::gridBeats(SnapValue::bar, four) == Approx(4.0));
    CHECK(Snap::gridBeats(SnapValue::bar, three) == Approx(3.0));
    CHECK(Snap::gridBeats(SnapValue::half, four) == Approx(2.0));
    CHECK(Snap::gridBeats(SnapValue::quarter, four) == Approx(1.0));
    CHECK(Snap::gridBeats(SnapValue::sixteenth, three) == Approx(0.25));
    CHECK(Snap::gridBeats(SnapValue::thirtySecond, four) == Approx(0.125));
    CHECK(Snap::gridBeats(SnapValue::eighthTriplet, four) == Approx(1.0 / 3.0));
    CHECK(Snap::gridBeats(SnapValue::quarterTriplet, four) == Approx(2.0 / 3.0));
    CHECK(Snap::gridBeats(SnapValue::sixteenthTriplet, four) == Approx(1.0 / 6.0));

    CHECK(Snap::snap(17.3, SnapValue::bar, four) == Approx(16.0));
    CHECK(Snap::snap(18.3, SnapValue::bar, four) == Approx(20.0));
    CHECK(Snap::snap(17.3, SnapValue::bar, four, true) == Approx(17.3)); // Alt
    CHECK(Snap::snap(4.4, SnapValue::bar, three) == Approx(3.0));
    CHECK(Snap::snap(1.4, SnapValue::eighthTriplet, four) == Approx(4.0 / 3.0));
    CHECK(Snap::snapDown(7.9, SnapValue::bar, four) == Approx(4.0));
    CHECK(Snap::minimumLength(SnapValue::off, four, 1.0) == Approx(1.0));
    CHECK(Snap::minimumLength(SnapValue::quarter, four, 1.0) == Approx(1.0));

    CHECK(Snap::all().size() == 10);
    for (auto v : Snap::all())
        CHECK(Snap::fromName(Snap::name(v)) == v);
    CHECK(Snap::fromName("nonsense") == SnapValue::bar);
}

TEST_CASE("snap grid lines count from the bar start so bar lines are always targets", "[ui][snap]")
{
    for (const TimeSig sig : {TimeSig{3, 4}, TimeSig{7, 8}, TimeSig{4, 4}, TimeSig{5, 4}})
    {
        const auto bar = static_cast<double>(sig.numerator);
        for (auto v : Snap::all())
        {
            if (v == SnapValue::off)
                continue;
            for (int n = 0; n < 4; ++n)
            {
                const auto barLine = bar * n;
                CHECK(Snap::snap(barLine, v, sig) == Approx(barLine));
                CHECK(Snap::snapDown(barLine, v, sig) == Approx(barLine));
                CHECK(Snap::snap(barLine + 0.01, v, sig) == Approx(barLine).margin(1.0e-9));
                CHECK(Snap::snap(barLine - 0.01, v, sig) == Approx(barLine).margin(1.0e-9));
                // The grid never exceeds a bar, so a beat just inside a bar floors to a line of that bar (its start at most).
                CHECK(Snap::gridBeats(v, sig) <= bar + 1.0e-9);
                CHECK(Snap::snapDown(barLine + 0.001, v, sig) == Approx(barLine).margin(1.0e-9));
                if (n > 0 && Snap::gridBeats(v, sig) >= bar - 1.0e-9) // whole-bar steps: just below a line floors to the previous
                    CHECK(Snap::snapDown(barLine - 0.001, v, sig) == Approx(barLine - bar).margin(1.0e-9));
            }
        }
    }
    const TimeSig seven{7, 8};
    CHECK(Snap::snap(7.0, SnapValue::quarter, seven) == Approx(7.0)); // not 8
    CHECK(Snap::snap(8.1, SnapValue::quarter, seven) == Approx(9.0));
    CHECK(Snap::snapDown(8.9, SnapValue::quarter, seven) == Approx(7.0));
    const TimeSig three{3, 4};
    CHECK(Snap::snap(2.9, SnapValue::half, three) == Approx(3.0)); // 1/2 = 2 beats; bar 2 starts at 3
    CHECK(Snap::snap(4.1, SnapValue::half, three) == Approx(5.0));
    CHECK(Snap::snap(3.1, SnapValue::quarterTriplet, three) == Approx(3.0));
    CHECK(Snap::snapDown(5.9, SnapValue::half, three) == Approx(5.0));
}

TEST_CASE("the snap grid never exceeds one bar", "[ui][snap]")
{
    for (const TimeSig sig : {TimeSig{3, 8}, TimeSig{6, 16}, TimeSig{3, 16}, TimeSig{2, 8}, TimeSig{1, 4}})
    {
        const auto bar = static_cast<double>(sig.numerator);
        for (auto v : Snap::all())
            if (v != SnapValue::off)
                CHECK(Snap::gridBeats(v, sig) <= bar + 1.0e-9);
        CHECK(Snap::minimumLength(SnapValue::half, sig, 1.0) <= bar + 1.0e-9);
        CHECK(Snap::snap(bar * 2.0 + 0.1, SnapValue::half, sig) == Approx(bar * 2.0));
    }
    CHECK(Snap::gridBeats(SnapValue::half, TimeSig{3, 8}) == Approx(3.0));
    CHECK(Snap::gridBeats(SnapValue::half, TimeSig{4, 4}) == Approx(2.0)); // 4/4 unchanged
}

TEST_CASE("selection: a selected track that is gone moves on and loses the header state", "[ui][selection]")
{
    SelectionModel s;
    s.selectHeader("t2");
    s.retainTrack({"t1", "t2"});
    CHECK(s.isHeaderSelected());
    s.retainTrack({"t1"});
    CHECK(s.track() == "t1");
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("t1");
    s.retainTrack({});
    CHECK(s.track().isEmpty());
    CHECK_FALSE(s.isHeaderSelected());
}

TEST_CASE("selection: a track is selected as a header only by selectHeader", "[ui][selection]")
{
    SelectionModel s;
    s.setTrack("t1");
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("t2");
    CHECK(s.isHeaderSelected());
    CHECK(s.track() == "t2");
    s.setClips({"a"});
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("t2");
    s.setTrack("t3"); // a lane click
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("t2");
    s.clearClips();
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("t2");
    s.marquee({}, {}, false);
    CHECK_FALSE(s.isHeaderSelected());
    s.selectHeader("");
    CHECK_FALSE(s.isHeaderSelected());
}

TEST_CASE("selection: click, Shift, Mod toggle, marquee and undo mapping", "[ui][selection]")
{
    SelectionModel s;
    int changes = 0;
    s.onChange = [&changes] { ++changes; };
    s.click("a", false, false);
    CHECK(s.clips() == std::vector<juce::String>{"a"});
    s.click("b", true, false);
    s.click("c", true, false);
    CHECK(s.clips().size() == 3);
    s.click("b", false, true); // Mod toggles one off
    CHECK(s.clips() == std::vector<juce::String>{"a", "c"});
    s.click("b", false, true);
    CHECK(s.isSelected("b"));
    s.click("c", false, false); // plain click selects only it
    CHECK(s.clips() == std::vector<juce::String>{"c"});

    s.marquee({"x", "y"}, s.clips(), false);
    CHECK(s.clips() == std::vector<juce::String>{"x", "y"});
    s.marquee({"z"}, {"x", "y"}, true); // Shift keeps the earlier ones
    CHECK(s.clips() == std::vector<juce::String>{"x", "y", "z"});

    // Undo of a delete of x and y: they come back and are selected; z is gone.
    s.setClips({"z"});
    s.afterHistoryChange({"z", "q"}, {"q", "x", "y"});
    CHECK(s.clips() == std::vector<juce::String>{"x", "y"});
    s.retainOnly({"y"});
    CHECK(s.clips() == std::vector<juce::String>{"y"});
    CHECK(changes > 0);
}

TEST_CASE("clipboard records are relative to the earliest clip and the topmost track", "[ui][clipboard]")
{
    sampler::engine::ClipInfo a, b;
    a.id = "a";
    a.trackId = "t2";
    a.start = 8.0;
    a.lengthSeconds = 2.0;
    a.sourceId = "src_a";
    b.id = "b";
    b.trackId = "t3";
    b.start = 12.0;
    b.lengthSeconds = 1.0;
    b.gainDb = -3.0;
    b.reversed = true;
    const std::map<juce::String, int> index{{"t1", 0}, {"t2", 1}, {"t3", 2}};
    const auto records = Clipboard::recordsFor({a, b}, [&index](const juce::String& t) { return index.at(t); });
    REQUIRE(records.size() == 2);
    CHECK(records[0].trackOffset == 0);
    CHECK(records[0].startOffset == Approx(0.0));
    CHECK(records[0].sourceId == "src_a");
    CHECK(records[1].trackOffset == 1);
    CHECK(records[1].startOffset == Approx(4.0));
    CHECK(records[1].gainDb == Approx(-3.0));
    CHECK(records[1].reversed);
    CHECK(Clipboard::recordsFor({}, [](const juce::String&) { return 0; }).empty());
}

TEST_CASE("meter ballistics: 20 dB/s fall, 1.5 s hold, 300 ms RMS, latched over", "[ui][meter]")
{
    MeterBallistics m;
    m.push(-6.0, 16.0);
    CHECK(m.levelDb() == Approx(-6.0));
    CHECK(m.heldDb() == Approx(-6.0));
    for (int i = 0; i < 30; ++i) // 0.5 s of silence
        m.push(-100.0, 1000.0 / 60.0);
    CHECK(m.levelDb() == Approx(-16.0).margin(0.1)); // fell 10 dB
    CHECK(m.heldDb() == Approx(-6.0));                // still held
    for (int i = 0; i < 70; ++i) // past 1.5 s: the hold lets go
        m.push(-100.0, 1000.0 / 60.0);
    CHECK(m.heldDb() < -6.0);
    for (int i = 0; i < 120; ++i) // 54 dB at 20 dB/s takes 2.7 s in all
        m.push(-100.0, 1000.0 / 60.0);
    CHECK(m.levelDb() == Approx(MeterBallistics::floorDb));

    MeterBallistics rms;
    for (int i = 0; i < 30; ++i)
        rms.push(-12.0, 1000.0 / 60.0);
    CHECK(rms.rmsDb() == Approx(-12.0).margin(0.1));
    for (int i = 0; i < 30; ++i) // after 300 ms of silence the average is gone too
        rms.push(-100.0, 1000.0 / 60.0);
    CHECK(rms.rmsDb() == Approx(MeterBallistics::floorDb));

    MeterBallistics over;
    over.push(0.5, 16.0);
    for (int i = 0; i < 200; ++i)
        over.push(-100.0, 16.0);
    CHECK(over.isOver());
    over.clearOver();
    CHECK_FALSE(over.isOver());
    over.push(-3.0, 16.0, true); // the engine reported an over
    CHECK(over.isOver());

    CHECK(formatDb(-100.0) == "-inf");
    CHECK(formatDb(-4.5) == "-4.5");
    CHECK(formatDb(0.0) == "0.0");
}

TEST_CASE("keyboard routing: dialog, text field, then shortcuts", "[ui][keys]")
{
    using Route = KeyboardRouter::Route;
    const juce::KeyPress space(juce::KeyPress::spaceKey);
    const juce::KeyPress z('z');
    const juce::KeyPress del(juce::KeyPress::deleteKey);
    const juce::KeyPress save('s', juce::ModifierKeys::commandModifier, 0);
    const juce::KeyPress copy('c', juce::ModifierKeys::commandModifier, 0);

    CHECK(KeyboardRouter::route(space, {}) == Route::shortcuts);
    CHECK(KeyboardRouter::route(z, {false, true}) == Route::leaveToFocused);
    CHECK(KeyboardRouter::route(del, {false, true}) == Route::leaveToFocused);
    CHECK(KeyboardRouter::route(space, {false, true}) == Route::leaveToFocused);
    CHECK(KeyboardRouter::route(copy, {false, true}) == Route::leaveToFocused); // copies text, not clips
    CHECK(KeyboardRouter::route(save, {false, true}) == Route::shortcuts);      // File shortcuts still work
    CHECK(KeyboardRouter::route(save, {true, false}) == Route::leaveToFocused); // a dialog takes everything
    CHECK(KeyboardRouter::isTransportKey(space));
    CHECK(KeyboardRouter::isTransportKey(juce::KeyPress('l')));
    CHECK_FALSE(KeyboardRouter::isTransportKey(juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0)));
}

TEST_CASE("keyboard routing: held transport keys do not repeat", "[ui][keys]")
{
    KeyboardRouter router;
    bool down = true;
    router.isKeyDown = [&down](int) { return down; };
    const juce::KeyPress space(juce::KeyPress::spaceKey);
    CHECK(router.acceptsTransportKey(space));
    CHECK_FALSE(router.acceptsTransportKey(space)); // auto-repeat
    CHECK_FALSE(router.acceptsTransportKey(space));
    CHECK(router.acceptsTransportKey(juce::KeyPress('m'))); // other keys are not filtered
    down = false;
    router.keysChanged();
    down = true;
    CHECK(router.acceptsTransportKey(space)); // pressed again after release
}
