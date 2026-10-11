#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler
{
class EngineHost;

/**
    Plays one decoded sound at its original speed and pitch through a preview edit of its own (design D-6), so it never
    touches the project edit, its undo history or its dirty state. One sound at a time: `play` replaces the current one.
    The end of playback is noticed by `poll()`, called from the UI's 30 Hz timer (no audio-thread callbacks).
    Message thread only. In M3 audition moves onto the pad track; this interface stays.
*/
class Audition
{
public:
    explicit Audition(EngineHost&);
    ~Audition();

    /** Starts `decodedWav` from the beginning, labelled with `sourceId`. False if it cannot be played. */
    bool play(const juce::File& decodedWav, const juce::String& sourceId);
    void stop();

    /** Id of the sound now playing, or an empty string. */
    juce::String playingId() const;
    bool isPlaying() const { return playingId().isNotEmpty(); }

    /** Seconds played of the current sound (0 when nothing plays). */
    double positionSeconds() const;

    /** Stops at the end of the sound. Returns true if the playing state changed (the end was reached). */
    bool poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
