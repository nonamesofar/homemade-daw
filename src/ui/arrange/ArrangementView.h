#pragma once

#include "ArrangeGeometry.h"
#include "ArrangeLogic.h"
#include "commands/ArrangeCommands.h"
#include "commands/CommandService.h"
#include "engine/Arrangement.h"
#include "engine/LevelMeters.h"
#include "engine/Transport.h"
#include "ui/widgets/Widgets.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace sampler::ui
{
/** A file import whose clips will land on the timeline (grey boxes until it is done). */
struct Placeholder
{
    commands::PlacementTarget target;
    int count = 1;
};

/**
    What the arrangement view works with. The view reads the arrangement (const) and changes the project only through
    the command service (rule 2 of §4.2); transport, meters, selection and snap are not undoable.
*/
struct ArrangeContext
{
    const engine::Arrangement& arrangement;
    engine::Transport& transport;
    engine::LevelMeters& meters;
    commands::CommandService& commands;
    SelectionModel& selection;
    juce::ValueTree sampler; // read only: names and lengths of the sounds
    std::function<std::unique_ptr<juce::AudioThumbnailBase>(const juce::String& sourceId, juce::Component& repaintTarget)> makeThumbnail;
    std::function<SnapValue()> snap;
    /** Explorer files dropped on the lanes: import them and place their clips. */
    std::function<void(const juce::Array<juce::File>&, commands::PlacementTarget)> importAndPlace;
    /** Imports with a placement that are still running. */
    std::function<std::vector<Placeholder>()> placeholders;
};

class ArrangementView;

//==============================================================================
/** One track header: colour, name (double-click renames), M, S, R (disabled), volume readout, pan and meter. */
class TrackHeader : public juce::Component, public juce::SettableTooltipClient, private juce::TextEditor::Listener
{
public:
    explicit TrackHeader(ArrangementView&);
    ~TrackHeader() override;

    void setTrack(const engine::TrackInfo&, bool selected);
    const juce::String& trackId() const { return info.id; }
    const engine::TrackInfo& track() const { return info; }

    void startRename();
    bool isRenaming() const { return nameEditor != nullptr; }
    juce::TextEditor* renameEditor() { return nameEditor.get(); }
    /** The meter reading of one frame. */
    void pushLevel(const engine::LevelReading&, double elapsedMs);
    const MeterBallistics& meter() const { return ballistics; }

    ToggleButton muteButton{"M"}, soloButton{"S"};
    juce::TextButton armButton{"R"};
    ValueField volume;
    Knob pan;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void textEditorReturnKeyPressed(juce::TextEditor&) override { finishRename(true); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { finishRename(false); }
    void textEditorFocusLost(juce::TextEditor&) override { finishRename(true); }
    void finishRename(bool keep);
    juce::Rectangle<int> nameArea() const;
    juce::Rectangle<int> meterArea() const;

    ArrangementView& view;
    engine::TrackInfo info;
    bool selected = false;
    std::unique_ptr<juce::TextEditor> nameEditor;
    MeterBallistics ballistics;
};

//==============================================================================
/** The headers on the left: drag a header to reorder, "+ Audio track" below the last one. */
class TrackHeaderList : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeaderList(ArrangementView&);

    void rebuild();
    void layoutHeaders();
    int numHeaders() const { return static_cast<int>(headers.size()); }
    TrackHeader* header(int index) { return juce::isPositiveAndBelow(index, numHeaders()) ? headers[static_cast<size_t>(index)].get() : nullptr; }
    TrackHeader* headerFor(const juce::String& trackId);
    juce::TextButton& addTrackButton() { return addButton; }

    // Reorder drag, called by the headers (positions in this component's coordinates).
    void headerPressed(int index, juce::Point<int>);
    void headerDragged(juce::Point<int>);
    void headerReleased();
    bool isReordering() const { return reorderTarget >= 0; }
    int reorderTargetIndex() const { return reorderTarget; }

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override { layoutHeaders(); }

private:
    void timerCallback() override;

    ArrangementView& view;
    std::vector<std::unique_ptr<TrackHeader>> headers;
    juce::TextButton addButton{"+ Audio track"};
    int pressedIndex = -1, reorderTarget = -1;
    juce::Point<int> pressPoint, lastPoint;
};

//==============================================================================
/** The loop bar and the bar ruler above the lanes. */
class TimelineRuler : public juce::Component
{
public:
    explicit TimelineRuler(ArrangementView&);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    /** The loop range shown while dragging (or the arrangement's). */
    engine::LoopRange shownLoop() const;
    bool isDraggingLoop() const { return loopDrag != LoopZone::none || drawingLoop; }
    void cancelDrag();

private:
    bool inLoopBar(int y) const;

    ArrangementView& view;
    LoopZone loopDrag = LoopZone::none;
    bool drawingLoop = false;
    double dragAnchorBeat = 0.0;
    engine::LoopRange startLoop, previewLoop;
};

//==============================================================================
/** The lanes: grid, clips with waveforms, drops, selection, moving and trimming. */
class TimelineLanes : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    explicit TimelineLanes(ArrangementView&);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int x, int y) override;

    /** True while a clip move, trim or marquee is in progress. */
    bool isDragging() const { return mode != Mode::idle; }
    void cancelDrag();
    /** Where the ghost of a dragged sound would land (for tests and painting). */
    std::optional<DropTarget> ghostTarget() const { return ghost; }
    /** The clips' rectangles as drawn, in this component's coordinates. */
    juce::Rectangle<int> boundsOfClip(const juce::String& clipId) const;
    /** Paints with the waveforms of every visible clip (performance test). */
    int clipsDrawnInLastPaint() const { return lastDrawnClips; }

private:
    enum class Mode
    {
        idle,
        move,
        trimLeft,
        trimRight,
        marquee
    };

    struct Preview
    {
        juce::String id;
        int track = 0;
        double start = 0.0, length = 0.0;
    };

    /** A clip's waveform drawn once into an image and reused while the view scrolls or zooms (design risk: 8 ms frame budget). */
    struct WaveImage
    {
        juce::Image image;
        juce::String key;   // what it was drawn from: sound, region, direction, size
        double widthPx = 0; // the clip width it was drawn for
        juce::uint32 usedFrame = 0;
    };

    double contentY(double y) const;
    DropTarget dropTargetAt(juce::Point<float>, bool bypassSnap) const;
    commands::PlacementTarget placementFor(const DropTarget&) const;
    void paintClip(juce::Graphics&, const engine::ClipInfo&, juce::Rectangle<float>, juce::Colour, bool selected, bool ghostOnly);
    void paintWaveform(juce::Graphics&, const engine::ClipInfo&, juce::Rectangle<float>, juce::Colour);
    void updateMove(const juce::MouseEvent&);
    void updateTrim(const juce::MouseEvent&);
    void commitDrag();

    ArrangementView& view;
    Mode mode = Mode::idle;
    juce::String grabbedId;
    double grabOffsetBeats = 0.0;
    juce::Point<float> downPos, lastPos;
    std::vector<Preview> previews;
    std::vector<juce::String> selectionBeforeMarquee;
    std::optional<DropTarget> ghost;
    double ghostLengthBeats = 0.0;
    juce::String ghostLabel;
    int ghostCount = 1;
    std::optional<double> snapGuideBeat;
    bool movedEnough = false;
    int lastDrawnClips = 0;
    std::map<juce::String, WaveImage> waveCache;
    juce::uint32 paintFrame = 0;
    int waveBuildsThisPaint = 0;
};

//==============================================================================
/** The playhead line over the ruler and lanes. Repaints only the strips it leaves and enters. */
class PlayheadOverlay : public juce::Component
{
public:
    PlayheadOverlay() { setInterceptsMouseClicks(false, false); }
    void setPlayheadX(std::optional<float> x);
    std::optional<float> playheadX() const { return current; }
    void paint(juce::Graphics&) override;

private:
    std::optional<float> current;
};

//==============================================================================
/**
    The arrangement view (design D-7): track headers, ruler with loop bar, lanes, playhead and scroll bars, drawn from
    the arrangement itself (no mirrored model: only a per-track sorted clip index for culling, rebuilt when the
    arrangement's generation changes).
*/
class ArrangementView : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener
{
public:
    explicit ArrangementView(ArrangeContext);
    ~ArrangementView() override;

    ArrangeContext& context() { return ctx; }
    const ArrangeLayout& layout() const { return geometry; }
    ArrangeViewport& viewport() { return view; }
    const ArrangeViewport& viewport() const { return view; }

    TrackHeaderList& headers() { return headerList; }
    TimelineRuler& ruler() { return rulerComponent; }
    TimelineLanes& lanes() { return lanesComponent; }
    PlayheadOverlay& playhead() { return playheadOverlay; }
    juce::ScrollBar& horizontalScrollBar() { return hScroll; }
    juce::ScrollBar& verticalScrollBar() { return vScroll; }

    /** Re-reads the arrangement (done on every change of it). */
    void refresh();
    /** One display frame: playhead, meters, renders (the VBlankAttachment calls it; tests call it directly). */
    void tick(double elapsedMs);

    // Data for the parts (rebuilt in refresh).
    const std::vector<engine::TrackInfo>& trackList() const { return tracks; }
    const std::vector<engine::ClipInfo>& clipsOnTrack(int index) const;
    const engine::ClipInfo* findClip(const juce::String& id) const;
    const std::vector<LaneClip>& laneClips() const { return allLaneClips; }
    int trackIndexOf(const juce::String& trackId) const;
    double contentLengthBeats() const;

    SnapValue snapValue() const { return ctx.snap ? ctx.snap() : SnapValue::bar; }
    engine::TimeSig timeSig() const { return sig; }
    double snapBeat(double beat, bool bypass) const { return Snap::snap(beat, snapValue(), sig, bypass); }
    double minimumLength() const;
    juce::Colour colourOfTrack(int index) const;
    juce::AudioThumbnailBase* thumbnailFor(const juce::String& sourceId);
    juce::String sourceName(const juce::String& sourceId) const;
    double sourceSeconds(const juce::String& sourceId) const;

    /** Zoom (Z / Shift+Z), keyboard scroll and zoom around a point. */
    void zoomToSelection();
    void zoomToSong();
    void zoomAround(double laneX, double factor);
    void scrollBy(double dxPixels, double dyPixels);
    void viewChanged();

    /** Apply helpers used by the parts (selection is updated with the result). */
    void applyCommand(commands::ArrangeCommand&, bool selectResult);
    void locate(double beat) { ctx.transport.locateBeats(beat); }

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBars();

    ArrangeContext ctx;
    ArrangeLayout geometry;
    ArrangeViewport view;
    std::vector<engine::TrackInfo> tracks;
    std::vector<std::vector<engine::ClipInfo>> clipsByTrack;
    std::vector<LaneClip> allLaneClips;
    engine::TimeSig sig;
    int shownGeneration = -1;
    bool anyRendering = false;
    double renderPollMs = 0.0;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnailBase>> thumbnails;

    TrackHeaderList headerList;
    TimelineRuler rulerComponent;
    TimelineLanes lanesComponent;
    PlayheadOverlay playheadOverlay;
    juce::ScrollBar hScroll{false}, vScroll{true};
    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastVBlank = 0.0;
    bool updatingScrollBars = false;
    bool initialZoomDone = false;
};
} // namespace sampler::ui
