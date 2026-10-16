// M3 1.8: the plain maths of the sample editor (positions, viewport, ruler, slice hit-testing) and the waveform tables.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/sampleeditor/EditorMath.h"
#include "ui/sampleeditor/WaveformPeaks.h"

#include <cmath>

using namespace sampler::ui::editor;
using Catch::Approx;

namespace
{
Tempo break90() { return Tempo{90.0, 4410, 44100.0}; }
} // namespace

TEST_CASE("positions count bars of four beats from the first beat", "[editor][math]")
{
    const auto t = break90();
    CHECK(t.framesPerBeat() == Approx(29400.0));
    CHECK(barBeatText(t, 4410) == "1.1.1");
    CHECK(barBeatText(t, 4410 + 29400) == "1.2.1");
    CHECK(barBeatText(t, 4410 + 4 * 29400) == "2.1.1");
    CHECK(barBeatText(t, 4410 + 29400 * 3 + 7350 * 2 + 5) == "1.4.3");
    CHECK(barBeatText(t, 0) == "0.4.4"); // before the first beat the bars count down
    CHECK(beatsAt(t, 4410 + 29400) == Approx(1.0));
}

TEST_CASE("typed positions give back frames on the grid", "[editor][math]")
{
    const auto t = break90();
    CHECK(*parseBarBeat(t, "1.1.1") == 4410);
    CHECK(*parseBarBeat(t, "1.3.3") == 4410 + 2 * 29400 + 2 * 7350);
    CHECK(*parseBarBeat(t, "1.3") == 4410 + 2 * 29400);
    CHECK(*parseBarBeat(t, "2") == 4410 + 4 * 29400);
    CHECK(*parseBarBeat(t, " 1.2.1 ") == 4410 + 29400);
    CHECK_FALSE(parseBarBeat(t, "").has_value());
    CHECK_FALSE(parseBarBeat(t, "a.b").has_value());
    CHECK_FALSE(parseBarBeat(t, "1.5.1").has_value());
    CHECK_FALSE(parseBarBeat(t, "1.1.5").has_value());
    CHECK_FALSE(parseBarBeat(t, "1.1.1.1").has_value());
    for (const juce::int64 frame : {juce::int64{4410}, juce::int64{34000}, juce::int64{250000}})
    {
        const auto text = barBeatText(t, frame);
        const auto back = *parseBarBeat(t, text);
        CHECK(back <= frame);
        CHECK(frame - back < 7350); // within one sixteenth
    }
}

TEST_CASE("the header text for the break fixture", "[editor][math]")
{
    CHECK(headerText(break90(), 470400, 2) == juce::String::fromUTF8("4 bars \xc2\xb7 90 BPM \xc2\xb7 44.1 kHz \xc2\xb7 stereo"));
    CHECK(headerText(Tempo{120.0, 0, 48000.0}, 192000, 1) == juce::String::fromUTF8("2 bars \xc2\xb7 120 BPM \xc2\xb7 48 kHz \xc2\xb7 mono"));
    CHECK(barsText(1.0) == "1 bar");
    CHECK(barsText(3.5) == "3.5 bars");
    CHECK(barsText(3.54) == "3.5 bars");
    CHECK(barsText(0.25) == "0.3 bars");
    CHECK(bpmText(92.5) == "92.5 BPM");
    CHECK(bpmText(90.0) == "90 BPM");
    CHECK(rateText(44100.0) == "44.1 kHz");
    CHECK(rateText(48000.0) == "48 kHz");
    CHECK(rateText(96000.0) == "96 kHz");
}

TEST_CASE("doubling the tempo halves the length in bars", "[editor][math]")
{
    CHECK(barsOf(Tempo{90.0, 0, 44100.0}, 470400) == Approx(4.0));
    CHECK(barsOf(Tempo{180.0, 0, 44100.0}, 470400) == Approx(8.0));
    CHECK(barsOf(Tempo{45.0, 0, 44100.0}, 470400) == Approx(2.0));
}

TEST_CASE("typed seconds and samples", "[editor][math]")
{
    CHECK(*parseSeconds(44100.0, "1.5") == 66150);
    CHECK(*parseSeconds(44100.0, "0.25 s") == 11025);
    CHECK_FALSE(parseSeconds(44100.0, "abc").has_value());
    CHECK(*parseSamples("12345") == 12345);
    CHECK_FALSE(parseSamples("12.5").has_value());
    CHECK(secondsText(44100.0, 66150) == "1.5000 s");
}

//==============================================================================
TEST_CASE("the viewport fits, zooms around a point and scrolls inside the sound", "[editor][viewport]")
{
    Viewport v;
    v.totalFrames = 470400;
    v.setWidth(1000);
    CHECK(v.framesPerPixel == Approx(470.4));
    CHECK(v.startFrame == 0.0);

    // Zoom in around pixel 500: the frame under the mouse stays under the mouse.
    const auto anchor = v.xToFrame(500);
    v.zoomAround(500, 4.0);
    CHECK(v.framesPerPixel == Approx(117.6));
    CHECK(std::abs(v.xToFrame(500) - anchor) <= 1);

    // Zooming out beyond the whole sound goes back to the fit.
    v.zoomAround(500, 0.001);
    CHECK(v.framesPerPixel == Approx(470.4));
    CHECK(v.startFrame == 0.0);

    // Scrolling stays inside the sound.
    v.zoomAround(0, 8.0);
    v.scrollByPixels(1.0e9);
    CHECK(v.startFrame == Approx(470400.0 - v.visibleFrames()));
    v.scrollByPixels(-1.0e9);
    CHECK(v.startFrame == 0.0);
    v.fit();
    CHECK(v.framesPerPixel == Approx(470.4));
}

TEST_CASE("at the closest zoom single samples are drawn as a line and a slice point lies on one sample", "[editor][viewport]")
{
    Viewport v;
    v.totalFrames = 470400;
    v.setWidth(1000);
    CHECK_FALSE(v.showsSamples());
    for (int i = 0; i < 40; ++i)
        v.zoomAround(500, 2.0);
    CHECK(v.framesPerPixel == Approx(Viewport::minFramesPerPixel));
    CHECK(v.showsSamples());
    // A sample is 16 pixels wide, so the pixel under the mouse names exactly one sample.
    CHECK(v.xToFrame(500) == v.xToFrame(504));
    CHECK(v.frameToX(static_cast<double>(v.xToFrame(500)) + 1.0) - v.frameToX(static_cast<double>(v.xToFrame(500))) == Approx(16.0));
}

TEST_CASE("the viewport keeps its zoom when the window is resized", "[editor][viewport]")
{
    Viewport v;
    v.totalFrames = 470400;
    v.setWidth(1000);
    v.zoomAround(100, 4.0);
    const auto fpp = v.framesPerPixel;
    v.setWidth(1200);
    CHECK(v.framesPerPixel == Approx(fpp));
    Viewport fitted;
    fitted.totalFrames = 470400;
    fitted.setWidth(1000);
    fitted.setWidth(1200);
    CHECK(fitted.framesPerPixel == Approx(470400.0 / 1200.0)); // still showing the whole sound
}

TEST_CASE("the ruler labels bars from the first beat and thins out when zoomed out", "[editor][ruler]")
{
    Viewport v;
    v.totalFrames = 470400;
    v.setWidth(1000);
    const auto ticks = rulerTicks(v, break90());
    REQUIRE_FALSE(ticks.empty());
    juce::StringArray labels;
    for (const auto& t : ticks)
        if (t.label.isNotEmpty() && t.isBar)
            labels.add(t.label);
    CHECK(labels.contains("1"));
    CHECK(labels.contains("2"));
    CHECK(labels.contains("4"));
    // The first beat (frame 4410) is where bar 1 is drawn.
    for (const auto& t : ticks)
        if (t.label == "1")
            CHECK(t.x == Approx(v.frameToX(4410.0)));

    v.totalFrames = 470400 * 100;
    v.fit();
    const auto thin = rulerTicks(v, break90());
    for (size_t i = 1; i < thin.size(); ++i)
        CHECK(thin[i].x - thin[i - 1].x >= 50.0);
}

//==============================================================================
TEST_CASE("slice geometry names the slice under a pixel and the point near it", "[editor][geometry]")
{
    SliceGeometry g;
    g.view.totalFrames = 1000;
    g.view.setWidth(1000); // one frame per pixel
    g.markers = {250, 500, 900};
    CHECK(g.numSlices() == 4);
    CHECK(g.sliceStart(0) == 0);
    CHECK(g.sliceEnd(0) == 250);
    CHECK(g.sliceStart(1) == 250);
    CHECK(g.sliceEnd(3) == 1000);
    CHECK(g.sliceAtFrame(0) == 0);
    CHECK(g.sliceAtFrame(249) == 0);
    CHECK(g.sliceAtFrame(250) == 1);
    CHECK(g.sliceAtFrame(999) == 3);
    CHECK(g.sliceAtX(600) == 2);

    CHECK(g.markerAtX(252, 4.0) == 0);
    CHECK(g.markerAtX(257, 4.0) == -1);
    CHECK(g.markerAtX(498, 4.0) == 1);
    // The nearest wins when two are close.
    g.markers = {500, 504};
    CHECK(g.markerAtX(503, 4.0) == 1);

    g.markers = {250, 500, 900};
    const auto span = g.sliceSpan(1);
    CHECK(span.first == Approx(250.0));
    CHECK(span.second == Approx(500.0));
    g.view.startFrame = 400.0;
    g.view.framesPerPixel = 1.0;
    const auto offscreen = g.sliceSpan(0);
    CHECK(offscreen.second <= offscreen.first); // slice 0 ends at 250, left of the view
}

TEST_CASE("a sound without slice points is one slice", "[editor][geometry]")
{
    SliceGeometry g;
    g.view.totalFrames = 1000;
    g.view.setWidth(500);
    CHECK(g.numSlices() == 1);
    CHECK(g.sliceStart(0) == 0);
    CHECK(g.sliceEnd(0) == 1000);
    CHECK(g.sliceAtFrame(777) == 0);
    CHECK(g.markerAtX(10, 5.0) == -1);
}

//==============================================================================
TEST_CASE("waveform tables agree with the samples at every level", "[editor][peaks]")
{
    const juce::File wav(juce::File(SAMPLER_FIXTURES_DIR).getChildFile("break-90.wav"));
    const auto peaks = WaveformPeaks::build(wav);
    REQUIRE(peaks != nullptr);
    CHECK(peaks->numFrames() == 470400);
    CHECK(peaks->sampleRate() == 44100.0);
    CHECK(peaks->numChannels() == 2);

    // Exact samples (mono mix) for a small region.
    std::vector<float> samples;
    peaks->readSamples(4400, 2000, samples);
    REQUIRE(samples.size() == 2000);
    float loudest = 0.0f;
    for (const auto s : samples)
        loudest = std::max(loudest, std::abs(s));
    CHECK(loudest > 0.1f); // the first kick starts at 4410

    // Peaks at a coarse zoom bound the samples they cover.
    std::vector<WaveformPeaks::Range> ranges;
    peaks->fillPeaks(0.0, 470.4, 1000, ranges);
    REQUIRE(ranges.size() == 1000);
    float high = -1.0f, low = 1.0f;
    for (const auto& r : ranges)
    {
        high = std::max(high, r.max);
        low = std::min(low, r.min);
        CHECK(r.min <= r.max);
    }
    CHECK(high > 0.2f);
    CHECK(low < -0.2f);
    CHECK(high <= 1.0f);
    CHECK(low >= -1.0f);

    // The finest zoom reads the samples from the file.
    std::vector<WaveformPeaks::Range> fine;
    peaks->fillPeaks(4410.0, 1.0, 400, fine);
    float fineHigh = -1.0f;
    for (const auto& r : fine)
        fineHigh = std::max(fineHigh, r.max);
    std::vector<float> region;
    peaks->readSamples(4410, 400, region);
    float regionHigh = -1.0f;
    for (const auto s : region)
        regionHigh = std::max(regionHigh, s);
    CHECK(fineHigh >= regionHigh - 1.0e-6f); // channels are folded with max, the mix averages them
}

TEST_CASE("waveform tables of a missing or cancelled build are null", "[editor][peaks]")
{
    CHECK(WaveformPeaks::build(juce::File("Z:/does/not/exist.wav")) == nullptr);
    std::atomic<bool> cancel{true};
    CHECK(WaveformPeaks::build(juce::File(SAMPLER_FIXTURES_DIR).getChildFile("break-90.wav"), &cancel) == nullptr);
}

TEST_CASE("a typed bar.beat.sixteenth reads back the same when a sixteenth is not a whole number of frames", "[editor][math]")
{
    for (const double rate : {44100.0, 48000.0})
        for (const double bpm : {92.0, 128.0, 174.0})
            for (const juce::int64 firstBeat : {juce::int64{0}, juce::int64{4410}, juce::int64{12345}})
            {
                const Tempo t{bpm, firstBeat, rate};
                for (int bar = 1; bar <= 3; ++bar)
                    for (int beat = 1; beat <= 4; ++beat)
                        for (int sixteenth = 1; sixteenth <= 4; ++sixteenth)
                        {
                            const auto text = juce::String(bar) + "." + juce::String(beat) + "." + juce::String(sixteenth);
                            INFO(text << " at " << bpm << " BPM, " << rate << " Hz, first beat " << firstBeat);
                            const auto frame = parseBarBeat(t, text);
                            REQUIRE(frame.has_value());
                            CHECK(barBeatText(t, *frame) == text);
                        }
            }
}

TEST_CASE("the start of a slice is the start of the sound when there are no points", "[editor][math]")
{
    SliceGeometry g;
    g.view.totalFrames = 1000;
    CHECK(g.sliceStart(0) == 0);
    CHECK(g.sliceStart(1) == 0);
    CHECK(g.sliceStart(5) == 0);
}
