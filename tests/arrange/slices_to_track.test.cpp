// M3 4.1: "Slices to new track": the clips, their order and origin, a render against the sound, reversed slices, undo.
#include "ArrangeFixture.h"

#include "commands/SliceCommands.h"
#include "engine/OfflineRender.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;
namespace test = sampler::test;

namespace
{
constexpr double rate = 48000.0;

/** A 16-second ramp sound cut into 16 slices of one second. */
struct Chopped
{
    explicit Chopped(const juce::String& name) : f(name)
    {
        id = f.addSound("ramp", 16.0, "ramp");
        for (int k = 1; k < 16; ++k)
            f.commands->apply(std::make_unique<cmd::AddSlicePoint>(id, static_cast<juce::int64>(k) * 48000));
    }

    void sliceToTrack(double beat)
    {
        command = std::make_unique<cmd::SlicesToTrack>(id, beat);
        f.commands->apply(*command);
    }

    juce::AudioBuffer<float> render()
    {
        const auto out = f.root.getChildFile("slices.wav");
        juce::String error;
        sampler::engine::RenderOptions options;
        REQUIRE(sampler::engine::renderToWav(*f.project, out, options, error));
        INFO(error);
        return test::readWav(out);
    }

    ArrangeFixture f;
    juce::String id;
    std::unique_ptr<cmd::SlicesToTrack> command;
};
} // namespace

TEST_CASE("16 slices from bar 5 give a track named after the sound with 16 adjacent clips", "[arrange][slices]")
{
    Chopped c("s2t-basic");
    const int tracksBefore = c.f.arrangement().numTracks();
    c.sliceToTrack(16.0); // bar 5 in 4/4

    CHECK(c.f.violations.isEmpty());
    REQUIRE(c.f.arrangement().numTracks() == tracksBefore + 1);
    const auto track = c.f.arrangement().tracks().back();
    CHECK(track.name == "ramp");
    CHECK(c.command->trackId() == track.id);
    CHECK(c.f.commands->undoName() == "Slices to new track");

    const auto clips = c.f.arrangement().clipsOnTrack(track.id);
    REQUIRE(clips.size() == 16);
    CHECK(c.command->resultIds().size() == 16);
    for (size_t i = 0; i < clips.size(); ++i)
    {
        INFO("clip " << i);
        CHECK(clips[i].name == "ramp " + juce::String(static_cast<int>(i) + 1));
        CHECK(clips[i].lengthSeconds == Approx(1.0).margin(1e-4));
        CHECK(clips[i].offsetSeconds == Approx(static_cast<double>(i)).margin(1e-4));
        CHECK(clips[i].start == Approx(16.0 + static_cast<double>(i) * 2.0).margin(1e-4)); // 1 s is 2 beats at 120 BPM
        CHECK(clips[i].originSourceId == c.id);
        CHECK(clips[i].originSlice == static_cast<int>(i));
        CHECK(clips[i].sourceId == c.id);
        if (i > 0)
            CHECK(clips[i].start == Approx(clips[i - 1].end()).margin(1e-6)); // back to back
    }
    CHECK(c.command->resultIds().front() == clips.front().id);
}

TEST_CASE("the clips played in a row sound like the sound from its first slice", "[arrange][slices]")
{
    Chopped c("s2t-render");
    c.sliceToTrack(16.0);
    const auto audio = c.render();
    const int origin = static_cast<int>(8.0 * rate); // bar 5 at 120 BPM
    REQUIRE(audio.getNumSamples() >= origin + static_cast<int>(16.0 * rate) - 10);

    // Silence before bar 5; after it the ramp, apart from the 3 ms declick fades at the slice boundaries.
    CHECK(test::peakOf(audio, 0, 0, origin - 10) == 0.0f);
    int checked = 0;
    for (int i = 0; i < static_cast<int>(16.0 * rate) - 10; i += 7)
    {
        const int inSlice = i % 48000;
        if (inSlice < 400 || inSlice > 48000 - 400) // within about 8 ms of a boundary
            continue;
        const float expected = static_cast<float>(i) / static_cast<float>(16.0 * rate);
        REQUIRE(audio.getSample(0, origin + i) == Approx(expected).margin(2e-3));
        ++checked;
    }
    CHECK(checked > 50000);
}

TEST_CASE("a reversed slice plays its part of the sound backwards and the others forwards", "[arrange][slices]")
{
    Chopped c("s2t-reverse");
    c.f.commands->apply(std::make_unique<cmd::SetSliceParam>(c.id, 3, cmd::SetSliceParam::Param::reverse, 1.0));
    c.sliceToTrack(16.0);
    const auto clips = c.f.arrangement().clipsOnTrack(c.f.arrangement().tracks().back().id);
    REQUIRE(clips.size() == 16);
    CHECK(clips[3].reversed);
    CHECK_FALSE(clips[2].reversed);
    CHECK_FALSE(clips[4].reversed);
    // The reversed file holds slice [3 s, 4 s) at [12 s, 13 s).
    CHECK(clips[3].offsetSeconds == Approx(16.0 - 4.0).margin(1e-4));

    const auto audio = c.render();
    const int origin = static_cast<int>(8.0 * rate);
    const auto at = [&](double seconds) { return audio.getSample(0, origin + static_cast<int>(seconds * rate)); };
    // Slice 3 (3 s to 4 s of the ramp, values 3/16 to 4/16) runs from high to low.
    CHECK(at(3.2) > at(3.8));
    CHECK(at(3.2) == Approx((4.0 - 0.2) / 16.0).margin(5e-3));
    CHECK(at(3.8) == Approx((4.0 - 0.8) / 16.0).margin(5e-3));
    // Its neighbours go up.
    CHECK(at(2.2) == Approx(2.2 / 16.0).margin(5e-3));
    CHECK(at(4.8) == Approx(4.8 / 16.0).margin(5e-3));
}

TEST_CASE("the slice gain is the clip gain", "[arrange][slices]")
{
    Chopped c("s2t-gain");
    c.f.commands->apply(std::make_unique<cmd::SetSliceParam>(c.id, 5, cmd::SetSliceParam::Param::gain, -6.0));
    c.sliceToTrack(0.0);
    const auto clips = c.f.arrangement().clipsOnTrack(c.f.arrangement().tracks().back().id);
    REQUIRE(clips.size() == 16);
    CHECK(clips[5].gainDb == Approx(-6.0).margin(0.01));
    CHECK(clips[4].gainDb == Approx(0.0).margin(0.01));
    CHECK(clips[5].pitch == 0.0);
}

TEST_CASE("Undo removes the track and its clips in one step, Redo brings them back", "[arrange][slices]")
{
    Chopped c("s2t-undo");
    const int tracksBefore = c.f.arrangement().numTracks();
    const auto before = c.f.arrangement().serialise();
    c.sliceToTrack(4.0);
    REQUIRE(c.f.arrangement().numClips() == 16);
    c.f.commands->undo();
    CHECK(c.f.arrangement().numTracks() == tracksBefore);
    CHECK(c.f.arrangement().numClips() == 0);
    // Tracktion adds an empty <SCENES/> node (the clip launcher) the first time a track is added; it is not part of the undo.
    const auto plain = [](juce::String xml) { return xml.replace("<SCENES/>", "").removeCharacters(" \n\r\t"); };
    CHECK(plain(c.f.arrangement().serialise()) == plain(before));
    c.f.commands->redo();
    CHECK(c.f.arrangement().numClips() == 16);
    CHECK(c.f.arrangement().numTracks() == tracksBefore + 1);
    CHECK(c.f.violations.isEmpty());
}

TEST_CASE("editing the slice set afterwards leaves the clips as they were", "[arrange][slices]")
{
    Chopped c("s2t-copies");
    c.sliceToTrack(0.0);
    const auto trackId = c.f.arrangement().tracks().back().id;
    const auto before = c.f.arrangement().clipsOnTrack(trackId);
    c.f.commands->apply(std::make_unique<cmd::MoveSlicePoint>(c.id, 2, 3 * 48000 + 5000));
    c.f.commands->apply(std::make_unique<cmd::SetSliceParam>(c.id, 4, cmd::SetSliceParam::Param::pitch, 7.0));
    c.f.commands->apply(std::make_unique<cmd::RemoveSlicePoint>(c.id, 0));
    const auto after = c.f.arrangement().clipsOnTrack(trackId);
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].offsetSeconds == Approx(before[i].offsetSeconds));
        CHECK(after[i].lengthSeconds == Approx(before[i].lengthSeconds));
        CHECK(after[i].originSlice == before[i].originSlice);
        CHECK(after[i].pitch == before[i].pitch);
    }
}

TEST_CASE("the origin of a slice clip survives a save and reopen, and the slice pitch is stored on the clip", "[arrange][slices]")
{
    Chopped c("s2t-roundtrip");
    c.f.commands->apply(std::make_unique<cmd::SetSliceParam>(c.id, 7, cmd::SetSliceParam::Param::pitch, 5.0));
    c.sliceToTrack(8.0);
    const auto bundle = c.f.root.getChildFile("Slices.sdaw");
    juce::String error;
    REQUIRE(c.f.project->saveAs(bundle, error));
    c.f.commands.reset();
    c.f.project.reset();

    auto reopened = sampler::ProjectEdit::open(c.f.host, bundle, error);
    REQUIRE(reopened != nullptr);
    auto& a = reopened->arrangement();
    const auto clips = a.clipsOnTrack(a.tracks().back().id);
    REQUIRE(clips.size() == 16);
    for (size_t i = 0; i < clips.size(); ++i)
    {
        CHECK(clips[i].originSourceId == c.id);
        CHECK(clips[i].originSlice == static_cast<int>(i));
    }
    CHECK(clips[7].pitch == Approx(5.0));
    CHECK(clips[6].pitch == Approx(0.0));
    // The slice set itself came along.
    const auto source = sampler::model::SamplerState(reopened->samplerState()).findSourceById(c.id);
    CHECK(source.sliceSet().numSlices() == 16);
    CHECK(source.sliceSet().slices[7].pitch == 5.0);
}

TEST_CASE("a sound that was never chopped gives one clip of the whole sound", "[arrange][slices]")
{
    ArrangeFixture f("s2t-whole");
    const auto id = f.addSound("tone", 2.0);
    cmd::SlicesToTrack command(id, 0.0);
    f.commands->apply(command);
    const auto clips = f.arrangement().clipsOnTrack(command.trackId());
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].name == "tone 1");
    CHECK(clips[0].lengthSeconds == Approx(2.0).margin(1e-4));
    CHECK(clips[0].offsetSeconds == Approx(0.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("an unknown sound makes no track and no step", "[arrange][slices]")
{
    ArrangeFixture f("s2t-unknown");
    const auto tracks = f.arrangement().numTracks();
    const auto depth = f.project->undoManager().getUndoDescriptions().size();
    f.commands->apply(std::make_unique<cmd::SlicesToTrack>("src_nope", 0.0));
    CHECK(f.arrangement().numTracks() == tracks);
    CHECK(f.project->undoManager().getUndoDescriptions().size() == depth);
}

TEST_CASE("a damaged slice set (points out of order) still lays out only slices with a length", "[arrange][slices]")
{
    ArrangeFixture f("s2t-damaged");
    const auto id = f.addSound("tone", 4.0);
    f.commands->apply(std::make_unique<cmd::AddSlicePoint>(id, 48000));
    auto node = sampler::model::SamplerState(f.project->samplerState()).findSourceById(id).node();
    node.getChildWithName(sampler::model::sliceIds::SLICESET).setProperty(sampler::model::sliceIds::markers, "88312 29512", nullptr);

    cmd::SlicesToTrack command(id, 0.0);
    f.commands->apply(command);
    const auto clips = f.arrangement().clipsOnTrack(command.trackId());
    REQUIRE(clips.size() == 3);
    for (const auto& clip : clips)
        CHECK(clip.lengthSeconds > 0.0);
    CHECK(clips[1].offsetSeconds == Approx(29512.0 / 48000.0).margin(1e-4));
}
