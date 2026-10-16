#pragma once

#include "analysis/OnsetDetector.h"

#include <vector>

namespace sampler::analysis
{
/** At most this many slice points (256 slices). */
constexpr int maxSlicePoints = 255;
/** A slice is at least 5 ms long. */
constexpr double minSliceSeconds = 0.005;

/** The result of choosing slice points. `limitReached` is set when more were wanted than a sound can have. */
struct SlicePoints
{
    std::vector<juce::int64> frames; // increasing, none at 0, every slice at least 5 ms
    bool limitReached = false;
};

/**
    Beat mode (spec "Beat slicing"): a slice point on every `slicesPerBeat`-th of a beat at `bpm`, counted from
    `firstBeatFrame` in both directions. The first slice always starts at the start of the sound, so a first beat
    after the start leaves a short first slice. Points closer than 5 ms to a neighbour or an end are dropped.
*/
SlicePoints beatSlicePoints(juce::int64 totalFrames, double sampleRate, double bpm, juce::int64 firstBeatFrame, double slicesPerBeat);

/**
    Transient mode: a slice point at each of the given hits (already chosen with `selectTransients`). Hits closer than
    5 ms to each other or to an end are thinned out, the stronger one staying. With more than 255 left, the 255 strongest
    stay and `limitReached` is set.
*/
SlicePoints transientSlicePoints(const std::vector<Onset>& hits, juce::int64 totalFrames, double sampleRate);

/**
    The zero crossing nearest to `frame` within 1 ms (a sign change between two samples; the frame after the change),
    or `frame` itself if there is none. `mono` holds the sound mixed to mono; null returns `frame`.
*/
juce::int64 nearestZeroCrossing(const float* mono, juce::int64 numFrames, juce::int64 frame, double sampleRate);

/**
    Where a slice point can go: `wanted` pulled to at least 5 ms from the neighbours (`previous`, `next`: the points
    around it, or 0 and the end of the sound). Returns the nearest allowed frame, or -1 if the gap is too small for
    any point.
*/
juce::int64 clampSlicePoint(juce::int64 wanted, juce::int64 previous, juce::int64 next, double sampleRate);
} // namespace sampler::analysis
