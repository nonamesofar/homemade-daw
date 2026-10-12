#pragma once

#include <juce_core/juce_core.h>

namespace sampler::platform
{
#if JUCE_DEBUG
/**
    Marks the code that runs on a real-time thread (AGENTS.md section 5.1). Debug builds count the nesting per thread;
    a test that replaces the global `operator new` can then assert that nothing allocates inside our audio code
    (tests/arrange/take_tap.test.cpp). Release builds compile it to nothing.
*/
inline thread_local int realtimeDepth = 0;

struct RealtimeScope
{
    RealtimeScope() { ++realtimeDepth; }
    ~RealtimeScope() { --realtimeDepth; }
    RealtimeScope(const RealtimeScope&) = delete;
    RealtimeScope& operator=(const RealtimeScope&) = delete;
};
inline bool inRealtimeScope() { return realtimeDepth > 0; }
#else
struct RealtimeScope
{
    RealtimeScope() {} // not trivial, so that a scope variable is never reported as unused
};
inline bool inRealtimeScope() { return false; }
#endif
} // namespace sampler::platform
