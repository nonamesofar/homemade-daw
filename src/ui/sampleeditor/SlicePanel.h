#pragma once

#include "model/SliceSet.h"
#include "ui/sampleeditor/EditorMath.h"
#include "ui/widgets/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sampler::ui::editor
{
/** What the slice panel shows about the selected slice. */
struct SliceView
{
    int index = 0;
    int count = 1;
    juce::int64 startFrame = 0;
    juce::int64 endFrame = 0;
    juce::int64 totalFrames = 0;
    Tempo tempo;
    model::SliceSettings settings;
};

/** Units of START, END and LENGTH. */
enum class Units
{
    bars = 0,
    seconds = 1,
    samples = 2
};

/** The settings of a slice the panel can change. */
enum class SliceParam
{
    pitch,
    gain,
    reverse,
    playMode
};

/** Field value at the left end of the gain field: silence. */
constexpr double silenceFieldDb = -60.0;

/** The text of a frame position in the chosen units (bar.beat.sixteenth from the first beat, seconds, or the sample number). */
juce::String positionText(Units, const Tempo&, juce::int64 frame);
/** The text of a length in frames: bars.beats.sixteenths counted from zero, seconds, or samples. */
juce::String lengthText(Units, const Tempo&, juce::int64 frames);
/** A typed position in the chosen units. */
std::optional<juce::int64> parsePosition(Units, const Tempo&, const juce::String&);

/**
    The slice panel: START, END and LENGTH of the selected slice with a Bars / Seconds / Samples switch, and (from the pad
    task on) its pitch, gain, reverse and play mode. Typing a START or END asks the owner to move the matching slice point;
    a refused value puts the old one back.
*/
class SlicePanel : public juce::Component
{
public:
    SlicePanel();

    void setSlice(const SliceView&, Units);
    const SliceView& slice() const { return view; }
    Units units() const { return shownUnits; }

    ValueField startField, endField;
    SegmentedControl unitsControl;

    // The pad settings of the selected slice (slice-pads spec, "Slice pitch, gain and reverse" and "Play modes").
    ValueField pitchField, gainField;
    ToggleButton reverseButton{"Reverse"};
    SegmentedControl playModeControl;
    /** The user changed a setting. `value`: semitones, dB (-100 = silence), 0 or 1, or the play mode as 0, 1 or 2. */
    std::function<void(SliceParam, double value)> onParamChanged;

    /** The user typed a START (isEnd false) or END; return false to refuse (the old value comes back). */
    std::function<bool(bool isEnd, juce::int64 frame)> onBoundsTyped;
    std::function<void(Units)> onUnitsChanged;

    juce::String lengthString() const { return lengthShown; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshFields();

    SliceView view;
    Units shownUnits = Units::bars;
    juce::String lengthShown;
    bool updating = false;
};
} // namespace sampler::ui::editor
