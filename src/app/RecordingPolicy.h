#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace sampler::app
{
/** One System audio take that is running. */
struct SystemRecording
{
    /** The take records the whole device (process loopback could not leave Sampler out). */
    bool endpointMode = false;
    /** Friendly name of the recorded playback device. */
    juce::String deviceName;
};

/**
    The decisions of a recording that are plain logic. Mute policy (design D4): Sampler mutes its own output when a
    System take records everything a device plays, unless Sampler clearly plays to another device: a Windows audio
    (WASAPI) or DirectSound output whose name neither contains nor is contained in the recorded device's name. On doubt
    (ASIO or an unknown driver type, an unknown name) it mutes: a silent song is better than a song in the take.
*/
struct RecordingPolicy
{
    static bool shouldMuteOutput(const std::vector<SystemRecording>& systemTakes, const juce::String& engineDeviceName,
                                 const juce::String& engineDeviceType);
    static bool clearlyOtherDevice(const juce::String& recordedName, const juce::String& engineName, const juce::String& engineType);

    /** The text of the no-signal warning on a growing System take. */
    static juce::String noSignalText(const juce::String& deviceName);

    /** How long a System take has to hear nothing (seconds) before the warning shows. */
    static constexpr double noSignalSeconds = 3.0;
    /**
        Used when the device reports no stream latency (both loopback modes do, on the dev machine): a click written by
        an output callback was heard by endpoint loopback 41.2 ms later (docs/m0-findings.md, M2b-e).
    */
    static constexpr double fallbackCaptureLatencySeconds = 0.041;
    /** The longest recording (seconds). */
    static constexpr double maxRecordingSeconds = 1800.0;
};
} // namespace sampler::app
