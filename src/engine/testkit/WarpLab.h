#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler
{
/**
    Raw Tracktion clip experiments for the warp spike (M4a task 1.1; probe `WarpProbe`). Owns a headless engine and an
    edit with one track. Public interface is Tracktion-free. Positions of clips are in beats, lengths and offsets in
    seconds of the clip (the way the arrangement facade sets them).
*/
class WarpLab
{
public:
    WarpLab();
    ~WarpLab();

    void newEdit(const juce::File& editFile, double bpm);
    void setTempo(double bpm);
    double tempo() const;

    /** A plain clip: auto-tempo off, proxy allowed, speed 1, no fades. Returns its index. */
    int addClip(const juce::File& audio, double startBeats, double lengthSeconds, double offsetSeconds);

    /** Re-pitch: auto-tempo off, no stretching, playback speed `ratio` (2 = twice as fast); position set as given. */
    void setRepitch(int clip, double ratio, double startBeats, double lengthSeconds, double offsetSeconds);
    /** Tones: auto-tempo on, LoopInfo BPM = sourceBpm, Rubber Band melodic, `semitones` pitch change, proxy on. */
    void setTones(int clip, double sourceBpm, double semitones);
    /** Loop range of a non-auto-tempo clip, in seconds of the clip (setLoopRange). */
    void setLoopSeconds(int clip, double startSeconds, double lengthSeconds);
    /** Loop range of an auto-tempo clip in beats of the sound (setLoopRangeBeats). */
    void setLoopBeats(int clip, double startBeats, double lengthBeats);
    void setClipLengthSeconds(int clip, double lengthSeconds);
    /** Clip length in beats (end beat - start beat) as the edit reports it now. */
    double clipLengthBeats(int clip) const;
    double clipLengthSeconds(int clip) const;
    bool clipIsLooping(int clip) const;

    /** Renders [0, seconds) of the edit to a 32-bit float stereo WAV, offline (waits for proxies first). */
    bool renderToWav(const juce::File& dest, double seconds, double sampleRate = 48000.0, int blockSize = 512);

    /** Waits (message loop) until no stretch proxy is being generated and every clip's playback file has content. */
    bool waitForProxies(int timeoutMs = 30000);

    /** Tells the engine that a file changed on disk (AudioFileManager::forceFileUpdate); `alsoNotifyClips` also calls the clip's sourceMediaChanged. */
    void reloadFile(const juce::File& file, bool alsoNotifyClips);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
