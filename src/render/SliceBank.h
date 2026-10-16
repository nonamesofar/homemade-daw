#pragma once

#include "render/SliceBuffer.h"

#include <array>
#include <cstdint>

namespace sampler::render
{
/** How a slice's pad plays: to the end whatever the key does, while the key is held, or repeating while held. */
enum class PlayMode : uint8_t
{
    oneShot,
    gate,
    loop
};

/** At most 256 slices per sound (spec "Slices cover the whole sound"). */
constexpr int maxSlices = 256;

/**
    Everything the pad player can play right now: one rendered buffer and one play mode per slice. Published to the
    audio thread as a whole (a single pointer exchange), so a bank is never half-updated. Reference counted; the audio
    thread hands its references back through a garbage FIFO so the last release happens off the audio thread.
*/
class SliceBank : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SliceBank>;

    std::array<SliceBuffer::Ptr, maxSlices> buffers;
    std::array<PlayMode, maxSlices> modes{};
};
} // namespace sampler::render
