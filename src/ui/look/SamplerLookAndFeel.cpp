#include "SamplerLookAndFeel.h"

#include "Fonts.h"
#include "Theme.h"

namespace sampler::ui
{
const juce::Identifier SamplerLookAndFeel::listEntryProperty{"listEntry"};

SamplerLookAndFeel::SamplerLookAndFeel()
{
    using juce::ResizableWindow;
    setDefaultSansSerifTypeface(Fonts::sansTypeface());

    setColour(ResizableWindow::backgroundColourId, theme::background);
    setColour(juce::DocumentWindow::textColourId, theme::text);

    setColour(juce::TextButton::buttonColourId, theme::raised);
    setColour(juce::TextButton::buttonOnColourId, theme::selected);
    setColour(juce::TextButton::textColourOffId, theme::muted);
    setColour(juce::TextButton::textColourOnId, theme::text);

    setColour(juce::TextEditor::backgroundColourId, theme::background);
    setColour(juce::TextEditor::textColourId, theme::text);
    setColour(juce::TextEditor::outlineColourId, theme::border);
    setColour(juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour(juce::TextEditor::highlightColourId, theme::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, theme::text);
    setColour(juce::CaretComponent::caretColourId, theme::accent);

    setColour(juce::Label::textColourId, theme::text);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::PopupMenu::backgroundColourId, theme::raised);
    setColour(juce::PopupMenu::textColourId, theme::text);
    setColour(juce::PopupMenu::headerTextColourId, theme::muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::selected);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::text);

    setColour(juce::ScrollBar::thumbColourId, theme::border);

    setColour(juce::AlertWindow::backgroundColourId, theme::panel);
    setColour(juce::AlertWindow::textColourId, theme::text);
    setColour(juce::AlertWindow::outlineColourId, theme::border);
}

juce::Typeface::Ptr SamplerLookAndFeel::getTypefaceForFont(const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || font.getTypefaceName() == "Default")
        return Fonts::sansTypeface(font.isBold() ? Fonts::Weight::semiBold : Fonts::Weight::regular);
    return LookAndFeel_V4::getTypefaceForFont(font);
}

juce::Font SamplerLookAndFeel::getPopupMenuFont() { return Fonts::sans(14.0f); }
juce::Font SamplerLookAndFeel::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) { return Fonts::sans(13.0f, Fonts::Weight::medium); }
juce::Font SamplerLookAndFeel::getLabelFont(juce::Label&) { return Fonts::sans(13.0f); }
juce::Font SamplerLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return Fonts::sans(13.0f, Fonts::Weight::medium); }

void SamplerLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    if (button.getProperties()[listEntryProperty])
    {
        if (button.getToggleState())
            g.setColour(theme::selected);
        else if (highlighted || down)
            g.setColour(theme::selected.withAlpha(0.5f));
        else
            return;
        g.fillRoundedRectangle(bounds, 6.0f);
        return;
    }
    g.setColour(down || highlighted ? theme::selected : theme::raised);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(theme::border);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void SamplerLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool listEntry = button.getProperties()[listEntryProperty];
    const bool on = button.getToggleState();
    g.setColour(listEntry ? (on ? theme::text : theme::muted) : theme::text);
    g.setFont(Fonts::sans(13.0f, listEntry && ! on ? Fonts::Weight::regular : Fonts::Weight::medium));
    const auto area = button.getLocalBounds().reduced(listEntry ? 8 : 4, 0);
    g.drawText(button.getButtonText(), area, listEntry ? juce::Justification::centredLeft : juce::Justification::centred, true);
}

void SamplerLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 6.0f);
}

void SamplerLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;
    const bool focus = editor.hasKeyboardFocus(true) && ! editor.isReadOnly();
    g.setColour(editor.findColour(focus ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f, 6.0f, 1.0f);
}

void SamplerLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                       int thumbStart, int thumbSize, bool mouseOver, bool mouseDown)
{
    juce::ignoreUnused(x, y);
    auto thumb = vertical ? juce::Rectangle<int>(width - 6, thumbStart, 4, thumbSize) : juce::Rectangle<int>(thumbStart, height - 6, thumbSize, 4);
    g.setColour(mouseOver || mouseDown ? theme::dashed : theme::border);
    g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
}
} // namespace sampler::ui
