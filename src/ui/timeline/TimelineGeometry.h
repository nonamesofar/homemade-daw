#pragma once

// Timeline logic with no GUI dependency, so Catch2 can test it: viewport math, hit-testing,
// clip-drop resolution and track-reorder index math. Positions are in beats; pixels are doubles.
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace sampler::ui
{
//==============================================================================
// Model (spike: plain data; the real one is the Tracktion edit + SAMPLER subtree).
struct ClipModel
{
    int id = 0;
    double startBeat = 0.0;
    double lengthBeats = 0.0;
    int sourceIndex = 0; // which audio source the waveform comes from
};

struct TrackModel
{
    int id = 0;
    std::string name;
    std::vector<ClipModel> clips;
};

struct TimelineModel
{
    std::vector<TrackModel> tracks;

    double endBeat() const
    {
        double end = 0.0;
        for (const auto& t : tracks)
            for (const auto& c : t.clips)
                end = std::max(end, c.startBeat + c.lengthBeats);
        return end;
    }
};

//==============================================================================
/** Horizontal view of the timeline: which beats are visible and how wide a beat is. */
struct Viewport
{
    double pixelsPerBeat = 40.0;
    double firstBeat = 0.0;
    double widthPx = 800.0;

    static constexpr double minPixelsPerBeat = 2.0;
    static constexpr double maxPixelsPerBeat = 400.0;

    double beatToX(double beat) const { return (beat - firstBeat) * pixelsPerBeat; }
    double xToBeat(double x) const { return firstBeat + x / pixelsPerBeat; }
    double lastBeat() const { return xToBeat(widthPx); }

    /** Zooms by `factor` (>1 zooms in) keeping the beat under `anchorX` where it is. */
    void zoomAround(double anchorX, double factor)
    {
        const auto anchorBeat = xToBeat(anchorX);
        pixelsPerBeat = std::clamp(pixelsPerBeat * factor, minPixelsPerBeat, maxPixelsPerBeat);
        firstBeat = anchorBeat - anchorX / pixelsPerBeat;
    }

    void scrollByPixels(double dx) { firstBeat += dx / pixelsPerBeat; }

    /** Keeps the view inside [0, contentBeats + a screen's worth of room]. */
    void clamp(double contentBeats)
    {
        const auto maxFirst = std::max(0.0, contentBeats - widthPx / pixelsPerBeat * 0.25);
        firstBeat = std::clamp(firstBeat, 0.0, maxFirst);
    }
};

//==============================================================================
/** Pixel layout of the timeline component. */
struct Layout
{
    int headerWidth = 140;
    int rulerHeight = 24;
    int trackHeight = 90;

    int trackTop(int trackIndex) const { return rulerHeight + trackIndex * trackHeight; }
    int contentHeight(int numTracks) const { return rulerHeight + numTracks * trackHeight; }

    /** Track row under component-space y, or -1 over the ruler / below the last track. */
    int trackIndexAt(double y, int numTracks) const
    {
        if (y < rulerHeight)
            return -1;
        const auto index = static_cast<int>((y - rulerHeight) / trackHeight);
        return index < numTracks ? index : -1;
    }
};

struct ClipHit
{
    int track = -1;
    int clip = -1; // index inside the track
    bool nearLeftEdge = false;
    bool nearRightEdge = false;
};

/** Topmost clip under (x, y) in component space; empty if none. x is measured from the component's left edge. */
inline std::optional<ClipHit> hitTestClip(const TimelineModel& model, const Layout& layout, const Viewport& view,
                                          double x, double y, double edgePx = 6.0)
{
    if (x < layout.headerWidth)
        return std::nullopt;

    const auto track = layout.trackIndexAt(y, static_cast<int>(model.tracks.size()));
    if (track < 0)
        return std::nullopt;

    const auto beat = view.xToBeat(x - layout.headerWidth);
    const auto& clips = model.tracks[static_cast<size_t>(track)].clips;
    for (int i = static_cast<int>(clips.size()) - 1; i >= 0; --i) // later clips are drawn on top
    {
        const auto& c = clips[static_cast<size_t>(i)];
        if (beat >= c.startBeat && beat < c.startBeat + c.lengthBeats)
        {
            ClipHit hit;
            hit.track = track;
            hit.clip = i;
            hit.nearLeftEdge = (x - layout.headerWidth) - view.beatToX(c.startBeat) < edgePx;
            hit.nearRightEdge = view.beatToX(c.startBeat + c.lengthBeats) - (x - layout.headerWidth) < edgePx;
            return hit;
        }
    }
    return std::nullopt;
}

/** Index of the track header under (x, y), or -1. */
inline int hitTestHeader(const Layout& layout, int numTracks, double x, double y)
{
    return x >= 0 && x < layout.headerWidth ? layout.trackIndexAt(y, numTracks) : -1;
}

//==============================================================================
inline double snapToGrid(double beat, double gridBeats)
{
    return gridBeats > 0.0 ? std::round(beat / gridBeats) * gridBeats : beat;
}

struct ClipDrop
{
    int track = 0;
    double startBeat = 0.0;
};

/**
    Where a dragged clip lands. `grabOffsetBeats` is how far into the clip the mouse grabbed it, so the clip
    does not jump to the cursor. The track is clamped to the existing tracks, the start to >= 0, and snapped.
*/
inline ClipDrop resolveClipDrop(const Layout& layout, const Viewport& view, int numTracks, double mouseX, double mouseY,
                                double grabOffsetBeats, double gridBeats)
{
    ClipDrop drop;
    const auto rawTrack = static_cast<int>(std::floor((mouseY - layout.rulerHeight) / layout.trackHeight));
    drop.track = std::clamp(rawTrack, 0, std::max(0, numTracks - 1));
    const auto beat = view.xToBeat(mouseX - layout.headerWidth) - grabOffsetBeats;
    drop.startBeat = std::max(0.0, snapToGrid(beat, gridBeats));
    return drop;
}

//==============================================================================
/** Index a dragged track would take if released with the mouse at `mouseY`. */
inline int reorderTargetIndex(const Layout& layout, int numTracks, double mouseY)
{
    const auto raw = static_cast<int>(std::floor((mouseY - layout.rulerHeight) / layout.trackHeight));
    return std::clamp(raw, 0, std::max(0, numTracks - 1));
}

/** Moves the element at `from` so it ends up at index `to` (the others keep their order). */
template <typename T>
void moveElement(std::vector<T>& items, int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= static_cast<int>(items.size()) || to >= static_cast<int>(items.size()))
        return;
    auto item = std::move(items[static_cast<size_t>(from)]);
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, std::move(item));
}

/** Where to draw the insertion marker while dragging a track: the y of the top edge of the target slot. */
inline int reorderMarkerY(const Layout& layout, int from, int to)
{
    // Dropping lower than where it started puts the track after the one at `to`, so mark that row's bottom edge.
    return layout.trackTop(to) + (to > from ? layout.trackHeight : 0);
}
} // namespace sampler::ui
