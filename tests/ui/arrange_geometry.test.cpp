// Arrangement geometry (carried over from the M0 timeline geometry tests and grown for M2).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/arrange/ArrangeGeometry.h"

using Catch::Approx;
using namespace sampler::ui;

namespace
{
ArrangeViewport makeView(double ppb = 20.0, double first = 0.0, double width = 800.0)
{
    ArrangeViewport v;
    v.pixelsPerBeat = ppb;
    v.firstBeat = first;
    v.widthPx = width;
    v.heightPx = 400.0;
    return v;
}

std::vector<LaneClip> fourTracks()
{
    return {{"a", 0, 0.0, 16.0}, {"b", 0, 16.0, 16.0}, {"c", 1, 4.0, 8.0}, {"d", 3, 32.0, 4.0}};
}
} // namespace

TEST_CASE("viewport converts between beats and pixels", "[ui][viewport]")
{
    auto v = makeView(20.0, 4.0);
    CHECK(v.beatToX(4.0) == Approx(0.0));
    CHECK(v.beatToX(14.0) == Approx(200.0));
    CHECK(v.xToBeat(200.0) == Approx(14.0));
    CHECK(v.lastBeat() == Approx(44.0));
}

TEST_CASE("zooming keeps the beat under the mouse in place and is limited", "[ui][viewport]")
{
    auto v = makeView(20.0, 0.0);
    const auto before = v.xToBeat(300.0);
    v.zoomAround(300.0, 2.0);
    CHECK(v.pixelsPerBeat == Approx(40.0));
    CHECK(v.xToBeat(300.0) == Approx(before));
    for (int i = 0; i < 100; ++i)
        v.zoomAround(0.0, 2.0);
    CHECK(v.pixelsPerBeat == Approx(ArrangeViewport::maxPixelsPerBeat));
    for (int i = 0; i < 100; ++i)
        v.zoomAround(0.0, 0.5);
    CHECK(v.pixelsPerBeat == Approx(ArrangeViewport::minPixelsPerBeat));
}

TEST_CASE("the scrollable length is at least 16 bars and 8 bars past the last clip", "[ui][viewport]")
{
    CHECK(contentBeats(0.0, 4.0) == Approx(64.0));
    CHECK(contentBeats(40.0, 4.0) == Approx(72.0));
    CHECK(contentBeats(160.0, 4.0) == Approx(192.0));
    CHECK(contentBeats(0.0, 3.0) == Approx(48.0)); // 3/4

    auto v = makeView(20.0, 1000.0, 400.0); // 20 visible beats
    v.scrollY = 5000.0;
    v.clamp(contentBeats(160.0, 4.0), 4 * 112.0 + 44.0);
    CHECK(v.firstBeat == Approx(192.0 - 20.0)); // the end of the content shows at the right edge, with room after the song
    CHECK(v.scrollY == Approx(4 * 112.0 + 44.0 - 400.0));
    v.firstBeat = -10.0;
    v.scrollY = -3.0;
    v.clamp(64.0, 1000.0);
    CHECK(v.firstBeat == Approx(0.0));
    CHECK(v.scrollY == Approx(0.0));
}

TEST_CASE("show range: a song ending at bar 40 fills the width", "[ui][viewport]")
{
    auto v = makeView(20.0, 30.0, 800.0);
    v.showRange(0.0, 160.0);
    CHECK(v.firstBeat == Approx(0.0));
    CHECK(v.beatToX(160.0) == Approx(800.0));
}

TEST_CASE("hit-testing finds clips, their edges, and misses gaps", "[ui][hittest]")
{
    const ArrangeLayout layout;
    const auto v = makeView(10.0);
    const auto clips = fourTracks();
    // Track 1 body: y between 8 and 104, clip "b" spans x 160..320.
    auto hit = hitTestClip(clips, layout, v, 200.0, 50.0);
    REQUIRE(hit);
    CHECK(hit->id == "b");
    CHECK(hit->zone == ClipZone::body);
    hit = hitTestClip(clips, layout, v, 162.0, 50.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::leftEdge);
    hit = hitTestClip(clips, layout, v, 318.0, 50.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::rightEdge);
    CHECK_FALSE(hitTestClip(clips, layout, v, 200.0, 2.0));          // in the lane's top inset
    CHECK_FALSE(hitTestClip(clips, layout, v, 200.0, 112.0 * 2 + 50)); // empty track 3
    hit = hitTestClip(clips, layout, v, 330.0, 112.0 * 3 + 50);
    REQUIRE(hit);
    CHECK(hit->id == "d");

    // A narrow clip keeps a body to grab: edges take at most a third.
    const std::vector<LaneClip> narrow{{"n", 0, 0.0, 1.2}};
    hit = hitTestClip(narrow, layout, v, 6.0, 50.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::body);
}

TEST_CASE("marquee selects the clips it touches across tracks", "[ui][hittest]")
{
    const ArrangeLayout layout;
    const auto v = makeView(10.0);
    const auto clips = fourTracks();
    // From bar 3 to 6 (beats 8..24 -> x 80..240), tracks 1 and 2.
    const auto ids = clipsInMarquee(clips, layout, v, {80.0, 20.0, 160.0, 112.0 + 40.0});
    CHECK(ids == std::vector<std::string>{"a", "b", "c"});
    // Drawn upwards and leftwards it is the same box.
    CHECK(clipsInMarquee(clips, layout, v, {240.0, 172.0, -160.0, -152.0}) == ids);
    CHECK(clipsInMarquee(clips, layout, v, {700.0, 20.0, 10.0, 10.0}).empty());
}

TEST_CASE("drop resolution: track, snapped beat, below the last lane makes a new track", "[ui][drop]")
{
    const ArrangeLayout layout;
    const auto v = makeView(10.0);
    const auto snapBar = [](double b) { return std::round(b / 4.0) * 4.0; };
    auto t = resolveDrop(layout, v, 3, 165.0, 112.0 + 30.0, snapBar);
    CHECK(t.track == 1);
    CHECK_FALSE(t.newTrack);
    CHECK(t.beat == Approx(16.0));
    t = resolveDrop(layout, v, 3, 10.0, 112.0 * 3 + 5.0, snapBar);
    CHECK(t.newTrack);
    t = resolveDrop(layout, v, 3, -50.0, 10.0, snapBar);
    CHECK(t.beat == Approx(0.0));
    t = resolveDrop(layout, v, 0, 10.0, 10.0, snapBar); // no tracks at all
    CHECK(t.newTrack);
}

TEST_CASE("moving a group is clamped to bar 1 and to the tracks", "[ui][drop]")
{
    // Grab clip at beat 8 on track 1; the group spans tracks 0..1 and starts at beat 4.
    auto m = clampMove(8.0, 12.0, 1, 3, 4, 0, 1, 4.0);
    CHECK(m.trackDelta == 2);
    CHECK(m.beatDelta == Approx(4.0));
    m = clampMove(8.0, 0.0, 1, 0, 4, 0, 1, 4.0); // would push the earliest clip before bar 1 and off the top track
    CHECK(m.beatDelta == Approx(-4.0));
    CHECK(m.trackDelta == 0);
    m = clampMove(8.0, 8.0, 1, -5, 4, 0, 1, 4.0); // cannot leave the top
    CHECK(m.trackDelta == -0);
    m = clampMove(8.0, 8.0, 1, 10, 4, 0, 1, 4.0); // nor the bottom
    CHECK(m.trackDelta == 2);
}

TEST_CASE("loop bar: hit zones, edge and body drags, drawing a new range", "[ui][loop]")
{
    const auto v = makeView(10.0);
    CHECK(hitTestLoop(16.0, 32.0, v, 161.0) == LoopZone::start);
    CHECK(hitTestLoop(16.0, 32.0, v, 318.0) == LoopZone::end);
    CHECK(hitTestLoop(16.0, 32.0, v, 240.0) == LoopZone::body);
    CHECK(hitTestLoop(16.0, 32.0, v, 400.0) == LoopZone::none);
    CHECK(hitTestLoop(0.0, 0.0, v, 0.0) == LoopZone::none);
    // A loop narrower than the grab radius: each edge can still be grabbed (the nearer one wins).
    CHECK(hitTestLoop(16.0, 16.5, v, 160.0) == LoopZone::start);
    CHECK(hitTestLoop(16.0, 16.5, v, 165.0) == LoopZone::end);
    {
        // dragLoop on a loop shorter than the minimum length does not break the clamp range
        const auto [s0, s1] = dragLoop(LoopZone::start, 0.5, 1.0, 3.0, 4.0);
        CHECK(s0 == Approx(0.5)); // it cannot shrink further, and does not jump
        CHECK(s1 == Approx(1.0));
        CHECK(dragLoop(LoopZone::start, 8.0, 9.0, 0.5, 4.0).first == Approx(8.0));
        CHECK(dragLoop(LoopZone::start, 8.0, 9.0, -2.0, 4.0).first == Approx(6.0));
    }

    auto [a, b] = dragLoop(LoopZone::end, 16.0, 28.0, 8.0, 4.0); // bars 5-8 -> 5-10
    CHECK(a == Approx(16.0));
    CHECK(b == Approx(36.0));
    std::tie(a, b) = dragLoop(LoopZone::start, 16.0, 28.0, 20.0, 4.0); // cannot pass the end
    CHECK(a == Approx(24.0));
    std::tie(a, b) = dragLoop(LoopZone::body, 16.0, 28.0, -40.0, 4.0); // stops at bar 1
    CHECK(a == Approx(0.0));
    CHECK(b == Approx(12.0));
    std::tie(a, b) = drawLoop(32.0, 16.0, 4.0);
    CHECK(a == Approx(16.0));
    CHECK(b == Approx(32.0));
    std::tie(a, b) = drawLoop(16.0, 16.0, 4.0); // a click makes the shortest loop
    CHECK(b - a == Approx(4.0));
}

TEST_CASE("track reorder index, marker and move", "[ui][reorder]")
{
    const ArrangeLayout layout;
    CHECK(reorderTargetIndex(layout, 4, -30.0) == 0);
    CHECK(reorderTargetIndex(layout, 4, 112.0 * 2 + 5) == 2);
    CHECK(reorderTargetIndex(layout, 4, 5000.0) == 3);
    CHECK(reorderMarkerY(layout, 2, 0) == Approx(0.0));
    CHECK(reorderMarkerY(layout, 0, 2) == Approx(112.0 * 3));

    std::vector<std::string> names{"Drums", "Bass", "Chops", "Vocal"};
    moveElement(names, 3, 0);
    CHECK(names == std::vector<std::string>{"Vocal", "Drums", "Bass", "Chops"});
    moveElement(names, 0, 9); // out of range: unchanged
    CHECK(names.front() == "Vocal");
}

TEST_CASE("auto-scroll near the edges of the header list, ruler label spacing", "[ui][reorder]")
{
    CHECK(edgeScrollSpeed(200.0, 400.0) == Approx(0.0));
    CHECK(edgeScrollSpeed(0.0, 400.0) < 0.0);
    CHECK(edgeScrollSpeed(399.0, 400.0) > 0.0);
    CHECK(rulerLabelStep(20.0, 4.0) == 1); // 80 px per bar
    CHECK(rulerLabelStep(2.0, 4.0) == 8);  // 8 px per bar: every 8th bar
}

TEST_CASE("fade handles win over the edges only in the top 8 px, and sit where the fades end", "[ui][hittest][fades]")
{
    const ArrangeLayout layout;
    const auto v = makeView(10.0);
    // Clip "b" spans x 160..320; its lane starts 8 px below the track top (see the test above).
    const std::vector<LaneClip> clips{{"a", 0, 0.0, 16.0}, {"b", 0, 16.0, 16.0}};

    // No fades: the handles are at the corners.
    auto hit = hitTestClip(clips, layout, v, 162.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->id == "b");
    CHECK(hit->zone == ClipZone::fadeInHandle);
    hit = hitTestClip(clips, layout, v, 318.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::fadeOutHandle);

    // Below the top 8 px the same x is the edge again.
    hit = hitTestClip(clips, layout, v, 162.0, 8.0 + 9.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::leftEdge);
    hit = hitTestClip(clips, layout, v, 318.0, 8.0 + 9.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::rightEdge);

    // The middle of the top band is the body.
    hit = hitTestClip(clips, layout, v, 240.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::body);

    // With a fade in of 4 beats (40 px) the handle sits 40 px in; the corner is the edge's again only below the band.
    const std::vector<LaneClip> faded{{"f", 0, 16.0, 16.0, 4.0, 2.0}};
    hit = hitTestClip(faded, layout, v, 200.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::fadeInHandle);
    hit = hitTestClip(faded, layout, v, 162.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::leftEdge); // the handle moved away from the corner
    hit = hitTestClip(faded, layout, v, 300.0, 10.0); // 320 - 2 beats * 10
    REQUIRE(hit);
    CHECK(hit->zone == ClipZone::fadeOutHandle);

    // A narrow clip has no handles.
    const std::vector<LaneClip> narrow{{"n", 0, 0.0, 1.2}};
    hit = hitTestClip(narrow, layout, v, 2.0, 10.0);
    REQUIRE(hit);
    CHECK(hit->zone != ClipZone::fadeInHandle);
}
