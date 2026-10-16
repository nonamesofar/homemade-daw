#pragma once

#include "ui/sampleeditor/EditorMath.h"
#include "ui/sampleeditor/WaveformPeaks.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace sampler::ui::editor
{
/**
    The ruler (bar.beat from the first beat) and the waveform of the open sound, with the slice points, the shaded
    selected slice, the sensitivity preview lines, the audition line and the draggable "1.1.1" marker. It draws what it
    is given and reports gestures; the owner turns them into commands. Zoom and scroll live in `viewport()`.
*/
class WaveformView : public juce::Component
{
public:
    static constexpr int rulerHeight = 26;
    /** How close (pixels) the mouse must be to the first-beat marker to grab it. */
    static constexpr double markerGrabPixels = 7.0;

    WaveformView();

    //==============================================================================
    void setPeaks(std::shared_ptr<WaveformPeaks>);
    bool hasPeaks() const { return peaks != nullptr; }
    void setSound(juce::int64 totalFrames, double sampleRate);
    void setTempo(const Tempo&);
    const Tempo& tempo() const { return musical; }
    /** Slice points (frames, increasing, excluding 0). */
    void setMarkers(std::vector<juce::int64>);
    const std::vector<juce::int64>& markers() const { return sliceMarkers; }
    void setSelectedSlice(int);
    int selectedSlice() const { return selected; }
    /** Where the sensitivity slider would cut; empty = no preview. */
    void setPreviewMarkers(std::vector<juce::int64>);
    /** The audition position as a frame, or -1 for no line. */
    void setPlayheadFrame(juce::int64 frame);

    Viewport& viewport() { return view; }
    const Viewport& viewport() const { return view; }
    SliceGeometry geometry() const { return SliceGeometry{view, sliceMarkers}; }
    void fit();

    //==============================================================================
    /** Zoom or scroll changed. */
    std::function<void()> onViewChanged;
    /** The "1.1.1" marker is dragged: start, each move (frame under the mouse), end. */
    std::function<void()> onFirstBeatDragStart;
    std::function<void(juce::int64 frame)> onFirstBeatDrag;
    std::function<void()> onFirstBeatDragEnd;

    //==============================================================================
    // Editing the slice points by hand (sample-editor spec, "Edit slice points by hand"). Frames come already snapped to a
    // zero crossing within 1 ms unless Alt is held.
    /** A click on the waveform or a slice (selecting is not an undo step). */
    std::function<void(int sliceIndex)> onSelectSlice;
    /** Double-click on empty waveform: add a point here. */
    std::function<void(juce::int64 frame)> onAddPoint;
    /** Double-click on a point, or Delete with a point selected. */
    std::function<void(int markerIndex)> onRemovePoint;
    /** A point is dragged: start, each move, end. */
    std::function<void(int markerIndex)> onMovePointStart;
    std::function<void(int markerIndex, juce::int64 frame)> onMovePoint;
    std::function<void()> onMovePointEnd;

    /** The point selected by a click (-1 for none); Delete removes it. */
    int selectedMarker() const { return selectedPoint; }
    void clearSelectedMarker()
    {
        selectedPoint = -1;
        repaint();
    }
    /** How close (pixels) the mouse must be to a slice point to grab it. */
    static constexpr double pointGrabPixels = 4.0;
    /** The frame a point lands on when placed at `frame`: the nearest zero crossing within 1 ms, unless `exact`. */
    juce::int64 snappedFrame(juce::int64 frame, bool exact) const;

    //==============================================================================
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Rectangle<int> waveArea() const { return getLocalBounds().withTrimmedTop(rulerHeight); }
    bool isDraggingFirstBeat() const { return draggingFirstBeat; }

private:
    bool nearFirstBeat(juce::Point<int>) const;
    void viewChanged();
    void drawRuler(juce::Graphics&);
    void drawWave(juce::Graphics&);

    std::shared_ptr<WaveformPeaks> peaks;
    Viewport view;
    Tempo musical;
    std::vector<juce::int64> sliceMarkers, previewMarkers;
    int selected = 0;
    juce::int64 playhead = -1;
    bool draggingFirstBeat = false;
    int selectedPoint = -1;
    int grabbedPoint = -1; // a point under the mouse since mouse-down; it moves once the mouse is dragged
    bool pointDragStarted = false;
    std::vector<WaveformPeaks::Range> peakCache;
};
} // namespace sampler::ui::editor
