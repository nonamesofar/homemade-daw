#pragma once

#include "CommandService.h"
#include "EditPlanner.h"
#include "engine/Arrangement.h"
#include "model/SamplerState.h"

#include <optional>
#include <vector>

/**
    The arrangement commands (design D-3). Each is one undo step named as in the undo-redo spec, and applies the
    no-overlap rule through EditPlanner in the same step. All need `ModelContext::arrangement`.
    Commands that create or move clips report the resulting clip ids in `resultIds` after they ran (for the selection).
*/
namespace sampler::commands
{
/** Clips the command produced or moved, for the selection. */
class ArrangeCommand : public Command
{
public:
    const std::vector<juce::String>& resultIds() const { return results; }

protected:
    std::vector<juce::String> results;
};

/** "Audio N" with the smallest number not in use, and the first palette colour no track uses. */
juce::String nextTrackName(const std::vector<engine::TrackInfo>&);
juce::Colour nextTrackColour(const std::vector<engine::TrackInfo>&);

/** "Move clip" / "Move 3 clips". */
juce::String clipCountName(const juce::String& verb, size_t count);

/**
    Where dropped sounds go: the track with `trackId`, or a new track at the end when `newTrack`. A pending import may
    outlive its track (deleted or moved meanwhile): the id is resolved when the clips are placed, and a track that is
    gone means a new track. Without an id (code that has none), `trackIndex` is used.
*/
struct PlacementTarget
{
    int trackIndex = 0;
    bool newTrack = false;
    double beat = 0.0;
    juce::String trackId;
};

//==============================================================================
/** Places sounds one after another from the target beat ("Add clip" / "Add 2 clips"). */
class CreateClipsFromSources final : public ArrangeCommand
{
public:
    CreateClipsFromSources(std::vector<juce::String> sourceIds, PlacementTarget target) : sources(std::move(sourceIds)), where(target) {}
    juce::String name() const override { return clipCountName("Add", sources.size()); }
    void perform(ModelContext&) override;

    /** Also used by ImportAndPlace. */
    static std::vector<juce::String> place(ModelContext&, const std::vector<juce::String>& sourceIds, PlacementTarget);

private:
    std::vector<juce::String> sources;
    PlacementTarget where;
};

/**
    "Slices to new track": a new track at the end, named after the sound, with one clip per slice laid back to back from
    `startBeat` (design D7, sample-editor spec "Slices to new track"). Each clip plays its slice's part of the sound with
    the slice's gain and direction, is named "<sound> <n>" and remembers where it came from; later edits of the slice set do
    not change it. A reversed slice plays the reversed render of its region (the clip's offset is into the reversed file).
    `resultIds` are the new clips in slice order; `trackId()` is the new track.
*/
class SlicesToTrack final : public ArrangeCommand
{
public:
    SlicesToTrack(juce::String sourceId, double startAtBeat) : id(std::move(sourceId)), startBeat(startAtBeat) {}
    juce::String name() const override { return "Slices to new track"; }
    void perform(ModelContext&) override;
    const juce::String& trackId() const { return newTrackId; }

private:
    juce::String id;
    double startBeat;
    juce::String newTrackId;
};

/** Records imported sounds and places them, as one step (named like the import). */
class ImportAndPlace final : public ArrangeCommand
{
public:
    /** `placeIds` are the sounds to place in order (new ones and ones that were already in the project). */
    ImportAndPlace(std::vector<model::SourceData> sources, std::vector<juce::String> placeIds, PlacementTarget target)
        : items(std::move(sources)), toPlace(std::move(placeIds)), where(target)
    {
    }
    juce::String name() const override;
    void perform(ModelContext&) override;

private:
    std::vector<model::SourceData> items;
    std::vector<juce::String> toPlace;
    PlacementTarget where;
};

struct ClipMove
{
    juce::String clipId;
    int trackIndex = 0;
    double start = 0.0;
};

class MoveClips final : public ArrangeCommand
{
public:
    explicit MoveClips(std::vector<ClipMove> clipMoves) : moves(std::move(clipMoves)) {}
    juce::String name() const override { return clipCountName("Move", moves.size()); }
    void perform(ModelContext&) override;

private:
    std::vector<ClipMove> moves;
};

/** Sets a clip's start and end (either may change); clamped to the sound and to `minLength` beats. */
class TrimClip final : public ArrangeCommand
{
public:
    TrimClip(juce::String id, double newStart, double newEnd, double minLengthBeats)
        : clipId(std::move(id)), start(newStart), end(newEnd), minLength(minLengthBeats)
    {
    }
    juce::String name() const override { return "Trim clip"; }
    void perform(ModelContext&) override;

private:
    juce::String clipId;
    double start, end, minLength;
};

class SplitClips final : public ArrangeCommand
{
public:
    SplitClips(std::vector<juce::String> ids, double atBeat) : clipIds(std::move(ids)), beat(atBeat) {}
    juce::String name() const override { return clipCountName("Split", clipIds.size()); }
    void perform(ModelContext&) override;

private:
    std::vector<juce::String> clipIds;
    double beat;
};

/** Copies right after the end of the selection, on the same tracks. */
class DuplicateClips final : public ArrangeCommand
{
public:
    explicit DuplicateClips(std::vector<juce::String> ids) : clipIds(std::move(ids)) {}
    juce::String name() const override { return clipCountName("Duplicate", clipIds.size()); }
    void perform(ModelContext&) override;

private:
    std::vector<juce::String> clipIds;
};

class DeleteClips final : public ArrangeCommand
{
public:
    explicit DeleteClips(std::vector<juce::String> ids) : clipIds(std::move(ids)) {}
    juce::String name() const override { return clipCountName("Delete", clipIds.size()); }
    void perform(ModelContext&) override;

private:
    std::vector<juce::String> clipIds;
};

/** A copied clip, relative to the earliest copied clip (time) and the topmost copied track. */
struct ClipRecord
{
    juce::String sourceId;
    juce::String name;
    int trackOffset = 0;
    double startOffset = 0.0;   // beats
    double lengthSeconds = 0.0; // clips play at native speed: the length is kept in seconds
    double offsetSeconds = 0.0;
    double gainDb = 0.0;
    bool reversed = false;
};

class PasteClips final : public ArrangeCommand
{
public:
    PasteClips(std::vector<ClipRecord> clipRecords, int trackIndex, double atBeat)
        : records(std::move(clipRecords)), track(trackIndex), beat(atBeat)
    {
    }
    juce::String name() const override { return clipCountName("Paste", records.size()); }
    void perform(ModelContext&) override;

private:
    std::vector<ClipRecord> records;
    int track;
    double beat;
};

class SetClipGain final : public ArrangeCommand
{
public:
    SetClipGain(juce::String id, double db) : clipId(std::move(id)), gainDb(db) {}
    juce::String name() const override { return "Set clip gain"; }
    void perform(ModelContext&) override;

private:
    juce::String clipId;
    double gainDb;
};

/** Moves the clip to `beat` on its own track (no-overlap rule applies). */
class SetClipStart final : public ArrangeCommand
{
public:
    SetClipStart(juce::String id, double atBeat) : clipId(std::move(id)), beat(atBeat) {}
    juce::String name() const override { return "Set clip start"; }
    void perform(ModelContext&) override;

private:
    juce::String clipId;
    double beat;
};

class SetClipReversed final : public ArrangeCommand
{
public:
    SetClipReversed(juce::String id, bool reverse) : clipId(std::move(id)), reversed(reverse) {}
    juce::String name() const override { return "Reverse clip"; }
    void perform(ModelContext&) override;

private:
    juce::String clipId;
    bool reversed;
};

//==============================================================================
/** Adds a track after `afterTrackId` (at the end when empty or unknown); `resultIds` holds the new track's id. */
class AddTrack final : public ArrangeCommand
{
public:
    explicit AddTrack(juce::String afterTrackId = {}) : after(std::move(afterTrackId)) {}
    juce::String name() const override { return "Add track"; }
    void perform(ModelContext&) override;

private:
    juce::String after;
};

class DeleteTracks final : public ArrangeCommand
{
public:
    explicit DeleteTracks(std::vector<juce::String> ids) : trackIds(std::move(ids)) {}
    juce::String name() const override { return trackIds.size() == 1 ? "Delete track" : "Delete " + juce::String(static_cast<int>(trackIds.size())) + " tracks"; }
    void perform(ModelContext&) override;

private:
    std::vector<juce::String> trackIds;
};

class ReorderTrack final : public ArrangeCommand
{
public:
    ReorderTrack(juce::String id, int index) : trackId(std::move(id)), newIndex(index) {}
    juce::String name() const override { return "Move track"; }
    void perform(ModelContext&) override;

private:
    juce::String trackId;
    int newIndex;
};

/** An empty name (after trimming) changes nothing. */
class RenameTrack final : public ArrangeCommand
{
public:
    RenameTrack(juce::String id, juce::String newName) : trackId(std::move(id)), text(std::move(newName)) {}
    juce::String name() const override { return "Rename track"; }
    void perform(ModelContext&) override;

private:
    juce::String trackId, text;
};

class SetTrackColour final : public ArrangeCommand
{
public:
    SetTrackColour(juce::String id, juce::Colour c) : trackId(std::move(id)), colour(c) {}
    juce::String name() const override { return "Set track colour"; }
    void perform(ModelContext&) override;

private:
    juce::String trackId;
    juce::Colour colour;
};

/** Sets what the track records (see engine::TrackInfo::input). A track cannot record itself: that does nothing. */
class SetTrackInput final : public ArrangeCommand
{
public:
    SetTrackInput(juce::String id, juce::String newInput) : trackId(std::move(id)), input(std::move(newInput)) {}
    juce::String name() const override { return "Set track input"; }
    void perform(ModelContext&) override;

private:
    juce::String trackId, input;
};

/** One finished take of a recording. */
struct Take
{
    juce::String trackId;
    juce::String name;         // "<track name> <date> <time>"
    juce::String relativePath; // inside the bundle: audio/recordings/rec-....wav
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 frames = 0;
    double startBeat = 0.0;
    juce::String recordedFrom; // shown in the browser tooltip
    juce::String recordedAt;   // ISO 8601
};

/**
    Adds the takes of one recording as sounds (category Recording) and places each as a clip on its track at its start
    beat, with the no-overlap rule ("Record take" / "Record N takes"). The files are used where they are.
*/
class AddTakes final : public ArrangeCommand
{
public:
    explicit AddTakes(std::vector<Take> recorded) : takes(std::move(recorded)) {}
    juce::String name() const override { return takes.size() == 1 ? "Record take" : "Record " + juce::String(static_cast<int>(takes.size())) + " takes"; }
    void perform(ModelContext&) override;
    /** Indexes (into the takes given) of the takes that could not be added: the track is gone, no frames, or the sound was refused. */
    const std::vector<size_t>& skippedTakes() const { return skipped; }

private:
    std::vector<Take> takes;
    std::vector<size_t> skipped;
};

/** Volume (dB), pan (-1..1), mute, solo, or exclusive solo (only this track soloed). An empty track id is the master. */
class SetTrackParam final : public ArrangeCommand
{
public:
    enum class Control
    {
        volume,
        pan,
        mute,
        solo,
        exclusiveSolo
    };

    SetTrackParam(juce::String id, Control c, double v) : trackId(std::move(id)), control(c), value(v) {}
    juce::String name() const override;
    void perform(ModelContext&) override;
    juce::String coalesceKey() const override;
    bool allowedWhileRecording() const override { return true; }

private:
    juce::String trackId;
    Control control;
    double value;
};

class SetLoop final : public ArrangeCommand
{
public:
    explicit SetLoop(engine::LoopRange r) : range(r) {}
    juce::String name() const override { return "Set loop"; }
    void perform(ModelContext&) override;

private:
    engine::LoopRange range;
};

/**
    Sets the tempo (clamped to 20..999). Clips keep their start beat; they keep their length in seconds (native speed);
    the loop range keeps its beats; a clip that now runs into the next one is shortened, all in the same step.
*/
class SetTempo final : public ArrangeCommand
{
public:
    explicit SetTempo(double beatsPerMinute) : bpm(beatsPerMinute) {}
    juce::String name() const override { return "Set tempo"; }
    void perform(ModelContext&) override;

    /**
        A tempo drag reuses one command for all its updates: the clip lengths seen at the first `perform` are the
        baseline every later one restores before it shortens overlaps, so going 120 -> 200 -> 120 loses nothing.
        A NaN or infinite tempo does nothing.
    */
    void setBpm(double beatsPerMinute) { bpm = beatsPerMinute; }

private:
    double bpm;
    std::optional<std::vector<engine::ClipInfo>> baseline;
};

class SetTimeSig final : public ArrangeCommand
{
public:
    SetTimeSig(int num, int den) : numerator(num), denominator(den) {}
    juce::String name() const override { return "Set time signature"; }
    void perform(ModelContext&) override;

private:
    int numerator, denominator;
};

/** The planner's view of the arrangement's clips. */
std::vector<Span> spansOf(const engine::Arrangement&);
TimeMap timeMapOf(const engine::Arrangement&);
} // namespace sampler::commands
