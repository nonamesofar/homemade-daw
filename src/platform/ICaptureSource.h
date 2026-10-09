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
    /** Tests only: fail the last start step (the stream's Start), after the output file and its writer exist. */
    bool simulateStreamStartFailure = false;
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
    /** Silence that could not be inserted because capture was stopping while the ring was full (frames). */
    juce::int64 silenceFramesDropped = 0;
    /** Packets the device flagged AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY (the device lost data before them). */
    int discontinuities = 0;
    float peak = 0.0f; // running peak since start, linear

    /** Set when capture stopped by itself or the file is incomplete; stop() then returns a failed Result. */
    juce::String error;
    /** The output device went away (default device changed, headset disconnected); capture stopped at that point. */
    bool deviceLost = false;
    /** Writing the file failed (disk full?); the recording is incomplete. */
    bool writeFailed = false;
};

/** Records what the computer is playing into a 32-bit float WAV. One implementation per platform. */
class ICaptureSource
{
public:
    virtual ~ICaptureSource() = default;

    /** Starts recording to `destination`. Fails with a message if capture cannot be started. */
    virtual juce::Result start(const juce::File& destination, const CaptureOptions&) = 0;

    /**
        Stops recording and finishes the file. Safe to call when not started. Fails (with stats().error) if capture
        ended early (device lost) or the file could not be written completely; the file then holds what was recorded.
    */
    virtual juce::Result stop() = 0;

    /** False once stopped, and also when capture ended by itself (see CaptureStats::error). */
    virtual bool isRecording() const = 0;
    virtual CaptureStats stats() const = 0;
};

/** Creates the capture source for this platform (implemented under src/platform/<os>/). */
std::unique_ptr<ICaptureSource> createCaptureSource();
} // namespace sampler::platform
