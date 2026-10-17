// Which warp fields of the clip inspector are usable, and their text (clip-warp and clip-inspector specs).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/arrange/InspectorLogic.h"

using Catch::Approx;
using namespace sampler::ui::InspectorLogic;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;

namespace
{
WarpSettings warped(WarpMode mode, double bpm = 90.0, double transpose = 0.0)
{
    WarpSettings w;
    w.on = true;
    w.mode = mode;
    w.sourceBpm = bpm;
    w.transpose = transpose;
    return w;
}
} // namespace

TEST_CASE("a Beats clip with WARP on can use every warp field", "[ui][inspector]")
{
    const auto s = stateFor(warped(WarpMode::beats), 90.0);
    CHECK(s.warpOn);
    CHECK(s.warpEnabled);
    CHECK(s.warpHint.isEmpty());
    CHECK(s.modeEnabled);
    CHECK(s.bpmEnabled);
    CHECK(s.halveEnabled);
    CHECK(s.doubleEnabled);
    CHECK(s.transposeEnabled);
}

TEST_CASE("WARP off greys MODE, BPM and TRANSPOSE", "[ui][inspector]")
{
    WarpSettings off = warped(WarpMode::tones, 90.0, 7.0);
    off.on = false;
    const auto s = stateFor(off, 90.0);
    CHECK_FALSE(s.warpOn);
    CHECK(s.warpEnabled); // the tempo is known, so it can be turned on again
    CHECK_FALSE(s.modeEnabled);
    CHECK_FALSE(s.bpmEnabled);
    CHECK_FALSE(s.halveEnabled);
    CHECK_FALSE(s.doubleEnabled);
    CHECK_FALSE(s.transposeEnabled);
}

TEST_CASE("TRANSPOSE is available in Beats and Tones and greyed in Re-pitch", "[ui][inspector]")
{
    CHECK(stateFor(warped(WarpMode::beats), 90.0).transposeEnabled);
    CHECK(stateFor(warped(WarpMode::tones), 90.0).transposeEnabled);
    const auto repitch = stateFor(warped(WarpMode::repitch, 90.0, 7.0), 90.0);
    CHECK_FALSE(repitch.transposeEnabled);
    CHECK(repitch.modeEnabled);
    CHECK(repitch.bpmEnabled);
}

TEST_CASE("a sound without a tempo cannot be warped and says why", "[ui][inspector]")
{
    const auto s = stateFor(WarpSettings{}, 0.0);
    CHECK_FALSE(s.warpEnabled);
    CHECK(s.warpHint == "Tempo not known");
    CHECK(s.tempoForWarpOn == 0.0);
}

TEST_CASE("a tempo typed on the clip is enough to warp it", "[ui][inspector]")
{
    WarpSettings off;
    off.sourceBpm = 100.0; // kept from an earlier WARP
    const auto s = stateFor(off, 0.0);
    CHECK(s.warpEnabled);
    CHECK(s.warpHint.isEmpty());
    CHECK(s.tempoForWarpOn == Approx(100.0));
}

TEST_CASE("turning WARP on takes the sound's tempo when the clip has none", "[ui][inspector]")
{
    const auto s = stateFor(WarpSettings{}, 92.0);
    CHECK(s.warpEnabled);
    CHECK(s.tempoForWarpOn == Approx(92.0));
    CHECK(stateFor(WarpSettings{}, 5000.0).warpHint == "Tempo not known"); // out of range is not a tempo
}

TEST_CASE("a warped clip can always be switched off, even when the tempo is not known", "[ui][inspector]")
{
    const auto s = stateFor(warped(WarpMode::beats, 90.0), 0.0);
    CHECK(s.warpEnabled);
    CHECK(s.warpHint.isEmpty());
}

TEST_CASE("halve and double stop at 20 and 999 BPM", "[ui][inspector]")
{
    CHECK_FALSE(stateFor(warped(WarpMode::beats, 30.0), 30.0).halveEnabled); // 15 < 20
    CHECK(stateFor(warped(WarpMode::beats, 40.0), 40.0).halveEnabled);       // 20 is the lowest
    CHECK(stateFor(warped(WarpMode::beats, 499.5), 0.0).doubleEnabled);      // 999 is the highest
    CHECK_FALSE(stateFor(warped(WarpMode::beats, 500.0), 0.0).doubleEnabled);

    CHECK(halved(90.0).value() == Approx(45.0));
    CHECK(doubled(90.0).value() == Approx(180.0));
    CHECK_FALSE(halved(30.0).has_value());
    CHECK_FALSE(doubled(600.0).has_value());
}

TEST_CASE("Texture is shown in MODE and cannot be chosen", "[ui][inspector]")
{
    CHECK(modeNames() == juce::StringArray{"Beats", "Tones", "Texture", "Re-pitch"});
    CHECK(isModeEnabled(modeBeats));
    CHECK(isModeEnabled(modeTones));
    CHECK_FALSE(isModeEnabled(modeTexture));
    CHECK(isModeEnabled(modeRepitch));
    CHECK_FALSE(isModeEnabled(-1));
    CHECK_FALSE(isModeEnabled(4));
}

TEST_CASE("mode and segment index map both ways", "[ui][inspector]")
{
    for (const auto mode : {WarpMode::beats, WarpMode::tones, WarpMode::repitch})
        CHECK(modeAt(indexOf(mode)).value() == mode);
    CHECK(indexOf(WarpMode::repitch) == 3);
}

TEST_CASE("BPM text has two decimals and typed text is clamped", "[ui][inspector]")
{
    CHECK(bpmText(90.0) == "90.00");
    CHECK(bpmText(92.456) == "92.46");
    CHECK(parseBpm("92.5").value() == Approx(92.5));
    CHECK(parseBpm(" 100 BPM ").value() == Approx(100.0));
    CHECK(parseBpm("92,5").value() == Approx(92.5));
    CHECK(parseBpm("5").value() == Approx(20.0));
    CHECK(parseBpm("5000").value() == Approx(999.0));
    CHECK_FALSE(parseBpm("").has_value());
    CHECK_FALSE(parseBpm("fast").has_value());
    CHECK_FALSE(parseBpm("0").has_value());
}

TEST_CASE("transpose text shows the sign and typed text is clamped to 24 semitones", "[ui][inspector]")
{
    CHECK(transposeText(0.0) == "0 st");
    CHECK(transposeText(7.0) == "+7 st");
    CHECK(transposeText(-12.0) == "-12 st");
    CHECK(transposeText(2.5) == "+2.5 st");
    CHECK(parseTranspose("+7").value() == Approx(7.0));
    CHECK(parseTranspose("-3 st").value() == Approx(-3.0));
    CHECK(parseTranspose("40").value() == Approx(24.0));
    CHECK(parseTranspose("-99").value() == Approx(-24.0));
    CHECK_FALSE(parseTranspose("").has_value());
    CHECK_FALSE(parseTranspose("-").has_value());
    CHECK_FALSE(parseTranspose("up").has_value());
}

TEST_CASE("length text counts bars and beats", "[ui][inspector][length]")
{
    CHECK(lengthText(16.0, 4) == "4 bars");
    CHECK(lengthText(4.0, 4) == "1 bar");
    CHECK(lengthText(6.0, 4) == "1 bar 2 beats");
    CHECK(lengthText(5.0, 4) == "1 bar 1 beat");
    CHECK(lengthText(3.0, 4) == "3 beats");
    CHECK(lengthText(1.0, 4) == "1 beat");
    CHECK(lengthText(0.5, 4) == "0.50 beats");
    CHECK(lengthText(21.333333, 4) == "5 bars 1.33 beats");
    CHECK(lengthText(3.999, 4) == "1 bar"); // rounds up to the whole bar
    CHECK(lengthText(9.0, 3) == "3 bars");
}

TEST_CASE("a typed length is bars, bars.beats or beats", "[ui][inspector][length]")
{
    CHECK(parseLength("8", 4).value() == Approx(32.0));
    CHECK(parseLength("2 bars", 4).value() == Approx(8.0));
    CHECK(parseLength("1 bar", 4).value() == Approx(4.0));
    CHECK(parseLength("1.2", 4).value() == Approx(6.0));
    CHECK(parseLength("1 bar 2 beats", 4).value() == Approx(6.0));
    CHECK(parseLength("3 beats", 4).value() == Approx(3.0));
    CHECK(parseLength("3b", 4).value() == Approx(3.0));
    CHECK(parseLength("2", 3).value() == Approx(6.0)); // a bar of three beats
    CHECK(parseLength("1,2", 4).value() == Approx(6.0));
    CHECK_FALSE(parseLength("", 4).has_value());
    CHECK_FALSE(parseLength("0", 4).has_value());
    CHECK_FALSE(parseLength("long", 4).has_value());
    CHECK_FALSE(parseLength("2 beats 1 bar", 4).has_value());
    CHECK_FALSE(parseLength("1 2 3", 4).has_value());
    // What the field shows can be typed back.
    CHECK(parseLength(lengthText(6.0, 4), 4).value() == Approx(6.0));
    CHECK(parseLength(lengthText(16.0, 4), 4).value() == Approx(16.0));
}

TEST_CASE("fade text is milliseconds and a typed fade is cut to fit the clip", "[ui][inspector][fades]")
{
    CHECK(msText(500.0) == "500 ms");
    CHECK(msText(0.4) == "0 ms");
    CHECK(parseMs("500").value() == Approx(500.0));
    CHECK(parseMs("500 ms").value() == Approx(500.0));
    CHECK(parseMs("0.5 s").value() == Approx(500.0));
    CHECK_FALSE(parseMs("slow").has_value());
    CHECK_FALSE(parseMs("").has_value());

    // A 1000 ms clip with a fade out of 600 ms: typing 800 ms for the fade in gives 400 ms.
    const auto in = withFadeIn(800.0, 600.0, 1000.0);
    CHECK(in.inMs == Approx(400.0));
    CHECK(in.outMs == Approx(600.0));
    const auto out = withFadeOut(900.0, 300.0, 1000.0);
    CHECK(out.inMs == Approx(300.0));
    CHECK(out.outMs == Approx(700.0));
    CHECK(withFadeIn(-5.0, 0.0, 1000.0).inMs == 0.0);
    CHECK(withFadeIn(5000.0, 0.0, 1000.0).inMs == Approx(1000.0));
}
