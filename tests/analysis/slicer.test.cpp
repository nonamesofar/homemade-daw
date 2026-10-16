// M3 2.2: choosing slice points from the beat grid or from the hits.
#include <catch2/catch_test_macros.hpp>

#include "analysis/Slicer.h"

#include <cmath>

using namespace sampler::analysis;

namespace
{
std::vector<juce::int64> slicesLengths(const SlicePoints& points, juce::int64 total)
{
    std::vector<juce::int64> lengths;
    juce::int64 previous = 0;
    for (const auto f : points.frames)
    {
        lengths.push_back(f - previous);
        previous = f;
    }
    lengths.push_back(total - previous);
    return lengths;
}
} // namespace

TEST_CASE("2 bars at 120 BPM at 1/16 give 32 slices of 125 ms", "[analysis][slicer]")
{
    const juce::int64 total = 4 * 44100; // 2 bars at 120 BPM
    const auto points = beatSlicePoints(total, 44100.0, 120.0, 0, 4.0);
    CHECK_FALSE(points.limitReached);
    const auto lengths = slicesLengths(points, total);
    REQUIRE(lengths.size() == 32);
    for (const auto length : lengths) // 125 ms is 5512.5 frames, so a slice has 5512 or 5513
    {
        CHECK(length >= 5512);
        CHECK(length <= 5513);
    }
}

TEST_CASE("quarter notes of a 4-bar loop at 90 BPM give 16 slices of one beat", "[analysis][slicer]")
{
    const auto points = beatSlicePoints(470400, 44100.0, 90.0, 0, 1.0);
    REQUIRE(points.frames.size() == 15);
    for (size_t i = 0; i < points.frames.size(); ++i)
        CHECK(points.frames[i] == static_cast<juce::int64>(i + 1) * 29400);
}

TEST_CASE("a first beat after the start gives a short first slice, and the grid also runs backwards", "[analysis][slicer]")
{
    // First beat a quarter of a beat in: 0.25 beats at 120 BPM = 5512.5 frames.
    const juce::int64 firstBeat = 5512;
    const auto points = beatSlicePoints(44100 * 4, 44100.0, 120.0, firstBeat, 1.0);
    REQUIRE_FALSE(points.frames.empty());
    CHECK(points.frames[0] == firstBeat);
    CHECK(points.frames[1] == firstBeat + 22050);

    // A first beat far from the start: the grid counts back toward the start of the sound.
    const auto back = beatSlicePoints(44100 * 4, 44100.0, 120.0, 40000, 1.0);
    REQUIRE_FALSE(back.frames.empty());
    CHECK(back.frames[0] == 40000 - 22050);
    CHECK(back.frames[1] == 40000);
}

TEST_CASE("grid points closer than 5 ms to the start or the end are dropped", "[analysis][slicer]")
{
    // The first beat 2 ms after the start: no point there, the next one is a beat later.
    const auto nearStart = beatSlicePoints(44100 * 2, 44100.0, 120.0, 88, 1.0);
    REQUIRE_FALSE(nearStart.frames.empty());
    CHECK(nearStart.frames[0] == 88 + 22050);
    // The sound ends 2 ms after a grid point.
    const auto nearEnd = beatSlicePoints(22050 + 88, 44100.0, 120.0, 0, 1.0);
    CHECK(nearEnd.frames.empty());
}

TEST_CASE("1/32 notes over a long sound stop at 255 points and say so", "[analysis][slicer]")
{
    const auto points = beatSlicePoints(44100 * 60, 44100.0, 120.0, 0, 8.0);
    CHECK(points.frames.size() == 255);
    CHECK(points.limitReached);
}

TEST_CASE("nonsense input gives no points", "[analysis][slicer]")
{
    CHECK(beatSlicePoints(0, 44100.0, 120.0, 0, 1.0).frames.empty());
    CHECK(beatSlicePoints(44100, 44100.0, 0.0, 0, 1.0).frames.empty());
    CHECK(beatSlicePoints(44100, 44100.0, 120.0, 0, 0.0).frames.empty());
}

TEST_CASE("300 transients give the 255 strongest points", "[analysis][slicer]")
{
    std::vector<Onset> hits;
    for (int i = 0; i < 300; ++i)
        hits.push_back({static_cast<juce::int64>(i + 1) * 4000, 0.3f + 0.7f * static_cast<float>((i * 37) % 300) / 300.0f});
    const auto points = transientSlicePoints(hits, 44100 * 30, 44100.0);
    CHECK(points.frames.size() == 255);
    CHECK(points.limitReached);
    for (size_t i = 1; i < points.frames.size(); ++i)
        CHECK(points.frames[i] > points.frames[i - 1]);

    // Every dropped hit is weaker than every kept one.
    float weakestKept = 2.0f;
    for (const auto f : points.frames)
        for (const auto& h : hits)
            if (h.frame == f)
                weakestKept = std::min(weakestKept, h.strength);
    int strongerDropped = 0;
    for (const auto& h : hits)
        if (std::find(points.frames.begin(), points.frames.end(), h.frame) == points.frames.end() && h.strength > weakestKept)
            ++strongerDropped;
    CHECK(strongerDropped == 0);
}

TEST_CASE("hits closer than 5 ms are thinned out and the stronger one stays", "[analysis][slicer]")
{
    std::vector<Onset> hits{{10000, 0.5f}, {10100, 0.9f}, {20000, 0.6f}, {20100, 0.4f}, {30000, 0.7f}};
    const auto points = transientSlicePoints(hits, 44100, 44100.0);
    CHECK(points.frames == std::vector<juce::int64>{10100, 20000, 30000});
    CHECK_FALSE(points.limitReached);
}

TEST_CASE("hits too close to the ends are dropped", "[analysis][slicer]")
{
    std::vector<Onset> hits{{100, 1.0f}, {5000, 0.8f}, {44100 - 100, 0.9f}};
    const auto points = transientSlicePoints(hits, 44100, 44100.0);
    CHECK(points.frames == std::vector<juce::int64>{5000});
}

TEST_CASE("every slice made from hits is at least 5 ms", "[analysis][slicer]")
{
    std::vector<Onset> hits;
    for (int i = 0; i < 1000; ++i)
        hits.push_back({static_cast<juce::int64>(i) * 97 + 300, static_cast<float>((i * 13) % 100) / 100.0f});
    const auto points = transientSlicePoints(hits, 200000, 44100.0);
    for (const auto length : slicesLengths(points, 200000))
        CHECK(length >= 221);
}

TEST_CASE("the nearest zero crossing within 1 ms", "[analysis][slicer]")
{
    std::vector<float> sine(4410);
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = std::sin(2.0f * 3.14159265f * 441.0f * static_cast<float>(i) / 44100.0f); // 100 samples per period
    // Crossings every 50 samples (rising at 100k, falling at 100k + 50).
    const auto snapped = nearestZeroCrossing(sine.data(), static_cast<juce::int64>(sine.size()), 1000 + 20, 44100.0);
    CHECK(std::abs(snapped - 1000) <= 1);
    const auto other = nearestZeroCrossing(sine.data(), static_cast<juce::int64>(sine.size()), 1000 + 30, 44100.0);
    CHECK(std::abs(other - 1050) <= 1);
    // No crossing within 1 ms (44 samples) of a steady signal: the frame is kept.
    std::vector<float> steady(1000, 0.5f);
    CHECK(nearestZeroCrossing(steady.data(), 1000, 500, 44100.0) == 500);
    CHECK(nearestZeroCrossing(nullptr, 0, 500, 44100.0) == 500);
}

TEST_CASE("a slice point is held 5 ms away from its neighbours", "[analysis][slicer]")
{
    CHECK(clampSlicePoint(10000, 0, 20000, 44100.0) == 10000);
    CHECK(clampSlicePoint(19990, 0, 20000, 44100.0) == 20000 - 221);
    CHECK(clampSlicePoint(5, 0, 20000, 44100.0) == 221);
    CHECK(clampSlicePoint(500, 0, 300, 44100.0) == -1); // no room
}
