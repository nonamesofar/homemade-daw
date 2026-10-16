#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace sampler::analysis
{
/** One hit of a sound: where its rise starts (frames at the sound's rate) and how strong it is (0..1, 1 = strongest). */
struct Onset
{
    juce::int64 frame = 0;
    float strength = 0.0f;
};

/**
    Result of the detector: every candidate hit at the most permissive setting (sorted by frame, at least 30 ms
    apart), plus the onset-strength envelope the tempo estimator reuses.
*/
struct OnsetAnalysis
{
    std::vector<Onset> onsets;
    std::vector<float> flux; // spectral flux per hop (before thresholding)
    int hopSize = 256;
};

/** Minimum distance between two hits, in seconds. */
constexpr double minOnsetGapSeconds = 0.030;

/**
    Spectral-flux onset detector (design D2): STFT 1024 / hop 256 with a Hann window, log-magnitude half-wave-rectified
    flux, an adaptive threshold (median of +-8 hops), peak picking with the 30 ms gap, refinement to the start of the
    energy rise and a zero-crossing snap within 1 ms. `mono` is the sound mixed to mono at `sampleRate`.
*/
OnsetAnalysis detectOnsets(const float* mono, juce::int64 numFrames, double sampleRate);

/**
    Keeps the candidates whose strength passes the sensitivity (0..100, higher keeps more). Monotonic: a higher
    sensitivity never gives fewer onsets. O(n), cheap enough to run while a slider moves.
*/
std::vector<Onset> selectTransients(const std::vector<Onset>& onsets, float sensitivity);

/** The strength threshold a sensitivity maps to (exposed for tests and the UI preview). */
float transientThreshold(float sensitivity);

/** Default sensitivity of the Transient mode. */
constexpr float defaultSensitivity = 50.0f;
} // namespace sampler::analysis
