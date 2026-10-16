#include "SlicePadPlugin.h"

namespace te = tracktion::engine;

namespace sampler::engine
{
const char* SlicePadPlugin::xmlTypeName = "samplerPads";

SlicePadPlugin::SlicePadPlugin(te::PluginCreationInfo info) : te::Plugin(info) {}

SlicePadPlugin::~SlicePadPlugin() { notifyListenersOfDeletion(); }

void SlicePadPlugin::initialise(const te::PluginInitialisationInfo& info) { mixer.prepare(info.sampleRate); }
void SlicePadPlugin::initialiseWithoutStopping(const te::PluginInitialisationInfo& info) { mixer.prepare(info.sampleRate); }

void SlicePadPlugin::applyToBuffer(const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr)
        return;
    float* channels[2] = {nullptr, nullptr};
    const int numChannels = juce::jmin(2, context.destBuffer->getNumChannels());
    for (int c = 0; c < numChannels; ++c)
        channels[c] = context.destBuffer->getWritePointer(c, context.bufferStartSample);
    mixer.process(channels, numChannels, context.bufferNumSamples);
}
} // namespace sampler::engine
