#pragma once

#include <juce_core/juce_core.h>

namespace sampler
{
class ProjectEdit;
}

namespace sampler::engine
{
struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    /** How long to wait for reverse renders before giving up. */
    int proxyTimeoutMs = 30000;
};

/**
    Renders the arrangement offline to a 32-bit float stereo WAV, from bar 1 to the end of the last clip, with the
    master volume and without the metronome (design D-11). Waits for Tracktion's reverse renders first. Works with a
    headless engine (no audio device). M4 export reuses it. Message thread only.
    Returns false with `error` set (a sentence for the user) on failure; no file is left behind then.
*/
bool renderToWav(ProjectEdit&, const juce::File& destination, const RenderOptions&, juce::String& error);
} // namespace sampler::engine
