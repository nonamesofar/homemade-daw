#pragma once

#include "analysis/OnsetDetector.h"

#include <vector>

namespace sampler::analysis
{
struct TempoCandidate
{
    double bpm = 0.0;
    float confidence = 0.0f; // 0..1, share of this candidate in the best three
};

struct TempoResult
{
    std::vector<TempoCandidate> candidates; // best first, at most three; empty when there is nothing to go on
    juce::int64 firstBeatFrame = 0;

    bool valid() const { return ! candidates.empty(); }
    double bpm() const { return candidates.empty() ? 0.0 : candidates.front().bpm; }
};

constexpr double minTempo = 60.0;
constexpr double maxTempo = 200.0;
/** Range of tempos a loop-length match may give (spec: 70 to 180 BPM). */
constexpr double minLoopTempo = 70.0;
constexpr double maxLoopTempo = 180.0;

/**
    Tempo estimate from the hits of a sound (design D2): the loop-length heuristic (a sound that is 1, 2, 4, 8 or 16 bars
    long at a tempo within 70..180 BPM, 0.5%) combined with the autocorrelation of the onset-strength envelope over
    60..200 BPM with a comb score and a penalty for octave errors. A loop-length match only wins when the hits support
    it, so a 7 s pattern is not mistaken for 4 bars at 137.14 BPM.
    The first beat is the first strong hit within the first beat period, else the start of the sound.
*/
TempoResult estimateTempo(const std::vector<Onset>& onsets, juce::int64 numFrames, double sampleRate);
} // namespace sampler::analysis
