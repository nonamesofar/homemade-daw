#pragma once

#include "Widgets.h"

#include <array>

namespace sampler::ui
{
/** Every widget in one place, for looking at them and trying them (Help > Widget gallery). */
class WidgetGallery : public juce::Component, private juce::Timer
{
public:
    WidgetGallery();
    ~WidgetGallery() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

    ToggleButton toggle{"Click"};
    SegmentedControl segmented;
    ValueField valueField;
    Knob knob;
    Fader fader;
    Meter meter;
    std::array<Pad, 4> pads{Pad("1"), Pad("2"), Pad("3"), Pad("4")};

private:
    void timerCallback() override;
    juce::String readout = "Move a control";
    double phase = 0.0;
};
} // namespace sampler::ui
