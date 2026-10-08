#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace sampler::io
{
/**
    Writes `data` to `destination` so that the destination is either the old content or the new content,
    never a mix: writes a temporary file next to it, flushes it to disk, then swaps it in.

    `beforeCommit` is called after the temporary file is complete and flushed but before the swap. It exists
    so tests can stop the process at that point; production code passes nothing.
*/
bool writeFileAtomically(const juce::File& destination, const void* data, size_t size,
                         const std::function<void(const juce::File& temporaryFile)>& beforeCommit = {});

/** Same, writing the first half, then calling `midWrite`, then the rest (tests: kill the process while half written). */
bool writeFileAtomicallyInTwoParts(const juce::File& destination, const void* data, size_t size,
                                   const std::function<void(const juce::File& temporaryFile)>& midWrite);
} // namespace sampler::io
