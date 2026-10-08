#pragma once

// Engine-internal: includes Tracktion, so only src/engine/*.cpp may include it.
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace sampler::detail
{
/** The one place an engine is constructed: our UI behaviour (runs render tasks) and no audio input. */
std::unique_ptr<tracktion::engine::Engine> makeEngine(const juce::String& appName);
} // namespace sampler::detail
