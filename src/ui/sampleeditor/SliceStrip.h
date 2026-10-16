#pragma once

#include "ui/sampleeditor/EditorMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sampler::ui::editor
{
/**
    The numbered strip above the waveform: one cell per slice, laid out by the same viewport as the waveform so a cell sits
    exactly over its slice. Clicking a cell selects that slice (not an undo step).
*/
class SliceStrip : public juce::Component
{
public:
    static constexpr int stripHeight = 26;

    /** The geometry of the waveform below, asked at every paint and click. */
    std::function<SliceGeometry()> geometry;
    /** The selected slice (highlighted), and how many pads/slices exist, come from the owner. */
    void setSelectedSlice(int slice)
    {
        selected = slice;
        repaint();
    }
    int selectedSlice() const { return selected; }
    std::function<void(int sliceIndex)> onSelectSlice;

    /** The slice whose cell contains pixel x, or -1. */
    int cellAt(int x) const;
    /** The cell's span in pixels (empty if off screen), for tests. */
    std::pair<double, double> cellSpan(int slice) const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    int selected = 0;
};
} // namespace sampler::ui::editor
