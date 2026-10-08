#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler::platform
{
struct CaptureOptions
{
    /** Try to leave this process's own audio out of the recording (process loopback); fall back to the whole device. */
    bool excludeOwnProcess = true;
    /** Tests only: behave as if process loopback were unavailable, to exercise the fallback. */
    bool simulateProcessLoopbackFailure = false;
};

struct CaptureStats
{
    /** "endpoint-loopback" (everything the device plays) or "process-loopback" (everything except this process). */
    juce::String mode;
    /** Why process loopback was not used, when it was asked for and failed. Empty otherwise. */
    juce::String processLoopbackFailure;
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 framesWritten = 0;
    juce::int64 silenceFramesInserted = 0;
    int overruns = 0;
    int gapFills = 0;                  // how many separate silence insertions happened
    juce::int64 largestGapFrames = 0;  // the biggest single insertion
    float peak = 0.0f; // running peak since start, linear
};

/** Records what the computer is playing into a 32-bit float WAV. One implementation per platform. */
class ICaptureSource
{
public:
    virtual ~ICaptureSource() = default;

    /** Starts recording to `destination`. Fails with a message if capture cannot be started. */
    virtual juce::Result start(const juce::File& destination, const CaptureOptions&) = 0;

    /** Stops recording and finishes the file. Safe to call when not started. */
    virtual juce::Result stop() = 0;

    virtual bool isRecording() const = 0;
    virtual CaptureStats stats() const = 0;
};

/** Creates the capture source for this platform (implemented under src/platform/<os>/). */
std::unique_ptr<ICaptureSource> createCaptureSource();
} // namespace sampler::platform
