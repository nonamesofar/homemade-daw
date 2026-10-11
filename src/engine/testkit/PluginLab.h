#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace juce
{
class AudioProcessorEditor;
}

namespace sampler
{
/**
    M0 plugin-hosting experiments, used by the tests and the spike app. Owns its own engine (use a different app name
    from the real app so settings stay apart). Public interface is Tracktion-free.

    Two layouts of the same multi-output instrument:
      direct: the plugin sits on track 0 and track 0 plays its first output pair.
      rack:   the plugin sits once inside a rack; track i holds an instance of that rack that plays output pair i.
*/
class PluginLab
{
public:
    enum class Layout { direct, rack };

    explicit PluginLab(const juce::String& appName = "SamplerPluginTest");
    ~PluginLab();

    //==============================================================================
    /** New edit saved at once to `editFile`, 120 BPM, with `numTracks` tracks hosting the VST3 at `plugin`. */
    juce::Result newEdit(const juce::File& editFile, const juce::File& plugin, Layout, int numTracks);
    /** Opens a saved edit. `plugin` is only used to find the loaded module for the process counter. */
    juce::Result open(const juce::File& editFile, const juce::File& plugin);
    bool save();
    void closeEdit();

    int numTracks() const;
    juce::String pluginName() const;
    /** Total output channels the host enabled on the plugin (8 for four stereo outputs). */
    int pluginOutputChannels() const;
    /** How many plugin instances the edit holds (1 for both layouts). */
    int numPluginInstances() const;

    //==============================================================================
    /** Sets a plugin parameter by name (normalised 0..1). False if there is no such parameter. */
    bool setParameter(const juce::String& name, float normalised);
    /** Normalised value, or -1 if there is no such parameter. */
    float parameter(const juce::String& name) const;

    /** Editor of the plugin (null if it has none). The caller shows it and deletes it before the lab goes. */
    std::unique_ptr<juce::AudioProcessorEditor> createEditor();

    //==============================================================================
    /** Offline render of [0, seconds) to a 24-bit WAV. `onlyTrack` >= 0 renders just that track. */
    bool renderToWav(const juce::File& dest, double seconds, int onlyTrack = -1, double sampleRate = 44100.0,
                     int blockSize = 512);

    /** Calls of processBlock the plugin has received since the last reset (read from the loaded module). -1 if unknown. */
    juce::int64 processBlockCount() const;
    void resetProcessBlockCount();

    //==============================================================================
    // Real-time playback (needs an audio output device).
    /** Runs the message loop until the engine has an open output device; false on timeout. */
    bool waitForOutputDevice(int timeoutMs);
    void play();
    void stop();
    bool isPlaying() const;
    double positionSeconds() const;
    /** Peak since the last call (linear, 0..1) of what track i sends on, via its level meter. */
    float takeTrackPeak(int track);
    void setMasterGainDb(float db);
    juce::String describeAudioSetup() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
