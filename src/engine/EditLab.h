#pragma once

#include <juce_core/juce_core.h>

#include <functional>
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
    /**
        Saves atomically (the edit file is always the old or the new version). `beforeCommit` runs once the new version
        is complete in a temporary file, before it replaces the edit file (tests only).
    */
    bool save(const std::function<void(const juce::File& temporaryFile)>& beforeCommit = {});
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

    struct StretchProxy
    {
        bool clipFound = false;
        bool usesProxy = false;  // whether the engine chose to render a stretch proxy at all
        bool finished = false;   // the render job ended within the timeout
        juce::File file;         // the clip's playback file (the proxy when usesProxy)
        double seconds = 0.0;    // length of that file, read back with the engine's decoders
        int wavFilesInTempDir = 0;
    };

    /**
        Sets the clip to the Rubber Band melodic stretch mode at `speedRatio` (playback speed: 0.5 = half speed),
        renders its stretch proxy if the engine wants one, and waits for the render to finish (up to 30 s).
    */
    StretchProxy buildStretchProxy(int clip, double speedRatio);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
