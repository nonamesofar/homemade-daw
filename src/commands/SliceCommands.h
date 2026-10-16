#pragma once

#include "CommandService.h"
#include "model/SamplerState.h"

#include <vector>

namespace sampler::commands
{
/**
    Chooses how a sound is chopped ("Slice by beat", "Slice by transient", "Slice manually"). Beat and Transient replace
    the slice points with `markers`, which the caller computed with `analysis::beatSlicePoints` or
    `analysis::transientSlicePoints` from the sound's effective tempo or its hits, and reset every slice to the defaults.
    Manual keeps the points and the slice settings. Does nothing (no undo step) if nothing would change.
*/
class SetSliceMode final : public Command
{
public:
    SetSliceMode(juce::String sourceId, model::SliceMode newMode, model::SliceDivision newDivision, double newSensitivity,
                 std::vector<juce::int64> newMarkers = {})
        : id(std::move(sourceId)), mode(newMode), division(newDivision), sensitivity(newSensitivity), markers(std::move(newMarkers))
    {
    }

    juce::String name() const override;
    void perform(ModelContext&) override;

private:
    juce::String id;
    model::SliceMode mode;
    model::SliceDivision division;
    double sensitivity;
    std::vector<juce::int64> markers;
};

/** Keeps the slices the Sensitivity slider previewed ("Auto-slice"): Transient mode with `markers`. */
class AutoSlice final : public Command
{
public:
    AutoSlice(juce::String sourceId, double newSensitivity, std::vector<juce::int64> newMarkers)
        : id(std::move(sourceId)), sensitivity(newSensitivity), markers(std::move(newMarkers))
    {
    }

    juce::String name() const override { return "Auto-slice"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    double sensitivity;
    std::vector<juce::int64> markers;
};

/** Another grid in Beat mode ("Slice by beat"): re-slices at once with `markers` for the new division. */
class SetSliceDivision final : public Command
{
public:
    SetSliceDivision(juce::String sourceId, model::SliceDivision newDivision, std::vector<juce::int64> newMarkers)
        : id(std::move(sourceId)), division(newDivision), markers(std::move(newMarkers))
    {
    }

    juce::String name() const override { return "Slice by beat"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    model::SliceDivision division;
    std::vector<juce::int64> markers;
};

/** Adds a slice point ("Add slice point"); both halves keep the settings of the slice that was split. Switches to Manual. */
class AddSlicePoint final : public Command
{
public:
    AddSlicePoint(juce::String sourceId, juce::int64 atFrame) : id(std::move(sourceId)), frame(atFrame) {}

    juce::String name() const override { return "Add slice point"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    juce::int64 frame;
};

/**
    Moves slice point `index` to `frame` ("Move slice point"), held 5 ms away from its neighbours. Meant for a gesture
    (`begin`, an `update` per mouse move, `commit`). Switches to Manual in the same step.
*/
class MoveSlicePoint final : public Command
{
public:
    MoveSlicePoint(juce::String sourceId, int markerIndex, juce::int64 toFrame) : id(std::move(sourceId)), index(markerIndex), frame(toFrame) {}

    juce::String name() const override { return "Move slice point"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    int index;
    juce::int64 frame;
};

/** Removes slice point `index` ("Remove slice point"); the slices merge and keep the left one's settings. Switches to Manual. */
class RemoveSlicePoint final : public Command
{
public:
    RemoveSlicePoint(juce::String sourceId, int markerIndex) : id(std::move(sourceId)), index(markerIndex) {}

    juce::String name() const override { return "Remove slice point"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    int index;
};

/**
    Typed START or END of a slice ("Set slice start" / "Set slice end"): moves the point that bounds it. The start of the
    first slice and the end of the last one are the ends of the sound and cannot move. A value that would leave a
    neighbouring slice shorter than 5 ms or cross a neighbour is refused (nothing changes).
*/
class SetSliceBounds final : public Command
{
public:
    SetSliceBounds(juce::String sourceId, int sliceIndex, bool isEnd, juce::int64 toFrame)
        : id(std::move(sourceId)), slice(sliceIndex), end(isEnd), frame(toFrame)
    {
    }

    juce::String name() const override { return end ? "Set slice end" : "Set slice start"; }
    void perform(ModelContext&) override;

    /** Whether the last `perform` changed something. */
    bool wasApplied() const { return applied; }

private:
    juce::String id;
    int slice;
    bool end;
    juce::int64 frame;
    bool applied = false;
};

/**
    Changes one setting of one slice: its pitch (-24 to +24 semitones, clamped), gain (silence to +24 dB), Reverse or play
    mode ("Set slice pitch", "Set slice gain", "Reverse slice", "Set play mode"). Repeated changes of the same setting of
    the same slice less than 500 ms apart share one undo step. For Reverse `value` is 0 or 1; for the play mode it is
    the PlayMode as a number (0 One-shot, 1 Gate, 2 Loop).
*/
class SetSliceParam final : public Command
{
public:
    enum class Param
    {
        pitch,
        gain,
        reverse,
        playMode
    };

    SetSliceParam(juce::String sourceId, int sliceIndex, Param which, double newValue)
        : id(std::move(sourceId)), slice(sliceIndex), param(which), value(newValue)
    {
    }

    juce::String name() const override;
    void perform(ModelContext&) override;
    juce::String coalesceKey() const override;

private:
    juce::String id;
    int slice;
    Param param;
    double value;
};
} // namespace sampler::commands
