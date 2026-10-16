#include "analysis/Slicer.h"

#include <algorithm>
#include <cmath>

namespace sampler::analysis
{
namespace
{
juce::int64 minFramesFor(double sampleRate)
{
    return std::max<juce::int64>(1, static_cast<juce::int64>(std::ceil(sampleRate * minSliceSeconds)));
}
} // namespace

SlicePoints beatSlicePoints(juce::int64 totalFrames, double sampleRate, double bpm, juce::int64 firstBeatFrame, double slicesPerBeat)
{
    SlicePoints result;
    if (totalFrames <= 0 || sampleRate <= 0.0 || bpm <= 0.0 || slicesPerBeat <= 0.0)
        return result;
    const double step = sampleRate * 60.0 / bpm / slicesPerBeat;
    if (step < 1.0)
        return result;
    const auto minFrames = minFramesFor(sampleRate);

    const auto first = static_cast<double>(firstBeatFrame);
    const auto kFrom = static_cast<juce::int64>(std::ceil((1.0 - first) / step));
    const auto kTo = static_cast<juce::int64>(std::floor((static_cast<double>(totalFrames) - 1.0 - first) / step));
    juce::int64 previous = 0;
    for (juce::int64 k = kFrom; k <= kTo; ++k)
    {
        const auto frame = static_cast<juce::int64>(std::llround(first + static_cast<double>(k) * step));
        if (frame <= 0 || frame >= totalFrames || frame - previous < minFrames)
            continue;
        result.frames.push_back(frame);
        previous = frame;
    }
    while (! result.frames.empty() && totalFrames - result.frames.back() < minFrames)
        result.frames.pop_back();
    if (static_cast<int>(result.frames.size()) > maxSlicePoints)
    {
        result.frames.resize(maxSlicePoints);
        result.limitReached = true;
    }
    return result;
}

SlicePoints transientSlicePoints(const std::vector<Onset>& hits, juce::int64 totalFrames, double sampleRate)
{
    SlicePoints result;
    if (totalFrames <= 0 || sampleRate <= 0.0)
        return result;
    const auto minFrames = minFramesFor(sampleRate);

    std::vector<Onset> kept;
    for (const auto& hit : hits)
    {
        if (hit.frame < minFrames || totalFrames - hit.frame < minFrames)
            continue;
        // Too close to the previous point: the stronger of the two stays.
        auto candidate = hit;
        bool dropped = false;
        while (! kept.empty() && candidate.frame - kept.back().frame < minFrames)
        {
            if (candidate.strength > kept.back().strength)
                kept.pop_back();
            else
            {
                dropped = true;
                break;
            }
        }
        if (! dropped)
            kept.push_back(candidate);
    }

    if (static_cast<int>(kept.size()) > maxSlicePoints)
    {
        std::stable_sort(kept.begin(), kept.end(), [](const Onset& a, const Onset& b) { return a.strength > b.strength; });
        kept.resize(maxSlicePoints);
        std::sort(kept.begin(), kept.end(), [](const Onset& a, const Onset& b) { return a.frame < b.frame; });
        result.limitReached = true;
    }
    for (const auto& hit : kept)
        result.frames.push_back(hit.frame);
    return result;
}

juce::int64 nearestZeroCrossing(const float* mono, juce::int64 numFrames, juce::int64 frame, double sampleRate)
{
    if (mono == nullptr || numFrames < 2)
        return frame;
    const auto radius = std::max<juce::int64>(1, static_cast<juce::int64>(sampleRate * 0.001));
    const auto crossing = [&](juce::int64 i) {
        return i > 0 && i < numFrames && ((mono[i - 1] <= 0.0f && mono[i] > 0.0f) || (mono[i - 1] >= 0.0f && mono[i] < 0.0f));
    };
    for (juce::int64 d = 0; d <= radius; ++d)
    {
        if (crossing(frame - d))
            return frame - d;
        if (d > 0 && crossing(frame + d))
            return frame + d;
    }
    return frame;
}

juce::int64 clampSlicePoint(juce::int64 wanted, juce::int64 previous, juce::int64 next, double sampleRate)
{
    const auto minFrames = minFramesFor(sampleRate);
    const auto lowest = previous + minFrames;
    const auto highest = next - minFrames;
    if (lowest > highest)
        return -1;
    return std::clamp(wanted, lowest, highest);
}
} // namespace sampler::analysis
