#include "ui/sampleeditor/PadGrid.h"

#include "ui/KeyboardRouter.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui::editor
{
namespace
{
constexpr int bankRowHeight = 28;
constexpr int gap = 8;
} // namespace

PadGrid::PadGrid() { setOpaque(false); }

void PadGrid::setSliceCount(int count)
{
    sliceCount = juce::jmax(1, count);
    currentBank = juce::jlimit(0, juce::jmax(0, bankCount() - 1), currentBank);
    resized();
    repaint();
}

void PadGrid::setBank(int bank)
{
    currentBank = juce::jlimit(0, juce::jmax(0, bankCount() - 1), bank);
    repaint();
}

void PadGrid::setSelectedSlice(int slice)
{
    if (slice == selectedSlice)
        return;
    selectedSlice = slice;
    repaint();
}

int PadGrid::sliceOfPad(int pad) const
{
    if (! juce::isPositiveAndBelow(pad, padsPerBank))
        return -1;
    const int slice = currentBank * padsPerBank + pad;
    return slice < sliceCount ? slice : -1;
}

juce::Rectangle<int> PadGrid::gridArea() const
{
    auto area = getLocalBounds();
    if (bankCount() > 1)
        area.removeFromTop(bankRowHeight + 6);
    return area;
}

juce::Rectangle<int> PadGrid::padArea(int pad) const
{
    const auto area = gridArea();
    const int cellW = (area.getWidth() - 3 * gap) / 4;
    const int cellH = (area.getHeight() - 3 * gap) / 4;
    const int col = pad % 4;
    const int rowFromBottom = pad / 4; // pad 1 is bottom left
    return {area.getX() + col * (cellW + gap), area.getY() + (3 - rowFromBottom) * (cellH + gap), cellW, cellH};
}

int PadGrid::padAt(juce::Point<int> p) const
{
    for (int pad = 0; pad < padsPerBank; ++pad)
        if (padArea(pad).contains(p))
            return pad;
    return -1;
}

juce::Rectangle<int> PadGrid::bankButtonArea(int bank) const
{
    // 34 px wide, narrower when all the banks would not fit the width.
    const int width = juce::jlimit(1, 34, (getWidth() + 4) / juce::jmax(1, bankCount()) - 4);
    return {bank * (width + 4), 0, width, bankRowHeight};
}

void PadGrid::tick()
{
    for (int pad = 0; pad < padsPerBank; ++pad)
    {
        const int slice = sliceOfPad(pad);
        const bool now = slice >= 0 && isSounding && isSounding(slice);
        if (now != lit[static_cast<size_t>(pad)])
        {
            lit[static_cast<size_t>(pad)] = now;
            repaint(padArea(pad)); // only this pad
        }
    }
}

void PadGrid::mouseDown(const juce::MouseEvent& e)
{
    if (bankCount() > 1 && e.y < bankRowHeight)
    {
        for (int bank = 0; bank < bankCount(); ++bank)
            if (bankButtonArea(bank).contains(e.getPosition()))
            {
                setBank(bank);
                if (onBankChosen)
                    onBankChosen(bank);
                return;
            }
    }
    const int pad = padAt(e.getPosition());
    const int slice = sliceOfPad(pad);
    if (slice < 0)
        return;
    pressedSlice = slice; // the pad is let go as the slice it started, whatever the bank shows by then
    if (onPadDown)
        onPadDown(slice);
}

void PadGrid::mouseUp(const juce::MouseEvent&)
{
    if (pressedSlice < 0)
        return;
    const int slice = pressedSlice;
    pressedSlice = -1;
    if (onPadUp)
        onPadUp(slice);
}

void PadGrid::resized() {}

void PadGrid::paint(juce::Graphics& g)
{
    if (bankCount() > 1)
    {
        g.setFont(Fonts::sans(12.0f, Fonts::Weight::semiBold));
        for (int bank = 0; bank < bankCount(); ++bank)
        {
            const auto r = bankButtonArea(bank).toFloat();
            const bool on = bank == currentBank;
            g.setColour(on ? theme::accent : theme::raised);
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(on ? theme::background : theme::text);
            g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>('A' + bank)), r, juce::Justification::centred);
        }
    }

    for (int pad = 0; pad < padsPerBank; ++pad)
    {
        const auto r = padArea(pad).toFloat();
        const int slice = sliceOfPad(pad);
        if (slice < 0)
        {
            // An empty pad: only an outline.
            g.setColour(theme::border);
            g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
            continue;
        }
        const bool on = lit[static_cast<size_t>(pad)];
        g.setColour(on ? theme::accent : theme::raised);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(slice == selectedSlice ? theme::accent : theme::border);
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, slice == selectedSlice ? 2.0f : 1.0f);

        g.setColour(on ? theme::background : theme::text);
        g.setFont(Fonts::sans(15.0f, Fonts::Weight::semiBold));
        g.drawText(juce::String(slice + 1), r.reduced(8.0f, 4.0f), juce::Justification::topLeft);
        g.setColour(on ? theme::background : theme::muted);
        g.setFont(Fonts::mono(11.0f));
        g.drawText(KeyboardRouter::keyLabelForPad(pad), r.reduced(8.0f, 4.0f), juce::Justification::bottomRight);
    }
}
} // namespace sampler::ui::editor
