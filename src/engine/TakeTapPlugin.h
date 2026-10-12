#pragma once

// Engine-internal: includes Tracktion, so only src/engine/*.cpp may include it.
#include "EngineSetup.h"
#include "platform/TakeWriter.h"

#include <atomic>

namespace sampler::engine
{
/**
    A plugin that copies what passes through it into a TakeWriter and changes nothing (design D2, D3). It is put at the
    end of a track's plugin list, or of the master plugin list, only while a recording runs, and never through the undo
    manager. It carries the property `sampler_tap`, which saveEditAtomically strips, so it never reaches a saved project.

    Real-time code (AGENTS.md section 5.1): `applyToBuffer` allocates nothing, takes no lock and does no I/O.
    The take file is opened in `initialise` (message thread), once the sample rate is known.
*/
class TakeTapPlugin final : public tracktion::engine::Plugin
{
public:
    explicit TakeTapPlugin(tracktion::engine::PluginCreationInfo);
    ~TakeTapPlugin() override;

    static const char* getPluginName() { return "Take tap"; }
    static const char* xmlTypeName;
    /** The property that marks a tap in the edit's state. */
    static const juce::Identifier tapProperty;

    juce::String getName() const override { return "Take tap"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Tap"; }
    juce::String getSelectableDescription() override { return "Take tap"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }

    void initialise(const tracktion::engine::PluginInitialisationInfo&) override;
    void initialiseWithoutStopping(const tracktion::engine::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::engine::PluginRenderContext&) override;

    //==============================================================================
    /** Message thread. The take is opened on the first `initialise`. */
    void configure(const platform::TakeOptions&);
    /**
        Master tap only. Tracktion runs the master volume and pan after the master plugin list, so a tap in that list
        would hear the mix before the master fader. Given the master volume plugin, the tap records the mix as that plugin
        will make it: it reads the fader, pan and polarity on the audio thread (like the plugin itself does, with a
        50 ms ramp against clicks) and applies them to its own copy. The buffer that passes through is not changed. A
        master mute is the fader at silence, so it is covered too.
    */
    void followMasterVolume(tracktion::engine::VolumeAndPanPlugin::Ptr);
    /** Stops delivering blocks; waits for a block that is being delivered. Does its work on the first call only. */
    void detach();

    platform::TakeWriter& writer() { return take; }
    const platform::TakeWriter& writer() const { return take; }
    bool isReady() const { return active.load(std::memory_order_acquire); }
    /** Timeline seconds of the first block delivered while playing, or negative. */
    double firstFrameSeconds() const { return firstSeconds.load(std::memory_order_acquire); }
    /** The device sample rate changed after the take began: the take stopped growing (its file keeps the old rate). */
    bool endedByRateChange() const { return rateChanged.load(std::memory_order_acquire); }
    /** `detach` gave up waiting for a block that was still in flight (should never happen). */
    bool detachTimedOut() const { return timedOut.load(std::memory_order_acquire); }

private:
    void open(double rate);
    void pushWithMasterGain(const float* const* channels, int numChannels, int frames, int offset, juce::int64 timelineFrame);

    platform::TakeWriter take;
    platform::TakeOptions pending;
    bool configured = false;
    std::atomic<bool> active{false};
    std::atomic<int> inCall{0};
    std::atomic<bool> timedOut{false};
    std::atomic<bool> detachDone{false};
    std::atomic<double> firstSeconds{-1.0};
    std::atomic<double> rateHz{48000.0};
    std::atomic<bool> rateChanged{false};

    // Master tap only.
    tracktion::engine::VolumeAndPanPlugin::Ptr masterVolume;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainL, gainR;
    juce::AudioBuffer<float> scratch; // sized once in the constructor: the audio thread never allocates
};
} // namespace sampler::engine
