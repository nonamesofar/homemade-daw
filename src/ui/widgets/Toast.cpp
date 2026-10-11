#include "Toast.h"

#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui
{
namespace
{
constexpr int rowHeight = 30;
constexpr int rowGap = 6;
constexpr int maxRows = 4;
constexpr int toastWidth = 460;
} // namespace

Toast::Toast()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void Toast::show(const juce::String& text, Kind kind)
{
    active.push_back({text, kind, juce::Time::getMillisecondCounter() + visibleMs});
    while (active.size() > maxRows)
        active.erase(active.begin());
    startTimer(250);
    if (auto* parent = getParentComponent())
        setBounds(boundsFor(parent->getLocalBounds()));
    setVisible(true);
    repaint();
}

juce::Rectangle<int> Toast::boundsFor(juce::Rectangle<int> area) const
{
    const int rows = static_cast<int>(active.size());
    const int height = rows * rowHeight + juce::jmax(0, rows - 1) * rowGap;
    const int width = juce::jmin(toastWidth, area.getWidth() - 32);
    return {area.getCentreX() - width / 2, area.getBottom() - 24 - height, width, height};
}

void Toast::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto before = active.size();
    active.erase(std::remove_if(active.begin(), active.end(), [now](const Message& m) { return static_cast<juce::int32>(now - m.expiresAtMs) >= 0; }),
                 active.end());
    if (active.size() == before)
        return;
    if (active.empty())
    {
        stopTimer();
        setVisible(false);
    }
    else if (auto* parent = getParentComponent())
    {
        setBounds(boundsFor(parent->getLocalBounds()));
    }
    repaint();
}

void Toast::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    for (const auto& message : active)
    {
        auto row = area.removeFromTop(rowHeight).toFloat();
        area.removeFromTop(rowGap);
        const bool error = message.kind == Kind::error;
        g.setColour(error ? theme::errorFill : theme::infoFill);
        g.fillRoundedRectangle(row, 8.0f);
        g.setColour(error ? theme::record : theme::border);
        g.drawRoundedRectangle(row.reduced(0.5f), 8.0f, 1.0f);
        g.setColour(theme::text);
        g.setFont(Fonts::sans(13.0f, Fonts::Weight::medium));
        g.drawText(message.text, row.reduced(14.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}
} // namespace sampler::ui
