#include "ClipInspector.h"

#include "ArrangeLogic.h"
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
constexpr int warpRowHeight = 56;
constexpr int labelHeight = 16;
} // namespace

juce::String ClipInspector::barsText(double startBeat, double endBeat, int beatsPerBar)
{
    const auto perBar = static_cast<double>(std::max(1, beatsPerBar));
    const auto first = static_cast<int>(std::floor(startBeat / perBar + 1.0e-9)) + 1;
    const auto last = std::max(first, static_cast<int>(std::ceil(endBeat / perBar - 1.0e-9)));
    return "Bars " + juce::String(first) + juce::String::fromUTF8("\xe2\x80\x93") + juce::String(last);
}

std::vector<juce::Component*> ClipInspector::fields()
{
    return {&gain,       &start,    &length,     &fadeIn, &fadeOut,   &loopButton, &reverse,
            &openEditor, &warpToggle, &mode, &bpm,    &halveButton, &doubleButton, &transpose};
}

ClipInspector::ClipInspector(const engine::Arrangement& a, commands::CommandService& c, SelectionModel& s, ThumbnailFactory factory,
                             SoundTempo tempo)
    : arrangement(a), commands(c), selection(s), makeThumbnail(std::move(factory)), soundTempo(std::move(tempo))
{
    for (auto* child : fields())
        addChildComponent(child);

    for (auto* field : {&gain, &start, &length, &fadeIn, &fadeOut})
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
    gain.onDragEnd = [this] {
        commands.commit();
        refresh(); // the field shows what the clip has now
    };
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

    // LENGTH is typed ("8", "1.2" = 1 bar 2 beats, "3 beats"); a looped clip may be longer than its sound.
    length.dragEnabled = false;
    length.setRange({0.001, 100000.0, 0.0, 0.0});
    length.formatter = [this](double beats) { return InspectorLogic::lengthText(beats, arrangement.timeSig().numerator); };
    length.parser = [this](const juce::String& text) { return InspectorLogic::parseLength(text, arrangement.timeSig().numerator); };
    length.setTooltip("Length in bars (type 8, or 1.2 for one bar and two beats); without LOOP it stops at the end of the sound");
    length.onChange = [this](double beats) {
        if (! clip)
            return;
        commands::SetClipLength command(clip->id, beats);
        commands.apply(command);
        refresh();
    };

    loopButton.setWantsKeyboardFocus(false);
    loopButton.setTooltip("Repeat what the clip plays now for as long as the clip is");
    loopButton.onClick = [this] {
        if (! clip)
            return;
        commands::SetClipLoop command(clip->id, loopButton.getToggleState());
        commands.apply(command);
        refresh();
    };

    // FADE IN / FADE OUT in milliseconds, linear; a drag is one undo step, a typed value is cut so that both fit in the clip.
    for (auto* fade : {&fadeIn, &fadeOut})
    {
        fade->setRange({0.0, 1000.0, 0.0, 1.0});
        fade->formatter = [](double ms) { return InspectorLogic::msText(ms); };
        fade->parser = [](const juce::String& text) { return InspectorLogic::parseMs(text); };
        fade->onDragStart = [this] { commands.begin("Set clip fades"); };
        fade->onDragEnd = [this] {
            commands.commit();
            refresh(); // a fade that was cut to fit shows the cut value
        };
    }
    const auto sendFades = [this](InspectorLogic::FadeValues fades, bool dragging) {
        commands::SetClipFades command(clip->id, fades.inMs / 1000.0, fades.outMs / 1000.0);
        if (dragging)
            commands.update(command);
        else
        {
            commands.apply(command);
            refresh();
        }
    };
    fadeIn.setTooltip("Fade in, in milliseconds (every clip also has a 3 ms click-free fade)");
    fadeOut.setTooltip("Fade out, in milliseconds (every clip also has a 3 ms click-free fade)");
    fadeIn.onChange = [this, sendFades](double ms) {
        if (clip)
            sendFades(InspectorLogic::withFadeIn(ms, clip->fadeOut * 1000.0, clip->lengthSeconds * 1000.0), fadeIn.isDragging());
    };
    fadeOut.onChange = [this, sendFades](double ms) {
        if (clip)
            sendFades(InspectorLogic::withFadeOut(ms, clip->fadeIn * 1000.0, clip->lengthSeconds * 1000.0), fadeOut.isDragging());
    };
    openEditor.onClick = [this] {
        if (clip && ! clip->missing && onOpenEditor)
            onOpenEditor(clip->sourceId);
    };

    reverse.setWantsKeyboardFocus(false);
    reverse.onClick = [this] {
        if (! clip)
            return;
        commands::SetClipReversed command(clip->id, reverse.getToggleState());
        commands.apply(command);
    };

    // The warp row. Which fields work is decided by InspectorLogic; every change is one command, so one undo step.
    warpToggle.setWantsKeyboardFocus(false);
    warpToggle.onClick = [this] {
        if (! clip)
            return;
        const bool on = warpToggle.getToggleState();
        applyWarp(on, clip->warp.mode, on ? warp.tempoForWarpOn : clip->warp.sourceBpm);
    };

    mode.setItems(InspectorLogic::modeNames());
    mode.setItemEnabled(InspectorLogic::modeTexture, false);
    mode.setTooltip("How the clip is stretched to the project tempo");
    mode.onChange = [this](int index) {
        if (const auto chosen = InspectorLogic::modeAt(index); chosen && clip)
            applyWarp(true, *chosen, clip->warp.sourceBpm);
    };

    bpm.dragEnabled = false;
    bpm.drawBox = false;
    bpm.justification = juce::Justification::centredLeft;
    bpm.setRange({0.0, engine::WarpSettings::maxBpm, 0.0, 0.0});
    bpm.formatter = [](double v) { return v > 0.0 ? InspectorLogic::bpmText(v) : juce::String::fromUTF8("\xe2\x80\x93"); };
    bpm.parser = [](const juce::String& text) { return InspectorLogic::parseBpm(text); };
    bpm.setTooltip("The clip's own tempo: type a value, or use the halve and double buttons");
    bpm.onChange = [this](double v) {
        if (clip && v > 0.0)
            applyWarp(true, clip->warp.mode, v);
    };
    halveButton.setTooltip("Halve the clip's tempo");
    halveButton.onClick = [this] {
        if (clip)
            if (const auto v = InspectorLogic::halved(clip->warp.sourceBpm))
                applyWarp(true, clip->warp.mode, *v);
    };
    doubleButton.setTooltip("Double the clip's tempo");
    doubleButton.onClick = [this] {
        if (clip)
            if (const auto v = InspectorLogic::doubled(clip->warp.sourceBpm))
                applyWarp(true, clip->warp.mode, *v);
    };

    transpose.drawBox = false;
    transpose.justification = juce::Justification::centredLeft;
    transpose.setRange({-engine::WarpSettings::maxTranspose, engine::WarpSettings::maxTranspose, 0.0, 0.0});
    transpose.formatter = [](double v) { return InspectorLogic::transposeText(v); };
    transpose.parser = [](const juce::String& text) { return InspectorLogic::parseTranspose(text); };
    transpose.onDragStart = [this] { commands.begin("Set clip transpose"); };
    transpose.onDragEnd = [this] {
        commands.commit();
        refresh(); // a drag moves in whole semitones: the field shows the rounded value
    };
    transpose.onChange = [this](double v) {
        if (! clip)
            return;
        // A drag moves in whole semitones; a typed value (a slice's fractional pitch) is kept as typed.
        commands::SetClipTranspose command(clip->id, transpose.isDragging() ? std::round(v) : v);
        if (transpose.isDragging())
            commands.update(command);
        else
            commands.apply(command);
    };

    const_cast<engine::Arrangement&>(arrangement).addChangeListener(this);
    refresh();
}

void ClipInspector::applyWarp(bool on, engine::WarpMode warpMode, double sourceBpm)
{
    if (! clip)
        return;
    commands::SetClipWarp command(clip->id, on, warpMode, sourceBpm);
    commands.apply(command);
    refresh(); // a refused change (WARP needs a tempo) leaves the controls as they were
}

void ClipInspector::refreshWarp()
{
    const auto tempo = soundTempo ? soundTempo(clip->sourceId) : 0.0;
    warp = InspectorLogic::stateFor(clip->warp, tempo);

    warpToggle.setToggleState(warp.warpOn, juce::dontSendNotification);
    warpToggle.setEnabled(warp.warpEnabled);
    warpToggle.setTooltip(warp.warpHint.isNotEmpty() ? warp.warpHint : juce::String("Play the clip in the project tempo"));
    mode.setEnabled(warp.modeEnabled);
    mode.setSelected(InspectorLogic::indexOf(clip->warp.mode), false);
    bpm.setEnabled(warp.bpmEnabled);
    if (! bpm.isEditing())
    {
        const auto shown = InspectorLogic::isTempoInRange(clip->warp.sourceBpm) ? clip->warp.sourceBpm
                                                                                : (warp.tempoForWarpOn > 0.0 ? warp.tempoForWarpOn : 0.0);
        bpm.setValue(shown, false);
    }
    halveButton.setEnabled(warp.halveEnabled);
    doubleButton.setEnabled(warp.doubleEnabled);
    transpose.setEnabled(warp.transposeEnabled);
    if (! transpose.isDragging() && ! transpose.isEditing())
        transpose.setValue(clip->warp.transpose, false);
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
        if (! length.isEditing())
            length.setValue(clip->length, false);
        loopButton.setToggleState(clip->loop, juce::dontSendNotification);
        // The fades go up to the clip's length; a drag keeps the range it began with.
        for (auto* fade : {&fadeIn, &fadeOut})
            if (! fade->isDragging() && ! fade->isEditing())
            {
                fade->setRange({0.0, std::max(1.0, clip->lengthSeconds * 1000.0), 0.0, 1.0});
                fade->setValue((fade == &fadeIn ? clip->fadeIn : clip->fadeOut) * 1000.0, false);
            }
        reverse.setToggleState(clip->reversed, juce::dontSendNotification);
        openEditor.setEnabled(! clip->missing);
        openEditor.setTooltip(clip->missing ? "The sound's file is missing" : "Opens the clip's sound in the sample editor");
        refreshWarp();
    }
    else
    {
        title = ids.empty() ? juce::String("No clip selected") : juce::String(static_cast<int>(ids.size())) + " clips selected";
        detail = {};
        warp = {};
    }
    for (auto* child : fields())
        child->setVisible(clip.has_value());
    repaint();
}

int ClipInspector::controlsTop() const { return headerRowHeight + warpRowHeight; }

std::array<juce::Rectangle<int>, 4> ClipInspector::warpCells() const
{
    constexpr int widths[] = {76, 300, 210, 112};
    constexpr int gap = 12;
    const auto row = getLocalBounds().withTrimmedTop(headerRowHeight).removeFromTop(warpRowHeight).reduced(16, 4);
    std::array<juce::Rectangle<int>, 4> cells;
    int x = row.getX();
    for (size_t i = 0; i < cells.size(); ++i)
    {
        cells[i] = juce::Rectangle<int>(x, row.getY(), widths[i], row.getHeight());
        x += widths[i] + gap;
    }
    return cells;
}

juce::Rectangle<int> ClipInspector::valueCell(int column, int row) const
{
    const auto grid = getLocalBounds().withTrimmedTop(controlsTop()).reduced(16).withWidth(valueGridWidth);
    const auto cellW = (grid.getWidth() - 2 * 8) / 3;
    const auto cellH = std::max(20, (grid.getHeight() - 8) / 2);
    return {grid.getX() + column * (cellW + 8), grid.getY() + row * (cellH + 8), cellW, cellH};
}

juce::Rectangle<int> ClipInspector::waveArea() const
{
    return getLocalBounds().withTrimmedTop(controlsTop()).reduced(16).withTrimmedLeft(valueGridWidth + 16);
}

void ClipInspector::resized()
{
    auto header = getLocalBounds().removeFromTop(headerRowHeight).reduced(16, 8);
    openEditor.setBounds(header.removeFromRight(180));
    header.removeFromRight(8);
    reverse.setBounds(header.removeFromRight(84));

    const auto cells = warpCells();
    const auto control = [](juce::Rectangle<int> cell) { return cell.withTrimmedTop(labelHeight); };
    warpToggle.setBounds(control(cells[0]));
    mode.setBounds(control(cells[1]));
    auto bpmCell = control(cells[2]);
    doubleButton.setBounds(bpmCell.removeFromRight(40));
    bpmCell.removeFromRight(4);
    halveButton.setBounds(bpmCell.removeFromRight(40));
    bpmCell.removeFromRight(6);
    bpm.setBounds(bpmCell);
    transpose.setBounds(control(cells[3]));

    const auto cell = [this](int column, int row) { return valueCell(column, row).reduced(10, 0).withTrimmedTop(18).withTrimmedBottom(4); };
    gain.setBounds(cell(0, 0));
    start.setBounds(cell(1, 0));
    length.setBounds(cell(2, 0));
    fadeIn.setBounds(cell(0, 1));
    fadeOut.setBounds(cell(1, 1));
    loopButton.setBounds(cell(2, 1));
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

    // The warp row: a small label over each control; the BPM and TRANSPOSE values sit on a raised cell like the value cells below.
    {
        const auto cells = warpCells();
        const char* titles[] = {"WARP", "MODE", "BPM", "TRANSPOSE"};
        const bool enabled[] = {warp.warpEnabled, warp.modeEnabled, warp.bpmEnabled, warp.transposeEnabled};
        for (size_t i = 0; i < cells.size(); ++i)
        {
            auto font = Fonts::sans(10.0f);
            font.setExtraKerningFactor(0.08f);
            g.setFont(font);
            g.setColour(theme::muted.withAlpha(enabled[i] ? 1.0f : 0.5f));
            g.drawText(titles[i], cells[i].withHeight(labelHeight - 2), juce::Justification::bottomLeft);
        }
        for (auto* field : {&bpm, &transpose})
        {
            g.setColour(theme::raised.withAlpha(field->isEnabled() ? 1.0f : 0.5f));
            g.fillRoundedRectangle(field->getBounds().toFloat(), 6.0f);
        }
        if (warp.warpHint.isNotEmpty())
        {
            g.setColour(theme::muted);
            g.setFont(Fonts::sans(11.0f));
            g.drawText(warp.warpHint, cells[1].withHeight(labelHeight - 2), juce::Justification::bottomRight, true);
        }
        g.setColour(theme::border);
        g.drawHorizontalLine(controlsTop() - 1, 0.0f, static_cast<float>(getWidth()));
    }

    // The six value cells: GAIN, START, LENGTH / FADE IN, FADE OUT, LOOP.
    const char* labels[] = {"GAIN", "START", "LENGTH", "FADE IN", "FADE OUT", "LOOP"};
    for (int i = 0; i < 6; ++i)
    {
        const auto r = valueCell(i % 3, i / 3);
        g.setColour(theme::raised);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        auto font = Fonts::sans(10.0f);
        font.setExtraKerningFactor(0.08f);
        g.setFont(font);
        g.setColour(theme::muted);
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
    const auto r = area.reduced(4);
    if (auto* thumb = found->second.get(); thumb != nullptr && thumb->getTotalLength() > 0.0)
    {
        // One piece per pass of a looped clip, each in its own rectangle (mirrored when reversed), with a mark at every repeat.
        const auto pieces = waveformPieces(*clip);
        for (const auto& piece : pieces)
        {
            const auto x0 = r.getX() + juce::roundToInt(piece.from * r.getWidth());
            const auto x1 = r.getX() + juce::roundToInt(piece.to * r.getWidth());
            const auto box = r.withLeft(x0).withRight(std::max(x0 + 1, x1));
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(box);
            if (piece.region.mirrored)
                g.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(static_cast<float>(box.getX() * 2 + box.getWidth()), 0.0f));
            g.setColour(colour);
            const auto from = std::max(0.0, piece.region.startSeconds);
            thumb->drawChannel(g, box, from, std::max(from, piece.region.startSeconds + piece.region.lengthSeconds), 0, 1.0f);
        }
        g.setColour(theme::text.withAlpha(0.45f));
        for (size_t i = 1; i < pieces.size(); ++i)
            g.drawVerticalLine(r.getX() + juce::roundToInt(pieces[i].from * r.getWidth()), static_cast<float>(r.getY()), static_cast<float>(r.getBottom()));
    }

    // The fades: a line from the foot to the top of the fade, and the part of the picture above it dimmed.
    if (clip->lengthSeconds > 0.0)
    {
        const auto rf = r.toFloat();
        const auto inW = static_cast<float>(clip->fadeIn / clip->lengthSeconds) * rf.getWidth();
        const auto outW = static_cast<float>(clip->fadeOut / clip->lengthSeconds) * rf.getWidth();
        const auto drawFade = [&](juce::Point<float> foot, juce::Point<float> top, juce::Point<float> corner) {
            juce::Path shade;
            shade.addTriangle(foot, top, corner);
            g.setColour(theme::background.withAlpha(0.6f));
            g.fillPath(shade);
            g.setColour(theme::text.withAlpha(0.85f));
            g.drawLine({foot, top}, 1.5f);
        };
        if (inW >= 1.0f)
            drawFade({rf.getX(), rf.getBottom()}, {rf.getX() + inW, rf.getY()}, {rf.getX(), rf.getY()});
        if (outW >= 1.0f)
            drawFade({rf.getRight(), rf.getBottom()}, {rf.getRight() - outW, rf.getY()}, {rf.getRight(), rf.getY()});
    }
}
} // namespace sampler::ui
