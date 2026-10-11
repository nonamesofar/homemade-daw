#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace tracktion { inline namespace engine
{
class Edit;
}} // namespace tracktion::engine

namespace sampler::engine
{
/** Peak levels since the previous read, per channel, and whether the signal went above 0 dBFS meanwhile. */
struct LevelReading
{
    float peakDb[2] = {-100.0f, -100.0f};
    bool over = false;
};

/**
    Level meters of the arrangement (design D-6): one reader per track, after its volume and pan (Tracktion's level
    meter plugin at the end of the track), and one for the master output. The UI polls them at display rate; the values
    are the peaks since the previous read of the same meter. Tracktion-free interface. Message thread only.
*/
class LevelMeters
{
public:
    explicit LevelMeters(tracktion::engine::Edit&);
    ~LevelMeters();

    LevelReading readTrack(const juce::String& trackId);
    LevelReading readMaster();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler::engine
