#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sampler::ui
{
/** The dark look of the mock: theme colours and IBM Plex for every standard JUCE component. */
class SamplerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SamplerLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Font getPopupMenuFont() override;
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    juce::Font getLabelFont(juce::Label&) override;
    int getDefaultScrollbarWidth() override { return 10; }
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical, int thumbStart,
                       int thumbSize, bool mouseOver, bool mouseDown) override;

    /** Property name: a TextButton with this property set to true is drawn as a left-aligned list entry. */
    static const juce::Identifier listEntryProperty;
};
} // namespace sampler::ui
