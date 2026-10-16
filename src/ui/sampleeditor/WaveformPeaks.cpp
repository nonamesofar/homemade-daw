#include "ui/sampleeditor/WaveformPeaks.h"

#include <algorithm>
#include <cmath>

namespace sampler::ui::editor
{
namespace
{
constexpr int readBlock = 65536;

WaveformPeaks::Range merge(WaveformPeaks::Range a, WaveformPeaks::Range b)
{
    return {std::min(a.min, b.min), std::max(a.max, b.max)};
}
} // namespace

std::shared_ptr<WaveformPeaks> WaveformPeaks::build(const juce::File& wav, const std::atomic<bool>* cancel)
{
    juce::WavAudioFormat format;
    auto stream = wav.createInputStream();
    if (stream == nullptr)
        return nullptr;
    std::unique_ptr<juce::AudioFormatReader> in(format.createReaderFor(stream.release(), true));
    if (in == nullptr || in->lengthInSamples <= 0)
        return nullptr;

    std::shared_ptr<WaveformPeaks> peaks(new WaveformPeaks());
    peaks->file = wav;
    peaks->frames = in->lengthInSamples;
    peaks->rate = in->sampleRate;
    peaks->channels = static_cast<int>(in->numChannels);

    const auto numBlocks = static_cast<size_t>((peaks->frames + blockSizes[0] - 1) / blockSizes[0]);
    auto& base = peaks->levels[0];
    base.assign(numBlocks, Range{1.0e9f, -1.0e9f});

    juce::AudioBuffer<float> buffer(peaks->channels, readBlock);
    for (juce::int64 position = 0; position < peaks->frames; position += readBlock)
    {
        if (cancel != nullptr && cancel->load())
            return nullptr;
        const auto n = static_cast<int>(std::min<juce::int64>(readBlock, peaks->frames - position));
        if (! in->read(&buffer, 0, n, position, true, true))
            return nullptr;
        for (int c = 0; c < peaks->channels; ++c)
        {
            const float* data = buffer.getReadPointer(c);
            for (int i = 0; i < n; ++i)
            {
                auto& r = base[static_cast<size_t>((position + i) / blockSizes[0])];
                r.min = std::min(r.min, data[i]);
                r.max = std::max(r.max, data[i]);
            }
        }
    }

    for (int level = 1; level < 3; ++level)
    {
        const int ratio = blockSizes[level] / blockSizes[level - 1];
        const auto& below = peaks->levels[level - 1];
        auto& here = peaks->levels[level];
        here.reserve(below.size() / static_cast<size_t>(ratio) + 1);
        for (size_t i = 0; i < below.size(); i += static_cast<size_t>(ratio))
        {
            Range r = below[i];
            for (size_t j = i + 1; j < std::min(below.size(), i + static_cast<size_t>(ratio)); ++j)
                r = merge(r, below[j]);
            here.push_back(r);
        }
    }
    return peaks;
}

void WaveformPeaks::readRegion(juce::int64 startFrame, int count, juce::AudioBuffer<float>& into) const
{
    into.setSize(channels, std::max(0, count), false, true, false);
    into.clear();
    if (count <= 0 || startFrame >= frames)
        return;
    const juce::ScopedLock lock(readerLock);
    if (reader == nullptr)
    {
        juce::WavAudioFormat format;
        if (auto stream = file.createInputStream())
            reader.reset(format.createReaderFor(stream.release(), true));
    }
    if (reader == nullptr)
        return;
    const auto from = std::max<juce::int64>(0, startFrame);
    const auto offset = static_cast<int>(from - startFrame);
    const auto n = static_cast<int>(std::min<juce::int64>(count - offset, frames - from));
    if (n > 0)
        reader->read(&into, offset, n, from, true, true);
}

void WaveformPeaks::fillPeaks(double startFrame, double framesPerPixel, int numPixels, std::vector<Range>& out) const
{
    out.assign(static_cast<size_t>(std::max(0, numPixels)), Range{0.0f, 0.0f});
    if (numPixels <= 0 || framesPerPixel <= 0.0 || frames <= 0)
        return;

    if (framesPerPixel < blockSizes[0])
    {
        const auto first = static_cast<juce::int64>(std::floor(startFrame));
        const auto count = static_cast<int>(std::ceil(framesPerPixel * numPixels)) + 2;
        juce::AudioBuffer<float> region;
        readRegion(first, count, region);
        for (int p = 0; p < numPixels; ++p)
        {
            const auto from = static_cast<juce::int64>(std::floor(startFrame + p * framesPerPixel)) - first;
            const auto to = std::max<juce::int64>(from + 1, static_cast<juce::int64>(std::ceil(startFrame + (p + 1) * framesPerPixel)) - first);
            Range r{1.0e9f, -1.0e9f};
            for (auto i = std::max<juce::int64>(0, from); i < std::min<juce::int64>(to, region.getNumSamples()); ++i)
                for (int c = 0; c < channels; ++c)
                {
                    const auto v = region.getSample(c, static_cast<int>(i));
                    r.min = std::min(r.min, v);
                    r.max = std::max(r.max, v);
                }
            out[static_cast<size_t>(p)] = r.min > r.max ? Range{0.0f, 0.0f} : r;
        }
        return;
    }

    int level = 0;
    for (int l = 2; l >= 0; --l)
        if (framesPerPixel >= blockSizes[l])
        {
            level = l;
            break;
        }
    const auto& table = levels[level];
    const double block = blockSizes[level];
    for (int p = 0; p < numPixels; ++p)
    {
        const double from = startFrame + p * framesPerPixel;
        const double to = from + framesPerPixel;
        if (from >= static_cast<double>(frames))
            break;
        auto i0 = static_cast<size_t>(std::max(0.0, std::floor(from / block)));
        auto i1 = static_cast<size_t>(std::max(0.0, std::ceil(to / block) - 1.0));
        i0 = std::min(i0, table.size() - 1);
        i1 = std::min(std::max(i1, i0), table.size() - 1);
        Range r = table[i0];
        for (auto i = i0 + 1; i <= i1; ++i)
            r = merge(r, table[i]);
        out[static_cast<size_t>(p)] = r;
    }
}

void WaveformPeaks::readSamples(juce::int64 startFrame, int count, std::vector<float>& out) const
{
    out.assign(static_cast<size_t>(std::max(0, count)), 0.0f);
    juce::AudioBuffer<float> region;
    readRegion(startFrame, count, region);
    for (int i = 0; i < region.getNumSamples(); ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += region.getSample(c, i);
        out[static_cast<size_t>(i)] = sum / static_cast<float>(channels);
    }
}
} // namespace sampler::ui::editor
