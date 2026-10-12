#pragma once

// A scripted System audio source for controller tests: no device, the test pushes the audio.
#include "engine/TakeRecorder.h"
#include "platform/ICaptureSource.h"

#include <cmath>
#include <cmath>
#include <functional>
#include <limits>
#include <limits>

namespace sampler::test
{
class FakeCaptureSource final : public platform::ICaptureSource
{
public:
    /** Shared settings of the fake devices (the tests set them before the controller makes a capture). */
    struct Script
    {
        double sampleRate = 48000.0;
        double latencySeconds = 0.0;
        juce::String mode = "process-loopback";
        juce::String deviceName = "Fake Speakers";
        juce::String startFailure; // non-empty: start() fails with this text
        /** False: a device that sends no packets while nothing plays (push() delivers nothing). */
        bool deliverPackets = true;
        std::function<double()> clockMs = [] { return juce::Time::getMillisecondCounterHiRes(); };
    };

    explicit FakeCaptureSource(Script& s) : script(s) {}
    ~FakeCaptureSource() override { stop(); }

    juce::Result start(const juce::File& destination, const platform::CaptureOptions& options) override
    {
        options_ = options;
        if (script.startFailure.isNotEmpty())
            return juce::Result::fail(script.startFailure);
        platform::TakeOptions o;
        o.file = destination;
        o.sampleRate = script.sampleRate;
        o.channels = 2;
        o.trimSilence = options.trimSilence;
        o.maxSeconds = options.maxSeconds;
        o.ringSeconds = 40;
        const auto r = writer.open(o);
        if (r.failed())
            return r;
        startedAt = script.clockMs();
        running = true;
        return juce::Result::ok();
    }

    juce::Result stop() override
    {
        if (writer.isOpen())
            finished = writer.finish();
        running = false;
        return error.isEmpty() ? juce::Result::ok() : juce::Result::fail(error);
    }

    bool isRecording() const override { return running && ! lost && ! writeBroken; }

    platform::CaptureStats stats() const override
    {
        platform::CaptureStats s;
        s.mode = script.mode;
        s.deviceName = script.deviceName;
        s.sampleRate = script.sampleRate;
        s.channels = 2;
        s.startedAtMs = startedAt;
        s.streamLatencySeconds = script.latencySeconds;
        s.deviceLost = lost;
        s.writeFailed = writeBroken;
        s.error = error;
        s.framesWritten = writer.framesKept();
        s.limitReached = writer.limitReached();
        return s;
    }

    void discardBefore(double clockMs) override
    {
        if (std::isinf(clockMs))
        {
            writer.keepFrom((std::numeric_limits<juce::int64>::max)());
            return;
        }
        const auto frames = static_cast<juce::int64>((clockMs - startedAt) * script.sampleRate / 1000.0);
        writer.keepFrom(juce::jmax<juce::int64>(0, frames));
    }

    const platform::TakeWriter& take() const override { return writer; }
    platform::TakeResult takeResult() const override { return finished; }

    //==============================================================================
    /** `seconds` of audio: a square wave at 0.5, or silence. */
    void push(double seconds, bool tone)
    {
        if (! script.deliverPackets)
            return;
        const int total = static_cast<int>(seconds * script.sampleRate);
        std::vector<float> block(480 * 2);
        for (int done = 0; done < total; done += 480)
        {
            const int n = juce::jmin(480, total - done);
            for (int i = 0; i < n * 2; ++i)
                block[static_cast<size_t>(i)] = tone ? ((i / 2) % 2 == 0 ? 0.5f : -0.5f) : 0.0f;
            writer.pushInterleaved(block.data(), 2, n);
        }
    }

    /** While held, the writer thread does not drain the ring. */
    void holdWriter(bool hold) { writer.holdWriter(hold); }

    void loseDevice()
    {
        lost = true;
        error = "the output device went away; capture stopped";
    }
    void breakWriting() { writeBroken = true; }

    platform::CaptureOptions options_;

private:
    Script& script;
    platform::TakeWriter writer;
    platform::TakeResult finished;
    double startedAt = 0.0;
    bool running = false, lost = false, writeBroken = false;
    juce::String error;
};

/** A scripted tap on a track or on the master: the test pushes the audio and says where the first block lies. */
class FakeTap final : public engine::TakeTap
{
public:
    FakeTap(const platform::TakeOptions& options, bool readyAtOnce) : ready(readyAtOnce)
    {
        auto o = options;
        o.sampleRate = 48000.0;
        o.channels = 2;
        o.ringSeconds = 40;
        writer.open(o);
    }

    const platform::TakeWriter& take() const override { return writer; }
    double firstFrameSeconds() const override { return first; }
    bool isReady() const override { return ready; }
    bool endedByRateChange() const override { return rateChanged; }
    void detach() override { detached = true; }
    platform::TakeResult finish() override
    {
        detached = true;
        return writer.finish();
    }

    /** The first block lies at `atSeconds` on the timeline. */
    void push(double seconds, bool tone, double atSeconds)
    {
        if (first < 0.0)
            first = atSeconds;
        const int total = static_cast<int>(seconds * 48000.0);
        std::vector<float> block(480 * 2);
        for (int done = 0; done < total; done += 480)
        {
            const int n = juce::jmin(480, total - done);
            for (int i = 0; i < n * 2; ++i)
                block[static_cast<size_t>(i)] = tone ? ((i / 2) % 2 == 0 ? 0.5f : -0.5f) : 0.0f;
            writer.pushInterleaved(block.data(), 2, n);
        }
    }

    /** Tests: every write from now on fails (a full disk). */
    void failWrites() { writer.failWritesAfterFrames(0); }

    ~FakeTap() override { ++destroyedCount; }
    /** Tests: how many taps have been destroyed so far (to see when the slots of a finish die). */
    static inline int destroyedCount = 0;

    bool ready = true, detached = false, rateChanged = false;

private:
    platform::TakeWriter writer;
    double first = -1.0;
};

class FakeTakeRecorder final : public engine::TakeRecorder
{
public:
    std::unique_ptr<engine::TakeTap> tapTrack(const juce::String& trackId, const platform::TakeOptions& o, juce::String& error) override
    {
        if (failure.isNotEmpty())
        {
            error = failure;
            return nullptr;
        }
        auto tap = std::make_unique<FakeTap>(o, readyAtOnce);
        taps.push_back(tap.get());
        tracks.push_back(trackId);
        return tap;
    }

    std::unique_ptr<engine::TakeTap> tapMaster(const platform::TakeOptions& o, juce::String& error) override
    {
        if (failure.isNotEmpty())
        {
            error = failure;
            return nullptr;
        }
        auto tap = std::make_unique<FakeTap>(o, readyAtOnce);
        taps.push_back(tap.get());
        tracks.push_back("master");
        return tap;
    }

    std::vector<FakeTap*> taps;
    std::vector<juce::String> tracks;
    bool readyAtOnce = true;
    juce::String failure;
};
} // namespace sampler::test
