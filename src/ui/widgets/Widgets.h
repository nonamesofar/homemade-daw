#pragma once

#include "WidgetMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

/** The reusable widgets. All drawing uses the theme; all value maths lives in WidgetMath. */
namespace sampler::ui
{
/** A labelled on/off button (Mute, Solo, Click). */
class ToggleButton : public juce::Button
{
public:
    explicit ToggleButton(const juce::String& label);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

/** A row of exclusive choices (Session | Arrange). */
class SegmentedControl : public juce::Component
{
public:
    void setItems(const juce::StringArray&);
    void setSelected(int index, bool notify = false);
    int selected() const { return selectedIndex; }
    int itemAt(juce::Point<int> position) const { return segmentAt(position.x, getWidth(), items.size()); }
    std::function<void(int)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::StringArray items;
    int selectedIndex = 0;
};

/** A value that is changed by dragging up and down (Shift = fine) and typed after a double click. */
class ValueField : public juce::Component, private juce::TextEditor::Listener
{
public:
    ValueField();
    void setRange(const ValueRange& r)
    {
        range = r;
        setValue(value, false);
    }
    void setSuffix(const juce::String& s, int decimalPlaces);
    void setValue(double v, bool notify = false);
    double getValue() const { return value; }
    juce::String text() const;
    /** Applies typed text; returns false (and keeps the value) if it is not a number. */
    bool commitText(const juce::String&);
    std::function<void(double)> onChange;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { closeEditor(); }
    void textEditorFocusLost(juce::TextEditor&) override { closeEditor(); }
    void closeEditor();

    ValueRange range;
    double value = 0.0;
    double dragStart = 0.0;
    juce::String suffix;
    int decimals = 2;
    std::unique_ptr<juce::TextEditor> editor;
};

/** A rotary control: drag up and down, double-click resets to the default. */
class Knob : public juce::Component
{
public:
    void setRange(const ValueRange& r)
    {
        range = r;
        setValue(value, false);
    }
    void setLabel(const juce::String& l)
    {
        label = l;
        repaint();
    }
    void setValue(double v, bool notify = false);
    double getValue() const { return value; }
    std::function<void(double)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    ValueRange range;
    double value = 0.0;
    double dragStart = 0.0;
    juce::String label;
};

/** A vertical fader with the same behaviour as the knob. */
class Fader : public juce::Component
{
public:
    void setRange(const ValueRange& r)
    {
        range = r;
        setValue(value, false);
    }
    void setValue(double v, bool notify = false);
    double getValue() const { return value; }
    std::function<void(double)> onChange;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    ValueRange range;
    double value = 0.0;
    double dragStart = 0.0;
};

/** A level meter with a held peak. Feed it levels in dB; call `tick` at display rate. */
class Meter : public juce::Component
{
public:
    void setLevelDb(double db) { levelDb = db; }
    /** Advances the peak hold by `elapsedMs` and repaints. */
    void tick(double elapsedMs);
    double peakDb() const { return hold.peakDb(); }
    void paint(juce::Graphics&) override;

private:
    double levelDb = -120.0;
    PeakHold hold;
};

/** A drum pad. Any press triggers it; the flash fades out by itself. */
class Pad : public juce::Component, private juce::Timer
{
public:
    explicit Pad(const juce::String& padLabel = {}) : label(padLabel) {}
    void trigger(float velocity);
    float flash() const { return flashLevel; }
    std::function<void(float velocity)> onTrigger;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::String label;
    float flashLevel = 0.0f;
};
} // namespace sampler::ui
