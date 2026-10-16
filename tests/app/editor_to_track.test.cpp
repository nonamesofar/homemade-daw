// M3 4.2: "Slices to new track" in the editor and "Open in sample editor" in the clip panel.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "EditorFixture.h"
#include "commands/SliceCommands.h"

using namespace sampler::test;

namespace
{
struct ToTrack : Session
{
    explicit ToTrack(const juce::String& name) : Session(name)
    {
        main.browser().onRowOpened(id);
        pump();
        ed().modeControl.setSelected(0, true); // Beat, 1/4: 17 slices
        pump(20);
    }

    sampler::ui::SampleEditorView& ed() { return editor(); }
    sampler::engine::Arrangement& arrangement() { return controller.project().arrangement(); }
};
} // namespace

TEST_CASE("Slices to new track lays the slices out from the playhead and shows the arrangement with the clips selected", "[app][editor][totrack]")
{
    ToTrack t("totrack-basic");
    const int tracksBefore = t.arrangement().numTracks();
    t.controller.project().transport().locateBeats(16.0); // bar 5
    CHECK(t.ed().slicesToTrackButton.isVisible());
    CHECK(t.ed().slicesToTrackButton.isEnabled());

    t.ed().slicesToTrackButton.onClick();
    pump(20);

    CHECK(t.controller.commandService().undoName() == "Slices to new track");
    REQUIRE(t.arrangement().numTracks() == tracksBefore + 1);
    const auto track = t.arrangement().tracks().back();
    CHECK(track.name == "break-90");
    const auto clips = t.arrangement().clipsOnTrack(track.id);
    REQUIRE(clips.size() == 17);
    CHECK(clips.front().start == Catch::Approx(16.0).margin(1e-6));
    CHECK(clips.front().name == "break-90 1");
    CHECK(clips.back().name == "break-90 17");

    // The arrangement is shown again, with the new clips (and their track) selected.
    CHECK_FALSE(t.main.isEditorOpen());
    CHECK(t.main.arrangement()->isVisible());
    CHECK(t.main.selection().track() == track.id);
    REQUIRE(t.main.selection().clips().size() == 17);
    for (const auto& clip : clips)
        CHECK(t.main.selection().isSelected(clip.id));

    // One Undo takes the track and its clips away.
    t.main.invoke(sampler::app::undoCommand);
    pump(20);
    CHECK(t.arrangement().numTracks() == tracksBefore);
    CHECK(t.arrangement().numClips() == 0);
}

TEST_CASE("a clip made from a slice opens its sound in the editor, and editing the slices leaves the clip alone", "[app][editor][totrack]")
{
    ToTrack t("totrack-open");
    t.ed().slicesToTrackButton.onClick();
    pump(20);
    const auto clips = t.arrangement().clipsOnTrack(t.arrangement().tracks().back().id);
    REQUIRE_FALSE(clips.empty());
    t.main.selection().setClips({clips[3].id});
    CHECK(t.main.inspector()->openEditor.isEnabled());
    t.main.inspector()->openEditor.onClick();
    CHECK(t.main.isEditorOpen());
    CHECK(t.ed().sourceId() == t.id);

    // Move a slice point: the clips do not change.
    const auto before = t.arrangement().clipsOnTrack(t.arrangement().tracks().back().id);
    auto& wave = t.ed().waveform();
    REQUIRE_FALSE(wave.markers().empty());
    t.controller.commandService().apply(std::make_unique<sampler::commands::MoveSlicePoint>(t.id, 2, wave.markers()[2] + 3000));
    pump(20);
    const auto after = t.arrangement().clipsOnTrack(t.arrangement().tracks().back().id);
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK(after[i].offsetSeconds == Catch::Approx(before[i].offsetSeconds));
        CHECK(after[i].lengthSeconds == Catch::Approx(before[i].lengthSeconds));
    }
}

TEST_CASE("the clip panel cannot open a sound whose file is missing", "[app][editor][totrack]")
{
    Session s("totrack-missing");
    sampler::commands::CreateClipsFromSources place({s.id}, {0, false, 0.0});
    s.controller.commandService().apply(place);
    const auto clipId = place.resultIds().front();

    const auto source = sampler::model::SamplerState(s.controller.project().samplerState()).findSourceById(s.id);
    REQUIRE(s.controller.project().bundleDir().getChildFile(source.decoded()).deleteFile());

    s.main.selection().setClips({});
    s.main.selection().setClips({clipId});
    REQUIRE(s.main.arrangement() != nullptr);
    s.main.inspector()->refresh();
    CHECK(s.main.inspector()->showsClip());
    CHECK_FALSE(s.main.inspector()->openEditor.isEnabled());
    s.main.inspector()->openEditor.onClick(); // does nothing
    CHECK_FALSE(s.main.isEditorOpen());
}
