#include "WidgetGallery.h"

#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui
{
WidgetGallery::WidgetGallery()
{
    segmented.setItems({"Session", "Arrange"});
    segmented.onChange = [this](int i) { readout = "Segmented control: " + juce::String(i == 0 ? "Session" : "Arrange"); repaint(); };

    valueField.setRange({20.0, 240.0, 120.0, 0.01, false});
    valueField.setValue(120.0);
    valueField.setSuffix("", 2);
    valueField.onChange = [this](double v) { readout = "Value field: " + juce::String(v, 2); repaint(); };

    knob.setRange({0.0, 1.0, 0.5, 0.0, false});
    knob.setLabel("Level");
    knob.setValue(0.5);
    knob.onChange = [this](double v) { readout = "Knob: " + juce::String(v, 2); repaint(); };

    fader.setRange({-60.0, 6.0, 0.0, 0.1, false});
    fader.setValue(0.0);
    fader.onChange = [this](double v) { readout = "Fader: " + juce::String(v, 1) + " dB"; repaint(); };

    toggle.onClick = [this] { readout = juce::String("Toggle: ") + (toggle.getToggleState() ? "on" : "off"); repaint(); };

    for (auto& pad : pads)
    {
        pad.onTrigger = [this](float velocity) { readout = "Pad: velocity " + juce::String(velocity, 2); repaint(); };
        addAndMakeVisible(pad);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*>{&toggle, &segmented, &valueField, &knob, &fader, &meter})
        addAndMakeVisible(*c);

    setSize(560, 340);
    startTimerHz(30);
}

void WidgetGallery::timerCallback()
{
    phase += 0.09;
    meter.setLevelDb(-30.0 + 28.0 * std::sin(phase) * std::sin(phase * 0.37));
    meter.tick(1000.0 / 30.0);
}

void WidgetGallery::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(8);
    auto top = area.removeFromTop(44);
    segmented.setBounds(top.removeFromLeft(200));
    top.removeFromLeft(16);
    toggle.setBounds(top.removeFromLeft(80).withSizeKeepingCentre(80, 34));
    top.removeFromLeft(16);
    valueField.setBounds(top.removeFromLeft(110).withSizeKeepingCentre(110, 36));

    area.removeFromTop(20);
    auto middle = area.removeFromTop(150);
    knob.setBounds(middle.removeFromLeft(100));
    middle.removeFromLeft(20);
    fader.setBounds(middle.removeFromLeft(60));
    middle.removeFromLeft(20);
    meter.setBounds(middle.removeFromLeft(14));
    middle.removeFromLeft(40);
    for (size_t i = 0; i < pads.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(middle.getX() + static_cast<int>(i % 2) * 78, middle.getY() + static_cast<int>(i / 2) * 74, 72, 68);
        pads[i].setBounds(cell);
    }
}

void WidgetGallery::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::muted);
    g.setFont(Fonts::sans(10.0f));
    g.drawText("WIDGET GALLERY", 20, 12, 300, 14, juce::Justification::centredLeft);
    g.setColour(theme::text);
    g.setFont(Fonts::mono(13.0f));
    g.drawText(readout, 20, getHeight() - 34, getWidth() - 40, 20, juce::Justification::centredLeft);
}
} // namespace sampler::ui
