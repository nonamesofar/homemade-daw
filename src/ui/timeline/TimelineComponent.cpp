#include "TimelineComponent.h"

namespace sampler::ui
{
TimelineComponent::TimelineComponent(TimelineModel modelToUse, std::vector<juce::File> sources,
                                     std::vector<double> sourceSeconds, ThumbnailFactory factory)
    : model(std::move(modelToUse)),
      sourceFiles(std::move(sources)),
      sourceLengthSeconds(std::move(sourceSeconds)),
      makeThumbnail(std::move(factory))
{
    setOpaque(true);
    setSize(1000, layout.contentHeight(static_cast<int>(model.tracks.size())));
}

TimelineComponent::~TimelineComponent() = default;

juce::Colour TimelineComponent::colourForTrack(int trackId) const
{
    return juce::Colour::fromHSV(std::fmod(static_cast<float>(trackId) * 0.19f + 0.05f, 1.0f), 0.45f, 0.62f, 1.0f);
}

juce::AudioThumbnailBase* TimelineComponent::thumbnailFor(int sourceIndex) const
{
    auto it = thumbnails.find(sourceIndex);
    if (it == thumbnails.end())
    {
        std::unique_ptr<juce::AudioThumbnailBase> thumb;
        if (makeThumbnail && juce::isPositiveAndBelow(sourceIndex, static_cast<int>(sourceFiles.size())))
            thumb = makeThumbnail(sourceFiles[static_cast<size_t>(sourceIndex)], const_cast<TimelineComponent&>(*this));
        it = thumbnails.emplace(sourceIndex, std::move(thumb)).first;
    }
    return it->second.get();
}

void TimelineComponent::resized()
{
    view.widthPx = std::max(1, getWidth() - layout.headerWidth);
    clampView();
}

void TimelineComponent::clampView() { view.clamp(model.endBeat()); }

//==============================================================================
void TimelineComponent::paint(juce::Graphics& g)
{
    const auto t0 = juce::Time::getHighResolutionTicks();

    g.fillAll(juce::Colour(0xff1b1d22));

    for (int i = 0; i < static_cast<int>(model.tracks.size()); ++i)
        drawTrack(g, i);

    drawRuler(g);

    if (clipDrag)
    {
        const auto& track = model.tracks[static_cast<size_t>(clipDrag->hit.track)];
        const auto& clip = track.clips[static_cast<size_t>(clipDrag->hit.clip)];
        const auto x = static_cast<float>(layout.headerWidth + view.beatToX(clipDrag->drop.startBeat));
        const auto w = static_cast<float>(clip.lengthBeats * view.pixelsPerBeat);
        const auto y = static_cast<float>(layout.trackTop(clipDrag->drop.track));

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(layout.headerWidth, layout.rulerHeight, getWidth() - layout.headerWidth, getHeight());
        drawClip(g, clip, {x, y, w, static_cast<float>(layout.trackHeight)}, colourForTrack(track.id), true);
    }

    if (trackDrag && trackDrag->from != trackDrag->to)
    {
        const auto y = static_cast<float>(reorderMarkerY(layout, trackDrag->from, trackDrag->to));
        g.setColour(juce::Colours::orange);
        g.fillRect(0.0f, y - 1.5f, static_cast<float>(getWidth()), 3.0f);
    }

    if (onPainted)
        onPainted(juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1000.0);
}

void TimelineComponent::drawRuler(juce::Graphics& g) const
{
    const juce::Rectangle<int> ruler(0, 0, getWidth(), layout.rulerHeight);
    g.setColour(juce::Colour(0xff25282f));
    g.fillRect(ruler);
    g.setColour(juce::Colour(0xff3a3e48));
    g.drawHorizontalLine(layout.rulerHeight - 1, 0.0f, static_cast<float>(getWidth()));

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(layout.headerWidth, 0, getWidth() - layout.headerWidth, layout.rulerHeight);

    const auto first = static_cast<int>(std::floor(view.firstBeat));
    const auto last = static_cast<int>(std::ceil(view.lastBeat()));
    const bool showBeats = view.pixelsPerBeat >= 12.0;
    int barLabelStep = 1;
    while (view.pixelsPerBeat * 4.0 * barLabelStep < 48.0)
        barLabelStep *= 2;

    g.setFont(juce::FontOptions(11.0f));
    for (int beat = first; beat <= last; ++beat)
    {
        const bool isBar = beat % 4 == 0;
        if (!isBar && !showBeats)
            continue;
        const auto x = static_cast<float>(layout.headerWidth + view.beatToX(beat));
        g.setColour(isBar ? juce::Colour(0xff8a90a0) : juce::Colour(0xff555a66));
        g.drawVerticalLine(juce::roundToInt(x), isBar ? 4.0f : 14.0f, static_cast<float>(layout.rulerHeight));
        if (isBar && (beat / 4) % barLabelStep == 0)
        {
            g.setColour(juce::Colour(0xffb5bac6));
            g.drawText(juce::String(beat / 4 + 1), juce::roundToInt(x) + 3, 2, 40, 14, juce::Justification::left, false);
        }
    }
}

void TimelineComponent::drawTrack(juce::Graphics& g, int trackIndex) const
{
    const auto& track = model.tracks[static_cast<size_t>(trackIndex)];
    const auto top = layout.trackTop(trackIndex);
    const auto colour = colourForTrack(track.id);
    const bool dragged = trackDrag && trackDrag->from == trackIndex;

    // Lane background and beat grid.
    const juce::Rectangle<int> lane(layout.headerWidth, top, getWidth() - layout.headerWidth, layout.trackHeight);
    g.setColour(trackIndex % 2 == 0 ? juce::Colour(0xff20232a) : juce::Colour(0xff1d2026));
    g.fillRect(lane);

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(lane);

        if (view.pixelsPerBeat >= 12.0)
        {
            g.setColour(juce::Colour(0x14ffffff));
            for (int beat = static_cast<int>(std::floor(view.firstBeat)); beat <= static_cast<int>(std::ceil(view.lastBeat())); ++beat)
                g.drawVerticalLine(juce::roundToInt(layout.headerWidth + view.beatToX(beat)), static_cast<float>(top),
                                   static_cast<float>(top + layout.trackHeight));
        }

        for (int c = 0; c < static_cast<int>(track.clips.size()); ++c)
        {
            const auto& clip = track.clips[static_cast<size_t>(c)];
            const auto x = static_cast<float>(layout.headerWidth + view.beatToX(clip.startBeat));
            const auto w = static_cast<float>(clip.lengthBeats * view.pixelsPerBeat);
            if (x + w < layout.headerWidth || x > getWidth())
                continue; // off screen

            const bool beingDragged = clipDrag && clipDrag->hit.track == trackIndex && clipDrag->hit.clip == c;
            drawClip(g, clip, {x, static_cast<float>(top), w, static_cast<float>(layout.trackHeight)}, colour,
                     beingDragged);
        }
    }

    // Header.
    juce::Rectangle<int> header(0, top, layout.headerWidth, layout.trackHeight);
    g.setColour(dragged ? juce::Colour(0xff3a4150) : juce::Colour(0xff2a2e37));
    g.fillRect(header);
    g.setColour(colour);
    g.fillRect(header.removeFromLeft(5));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(track.name, 14, top + 8, layout.headerWidth - 20, 20, juce::Justification::left, true);
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(juce::String(track.clips.size()) + " clips", 14, top + 30, layout.headerWidth - 20, 16,
               juce::Justification::left, true);
    g.setColour(juce::Colour(0xff111317));
    g.drawHorizontalLine(top + layout.trackHeight - 1, 0.0f, static_cast<float>(getWidth()));
}

void TimelineComponent::drawClip(juce::Graphics& g, const ClipModel& clip, juce::Rectangle<float> bounds,
                                 juce::Colour colour, bool ghost) const
{
    auto body = bounds.reduced(1.0f, 3.0f);
    if (body.getWidth() < 1.0f)
        return;

    const auto alpha = ghost ? 0.45f : 1.0f;
    g.setColour(colour.darker(0.55f).withAlpha(alpha));
    g.fillRoundedRectangle(body, 3.0f);

    // Waveform: only the part that is on screen, mapped to the matching part of the source.
    const auto clipSeconds = std::min(clip.lengthBeats * 60.0 / bpm,
                                      juce::isPositiveAndBelow(clip.sourceIndex, static_cast<int>(sourceLengthSeconds.size()))
                                          ? sourceLengthSeconds[static_cast<size_t>(clip.sourceIndex)]
                                          : clip.lengthBeats * 60.0 / bpm);
    const auto visibleLeft = std::max(body.getX(), static_cast<float>(layout.headerWidth));
    const auto visibleRight = std::min(body.getRight(), static_cast<float>(getWidth()));
    auto wave = body.withTrimmedTop(16.0f).reduced(0.0f, 2.0f);

    if (visibleRight - visibleLeft >= 1.0f && wave.getHeight() > 4.0f)
        if (auto* thumb = thumbnailFor(clip.sourceIndex))
        {
            const auto t0 = (visibleLeft - body.getX()) / body.getWidth() * clipSeconds;
            const auto t1 = (visibleRight - body.getX()) / body.getWidth() * clipSeconds;
            g.setColour(colour.brighter(0.5f).withAlpha(alpha));
            thumb->drawChannels(g, wave.withX(visibleLeft).withRight(visibleRight).toNearestInt(), t0, t1, 1.0f);
        }

    // Name strip and outline (the strip is skipped when the clip is too narrow to read).
    g.setColour(colour.withAlpha(alpha));
    g.fillRoundedRectangle(body.withHeight(14.0f), 3.0f);
    if (body.getWidth() > 40.0f)
    {
        g.setColour(juce::Colours::black.withAlpha(0.8f * alpha));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("clip " + juce::String(clip.id), body.withHeight(14.0f).reduced(4.0f, 0.0f).toNearestInt(),
                   juce::Justification::left, true);
    }

    const bool selected = selectedClip && selectedClip->second == clip.id;
    g.setColour(selected ? juce::Colours::white : colour.brighter(0.3f).withAlpha(0.6f * alpha));
    g.drawRoundedRectangle(body, 3.0f, selected ? 1.5f : 1.0f);
}

//==============================================================================
void TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    clipDrag.reset();
    trackDrag.reset();

    const auto header = hitTestHeader(layout, static_cast<int>(model.tracks.size()), e.x, e.y);
    if (header >= 0)
    {
        trackDrag = TrackDrag{header, header, static_cast<float>(e.y)};
        repaint();
        return;
    }

    if (auto hit = hitTestClip(model, layout, view, e.x, e.y))
    {
        const auto& clip = model.tracks[static_cast<size_t>(hit->track)].clips[static_cast<size_t>(hit->clip)];
        ClipDrag drag;
        drag.hit = *hit;
        drag.grabOffsetBeats = view.xToBeat(e.x - layout.headerWidth) - clip.startBeat;
        drag.drop = {hit->track, clip.startBeat};
        clipDrag = drag;
        selectedClip = std::make_pair(model.tracks[static_cast<size_t>(hit->track)].id, clip.id);
    }
    else
    {
        selectedClip.reset();
    }
    repaint();
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    const auto numTracks = static_cast<int>(model.tracks.size());
    if (trackDrag)
    {
        trackDrag->to = reorderTargetIndex(layout, numTracks, e.y);
        trackDrag->mouseY = static_cast<float>(e.y);
        repaint();
    }
    else if (clipDrag)
    {
        clipDrag->drop = resolveClipDrop(layout, view, numTracks, e.x, e.y, clipDrag->grabOffsetBeats, gridBeats);
        repaint();
    }
}

void TimelineComponent::mouseUp(const juce::MouseEvent&)
{
    bool changed = false;

    if (trackDrag)
    {
        if (trackDrag->from != trackDrag->to)
        {
            moveElement(model.tracks, trackDrag->from, trackDrag->to);
            changed = true;
        }
        trackDrag.reset();
    }
    else if (clipDrag)
    {
        auto& source = model.tracks[static_cast<size_t>(clipDrag->hit.track)].clips;
        auto clip = source[static_cast<size_t>(clipDrag->hit.clip)];
        if (clipDrag->drop.track != clipDrag->hit.track || clipDrag->drop.startBeat != clip.startBeat)
        {
            clip.startBeat = clipDrag->drop.startBeat;
            source.erase(source.begin() + clipDrag->hit.clip);
            model.tracks[static_cast<size_t>(clipDrag->drop.track)].clips.push_back(clip);
            changed = true;
        }
        clipDrag.reset();
    }

    if (changed && onModelChanged)
        onModelChanged();
    repaint();
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        view.zoomAround(std::max(0.0, static_cast<double>(e.x - layout.headerWidth)), std::exp(wheel.deltaY * 0.8));
    else
        view.scrollByPixels(-(wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 400.0);

    clampView();
    repaint();
}
} // namespace sampler::ui
