#include "analysis/TempoEstimator.h"

#include <algorithm>
#include <cmath>

namespace sampler::analysis
{
namespace
{
constexpr double envelopeRate = 400.0;           // envelope samples per second
constexpr double maxAnalysedSeconds = 180.0;     // the tempo of a long take is read from its first three minutes
constexpr double combWeights[] = {1.0, 0.6, 0.4, 0.25};
constexpr double loopBonus = 1.35;
constexpr double loopTolerance = 0.005;          // 0.5 %
constexpr int barsChoices[] = {1, 2, 4, 8, 16};

struct Scored
{
    double bpm;
    double score;
    bool isLoop;
};

std::vector<float> buildEnvelope(const std::vector<Onset>& onsets, double sampleRate, double seconds)
{
    const auto length = static_cast<size_t>(std::ceil(seconds * envelopeRate)) + 1;
    std::vector<float> envelope(length, 0.0f);
    constexpr double sigma = 3.0; // 7.5 ms
    constexpr int radius = 9;
    for (const auto& onset : onsets)
    {
        const double position = static_cast<double>(onset.frame) / sampleRate * envelopeRate;
        if (position >= static_cast<double>(length))
            break;
        const int centre = static_cast<int>(std::lround(position));
        for (int d = -radius; d <= radius; ++d)
        {
            const int i = centre + d;
            if (i < 0 || i >= static_cast<int>(length))
                continue;
            const double x = (static_cast<double>(i) - position) / sigma;
            envelope[static_cast<size_t>(i)] += onset.strength * static_cast<float>(std::exp(-0.5 * x * x));
        }
    }
    return envelope;
}

/** Unbiased autocorrelation at integer lags 0..maxLag, normalised by lag 0. */
std::vector<double> autocorrelation(const std::vector<float>& envelope, size_t maxLag)
{
    std::vector<double> ac(maxLag + 1, 0.0);
    const auto n = envelope.size();
    for (size_t lag = 0; lag <= maxLag && lag < n; ++lag)
    {
        double sum = 0.0;
        for (size_t i = 0; i + lag < n; ++i)
            sum += static_cast<double>(envelope[i]) * static_cast<double>(envelope[i + lag]);
        ac[lag] = sum / static_cast<double>(n - lag);
    }
    if (ac[0] > 0.0)
        for (auto& v : ac)
            v /= ac[0];
    return ac;
}

double acAt(const std::vector<double>& ac, double lag)
{
    if (lag < 0.0 || lag >= static_cast<double>(ac.size()) - 1.0)
        return 0.0;
    const auto i = static_cast<size_t>(lag);
    const double f = lag - static_cast<double>(i);
    return ac[i] * (1.0 - f) + ac[i + 1] * f;
}

/** Peak of the autocorrelation near `lag` (within +-3 samples): the comb tolerates a small drift of the hits. */
double acPeakNear(const std::vector<double>& ac, double lag)
{
    double best = 0.0;
    for (double d = -2.0; d <= 2.0; d += 0.5)
        best = std::max(best, acAt(ac, lag + d));
    return best;
}

double combScore(const std::vector<double>& ac, double bpm)
{
    const double period = 60.0 / bpm * envelopeRate;
    double score = 0.0;
    for (int k = 0; k < 4; ++k)
        score += combWeights[k] * acPeakNear(ac, period * (k + 1));
    // A mild preference for tempos around 120 BPM settles octave errors (60 vs 120 vs 240).
    const double octaves = std::log2(bpm / 120.0);
    return score * std::exp(-0.5 * octaves * octaves / (0.9 * 0.9));
}

bool nearExisting(const std::vector<Scored>& list, double bpm)
{
    return std::any_of(list.begin(), list.end(), [bpm](const Scored& s) { return std::abs(s.bpm - bpm) / bpm < 0.02; });
}

juce::int64 findFirstBeat(const std::vector<Onset>& onsets, double bpm, double sampleRate)
{
    if (bpm <= 0.0)
        return 0;
    const auto period = static_cast<juce::int64>(60.0 / bpm * sampleRate);
    // The first strong hit within the first beat, else the start of the sound.
    for (const auto& onset : onsets)
    {
        if (onset.frame >= period)
            break;
        if (onset.strength >= 0.5f)
            return onset.frame;
    }
    return 0;
}
} // namespace

TempoResult estimateTempo(const std::vector<Onset>& onsets, juce::int64 numFrames, double sampleRate)
{
    TempoResult result;
    if (sampleRate <= 0.0 || numFrames <= 0)
        return result;

    const double lengthSeconds = static_cast<double>(numFrames) / sampleRate;
    std::vector<Scored> scored;

    if (onsets.size() >= 3)
    {
        const auto envelope = buildEnvelope(onsets, sampleRate, std::min(lengthSeconds, maxAnalysedSeconds));
        const auto maxLag = static_cast<size_t>(std::ceil(4.0 * 60.0 / minTempo * envelopeRate)) + 4;
        const auto ac = autocorrelation(envelope, maxLag);

        // Scan the tempo range, keep local maxima of the comb score.
        std::vector<double> grid, values;
        for (double bpm = minTempo; bpm <= maxTempo + 1e-9; bpm += 0.25)
        {
            grid.push_back(bpm);
            values.push_back(combScore(ac, bpm));
        }
        std::vector<Scored> peaks;
        for (size_t i = 0; i < grid.size(); ++i)
        {
            const double left = i > 0 ? values[i - 1] : -1.0;
            const double right = i + 1 < grid.size() ? values[i + 1] : -1.0;
            if (values[i] > 0.0 && values[i] >= left && values[i] > right)
                peaks.push_back({grid[i], values[i], false});
        }
        std::sort(peaks.begin(), peaks.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
        for (const auto& peak : peaks)
            if (scored.size() < 6 && ! nearExisting(scored, peak.bpm))
                scored.push_back(peak);

        // Loop-length candidates: scored by the same comb, with a bonus, replacing a nearby grid peak.
        for (const int bars : barsChoices)
        {
            const double bpm = 240.0 * bars / lengthSeconds;
            if (bpm < minLoopTempo * (1.0 - loopTolerance) || bpm > maxLoopTempo * (1.0 + loopTolerance))
                continue;
            const double score = combScore(ac, bpm) * loopBonus;
            auto near = std::find_if(scored.begin(), scored.end(), [bpm](const Scored& s) { return std::abs(s.bpm - bpm) / bpm < 0.02; });
            if (near != scored.end())
            {
                if (score > near->score)
                    *near = {bpm, score, true};
            }
            else if (score > 0.0)
                scored.push_back({bpm, score, true});
        }
    }
    else if (onsets.size() >= 2)
    {
        // Too few hits for autocorrelation: only a loop-length match can say anything, with low confidence.
        for (const int bars : barsChoices)
        {
            const double bpm = 240.0 * bars / lengthSeconds;
            if (bpm >= minLoopTempo && bpm <= maxLoopTempo)
                scored.push_back({bpm, 0.1, true});
        }
    }

    if (scored.empty())
        return result;
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    const size_t keep = std::min<size_t>(3, scored.size());
    double total = 0.0;
    for (size_t i = 0; i < keep; ++i)
        total += scored[i].score;
    for (size_t i = 0; i < keep; ++i)
        result.candidates.push_back({scored[i].bpm, total > 0.0 ? static_cast<float>(scored[i].score / total) : 0.0f});
    result.firstBeatFrame = findFirstBeat(onsets, result.candidates.front().bpm, sampleRate);
    return result;
}
} // namespace sampler::analysis
