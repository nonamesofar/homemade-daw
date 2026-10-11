#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler
{
class EngineHost;

/**
    Plays or renders one audio file through a single-track edit of its own on the given engine (the M0 "player").
    For tests and probes only: the app plays through the project edit and Audition. Public interface is Tracktion-free.
    Must be destroyed before the EngineHost.
*/
class FilePlayer
{
public:
    explicit FilePlayer(EngineHost&);
    ~FilePlayer();

    /** Replaces the edit with one track holding the whole file as a clip at time 0. */
    bool loadFile(const juce::File&);
    double loadedLengthSeconds() const;

    void play();
    void stop();
    bool isPlaying() const;
    double positionSeconds() const;
    void setTempo(double bpm);

    /** Renders [startSeconds, startSeconds + lengthSeconds) of the edit to a 24-bit WAV, offline. */
    bool renderToWav(const juce::File& dest, double startSeconds, double lengthSeconds, double sampleRate = 44100.0);

    /** Master volume of the current edit in dB (0 if there is no edit). */
    float masterVolumeDb() const;
    /** The current edit's state as XML (empty if there is no edit). */
    juce::String editStateXml() const;
    /** The undo history: transaction names and the number of actions not yet in a transaction. */
    juce::String undoHistoryDescription() const;
    juce::String describeTransport() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
