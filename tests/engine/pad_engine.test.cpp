// M3 3.4: the pad engine on a real device. Needs an output device (hardware label); the pad player core is covered
// offline by tests/engine/pad_mixer.test.cpp.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/EngineHost.h"
#include "engine/PadEngine.h"
#include "engine/testkit/FilePlayer.h"
#include "platform/ICaptureSource.h"
#include "render/SliceRenderer.h"

#include <cmath>

namespace
{
void pump(double seconds)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

bool waitForAudio(sampler::EngineHost& host)
{
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! host.isAudioReady() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    return host.isAudioReady();
}

juce::AudioBuffer<float> sine(double frequency, double rate, double seconds)
{
    const auto frames = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.4f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * frequency * i / rate)));
    return buffer;
}

double amplitudeAt(const float* x, int n, double rate, double freq)
{
    const auto k = 2.0 * std::cos(2.0 * juce::MathConstants<double>::pi * freq / rate);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto s0 = static_cast<double>(x[i]) + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / n;
}

sampler::render::SliceBank::Ptr bankWithSine(double frequency, double rate, double seconds)
{
    sampler::render::SliceParams params;
    params.sourceRate = params.outputRate = rate;
    sampler::render::SliceBank::Ptr bank = new sampler::render::SliceBank();
    bank->buffers[0] = sampler::render::renderSlice(sine(frequency, rate, seconds), params);
    bank->modes[0] = sampler::render::PlayMode::oneShot;
    return bank;
}
} // namespace

TEST_CASE("pad events produce output while the preview edit is stopped", "[pads][engine][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::EngineHost host;
    if (! waitForAudio(host))
        SKIP("no audio output on this machine");
    sampler::PadEngine pads(host);
    pump(0.3);

    const auto idle = pads.blocksProcessed();
    pump(0.3);
    CHECK(pads.blocksProcessed() > idle); // the plugin is processed although nothing plays
    CHECK_FALSE(pads.usesKeepPlayingFallback());

    pads.setBank(bankWithSine(1000.0, host.currentSampleRate(), 0.3));
    pump(0.1);
    pads.padOn(0);
    pump(0.15);
    CHECK(pads.soundingCount(0) == 1);
    CHECK(pads.isAnySounding());
    pump(0.5);
    CHECK(pads.soundingCount(0) == 0); // the 0.3 s slice has ended
    pads.clearBank();
    pump(0.2);
}

TEST_CASE("a pad is heard while the project edit plays to the same device", "[pads][engine][hardware][realtime]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::EngineHost host;
    if (! waitForAudio(host))
        SKIP("no audio output on this machine");
    sampler::PadEngine pads(host);
    sampler::FilePlayer player(host);
    const auto dir = sampler::test::tempDir("pad-engine");
    const auto tone = sampler::test::writeToneWav(dir.getChildFile("project-tone.wav"), 8.0);
    REQUIRE(player.loadFile(tone));

    const double rate = host.currentSampleRate();
    pads.setBank(bankWithSine(1000.0, rate, 2.0));
    pump(0.3);
    player.play();
    pump(0.5);

    const auto out = dir.getChildFile("pad-engine-capture.wav");
    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false;
    REQUIRE(capture->start(out, options).wasOk());
    pump(1.0);
    pads.padOn(0);
    pump(2.5);
    capture->stop();
    player.stop();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(out.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    const auto n = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> recorded(static_cast<int>(reader->numChannels), n);
    reader->read(&recorded, 0, n, 0, true, true);
    const auto capRate = reader->sampleRate;
    const int window = static_cast<int>(capRate / 2.0);
    REQUIRE(n > static_cast<int>(capRate * 3.0));
    const auto at = [&](double seconds, double freq) {
        return amplitudeAt(recorded.getReadPointer(0) + static_cast<int>(seconds * capRate), window, capRate, freq);
    };
    // Before the pad: the project tone only. During the pad: both.
    CHECK(at(0.3, 220.0) > 0.03);
    CHECK(at(0.3, 1000.0) < 0.01);
    CHECK(at(1.8, 220.0) > 0.03);
    CHECK(at(1.8, 1000.0) > 0.05);
}
