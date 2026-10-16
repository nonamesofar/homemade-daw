#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace sampler::render
{
/**
    One rendered slice: interleaved stereo float at the output device's rate. Reference counted so the pad player's
    voices and the bank can share it; the last release must happen off the audio thread (AGENTS.md §5.1).
*/
class SliceBuffer : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SliceBuffer>;

    SliceBuffer(double rate, int frames) : sampleRate(rate), samples(static_cast<size_t>(frames) * 2, 0.0f) {}

    int numFrames() const { return static_cast<int>(samples.size() / 2); }
    float* data() { return samples.data(); }
    const float* data() const { return samples.data(); }
    size_t sizeInBytes() const { return samples.size() * sizeof(float); }

    const double sampleRate;

private:
    std::vector<float> samples;
};
} // namespace sampler::render
