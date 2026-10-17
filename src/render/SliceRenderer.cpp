#include "render/SliceRenderer.h"

#include "render/PitchShift.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <CDSPResampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sampler::render
{
namespace
{
using Channels = StereoChannels;

Channels toStereo(const juce::AudioBuffer<float>& region)
{
    const auto frames = static_cast<size_t>(region.getNumSamples());
    Channels out;
    for (auto& channel : out)
        channel.assign(frames, 0.0f);
    if (region.getNumChannels() == 0 || frames == 0)
        return out;
    std::copy_n(region.getReadPointer(0), frames, out[0].begin());
    std::copy_n(region.getReadPointer(region.getNumChannels() > 1 ? 1 : 0), frames, out[1].begin());
    return out;
}

Channels resample(const Channels& audio, double sourceRate, double outputRate)
{
    const auto inFrames = audio[0].size();
    const auto outFrames = static_cast<size_t>(std::ceil(static_cast<double>(inFrames) * outputRate / sourceRate));
    Channels out;
    for (size_t c = 0; c < 2; ++c)
    {
        std::vector<double> in(audio[c].begin(), audio[c].end());
        std::vector<double> result(outFrames, 0.0);
        r8b::CDSPResampler24 resampler(sourceRate, outputRate, static_cast<int>(inFrames));
        resampler.oneshot(in.data(), static_cast<int>(inFrames), result.data(), static_cast<int>(outFrames));
        out[c].resize(outFrames);
        std::transform(result.begin(), result.end(), out[c].begin(), [](double v) { return static_cast<float>(v); });
    }
    return out;
}
} // namespace

SliceBuffer::Ptr renderSlice(const juce::AudioBuffer<float>& region, const SliceParams& params)
{
    auto audio = toStereo(region);
    if (audio[0].empty())
        return new SliceBuffer(params.outputRate, 0);

    if (params.reverse)
        for (auto& channel : audio)
            std::reverse(channel.begin(), channel.end());

    if (std::abs(params.pitchSemitones) > 0.001f)
        pitchShift(audio, params.pitchSemitones, params.sourceRate);

    if (std::abs(params.outputRate - params.sourceRate) > 0.5)
        audio = resample(audio, params.sourceRate, params.outputRate);

    const auto frames = static_cast<int>(audio[0].size());
    SliceBuffer::Ptr buffer = new SliceBuffer(params.outputRate, frames);
    const float gain = juce::Decibels::decibelsToGain(params.gainDb, -100.0f);
    const int fade = std::min(frames / 2, std::max(1, static_cast<int>(params.outputRate * edgeFadeMs / 1000.0)));
    float* out = buffer->data();
    for (int i = 0; i < frames; ++i)
    {
        float envelope = gain;
        if (i < fade)
            envelope *= static_cast<float>(i) / static_cast<float>(fade);
        else if (i >= frames - fade)
            envelope *= static_cast<float>(frames - 1 - i) / static_cast<float>(fade);
        out[2 * i] = audio[0][static_cast<size_t>(i)] * envelope;
        out[2 * i + 1] = audio[1][static_cast<size_t>(i)] * envelope;
    }
    return buffer;
}

SliceBuffer::Ptr renderFromFile(const juce::File& wav, juce::int64 startFrame, juce::int64 endFrame, const SliceParams& params)
{
    juce::WavAudioFormat format;
    auto stream = wav.createInputStream();
    if (stream == nullptr)
        return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    if (reader == nullptr)
        return nullptr;
    const auto from = std::max<juce::int64>(0, startFrame);
    const auto to = std::min<juce::int64>(reader->lengthInSamples, endFrame);
    if (to <= from)
        return nullptr;
    juce::AudioBuffer<float> region(static_cast<int>(std::min<juce::int64>(2, reader->numChannels)), static_cast<int>(to - from));
    if (! reader->read(&region, 0, region.getNumSamples(), from, true, region.getNumChannels() > 1))
        return nullptr;
    auto adjusted = params;
    adjusted.sourceRate = reader->sampleRate;
    return renderSlice(region, adjusted);
}
} // namespace sampler::render
