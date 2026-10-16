#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace sampler::ui::editor
{
/**
    The 16 pads of the sample editor (slice-pads spec), laid out like an MPC: pad 1 bottom left, pad 4 bottom right, pad 13
    top left. Each pad shows its number and its key; a pad without a slice is drawn empty and does nothing; a pad lights
    while its slice sounds. With more than 16 slices, bank buttons A, B, C... appear above the grid; pad n of bank b plays
    slice b * 16 + n. It draws and reports presses; the owner plays.
*/
class PadGrid : public juce::Component
{
public:
    static constexpr int padsPerBank = 16;
    static constexpr int maxBanks = 16;

    PadGrid();

    /** How many slices there are (pads past the last slice are empty) and the bank shown. */
    void setSliceCount(int count);
    void setBank(int bank);
    int bank() const { return currentBank; }
    int bankCount() const { return (sliceCount + padsPerBank - 1) / padsPerBank; }
    int numSlices() const { return sliceCount; }

    /** The slice a pad plays in the current bank, or -1 if the pad is empty. */
    int sliceOfPad(int pad) const;
    /** The pad (0..15) at a point, or -1. */
    int padAt(juce::Point<int>) const;
    juce::Rectangle<int> padArea(int pad) const;

    /** The slice selected in the editor gets an outline. */
    void setSelectedSlice(int slice);
    /** Which slices sound now (asked at every `tick`). */
    std::function<bool(int slice)> isSounding;
    /** One display frame: repaints only the pads whose light changed. */
    void tick();
    bool isLit(int pad) const { return juce::isPositiveAndBelow(pad, padsPerBank) && lit[static_cast<size_t>(pad)]; }

    /** A pad was pressed with the mouse / let go; `slice` is the slice index. */
    std::function<void(int slice)> onPadDown, onPadUp;
    std::function<void(int bank)> onBankChosen;

    juce::Rectangle<int> bankButtonArea(int bank) const;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> gridArea() const;

    int sliceCount = 1;
    int currentBank = 0;
    int selectedSlice = 0;
    int pressedSlice = -1;
    std::array<bool, padsPerBank> lit{};
};
} // namespace sampler::ui::editor
