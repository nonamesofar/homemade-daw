#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace sampler
{
/**
    Edit-level operations for the M0 assumption tests and later regression tests. Owns its own engine
    (use a different app name from the real app so settings stay apart) and one audio track.
    Public interface is Tracktion-free.
*/
class EditLab
{
public:
    explicit EditLab(const juce::String& appName = "SamplerTest");
    ~EditLab();

    /** New empty edit with one audio track, saved at once to `editFile` (relative source paths need the file to exist). */
    void newEdit(const juce::File& editFile);
    bool open(const juce::File& editFile);
    bool save();
    void closeEdit();

    //==============================================================================
    /** Adds a clip at a beat position; returns its index. `useRelativePath` stores the source path relative to the edit file. */
    int addClip(const juce::File& audio, double startBeats, double lengthSeconds, bool useRelativePath);
    int numClips() const;
    double clipStartBeats(int clip) const;
    double clipStartSeconds(int clip) const;
    void setTempo(double bpm);

    void setClipProperty(int clip, const juce::String& name, const juce::String& value);
    juce::String clipProperty(int clip, const juce::String& name) const;
    bool clipHasProperty(int clip, const juce::String& name) const;

    /** The path string stored in the edit for the clip's audio source. */
    juce::String clipSourceReference(int clip) const;
    /** True if the clip's source resolves to an existing file. */
    bool clipSourceResolves(int clip) const;

    //==============================================================================
    /** Redirects the engine's derived files (proxies, thumbnails, renders) into a folder. */
    bool setCacheDirectory(const juce::File&);
    juce::File editTempDirectory() const;
    juce::File thumbnailsDirectory() const;

    /** Builds a waveform thumbnail for the clip's source and waits until it is complete. */
    bool buildThumbnail(int clip);
    /** Renders [0, lengthSeconds) of the edit to a WAV, offline. */
    bool renderToWav(const juce::File& dest, double lengthSeconds);

    /** Reverses the clip, which makes the engine render a proxy file, and waits for it. */
    bool buildReverseProxy(int clip);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
