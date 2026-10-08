#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/EngineHost.h"

namespace
{
double wavLengthSeconds(const juce::File& f)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
    return r != nullptr ? static_cast<double>(r->lengthInSamples) / r->sampleRate : -1.0;
}
} // namespace

TEST_CASE("engine decodes a WAV and reports its format", "[engine][playback]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("decode");
    auto wav = sampler::test::writeToneWav(dir.getChildFile("tone.wav"), 2.0);

    sampler::EngineHost host;
    const auto info = host.probeDecode(wav);
    REQUIRE(info.ok);
    CHECK(info.channels == 2);
    CHECK(info.sampleRate == 44100.0);
    CHECK(info.lengthInFrames == 88200);
}

TEST_CASE("offline render of 16 bars at 120 BPM is 32 s and faster than real time", "[engine][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("render");
    auto wav = sampler::test::writeToneWav(dir.getChildFile("source.wav"), 40.0);
    auto out = dir.getChildFile("out.wav");

    sampler::EngineHost host;
    REQUIRE(host.loadFile(wav));
    host.setTempo(120.0);

    constexpr double sixteenBarsSeconds = 16 * 4 * 60.0 / 120.0; // 32 s
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    REQUIRE(host.renderToWav(out, 0.0, sixteenBarsSeconds));
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

    const auto length = wavLengthSeconds(out);
    INFO("render took " << elapsedSeconds << " s, file is " << length << " s");
    CHECK(length == Catch::Approx(sixteenBarsSeconds).margin(0.05));
    CHECK(elapsedSeconds < sixteenBarsSeconds);
}
