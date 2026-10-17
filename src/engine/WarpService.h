#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace sampler::engine
{
class Arrangement;

/**
    Keeps the Beats renders of a project up to date (design D4). Beats clips play a render of their sound that lives in
    `cache/beats/<key>.wav`; the key comes from the sound, the clip's tempo, the project tempo, reverse and transpose, so
    a file is never changed once written. This service looks for renders that clips point at and that do not exist
    (a new clip, a new tempo, a deleted cache folder), makes them on two worker threads with `render::renderBeatsToFile`,
    and tells the arrangement when a file is complete so that the clip is heard and drawn without "rendering".

    It scans on construction, whenever the arrangement changes and on `scan()`. Queued jobs whose file no clip wants any
    more are skipped (the newest request wins); jobs nearest the playhead go first. Message thread only, except for the
    jobs themselves.
*/
class WarpService : private juce::ChangeListener
{
public:
    /** `playheadBeats` tells where the transport is (renders near it are made first); may be empty. */
    explicit WarpService(Arrangement&, std::function<double()> playheadBeats = {});
    ~WarpService() override;

    /** Looks for missing renders now (also done on every arrangement change). */
    void scan();

    /** Nothing is queued or running, and every finished render has been handed to the arrangement. */
    bool isIdle() const;
    /** Renders that failed (an unreadable sound, a full disk); they are not tried again until the clip changes. */
    int numFailures() const;
    juce::String lastError() const;

    static constexpr int numWorkers = 2;

private:
    struct Impl;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler::engine
