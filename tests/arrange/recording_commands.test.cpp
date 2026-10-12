// M2b: track inputs and takes as commands.
#include "ArrangeFixture.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;

namespace
{
cmd::Take makeTake(ArrangeFixture& f, const juce::String& trackId, double seconds, double startBeat, const juce::String& file = "rec-20261012-101500-1.wav")
{
    const auto relative = "audio/recordings/" + file;
    sampler::test::writeTestWav(f.project->bundleDir().getChildFile(relative), seconds);
    cmd::Take t;
    t.trackId = trackId;
    t.name = "Audio 1 2026-10-12 10:15";
    t.relativePath = relative;
    t.sampleRate = 48000.0;
    t.channels = 2;
    t.frames = static_cast<juce::int64>(seconds * 48000.0);
    t.startBeat = startBeat;
    t.recordedFrom = "Master";
    t.recordedAt = "2026-10-12T10:15:00";
    return t;
}
} // namespace

TEST_CASE("AddTakes over a 16-bar clip splits it; one undo restores it, redo brings the take back", "[arrange][commands][record]")
{
    ArrangeFixture f("rec-addtakes");
    const auto sound = f.addSound("long", 32.0); // 16 bars at 120 BPM
    f.place(sound, 0, 0.0);
    REQUIRE(f.clipsOn(0).size() == 1);
    const auto before = f.arrangement().serialise();
    const auto take = makeTake(f, f.trackId(0), 4.0, 8.0);

    f.commands->apply(std::make_unique<cmd::AddTakes>(std::vector<cmd::Take>{take}));
    CHECK(f.commands->undoName() == "Record take");
    auto clips = f.clipsOn(0);
    REQUIRE(clips.size() == 3);
    CHECK(clips[1].start == Approx(8.0));
    CHECK(clips[1].length == Approx(8.0));
    CHECK(clips[1].name == take.name);

    const auto source = sampler::model::SamplerState(f.project->samplerState()).findSourceById(clips[1].sourceId);
    REQUIRE(source.isValid());
    CHECK(source.category() == sampler::model::Category::recording);
    CHECK(source.recordedFrom() == "Master");
    CHECK(source.original() == take.relativePath);
    CHECK(source.decoded() == take.relativePath);

    f.commands->undo();
    CHECK(f.arrangement().serialise() == before);
    CHECK(f.project->bundleDir().getChildFile(take.relativePath).existsAsFile());
    f.commands->redo();
    CHECK(f.clipsOn(0).size() == 3);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("AddTakes names the step after the number of takes", "[arrange][commands][record]")
{
    ArrangeFixture f("rec-addtakes-two");
    cmd::AddTrack add;
    f.commands->apply(add);
    auto a = makeTake(f, f.trackId(0), 2.0, 0.0, "rec-20261012-101500-1.wav");
    auto b = makeTake(f, f.trackId(1), 2.0, 0.0, "rec-20261012-101500-2.wav");
    f.commands->apply(std::make_unique<cmd::AddTakes>(std::vector<cmd::Take>{a, b}));
    CHECK(f.commands->undoName() == "Record 2 takes");
    CHECK(f.clipsOn(0).size() == 1);
    CHECK(f.clipsOn(1).size() == 1);
    f.commands->undo();
    CHECK(f.arrangement().numClips() == 0);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("track inputs: default, set, undo, round trip through save and reopen", "[arrange][commands][record]")
{
    ArrangeFixture f("rec-input");
    CHECK(f.arrangement().tracks()[0].input == "system:default");
    CHECK_FALSE(f.arrangement().serialise().contains("sampler_input")); // an M2 project is unchanged

    cmd::AddTrack add;
    f.commands->apply(add);
    f.commands->apply(std::make_unique<cmd::SetTrackInput>(f.trackId(1), "master"));
    CHECK(f.commands->undoName() == "Set track input");
    CHECK(f.arrangement().tracks()[1].input == "master");

    f.commands->apply(std::make_unique<cmd::SetTrackInput>(f.trackId(1), "track:" + f.trackId(1))); // itself: refused
    CHECK(f.arrangement().tracks()[1].input == "master");

    const auto target = f.root.getChildFile("Grab.sdaw");
    juce::String error;
    REQUIRE(f.project->saveAs(target, error));
    REQUIRE(f.project->save());
    f.commands.reset();
    f.project.reset();
    f.project = sampler::ProjectEdit::open(f.host, target, error);
    REQUIRE(f.project != nullptr);
    REQUIRE(f.arrangement().numTracks() == 2);
    CHECK(f.arrangement().tracks()[0].input == "system:default");
    CHECK(f.arrangement().tracks()[1].input == "master");
}

TEST_CASE("deleting a source track resets the dependent input in the same step", "[arrange][commands][record]")
{
    ArrangeFixture f("rec-delete-source");
    cmd::AddTrack add;
    f.commands->apply(add);
    const auto drums = f.trackId(0);
    const auto vocal = f.trackId(1);
    f.commands->apply(std::make_unique<cmd::SetTrackInput>(vocal, "track:" + drums));
    REQUIRE(f.arrangement().tracks()[1].input == "track:" + drums);

    f.commands->apply(std::make_unique<cmd::DeleteTracks>(std::vector<juce::String>{drums}));
    REQUIRE(f.arrangement().numTracks() == 1);
    CHECK(f.arrangement().tracks()[0].input == "system:default");

    f.commands->undo();
    REQUIRE(f.arrangement().numTracks() == 2);
    CHECK(f.arrangement().tracks()[1].input == "track:" + drums);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("AddTakes reports the takes it could not add, and names each sound after its whole file", "[arrange][commands][record]")
{
    ArrangeFixture f("rec-addtakes-skipped");
    auto good = makeTake(f, f.trackId(0), 2.0, 0.0, "rec-20261012-101500-1.wav");
    auto goneTrack = makeTake(f, f.trackId(0), 2.0, 0.0, "rec-20261012-101500-2.wav");
    goneTrack.trackId = "no-such-track";
    auto empty = makeTake(f, f.trackId(0), 2.0, 0.0, "rec-20261012-101500-3.wav");
    empty.frames = 0;
    auto otherStamp = makeTake(f, f.trackId(0), 2.0, 16.0, "rec-20261012-101600-1.wav");

    cmd::AddTakes command({good, goneTrack, empty, otherStamp});
    f.commands->apply(command);

    CHECK(command.skippedTakes() == std::vector<size_t>{1, 2});
    CHECK(f.clipsOn(0).size() == 2);
    const auto state = sampler::model::SamplerState(f.project->samplerState());
    const auto first = state.findSourceById(f.clipsOn(0)[0].sourceId);
    const auto second = state.findSourceById(f.clipsOn(0)[1].sourceId);
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());
    CHECK(first.hash() != second.hash()); // "...-1" of two recordings must not collide
    CHECK(first.hash() == "rec:rec-20261012-101500-1");
}

TEST_CASE("the transport refuses to be located while the locate lock is on", "[arrange][record]")
{
    ArrangeFixture f("rec-locate-lock");
    auto& t = f.project->transport();
    int refused = 0;
    t.onLocateRefused = [&refused] { ++refused; };

    CHECK(t.locateBeats(4.0));
    CHECK(t.positionBeats() == Approx(4.0));

    t.setLocateLocked(true);
    CHECK_FALSE(t.locateBeats(12.0));
    CHECK(refused == 1);
    CHECK(t.positionBeats() == Approx(4.0)); // the playhead did not move

    t.setLocateLocked(false);
    CHECK(t.locateBeats(12.0));
    CHECK(t.positionBeats() == Approx(12.0));
    CHECK(refused == 1);
}
