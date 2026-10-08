#include "EngineHost.h"

#include <tracktion_engine/tracktion_engine.h>

namespace sampler
{
struct EngineHost::Impl
{
    Impl() : engine("Sampler") {}
    tracktion::engine::Engine engine;
};

EngineHost::EngineHost() : impl(std::make_unique<Impl>()) {}
EngineHost::~EngineHost() = default;

const char* EngineHost::engineName() const { return "Tracktion Engine"; }
} // namespace sampler
