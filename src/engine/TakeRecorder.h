#pragma once

#include "platform/TakeWriter.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler::engine
{
/**
    A take being recorded from inside the engine (a track's output or the master mix): the engine side pushes blocks
    into a TakeWriter from the audio thread, sample-aligned with the transport. Tracktion-free interface.
*/
class TakeTap
{
public:
    virtual ~TakeTap() = default;

    /** The live side of the take (levels, waveform buckets), readable from any thread. */
    virtual const platform::TakeWriter& take() const = 0;
    /** Timeline position in seconds of the first recorded frame; negative until the first block arrived. */
    virtual double firstFrameSeconds() const = 0;
    /** The tap is in the engine's graph and the take file is open (blocks are being recorded). */
    virtual bool isReady() const = 0;
    /** The audio device changed its sample rate during the take: the take stopped growing (what it has is kept). */
    virtual bool endedByRateChange() const { return false; }
    /** Stops listening to the engine (message thread, fast). After it no more blocks arrive. */
    virtual void detach() = 0;
    /** Detaches if needed, lets the writer drain and closes the file (may block: call it off the message thread). */
    virtual platform::TakeResult finish() = 0;
};

/** Opens taps on the open project's outputs (design D2, D3). */
class TakeRecorder
{
public:
    virtual ~TakeRecorder() = default;

    /** The output of one track, after its volume and pan. Null with `error` set if it cannot be tapped. */
    virtual std::unique_ptr<TakeTap> tapTrack(const juce::String& trackId, const platform::TakeOptions&, juce::String& error) = 0;
    /** The master output of the edit. */
    virtual std::unique_ptr<TakeTap> tapMaster(const platform::TakeOptions&, juce::String& error) = 0;
};
} // namespace sampler::engine
