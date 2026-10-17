#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace sampler::commands
{
/** Beats <-> seconds, handed in by the command (a tempo map later changes nothing here). */
struct TimeMap
{
    std::function<double(double beats)> toSeconds;
    std::function<double(double seconds)> toBeats;

    double secondsBetween(double fromBeat, double toBeat) const { return toSeconds(toBeat) - toSeconds(fromBeat); }
    /** The beat that lies `seconds` after `fromBeat`. */
    double beatAfter(double fromBeat, double seconds) const { return toBeats(toSeconds(fromBeat) + seconds); }

    static TimeMap constantTempo(double bpm);
};

struct Span;
/** Seconds of the sound between two beats of a clip: through the tempo when unwarped, through its own tempo when warped. */
double soundSecondsBetween(const Span&, double fromBeat, double toBeat, const TimeMap&);
/** The beat that lies `soundSeconds` of the clip's sound after `fromBeat`. */
double beatAfterSoundSeconds(const Span&, double fromBeat, double soundSeconds, const TimeMap&);

/**
    A clip as the planner sees it: track index, start and length in beats, offset into its sound in seconds.
    Existing clips have an `id`; clips that do not exist yet have none and say where they come from: a copy of an
    existing clip (`copyOf`) or entry `specIndex` of the command's list of new clips.
*/
struct Span
{
    juce::String id;
    juce::String copyOf;
    int specIndex = -1;
    bool cutPiece = false; // the part of a clip that remains after another clip landed inside it
    int tag = -1; // the command's own number for a placed clip (pieces cut from other clips keep -1)
    int track = 0;
    double start = 0.0;
    double length = 0.0;
    double offset = 0.0; // seconds into the sound (the reversed sound when reversed)
    /**
        The clip's own tempo when it is warped, else 0. A warped clip plays 60 / warpBpm seconds of its sound per beat
        whatever the project tempo, so its offset moves by that when its start moves; an unwarped clip plays the sound at
        its own speed, so seconds of the sound are seconds of the timeline (through the TimeMap).
    */
    double warpBpm = 0.0;
    /**
        A looped clip repeats [loopStart, loopStart + loopLength) of its sound (seconds of the sound; 0 length = not looped),
        and `offset` is where inside that part it starts: moving its start wraps the offset around the loop instead of
        running out of sound.
    */
    double loopStart = 0.0;
    double loopLength = 0.0;

    bool isLooped() const { return loopLength > 0.0; }
    double end() const { return start + length; }
};

/** The primitive edits a command performs: bounds changes of existing clips, removals, and new clips. */
struct Plan
{
    std::vector<Span> changed;
    std::vector<juce::String> removed;
    std::vector<Span> created;

    bool isEmpty() const { return changed.empty() && removed.empty() && created.empty(); }
};

/**
    Plain edit planning (design D-2): where clips go and what the no-overlap rule of §8.3 does to the clips underneath.
    A clip that is placed (moved, pasted, duplicated, dropped, trimmed) wins: a clip it partly covers is shortened, a clip
    it fully covers is removed, a clip it lands inside is split around it. Placed clips are applied in order, so a later
    one wins over an earlier one. Trimming the start of a clip moves its offset, so its audio stays in time.
*/
class EditPlanner
{
public:
    static constexpr double epsilon = 1.0e-7;

    /** `existing`: every clip of the arrangement. `placed`: the winners (existing ids at new bounds, or new clips). */
    static Plan place(const std::vector<Span>& existing, const std::vector<Span>& placed, const TimeMap&);

    /** Splits each listed clip that crosses `beat` into two clips at `beat`. */
    static Plan split(const std::vector<Span>& existing, const std::vector<juce::String>& ids, double beat, const TimeMap&);

    /** After a tempo change: on each track, a clip that runs into the next one ends where the next one starts. */
    static Plan shortenOverlaps(const std::vector<Span>& existing);

    /** The clip with its start moved to `newStart` and its end kept, audio in time (offset follows). */
    static Span withStart(const Span&, double newStart, const TimeMap&);

    /**
        Limits for trimming: the start may not go before the sound's start (offset 0) or within `minLength` of the end;
        the end may not pass the sound's end (`sourceSeconds`) or come within `minLength` of the start. A looped clip can
        be made longer than its sound (up to `maxLoopRepeats` times the sound), but not start earlier.
    */
    static constexpr double maxLoopRepeats = 50.0;
    static double clampTrimStart(const Span&, double newStart, double minLength, const TimeMap&);
    static double clampTrimEnd(const Span&, double newEnd, double sourceSeconds, double minLength, const TimeMap&);
};
} // namespace sampler::commands
