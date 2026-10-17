#include "analysis/OnsetDetector.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace sampler::analysis
{
namespace
{
constexpr int fftOrder = 10;
constexpr int fftSize = 1 << fftOrder;
constexpr int hop = 256;
constexpr int medianRadius = 8;

/**
    Band edges in FFT bins (1024-point FFT): single bins at the bottom so a kick drum counts as much as a hat, wider
    bands towards the top. Each band contributes its own log-magnitude change to the flux.
*/
constexpr int bandEdges[] = {1,  2,  3,  4,  5,  6,  8,  10, 12,  15,  18,  22,  27,
                             33, 40, 50, 62, 77, 96, 120, 150, 190, 240, 300, 380, 513};
constexpr int numBands = static_cast<int>(sizeof(bandEdges) / sizeof(bandEdges[0])) - 1;

/**
    Half-wave-rectified spectral flux of the band-wise log-compressed magnitudes, one value per hop. Hop 0 is compared
    with silence, so a sound that starts loud has an onset at the very start.
*/
std::vector<float> computeFlux(const float* mono, juce::int64 numFrames)
{
    const auto numHops = numFrames >= fftSize ? static_cast<size_t>((numFrames - fftSize) / hop + 1) : size_t{0};
    std::vector<float> flux(numHops, 0.0f);
    if (numHops == 0)
        return flux;

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(static_cast<size_t>(fftSize), juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> frame(static_cast<size_t>(fftSize) * 2);
    std::vector<float> previous(static_cast<size_t>(numBands), 0.0f);

    for (size_t h = 0; h < numHops; ++h)
    {
        std::fill(frame.begin(), frame.end(), 0.0f);
        std::copy_n(mono + static_cast<juce::int64>(h) * hop, fftSize, frame.begin());
        window.multiplyWithWindowingTable(frame.data(), static_cast<size_t>(fftSize));
        fft.performFrequencyOnlyForwardTransform(frame.data(), true); // bins 0..size/2 only: all the bands use

        float sum = 0.0f;
        for (int band = 0; band < numBands; ++band)
        {
            float energy = 0.0f;
            for (int bin = bandEdges[band]; bin < bandEdges[band + 1]; ++bin)
                energy += frame[static_cast<size_t>(bin)];
            energy /= static_cast<float>(bandEdges[band + 1] - bandEdges[band]);
            const float magnitude = std::log1p(100.0f * energy / static_cast<float>(fftSize));
            sum += std::max(0.0f, magnitude - previous[static_cast<size_t>(band)]);
            previous[static_cast<size_t>(band)] = magnitude;
        }
        flux[h] = sum;
    }
    return flux;
}

float median(std::vector<float>& values)
{
    const auto mid = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

struct Candidate
{
    size_t hopIndex;
    float strength;
};

/** Adaptive threshold: median of the neighbourhood scaled up, plus a floor relative to the loudest flux. */
std::vector<Candidate> pickPeaks(const std::vector<float>& flux, double sampleRate)
{
    std::vector<Candidate> candidates;
    if (flux.size() < 3)
        return candidates;

    const float maxFlux = *std::max_element(flux.begin(), flux.end());
    if (maxFlux <= 0.0f)
        return candidates;
    const float floorValue = 0.04f * maxFlux;

    std::vector<float> window;
    for (size_t i = 0; i < flux.size(); ++i)
    {
        const float before = i > 0 ? flux[i - 1] : 0.0f;
        const float after = i + 1 < flux.size() ? flux[i + 1] : 0.0f;
        if (! (flux[i] > before && flux[i] >= after))
            continue;
        window.clear();
        const auto from = i >= static_cast<size_t>(medianRadius) ? i - medianRadius : size_t{0};
        const auto to = std::min(flux.size() - 1, i + medianRadius);
        for (auto j = from; j <= to; ++j)
            window.push_back(flux[j]);
        const float threshold = 1.5f * median(window) + floorValue;
        if (flux[i] > threshold)
            candidates.push_back({i, flux[i]});
    }

    // 30 ms gap: of two peaks closer than that, the stronger one stays.
    const auto minGapHops = static_cast<size_t>(std::ceil(minOnsetGapSeconds * sampleRate / hop));
    std::vector<Candidate> kept;
    for (const auto& c : candidates)
    {
        if (! kept.empty() && c.hopIndex - kept.back().hopIndex < minGapHops)
        {
            if (c.strength > kept.back().strength)
                kept.back() = c;
            continue;
        }
        kept.push_back(c);
    }
    return kept;
}

/**
    The flux peak of a hop marks the moment the hit sits in the middle of the analysis window. The hit really starts
    where the energy begins to rise before that: search the short-time energy around the estimate.
*/
juce::int64 refineToRise(const float* mono, juce::int64 numFrames, juce::int64 estimate, double sampleRate)
{
    const int block = std::max(8, static_cast<int>(sampleRate * 0.0005)); // 0.5 ms
    const juce::int64 searchBefore = static_cast<juce::int64>(sampleRate * 0.025);
    const juce::int64 searchAfter = static_cast<juce::int64>(sampleRate * 0.010);
    const auto from = std::max<juce::int64>(0, estimate - searchBefore);
    const auto to = std::min<juce::int64>(numFrames, estimate + searchAfter);
    const auto blocks = static_cast<size_t>((to - from) / block);
    if (blocks < 4)
        return estimate;

    std::vector<float> energy(blocks);
    for (size_t b = 0; b < blocks; ++b)
    {
        double sum = 0.0;
        for (int i = 0; i < block; ++i)
        {
            const double s = mono[from + static_cast<juce::int64>(b) * block + i];
            sum += s * s;
        }
        energy[b] = static_cast<float>(std::sqrt(sum / block));
    }

    // Steepest rise inside the window; the quietest block before it is the floor (the end of a previous hit's tail).
    size_t steepest = 1;
    float biggestStep = 0.0f;
    for (size_t b = 1; b < blocks; ++b)
        if (energy[b] - energy[b - 1] > biggestStep)
        {
            biggestStep = energy[b] - energy[b - 1];
            steepest = b;
        }
    if (biggestStep <= 0.0f)
        return estimate;

    const float peak = *std::max_element(energy.begin() + static_cast<std::ptrdiff_t>(steepest), energy.end());
    const auto floorBlock = static_cast<size_t>(std::min_element(energy.begin(), energy.begin() + static_cast<std::ptrdiff_t>(steepest)) - energy.begin());
    const float floorLevel = energy[floorBlock];
    const float threshold = floorLevel + 0.02f * (peak - floorLevel);

    // Walk back down the rise while the level at least doubles from one block to the next: a hit's attack is that steep,
    // the slow swell of a low note in a previous hit's tail is not. The block before the rise is where the hit starts.
    size_t start = steepest;
    while (start > floorBlock && energy[start - 1] > threshold && energy[start - 1] < 0.5f * energy[start])
        --start;
    if (start > 0)
        --start;
    return from + static_cast<juce::int64>(start) * block;
}

/** Snaps to a zero crossing within 1 ms, looking back first so the point never moves into the hit (kept if none). */
juce::int64 snapToZeroCrossing(const float* mono, juce::int64 numFrames, juce::int64 position, double sampleRate)
{
    const auto radius = std::max<juce::int64>(1, static_cast<juce::int64>(sampleRate * 0.001));
    const auto crossing = [&](juce::int64 i) {
        return i > 0 && i < numFrames && ((mono[i - 1] <= 0.0f && mono[i] > 0.0f) || (mono[i - 1] >= 0.0f && mono[i] < 0.0f));
    };
    for (juce::int64 d = 0; d <= radius; ++d)
        if (crossing(position - d))
            return position - d;
    for (juce::int64 d = 1; d <= radius; ++d)
        if (crossing(position + d))
            return position + d;
    return position;
}
} // namespace

OnsetAnalysis detectOnsets(const float* mono, juce::int64 numFrames, double sampleRate)
{
    OnsetAnalysis result;
    result.hopSize = hop;
    result.flux = computeFlux(mono, numFrames);

    const auto candidates = pickPeaks(result.flux, sampleRate);
    // The first hop is compared with silence and would dwarf every other hit: it does not set the scale.
    float maxStrength = 0.0f;
    for (const auto& c : candidates)
        if (c.hopIndex > 0)
            maxStrength = std::max(maxStrength, c.strength);
    if (maxStrength <= 0.0f)
        for (const auto& c : candidates)
            maxStrength = std::max(maxStrength, c.strength);

    for (const auto& c : candidates)
    {
        const juce::int64 estimate = static_cast<juce::int64>(c.hopIndex) * hop + fftSize / 2;
        auto frame = refineToRise(mono, numFrames, estimate, sampleRate);
        frame = snapToZeroCrossing(mono, numFrames, frame, sampleRate);
        const float strength = maxStrength > 0.0f ? std::min(1.0f, c.strength / maxStrength) : 0.0f;
        const auto minGap = static_cast<juce::int64>(minOnsetGapSeconds * sampleRate);
        if (! result.onsets.empty() && frame - result.onsets.back().frame < minGap)
        {
            // Too close to the last one (refining moved them together): the stronger of the two stays, as in pickPeaks.
            const bool roomBefore = result.onsets.size() < 2 || frame - result.onsets[result.onsets.size() - 2].frame >= minGap;
            if (strength > result.onsets.back().strength && roomBefore)
                result.onsets.back() = {frame, strength};
            continue;
        }
        result.onsets.push_back({frame, strength});
    }
    return result;
}

float transientThreshold(float sensitivity)
{
    const float s = juce::jlimit(0.0f, 100.0f, sensitivity) / 100.0f;
    const float keep = 1.0f - s;
    return keep * keep * keep;
}

std::vector<Onset> selectTransients(const std::vector<Onset>& onsets, float sensitivity)
{
    const float threshold = transientThreshold(sensitivity);
    std::vector<Onset> selected;
    for (const auto& onset : onsets)
        if (onset.strength >= threshold)
            selected.push_back(onset);
    return selected;
}
} // namespace sampler::analysis
