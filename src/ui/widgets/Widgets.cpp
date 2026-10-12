#include "Widgets.h"

#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui
{
namespace
{
constexpr double dragPixelsForFullRange = 160.0;

/** Drag position of a vertical drag: pixels moved up since the mouse went down. */
double movedUp(const juce::MouseEvent& e) { return static_cast<double>(e.getMouseDownPosition().y - e.getPosition().y); }

double applyDetent(double v, const std::optional<double>& at, double width)
{
    return at.has_value() && std::abs(v - *at) <= width ? *at : v;
}
} // namespace

//==============================================================================
ToggleButton::ToggleButton(const juce::String& label) : juce::Button(label)
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ToggleButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? litColour : (down || highlighted ? theme::selected : theme::raised));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(on ? litColour : theme::border);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    g.setColour(on ? theme::background : theme::text);
    g.setFont(Fonts::sans(12.0f, Fonts::Weight::semiBold));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void SegmentedControl::setItems(const juce::StringArray& newItems)
{
    items = newItems;
    selectedIndex = juce::jlimit(0, juce::jmax(0, items.size() - 1), selectedIndex);
    repaint();
}

void SegmentedControl::setSelected(int index, bool notify)
{
    if (! juce::isPositiveAndBelow(index, items.size()))
        return;
    const bool changed = index != selectedIndex;
    selectedIndex = index;
    repaint();
    if (notify && changed && onChange)
        onChange(index);
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e)
{
    if (const auto index = itemAt(e.getPosition()); index >= 0)
        setSelected(index, true);
}

void SegmentedControl::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour(theme::background);
    g.fillRoundedRectangle(area, 8.0f);
    area = area.reduced(3.0f);
    if (items.isEmpty())
        return;
    const float width = area.getWidth() / static_cast<float>(items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        auto cell = area.withX(area.getX() + width * static_cast<float>(i)).withWidth(width);
        const bool on = i == selectedIndex;
        if (on)
        {
            g.setColour(theme::accent);
            g.fillRoundedRectangle(cell, 6.0f);
        }
        g.setColour(on ? theme::background : theme::muted);
        g.setFont(Fonts::sans(13.0f, on ? Fonts::Weight::semiBold : Fonts::Weight::medium));
        g.drawText(items[i], cell.toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
ValueField::ValueField()
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ValueField::setSuffix(const juce::String& s, int decimalPlaces)
{
    suffix = s;
    decimals = decimalPlaces;
    repaint();
}

void ValueField::setValue(double v, bool notify)
{
    const auto snapped = range.snap(v);
    const bool changed = snapped != value;
    value = snapped;
    repaint();
    if (notify && changed && onChange)
        onChange(value);
}

juce::String ValueField::text() const { return formatter ? formatter(value) : juce::String(value, decimals) + suffix; }

bool ValueField::commitText(const juce::String& typed)
{
    if (parser)
    {
        const auto parsed = parser(typed);
        if (! parsed)
            return false;
        setValue(*parsed, true);
        return true;
    }
    auto cleaned = typed.trim();
    if (suffix.isNotEmpty() && cleaned.endsWithIgnoreCase(suffix))
        cleaned = cleaned.dropLastCharacters(suffix.length()).trim();
    if (cleaned.isEmpty() || ! cleaned.containsOnly("0123456789.-+"))
        return false;
    setValue(cleaned.getDoubleValue(), true);
    return true;
}

void ValueField::mouseDown(const juce::MouseEvent&)
{
    if (! isEnabled())
        return;
    dragStart = range.toNormalised(value);
    dragging = true;
    moved = false;
    if (onDragStart)
        onDragStart();
}

void ValueField::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragEnabled || ! isEnabled())
        return;
    if (e.getDistanceFromDragStart() > 2)
        moved = true;
    const auto raw = range.fromNormalised(normalisedAfterDrag(dragStart, movedUp(e), dragPixelsForFullRange, e.mods.isShiftDown()));
    setValue(applyDetent(raw, detentValue, detentWidth), true);
}

void ValueField::mouseUp(const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    if (onDragEnd)
        onDragEnd();
    if ((clickToType || ! dragEnabled) && ! moved && e.getNumberOfClicks() == 1)
        showEditor();
}

void ValueField::mouseDoubleClick(const juce::MouseEvent&)
{
    if (doubleClickResets)
    {
        setValue(range.defaultValue, true);
        return;
    }
    showEditor();
}

void ValueField::showEditor()
{
    if (editor != nullptr)
        return;
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont(Fonts::mono(fontHeight, true));
    editor->setJustification(justification);
    editor->setText(formatter ? text() : juce::String(value, decimals), false);
    editor->addListener(this);
    addAndMakeVisible(*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
}

void ValueField::textEditorReturnKeyPressed(juce::TextEditor& e)
{
    commitText(e.getText());
    closeEditor();
}

void ValueField::closeEditor()
{
    if (editor == nullptr)
        return;
    // Deleting the editor from inside its own callback is not allowed: let the call return first.
    auto* raw = editor.release();
    raw->removeListener(this);
    raw->setVisible(false);
    juce::MessageManager::callAsync([raw] { delete raw; });
    repaint();
}

void ValueField::resized()
{
    if (editor != nullptr)
        editor->setBounds(getLocalBounds().reduced(2));
}

void ValueField::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (drawBox)
    {
        g.setColour(theme::background);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(theme::border);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
    if (editor == nullptr)
    {
        g.setColour(isEnabled() ? theme::text : theme::muted.withAlpha(0.6f));
        g.setFont(Fonts::mono(fontHeight, true));
        g.drawText(text(), getLocalBounds(), justification);
    }
}

//==============================================================================
void Knob::setValue(double v, bool notify)
{
    const auto snapped = range.snap(v);
    const bool changed = snapped != value;
    value = snapped;
    repaint();
    if (notify && changed && onChange)
        onChange(value);
}

void Knob::mouseDown(const juce::MouseEvent&)
{
    dragStart = range.toNormalised(value);
    dragging = true;
    if (onDragStart)
        onDragStart();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    const auto raw = range.fromNormalised(normalisedAfterDrag(dragStart, movedUp(e), dragPixelsForFullRange, e.mods.isShiftDown()));
    setValue(applyDetent(raw, detentValue, detentWidth), true);
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (onDragEnd)
        onDragEnd();
}

void Knob::mouseDoubleClick(const juce::MouseEvent&) { setValue(range.defaultValue, true); }

void Knob::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (label.isNotEmpty())
    {
        g.setColour(theme::muted);
        g.setFont(Fonts::sans(10.0f));
        g.drawText(label.toUpperCase(), getLocalBounds().removeFromBottom(14), juce::Justification::centred);
        area.removeFromBottom(14.0f);
    }
    const auto diameter = juce::jmin(area.getWidth(), area.getHeight()) - 6.0f;
    const auto dial = area.withSizeKeepingCentre(diameter, diameter);
    const float startAngle = juce::MathConstants<float>::pi * 1.25f;
    const float endAngle = juce::MathConstants<float>::pi * 2.75f;
    const float angle = startAngle + static_cast<float>(range.toNormalised(value)) * (endAngle - startAngle);

    g.setColour(theme::raised);
    g.fillEllipse(dial);
    juce::Path track;
    track.addCentredArc(dial.getCentreX(), dial.getCentreY(), diameter * 0.5f - 2.0f, diameter * 0.5f - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour(theme::border);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path filled;
    filled.addCentredArc(dial.getCentreX(), dial.getCentreY(), diameter * 0.5f - 2.0f, diameter * 0.5f - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour(theme::accent);
    g.strokePath(filled, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.5f - 7.0f;
    g.setColour(theme::text);
    g.drawLine({centre, centre.getPointOnCircumference(radius, angle)}, 2.0f);
}

//==============================================================================
void Fader::setValue(double v, bool notify)
{
    const auto snapped = range.snap(v);
    const bool changed = snapped != value;
    value = snapped;
    repaint();
    if (notify && changed && onChange)
        onChange(value);
}

void Fader::mouseDown(const juce::MouseEvent&) { dragStart = range.toNormalised(value); }

void Fader::mouseDrag(const juce::MouseEvent& e)
{
    const auto travel = juce::jmax(1.0, static_cast<double>(getHeight()) - 20.0);
    setValue(range.fromNormalised(normalisedAfterDrag(dragStart, movedUp(e), travel, e.mods.isShiftDown())), true);
}

void Fader::mouseDoubleClick(const juce::MouseEvent&) { setValue(range.defaultValue, true); }

void Fader::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float trackWidth = 6.0f;
    const auto track = juce::Rectangle<float>(area.getCentreX() - trackWidth * 0.5f, area.getY() + 10.0f, trackWidth, area.getHeight() - 20.0f);
    g.setColour(theme::background);
    g.fillRoundedRectangle(track, 3.0f);
    const float fraction = static_cast<float>(range.toNormalised(value));
    const float y = track.getBottom() - fraction * track.getHeight();
    g.setColour(theme::accent);
    g.fillRoundedRectangle(track.withTop(y), 3.0f);
    const auto cap = juce::Rectangle<float>(area.getCentreX() - 14.0f, y - 8.0f, 28.0f, 16.0f);
    g.setColour(theme::raised);
    g.fillRoundedRectangle(cap, 4.0f);
    g.setColour(theme::border);
    g.drawRoundedRectangle(cap, 4.0f, 1.0f);
    g.setColour(theme::text);
    g.drawHorizontalLine(static_cast<int>(cap.getCentreY()), cap.getX() + 5.0f, cap.getRight() - 5.0f);
}

//==============================================================================
void Meter::tick(double elapsedMs)
{
    hold.update(levelDb, elapsedMs);
    repaint();
}

void Meter::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour(theme::background);
    g.fillRoundedRectangle(area, 3.0f);
    const auto inner = area.reduced(1.0f);
    const float fraction = static_cast<float>(dbToFraction(levelDb));
    const auto bar = inner.withTop(inner.getBottom() - fraction * inner.getHeight());
    g.setColour(levelDb > -3.0 ? theme::record : levelDb > -12.0 ? theme::trackColours[3] : theme::ok);
    g.fillRect(bar);
    const float peakY = inner.getBottom() - static_cast<float>(dbToFraction(hold.peakDb())) * inner.getHeight();
    g.setColour(theme::text);
    g.fillRect(inner.getX(), peakY - 1.0f, inner.getWidth(), 2.0f);
}

//==============================================================================
void Pad::trigger(float velocity)
{
    flashLevel = juce::jlimit(0.0f, 1.0f, velocity);
    startTimerHz(60);
    repaint();
    if (onTrigger)
        onTrigger(flashLevel);
}

void Pad::mouseDown(const juce::MouseEvent& e)
{
    // Pressing lower on the pad plays softer: a simple velocity from the hit position.
    const float height = static_cast<float>(juce::jmax(1, getHeight()));
    trigger(juce::jlimit(0.3f, 1.0f, 1.0f - 0.7f * static_cast<float>(e.y) / height));
}

void Pad::timerCallback()
{
    flashLevel *= 0.82f;
    if (flashLevel < 0.02f)
    {
        flashLevel = 0.0f;
        stopTimer();
    }
    repaint();
}

void Pad::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(theme::raised.interpolatedWith(theme::accent, flashLevel));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(theme::border);
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    g.setColour(flashLevel > 0.5f ? theme::background : theme::muted);
    g.setFont(Fonts::sans(12.0f, Fonts::Weight::medium));
    g.drawText(label, getLocalBounds(), juce::Justification::centred);
}
} // namespace sampler::ui
