#include "ui/sampleeditor/WaveformView.h"

#include "analysis/Slicer.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

#include <cmath>

namespace sampler::ui::editor
{
WaveformView::WaveformView()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
}

void WaveformView::setPeaks(std::shared_ptr<WaveformPeaks> p)
{
    peaks = std::move(p);
    repaint();
}

void WaveformView::setSound(juce::int64 totalFrames, double sampleRate)
{
    view.totalFrames = totalFrames;
    musical.sampleRate = sampleRate;
    view.setWidth(juce::jmax(1, getWidth()));
    view.clamp();
    repaint();
}

void WaveformView::setTempo(const Tempo& t)
{
    musical = t;
    repaint();
}

void WaveformView::setMarkers(std::vector<juce::int64> markers)
{
    if (markers.size() != sliceMarkers.size())
        selectedPoint = -1; // the indices moved: the selected point is not the same one any more
    sliceMarkers = std::move(markers);
    repaint();
}

void WaveformView::setSelectedSlice(int slice)
{
    selected = slice;
    repaint();
}

void WaveformView::setPreviewMarkers(std::vector<juce::int64> markers)
{
    previewMarkers = std::move(markers);
    repaint();
}

void WaveformView::setPlayheadFrame(juce::int64 frame)
{
    if (frame == playhead)
        return;
    playhead = frame;
    repaint();
}

void WaveformView::fit()
{
    view.fit();
    viewChanged();
}

void WaveformView::resized()
{
    view.setWidth(juce::jmax(1, getWidth()));
    viewChanged();
}

void WaveformView::viewChanged()
{
    repaint();
    if (onViewChanged)
        onViewChanged();
}

//==============================================================================
bool WaveformView::nearFirstBeat(juce::Point<int> p) const
{
    return p.y < rulerHeight && std::abs(view.frameToX(static_cast<double>(musical.firstBeatFrame)) - p.x) <= markerGrabPixels;
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(nearFirstBeat(e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

juce::int64 WaveformView::snappedFrame(juce::int64 frame, bool exact) const
{
    if (exact || peaks == nullptr)
        return juce::jlimit<juce::int64>(0, view.totalFrames, frame);
    const auto radius = static_cast<juce::int64>(musical.sampleRate * 0.001) + 2;
    const auto base = std::max<juce::int64>(0, frame - radius);
    std::vector<float> window;
    peaks->readSamples(base, static_cast<int>(2 * radius + 1), window);
    const auto local = analysis::nearestZeroCrossing(window.data(), static_cast<juce::int64>(window.size()), frame - base, musical.sampleRate);
    return juce::jlimit<juce::int64>(0, view.totalFrames, base + local);
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    grabbedPoint = -1;
    pointDragStarted = false;
    if (nearFirstBeat(e.getMouseDownPosition()))
    {
        draggingFirstBeat = true;
        if (onFirstBeatDragStart)
            onFirstBeatDragStart();
        return;
    }
    if (e.getMouseDownPosition().y < rulerHeight)
        return;
    const auto point = geometry().markerAtX(static_cast<double>(e.x), pointGrabPixels);
    if (point >= 0)
    {
        grabbedPoint = point;
        selectedPoint = point;
        repaint();
        return;
    }
    selectedPoint = -1;
    const auto slice = geometry().sliceAtX(static_cast<double>(e.x));
    if (onSelectSlice)
        onSelectSlice(slice);
    repaint();
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingFirstBeat)
    {
        if (onFirstBeatDrag)
            onFirstBeatDrag(view.xToFrame(e.x));
        return;
    }
    if (grabbedPoint < 0 || ! e.mouseWasDraggedSinceMouseDown())
        return;
    if (! pointDragStarted)
    {
        pointDragStarted = true;
        if (onMovePointStart)
            onMovePointStart(grabbedPoint);
    }
    // Alt: exactly under the mouse; otherwise the nearest zero crossing within 1 ms.
    if (onMovePoint)
        onMovePoint(grabbedPoint, snappedFrame(view.xToFrame(e.x), e.mods.isAltDown()));
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    if (draggingFirstBeat)
    {
        draggingFirstBeat = false;
        if (onFirstBeatDragEnd)
            onFirstBeatDragEnd();
    }
    else if (pointDragStarted && onMovePointEnd)
        onMovePointEnd();
    grabbedPoint = -1;
    pointDragStarted = false;
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.y < rulerHeight)
        return;
    const auto point = geometry().markerAtX(static_cast<double>(e.x), pointGrabPixels);
    if (point >= 0)
    {
        selectedPoint = -1;
        if (onRemovePoint)
            onRemovePoint(point);
        return;
    }
    if (onAddPoint)
        onAddPoint(snappedFrame(view.xToFrame(e.x), e.mods.isAltDown()));
}

bool WaveformView::keyPressed(const juce::KeyPress& key)
{
    if ((key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey)) && selectedPoint >= 0)
    {
        const auto point = selectedPoint;
        selectedPoint = -1;
        if (onRemovePoint)
            onRemovePoint(point);
        return true;
    }
    return false;
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        view.zoomAround(e.position.x, std::pow(2.0, static_cast<double>(wheel.deltaY) * 3.0));
        viewChanged();
        return;
    }
    // Wheel and Shift+wheel both scroll sideways.
    const float amount = std::abs(wheel.deltaX) > 0.0f ? wheel.deltaX : wheel.deltaY;
    view.scrollByPixels(-static_cast<double>(amount) * 300.0);
    viewChanged();
}

//==============================================================================
void WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    drawWave(g);
    drawRuler(g);
}

void WaveformView::drawRuler(juce::Graphics& g)
{
    auto ruler = getLocalBounds().removeFromTop(rulerHeight);
    g.setColour(theme::raised);
    g.fillRect(ruler);
    g.setColour(theme::border);
    g.drawHorizontalLine(rulerHeight - 1, 0.0f, static_cast<float>(getWidth()));

    g.setFont(Fonts::mono(10.0f));
    for (const auto& tick : rulerTicks(view, musical))
    {
        const auto x = static_cast<float>(tick.x);
        g.setColour(tick.isBar ? theme::muted : theme::dashed);
        g.drawVerticalLine(juce::roundToInt(x), tick.isBar ? 6.0f : 14.0f, static_cast<float>(rulerHeight - 1));
        if (tick.label.isNotEmpty())
        {
            g.setColour(theme::muted);
            g.drawText(tick.label, juce::Rectangle<float>(x + 3.0f, 2.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
        }
    }

    // The first beat: a handle to drag.
    const auto fx = static_cast<float>(view.frameToX(static_cast<double>(musical.firstBeatFrame)));
    if (fx > -40.0f && fx < static_cast<float>(getWidth()) + 40.0f)
    {
        g.setColour(theme::accent);
        g.drawVerticalLine(juce::roundToInt(fx), 0.0f, static_cast<float>(getHeight()));
        juce::Path flag;
        flag.addTriangle(fx, static_cast<float>(rulerHeight - 2), fx + 7.0f, static_cast<float>(rulerHeight - 12), fx - 7.0f,
                         static_cast<float>(rulerHeight - 12));
        g.fillPath(flag);
        g.setFont(Fonts::mono(10.0f, true));
        g.drawText("1.1.1", juce::Rectangle<float>(fx + 9.0f, static_cast<float>(rulerHeight - 14), 40.0f, 12.0f), juce::Justification::centredLeft);
    }
}

void WaveformView::drawWave(juce::Graphics& g)
{
    const auto area = waveArea();
    const float mid = static_cast<float>(area.getCentreY());
    const float amp = static_cast<float>(area.getHeight()) * 0.5f * 0.92f;
    const int width = getWidth();

    // Selected slice shading.
    if (! sliceMarkers.empty() || selected == 0)
    {
        const auto span = geometry().sliceSpan(selected);
        if (span.second > span.first)
        {
            g.setColour(theme::accent.withAlpha(0.14f));
            g.fillRect(juce::Rectangle<float>(static_cast<float>(span.first), static_cast<float>(area.getY()),
                                              static_cast<float>(span.second - span.first), static_cast<float>(area.getHeight())));
        }
    }

    g.setColour(theme::border);
    g.drawHorizontalLine(juce::roundToInt(mid), 0.0f, static_cast<float>(width));

    if (peaks == nullptr)
    {
        g.setColour(theme::muted);
        g.setFont(Fonts::sans(13.0f));
        g.drawText("Loading waveform...", area, juce::Justification::centred);
    }
    else if (view.showsSamples())
    {
        const auto first = static_cast<juce::int64>(std::floor(view.startFrame));
        const int count = static_cast<int>(std::ceil(view.visibleFrames())) + 3;
        std::vector<float> samples;
        peaks->readSamples(first, count, samples);
        juce::Path line;
        for (int i = 0; i < count; ++i)
        {
            const float x = static_cast<float>(view.frameToX(static_cast<double>(first + i)));
            const float y = mid - samples[static_cast<size_t>(i)] * amp;
            if (i == 0)
                line.startNewSubPath(x, y);
            else
                line.lineTo(x, y);
        }
        g.setColour(theme::text);
        g.strokePath(line, juce::PathStrokeType(1.2f));
        if (view.framesPerPixel <= 0.25)
        {
            g.setColour(theme::accent);
            for (int i = 0; i < count; ++i)
                g.fillEllipse(static_cast<float>(view.frameToX(static_cast<double>(first + i))) - 2.0f,
                              mid - samples[static_cast<size_t>(i)] * amp - 2.0f, 4.0f, 4.0f);
        }
    }
    else
    {
        peaks->fillPeaks(view.startFrame, view.framesPerPixel, width, peakCache);
        // One batch of pixel-wide columns: much cheaper than a fill call per column.
        juce::RectangleList<float> columns;
        for (int x = 0; x < width; ++x)
        {
            const auto& r = peakCache[static_cast<size_t>(x)];
            const float top = mid - juce::jlimit(-1.0f, 1.0f, r.max) * amp;
            const float bottom = mid - juce::jlimit(-1.0f, 1.0f, r.min) * amp;
            columns.addWithoutMerging(juce::Rectangle<float>(static_cast<float>(x), top, 1.0f, juce::jmax(1.0f, bottom - top)));
        }
        g.setColour(theme::muted);
        g.fillRectList(columns);
    }

    // Slice points and the preview of the sensitivity.
    g.setColour(theme::accent);
    for (size_t i = 0; i < sliceMarkers.size(); ++i)
    {
        const auto x = juce::roundToInt(view.frameToX(static_cast<double>(sliceMarkers[i])));
        if (x < -2 || x >= width + 2)
            continue;
        const bool isSelected = static_cast<int>(i) == selectedPoint;
        g.fillRect(static_cast<float>(x) - (isSelected ? 1.0f : 0.0f), static_cast<float>(area.getY()), isSelected ? 3.0f : 1.0f,
                   static_cast<float>(area.getHeight()));
    }
    g.setColour(theme::ok.withAlpha(0.8f));
    for (const auto marker : previewMarkers)
    {
        const auto x = juce::roundToInt(view.frameToX(static_cast<double>(marker)));
        if (x >= 0 && x < width)
            for (int y = area.getY(); y < area.getBottom(); y += 6)
                g.fillRect(x, y, 1, 3);
    }

    if (playhead >= 0)
    {
        g.setColour(theme::text);
        g.drawVerticalLine(juce::roundToInt(view.frameToX(static_cast<double>(playhead))), static_cast<float>(area.getY()),
                           static_cast<float>(area.getBottom()));
    }
}
} // namespace sampler::ui::editor
