#pragma once

#include <juce_core/juce_core.h>

namespace sampler::platform
{
/** Result of decoding a whole file through Windows Media Foundation (float, stereo, native rate). */
struct MfDecodeResult
{
    bool ok = false;
    juce::String error;
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 frames = 0;
    double declaredDurationSeconds = 0.0;
    juce::int64 firstAudibleFrame = -1;
};

/** Decodes the file with a Media Foundation source reader (works for AAC in .m4a, unlike JUCE's wrapper). */
MfDecodeResult decodeWithMediaFoundation(const juce::File&);
} // namespace sampler::platform
