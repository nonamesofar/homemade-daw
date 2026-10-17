#pragma once

#include "engine/Arrangement.h"
#include "io/AnalysisService.h"
#include "model/SamplerState.h"

namespace sampler::io
{
/**
    The warp settings a new clip of this sound starts with (clip-warp spec, "Default warp for new clips"): WARP on in Beats
    mode at the sound's tempo (the user's correction if there is one, else the analysed tempo) when that tempo is known and
    the sound is a loop; otherwise WARP off. One-shots, takes and sounds whose analysis is not ready give WARP off.
    `analysis` may be null (no result yet).
*/
engine::WarpSettings defaultWarpFor(const model::Source&, const SoundAnalysis* analysis);

/** The same with the analysis service asked (cache only; never starts a job). */
engine::WarpSettings defaultWarpFor(const model::Source&, AnalysisService&);
} // namespace sampler::io
