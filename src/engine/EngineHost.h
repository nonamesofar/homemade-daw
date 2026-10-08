#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace juce
{
class AudioThumbnailBase;
class Component;
} // namespace juce

namespace sampler
{
/** What a decoder reports about an audio file (no playback involved). */
struct DecodeInfo
{
    bool ok = false;
    int64_t lengthInFrames = 0;
    double sampleRate = 0.0;
    int channels = 0;
    juce::String formatName;
};

/** Owns the Tracktion engine and the audio device setup. Public interface is Tracktion-free. */
class EngineHost
{
public:
    explicit EngineHost(const juce::String& appName = "Sampler");
    ~EngineHost();

    //==============================================================================
    /** Replaces the edit with one track holding the whole file as a clip at time 0. */
    bool loadFile(const juce::File&);
    double loadedLengthSeconds() const;

    void play();
    void stop();
    bool isPlaying() const;
    double positionSeconds() const;
    void setPositionSeconds(double);
    void setTempo(double bpm);

    /** Renders [startSeconds, startSeconds + lengthSeconds) of the edit to a 24-bit WAV, offline. */
    bool renderToWav(const juce::File& dest, double startSeconds, double lengthSeconds, double sampleRate = 44100.0);

    //==============================================================================
    /** Opens the file with every decoder the engine knows and reports what it found. */
    DecodeInfo probeDecode(const juce::File&) const;

    /**
        Silences (true) or restores (false) everything the engine sends to the output device, by taking the master
        volume to zero. Used while capturing the whole device, so the app does not record itself.
    */
    void setOutputMuted(bool muted);
    bool isOutputMuted() const;

    /**
        Waveform thumbnail for a file (Tracktion's SmartThumbnail behind JUCE's AudioThumbnailBase interface), which
        repaints `repaintTarget` as it fills in. Callers need juce_audio_utils to use or delete it.
    */
    std::unique_ptr<juce::AudioThumbnailBase> createThumbnail(const juce::File&, juce::Component& repaintTarget) const;

    /** Opens a reader with the same decoders the engine uses for clips. Null if no decoder accepts the file. */
    std::unique_ptr<juce::AudioFormatReader> openReader(const juce::File&) const;

    /** "Windows Audio" (WASAPI shared) is expected; empty if no device is open. */
    juce::String currentAudioDeviceType() const;
    juce::String currentAudioDeviceName() const;
    int xrunCount() const;

    /** Multi-line description of the engine's wave output devices and the open device (diagnostics). */
    juce::String describeAudioSetup() const;
    juce::String describeTransport() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
