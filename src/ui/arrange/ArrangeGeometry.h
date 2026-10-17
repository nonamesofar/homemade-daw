#pragma once

// Arrangement geometry with no GUI dependency, so Catch2 tests it: the viewport (horizontal and vertical), the
// content-length rule, clip hit-testing with edge zones, marquee intersection, drop resolution (including below the last
// lane), loop-bar hit-testing and track-reorder index maths. Grown from the M0 spike's TimelineGeometry.h.
// Positions in beats; pixels are doubles. Lane coordinates: x = 0 at the lanes' left edge, y = 0 at the top of track 1
// (before vertical scrolling).
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace sampler::ui
{
//==============================================================================
/** Pixel layout of the arrangement (mock: 220 px headers, 112 px lanes, 44 px "+ Audio track" row). */
struct ArrangeLayout
{
    int headerWidth = 220;
    int loopBarHeight = 12;
    int rulerHeight = 24;
    int trackHeight = 112;
    int addRowHeight = 44;
    int clipInset = 8;

    int topHeight() const { return loopBarHeight + rulerHeight; }
    double trackTop(int index) const { return static_cast<double>(index) * trackHeight; }
    double contentHeight(int numTracks) const { return static_cast<double>(numTracks) * trackHeight + addRowHeight; }

    /** Track under lane y (scrolled already removed), -1 above, `numTracks` (one past the last) below the last lane. */
    int trackAt(double y, int numTracks) const
    {
        if (y < 0.0)
            return -1;
        return std::min(numTracks, static_cast<int>(std::floor(y / trackHeight)));
    }
};

//==============================================================================
/** What is visible: beats horizontally, pixels vertically. */
struct ArrangeViewport
{
    double pixelsPerBeat = 24.0;
    double firstBeat = 0.0;
    double widthPx = 800.0;
    double scrollY = 0.0;
    double heightPx = 400.0;

    static constexpr double minPixelsPerBeat = 1.0;
    static constexpr double maxPixelsPerBeat = 600.0;

    double beatToX(double beat) const { return (beat - firstBeat) * pixelsPerBeat; }
    double xToBeat(double x) const { return firstBeat + x / pixelsPerBeat; }
    double lastBeat() const { return xToBeat(widthPx); }
    double visibleBeats() const { return widthPx / pixelsPerBeat; }

    /** Zooms by `factor` (>1 zooms in) keeping the beat under `anchorX` where it is. */
    void zoomAround(double anchorX, double factor)
    {
        const auto anchorBeat = xToBeat(anchorX);
        pixelsPerBeat = std::clamp(pixelsPerBeat * factor, minPixelsPerBeat, maxPixelsPerBeat);
        firstBeat = anchorBeat - anchorX / pixelsPerBeat;
    }

    /** Shows [start, end) across the whole width. */
    void showRange(double start, double end)
    {
        if (end <= start)
            return;
        pixelsPerBeat = std::clamp(widthPx / (end - start), minPixelsPerBeat, maxPixelsPerBeat);
        firstBeat = std::max(0.0, start);
    }

    void scrollByPixels(double dx) { firstBeat += dx / pixelsPerBeat; }

    /** Keeps the view inside the content: horizontally [0, contentBeats], vertically [0, contentHeight]. */
    void clamp(double contentBeats, double contentHeight)
    {
        firstBeat = std::clamp(firstBeat, 0.0, std::max(0.0, contentBeats - visibleBeats()));
        scrollY = std::clamp(scrollY, 0.0, std::max(0.0, contentHeight - heightPx));
    }
};

/** The scrollable length: at least 16 bars, and 8 bars past the last clip (arrangement spec). */
inline double contentBeats(double endBeat, double beatsPerBar)
{
    return std::max(16.0 * beatsPerBar, endBeat + 8.0 * beatsPerBar);
}

//==============================================================================
/** A clip as the geometry sees it. */
struct LaneClip
{
    std::string id;
    int track = 0;
    double start = 0.0;
    double length = 0.0;
    double fadeIn = 0.0;  // beats; where the fade handles sit
    double fadeOut = 0.0;
    double end() const { return start + length; }
};

/** A clip's rectangle in lane coordinates (before vertical scrolling). */
struct LaneRect
{
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    bool contains(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    bool intersects(const LaneRect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

inline LaneRect clipRect(const LaneClip& c, const ArrangeLayout& layout, const ArrangeViewport& view)
{
    const auto x0 = view.beatToX(c.start);
    const auto x1 = view.beatToX(c.end());
    return {x0, layout.trackTop(c.track) + layout.clipInset, std::max(1.0, x1 - x0),
            static_cast<double>(layout.trackHeight - 2 * layout.clipInset)};
}

enum class ClipZone
{
    body,
    leftEdge,
    rightEdge,
    fadeInHandle,  // top corners: sit at the end of the fade (at the corner while there is none)
    fadeOutHandle
};

struct ClipHit
{
    std::string id;
    ClipZone zone = ClipZone::body;
};

/**
    The clip under (x, y) in lane coordinates with `view.scrollY` applied by the caller (y is content y). Edges are
    `edgePx` wide inside the clip, and never more than a third of a narrow clip, so its body stays draggable. The fade
    handles are `handlePx` either side of where each fade ends and win over the edges in the top `handleBandPx` of the
    clip only; a clip narrower than four handles has none.
*/
inline std::optional<ClipHit> hitTestClip(const std::vector<LaneClip>& clips, const ArrangeLayout& layout,
                                          const ArrangeViewport& view, double x, double y, double edgePx = 6.0,
                                          double handlePx = 6.0, double handleBandPx = 8.0)
{
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        const auto r = clipRect(*it, layout, view);
        if (! r.contains(x, y))
            continue;
        if (y - r.y < handleBandPx && r.w >= 4.0 * handlePx)
        {
            const auto inX = r.x + std::clamp(it->fadeIn * view.pixelsPerBeat, 0.0, r.w);
            const auto outX = r.x + r.w - std::clamp(it->fadeOut * view.pixelsPerBeat, 0.0, r.w);
            const auto nearIn = std::abs(x - inX) <= handlePx;
            const auto nearOut = std::abs(x - outX) <= handlePx;
            // Handles on the same point (the fades fill the clip): neither fade can grow, so each can only be shrunk, the fade
            // in by dragging left and the fade out by dragging right. The mouse side picks the one that can move that way.
            const auto together = nearIn && nearOut && std::abs(inX - outX) < 1.0e-6;
            if (together)
                return ClipHit{it->id, x < inX ? ClipZone::fadeInHandle : ClipZone::fadeOutHandle};
            if (nearIn && (! nearOut || std::abs(x - inX) <= std::abs(x - outX)))
                return ClipHit{it->id, ClipZone::fadeInHandle};
            if (nearOut)
                return ClipHit{it->id, ClipZone::fadeOutHandle};
        }
        const auto edge = std::min(edgePx, r.w / 3.0);
        ClipHit hit{it->id, ClipZone::body};
        if (x - r.x < edge)
            hit.zone = ClipZone::leftEdge;
        else if (r.x + r.w - x < edge)
            hit.zone = ClipZone::rightEdge;
        return hit;
    }
    return std::nullopt;
}

/** Ids of the clips the marquee (lane coordinates, content y) touches. */
inline std::vector<std::string> clipsInMarquee(const std::vector<LaneClip>& clips, const ArrangeLayout& layout,
                                               const ArrangeViewport& view, LaneRect marquee)
{
    if (marquee.w < 0)
    {
        marquee.x += marquee.w;
        marquee.w = -marquee.w;
    }
    if (marquee.h < 0)
    {
        marquee.y += marquee.h;
        marquee.h = -marquee.h;
    }
    std::vector<std::string> ids;
    for (const auto& c : clips)
    {
        // Touching the lane between the clip's top and bottom insets counts too.
        auto r = clipRect(c, layout, view);
        r.y = layout.trackTop(c.track);
        r.h = layout.trackHeight;
        if (r.intersects(marquee))
            ids.push_back(c.id);
    }
    return ids;
}

//==============================================================================
/** Where a dragged sound lands: a track index (or a new track below the last) and the start beat. */
struct DropTarget
{
    int track = 0;
    bool newTrack = false;
    double beat = 0.0;
};

/** Content y and lane x -> drop target; `snapBeat` snaps the raw beat (may be identity). */
template <typename SnapFn>
DropTarget resolveDrop(const ArrangeLayout& layout, const ArrangeViewport& view, int numTracks, double x, double contentY,
                       SnapFn&& snapBeat)
{
    DropTarget target;
    const auto track = layout.trackAt(contentY, numTracks);
    target.newTrack = track >= numTracks || numTracks == 0;
    target.track = std::clamp(track, 0, std::max(0, numTracks - 1));
    target.beat = std::max(0.0, snapBeat(view.xToBeat(x)));
    return target;
}

/** Where dragged clips go: the grabbed clip's new start (snapped) and the track delta, both clamped. */
struct MoveTarget
{
    int trackDelta = 0;
    double beatDelta = 0.0;
};

/**
    `grabStart`/`grabTrack`: the grabbed clip; `rawStart`: where its start would be under the mouse; `minTrack` and
    `maxTrack`: the topmost and lowest selected tracks (the group may not leave the tracks); `earliest`: the earliest
    selected start (the group may not go before bar 1).
*/
inline MoveTarget clampMove(double grabStart, double snappedStart, int grabTrack, int mouseTrack, int numTracks, int minTrack,
                            int maxTrack, double earliest)
{
    MoveTarget t;
    t.trackDelta = std::clamp(mouseTrack - grabTrack, -minTrack, numTracks - 1 - maxTrack);
    t.beatDelta = std::max(snappedStart - grabStart, -earliest);
    return t;
}

//==============================================================================
enum class LoopZone
{
    none,
    start,
    end,
    body
};

/** Which part of the loop bar is under x (edges are `edgePx` wide); `none` outside the range or with no range. */
inline LoopZone hitTestLoop(double loopStart, double loopEnd, const ArrangeViewport& view, double x, double edgePx = 6.0)
{
    if (loopEnd <= loopStart)
        return LoopZone::none;
    const auto x0 = view.beatToX(loopStart), x1 = view.beatToX(loopEnd);
    const auto d0 = std::abs(x - x0), d1 = std::abs(x - x1);
    if (d0 <= edgePx && d0 < d1) // both edges in reach (a narrow loop): the nearer one
        return LoopZone::start;
    if (d1 <= edgePx)
        return LoopZone::end;
    if (d0 <= edgePx)
        return LoopZone::start;
    return x > x0 && x < x1 ? LoopZone::body : LoopZone::none;
}

/** The loop range while dragging a zone by `deltaBeats` (already snapped), at least `minLength` long, not before 0 (a loop already shorter keeps its start when dragged inward). */
inline std::pair<double, double> dragLoop(LoopZone zone, double start, double end, double deltaBeats, double minLength)
{
    switch (zone)
    {
        case LoopZone::start: return {std::clamp(start + deltaBeats, 0.0, std::max(start, end - minLength)), end};
        case LoopZone::end: return {start, std::max(end + deltaBeats, start + minLength)};
        case LoopZone::body:
        {
            const auto d = std::max(deltaBeats, -start);
            return {start + d, end + d};
        }
        case LoopZone::none: break;
    }
    return {start, end};
}

/** A new range drawn from `anchor` to `current` (either direction), at least `minLength`. */
inline std::pair<double, double> drawLoop(double anchor, double current, double minLength)
{
    auto a = std::max(0.0, std::min(anchor, current));
    auto b = std::max(anchor, current);
    if (b - a < minLength)
        b = a + minLength;
    return {a, b};
}

//==============================================================================
/** Index a dragged track takes when released with its header's mouse at content y. */
inline int reorderTargetIndex(const ArrangeLayout& layout, int numTracks, double contentY)
{
    const auto raw = static_cast<int>(std::floor(contentY / layout.trackHeight));
    return std::clamp(raw, 0, std::max(0, numTracks - 1));
}

/** Where the insertion line goes while dragging track `from` to `to`: content y of the line. */
inline double reorderMarkerY(const ArrangeLayout& layout, int from, int to)
{
    return layout.trackTop(to) + (to > from ? layout.trackHeight : 0);
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

/** Auto-scroll speed (pixels per tick) when the mouse is within `zone` px of an edge of a `height`-tall list. */
inline double edgeScrollSpeed(double y, double height, double zone = 24.0, double maxSpeed = 16.0)
{
    if (y < zone)
        return -maxSpeed * std::min(1.0, (zone - y) / zone);
    if (y > height - zone)
        return maxSpeed * std::min(1.0, (y - (height - zone)) / zone);
    return 0.0;
}

/** Bar lines and labels for the ruler: label every `labelEveryBars` bars so labels stay at least `minLabelPx` apart. */
inline int rulerLabelStep(double pixelsPerBeat, double beatsPerBar, double minLabelPx = 36.0)
{
    const auto barPx = pixelsPerBeat * beatsPerBar;
    int step = 1;
    while (barPx * step < minLabelPx && step < 4096)
        step *= 2;
    return step;
}
} // namespace sampler::ui
