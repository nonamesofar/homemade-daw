#pragma once

// Engine-internal: includes Tracktion, so only src/engine/*.cpp may include it.
#include "EngineSetup.h"
#include "engine/PadMixer.h"

namespace sampler::engine
{
/**
    The pad player as a Tracktion plugin: the thin shell around PadMixer. It lives on the one track of the PadEngine
    preview edit and is never part of a project (design D4, D5). It adds the sounding slices to whatever the track
    already carries (nothing), so it is an instrument that ignores MIDI and takes its events from the UI instead.
*/
class SlicePadPlugin final : public tracktion::engine::Plugin
{
public:
    explicit SlicePadPlugin(tracktion::engine::PluginCreationInfo);
    ~SlicePadPlugin() override;

    static const char* getPluginName() { return "Slice pads"; }
    static const char* xmlTypeName;

    juce::String getName() const override { return "Slice pads"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Pads"; }
    juce::String getSelectableDescription() override { return "Slice pads"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    int getNumOutputChannelsGivenInputs(int) override { return 2; }
    bool producesAudioWhenNoAudioInput() override { return true; }

    void initialise(const tracktion::engine::PluginInitialisationInfo&) override;
    void initialiseWithoutStopping(const tracktion::engine::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::engine::PluginRenderContext&) override;

    PadMixer& padMixer() { return mixer; }
    const PadMixer& padMixer() const { return mixer; }

private:
    PadMixer mixer;
};
} // namespace sampler::engine
