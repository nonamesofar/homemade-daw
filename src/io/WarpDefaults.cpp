#include "io/WarpDefaults.h"

namespace sampler::io
{
engine::WarpSettings defaultWarpFor(const model::Source& source, const SoundAnalysis* analysis)
{
    engine::WarpSettings warp;
    if (! source.isValid() || source.category() != model::Category::loop)
        return warp;
    const auto bpm = source.effectiveBpm(analysis != nullptr ? analysis->bpm() : 0.0);
    if (bpm < engine::WarpSettings::minBpm || bpm > engine::WarpSettings::maxBpm)
        return warp;
    warp.on = true;
    warp.mode = engine::WarpMode::beats;
    warp.sourceBpm = bpm;
    return warp;
}

engine::WarpSettings defaultWarpFor(const model::Source& source, AnalysisService& service)
{
    const auto result = source.isValid() ? service.find(source) : nullptr;
    return defaultWarpFor(source, result.get());
}
} // namespace sampler::io
