#pragma once

// Engine-internal: includes Tracktion, so only src/engine/*.cpp may include it.
#include "TakeRecorder.h"

namespace tracktion { inline namespace engine
{
class Edit;
}} // namespace tracktion::engine

namespace sampler::engine
{
/**
    The TakeRecorder of one edit (design D2, D3): a tap plugin at the end of a track's plugin list or of the master
    plugin list. The tap lives in the edit's state only while the take runs, is added and removed without the undo
    manager, and is left out of every saved file (`saveEditAtomically`).
*/
class EditTakeRecorder final : public TakeRecorder
{
public:
    explicit EditTakeRecorder(tracktion::engine::Edit&);

    std::unique_ptr<TakeTap> tapTrack(const juce::String& trackId, const platform::TakeOptions&, juce::String& error) override;
    std::unique_ptr<TakeTap> tapMaster(const platform::TakeOptions&, juce::String& error) override;

private:
    tracktion::engine::Edit& edit;
};
} // namespace sampler::engine
