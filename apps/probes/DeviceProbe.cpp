// Plain JUCE audio output (no Tracktion): plays a quiet tone and reports xruns and callback timing.
// Separates "this device/driver glitches" from "the engine glitches".
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdio>

namespace
{
class ToneCallback : public juce::AudioIODeviceCallback
{
public:
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = 0.05f * static_cast<float>(std::sin(phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / rate;
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr)
                    out[c][i] = s;
        }
        ++callbacks;
        const auto us = static_cast<int>(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1e6);
        if (us > maxMicros.load())
            maxMicros = us;
        blockSize = numSamples;
    }

    std::atomic<int> callbacks{0}, maxMicros{0}, blockSize{0};

private:
    double rate = 44100.0;
    double phase = 0.0;
};
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto seconds = argc > 1 ? std::atoi(argv[1]) : 10;

    juce::AudioDeviceManager manager;
    const auto err = manager.initialiseWithDefaultDevices(0, 2);
    if (err.isNotEmpty())
    {
        std::printf("init error: %s\n", err.toRawUTF8());
        return 1;
    }

    ToneCallback cb;
    manager.addAudioCallback(&cb);
    auto* device = manager.getCurrentAudioDevice();
    std::printf("type=%s device=%s rate=%.0f buffer=%d\n", manager.getCurrentAudioDeviceType().toRawUTF8(),
                device->getName().toRawUTF8(), device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());

    for (int s = 0; s < seconds; ++s)
    {
        juce::Thread::sleep(1000);
        std::printf("t=%2d callbacks=%d maxCallbackUs=%d xruns=%d\n", s + 1, cb.callbacks.load(), cb.maxMicros.load(),
                    device->getXRunCount());
        std::fflush(stdout);
    }
    manager.removeAudioCallback(&cb);
    return 0;
}
