#pragma once

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace tracktion { inline namespace engine
{
class Edit;
}} // namespace tracktion::engine

namespace sampler::engine
{
/** One track as the UI and the commands see it (a snapshot, built from the edit on each call). */
struct TrackInfo
{
    juce::String id;
    juce::String name;
    juce::Colour colour;
    double volumeDb = 0.0; // -100 or lower means silence ("-inf")
    double pan = 0.0;      // -1 full left .. +1 full right
    bool mute = false;
    bool solo = false;
    /** What the track records: "system:default", "system:<device id>", "master" or "track:<track id>". */
    juce::String input = "system:default";
};

/** One clip. Positions in beats; the offset into the sound in seconds of the sound (clips play at native speed until M4). */
struct ClipInfo
{
    juce::String id;
    juce::String trackId;
    juce::String sourceId;
    juce::String name;
    double start = 0.0;          // beats
    double length = 0.0;         // beats
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    double offsetSeconds = 0.0;  // into the file that plays (the reversed render when reversed)
    double sourceSeconds = 0.0;  // length of the sound
    double gainDb = 0.0;
    bool reversed = false;
    bool missing = false;        // the sound's decoded file is not there
    bool rendering = false;      // a reversed clip whose render is not ready yet (silent meanwhile)

    double end() const { return start + length; }
};

struct TimeSig
{
    int numerator = 4;
    int denominator = 4;
};

/** A loop range in beats; empty when end <= start. */
struct LoopRange
{
    double start = 0.0;
    double end = 0.0;
    bool isEmpty() const { return end <= start; }
};

/** What a new clip is made of. */
struct ClipSpec
{
    juce::String sourceId;
    juce::String name;
    juce::File file;              // the decoded WAV inside the bundle
    double start = 0.0;           // beats
    double lengthSeconds = 0.0;
    double offsetSeconds = 0.0;
    double gainDb = 0.0;
    bool reversed = false;
};

/**
    The arrangement of the open project (design D-1): tracks, clips, mixer values, tempo, time signature and loop range,
    over the Tracktion edit, with a Tracktion-free interface. The const methods return plain snapshots; the UI only ever
    holds a const reference. The non-const methods are primitive edits recorded into the edit's UndoManager (they do
    not open transactions: CommandService does). Ids are Tracktion's item ids as strings and survive save and reopen.

    Changes (including undo and redo) bump `generation()` and send one coalesced change message.
    Message thread only.
*/
class Arrangement : public juce::ChangeBroadcaster
{
public:
    explicit Arrangement(tracktion::engine::Edit&);
    ~Arrangement() override;

    //==============================================================================
    std::vector<TrackInfo> tracks() const;
    int numTracks() const;
    /** Index of the track in the order shown, or -1. */
    int trackIndex(const juce::String& trackId) const;
    std::optional<TrackInfo> track(const juce::String& trackId) const;

    /** Every clip, track by track (in track order), each track's clips sorted by start. */
    std::vector<ClipInfo> clips() const;
    std::vector<ClipInfo> clipsOnTrack(const juce::String& trackId) const;
    std::optional<ClipInfo> clip(const juce::String& clipId) const;
    int numClips() const;

    double tempo() const;
    TimeSig timeSig() const;
    double beatsPerBar() const { return timeSig().numerator; }
    LoopRange loopRange() const;
    double masterVolumeDb() const;

    /** End of the last clip in beats (0 without clips). */
    double endBeat() const;

    double beatsToSeconds(double beats) const;
    double secondsToBeats(double seconds) const;

    /** Bumped on every change of the edit (not on transport movement). */
    int generation() const { return changeGeneration; }

    /**
        The saved form of the arrangement: the edit XML without the transport's playhead position and other values that
        change without an edit. Used by the undo property tests.
    */
    juce::String serialise() const;

    //==============================================================================
    // Primitive edits (undoable, no transaction of their own).

    /** Inserts an audio track at `index` (clamped); returns its id. */
    juce::String addTrack(int index, const juce::String& name, juce::Colour);
    void removeTrack(const juce::String& trackId);
    void moveTrack(const juce::String& trackId, int newIndex);
    void setTrackName(const juce::String& trackId, const juce::String&);
    void setTrackColour(const juce::String& trackId, juce::Colour);
    void setTrackVolumeDb(const juce::String& trackId, double db);
    void setTrackPan(const juce::String& trackId, double pan);
    void setTrackMute(const juce::String& trackId, bool);
    void setTrackSolo(const juce::String& trackId, bool);
    void setMasterVolumeDb(double db);
    /** Stores the track's recording input (a `sampler_input` property of the track; the default is not stored). */
    void setTrackInput(const juce::String& trackId, const juce::String& input);

    /** Creates a clip on the track (stores `sampler_sourceId`, a relative file path and 3 ms declick fades). Empty id on failure. */
    juce::String createClip(const juce::String& trackId, const ClipSpec&);
    /** Copies a clip (all its settings) to a track with new bounds. Empty id on failure. */
    juce::String copyClip(const juce::String& clipId, const juce::String& trackId, double startBeats, double lengthSeconds,
                          double offsetSeconds);
    void removeClip(const juce::String& clipId);
    /** Moves the clip (also to another track) and sets its length and offset. */
    void setClipBounds(const juce::String& clipId, const juce::String& trackId, double startBeats, double lengthSeconds,
                       double offsetSeconds);
    void setClipGainDb(const juce::String& clipId, double db);
    void setClipReversed(const juce::String& clipId, bool);

    void setTempo(double bpm);
    void setTimeSig(int numerator, int denominator);
    void setLoopRange(LoopRange);

    /** Without the undo manager and without a change of history: used for projects that have no track yet (design D-12). */
    void addDefaultTrackIfEmpty();

    /** Every clip's playback file is ready (reverse renders finished). */
    bool allRendersReady() const;

    static constexpr double minClipSeconds = 0.005;
    static constexpr double silenceDb = -100.0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    int changeGeneration = 0;
};

/** The 12 track colours in order (also used by the UI). */
juce::Colour trackPaletteColour(int index);
constexpr int trackPaletteSize = 12;
} // namespace sampler::engine
