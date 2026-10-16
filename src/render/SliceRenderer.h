#pragma once

#include "render/SliceBuffer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace sampler::render
{
/** What a slice sounds like when played from a pad. */
struct SliceParams
{
    float pitchSemitones = 0.0f; // -24..+24, the length is kept
    float gainDb = 0.0f;
    bool reverse = false;
    double sourceRate = 44100.0;
    double outputRate = 44100.0;
};

/**
    Renders one slice into a SliceBuffer (design D6): reverse, Signalsmith pitch shift with the length kept, r8brain
    resampling to the output rate, gain and 1 ms edge fades against clicks. Mono regions are rendered as dual mono.
    Pure function of its inputs; safe to call from several worker threads at once.

    `region` holds the slice's samples at the source rate (1 or 2 channels).
*/
SliceBuffer::Ptr renderSlice(const juce::AudioBuffer<float>& region, const SliceParams& params);

/**
    Reads [startFrame, endFrame) of a decoded WAV and renders it. Null if the file or the region cannot be read. Any thread;
    this is what the pad render scheduler runs.
*/
SliceBuffer::Ptr renderFromFile(const juce::File& wav, juce::int64 startFrame, juce::int64 endFrame, const SliceParams&);

/** Length of the edge fades in milliseconds. */
constexpr double edgeFadeMs = 1.0;
} // namespace sampler::render
