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

TEST_CASE("muting the output for capture leaves the edit alone and survives loading a file", "[engine][capture][mute]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("mute");
    const auto first = sampler::test::writeToneWav(dir.getChildFile("first.wav"), 2.0);
    const auto second = sampler::test::writeToneWav(dir.getChildFile("second.wav"), 3.0);

    sampler::EngineHost host;
    REQUIRE(host.loadFile(first));
    const auto volumeBefore = host.masterVolumeDb();
    const auto stateBefore = host.editStateXml();
    const auto undoBefore = host.undoHistoryDescription();
    REQUIRE(stateBefore.isNotEmpty());

    host.setOutputMuted(true);
    CHECK(host.isOutputMuted());
    CHECK(host.masterVolumeDb() == volumeBefore);
    CHECK(host.editStateXml() == stateBefore);       // nothing in the document (so nothing in a saved project)
    CHECK(host.undoHistoryDescription() == undoBefore); // and nothing to undo

    // Loading another file while capturing must not unmute: the app would record itself.
    REQUIRE(host.loadFile(second));
    CHECK(host.isOutputMuted());

    host.setOutputMuted(false);
    CHECK_FALSE(host.isOutputMuted());
    host.setOutputMuted(true);
    host.setOutputMuted(true); // idempotent
    CHECK(host.isOutputMuted());
}
