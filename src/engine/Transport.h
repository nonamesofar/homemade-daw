#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace tracktion { inline namespace engine
{
class Edit;
}} // namespace tracktion::engine

namespace sampler
{
class EngineHost;
}

namespace sampler::engine
{
/**
    Plays the project's arrangement (design D-5). Not undoable: play, stop, locate, looping on/off and the metronome
    never touch the undo history. Loop on/off and the metronome are saved in the edit (Tracktion's transport and click
    state). Tracktion-free interface. Message thread only.

    Play is held back until the audio output is ready and, right after the playback context was allocated, issued from
    a short timer (a play in the same message-loop turn stops itself, docs/m0-findings.md). `isPlaying` reports the
    real state, so the Play button only lights up once sound is really running.

    Looping: while looping is on and the playhead is before the loop end, playback rolls into the loop and repeats it;
    when looping is turned on with the playhead after the loop end, playback carries on to the end without jumping back.
*/
class Transport
{
public:
    Transport(tracktion::engine::Edit&, EngineHost&);
    ~Transport();

    void play();
    void stop();
    /** True while sound is really playing. */
    bool isPlaying() const;
    /** Play was asked for and is waiting for the output or the playback context. */
    bool isPlayPending() const;

    /** The playhead in beats (the audible position while playing). */
    double positionBeats() const;
    void locateBeats(double beats);

    bool isLooping() const;
    void setLooping(bool);

    bool isClickOn() const;
    void setClickOn(bool);

    /** Re-applies the loop to the playhead (call after the loop range changed). */
    void loopRangeChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler::engine
