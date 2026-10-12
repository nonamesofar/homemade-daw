#pragma once

#include "platform/TakeWriter.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <set>
#include <vector>

namespace sampler::ui
{
/** A take that is growing while a recording runs, as the lanes and headers draw it. */
struct LiveTake
{
    std::vector<juce::String> trackIds;
    const platform::TakeWriter* writer = nullptr;
    /** Timeline seconds where the take starts, or negative while that is not known yet. */
    double startSeconds = -1.0;
    juce::String deviceName; // System takes
    bool isSystem = false;
    /** A System take that has heard nothing for 3 seconds. */
    bool noSignal = false;
    /** Seconds of the timeline the take covers so far (from `startSeconds`). */
    double lengthSeconds = 0.0;
    /** Sound has been written (a System take starts at its first sound). */
    bool started = false;
    /** The recording ended and the take is being finished: the writer is gone. */
    bool finishing = false;
    /** Shown on the growing take: "No sound is coming in (Speakers)" or empty. */
    juce::String warning;
};

/**
    Which tracks are armed for recording (track-recording spec, "Arm a track"). Ids, not undoable, not saved. Plain
    logic with no drawing, so it is unit tested.
*/
class ArmState
{
public:
    bool isArmed(const juce::String& trackId) const { return armedIds.count(trackId) > 0; }
    bool isAnyArmed() const { return ! armedIds.empty(); }

    /** Plain click: this track only. A click on the only armed track disarms it. */
    void arm(const juce::String& trackId)
    {
        const bool wasOnlyOne = armedIds.size() == 1 && isArmed(trackId);
        armedIds.clear();
        if (! wasOnlyOne)
            armedIds.insert(trackId);
    }

    /** Mod-click: adds or removes this track and leaves the others alone. */
    void toggle(const juce::String& trackId)
    {
        if (! armedIds.erase(trackId))
            armedIds.insert(trackId);
    }

    void clear() { armedIds.clear(); }

    /** Forgets tracks that are gone. */
    void prune(const std::vector<juce::String>& existingTrackIds)
    {
        for (auto it = armedIds.begin(); it != armedIds.end();)
            it = std::find(existingTrackIds.begin(), existingTrackIds.end(), *it) == existingTrackIds.end() ? armedIds.erase(it) : std::next(it);
    }

    /** The armed ids in the order of `trackOrder` (tracks that are not in it are left out). */
    std::vector<juce::String> armedInOrder(const std::vector<juce::String>& trackOrder) const
    {
        std::vector<juce::String> result;
        for (const auto& id : trackOrder)
            if (isArmed(id))
                result.push_back(id);
        return result;
    }

private:
    std::set<juce::String> armedIds;
};
} // namespace sampler::ui
