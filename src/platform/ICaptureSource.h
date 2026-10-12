#pragma once

#include "TakeWriter.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

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

    /** The playback device to record: a `PlaybackDevice::id`, or empty for the Windows default device (design D4). */
    juce::String deviceId;
    /** System takes: cut the silence before the first and after the last sound (design D5). */
    bool trimSilence = false;
    /** Stop recording after this many seconds of audio (0 = no limit). */
    double maxSeconds = 0.0;
};

struct CaptureStats
{
    /** "endpoint-loopback" (everything the device plays) or "process-loopback" (everything except this process). */
    juce::String mode;
    /** Why process loopback was not used, when it was asked for and failed. Empty otherwise. */
    juce::String processLoopbackFailure;
    /** Friendly name of the recorded playback device. */
    juce::String deviceName;
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

    /** Delay the device reports between a sample being played and being captured, in seconds. */
    double streamLatencySeconds = 0.0;
    /** `Time::getMillisecondCounterHiRes()` at the moment frame 0 of the capture was taken. */
    double startedAtMs = 0.0;
    /** The take reached `CaptureOptions::maxFrames`. */
    bool limitReached = false;

    /** Set when capture stopped by itself or the file is incomplete; stop() then returns a failed Result. */
    juce::String error;
    /** The output device went away (default device changed, headset disconnected); capture stopped at that point. */
    bool deviceLost = false;
    /** Writing the file failed (disk full?); the recording is incomplete. */
    bool writeFailed = false;
};

/** An active playback (render) device. */
struct PlaybackDevice
{
    juce::String id;   // stable endpoint id, saved in the project as "system:<id>"
    juce::String name; // friendly name, e.g. "Speakers (Realtek Audio)"
    bool isDefault = false;
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

    /**
        Frames captured before this moment (`Time::getMillisecondCounterHiRes()` domain, like CaptureStats::startedAtMs)
        are not kept; it applies to what arrives after the call, so a caller that wants to drop the lead-in calls it
        right after `start` with infinity (everything is dropped) and again with the real moment. Used when the song
        starts a little after the capture.
    */
    virtual void discardBefore(double clockMs) = 0;

    /** The live side of the take (levels, waveform buckets, sound heard), readable from any thread while recording. */
    virtual const TakeWriter& take() const = 0;
    /** The finished take: valid after `stop()`. */
    virtual TakeResult takeResult() const = 0;
};

/** Creates the capture source for this platform (implemented under src/platform/<os>/). */
std::unique_ptr<ICaptureSource> createCaptureSource();

/** Active playback devices with the Windows default marked; empty if they cannot be listed. */
std::vector<PlaybackDevice> listPlaybackDevices();
} // namespace sampler::platform
