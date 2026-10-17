#include "ArrangementView.h"

#include "model/SamplerState.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace sampler::ui
{
namespace
{
const juce::Colour gridLine{0xff2c2d33};
const juce::Colour clipInk{0xff17181b};
constexpr int scrollBarSize = 10;
constexpr double silenceFieldDb = -70.0;

double fieldToDb(double v) { return v <= silenceFieldDb + 0.05 ? engine::Arrangement::silenceDb : v; }
double dbToField(double db) { return std::max(silenceFieldDb, db); }

/** Diagonal hatching for missing and rendering clips. */
void hatch(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(r.toNearestInt());
    g.setColour(c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 8.0f)
        g.drawLine(x, r.getBottom(), x + r.getHeight(), r.getY(), 1.5f);
}

std::vector<juce::String> toStrings(const std::vector<std::string>& ids)
{
    std::vector<juce::String> result;
    for (const auto& id : ids)
        result.emplace_back(id);
    return result;
}
} // namespace

//==============================================================================
TrackHeader::TrackHeader(ArrangementView& v) : view(v)
{
    for (auto* b : {static_cast<juce::Component*>(&muteButton), static_cast<juce::Component*>(&soloButton),
                    static_cast<juce::Component*>(&armButton), static_cast<juce::Component*>(&volume),
                    static_cast<juce::Component*>(&pan)})
        addAndMakeVisible(b);

    armButton.litColour = theme::record;
    armButton.setClickingTogglesState(false);
    armButton.setTooltip("Arm for recording; Ctrl+click arms several tracks");
    armButton.setWantsKeyboardFocus(false);
    armButton.onClick = [this] { view.armClicked(info.id, juce::ModifierKeys::getCurrentModifiers().isCommandDown()); };
    muteButton.setWantsKeyboardFocus(false);
    soloButton.setWantsKeyboardFocus(false);
    muteButton.setTooltip("Mute (M)");
    soloButton.setTooltip("Solo (S); Ctrl+click solos only this track");

    using Control = commands::SetTrackParam::Control;
    muteButton.onClick = [this] {
        commands::SetTrackParam command(info.id, Control::mute, muteButton.getToggleState() ? 1.0 : 0.0);
        view.applyCommand(command, false);
    };
    soloButton.onClick = [this] {
        const bool exclusive = juce::ModifierKeys::getCurrentModifiers().isCommandDown();
        commands::SetTrackParam command(info.id, exclusive ? Control::exclusiveSolo : Control::solo,
                                        exclusive || soloButton.getToggleState() ? 1.0 : 0.0);
        view.applyCommand(command, false);
    };

    volume.setRange({silenceFieldDb, 6.0, 0.0, 0.1});
    volume.formatter = [](double v) { return formatDb(fieldToDb(v)) + " dB"; };
    volume.parser = [](const juce::String& text) -> std::optional<double> {
        const auto t = text.trim().upToFirstOccurrenceOf("dB", false, true).trim();
        if (t.equalsIgnoreCase("-inf"))
            return silenceFieldDb;
        if (t.isEmpty() || ! t.containsOnly("0123456789.-+"))
            return std::nullopt;
        return t.getDoubleValue();
    };
    volume.detentValue = 0.0;
    volume.detentWidth = 0.6;
    volume.doubleClickResets = true;
    volume.setTooltip("Volume: drag up or down, double-click for 0 dB");
    volume.onDragStart = [this] { view.context().commands.begin("Set volume"); };
    volume.onDragEnd = [this] { view.context().commands.commit(); };
    volume.onChange = [this](double v) {
        auto command = std::make_unique<commands::SetTrackParam>(info.id, Control::volume, fieldToDb(v));
        if (volume.isDragging())
            view.context().commands.update(*command);
        else
            view.context().commands.apply(*command);
    };

    pan.setRange({-1.0, 1.0, 0.0, 0.0});
    pan.detentValue = 0.0;
    pan.detentWidth = 0.04;
    pan.onDragStart = [this] { view.context().commands.begin("Set pan"); };
    pan.onDragEnd = [this] { view.context().commands.commit(); };
    pan.onChange = [this](double v) {
        auto command = std::make_unique<commands::SetTrackParam>(info.id, Control::pan, v);
        if (pan.isDragging())
            view.context().commands.update(*command);
        else
            view.context().commands.apply(*command);
    };
}

TrackHeader::~TrackHeader()
{
    if (nameEditor != nullptr)
        nameEditor->removeListener(this);
}

void TrackHeader::setTrack(const engine::TrackInfo& track, bool isSelected)
{
    info = track;
    selected = isSelected;
    syncRecordingState();
    muteButton.setToggleState(track.mute, juce::dontSendNotification);
    soloButton.setToggleState(track.solo, juce::dontSendNotification);
    if (! volume.isDragging())
        volume.setValue(dbToField(track.volumeDb), false);
    if (! pan.isDragging())
        pan.setValue(track.pan, false);
    repaint();
}

juce::Rectangle<int> TrackHeader::nameArea() const { return {34, 8, getWidth() - 34 - 58, 24}; }
juce::Rectangle<int> TrackHeader::inputArea() const { return {16, 38, getWidth() - 16 - 26, 20}; }

void TrackHeader::syncRecordingState()
{
    inputLabelText = view.inputLabel(info);
    armButton.setToggleState(view.isArmed(info.id), juce::dontSendNotification);
    repaint();
}
juce::Rectangle<int> TrackHeader::meterArea() const { return {getWidth() - 12, 10, 5, getHeight() - 20}; }

void TrackHeader::resized()
{
    auto row = getLocalBounds().withTrimmedLeft(16).withTrimmedRight(18).removeFromBottom(40).withSizeKeepingCentre(getWidth() - 34, 26);
    muteButton.setBounds(row.removeFromLeft(30));
    row.removeFromLeft(4);
    soloButton.setBounds(row.removeFromLeft(30));
    row.removeFromLeft(4);
    armButton.setBounds(row.removeFromLeft(30));
    row.removeFromLeft(6);
    volume.setBounds(row);
    pan.setBounds(getWidth() - 20 - 28, 6, 28, 28);
    if (nameEditor != nullptr)
        nameEditor->setBounds(nameArea());
}

void TrackHeader::paint(juce::Graphics& g)
{
    g.fillAll(selected ? theme::selected : theme::panel);
    g.setColour(theme::border);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
    if (selected)
    {
        g.setColour(info.colour);
        g.fillRect(0, 0, 3, getHeight() - 1);
    }

    g.setColour(info.colour);
    g.fillRoundedRectangle(juce::Rectangle<float>(16.0f, 14.0f, 12.0f, 12.0f), 3.0f);
    if (nameEditor == nullptr)
    {
        g.setColour(theme::text);
        g.setFont(Fonts::sans(14.0f, Fonts::Weight::semiBold));
        g.drawText(info.name, nameArea(), juce::Justification::centredLeft, true);
    }

    // What the track records, under the name; click opens the input menu.
    g.setColour(theme::muted);
    g.setFont(Fonts::sans(12.0f));
    g.drawText(inputLabelText + "  " + juce::String::charToString(0x25be), inputArea(), juce::Justification::centredLeft, true);

    // Meter: the falling peak, the RMS as a darker inner bar, the held peak as a line.
    const auto m = meterArea().toFloat();
    g.setColour(theme::background);
    g.fillRoundedRectangle(m, 2.0f);
    const auto heightFor = [&m](double db) { return m.getHeight() * static_cast<float>(dbToFraction(db, MeterBallistics::floorDb)); };
    const auto peakH = heightFor(ballistics.levelDb());
    g.setColour(theme::ok.withAlpha(0.9f));
    g.fillRect(m.withTop(m.getBottom() - peakH));
    const auto rmsH = heightFor(ballistics.rmsDb());
    g.setColour(theme::ok.darker(0.6f));
    g.fillRect(m.reduced(1.0f, 0.0f).withTop(m.getBottom() - rmsH));
    if (ballistics.heldDb() > MeterBallistics::floorDb)
    {
        g.setColour(ballistics.heldDb() > -1.0 ? theme::record : theme::text);
        g.fillRect(m.withTop(m.getBottom() - heightFor(ballistics.heldDb())).withHeight(1.5f));
    }
}

void TrackHeader::pushLevel(const engine::LevelReading& reading, double elapsedMs)
{
    const auto before = std::make_tuple(ballistics.levelDb(), ballistics.heldDb(), ballistics.rmsDb());
    ballistics.push(std::max(reading.peakDb[0], reading.peakDb[1]), elapsedMs, reading.over);
    if (before != std::make_tuple(ballistics.levelDb(), ballistics.heldDb(), ballistics.rmsDb()))
        repaint(meterArea());
}

void TrackHeader::mouseDown(const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && inputArea().contains(e.getPosition()))
    {
        view.showInputMenu(info.id);
        return;
    }
    view.context().selection.selectHeader(info.id);
    view.headers().repaint();
    for (int i = 0; i < view.headers().numHeaders(); ++i)
        if (auto* h = view.headers().header(i))
            h->setTrack(h->track(), h == this);

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu colours;
        for (int i = 0; i < engine::trackPaletteSize; ++i)
            colours.addColouredItem(100 + i, juce::String::repeatedString(juce::String::charToString(0x25a0), 3),
                                    engine::trackPaletteColour(i), true, engine::trackPaletteColour(i) == info.colour);
        juce::PopupMenu menu;
        menu.addItem(1, "Rename track");
        menu.addSubMenu("Colour", colours);
        menu.addSeparator();
        menu.addItem(2, "Delete track");
        juce::Component::SafePointer<TrackHeader> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int result) {
            if (safe == nullptr || result == 0)
                return;
            auto& v = safe->view;
            if (result == 1)
                safe->startRename();
            else if (result == 2)
            {
                commands::DeleteTracks command({safe->info.id});
                v.applyCommand(command, false);
            }
            else if (result >= 100)
            {
                commands::SetTrackColour command(safe->info.id, engine::trackPaletteColour(result - 100));
                v.applyCommand(command, false);
            }
        });
        return;
    }
    view.headers().headerPressed(view.trackIndexOf(info.id), e.getEventRelativeTo(&view.headers()).getPosition());
}

void TrackHeader::mouseDrag(const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        view.headers().headerDragged(e.getEventRelativeTo(&view.headers()).getPosition());
}

void TrackHeader::mouseUp(const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        view.headers().headerReleased();
}

void TrackHeader::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (nameArea().contains(e.getPosition()))
        startRename();
}

void TrackHeader::startRename()
{
    if (nameEditor != nullptr)
        return;
    nameEditor = std::make_unique<juce::TextEditor>("rename");
    nameEditor->setFont(Fonts::sans(14.0f, Fonts::Weight::semiBold));
    nameEditor->setText(info.name, false);
    nameEditor->addListener(this);
    addAndMakeVisible(*nameEditor);
    nameEditor->setBounds(nameArea());
    nameEditor->grabKeyboardFocus();
    nameEditor->selectAll();
    repaint();
}

void TrackHeader::finishRename(bool keep)
{
    if (nameEditor == nullptr)
        return;
    const auto text = nameEditor->getText().trim();
    // Deleting the editor inside its own callback is not allowed: let the call return first.
    auto* raw = nameEditor.release();
    raw->removeListener(this);
    raw->setVisible(false);
    juce::MessageManager::callAsync([raw] { delete raw; });
    repaint();
    if (keep && text.isNotEmpty() && text != info.name)
    {
        commands::RenameTrack command(info.id, text);
        view.applyCommand(command, false);
    }
}

//==============================================================================
TrackHeaderList::TrackHeaderList(ArrangementView& v) : view(v)
{
    addAndMakeVisible(addButton);
    addButton.setWantsKeyboardFocus(false);
    addButton.onClick = [this] {
        commands::AddTrack command(view.context().selection.track());
        view.applyCommand(command, false);
        if (! command.resultIds().empty())
            view.context().selection.setTrack(command.resultIds().front());
    };
}

TrackHeader* TrackHeaderList::headerFor(const juce::String& trackId)
{
    for (auto& h : headers)
        if (h->trackId() == trackId)
            return h.get();
    return nullptr;
}

void TrackHeaderList::rebuild()
{
    const auto& tracks = view.trackList();
    std::vector<std::unique_ptr<TrackHeader>> next;
    for (const auto& t : tracks)
    {
        auto found = std::find_if(headers.begin(), headers.end(), [&t](const auto& h) { return h != nullptr && h->trackId() == t.id; });
        std::unique_ptr<TrackHeader> header;
        if (found != headers.end())
            header = std::move(*found);
        else
        {
            header = std::make_unique<TrackHeader>(view);
            addAndMakeVisible(*header);
        }
        header->setTrack(t, t.id == view.context().selection.track());
        next.push_back(std::move(header));
    }
    headers = std::move(next); // headers of removed tracks are deleted here
    layoutHeaders();
}

void TrackHeaderList::layoutHeaders()
{
    const auto& layout = view.layout();
    const auto scroll = view.viewport().scrollY;
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds(0, static_cast<int>(layout.trackTop(static_cast<int>(i)) - scroll), getWidth(), layout.trackHeight);
    const auto addTop = static_cast<int>(layout.trackTop(static_cast<int>(headers.size())) - scroll);
    addButton.setBounds(16, addTop + (layout.addRowHeight - 30) / 2, 124, 30);
}

void TrackHeaderList::headerPressed(int index, juce::Point<int> p)
{
    pressedIndex = index;
    pressPoint = lastPoint = p;
    reorderTarget = -1;
}

void TrackHeaderList::headerDragged(juce::Point<int> p)
{
    if (pressedIndex < 0)
        return;
    lastPoint = p;
    if (reorderTarget < 0 && p.getDistanceFrom(pressPoint) < 4)
        return; // under the threshold it is still a click
    reorderTarget = ui::reorderTargetIndex(view.layout(), numHeaders(), p.y + view.viewport().scrollY);
    if (! isTimerRunning())
        startTimerHz(30);
    repaint();
}

void TrackHeaderList::headerReleased()
{
    stopTimer();
    const auto from = pressedIndex, to = reorderTarget;
    pressedIndex = reorderTarget = -1;
    repaint();
    if (to >= 0 && from >= 0 && to != from)
        if (auto* h = header(from))
        {
            commands::ReorderTrack command(h->trackId(), to);
            view.applyCommand(command, false);
        }
}

void TrackHeaderList::timerCallback()
{
    const auto speed = edgeScrollSpeed(lastPoint.y, getHeight());
    if (speed == 0.0 || reorderTarget < 0)
        return;
    view.scrollBy(0.0, speed);
    reorderTarget = ui::reorderTargetIndex(view.layout(), numHeaders(), lastPoint.y + view.viewport().scrollY);
    repaint();
}

void TrackHeaderList::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));
}

void TrackHeaderList::paintOverChildren(juce::Graphics& g)
{
    if (reorderTarget < 0 || pressedIndex < 0)
        return;
    const auto y = static_cast<float>(reorderMarkerY(view.layout(), pressedIndex, reorderTarget) - view.viewport().scrollY);
    g.setColour(theme::accent);
    g.fillRect(0.0f, y - 1.0f, static_cast<float>(getWidth()), 2.0f);
}

//==============================================================================
TimelineRuler::TimelineRuler(ArrangementView& v) : view(v) {}

bool TimelineRuler::inLoopBar(int y) const { return y < view.layout().loopBarHeight; }

engine::LoopRange TimelineRuler::shownLoop() const
{
    return isDraggingLoop() ? previewLoop : view.context().arrangement.loopRange();
}

void TimelineRuler::cancelDrag()
{
    loopDrag = LoopZone::none;
    drawingLoop = false;
    repaint();
}

void TimelineRuler::paint(juce::Graphics& g)
{
    const auto& layout = view.layout();
    const auto& vp = view.viewport();
    const auto w = static_cast<float>(getWidth());
    g.fillAll(theme::panel);
    g.setColour(theme::background.brighter(0.03f));
    g.fillRect(0, 0, getWidth(), layout.loopBarHeight);

    // Loop range: brighter while looping is on.
    const auto loop = shownLoop();
    if (! loop.isEmpty())
    {
        const bool on = view.context().transport.isLooping();
        const auto x0 = static_cast<float>(vp.beatToX(loop.start)), x1 = static_cast<float>(vp.beatToX(loop.end));
        const auto bar = juce::Rectangle<float>(x0, 1.0f, x1 - x0, static_cast<float>(layout.loopBarHeight) - 2.0f);
        g.setColour(theme::accent.withAlpha(on ? 0.55f : 0.2f));
        g.fillRect(bar);
        g.setColour(theme::accent.withAlpha(on ? 1.0f : 0.5f));
        g.fillRect(bar.withWidth(2.0f));
        g.fillRect(bar.withX(x1 - 2.0f).withWidth(2.0f));
        g.setColour(theme::accent.withAlpha(on ? 0.12f : 0.05f));
        g.fillRect(juce::Rectangle<float>(x0, static_cast<float>(layout.loopBarHeight), x1 - x0, static_cast<float>(layout.rulerHeight)));
    }

    // Bars, then beats when there is room.
    const auto beatsPerBar = static_cast<double>(view.timeSig().numerator);
    const auto step = rulerLabelStep(vp.pixelsPerBeat, beatsPerBar);
    const bool showBeats = vp.pixelsPerBeat >= 40.0;
    const auto firstBar = static_cast<int>(std::floor(vp.firstBeat / beatsPerBar));
    const auto lastBar = static_cast<int>(std::ceil(vp.lastBeat() / beatsPerBar));
    const auto top = static_cast<float>(layout.loopBarHeight);
    g.setFont(Fonts::mono(11.0f));
    for (int bar = std::max(0, firstBar); bar <= lastBar; ++bar)
    {
        const auto x = static_cast<float>(vp.beatToX(bar * beatsPerBar));
        const bool labelled = bar % step == 0;
        g.setColour(theme::border);
        g.drawLine(x, labelled ? top : top + layout.rulerHeight * 0.6f, x, top + static_cast<float>(layout.rulerHeight));
        if (labelled)
        {
            g.setColour(theme::muted);
            g.drawText(juce::String(bar + 1), juce::Rectangle<float>(x + 4.0f, top, 60.0f, static_cast<float>(layout.rulerHeight)),
                       juce::Justification::centredLeft, false);
        }
        if (showBeats)
            for (int beat = 1; beat < view.timeSig().numerator; ++beat)
            {
                const auto bx = static_cast<float>(vp.beatToX(bar * beatsPerBar + beat));
                g.setColour(theme::border.withAlpha(0.6f));
                g.drawLine(bx, top + layout.rulerHeight * 0.7f, bx, top + static_cast<float>(layout.rulerHeight));
                g.setColour(theme::muted.withAlpha(0.6f));
                g.drawText(juce::String(bar + 1) + "." + juce::String(beat + 1),
                           juce::Rectangle<float>(bx + 3.0f, top, 50.0f, static_cast<float>(layout.rulerHeight)),
                           juce::Justification::centredLeft, false);
            }
    }
    g.setColour(theme::border);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, w);
}

void TimelineRuler::mouseMove(const juce::MouseEvent& e)
{
    if (inLoopBar(e.y))
    {
        const auto loop = view.context().arrangement.loopRange();
        const auto zone = hitTestLoop(loop.start, loop.end, view.viewport(), e.x);
        setMouseCursor(zone == LoopZone::start || zone == LoopZone::end ? juce::MouseCursor::LeftRightResizeCursor
                       : zone == LoopZone::body                         ? juce::MouseCursor::DraggingHandCursor
                                                                        : juce::MouseCursor::NormalCursor);
    }
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineRuler::mouseDown(const juce::MouseEvent& e)
{
    const bool bypass = e.mods.isAltDown();
    const auto beat = view.viewport().xToBeat(e.x);
    if (! inLoopBar(e.y))
    {
        view.locate(std::max(0.0, view.snapBeat(beat, bypass)));
        return;
    }
    startLoop = view.context().arrangement.loopRange();
    previewLoop = startLoop;
    loopDrag = hitTestLoop(startLoop.start, startLoop.end, view.viewport(), e.x);
    dragAnchorBeat = beat;
    if (loopDrag == LoopZone::none)
    {
        drawingLoop = true;
        dragAnchorBeat = std::max(0.0, view.snapBeat(beat, bypass));
        const auto [a, b] = drawLoop(dragAnchorBeat, dragAnchorBeat, view.minimumLength());
        previewLoop = {a, b};
    }
    repaint();
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& e)
{
    const bool bypass = e.mods.isAltDown();
    const auto beat = view.viewport().xToBeat(e.x);
    if (! isDraggingLoop())
    {
        if (! inLoopBar(e.getMouseDownY()))
            view.locate(std::max(0.0, view.snapBeat(beat, bypass)));
        return;
    }
    const auto minLength = view.minimumLength();
    if (drawingLoop)
    {
        const auto [a, b] = drawLoop(dragAnchorBeat, std::max(0.0, view.snapBeat(beat, bypass)), minLength);
        previewLoop = {a, b};
    }
    else
    {
        const auto raw = beat - dragAnchorBeat;
        const auto reference = loopDrag == LoopZone::end ? startLoop.end : startLoop.start;
        const auto delta = view.snapBeat(reference + raw, bypass) - reference;
        const auto [a, b] = dragLoop(loopDrag, startLoop.start, startLoop.end, delta, minLength);
        previewLoop = {a, b};
    }
    repaint();
}

void TimelineRuler::mouseUp(const juce::MouseEvent&)
{
    if (! isDraggingLoop())
        return;
    const auto result = previewLoop;
    loopDrag = LoopZone::none;
    drawingLoop = false;
    if (std::abs(result.start - startLoop.start) > 1.0e-9 || std::abs(result.end - startLoop.end) > 1.0e-9)
    {
        commands::SetLoop command(result);
        view.applyCommand(command, false);
    }
    repaint();
}

//==============================================================================
TimelineLanes::TimelineLanes(ArrangementView& v) : view(v)
{
    setWantsKeyboardFocus(true);
    setOpaque(true);
}

double TimelineLanes::contentY(double y) const { return y + view.viewport().scrollY; }

DropTarget TimelineLanes::dropTargetAt(juce::Point<float> p, bool bypassSnap) const
{
    return resolveDrop(view.layout(), view.viewport(), static_cast<int>(view.trackList().size()), p.x, contentY(p.y),
                       [this, bypassSnap](double beat) { return view.snapBeat(beat, bypassSnap); });
}

commands::PlacementTarget TimelineLanes::placementFor(const DropTarget& target) const
{
    const auto& list = view.trackList();
    const auto id = ! target.newTrack && juce::isPositiveAndBelow(target.track, static_cast<int>(list.size()))
                        ? list[static_cast<size_t>(target.track)].id
                        : juce::String();
    return {target.track, target.newTrack, target.beat, id};
}

juce::Rectangle<int> TimelineLanes::boundsOfClip(const juce::String& clipId) const
{
    for (const auto& c : view.laneClips())
        if (juce::String(c.id) == clipId)
        {
            const auto r = clipRect(c, view.layout(), view.viewport());
            return juce::Rectangle<double>(r.x, r.y - view.viewport().scrollY, r.w, r.h).toNearestInt();
        }
    return {};
}

/** The growing takes of a recording: red clips from where the take starts to what has been recorded, with the waveform so far. */
void TimelineLanes::paintLiveTakes(juce::Graphics& g)
{
    if (! view.context().liveTakes)
        return;
    const auto takes = view.context().liveTakes();
    if (takes.empty())
        return;

    const auto& layout = view.layout();
    const auto& vp = view.viewport();
    const auto& arrangement = view.context().arrangement;
    const auto width = static_cast<float>(getWidth());
    const double pixelsPerSecond = vp.pixelsPerBeat * arrangement.tempo() / 60.0;

    for (const auto& take : takes)
        for (const auto& id : take.trackIds)
        {
            const auto t = view.trackIndexOf(id);
            if (t < 0)
                continue;
            const auto top = layout.trackTop(t) - vp.scrollY;
            if (top + layout.trackHeight < 0 || top > getHeight())
                continue;

            const auto beatStart = arrangement.secondsToBeats(take.startSeconds);
            const auto x0 = static_cast<float>(vp.beatToX(beatStart));
            const auto x1 = std::max(x0 + 2.0f, static_cast<float>(vp.beatToX(arrangement.secondsToBeats(take.startSeconds + take.lengthSeconds))));
            if (x1 < 0.0f || x0 > width)
                continue;
            const juce::Rectangle<float> r(x0, static_cast<float>(top) + static_cast<float>(layout.clipInset), x1 - x0,
                                           static_cast<float>(layout.trackHeight - 2 * layout.clipInset));

            // The clip body and its waveform are written straight into a small picture: a thousand one-pixel columns
            // cost far less that way than as drawing calls (four growing takes must paint in under 2 ms).
            const auto box = r.toNearestInt();
            const auto visible = box.getIntersection(juce::Rectangle<int>(0, box.getY(), getWidth(), box.getHeight()));
            if (visible.isEmpty())
                continue;
            juce::Image picture(juce::Image::ARGB, visible.getWidth(), visible.getHeight(), false);
            {
                juce::Image::BitmapData pixels(picture, juce::Image::BitmapData::writeOnly);
                const auto body = theme::background.interpolatedWith(theme::record, 0.22f).getPixelARGB();
                const auto ink = theme::record.getPixelARGB();
                for (int y = 0; y < pixels.height; ++y)
                {
                    auto* row = reinterpret_cast<juce::PixelARGB*>(pixels.getLinePointer(y));
                    for (int x = 0; x < pixels.width; ++x)
                        row[x].set(body);
                }

                if (take.writer != nullptr && take.started)
                {
                    // One column per pixel over the lowest and highest of the buckets that fall into it.
                    const auto& writer = *take.writer;
                    const double bucketSeconds = static_cast<double>(writer.bucketFramesPerBucket()) / writer.options().sampleRate;
                    const int count = writer.numBuckets();
                    const auto mid = static_cast<float>(pixels.height) * 0.5f;
                    const auto half = mid - 3.0f;
                    for (int px = 0; px < pixels.width; ++px)
                    {
                        const auto laneX = static_cast<double>(visible.getX() + px);
                        const auto s0 = (laneX - x0) / pixelsPerSecond;
                        const auto s1 = (laneX + 1.0 - x0) / pixelsPerSecond;
                        const int b0 = std::max(0, static_cast<int>(s0 / bucketSeconds));
                        const int b1 = std::min(count, std::max(b0 + 1, static_cast<int>(s1 / bucketSeconds)));
                        if (b0 >= count)
                            break;
                        const int stride = std::max(1, (b1 - b0) / 8);
                        float lo = 0.0f, hi = 0.0f;
                        for (int b = b0; b < b1; b += stride)
                        {
                            const auto m = writer.bucketAt(b);
                            lo = std::min(lo, m.min);
                            hi = std::max(hi, m.max);
                        }
                        const auto yTop = std::clamp(static_cast<int>(mid - std::min(1.0f, hi) * half), 0, pixels.height - 1);
                        const auto yBottom = std::clamp(std::max(yTop + 1, static_cast<int>(mid - std::max(-1.0f, lo) * half)), 1, pixels.height);
                        for (int y = yTop; y < yBottom; ++y)
                            reinterpret_cast<juce::PixelARGB*>(pixels.getLinePointer(y))[px].set(ink);
                    }
                }
            }
            g.drawImageAt(picture, visible.getX(), visible.getY());
            g.setColour(theme::record);
            g.drawRect(box, 1);

            if (take.warning.isNotEmpty())
            {
                g.setColour(theme::text);
                g.setFont(Fonts::sans(12.0f, Fonts::Weight::semiBold));
                const auto textArea = r.reduced(8.0f, 4.0f).withWidth(std::max(r.getWidth() - 16.0f, 260.0f));
                g.drawText(take.warning, textArea.toNearestInt(), juce::Justification::topLeft, false);
            }
        }
}

void TimelineLanes::paint(juce::Graphics& g)
{
    const auto& layout = view.layout();
    const auto& vp = view.viewport();
    const auto& tracks = view.trackList();
    const auto numTracks = static_cast<int>(tracks.size());
    const auto width = static_cast<float>(getWidth());
    g.fillAll(theme::background);

    // Grid: the snap step when it has room, else beats, else bars (bar lines brighter).
    const auto sig = view.timeSig();
    const auto beatsPerBar = static_cast<double>(sig.numerator);
    auto grid = Snap::gridBeats(view.snapValue(), sig);
    if (grid <= 0.0 || grid * vp.pixelsPerBeat < 6.0)
        grid = 1.0;
    while (grid * vp.pixelsPerBeat < 6.0)
        grid *= 2.0;
    const auto lanesBottom = static_cast<float>(layout.trackTop(numTracks) - vp.scrollY);
    // Lines are laid out per bar, like the snap grid, so bar lines always get one; zoomed out they step in whole bars.
    const bool wholeBars = grid >= beatsPerBar;
    const auto barStride = wholeBars ? std::ceil(grid / beatsPerBar - 1.0e-9) : 1.0;
    const auto drawLine = [&](double beat, bool isBar) {
        g.setColour(isBar ? theme::border : gridLine);
        g.drawVerticalLine(static_cast<int>(std::round(vp.beatToX(beat))), 0.0f, lanesBottom);
    };
    const auto firstBar = std::max(0.0, std::floor(vp.firstBeat / beatsPerBar));
    for (double bar = std::floor(firstBar / barStride) * barStride; bar * beatsPerBar <= vp.lastBeat(); bar += barStride)
    {
        const auto barStart = bar * beatsPerBar;
        drawLine(barStart, true);
        if (! wholeBars)
            for (double beat = barStart + grid; beat < barStart + beatsPerBar - 1.0e-6; beat += grid)
                drawLine(beat, false);
    }
    for (int i = 0; i <= numTracks; ++i)
    {
        g.setColour(theme::border);
        g.drawHorizontalLine(static_cast<int>(layout.trackTop(i) - vp.scrollY) - (i == 0 ? 0 : 1), 0.0f, width);
    }

    // Clips, culled per track by a binary search on the sorted start positions.
    lastDrawnClips = 0;
    ++paintFrame;
    waveBuildsThisPaint = 0;
    bool anyClip = false;
    for (int t = 0; t < numTracks; ++t)
    {
        const auto top = layout.trackTop(t) - vp.scrollY;
        const auto& clips = view.clipsOnTrack(t);
        anyClip = anyClip || ! clips.empty();
        if (top + layout.trackHeight < 0 || top > getHeight())
            continue;
        auto it = std::lower_bound(clips.begin(), clips.end(), vp.firstBeat,
                                   [](const engine::ClipInfo& c, double beat) { return c.end() < beat; });
        for (; it != clips.end() && it->start <= vp.lastBeat(); ++it)
        {
            const auto rect = clipRect({it->id.toStdString(), t, it->start, it->length}, layout, vp);
            paintClip(g, *it, juce::Rectangle<double>(rect.x, rect.y - vp.scrollY, rect.w, rect.h).toFloat(), view.colourOfTrack(t),
                      view.context().selection.isSelected(it->id), false);
            ++lastDrawnClips;
        }
    }

    for (auto it = waveCache.begin(); it != waveCache.end();) // pictures of clips that are gone or far off screen
        it = paintFrame - it->second.usedFrame > 120 ? waveCache.erase(it) : std::next(it);

    // Previews of a move or trim: where the clips will land.
    for (const auto& p : previews)
        if (const auto* clip = view.findClip(p.id))
        {
            const auto rect = clipRect({p.id.toStdString(), p.track, p.start, p.length}, layout, vp);
            paintClip(g, *clip, juce::Rectangle<double>(rect.x, rect.y - vp.scrollY, rect.w, rect.h).toFloat(), view.colourOfTrack(p.track),
                      false, true);
        }

    paintLiveTakes(g);

    // Files being imported onto the timeline: grey boxes, one bar each.
    if (view.context().placeholders)
        for (const auto& ph : view.context().placeholders())
        {
            auto track = ph.target.newTrack ? numTracks : ph.target.trackIndex;
            if (! ph.target.newTrack && ph.target.trackId.isNotEmpty())
                track = view.trackIndexOf(ph.target.trackId);
            if (track < 0)
                continue; // its track is gone: the clips will get a new one, so there is nothing to draw in place
            for (int i = 0; i < ph.count; ++i)
            {
                const auto rect = clipRect({"", track, ph.target.beat + i * beatsPerBar, beatsPerBar}, layout, vp);
                const auto r = juce::Rectangle<double>(rect.x, rect.y - vp.scrollY, rect.w, rect.h).toFloat().reduced(1.0f, 0.0f);
                g.setColour(theme::raised);
                g.fillRoundedRectangle(r, 5.0f);
                g.setColour(theme::muted);
                g.setFont(Fonts::sans(12.0f));
                g.drawText("Importing...", r.reduced(8.0f, 4.0f), juce::Justification::topLeft, true);
            }
        }

    // The ghost of a sound or files being dragged in.
    if (ghost.has_value())
    {
        const auto track = ghost->newTrack ? numTracks : ghost->track;
        const auto rect = clipRect({"", track, ghost->beat, std::max(ghostLengthBeats, 0.25) * ghostCount}, layout, vp);
        const auto r = juce::Rectangle<double>(rect.x, rect.y - vp.scrollY, rect.w, rect.h).toFloat();
        g.setColour(theme::accent.withAlpha(0.25f));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(theme::accent);
        g.drawRoundedRectangle(r, 5.0f, 1.5f);
        g.setColour(theme::text);
        g.setFont(Fonts::sans(12.0f, Fonts::Weight::semiBold));
        g.drawText(ghostLabel, r.reduced(8.0f, 6.0f), juce::Justification::topLeft, true);
    }

    if (snapGuideBeat.has_value())
    {
        g.setColour(theme::accent.withAlpha(0.8f));
        g.drawVerticalLine(static_cast<int>(std::round(vp.beatToX(*snapGuideBeat))), 0.0f, lanesBottom);
    }

    if (mode == Mode::marquee && movedEnough)
    {
        const auto r = juce::Rectangle<float>(downPos, lastPos);
        g.setColour(theme::text.withAlpha(0.08f));
        g.fillRect(r);
        g.setColour(theme::text.withAlpha(0.5f));
        g.drawRect(r, 1.0f);
    }

    // The row under the last lane: the hint while the project has no clips.
    if (! anyClip && ! ghost.has_value())
    {
        const auto row = juce::Rectangle<float>(16.0f, lanesBottom, width - 32.0f, static_cast<float>(layout.addRowHeight));
        g.setColour(theme::muted);
        g.setFont(Fonts::sans(13.0f));
        g.drawText("Drag a sample from the browser onto the timeline to create a clip", row, juce::Justification::centredLeft, true);
    }
}

void TimelineLanes::paintClip(juce::Graphics& g, const engine::ClipInfo& clip, juce::Rectangle<float> r, juce::Colour colour,
                              bool selected, bool ghostOnly)
{
    if (r.getRight() < 0.0f || r.getX() > static_cast<float>(getWidth()))
        return;
    if (ghostOnly)
    {
        g.setColour(colour.withAlpha(0.35f));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(theme::text);
        g.drawRoundedRectangle(r, 5.0f, 1.5f);
        return;
    }
    const auto fill = clip.missing ? theme::raised : (clip.rendering ? colour.withMultipliedSaturation(0.4f) : colour);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 5.0f);
    if (clip.missing || clip.rendering)
        hatch(g, r, clipInk.withAlpha(0.25f));
    else
        paintWaveform(g, clip, r, colour);

    // The name stays readable when the clip starts left of the view.
    auto label = r.withTrimmedLeft(std::max(0.0f, -r.getX())).reduced(8.0f, 6.0f);
    g.setColour(clip.missing ? theme::record : clipInk);
    g.setFont(Fonts::sans(12.0f, Fonts::Weight::semiBold));
    auto name = clip.name;
    if (clip.missing)
        name << " (missing)";
    else if (clip.rendering)
        name << " (rendering)";
    else if (clip.reversed)
        name << " (rev)";
    g.drawText(name, label.withHeight(16.0f), juce::Justification::centredLeft, true);

    if (! clip.missing && ! clip.rendering)
        paintFades(g, clip, r);

    if (selected)
    {
        g.setColour(theme::text);
        g.drawRoundedRectangle(r.expanded(1.0f), 6.0f, 2.0f);
    }
    if (clip.id == hoverId || ((mode == Mode::fadeIn || mode == Mode::fadeOut) && clip.id == grabbedId))
        paintFadeHandles(g, clip, r);
}

void TimelineLanes::paintFades(juce::Graphics& g, const engine::ClipInfo& clip, juce::Rectangle<float> r)
{
    if (clip.lengthSeconds <= 0.0 || r.getWidth() < 4.0f)
        return;
    const auto inW = static_cast<float>(clip.fadeIn / clip.lengthSeconds) * r.getWidth();
    const auto outW = static_cast<float>(clip.fadeOut / clip.lengthSeconds) * r.getWidth();
    // A linear fade: the part of the clip above the line is dimmed, and the line itself drawn.
    const auto draw = [&](juce::Point<float> foot, juce::Point<float> top, juce::Point<float> corner) {
        juce::Path shade;
        shade.addTriangle(foot, top, corner);
        g.setColour(theme::background.withAlpha(0.45f));
        g.fillPath(shade);
        g.setColour(clipInk.withAlpha(0.85f));
        g.drawLine({foot, top}, 1.25f);
    };
    if (inW >= 1.0f)
        draw({r.getX(), r.getBottom()}, {r.getX() + inW, r.getY()}, {r.getX(), r.getY()});
    if (outW >= 1.0f)
        draw({r.getRight(), r.getBottom()}, {r.getRight() - outW, r.getY()}, {r.getRight(), r.getY()});
}

void TimelineLanes::paintFadeHandles(juce::Graphics& g, const engine::ClipInfo& clip, juce::Rectangle<float> r)
{
    if (clip.lengthSeconds <= 0.0 || r.getWidth() < 24.0f)
        return;
    const auto inX = r.getX() + static_cast<float>(clip.fadeIn / clip.lengthSeconds) * r.getWidth();
    const auto outX = r.getRight() - static_cast<float>(clip.fadeOut / clip.lengthSeconds) * r.getWidth();
    for (const auto x : {inX, outX})
    {
        const auto box = juce::Rectangle<float>(6.0f, 6.0f).withCentre({x, r.getY() + 4.0f});
        g.setColour(theme::text);
        g.fillRect(box);
        g.setColour(clipInk.withAlpha(0.8f));
        g.drawRect(box, 1.0f);
    }
}

void TimelineLanes::paintWaveform(juce::Graphics& g, const engine::ClipInfo& clip, juce::Rectangle<float> r, juce::Colour)
{
    auto* thumb = view.thumbnailFor(clip.sourceId);
    if (thumb == nullptr || thumb->getTotalLength() <= 0.0 || clip.lengthSeconds <= 0.0)
        return;
    const auto area = r.withTrimmedTop(r.getHeight() * 0.44f).withTrimmedBottom(4.0f);
    if (area.getIntersection(getLocalBounds().toFloat()).isEmpty())
        return;

    // The forward stretch of the sound this clip plays (the reversed file plays it backwards: drawn mirrored). A warped clip
    // shows the part of the sound it covers, stretched over its bars.
    // The picture is made once for the whole loop of a looped clip (else for the clip's region) and cut into one piece per pass.
    const auto region = waveformLoopRegion(clip);
    const auto forwardStart = region.startSeconds;
    const int imageWidth = juce::jlimit(8, 800, juce::roundToInt(area.getWidth()));
    const int imageHeight = std::max(4, juce::roundToInt(area.getHeight()));
    const auto key = clip.sourceId + "|" + juce::String(forwardStart, 4) + "|" + juce::String(region.lengthSeconds, 4) + "|" + juce::String(clip.reversed ? 1 : 0)
                     + "|" + juce::String(imageHeight);

    auto& entry = waveCache[clip.id];
    entry.usedFrame = paintFrame;
    // Redrawn when the clip's region changed, or when it has been scaled by more than 2x since (at most one per paint; the rest follow on the next paints).
    const bool sameRegion = entry.image.isValid() && entry.key == key;
    const bool scaledTooMuch = sameRegion && (area.getWidth() > entry.widthPx * 2.0 || area.getWidth() < entry.widthPx * 0.5);
    if ((! sameRegion || scaledTooMuch) && waveBuildsThisPaint < 1)
    {
        ++waveBuildsThisPaint;
        juce::Image image(juce::Image::ARGB, imageWidth, imageHeight, true);
        juce::Graphics ig(image);
        if (clip.reversed)
            ig.addTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(static_cast<float>(imageWidth), 0.0f));
        ig.setColour(clipInk.withAlpha(0.6f));
        thumb->drawChannel(ig, {0, 0, imageWidth, imageHeight}, std::max(0.0, forwardStart),
                           std::max(0.0, forwardStart + region.lengthSeconds), 0, 1.0f);
        entry.image = image;
        entry.key = key;
        entry.widthPx = area.getWidth();
    }
    if (waveBuildsThisPaint >= 1 && (! sameRegion || scaledTooMuch))
        repaint(); // more pictures to make: continue on the next frame
    if (! entry.image.isValid())
        return;
    if (! thumb->isFullyLoaded())
        entry.key = {}; // a picture drawn while the waveform was still loading is drawn again

    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(getLocalBounds());
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    const auto pieces = waveformPieces(clip);
    if (pieces.size() == 1 && ! clip.loop)
    {
        g.drawImage(entry.image, area, juce::RectanglePlacement::stretchToFit);
        return;
    }
    // Looped: every pass is the part of the loop's picture that the clip plays in it, stretched to the pass; a mark at each repeat.
    const auto imageW = static_cast<float>(entry.image.getWidth()), imageH = static_cast<float>(entry.image.getHeight());
    for (const auto& piece : pieces)
    {
        const auto x0 = area.getX() + static_cast<float>(piece.from) * area.getWidth();
        const auto x1 = area.getX() + static_cast<float>(piece.to) * area.getWidth();
        if (x1 < 0.0f || x0 > static_cast<float>(getWidth()))
            continue;
        auto s0 = (piece.region.startSeconds - region.startSeconds) / region.lengthSeconds;
        auto s1 = (piece.region.startSeconds + piece.region.lengthSeconds - region.startSeconds) / region.lengthSeconds;
        if (region.mirrored) // the picture was drawn mirrored
            std::tie(s0, s1) = std::pair<double, double>(1.0 - s1, 1.0 - s0);
        const auto srcX = juce::roundToInt(static_cast<float>(s0) * imageW);
        const auto srcW = std::max(1, juce::roundToInt(static_cast<float>(s1 - s0) * imageW));
        g.drawImage(entry.image, juce::roundToInt(x0), juce::roundToInt(area.getY()), std::max(1, juce::roundToInt(x1 - x0)),
                    juce::roundToInt(area.getHeight()), srcX, 0, srcW, juce::roundToInt(imageH));
    }
    g.setColour(clipInk.withAlpha(0.5f));
    for (size_t i = 1; i < pieces.size(); ++i)
    {
        const auto x = area.getX() + static_cast<float>(pieces[i].from) * area.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), area.getY() - r.getHeight() * 0.44f + 2.0f, r.getBottom() - 2.0f);
    }
}

void TimelineLanes::mouseMove(const juce::MouseEvent& e)
{
    const auto hit = hitTestClip(view.laneClips(), view.layout(), view.viewport(), e.x, contentY(e.y));
    setMouseCursor(hit && hit->zone != ClipZone::body ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    const auto over = hit ? juce::String(hit->id) : juce::String();
    if (over != hoverId)
    {
        hoverId = over; // the fade handles of the clip under the mouse are drawn
        repaint();
    }
}

void TimelineLanes::mouseExit(const juce::MouseEvent&)
{
    if (hoverId.isNotEmpty() && mode == Mode::idle)
    {
        hoverId = {};
        repaint();
    }
}

void TimelineLanes::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    downPos = lastPos = e.position;
    movedEnough = false;
    previews.clear();
    snapGuideBeat.reset();
    auto& selection = view.context().selection;
    const auto numTracks = static_cast<int>(view.trackList().size());
    if (const auto track = view.layout().trackAt(contentY(e.y), numTracks); track >= 0 && track < numTracks)
    {
        selection.setTrack(view.trackList()[static_cast<size_t>(track)].id);
        view.headers().rebuild();
    }

    const auto hit = hitTestClip(view.laneClips(), view.layout(), view.viewport(), e.x, contentY(e.y));
    if (! hit)
    {
        if (! e.mods.isShiftDown())
            selection.clearClips();
        selectionBeforeMarquee = selection.clips();
        mode = Mode::marquee;
        repaint();
        return;
    }
    grabbedId = hit->id;
    const auto* clip = view.findClip(grabbedId);
    if (clip == nullptr)
        return;
    if (hit->zone != ClipZone::body)
    {
        selection.setClips({grabbedId});
        switch (hit->zone)
        {
            case ClipZone::leftEdge: mode = Mode::trimLeft; break;
            case ClipZone::rightEdge: mode = Mode::trimRight; break;
            case ClipZone::fadeInHandle: mode = Mode::fadeIn; break;
            default: mode = Mode::fadeOut; break;
        }
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isShiftDown())
    {
        selection.click(grabbedId, e.mods.isShiftDown(), e.mods.isCommandDown());
        if (! selection.isSelected(grabbedId))
        {
            mode = Mode::idle;
            return;
        }
    }
    else if (! selection.isSelected(grabbedId))
        selection.click(grabbedId, false, false);
    mode = Mode::move;
    grabOffsetBeats = view.viewport().xToBeat(e.x) - clip->start;
}

void TimelineLanes::mouseDrag(const juce::MouseEvent& e)
{
    if (mode == Mode::idle)
        return;
    lastPos = e.position;
    if (e.getDistanceFromDragStart() > 3)
        movedEnough = true;
    if (! movedEnough)
        return;
    switch (mode)
    {
        case Mode::move: updateMove(e); break;
        case Mode::trimLeft:
        case Mode::trimRight: updateTrim(e); break;
        case Mode::fadeIn:
        case Mode::fadeOut: updateFade(e); break;
        case Mode::marquee:
        {
            const LaneRect box{downPos.x, contentY(downPos.y), lastPos.x - downPos.x, lastPos.y - downPos.y};
            view.context().selection.marquee(toStrings(clipsInMarquee(view.laneClips(), view.layout(), view.viewport(), box)),
                                             selectionBeforeMarquee, e.mods.isShiftDown());
            break;
        }
        case Mode::idle: break;
    }
    repaint();
}

void TimelineLanes::updateMove(const juce::MouseEvent& e)
{
    const auto* grabbed = view.findClip(grabbedId);
    if (grabbed == nullptr)
        return;
    const auto numTracks = static_cast<int>(view.trackList().size());
    const auto rawStart = view.viewport().xToBeat(e.x) - grabOffsetBeats;
    const auto snapped = view.snapBeat(rawStart, e.mods.isAltDown());
    const auto mouseTrack = std::clamp(view.layout().trackAt(contentY(e.y), numTracks), 0, numTracks - 1);
    const auto grabTrack = view.trackIndexOf(grabbed->trackId);

    std::vector<const engine::ClipInfo*> chosen;
    int minTrack = numTracks, maxTrack = 0;
    double earliest = grabbed->start;
    for (const auto& id : view.context().selection.clips())
        if (const auto* c = view.findClip(id))
        {
            chosen.push_back(c);
            const auto t = view.trackIndexOf(c->trackId);
            minTrack = std::min(minTrack, t);
            maxTrack = std::max(maxTrack, t);
            earliest = std::min(earliest, c->start);
        }
    const auto move = clampMove(grabbed->start, snapped, grabTrack, mouseTrack, numTracks, minTrack, maxTrack, earliest);
    previews.clear();
    for (const auto* c : chosen)
        previews.push_back({c->id, view.trackIndexOf(c->trackId) + move.trackDelta, c->start + move.beatDelta, c->length});
    snapGuideBeat = grabbed->start + move.beatDelta;
}

void TimelineLanes::updateTrim(const juce::MouseEvent& e)
{
    const auto* clip = view.findClip(grabbedId);
    if (clip == nullptr)
        return;
    const auto map = commands::timeMapOf(view.context().arrangement);
    const auto span = commands::spanFor(*clip, view.trackIndexOf(clip->trackId));
    const auto minLength = std::max(Snap::minimumLength(view.snapValue(), view.timeSig(), 0.0),
                                    map.beatAfter(clip->start, engine::Arrangement::minClipSeconds) - clip->start);
    const auto edge = std::max(0.0, view.snapBeat(view.viewport().xToBeat(e.x), e.mods.isAltDown()));
    double start = clip->start, end = clip->end();
    if (mode == Mode::trimLeft)
        start = commands::EditPlanner::clampTrimStart(span, edge, minLength, map);
    else
        end = commands::EditPlanner::clampTrimEnd(span, edge, clip->sourceSeconds, minLength, map);
    previews = {{clip->id, view.trackIndexOf(clip->trackId), start, end - start}};
    snapGuideBeat = mode == Mode::trimLeft ? start : end;
}

void TimelineLanes::updateFade(const juce::MouseEvent& e)
{
    const auto* clip = view.findClip(grabbedId);
    if (clip == nullptr || clip->length <= 0.0 || clip->lengthSeconds <= 0.0)
        return;
    // Not snapped: the handle follows the mouse. In seconds of the clip (a warped clip's length in seconds is its timeline length).
    const auto secondsPerBeat = clip->lengthSeconds / clip->length;
    const auto beat = view.viewport().xToBeat(e.x);
    const auto wanted = (mode == Mode::fadeIn ? beat - clip->start : clip->end() - beat) * secondsPerBeat;
    const auto other = mode == Mode::fadeIn ? clip->fadeOut : clip->fadeIn;
    const auto seconds = std::clamp(wanted, 0.0, std::max(0.0, clip->lengthSeconds - other));
    commands::SetClipFades command(clip->id, mode == Mode::fadeIn ? seconds : clip->fadeIn, mode == Mode::fadeOut ? seconds : clip->fadeOut);
    auto& service = view.context().commands;
    if (! service.isGestureOpen())
        service.begin("Set clip fades");
    service.update(command); // heard at once; the gesture is one undo step
}

void TimelineLanes::mouseUp(const juce::MouseEvent& e)
{
    if (mode == Mode::move && ! movedEnough && ! e.mods.isShiftDown() && ! e.mods.isCommandDown())
        view.context().selection.setClips({grabbedId}); // a plain click on one of several selected clips
    commitDrag();
}

void TimelineLanes::commitDrag()
{
    const auto finished = mode;
    const auto result = previews;
    if ((finished == Mode::fadeIn || finished == Mode::fadeOut) && view.context().commands.isGestureOpen())
        view.context().commands.commit();
    mode = Mode::idle;
    previews.clear();
    snapGuideBeat.reset();
    repaint();
    if (! movedEnough || result.empty())
        return;
    if (finished == Mode::move)
    {
        std::vector<commands::ClipMove> moves;
        bool changed = false;
        for (const auto& p : result)
        {
            if (const auto* c = view.findClip(p.id))
                changed = changed || std::abs(c->start - p.start) > 1.0e-9 || view.trackIndexOf(c->trackId) != p.track;
            moves.push_back({p.id, p.track, p.start});
        }
        if (changed)
        {
            commands::MoveClips command(moves);
            view.applyCommand(command, false);
        }
    }
    else if (finished == Mode::trimLeft || finished == Mode::trimRight)
    {
        const auto& p = result.front();
        if (const auto* c = view.findClip(p.id); c != nullptr && (std::abs(c->start - p.start) > 1.0e-9 || std::abs(c->length - p.length) > 1.0e-9))
        {
            commands::TrimClip command(p.id, p.start, p.start + p.length, 0.0);
            view.applyCommand(command, false);
        }
    }
}

void TimelineLanes::cancelDrag()
{
    if ((mode == Mode::fadeIn || mode == Mode::fadeOut) && view.context().commands.isGestureOpen())
        view.context().commands.cancel();
    mode = Mode::idle;
    previews.clear();
    snapGuideBeat.reset();
    movedEnough = false;
    repaint();
}

bool TimelineLanes::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && (isDragging() || view.ruler().isDraggingLoop()))
    {
        cancelDrag();
        view.ruler().cancelDrag();
        return true;
    }
    return false;
}

void TimelineLanes::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        view.zoomAround(e.position.x, wheel.deltaY > 0.0f ? 1.18 : 1.0 / 1.18);
        return;
    }
    const auto dx = wheel.deltaX != 0.0f ? wheel.deltaX : (e.mods.isShiftDown() ? wheel.deltaY : 0.0f);
    const auto dy = e.mods.isShiftDown() ? 0.0f : wheel.deltaY;
    view.scrollBy(-dx * 400.0, -dy * 400.0);
}

bool TimelineLanes::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith("sample:");
}

void TimelineLanes::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void TimelineLanes::itemDragMove(const SourceDetails& details)
{
    const auto sourceId = details.description.toString().fromFirstOccurrenceOf("sample:", false, false);
    ghost = dropTargetAt(details.localPosition.toFloat(), juce::ModifierKeys::getCurrentModifiers().isAltDown());
    const auto& a = view.context().arrangement;
    ghostLengthBeats = a.secondsToBeats(a.beatsToSeconds(ghost->beat) + view.sourceSeconds(sourceId)) - ghost->beat;
    ghostLabel = view.sourceName(sourceId);
    ghostCount = 1;
    repaint();
}

void TimelineLanes::itemDragExit(const SourceDetails&)
{
    ghost.reset();
    repaint();
}

void TimelineLanes::itemDropped(const SourceDetails& details)
{
    const auto target = dropTargetAt(details.localPosition.toFloat(), juce::ModifierKeys::getCurrentModifiers().isAltDown());
    ghost.reset();
    repaint();
    const auto sourceId = details.description.toString().fromFirstOccurrenceOf("sample:", false, false);
    std::vector<engine::WarpSettings> warps;
    if (const auto& defaultWarp = view.context().defaultWarp)
        warps.push_back(defaultWarp(sourceId));
    commands::CreateClipsFromSources command({sourceId}, placementFor(target), warps);
    view.applyCommand(command, true);
}

bool TimelineLanes::isInterestedInFileDrag(const juce::StringArray&) { return view.context().importAndPlace != nullptr; }

void TimelineLanes::fileDragEnter(const juce::StringArray& files, int x, int y) { fileDragMove(files, x, y); }

void TimelineLanes::fileDragMove(const juce::StringArray& files, int x, int y)
{
    ghost = dropTargetAt({static_cast<float>(x), static_cast<float>(y)}, false);
    ghostLengthBeats = view.timeSig().numerator;
    ghostCount = files.size();
    ghostLabel = files.size() == 1 ? juce::File(files[0]).getFileNameWithoutExtension() : juce::String(files.size()) + " files";
    repaint();
}

void TimelineLanes::fileDragExit(const juce::StringArray&)
{
    ghost.reset();
    repaint();
}

void TimelineLanes::filesDropped(const juce::StringArray& files, int x, int y)
{
    const auto target = dropTargetAt({static_cast<float>(x), static_cast<float>(y)}, false);
    ghost.reset();
    repaint();
    juce::Array<juce::File> list;
    for (const auto& f : files)
        list.add(juce::File(f));
    if (view.context().importAndPlace)
        view.context().importAndPlace(list, placementFor(target));
}

//==============================================================================
void PlayheadOverlay::setPlayheadX(std::optional<float> x)
{
    if (x == current)
        return;
    const auto strip = [this](float px) { return juce::Rectangle<int>(static_cast<int>(px) - 2, 0, 5, getHeight()); };
    if (current)
        repaint(strip(*current));
    current = x;
    if (current)
        repaint(strip(*current));
}

void PlayheadOverlay::paint(juce::Graphics& g)
{
    if (! current)
        return;
    g.setColour(theme::text);
    g.fillRect(juce::Rectangle<float>(*current - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight())));
}

//==============================================================================
ArrangementView::ArrangementView(ArrangeContext c)
    : ctx(std::move(c)), headerList(*this), rulerComponent(*this), lanesComponent(*this)
{
    for (auto* child : {static_cast<juce::Component*>(&headerList), static_cast<juce::Component*>(&rulerComponent),
                        static_cast<juce::Component*>(&lanesComponent), static_cast<juce::Component*>(&playheadOverlay),
                        static_cast<juce::Component*>(&hScroll), static_cast<juce::Component*>(&vScroll)})
        addAndMakeVisible(child);
    hScroll.addListener(this);
    vScroll.addListener(this);
    hScroll.setAutoHide(false);
    vScroll.setAutoHide(false);
    const_cast<engine::Arrangement&>(ctx.arrangement).addChangeListener(this);
    vblank = std::make_unique<juce::VBlankAttachment>(this, [this](double seconds) {
        const auto elapsed = lastVBlank > 0.0 ? (seconds - lastVBlank) * 1000.0 : 16.7;
        lastVBlank = seconds;
        tick(juce::jlimit(0.0, 100.0, elapsed));
    });
    refresh();
}

ArrangementView::~ArrangementView()
{
    vblank.reset();
    const_cast<engine::Arrangement&>(ctx.arrangement).removeChangeListener(this);
}

void ArrangementView::refresh()
{
    tracks = ctx.arrangement.tracks();
    sig = ctx.arrangement.timeSig();
    clipsByTrack.assign(tracks.size(), {});
    allLaneClips.clear();
    anyRendering = false;
    std::set<juce::String> existing;
    for (const auto& clip : ctx.arrangement.clips())
    {
        const auto index = trackIndexOf(clip.trackId);
        if (index < 0)
            continue;
        clipsByTrack[static_cast<size_t>(index)].push_back(clip);
        const auto beatsPerSecond = clip.lengthSeconds > 0.0 ? clip.length / clip.lengthSeconds : 0.0;
        allLaneClips.push_back({clip.id.toStdString(), index, clip.start, clip.length, clip.fadeIn * beatsPerSecond, clip.fadeOut * beatsPerSecond});
        existing.insert(clip.id);
        anyRendering = anyRendering || clip.rendering;
    }
    shownGeneration = ctx.arrangement.generation();
    ctx.selection.retainOnly(existing);
    std::vector<juce::String> trackIds;
    for (const auto& t : tracks)
        trackIds.push_back(t.id);
    if (ctx.arms != nullptr)
        ctx.arms->prune(trackIds); // armed tracks that were deleted are forgotten
    if (ctx.playbackDevices && ! devicesLoaded)
    {
        devices = ctx.playbackDevices(); // listing devices takes a moment: not on every edit (tick looks again now and then)
        devicesLoaded = true;
    }
    ctx.selection.retainTrack(trackIds);
    headerList.rebuild();
    viewChanged();
}

//==============================================================================
std::vector<ArrangementView::InputChoice> ArrangementView::inputChoices(const engine::TrackInfo& track) const
{
    std::vector<InputChoice> choices;
    bool defaultListed = false;
    for (const auto& d : devices)
    {
        if (d.isDefault)
        {
            choices.push_back({"system:default", "System audio (" + d.name + ")", track.input == "system:default"});
            defaultListed = true;
        }
    }
    if (! defaultListed)
        choices.push_back({"system:default", "System audio (default device)", track.input == "system:default"});
    for (const auto& d : devices)
        if (! d.isDefault)
            choices.push_back({"system:" + d.id, "System audio (" + d.name + ")", track.input == "system:" + d.id});
    choices.push_back({"master", "Master", track.input == "master"});
    for (const auto& other : tracks)
        if (other.id != track.id)
            choices.push_back({"track:" + other.id, other.name, track.input == "track:" + other.id});
    return choices;
}

juce::String ArrangementView::inputLabel(const engine::TrackInfo& track) const
{
    const auto defaultDevice = [this]
    {
        for (const auto& d : devices)
            if (d.isDefault)
                return d.name;
        return juce::String();
    };
    const auto system = [](const juce::String& device) { return device.isEmpty() ? juce::String("In: System") : "In: System (" + device + ")"; };

    if (track.input == "master")
        return "In: Master";
    if (track.input.startsWith("track:"))
    {
        const auto id = track.input.fromFirstOccurrenceOf(":", false, false);
        for (const auto& other : tracks)
            if (other.id == id)
                return "In: " + other.name;
        return system(defaultDevice());
    }
    if (track.input.startsWith("system:") && track.input != "system:default")
    {
        const auto id = track.input.fromFirstOccurrenceOf(":", false, false);
        for (const auto& d : devices)
            if (d.id == id)
                return system(d.name);
        return system(defaultDevice()) + ", chosen device missing"; // it records the default device instead
    }
    return system(defaultDevice());
}

void ArrangementView::showInputMenu(const juce::String& trackId)
{
    const auto index = trackIndexOf(trackId);
    if (index < 0)
        return;
    if (ctx.playbackDevices)
        devices = ctx.playbackDevices(); // the devices as they are now
    const auto track = tracks[static_cast<size_t>(index)];
    const auto choices = inputChoices(track);
    juce::PopupMenu menu;
    bool separated = false;
    for (size_t i = 0; i < choices.size(); ++i)
    {
        if (! separated && choices[i].input == "master")
        {
            menu.addSeparator();
            separated = true;
        }
        menu.addItem(static_cast<int>(i) + 1, choices[i].label, true, choices[i].ticked);
    }
    juce::Component::SafePointer<ArrangementView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(headerList.headerFor(trackId)), [safe, trackId, choices](int result) {
        if (safe != nullptr && result > 0)
            safe->setTrackInput(trackId, choices[static_cast<size_t>(result - 1)].input);
    });
}

void ArrangementView::setTrackInput(const juce::String& trackId, const juce::String& input)
{
    commands::SetTrackInput command(trackId, input);
    applyCommand(command, false);
}

void ArrangementView::armClicked(const juce::String& trackId, bool toggle)
{
    if (ctx.arms == nullptr)
        return;
    if (toggle)
        ctx.arms->toggle(trackId);
    else
        ctx.arms->arm(trackId);
    syncRecordingState();
}

void ArrangementView::syncRecordingState()
{
    for (int i = 0; i < headerList.numHeaders(); ++i)
        if (auto* h = headerList.header(i))
            h->syncRecordingState();
}

int ArrangementView::trackIndexOf(const juce::String& trackId) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId)
            return static_cast<int>(i);
    return -1;
}

const std::vector<engine::ClipInfo>& ArrangementView::clipsOnTrack(int index) const
{
    static const std::vector<engine::ClipInfo> none;
    return juce::isPositiveAndBelow(index, static_cast<int>(clipsByTrack.size())) ? clipsByTrack[static_cast<size_t>(index)] : none;
}

const engine::ClipInfo* ArrangementView::findClip(const juce::String& id) const
{
    for (const auto& list : clipsByTrack)
        for (const auto& c : list)
            if (c.id == id)
                return &c;
    return nullptr;
}

double ArrangementView::contentLengthBeats() const { return contentBeats(ctx.arrangement.endBeat(), sig.numerator); }

double ArrangementView::minimumLength() const { return Snap::minimumLength(snapValue(), sig, 1.0); }

juce::Colour ArrangementView::colourOfTrack(int index) const
{
    return juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())) ? tracks[static_cast<size_t>(index)].colour
                                                                            : engine::trackPaletteColour(index);
}

juce::AudioThumbnailBase* ArrangementView::thumbnailFor(const juce::String& sourceId)
{
    if (const auto found = thumbnails.find(sourceId); found != thumbnails.end())
        return found->second.get();
    auto thumb = ctx.makeThumbnail ? ctx.makeThumbnail(sourceId, lanesComponent) : nullptr;
    auto* raw = thumb.get();
    thumbnails[sourceId] = std::move(thumb); // also remembers "no picture" so it is not asked again every paint
    return raw;
}

juce::String ArrangementView::sourceName(const juce::String& sourceId) const
{
    return model::SamplerState(ctx.sampler).findSourceById(sourceId).name();
}

double ArrangementView::sourceSeconds(const juce::String& sourceId) const
{
    return model::SamplerState(ctx.sampler).findSourceById(sourceId).lengthSeconds();
}

void ArrangementView::tick(double elapsedMs)
{
    // Playhead: only the strips it leaves and enters are repainted.
    const auto x = view.beatToX(ctx.transport.positionBeats());
    playheadOverlay.setPlayheadX(x >= -2.0 && x <= playheadOverlay.getWidth() + 2.0 ? std::optional<float>(static_cast<float>(x))
                                                                                    : std::nullopt);
    std::map<juce::String, engine::LevelReading> incoming; // the input level of tracks that are recording
    bool anyTake = false;
    if (ctx.liveTakes)
        for (const auto& take : ctx.liveTakes())
        {
            anyTake = true;
            if (take.writer == nullptr)
                continue;
            const auto peak = std::max(take.writer->takePeak(), 1.0e-5f);
            engine::LevelReading reading;
            reading.peakDb[0] = reading.peakDb[1] = juce::Decibels::gainToDecibels(peak);
            reading.over = peak > 1.0f;
            for (const auto& id : take.trackIds)
                incoming[id] = reading;
        }
    for (int i = 0; i < headerList.numHeaders(); ++i)
        if (auto* h = headerList.header(i))
        {
            const auto live = incoming.find(h->trackId());
            h->pushLevel(live != incoming.end() ? live->second : ctx.meters.readTrack(h->trackId()), elapsedMs);
        }
    if (anyTake || hadLiveTakes)
        lanesComponent.repaint();
    hadLiveTakes = anyTake;

    // A headset plugged in or the default device changed: the input labels follow within a few seconds of the song being stopped.
    // (Not while the song plays: listing the devices takes a moment and would drop a frame.)
    if (ctx.playbackDevices && (deviceCheckMs += elapsedMs) > 5000.0 && ! anyTake && ! ctx.transport.isPlaying())
    {
        deviceCheckMs = 0.0;
        auto now = ctx.playbackDevices();
        const auto same = now.size() == devices.size()
                          && std::equal(now.begin(), now.end(), devices.begin(), [](const auto& a, const auto& b) {
                                 return a.id == b.id && a.name == b.name && a.isDefault == b.isDefault;
                             });
        if (! same)
        {
            devices = std::move(now);
            syncRecordingState();
        }
    }

    // Reverse renders finish without an edit: look again now and then while one is pending.
    if (anyRendering && (renderPollMs += elapsedMs) > 250.0)
    {
        renderPollMs = 0.0;
        refresh();
    }
}

void ArrangementView::zoomToSelection()
{
    double start = 0.0, end = 0.0;
    bool any = false;
    for (const auto& id : ctx.selection.clips())
        if (const auto* c = findClip(id))
        {
            start = any ? std::min(start, c->start) : c->start;
            end = any ? std::max(end, c->end()) : c->end();
            any = true;
        }
    if (! any)
        return;
    view.showRange(start, end);
    viewChanged();
}

void ArrangementView::zoomToSong()
{
    const auto end = std::max(ctx.arrangement.endBeat(), static_cast<double>(sig.numerator));
    view.showRange(0.0, end);
    viewChanged();
}

void ArrangementView::zoomAround(double laneX, double factor)
{
    view.zoomAround(laneX, factor);
    viewChanged();
}

void ArrangementView::scrollBy(double dxPixels, double dyPixels)
{
    view.scrollByPixels(dxPixels);
    view.scrollY += dyPixels;
    viewChanged();
}

void ArrangementView::viewChanged()
{
    view.clamp(contentLengthBeats(), geometry.contentHeight(static_cast<int>(tracks.size())));
    headerList.layoutHeaders();
    updateScrollBars();
    lanesComponent.repaint();
    rulerComponent.repaint();
    headerList.repaint();
    tick(0.0);
}

void ArrangementView::updateScrollBars()
{
    const juce::ScopedValueSetter<bool> guard(updatingScrollBars, true);
    hScroll.setRangeLimits(0.0, std::max(contentLengthBeats(), view.visibleBeats()), juce::dontSendNotification);
    hScroll.setCurrentRange(view.firstBeat, view.visibleBeats(), juce::dontSendNotification);
    const auto contentHeight = geometry.contentHeight(static_cast<int>(tracks.size()));
    vScroll.setRangeLimits(0.0, std::max(contentHeight, view.heightPx), juce::dontSendNotification);
    vScroll.setCurrentRange(view.scrollY, view.heightPx, juce::dontSendNotification);
}

void ArrangementView::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollBars)
        return;
    if (bar == &hScroll)
        view.firstBeat = newRangeStart;
    else
        view.scrollY = newRangeStart;
    viewChanged();
}

void ArrangementView::applyCommand(commands::ArrangeCommand& command, bool selectResult)
{
    ctx.commands.apply(command);
    if (selectResult && ! command.resultIds().empty())
        ctx.selection.setClips(command.resultIds());
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();
    const auto top = geometry.topHeight();
    auto bottom = area.removeFromBottom(scrollBarSize);
    auto headerColumn = area.removeFromLeft(geometry.headerWidth);
    bottom.removeFromLeft(geometry.headerWidth);
    vScroll.setBounds(area.removeFromRight(scrollBarSize).withTrimmedTop(top));
    hScroll.setBounds(bottom.withTrimmedRight(scrollBarSize));
    headerList.setBounds(headerColumn.withTrimmedTop(top));
    rulerComponent.setBounds(area.removeFromTop(top));
    lanesComponent.setBounds(area);
    playheadOverlay.setBounds(rulerComponent.getBounds().getUnion(lanesComponent.getBounds()));
    view.widthPx = std::max(1, lanesComponent.getWidth());
    view.heightPx = std::max(1, lanesComponent.getHeight());
    if (! initialZoomDone && lanesComponent.getWidth() > 200)
    {
        initialZoomDone = true; // the first layout shows 16 bars, as in the mock
        view.pixelsPerBeat = std::clamp(view.widthPx / (16.0 * std::max(1, sig.numerator)), ArrangeViewport::minPixelsPerBeat,
                                        ArrangeViewport::maxPixelsPerBeat);
    }
    viewChanged();
}

void ArrangementView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    auto corner = juce::Rectangle<int>(0, 0, geometry.headerWidth, geometry.topHeight());
    g.setColour(theme::muted);
    auto font = Fonts::sans(10.0f);
    font.setExtraKerningFactor(0.08f);
    g.setFont(font);
    g.drawText("TRACKS", corner.withTrimmedLeft(16), juce::Justification::centredLeft);
    g.setColour(theme::border);
    g.drawHorizontalLine(corner.getBottom() - 1, 0.0f, static_cast<float>(geometry.headerWidth));
    g.drawVerticalLine(geometry.headerWidth - 1, 0.0f, static_cast<float>(corner.getBottom()));
}
} // namespace sampler::ui
