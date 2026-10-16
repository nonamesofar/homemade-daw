#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <optional>
#include <vector>

namespace sampler::model
{
/** Property and node names of the slice set (children of a SOURCE node). */
namespace sliceIds
{
#define SAMPLER_SLICE_ID(name) inline const juce::Identifier name(#name)
SAMPLER_SLICE_ID(SLICESET);
SAMPLER_SLICE_ID(SLICE);
SAMPLER_SLICE_ID(mode);
SAMPLER_SLICE_ID(division);
SAMPLER_SLICE_ID(sensitivity);
SAMPLER_SLICE_ID(markers);
SAMPLER_SLICE_ID(pitch);
SAMPLER_SLICE_ID(gainDb);
SAMPLER_SLICE_ID(reverse);
SAMPLER_SLICE_ID(playMode);
#undef SAMPLER_SLICE_ID
} // namespace sliceIds

/** How the slice points were made: from the beat grid, from the hits, or by hand. */
enum class SliceMode
{
    beat,
    transient,
    manual
};
juce::String toString(SliceMode);
SliceMode sliceModeFromString(const juce::String&, SliceMode fallback = SliceMode::manual);

/** The grid of Beat mode, as a fraction of a whole note. */
enum class SliceDivision
{
    quarter,
    eighth,
    sixteenth,
    thirtySecond
};
juce::String toString(SliceDivision);
SliceDivision sliceDivisionFromString(const juce::String&, SliceDivision fallback = SliceDivision::quarter);
/** Slices per beat: 1, 2, 4 or 8. */
double slicesPerBeat(SliceDivision);

/** How a slice's pad plays: to the end whatever the key does, while the key is held, or repeating while held. */
enum class PlayMode
{
    oneShot,
    gate,
    loop
};
juce::String toString(PlayMode);
PlayMode playModeFromString(const juce::String&, PlayMode fallback = PlayMode::oneShot);

constexpr double minSlicePitch = -24.0;
constexpr double maxSlicePitch = 24.0;
/** Gain range of a slice: silence to +24 dB (-100 dB stands for silence). */
constexpr double silenceGainDb = -100.0;
constexpr double maxSliceGainDb = 24.0;
constexpr double defaultSensitivity = 50.0;

/** At most this many slices in a sound, so at most 255 slice points. */
constexpr int maxSliceCount = 256;
/** A slice is at least 5 ms long. */
constexpr double minSliceSeconds = 0.005;
juce::int64 minSliceFrames(double sampleRate);

/** What one slice plays like: its pad settings. */
struct SliceSettings
{
    double pitch = 0.0;  // semitones
    double gainDb = 0.0;
    bool reverse = false;
    PlayMode playMode = PlayMode::oneShot;

    bool operator==(const SliceSettings&) const = default;
};

/**
    Everything saved about the slices of one sound (design D3): the mode, the grid and the sensitivity last used, the
    slice points as frames of the sound (strictly increasing, never 0 because the first slice always starts there) and
    one SliceSettings per slice (points + 1 of them). A sound that was never chopped has no points and one slice.
*/
struct SliceSetData
{
    SliceMode mode = SliceMode::manual;
    SliceDivision division = SliceDivision::quarter;
    double sensitivity = defaultSensitivity;
    std::vector<juce::int64> markers;
    std::vector<SliceSettings> slices{SliceSettings{}};

    int numSlices() const { return static_cast<int>(markers.size()) + 1; }
    /** Makes `slices` as long as the slices there are (new ones get the defaults, extra ones are dropped). */
    void normalise();
    bool operator==(const SliceSetData&) const = default;
};

/**
    The slice set of a SOURCE node (the defaults while it has none). By default the points are made safe to use, as a
    corrupt or hand-edited project may hold bad ones: sorted, without duplicates, none at or before the start, at most 255.
    `sanitise = false` reads them as saved (the debug post-condition looks for violations that way).
*/
SliceSetData readSliceSet(const juce::ValueTree& sourceNode, bool sanitise = true);
/** Drops the points at or past the end of a sound of `frames` frames (no-op when `frames` is not positive). */
void dropMarkersPastEnd(SliceSetData&, juce::int64 frames);
/** True if the SOURCE node has a SLICESET child. */
bool hasSliceSet(const juce::ValueTree& sourceNode);
/** Writes the slice set into the SOURCE node, replacing the old one (through `undo`). */
void writeSliceSet(juce::ValueTree& sourceNode, const SliceSetData&, juce::UndoManager* undo);
/** The node form of a slice set (also used by the tests). */
juce::ValueTree createSliceSetNode(const SliceSetData&);

/**
    What is wrong with a slice set of a sound with `frames` frames at `sampleRate`, or nothing: points increasing and
    inside the sound, every slice at least 5 ms long, at most 256 slices, one settings entry per slice, values in range.
*/
std::optional<juce::String> findSliceSetViolation(const SliceSetData&, juce::int64 frames, double sampleRate);

/**
    Splits the slice that contains `frame` at `frame`: both halves keep its settings. False (nothing changed) if the
    point is already there, lies outside the sound or would make a slice shorter than 5 ms or more than 256 slices.
*/
bool insertSliceMarker(SliceSetData&, juce::int64 frame, juce::int64 totalFrames, double sampleRate);
/** Removes slice point `index`: the two slices merge and keep the settings of the left one. */
void removeSliceMarker(SliceSetData&, int index);
} // namespace sampler::model
