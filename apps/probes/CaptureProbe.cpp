// Capture probe (tasks 4.1 to 4.4).
//
//   CaptureProbe record <out.wav> <seconds> [--endpoint]
//       Records what the computer plays (play a YouTube video meanwhile). Prints which loopback mode was used.
//   CaptureProbe selftest [seconds] [--mute-own]
//       Plays a 440 Hz tone itself, with the audio device closed for a few seconds in the middle (nothing playing at
//       all), records the output, and checks length, tone and gap. With --mute-own the tone is muted while recording
//       is running, and the recording must then be silent.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_events/juce_events.h>

#include "engine/EngineHost.h"
#include "platform/ICaptureSource.h"

namespace
{
bool gSimulateNoProcessLoopback = false;

class ToneCallback : public juce::AudioIODeviceCallback
{
public:
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); }
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* out, int numOut, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        const auto gain = muted.load() ? 0.0f : 0.2f;
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = gain * static_cast<float>(std::sin(phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / rate;
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr)
                    out[c][i] = s;
        }
    }

    std::atomic<bool> muted{false};

private:
    double rate = 44100.0;
    double phase = 0.0;
};

/** Amplitude of the 440 Hz component in `x` (Goertzel), as a fraction of a full-scale sine's amplitude. */
double toneAmplitude(const float* x, int n, double rate, double freq = 440.0)
{
    const auto k = 2.0 * std::cos(2.0 * juce::MathConstants<double>::pi * freq / rate);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto s0 = static_cast<double>(x[i]) + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const auto power = s1 * s1 + s2 * s2 - k * s1 * s2;
    return 2.0 * std::sqrt(juce::jmax(0.0, power)) / n;
}

struct Window
{
    double startSeconds;
    double rms;
    double tone;
};

std::vector<Window> analyse(const juce::File& wav, double& lengthSeconds, double& rate)
{
    std::vector<Window> windows;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
    if (reader == nullptr)
        return windows;

    rate = reader->sampleRate;
    lengthSeconds = static_cast<double>(reader->lengthInSamples) / rate;

    const auto step = static_cast<int>(rate / 2.0); // 0.5 s windows
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), step);
    for (juce::int64 pos = 0; pos + step <= reader->lengthInSamples; pos += step)
    {
        reader->read(&buffer, 0, step, pos, true, true);
        double sum = 0.0;
        for (int i = 0; i < step; ++i)
            sum += buffer.getSample(0, i) * buffer.getSample(0, i);
        windows.push_back({static_cast<double>(pos) / rate, std::sqrt(sum / step), toneAmplitude(buffer.getReadPointer(0), step, rate)});
    }
    return windows;
}

void printStats(const sampler::platform::CaptureStats& s)
{
    std::printf("mode=%s rate=%.0f ch=%d frames=%lld (%.2f s) silenceInserted=%lld (%.2f s) overruns=%d peak=%.4f\n",
                s.mode.toRawUTF8(), s.sampleRate, s.channels, static_cast<long long>(s.framesWritten),
                s.framesWritten / juce::jmax(1.0, s.sampleRate), static_cast<long long>(s.silenceFramesInserted),
                s.silenceFramesInserted / juce::jmax(1.0, s.sampleRate), s.overruns, s.peak);
    std::printf("gap fills: %d, largest %.3f s\n", s.gapFills, s.largestGapFrames / juce::jmax(1.0, s.sampleRate));
    if (s.processLoopbackFailure.isNotEmpty())
        std::printf("process loopback: %s\n", s.processLoopbackFailure.toRawUTF8());
}

int record(const juce::File& out, double seconds, bool excludeOwn)
{
    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = excludeOwn;
    options.simulateProcessLoopbackFailure = gSimulateNoProcessLoopback;

    const auto started = capture->start(out, options);
    if (started.failed())
    {
        std::printf("start failed: %s\n", started.getErrorMessage().toRawUTF8());
        return 1;
    }
    std::printf("recording %.0f s to %s ...\n", seconds, out.getFullPathName().toRawUTF8());
    for (int s = 0; s < static_cast<int>(seconds); ++s)
    {
        juce::Thread::sleep(1000);
        std::printf("  t=%2d peak=%.3f\n", s + 1, capture->stats().peak);
        std::fflush(stdout);
    }
    capture->stop();
    printStats(capture->stats());
    return 0;
}

/** Pumps the message loop (the engine's device scan and render tasks need it). */
void pump(double seconds)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

/**
    Task 4.4 with the real engine: the engine plays a tone, a device-mode capture runs, and the engine output is
    muted for the first half and live for the second. The first half of the recording must be silent.
*/
int engineMuteTest()
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests");
    dir.createDirectory();
    const auto source = dir.getChildFile("engine-mute-tone.wav");
    {
        juce::WavAudioFormat wav;
        source.deleteFile();
        auto stream = source.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 2, 16, {}, 0));
        stream.release();
        juce::AudioBuffer<float> b(2, 44100 * 20);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto s = 0.3f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
            b.setSample(0, i, s);
            b.setSample(1, i, s);
        }
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }

    sampler::EngineHost host;
    pump(4.0); // the engine needs its start-up device scan to finish before playing
    if (!host.loadFile(source))
    {
        std::printf("cannot load tone%s", "\n");
        return 1;
    }
    host.setOutputMuted(true);
    host.play();
    pump(0.5);

    const auto out = dir.getChildFile("engine-mute-capture.wav");
    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false; // device mode, the case that needs muting
    if (capture->start(out, options).failed())
        return 1;

    pump(7.0);                  // 0 - 7 s muted
    host.setOutputMuted(false);
    pump(7.0);                  // 7 - 14 s live
    capture->stop();
    host.stop();

    double length = 0.0, rate = 0.0;
    const auto windows = analyse(out, length, rate);
    printStats(capture->stats());
    int mutedOk = 0, mutedN = 0, liveOk = 0, liveN = 0;
    for (const auto& w : windows)
    {
        const auto end = w.startSeconds + 0.5;
        if (w.startSeconds >= 0.5 && end <= 6.5)
        {
            ++mutedN;
            if (w.rms < 1.0e-4)
                ++mutedOk;
        }
        else if (w.startSeconds >= 8.0 && end <= 13.5)
        {
            ++liveN;
            if (w.rms > 5.0e-4)
                ++liveOk;
        }
        std::printf("  %5.1f s  rms=%.4f%s", w.startSeconds, w.rms, "\n");
    }
    std::printf("muted half silent: %d/%d %s\n", mutedOk, mutedN, mutedN > 0 && mutedOk == mutedN ? "PASS" : "FAIL");
    std::printf("live half audible: %d/%d %s\n", liveOk, liveN, liveN > 0 && liveOk == liveN ? "PASS" : "FAIL");
    return mutedN > 0 && mutedOk == mutedN && liveN > 0 && liveOk == liveN ? 0 : 3;
}

int selftest(double seconds, bool muteOwn, bool processMode)
{
    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/capture-selftest.wav");

    juce::AudioDeviceManager manager;
    ToneCallback tone;
    auto openDevice = [&]
    {
        const auto err = manager.initialiseWithDefaultDevices(0, 2);
        if (err.isNotEmpty())
        {
            std::printf("audio device error: %s\n", err.toRawUTF8());
            return false;
        }
        manager.addAudioCallback(&tone);
        return true;
    };
    auto closeDevice = [&]
    {
        manager.removeAudioCallback(&tone);
        manager.closeAudioDevice();
    };

    // Timeline: tone 2-6 s, device closed 6-9 s (nothing playing at all), tone 9 s to the end minus 1 s.
    const double toneAStart = 2.0, toneAEnd = 6.0, toneBStart = 9.0, toneBEnd = seconds - 1.0;

    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.simulateProcessLoopbackFailure = gSimulateNoProcessLoopback;
    options.excludeOwnProcess = processMode; // device mode: our own tone must be captured; process mode: it must not be
    if (muteOwn)
        tone.muted = true;
    const bool expectSilence = muteOwn || processMode;

    const auto started = capture->start(out, options);
    if (started.failed())
    {
        std::printf("start failed: %s\n", started.getErrorMessage().toRawUTF8());
        return 1;
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto now = [&] { return (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0; };
    auto waitUntil = [&](double t)
    {
        while (now() < t)
            juce::Thread::sleep(5);
    };

    waitUntil(toneAStart);
    if (!openDevice())
        return 1;
    waitUntil(toneAEnd);
    closeDevice();
    waitUntil(toneBStart);
    if (!openDevice())
        return 1;
    waitUntil(toneBEnd);
    closeDevice();
    waitUntil(seconds);
    capture->stop();

    const auto stats = capture->stats();
    printStats(stats);

    double length = 0.0, rate = 0.0;
    const auto windows = analyse(out, length, rate);
    std::printf("file: %.3f s (expected %.1f s)\n", length, seconds);

    // 0.5 s windows; ignore windows touching a boundary (device open/close latency).
    int toneWindows = 0, toneOk = 0, quietWindows = 0, quietOk = 0;
    for (const auto& w : windows)
    {
        const auto end = w.startSeconds + 0.5;
        const bool inTone = (w.startSeconds >= toneAStart + 1.0 && end <= toneAEnd - 0.5)
                            || (w.startSeconds >= toneBStart + 1.0 && end <= toneBEnd - 0.5);
        const bool inGap = w.startSeconds >= toneAEnd + 1.0 && end <= toneBStart - 0.5;
        if (inTone)
        {
            ++toneWindows;
            // The device volume scales what loopback sees, so judge the tone by purity (a sine has amplitude = rms x 1.414).
            const bool toneHeard = w.tone > 5.0e-4 && w.tone > 0.9 * w.rms * 1.4142;
            if (expectSilence ? w.rms < 1.0e-4 : toneHeard)
                ++toneOk;
        }
        else if (inGap)
        {
            ++quietWindows;
            if (w.rms < 1.0e-4)
                ++quietOk;
        }
        std::printf("  %5.1f s  rms=%.4f  440Hz=%.4f\n", w.startSeconds, w.rms, w.tone);
    }

    const bool lengthOk = std::abs(length - seconds) < 0.3;
    std::printf("\nlength within 0.3 s: %s\n", lengthOk ? "PASS" : "FAIL");
    std::printf("%s windows: %d/%d %s\n", expectSilence ? "own-tone windows (must be silent)" : "tone", toneOk, toneWindows,
                toneWindows > 0 && toneOk == toneWindows ? "PASS" : "FAIL");
    std::printf("gap windows silent: %d/%d %s\n", quietOk, quietWindows, quietWindows > 0 && quietOk == quietWindows ? "PASS" : "FAIL");

    const bool pass = lengthOk && toneWindows > 0 && toneOk == toneWindows && quietWindows > 0 && quietOk == quietWindows;
    return pass ? 0 : 3;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "";

    bool excludeOwn = true, muteOwn = false, processMode = false;
    for (int i = 2; i < argc; ++i)
    {
        if (juce::String(argv[i]) == "--endpoint")
            excludeOwn = false;
        if (juce::String(argv[i]) == "--mute-own")
            muteOwn = true;
        if (juce::String(argv[i]) == "--process")
            processMode = true;
        if (juce::String(argv[i]) == "--simulate-no-process")
            gSimulateNoProcessLoopback = true;
    }

    if (mode == "record" && argc >= 4)
        return record(juce::File(juce::String::fromUTF8(argv[2])), std::atof(argv[3]), excludeOwn);
    if (mode == "engine-mute")
        return engineMuteTest();
    if (mode == "selftest")
        return selftest(argc >= 3 && std::atof(argv[2]) > 0 ? std::atof(argv[2]) : 14.0, muteOwn, processMode);

    std::printf("usage: CaptureProbe record <out.wav> <seconds> [--endpoint]\n       CaptureProbe selftest [seconds] [--mute-own]\n");
    return 2;
}
