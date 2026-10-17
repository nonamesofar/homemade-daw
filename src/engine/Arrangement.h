#pragma once

#include "render/BeatsRenderer.h"

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

/** How a warped clip is stretched to the project tempo (clip-warp spec). */
enum class WarpMode
{
    beats,
    tones,
    repitch
};

/** Stored form: "beats", "tones", "repitch". */
juce::String toString(WarpMode);
WarpMode warpModeFromString(const juce::String&, WarpMode fallback = WarpMode::beats);

/**
    The warp settings of a clip (design D1). `sourceBpm` is the sound's tempo as this clip uses it (20..999). With `on`
    false the clip plays at the sound's own speed, as before M4a; mode, tempo and transpose are kept for when it is turned
    on again. Transpose (semitones, -24..+24) is heard in Beats and Tones mode only.
*/
struct WarpSettings
{
    bool on = false;
    WarpMode mode = WarpMode::beats;
    double sourceBpm = 0.0;
    double transpose = 0.0;

    bool operator==(const WarpSettings& o) const
    {
        return on == o.on && mode == o.mode && sourceBpm == o.sourceBpm && transpose == o.transpose;
    }
    bool operator!=(const WarpSettings& o) const { return ! (*this == o); }
    /** Warping is in effect: on, with a usable tempo. */
    bool active() const { return on && sourceBpm >= minBpm && sourceBpm <= maxBpm; }

    static constexpr double minBpm = 20.0;
    static constexpr double maxBpm = 999.0;
    static constexpr double maxTranspose = 24.0;
};

/**
    One clip. Positions in beats; the offset into the sound in seconds of the sound (of the reversed sound when the clip
    is reversed). A warped clip covers `length * 60 / sourceBpm` seconds of its sound; an unwarped one covers its length
    in seconds.
*/
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
    /** Clips made by "Slices to new track" remember where they came from (sampler_originSource / _originSlice / _pitch). */
    juce::String originSourceId;
    int originSlice = -1;        // index of the slice at the time; -1 for a clip that is not a slice copy
    double pitch = 0.0;          // semitones saved by "Slices to new track" (the initial transpose of such a clip)
    bool rendering = false;      // the clip's playback file is not ready yet (reverse, Tones or Beats render): silent meanwhile
    WarpSettings warp;           // transpose already includes the saved slice pitch
    /**
        Loop and fades (design D1, D6). With `loop` the clip repeats [loopStart, loopStart + loopLength) of the sound, and
        `offsetSeconds` is where in the sound it starts (inside that part). The fades are the user's, in seconds, cut to the
        clip's length (the 3 ms declick is not part of them).
    */
    bool loop = false;
    double loopStart = 0.0;      // seconds of the sound
    double loopLength = 0.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;

    double end() const { return start + length; }
    /** Seconds of the sound this clip plays from its offset (without looping). */
    double coveredSeconds() const { return warp.active() ? length * 60.0 / warp.sourceBpm : lengthSeconds; }
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
    /** Set for a copy of a slice: the sound and slice it came from, and the slice's pitch (stored, not applied). */
    juce::String originSourceId;
    int originSlice = -1;
    double pitch = 0.0;
    /**
        Warp settings of the new clip. When `warp.active()`, `lengthSeconds` is still seconds of the sound, and the clip
        covers that part of it: `lengthSeconds * sourceBpm / 60` beats. A command that knows the length in beats passes
        `lengthBeats` instead (> 0 wins).
    */
    WarpSettings warp;
    double lengthBeats = 0.0;
    /** A looped clip: the part of the sound it repeats (`offsetSeconds` lies inside it; `lengthSeconds` is the whole clip). */
    double loopStart = 0.0;
    double loopLength = 0.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
};

/** A Beats render the project needs and does not have yet (see Arrangement::missingBeatsRenders). */
struct BeatsRenderRequest
{
    juce::String clipId;
    juce::String key;
    juce::File decodedWav;
    juce::File destination;
    render::BeatsParams params;
    double startBeat = 0.0;
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

    /**
        Creates a clip on the track (stores `sampler_sourceId`, a relative file path, 3 ms declick fades and the warp
        settings). Empty id on failure.
    */
    juce::String createClip(const juce::String& trackId, const ClipSpec&);
    /** Copies a clip (all its settings) to a track with new bounds. Empty id on failure. */
    juce::String copyClip(const juce::String& clipId, const juce::String& trackId, double startBeats, double lengthSeconds,
                          double offsetSeconds);
    void removeClip(const juce::String& clipId);
    /**
        Moves the clip (also to another track) and sets its length (seconds on the timeline at the current tempo, which
        for a warped clip is its length in bars) and its offset (seconds of the sound).
    */
    void setClipBounds(const juce::String& clipId, const juce::String& trackId, double startBeats, double lengthSeconds,
                       double offsetSeconds);
    void setClipGainDb(const juce::String& clipId, double db);
    void setClipReversed(const juce::String& clipId, bool);

    /**
        Turns warping on or off, or changes its mode or the clip's own tempo, keeping the clip's start and the part of
        the sound it plays: an unwarped clip of 5.33 bars becomes a warped one of 4 bars and back (clip-warp spec). The
        transpose is stored as given (clamped to -24..+24).
    */
    void setClipWarp(const juce::String& clipId, const WarpSettings&);
    void setClipTranspose(const juce::String& clipId, double semitones);

    /**
        Turns the clip's loop on or off (design D6). On: the loop is the part of the sound the clip plays now, and the clip
        can be made longer than that (`setClipBounds`), repeating it. Off: a clip longer than one pass becomes one pass long.
    */
    void setClipLoop(const juce::String& clipId, bool on);
    /** Sets the user fades in seconds, cut so that in + out fit the clip's length. The 3 ms declick is always there. */
    void setClipFades(const juce::String& clipId, double fadeIn, double fadeOut);

    /**
        Sets the tempo. Warped clips keep their start and their length in bars. `rewriteBeats` false (during a tempo drag)
        leaves Beats clips on the file they play, so that no render is asked for at every step of the drag.
    */
    void setTempo(double bpm, bool rewriteBeats = true);
    void setTimeSig(int numerator, int denominator);
    void setLoopRange(LoopRange);

    /** Without the undo manager and without a change of history: used for projects that have no track yet (design D-12). */
    void addDefaultTrackIfEmpty();

    /** Every clip's playback file is ready (reverse and Tones proxies and Beats renders finished). */
    bool allRendersReady() const;

    /**
        The Beats renders that clips point at and that do not exist yet, one per file (design D4). `WarpService` makes
        them and calls `reloadFile` when a file is complete.
    */
    std::vector<BeatsRenderRequest> missingBeatsRenders() const;
    /** Tells Tracktion that a file changed on disk (a finished Beats render) and sends a change message. */
    void reloadFile(const juce::File&);

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
