#pragma once

#include <array>
#include <vector>

namespace sampler::render
{
using StereoChannels = std::array<std::vector<float>, 2>;

/**
    Shifts the pitch of a stereo buffer by `semitones` with Signalsmith Stretch, keeping its length. Processes the whole
    buffer in one go (latency handled); a buffer shorter than the stretcher's block is padded with silence inside and
    trimmed afterwards. Any thread.
*/
void pitchShift(StereoChannels& audio, float semitones, double rate);
} // namespace sampler::render
