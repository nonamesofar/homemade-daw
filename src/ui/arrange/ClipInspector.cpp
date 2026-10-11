#include "ClipInspector.h"

#include "commands/ArrangeCommands.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

#include <cmath>

namespace sampler::ui
{
namespace
{
constexpr double silenceFieldDb = -70.0;
constexpr int headerRowHeight = 52;
} // namespace

juce::String ClipInspector::barsText(double startBeat, double endBeat, int beatsPerBar)
{
    const auto perBar = static_cast<double>(std::max(1, beatsPerBar));
    const auto first = static_cast<int>(std::floor(startBeat / perBar + 1.0e-9)) + 1;
    const auto last = std::max(first, static_cast<int>(std::ceil(endBeat / perBar - 1.0e-9)));
    return "Bars " + juce::String(first) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(last);
}

ClipInspector::ClipInspector(const engine::Arrangement& a, commands::CommandService& c, SelectionModel& s, ThumbnailFactory factory)
    : arrangement(a), commands(c), selection(s), makeThumbnail(std::move(factory))
{
    for (auto* child : {static_cast<juce::Component*>(&gain), static_cast<juce::Component*>(&start),
                        static_cast<juce::Component*>(&fadeIn), static_cast<juce::Component*>(&fadeOut),
                        static_cast<juce::Component*>(&reverse), static_cast<juce::Component*>(&openEditor)})
        addChildComponent(child);

    for (auto* field : {&gain, &start, &fadeIn, &fadeOut})
    {
        field->drawBox = false;
        field->justification = juce::Justification::centredLeft;
    }

    gain.setRange({silenceFieldDb, 24.0, 0.0, 0.1});
    gain.formatter = [](double v) { return formatDb(v <= silenceFieldDb + 0.05 ? engine::Arrangement::silenceDb : v) + " dB"; };
    gain.parser = [](const juce::String& text) -> std::optional<double> {
        const auto t = text.trim().upToFirstOccurrenceOf("dB", false, true).trim();
        if (t.equalsIgnoreCase("-inf"))
            return silenceFieldDb;
        if (t.isEmpty() || ! t.containsOnly("0123456789.-+"))
            return std::nullopt;
        return t.getDoubleValue();
    };
    gain.onDragStart = [this] { commands.begin("Set clip gain"); };
    gain.onDragEnd = [this] { commands.commit(); };
    gain.onChange = [this](double v) {
        if (! clip)
            return;
        commands::SetClipGain command(clip->id, v <= silenceFieldDb + 0.05 ? engine::Arrangement::silenceDb : v);
        if (gain.isDragging())
            commands.update(command);
        else
            commands.apply(command);
    };

    start.dragEnabled = false;
    start.setRange({0.0, 100000.0, 0.0, 0.0});
    start.formatter = [this](double beats) { return PositionText::format(beats, arrangement.timeSig()); };
    start.parser = [this](const juce::String& text) { return PositionText::parse(text, arrangement.timeSig()); };
    start.onChange = [this](double beats) {
        if (! clip)
            return;
        commands::SetClipStart command(clip->id, beats);
        commands.apply(command);
    };

    for (auto* fade : {&fadeIn, &fadeOut})
    {
        fade->formatter = [](double) { return juce::String("0 ms"); };
        fade->setEnabled(false);
        fade->setTooltip("Fades arrive in a later version");
    }
    openEditor.setEnabled(false);
    openEditor.setTooltip("The sample editor arrives in a later version");

    reverse.setWantsKeyboardFocus(false);
    reverse.onClick = [this] {
        if (! clip)
            return;
        commands::SetClipReversed command(clip->id, reverse.getToggleState());
        commands.apply(command);
    };

    const_cast<engine::Arrangement&>(arrangement).addChangeListener(this);
    refresh();
}

ClipInspector::~ClipInspector() { const_cast<engine::Arrangement&>(arrangement).removeChangeListener(this); }

void ClipInspector::refresh()
{
    clip.reset();
    const auto& ids = selection.clips();
    if (ids.size() == 1)
        clip = arrangement.clip(ids.front());

    if (clip)
    {
        const auto track = arrangement.track(clip->trackId);
        colour = track ? track->colour : theme::accent;
        title = clip->name;
        detail = (track ? track->name : juce::String()) + juce::String::fromUTF8(" \xc2\xb7 ")
                 + barsText(clip->start, clip->end(), arrangement.timeSig().numerator);
        if (! gain.isDragging())
            gain.setValue(std::max(silenceFieldDb, clip->gainDb), false);
        if (! start.isEditing())
            start.setValue(clip->start, false);
        start.repaint();
        reverse.setToggleState(clip->reversed, juce::dontSendNotification);
    }
    else
    {
        title = ids.empty() ? juce::String("No clip selected") : juce::String(static_cast<int>(ids.size())) + " clips selected";
        detail = {};
    }
    for (auto* child : {static_cast<juce::Component*>(&gain), static_cast<juce::Component*>(&start),
                        static_cast<juce::Component*>(&fadeIn), static_cast<juce::Component*>(&fadeOut),
                        static_cast<juce::Component*>(&reverse), static_cast<juce::Component*>(&openEditor)})
        child->setVisible(clip.has_value());
    repaint();
}

juce::Rectangle<int> ClipInspector::waveArea() const
{
    return getLocalBounds().withTrimmedTop(headerRowHeight).reduced(16).withTrimmedLeft(232 + 16);
}

void ClipInspector::resized()
{
    auto header = getLocalBounds().removeFromTop(headerRowHeight).reduced(16, 8);
    openEditor.setBounds(header.removeFromRight(180));
    header.removeFromRight(8);
    reverse.setBounds(header.removeFromRight(84));

    auto grid = getLocalBounds().withTrimmedTop(headerRowHeight).reduced(16).withWidth(232);
    const auto cellW = (grid.getWidth() - 8) / 2;
    const auto cellH = std::max(20, (grid.getHeight() - 8) / 2);
    const auto cell = [&](int column, int row) {
        return juce::Rectangle<int>(grid.getX() + column * (cellW + 8), grid.getY() + row * (cellH + 8), cellW, cellH)
            .reduced(10, 0)
            .withTrimmedTop(18)
            .withTrimmedBottom(4);
    };
    gain.setBounds(cell(0, 0));
    start.setBounds(cell(1, 0));
    fadeIn.setBounds(cell(0, 1));
    fadeOut.setBounds(cell(1, 1));
}

void ClipInspector::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.drawHorizontalLine(headerRowHeight, 0.0f, static_cast<float>(getWidth()));

    auto header = getLocalBounds().removeFromTop(headerRowHeight).reduced(16, 0);
    if (! clip)
    {
        g.setColour(theme::muted);
        g.setFont(Fonts::sans(14.0f));
        g.drawText(title, header, juce::Justification::centredLeft);
        return;
    }
    g.setColour(colour);
    g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(header.getX()), header.getCentreY() - 6.0f, 12.0f, 12.0f), 3.0f);
    header.removeFromLeft(26);
    g.setColour(theme::text);
    g.setFont(Fonts::sans(15.0f, Fonts::Weight::semiBold));
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), title) + 14;
    g.drawText(title, header.removeFromLeft(titleWidth), juce::Justification::centredLeft, true);
    g.setColour(theme::muted);
    g.setFont(Fonts::sans(13.0f));
    g.drawText(detail, header.removeFromLeft(320), juce::Justification::centredLeft, true);

    // The four value cells.
    auto grid = getLocalBounds().withTrimmedTop(headerRowHeight).reduced(16).withWidth(232);
    const auto cellW = (grid.getWidth() - 8) / 2;
    const auto cellH = std::max(20, (grid.getHeight() - 8) / 2);
    const char* labels[] = {"GAIN", "START", "FADE IN", "FADE OUT"};
    for (int i = 0; i < 4; ++i)
    {
        const auto r = juce::Rectangle<int>(grid.getX() + (i % 2) * (cellW + 8), grid.getY() + (i / 2) * (cellH + 8), cellW, cellH);
        g.setColour(theme::raised.withAlpha(i >= 2 ? 0.5f : 1.0f));
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        auto font = Fonts::sans(10.0f);
        font.setExtraKerningFactor(0.08f);
        g.setFont(font);
        g.setColour(theme::muted.withAlpha(i >= 2 ? 0.5f : 1.0f));
        g.drawText(labels[i], r.reduced(10, 6).removeFromTop(12), juce::Justification::topLeft);
    }

    // The waveform of the clip's region, in the track colour, mirrored when reversed.
    const auto area = waveArea();
    g.setColour(theme::background);
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    const auto bars = static_cast<int>(std::ceil(clip->length / std::max(1, arrangement.timeSig().numerator)));
    g.setColour(juce::Colour(0xff2c2d33));
    for (int i = 1; i < bars; ++i)
        g.drawVerticalLine(area.getX() + area.getWidth() * i / bars, static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));

    auto found = thumbnails.find(clip->sourceId);
    if (found == thumbnails.end())
        found = thumbnails.emplace(clip->sourceId, makeThumbnail ? makeThumbnail(clip->sourceId, *this) : nullptr).first;
    if (auto* thumb = found->second.get(); thumb != nullptr && thumb->getTotalLength() > 0.0)
    {
        const auto forwardStart = clip->reversed ? clip->sourceSeconds - clip->offsetSeconds - clip->lengthSeconds : clip->offsetSeconds;
        juce::Graphics::ScopedSaveState save(g);
        const auto r = area.reduced(4);
        g.reduceClipRegion(r);
        if (clip->reversed)
            g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(static_cast<float>(r.getX() * 2 + r.getWidth()), 0.0f));
        g.setColour(colour);
        thumb->drawChannel(g, r, std::max(0.0, forwardStart), std::max(0.0, forwardStart + clip->lengthSeconds), 0, 1.0f);
    }
}
} // namespace sampler::ui
