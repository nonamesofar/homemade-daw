#pragma once

#include <memory>

namespace sampler
{
/** Owns the Tracktion engine and the audio device setup. Public interface is Tracktion-free. */
class EngineHost
{
public:
    EngineHost();
    ~EngineHost();

    /** Name of the engine, used by the spike to prove the engine links and starts. */
    const char* engineName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
