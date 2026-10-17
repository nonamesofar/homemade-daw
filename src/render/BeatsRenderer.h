#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace sampler::render
{
/** What a Beats render of a whole sound depends on. The region, trim, split and loop of a clip are not part of it. */
struct BeatsParams
{
    double sourceBpm = 120.0;
    double projectBpm = 120.0;
    bool reverse = false;
    float transposeSemitones = 0.0f; // -24..+24, the length is kept

    /** How much shorter the render is than the sound: sourceBpm / projectBpm (0.75 for a 90 BPM sound at 120 BPM). */
    double ratio() const { return sourceBpm / projectBpm; }
};

/** Bumped whenever a render would sound different for the same inputs, so that old cache files are not reused. */
constexpr int beatsRendererVersion = 1;

/** Sensitivity of the onset detection that cuts the sound into pieces (fixed, so renders are reproducible). */
constexpr float beatsOnsetSensitivity = 60.0f;

/**
    The weakest spectral flux (after the first hop) that counts as a hit; a drum hit measures 10 or more, a sustained pad
    below 2 (OnsetAnalysis::flux units). A sound whose strongest flux is below this, or with fewer than two hits, has no hits.
*/
constexpr float beatsMinHitFlux = 3.0f;

/** A sound with no hits is cut on a grid of 1/16 notes (four cuts per beat) at the sound's own tempo. */
constexpr int beatsGridCutsPerBeat = 4;

/** Fade-out at the end of a piece that is cut short because the tempo got faster. */
constexpr double beatsCutFadeMs = 3.0;
/** Fade-out at the end of a piece that is followed by a gap because the tempo got slower. */
constexpr double beatsGapFadeMs = 10.0;

/**
    Where the sound is cut: frame 0 plus the start of every hit (onsets at `beatsOnsetSensitivity`), or the 1/16 grid at
    `sourceBpm` when there is no hit (see `beatsMinHitFlux`). A first hit within 5 ms of the start replaces frame 0. `mono` is at `rate`.
*/
std::vector<juce::int64> beatsCutPoints(const float* mono, juce::int64 numFrames, double rate, double sourceBpm);

/**
    The Beats render of a whole sound (design D3): reverse first when asked, cut at the hits, copy each piece unstretched
    to `ratio * start`; a piece that no longer fits its slot is cut at the next piece with a 3 ms fade-out, a piece
    followed by a gap gets a 10 ms fade-out, and with a transpose each piece is pitch-shifted (length kept) before it is
    placed. The result has the channel count (1 or 2) of the sound and `round(frames * ratio)` frames. Pure function of
    its inputs; any thread. An empty buffer means the parameters cannot be rendered (tempo out of range, no audio, an
    output longer than 30 minutes at 192 kHz) or that `shouldStop`, polled once per piece, returned true.
*/
juce::AudioBuffer<float> renderBeats(const juce::AudioBuffer<float>& sound, double rate, const BeatsParams&,
                                    const std::function<bool()>& shouldStop = {});

/**
    Reads the decoded WAV, renders it and writes `destination` as a 32-bit float WAV through a temporary file in the same
    folder and one rename, so the file is either missing or complete. An existing destination is left as it is (the name
    holds the key). Returns false with `error` set on failure, including a failed allocation. When `shouldStop` returns true
    the render is abandoned and nothing is written. Any thread.
*/
bool renderBeatsToFile(const juce::File& decodedWav, const juce::File& destination, const BeatsParams&, juce::String& error,
                       const std::function<bool()>& shouldStop = {});

/**
    The cache name of a render: a hash of the sound's content key, the two tempos (4 decimals), reverse, transpose and
    `beatsRendererVersion`. 16 lower-case hex characters.
*/
juce::String beatsRenderKey(const juce::String& soundContentKey, const BeatsParams&);
} // namespace sampler::render
