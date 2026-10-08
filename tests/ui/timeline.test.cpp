#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/timeline/TimelineGeometry.h"

using namespace sampler::ui;
using Catch::Approx;

namespace
{
TimelineModel makeModel()
{
    TimelineModel m;
    for (int t = 0; t < 4; ++t)
    {
        TrackModel track;
        track.id = t + 1;
        track.name = "Track " + std::to_string(t + 1);
        for (int c = 0; c < 3; ++c)
            track.clips.push_back({t * 10 + c, c * 8.0, 6.0, c});
        m.tracks.push_back(track);
    }
    return m;
}
} // namespace

TEST_CASE("viewport converts between beats and pixels", "[ui][viewport]")
{
    Viewport v;
    v.pixelsPerBeat = 40.0;
    v.firstBeat = 4.0;
    CHECK(v.beatToX(4.0) == Approx(0.0));
    CHECK(v.beatToX(6.0) == Approx(80.0));
    CHECK(v.xToBeat(80.0) == Approx(6.0));
    CHECK(v.xToBeat(v.beatToX(13.37)) == Approx(13.37));
}

TEST_CASE("zooming keeps the beat under the mouse in place", "[ui][viewport]")
{
    Viewport v;
    v.pixelsPerBeat = 40.0;
    v.firstBeat = 10.0;

    const double anchorX = 300.0;
    const auto beatBefore = v.xToBeat(anchorX);
    v.zoomAround(anchorX, 2.0);
    CHECK(v.pixelsPerBeat == Approx(80.0));
    CHECK(v.xToBeat(anchorX) == Approx(beatBefore));

    v.zoomAround(anchorX, 0.25);
    CHECK(v.pixelsPerBeat == Approx(20.0));
    CHECK(v.xToBeat(anchorX) == Approx(beatBefore));
}

TEST_CASE("zoom is limited to the allowed range", "[ui][viewport]")
{
    Viewport v;
    for (int i = 0; i < 50; ++i)
        v.zoomAround(100.0, 2.0);
    CHECK(v.pixelsPerBeat == Approx(Viewport::maxPixelsPerBeat));
    for (int i = 0; i < 50; ++i)
        v.zoomAround(100.0, 0.5);
    CHECK(v.pixelsPerBeat == Approx(Viewport::minPixelsPerBeat));
}

TEST_CASE("scrolling and clamping", "[ui][viewport]")
{
    Viewport v;
    v.pixelsPerBeat = 50.0;
    v.scrollByPixels(500.0);
    CHECK(v.firstBeat == Approx(10.0));
    v.scrollByPixels(-2000.0);
    v.clamp(100.0);
    CHECK(v.firstBeat == Approx(0.0)); // cannot scroll before beat 0
    v.firstBeat = 1.0e6;
    v.clamp(100.0);
    CHECK(v.firstBeat <= 100.0);
}

TEST_CASE("hit-testing finds the clip under the mouse", "[ui][hittest]")
{
    const auto model = makeModel();
    Layout layout;
    Viewport view;
    view.pixelsPerBeat = 40.0;
    view.firstBeat = 0.0;

    // Track 2 (index 1), second clip starts at beat 8: x = headerWidth + 8 * 40.
    const double x = layout.headerWidth + 8.0 * 40.0 + 100.0;
    const double y = layout.trackTop(1) + 10.0;
    const auto hit = hitTestClip(model, layout, view, x, y);
    REQUIRE(hit.has_value());
    CHECK(hit->track == 1);
    CHECK(hit->clip == 1);
    CHECK(!hit->nearLeftEdge);
    CHECK(!hit->nearRightEdge);
}

TEST_CASE("hit-testing misses gaps, the header and the ruler", "[ui][hittest]")
{
    const auto model = makeModel();
    Layout layout;
    Viewport view;

    // Beats 6 to 8 are a gap between the first and second clip.
    CHECK(!hitTestClip(model, layout, view, layout.headerWidth + 7.0 * 40.0, layout.trackTop(0) + 5.0));
    CHECK(!hitTestClip(model, layout, view, 20.0, layout.trackTop(0) + 5.0));   // over the header
    CHECK(!hitTestClip(model, layout, view, 300.0, 5.0));                       // over the ruler
    CHECK(!hitTestClip(model, layout, view, 300.0, layout.trackTop(4) + 5.0));  // below the last track
}

TEST_CASE("hit-testing reports clip edges and respects scrolling", "[ui][hittest]")
{
    const auto model = makeModel();
    Layout layout;
    Viewport view;
    view.pixelsPerBeat = 40.0;
    view.firstBeat = 8.0; // second clip's start is now at x = 0 of the clip area

    const double y = layout.trackTop(0) + 5.0;
    auto left = hitTestClip(model, layout, view, layout.headerWidth + 2.0, y);
    REQUIRE(left.has_value());
    CHECK(left->clip == 1);
    CHECK(left->nearLeftEdge);

    auto right = hitTestClip(model, layout, view, layout.headerWidth + 6.0 * 40.0 - 2.0, y);
    REQUIRE(right.has_value());
    CHECK(right->nearRightEdge);
}

TEST_CASE("overlapping clips: the one drawn on top wins", "[ui][hittest]")
{
    TimelineModel m;
    TrackModel t;
    t.clips.push_back({1, 0.0, 10.0, 0});
    t.clips.push_back({2, 4.0, 10.0, 0});
    m.tracks.push_back(t);
    Layout layout;
    Viewport view;
    const auto hit = hitTestClip(m, layout, view, layout.headerWidth + 6.0 * 40.0, layout.trackTop(0) + 5.0);
    REQUIRE(hit.has_value());
    CHECK(hit->clip == 1);
}

TEST_CASE("track index from y", "[ui][layout]")
{
    Layout layout;
    CHECK(layout.trackIndexAt(0.0, 4) == -1);
    CHECK(layout.trackIndexAt(layout.rulerHeight, 4) == 0);
    CHECK(layout.trackIndexAt(layout.trackTop(3) + layout.trackHeight - 1, 4) == 3);
    CHECK(layout.trackIndexAt(layout.trackTop(4), 4) == -1);
}

TEST_CASE("dropping a clip: track, grab offset, snapping and clamping", "[ui][drop]")
{
    Layout layout;
    Viewport view;
    view.pixelsPerBeat = 40.0;
    view.firstBeat = 0.0;

    // Mouse over track 3 (index 2) at beat 10.1, grabbed 2 beats into the clip, 0.25 grid -> start 8.0 (8.1 snaps down).
    auto drop = resolveClipDrop(layout, view, 4, layout.headerWidth + 10.1 * 40.0, layout.trackTop(2) + 20.0, 2.0, 0.25);
    CHECK(drop.track == 2);
    CHECK(drop.startBeat == Approx(8.0));

    // Dragged left past the start: clamps to beat 0.
    drop = resolveClipDrop(layout, view, 4, 0.0, layout.trackTop(0) + 1.0, 0.0, 0.25);
    CHECK(drop.startBeat == Approx(0.0));

    // Dragged below the last track or above the first: clamps to the nearest track.
    CHECK(resolveClipDrop(layout, view, 4, 400.0, 5000.0, 0.0, 0.25).track == 3);
    CHECK(resolveClipDrop(layout, view, 4, 400.0, -50.0, 0.0, 0.25).track == 0);
}

TEST_CASE("header hit-testing", "[ui][hittest]")
{
    Layout layout;
    CHECK(hitTestHeader(layout, 4, 10.0, layout.trackTop(2) + 5.0) == 2);
    CHECK(hitTestHeader(layout, 4, layout.headerWidth + 1.0, layout.trackTop(2) + 5.0) == -1);
    CHECK(hitTestHeader(layout, 4, 10.0, 3.0) == -1);
}

TEST_CASE("track reorder index and move", "[ui][reorder]")
{
    Layout layout;
    CHECK(reorderTargetIndex(layout, 4, 0.0) == 0);
    CHECK(reorderTargetIndex(layout, 4, layout.trackTop(2) + 10.0) == 2);
    CHECK(reorderTargetIndex(layout, 4, 9999.0) == 3);

    std::vector<int> v{10, 20, 30, 40};
    moveElement(v, 2, 0); // drag track 3 above track 1
    CHECK(v == std::vector<int>{30, 10, 20, 40});
    moveElement(v, 0, 3);
    CHECK(v == std::vector<int>{10, 20, 40, 30});
    moveElement(v, 1, 1); // no-op
    CHECK(v == std::vector<int>{10, 20, 40, 30});
    moveElement(v, -1, 2); // out of range: no-op
    moveElement(v, 1, 9);
    CHECK(v == std::vector<int>{10, 20, 40, 30});
}

TEST_CASE("moving a track keeps its clips", "[ui][reorder]")
{
    auto model = makeModel();
    const auto movedId = model.tracks[2].id;
    const auto clips = model.tracks[2].clips.size();
    moveElement(model.tracks, 2, 0);
    CHECK(model.tracks[0].id == movedId);
    CHECK(model.tracks[0].clips.size() == clips);
    CHECK(model.tracks[1].id == 1);
    CHECK(model.tracks[2].id == 2);
    CHECK(model.tracks[3].id == 4);
}

TEST_CASE("reorder marker is drawn at the slot the track will take", "[ui][reorder]")
{
    Layout layout;
    CHECK(reorderMarkerY(layout, 2, 0) == layout.trackTop(0));                       // moving up: above row 0
    CHECK(reorderMarkerY(layout, 0, 2) == layout.trackTop(2) + layout.trackHeight);  // moving down: below row 2
}

TEST_CASE("model end beat", "[ui][model]")
{
    CHECK(makeModel().endBeat() == Approx(22.0)); // last clip starts at 16 and is 6 long
    CHECK(TimelineModel{}.endBeat() == Approx(0.0));
}
