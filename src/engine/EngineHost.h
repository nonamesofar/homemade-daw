#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>

namespace juce
{
class AudioThumbnailBase;
class Component;
} // namespace juce

namespace tracktion { inline namespace engine
{
class Engine;
}} // namespace tracktion::engine

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
    /** `headless` opens no audio device (offline renders, the command-line tool); audio never becomes ready then. */
    explicit EngineHost(const juce::String& appName = "Sampler", bool headless = false);
    ~EngineHost();

    //==============================================================================
    /**
        True once an output device is open and the engine's start-up device scan has finished, so that playing works
        (before that the transport starts and stops itself, see docs/m0-findings.md). Message thread only.
    */
    bool isAudioReady() const;
    bool isHeadless() const;

    /** Called once on the message thread when the audio becomes ready (not called if it already is: check `isAudioReady`). */
    std::function<void()> onAudioReady;

    /** Developer flag `--audio-start-delay`: holds back the ready signal until this long after start-up. */
    void holdAudioReadyFor(int milliseconds);

    /** The engine, for the services inside `src/engine` (ProjectEdit, Audition). Include EngineSetup.h to use it. */
    tracktion::engine::Engine& tracktionEngine() const;

    //==============================================================================
    /** Opens the file with every decoder the engine knows and reports what it found. */
    DecodeInfo probeDecode(const juce::File&) const;

    /**
        Silences (true) or restores (false) everything the engine sends to the output device. Used while capturing the
        whole device, so the app does not record itself. Works below the edit (on the device output), so it never
        changes the edit, its undo history or a saved project, and it stays on when another file is loaded.
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

    /** The open output device's sample rate (0 if none) and buffer size in frames (0 if none). */
    double currentSampleRate() const;
    int currentBufferSize() const;

    /** "Windows Audio" (WASAPI shared) is expected; empty if no device is open. */
    juce::String currentAudioDeviceType() const;
    juce::String currentAudioDeviceName() const;

    /** Multi-line description of the engine's wave output devices and the open device (diagnostics). */
    juce::String describeAudioSetup() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
