#pragma once

#include <juce_core/juce_core.h>

namespace sampler::platform
{
/**
    Cuts a finished WAV file down to its first `keepFrames` frames in place, without reading the audio: the file is
    truncated and the `data` and `RIFF` sizes (and `ds64` for RF64) are rewritten. Only the layout JUCE's WavAudioFormat
    writes is accepted: a RIFF or RF64 header whose `data` chunk is the last chunk of the file. Anything else is left
    untouched and the result fails. `keepFrames` at or past the end of the data is a no-op that succeeds.

    Used on take files that are not part of a project yet, so the "audio files are never modified" rule holds.
*/
juce::Result trimWavTail(const juce::File& file, juce::int64 keepFrames);
} // namespace sampler::platform
