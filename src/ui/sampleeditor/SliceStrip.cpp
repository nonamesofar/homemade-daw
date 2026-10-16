#include "ui/sampleeditor/SliceStrip.h"

#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui::editor
{
int SliceStrip::cellAt(int x) const
{
    if (! geometry || x < 0 || x >= getWidth())
        return -1;
    return geometry().sliceAtX(static_cast<double>(x));
}

std::pair<double, double> SliceStrip::cellSpan(int slice) const
{
    if (! geometry)
        return {0.0, 0.0};
    return geometry().sliceSpan(slice);
}

void SliceStrip::mouseDown(const juce::MouseEvent& e)
{
    const auto cell = cellAt(e.x);
    if (cell >= 0 && onSelectSlice)
        onSelectSlice(cell);
}

void SliceStrip::paint(juce::Graphics& g)
{
    g.fillAll(theme::raised);
    g.setColour(theme::border);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    if (! geometry)
        return;

    const auto geo = geometry();
    g.setFont(Fonts::mono(11.0f));
    for (int slice = 0; slice < geo.numSlices(); ++slice)
    {
        const auto span = geo.sliceSpan(slice);
        if (span.second <= span.first)
            continue; // off screen
        const auto cell = juce::Rectangle<float>(static_cast<float>(span.first), 2.0f, static_cast<float>(span.second - span.first) - 1.0f,
                                                 static_cast<float>(getHeight()) - 5.0f);
        const bool isSelected = slice == selected;
        g.setColour(isSelected ? theme::accent : theme::selected);
        g.fillRoundedRectangle(cell, 3.0f);
        if (cell.getWidth() >= 14.0f)
        {
            g.setColour(isSelected ? theme::background : theme::muted);
            g.drawText(sliceLabel(slice), cell, juce::Justification::centred, false);
        }
    }
}
} // namespace sampler::ui::editor
