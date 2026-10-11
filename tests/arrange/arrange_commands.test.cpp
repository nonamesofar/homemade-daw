// M2: the arrangement commands on a real (headless) engine: names, undo, the no-overlap rule and the invariant check.
#include "ArrangeFixture.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;

TEST_CASE("AddTrack and CreateClipsFromSources: step names, positions and one undo for a drop that made a track",
          "[arrange][commands]")
{
    ArrangeFixture f("cmd-create");
    auto& a = f.arrangement();
    const auto loop = f.addSound("drums-loop-120", 8.0); // 4 bars at 120 BPM
    const auto bass = f.addSound("bass-phrase", 4.0);

    cmd::AddTrack add;
    f.commands->apply(add);
    CHECK(f.commands->undoName() == "Add track");
    REQUIRE(a.numTracks() == 2);
    CHECK(a.tracks()[1].name == "Audio 2");
    CHECK(a.tracks()[1].colour != a.tracks()[0].colour);
    REQUIRE(add.resultIds().size() == 1);
    CHECK(add.resultIds()[0] == a.tracks()[1].id);

    cmd::CreateClipsFromSources drop({loop}, {1, false, 16.0}); // track 2 at bar 5
    f.commands->apply(drop);
    CHECK(f.commands->undoName() == "Add clip");
    auto clips = f.clipsOn(1);
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == Approx(16.0));
    CHECK(clips[0].length == Approx(16.0));
    CHECK(clips[0].name == "drums-loop-120");
    CHECK(clips[0].offsetSeconds == Approx(0.0));

    // Below the last lane: a new track with the clip; one undo removes both.
    cmd::CreateClipsFromSources below({bass}, {0, true, 4.0});
    f.commands->apply(below);
    REQUIRE(a.numTracks() == 3);
    CHECK(a.tracks()[2].name == "Audio 3");
    CHECK(f.clipsOn(2).size() == 1);
    f.commands->undo();
    CHECK(a.numTracks() == 2);
    CHECK(a.numClips() == 1);
    CHECK(f.violations.isEmpty());

    // Several sounds go one after another.
    cmd::CreateClipsFromSources two({bass, bass}, {0, false, 0.0});
    f.commands->apply(two);
    CHECK(f.commands->undoName() == "Add 2 clips");
    clips = f.clipsOn(0);
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].start == Approx(0.0));
    CHECK(clips[1].start == Approx(8.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("the invariant check catches an overlapping edit", "[arrange][commands]")
{
    ArrangeFixture f("cmd-invariant");
    const auto source = f.addSound("pad", 4.0);
    f.place(source, 0, 0.0);
    REQUIRE(f.violations.isEmpty());

    // A deliberately bad command: puts a second clip over the first without the planner.
    struct Overlap final : cmd::Command
    {
        juce::String name() const override { return "Bad overlap"; }
        void perform(cmd::ModelContext& c) override
        {
            auto& a = *c.arrangement;
            sampler::engine::ClipSpec spec;
            spec.sourceId = "src_pad";
            spec.name = "pad";
            spec.file = c.bundleDir.getChildFile("audio/pad.wav");
            spec.start = 2.0;
            spec.lengthSeconds = 4.0;
            a.createClip(a.tracks()[0].id, spec);
        }
    } bad;
    f.commands->apply(bad);
    REQUIRE(f.violations.size() == 1);
    CHECK(f.violations[0].contains("Bad overlap"));
    CHECK(f.violations[0].contains("overlap"));
}

TEST_CASE("placing onto clips: shorten, remove, split around, as one undo step", "[arrange][commands]")
{
    ArrangeFixture f("cmd-overlap");
    const auto longSound = f.addSound("long", 16.0, "ramp"); // 8 bars
    const auto bar = f.addSound("bar", 2.0);                 // 1 bar

    SECTION("partly covered: the clip underneath ends where the new one starts")
    {
        const auto four = f.addSound("four", 8.0); // bars 1 to 4
        const auto a = f.place(four, 0, 0.0);
        const auto b = f.place(four, 0, 32.0); // B is long enough to run past A's end once moved
        f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{b, 0, 8.0}}));
        CHECK(f.commands->undoName() == "Move clip");
        auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 2);
        CHECK(clips[0].id == a);
        CHECK(clips[0].length == Approx(8.0)); // ends at bar 3 where B starts
        CHECK(clips[1].id == b);
        CHECK(clips[1].start == Approx(8.0));
        f.commands->undo();
        clips = f.clipsOn(0);
        CHECK(clips[0].length == Approx(16.0));
        CHECK(clips[1].start == Approx(32.0));
    }

    SECTION("landing inside: the long clip is split around the new one and keeps playing the right parts")
    {
        f.place(longSound, 0, 0.0);
        f.place(bar, 0, 8.0); // bar 3
        const auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 3);
        CHECK(clips[0].start == Approx(0.0));
        CHECK(clips[0].length == Approx(8.0));
        CHECK(clips[1].start == Approx(8.0));
        CHECK(clips[1].name == "bar");
        CHECK(clips[2].start == Approx(12.0));
        CHECK(clips[2].length == Approx(20.0));
        CHECK(clips[2].offsetSeconds == Approx(6.0)); // the part from bar 4 of the sound
        CHECK(clips[2].sourceId == longSound);
        f.commands->undo();
        CHECK(f.clipsOn(0).size() == 1);
    }

    SECTION("fully covered: the short clip is removed")
    {
        const auto shortClip = f.place(bar, 0, 40.0);
        const auto longClip = f.place(longSound, 0, 0.0);
        f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{longClip, 0, 36.0}}));
        const auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 1);
        CHECK(clips[0].id == longClip);
        f.commands->undo();
        CHECK(f.arrangement().clip(shortClip).has_value());
    }
    CHECK(f.violations.isEmpty());
}

TEST_CASE("trim, split, duplicate and delete", "[arrange][commands]")
{
    ArrangeFixture f("cmd-edits");
    const auto loop = f.addSound("loop4", 8.0, "ramp"); // 4 bars
    const auto clip = f.place(loop, 0, 0.0);

    SECTION("left trim keeps the audio in time; it cannot go past the sound or below the minimum")
    {
        f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 4.0, 16.0, 4.0));
        CHECK(f.commands->undoName() == "Trim clip");
        auto c = f.arrangement().clip(clip);
        CHECK(c->start == Approx(4.0));
        CHECK(c->length == Approx(12.0));
        CHECK(c->offsetSeconds == Approx(2.0));

        f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 4.0, 40.0, 4.0)); // past the end of the sound
        c = f.arrangement().clip(clip);
        CHECK(c->end() == Approx(16.0));
        f.commands->apply(std::make_unique<cmd::TrimClip>(clip, -8.0, 16.0, 4.0)); // before the sound's start
        c = f.arrangement().clip(clip);
        CHECK(c->start == Approx(0.0));
        CHECK(c->offsetSeconds == Approx(0.0));
        f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 0.0, 1.0, 4.0)); // shorter than one grid step
        c = f.arrangement().clip(clip);
        CHECK(c->length == Approx(4.0));
        f.commands->undo();
        CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    }

    SECTION("split at a beat makes two clips that together play the same")
    {
        f.commands->apply(std::make_unique<cmd::SplitClips>(std::vector<juce::String>{clip}, 8.0));
        CHECK(f.commands->undoName() == "Split clip");
        const auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 2);
        CHECK(clips[0].length == Approx(8.0));
        CHECK(clips[1].start == Approx(8.0));
        CHECK(clips[1].length == Approx(8.0));
        CHECK(clips[1].offsetSeconds == Approx(4.0));
        f.commands->undo();
        CHECK(f.clipsOn(0).size() == 1);
    }

    SECTION("duplicate places copies right after the selection; delete removes them in one step")
    {
        f.commands->apply(std::make_unique<cmd::TrimClip>(clip, 0.0, 4.0, 1.0)); // one bar
        cmd::DuplicateClips first({clip});
        f.commands->apply(first);
        CHECK(f.commands->undoName() == "Duplicate clip");
        REQUIRE(first.resultIds().size() == 1);
        cmd::DuplicateClips second(first.resultIds());
        f.commands->apply(second);
        auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 3);
        CHECK(clips[1].start == Approx(4.0));
        CHECK(clips[2].start == Approx(8.0));

        f.commands->apply(std::make_unique<cmd::DeleteClips>(std::vector<juce::String>{clips[1].id, clips[2].id}));
        CHECK(f.commands->undoName() == "Delete 2 clips");
        CHECK(f.clipsOn(0).size() == 1);
        CHECK(sampler::model::SamplerState(f.project->samplerState()).numSources() == 1); // the sound stays
        f.commands->undo();
        CHECK(f.clipsOn(0).size() == 3);
    }
    CHECK(f.violations.isEmpty());
}

TEST_CASE("moving several clips keeps their spacing across tracks", "[arrange][commands]")
{
    ArrangeFixture f("cmd-move-many");
    const auto bar = f.addSound("bar", 2.0);
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    const auto a = f.place(bar, 0, 0.0);
    const auto b = f.place(bar, 1, 4.0);
    f.commands->apply(std::make_unique<cmd::MoveClips>(std::vector<cmd::ClipMove>{{a, 1, 4.0}, {b, 2, 8.0}}));
    CHECK(f.commands->undoName() == "Move 2 clips");
    CHECK(f.arrangement().clip(a)->trackId == f.trackId(1));
    CHECK(f.arrangement().clip(a)->start == Approx(4.0));
    CHECK(f.arrangement().clip(b)->trackId == f.trackId(2));
    CHECK(f.arrangement().clip(b)->start == Approx(8.0));
    CHECK(f.violations.isEmpty());
}
