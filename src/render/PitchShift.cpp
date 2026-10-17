#include "render/PitchShift.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>

namespace sampler::render
{
void pitchShift(StereoChannels& audio, float semitones, double rate)
{
    const auto frames = audio[0].size();
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(2, static_cast<float>(rate));
    stretch.setTransposeSemitones(semitones);

    // exact() needs at least one block of input; a shorter slice is padded with silence and trimmed afterwards.
    const auto minimum = static_cast<size_t>(stretch.inputLatency() + stretch.outputLatency() + stretch.blockSamples()) * 2;
    const auto padded = std::max(frames, minimum);
    StereoChannels in = audio;
    StereoChannels out;
    for (size_t c = 0; c < 2; ++c)
    {
        in[c].resize(padded, 0.0f);
        out[c].assign(padded, 0.0f);
    }
    const float* inPtr[2] = {in[0].data(), in[1].data()};
    float* outPtr[2] = {out[0].data(), out[1].data()};
    stretch.exact(inPtr, static_cast<int>(padded), outPtr, static_cast<int>(padded));
    for (size_t c = 0; c < 2; ++c)
    {
        out[c].resize(frames);
        audio[c] = std::move(out[c]);
    }
}
} // namespace sampler::render
