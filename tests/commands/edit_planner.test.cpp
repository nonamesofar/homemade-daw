// The plain edit planner: the no-overlap rule of §8.3, splits and trims (no engine).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "commands/EditPlanner.h"

#include <algorithm>

using Catch::Approx;
using namespace sampler::commands;

namespace
{
const auto tempo120 = TimeMap::constantTempo(120.0); // 2 beats per second

Span span(const char* id, int track, double start, double length, double offset = 0.0)
{
    Span s;
    s.id = id;
    s.track = track;
    s.start = start;
    s.length = length;
    s.offset = offset;
    return s;
}

const Span* changed(const Plan& p, const char* id)
{
    for (const auto& s : p.changed)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool removed(const Plan& p, const char* id) { return std::find(p.removed.begin(), p.removed.end(), juce::String(id)) != p.removed.end(); }
} // namespace

TEST_CASE("the end of a clip is covered: it is shortened", "[commands][planner]")
{
    const std::vector<Span> existing{span("A", 0, 0.0, 16.0), span("B", 0, 32.0, 16.0)};
    auto moved = existing[1];
    moved.start = 8.0; // 8..24 covers the end of A
    const auto plan = EditPlanner::place(existing, {moved}, tempo120);
    REQUIRE(changed(plan, "A") != nullptr);
    CHECK(changed(plan, "A")->length == Approx(8.0));
    CHECK(changed(plan, "A")->offset == Approx(0.0));
    REQUIRE(changed(plan, "B") != nullptr);
    CHECK(changed(plan, "B")->start == Approx(8.0));
    CHECK(plan.removed.empty());
    CHECK(plan.created.empty());
}

TEST_CASE("the start of a clip is covered: it starts later and its audio stays in tempo120", "[commands][planner]")
{
    const std::vector<Span> existing{span("A", 0, 8.0, 16.0, 1.0)};
    Span placed;
    placed.specIndex = 0;
    placed.track = 0;
    placed.start = 4.0;
    placed.length = 8.0; // covers 4..12
    const auto plan = EditPlanner::place(existing, {placed}, tempo120);
    REQUIRE(changed(plan, "A") != nullptr);
    CHECK(changed(plan, "A")->start == Approx(12.0));
    CHECK(changed(plan, "A")->length == Approx(12.0));
    CHECK(changed(plan, "A")->offset == Approx(3.0)); // 1 s + 4 beats (2 s)
    REQUIRE(plan.created.size() == 1);
    CHECK(plan.created[0].specIndex == 0);
}

TEST_CASE("a clip fully covered is removed", "[commands][planner]")
{
    const std::vector<Span> existing{span("short", 0, 4.0, 4.0), span("long", 0, 20.0, 16.0)};
    auto moved = existing[1];
    moved.start = 2.0; // 2..18 covers 4..8
    const auto plan = EditPlanner::place(existing, {moved}, tempo120);
    CHECK(removed(plan, "short"));
    CHECK(changed(plan, "long")->start == Approx(2.0));
}

TEST_CASE("landing inside a clip splits it around the new one", "[commands][planner]")
{
    const std::vector<Span> existing{span("A", 0, 0.0, 32.0, 0.5)};
    Span placed;
    placed.specIndex = 0;
    placed.tag = 7;
    placed.track = 0;
    placed.start = 8.0;
    placed.length = 4.0;
    const auto plan = EditPlanner::place(existing, {placed}, tempo120);
    REQUIRE(changed(plan, "A") != nullptr);
    CHECK(changed(plan, "A")->length == Approx(8.0));
    REQUIRE(plan.created.size() == 2);
    const auto piece = std::find_if(plan.created.begin(), plan.created.end(), [](const Span& s) { return s.copyOf == "A"; });
    REQUIRE(piece != plan.created.end());
    CHECK(piece->start == Approx(12.0));
    CHECK(piece->length == Approx(20.0));
    CHECK(piece->offset == Approx(6.5)); // 0.5 s + 12 beats
    CHECK(piece->tag == -1);
    const auto newClip = std::find_if(plan.created.begin(), plan.created.end(), [](const Span& s) { return s.tag == 7; });
    REQUIRE(newClip != plan.created.end());
}

TEST_CASE("other tracks are untouched, and several clips move over several tracks", "[commands][planner]")
{
    const std::vector<Span> existing{span("a", 0, 0.0, 4.0), span("b", 1, 4.0, 4.0), span("x", 1, 12.0, 8.0), span("y", 2, 12.0, 8.0)};
    auto a = existing[0], b = existing[1];
    a.track = 1;
    a.start = 8.0; // onto track 1 at 8..12: touches x only at its start
    b.track = 2;
    b.start = 14.0; // inside y (12..20)
    const auto plan = EditPlanner::place(existing, {a, b}, tempo120);
    CHECK(changed(plan, "a")->track == 1);
    CHECK(changed(plan, "x") == nullptr); // 8..12 and 12..20 only touch
    REQUIRE(changed(plan, "y") != nullptr);
    CHECK(changed(plan, "y")->length == Approx(2.0));
    REQUIRE(plan.created.size() == 1);
    CHECK(plan.created[0].copyOf == "y");
    CHECK(plan.created[0].start == Approx(18.0));
}

TEST_CASE("a clip moved onto its own old position changes nothing", "[commands][planner]")
{
    const std::vector<Span> existing{span("a", 0, 0.0, 4.0), span("b", 0, 4.0, 4.0)};
    const auto plan = EditPlanner::place(existing, {existing[0]}, tempo120);
    CHECK(plan.isEmpty());
    // Moving a clip right by its own length over its old place: the old place is free.
    auto moved = existing[0];
    moved.start = 2.0;
    const auto overlap = EditPlanner::place(existing, {moved}, tempo120);
    REQUIRE(changed(overlap, "b") != nullptr);
    CHECK(changed(overlap, "b")->start == Approx(6.0));
}

TEST_CASE("later placed clips win over earlier ones", "[commands][planner]")
{
    Span p1, p2;
    p1.specIndex = 0;
    p1.tag = 0;
    p1.start = 0.0;
    p1.length = 8.0;
    p2.specIndex = 1;
    p2.tag = 1;
    p2.start = 4.0;
    p2.length = 8.0;
    const auto plan = EditPlanner::place({}, {p1, p2}, tempo120);
    REQUIRE(plan.created.size() == 2);
    const auto first = std::find_if(plan.created.begin(), plan.created.end(), [](const Span& s) { return s.tag == 0; });
    CHECK(first->length == Approx(4.0));
}

TEST_CASE("split makes two clips that play the same", "[commands][planner]")
{
    const std::vector<Span> existing{span("a", 0, 0.0, 16.0, 1.0), span("b", 1, 0.0, 4.0)};
    const auto plan = EditPlanner::split(existing, {"a", "b"}, 8.0, tempo120);
    REQUIRE(plan.changed.size() == 1);
    CHECK(plan.changed[0].length == Approx(8.0));
    REQUIRE(plan.created.size() == 1);
    CHECK(plan.created[0].start == Approx(8.0));
    CHECK(plan.created[0].length == Approx(8.0));
    CHECK(plan.created[0].offset == Approx(5.0)); // 1 s + 8 beats
    CHECK(EditPlanner::split(existing, {"a"}, 0.0, tempo120).isEmpty()); // at an edge: nothing to split
}

TEST_CASE("trim limits: not before the sound, not past it, not under the minimum", "[commands][planner]")
{
    const auto s = span("a", 0, 8.0, 8.0, 1.0); // starts 1 s (2 beats) into an 8 s (16 beat) sound
    CHECK(EditPlanner::clampTrimStart(s, 0.0, 1.0, tempo120) == Approx(6.0));   // offset 0 sits at beat 6
    CHECK(EditPlanner::clampTrimStart(s, 15.5, 1.0, tempo120) == Approx(15.0)); // keeps one beat
    CHECK(EditPlanner::clampTrimEnd(s, 40.0, 8.0, 1.0, tempo120) == Approx(22.0)); // the sound ends at beat 22
    CHECK(EditPlanner::clampTrimEnd(s, 8.2, 8.0, 1.0, tempo120) == Approx(9.0));
    const auto trimmed = EditPlanner::withStart(s, 10.0, tempo120);
    CHECK(trimmed.offset == Approx(2.0));
    CHECK(trimmed.end() == Approx(16.0));
}

TEST_CASE("after a tempo change the earlier clip ends where the next starts", "[commands][planner]")
{
    const std::vector<Span> existing{span("a", 0, 0.0, 4.6), span("b", 0, 4.0, 4.0), span("c", 1, 0.0, 9.0)};
    const auto plan = EditPlanner::shortenOverlaps(existing);
    REQUIRE(plan.changed.size() == 1);
    CHECK(plan.changed[0].id == "a");
    CHECK(plan.changed[0].length == Approx(4.0));
}

TEST_CASE("edges off by float noise do not leave sliver clips", "[commands][planner]")
{
    const double noise = 1.0e-12;
    const auto winnerOf = [](double start, double length) {
        Span w;
        w.specIndex = 0;
        w.tag = 7;
        w.track = 0;
        w.start = start;
        w.length = length;
        return w;
    };
    const auto noSlivers = [](const Plan& p) {
        for (const auto& s : p.created)
            CHECK(s.length > 1.0e-9);
        for (const auto& s : p.changed)
            CHECK(s.length > 1.0e-9);
    };

    // The old clip starts a hair before the winner and ends inside it: it is covered, not cut to a sliver.
    auto plan = EditPlanner::place({span("A", 0, 4.0 - noise, 4.0)}, {winnerOf(4.0, 8.0)}, tempo120);
    CHECK(removed(plan, "A"));
    noSlivers(plan);

    // It starts a hair before the winner and ends after it: only the covered start moves.
    plan = EditPlanner::place({span("A", 0, 4.0 - noise, 20.0)}, {winnerOf(4.0, 8.0)}, tempo120);
    REQUIRE(changed(plan, "A") != nullptr);
    CHECK(changed(plan, "A")->start == Approx(12.0));
    CHECK(plan.created.size() == 1);
    noSlivers(plan);

    // It ends a hair after the winner's end and starts before: no right-hand sliver is created.
    plan = EditPlanner::place({span("A", 0, 0.0, 12.0 + noise)}, {winnerOf(4.0, 8.0)}, tempo120);
    REQUIRE(changed(plan, "A") != nullptr);
    CHECK(changed(plan, "A")->length == Approx(4.0));
    CHECK(plan.created.size() == 1); // only the winner
    noSlivers(plan);

    // It ends a hair before the winner starts: untouched.
    plan = EditPlanner::place({span("A", 0, 0.0, 4.0 + noise)}, {winnerOf(4.0, 8.0)}, tempo120);
    CHECK(plan.changed.empty());
    CHECK(plan.removed.empty());
}
