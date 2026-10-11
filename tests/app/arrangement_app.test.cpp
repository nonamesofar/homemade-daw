// The main window with the arrangement: Explorer drops, Edit and Track menu commands, shortcuts and key routing.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"
#include "engine/Arrangement.h"
#include "engine/Transport.h"

using Catch::Approx;
using namespace sampler::app;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

AppOptions options(const juce::String& name)
{
    AppOptions o;
    o.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name);
    o.untitledRoot.deleteRecursively();
    return o;
}

void pump(AppController& controller, int timeoutMs = 20000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    do
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    while (controller.importer().isBusy() && juce::Time::getMillisecondCounter() < end);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}

struct App
{
    explicit App(const juce::String& name) : controller(options(name)), main(controller)
    {
        REQUIRE(controller.startupError().isEmpty());
        main.setSize(1440, 900);
    }

    sampler::engine::Arrangement& arrangement() { return controller.project().arrangement(); }

    void importAndPlaceFixtures(std::initializer_list<const char*> names)
    {
        juce::Array<juce::File> files;
        for (const auto* n : names)
            files.add(fixture(n));
        controller.importFiles(files);
        pump(controller);
    }

    bool enabled(juce::CommandID id)
    {
        juce::ApplicationCommandInfo info(id);
        main.getCommandInfo(id, info);
        return (info.flags & juce::ApplicationCommandInfo::isDisabled) == 0;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    AppController controller;
    MainComponent main;
};
} // namespace

TEST_CASE("Explorer files dropped on a lane become clips one after another, in one undo step with the sounds", "[app][gui][arrange]")
{
    App app("app-explorer-drop");
    REQUIRE(app.main.arrangement() != nullptr);
    auto& lanes = app.main.arrangement()->lanes();
    const auto before = app.controller.commandService().undoName();

    // Bar 1 of track 1: lane x = 0, y inside the first lane.
    lanes.filesDropped({fixture("drums-loop-120.wav").getFullPathName(), fixture("kick.wav").getFullPathName()}, 2, 50);
    pump(app.controller);

    auto clips = app.arrangement().clips();
    REQUIRE(clips.size() == 2);
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    CHECK(clips[0].start == Approx(0.0));
    CHECK(clips[1].start == Approx(clips[0].end()));
    CHECK(clips[0].trackId == clips[1].trackId);
    CHECK(app.controller.commandService().undoName().startsWith("Import"));

    app.controller.commandService().undo(); // one undo removes the clips and the sounds
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(app.arrangement().numClips() == 0);
    CHECK(app.main.browser().rows().empty());
    CHECK(app.controller.commandService().undoName() == before);

    // Below the lanes: a new track.
    lanes.filesDropped({fixture("kick.wav").getFullPathName()}, 2, 400);
    pump(app.controller);
    CHECK(app.arrangement().numTracks() == 2);
    CHECK(app.arrangement().numClips() == 1);
}

TEST_CASE("placeholders show while a file imports onto the timeline", "[app][gui][arrange]")
{
    App app("app-placeholder");
    auto& lanes = app.main.arrangement()->lanes();
    lanes.filesDropped({fixture("drums-loop-120.wav").getFullPathName()}, 2, 50);
    // Right after the drop the import runs: the registry carries its target (the lanes paint it as a grey box).
    const auto& jobs = app.controller.jobs().jobs();
    REQUIRE_FALSE(jobs.empty());
    REQUIRE(jobs.front().placement.has_value());
    CHECK(jobs.front().placement->trackIndex == 0);
    CHECK(jobs.front().placement->trackId == app.arrangement().tracks()[0].id); // by id, not only by position
    pump(app.controller);
    CHECK(app.controller.jobs().jobs().empty());
    CHECK(app.arrangement().numClips() == 1);
}

TEST_CASE("Edit and Track menus offer the arrangement commands with the right enabled states", "[app][gui][arrange]")
{
    App app("app-menus");
    CHECK(app.main.getMenuBarNames() == juce::StringArray{"File", "Edit", "Track", "Help"});
    CHECK_FALSE(app.enabled(pasteCommand)); // nothing copied yet
    CHECK_FALSE(app.enabled(copyCommand));
    CHECK_FALSE(app.enabled(duplicateCommand));
    CHECK_FALSE(app.enabled(splitCommand));
    CHECK_FALSE(app.enabled(selectAllCommand)); // no clips
    CHECK(app.enabled(newTrackCommand));

    app.importAndPlaceFixtures({"kick.wav"});
    const auto id = app.main.browser().rows().front().id;
    sampler::commands::CreateClipsFromSources command({id}, {0, false, 0.0});
    app.controller.commandService().apply(command);
    pump(app.controller);
    CHECK(app.enabled(selectAllCommand));
    CHECK_FALSE(app.enabled(copyCommand)); // nothing selected

    REQUIRE(app.main.invoke(selectAllCommand));
    CHECK(app.main.selection().clips().size() == 1);
    CHECK(app.enabled(copyCommand));
    CHECK(app.enabled(duplicateCommand));
    CHECK(app.enabled(deleteCommand));
    CHECK_FALSE(app.enabled(pasteCommand));
    app.main.invoke(copyCommand);
    CHECK(app.enabled(pasteCommand));
    CHECK(app.main.clipboard().size() == 1);

    // The Edit menu names the step to undo.
    juce::ApplicationCommandInfo info(undoCommand);
    app.main.getCommandInfo(undoCommand, info);
    CHECK(info.shortName.startsWith("Undo Add clip"));
}

TEST_CASE("shortcut commands: split, duplicate, copy, paste, cut, delete, undo with selection mapping", "[app][gui][arrange]")
{
    App app("app-shortcuts");
    app.importAndPlaceFixtures({"drums-loop-120.wav"});
    const auto id = app.main.browser().rows().front().id;
    sampler::commands::CreateClipsFromSources place({id}, {0, false, 0.0});
    app.controller.commandService().apply(place);
    pump(app.controller);
    auto& a = app.arrangement();
    auto& transport = app.controller.project().transport();
    REQUIRE(a.numClips() == 1);
    const auto clip = place.resultIds().front();
    app.main.selection().setClips({clip});

    // Ctrl+E at bar 2: two clips.
    transport.locateBeats(4.0);
    app.main.invoke(splitCommand);
    CHECK(a.numClips() == 2);
    CHECK(app.controller.commandService().undoName() == "Split clip");

    // Ctrl+D on one clip, twice.
    app.main.selection().setClips({clip});
    app.main.invoke(duplicateCommand);
    app.main.invoke(duplicateCommand);
    CHECK(a.numClips() == 3); // the first copy covers the right half (no-overlap rule), the second lands after it
    CHECK(app.main.selection().clips().size() == 1); // the copies are selected

    // Copy, select the next track, paste at the playhead.
    app.main.invoke(selectAllCommand);
    CHECK(app.main.selection().clips().size() == 3);
    app.main.invoke(copyCommand);
    app.main.invoke(newTrackCommand);
    transport.locateBeats(40.0);
    app.main.invoke(pasteCommand);
    CHECK(a.numClips() == 6);
    CHECK(app.controller.commandService().undoName() == "Paste 3 clips");
    CHECK(app.main.selection().clips().size() == 3);

    // Cut removes them again; undo brings them back and selects them.
    app.main.invoke(cutCommand);
    CHECK(a.numClips() == 3);
    CHECK(app.main.selection().isEmpty());
    app.main.invoke(undoCommand);
    CHECK(a.numClips() == 6);
    CHECK(app.main.selection().clips().size() == 3);

    // Delete, with the sounds staying in the browser.
    app.main.invoke(deleteCommand);
    CHECK(a.numClips() == 3);
    CHECK(app.main.browser().rows().size() == 1);

    // Undo and redo leave the transport alone.
    const auto position = transport.positionBeats();
    app.main.invoke(undoCommand);
    app.main.invoke(redoCommand);
    CHECK(transport.positionBeats() == Approx(position));
}

TEST_CASE("track commands: Mod+T after the selected track, Delete Track, M and S on the selected track", "[app][gui][arrange]")
{
    App app("app-track-commands");
    auto& a = app.arrangement();
    const auto first = a.tracks()[0].id;
    app.main.invoke(newTrackCommand);
    app.main.invoke(newTrackCommand);
    REQUIRE(a.numTracks() == 3);
    CHECK(a.tracks()[0].id == first);
    CHECK(a.tracks()[2].name == "Audio 3"); // each Mod+T goes after the track the previous one selected

    app.main.selection().setTrack(a.tracks()[2].id);
    app.main.invoke(muteCommand);
    CHECK(a.tracks()[2].mute);
    app.main.invoke(muteCommand);
    CHECK_FALSE(a.tracks()[2].mute);
    app.main.invoke(soloCommand);
    CHECK(a.tracks()[2].solo);

    app.main.invoke(deleteTrackCommand);
    CHECK(a.numTracks() == 2);
    app.controller.commandService().undo();
    CHECK(a.numTracks() == 3);
}

TEST_CASE("Delete removes a track only when its header was selected and no clip is", "[app][gui][arrange]")
{
    App app("app-delete-track");
    auto& a = app.arrangement();
    const auto first = a.tracks()[0].id;
    app.main.invoke(deleteCommand); // fresh project: the first track is only "current"
    REQUIRE(a.numTracks() == 1);
    CHECK(a.tracks()[0].name == "Audio 1");

    app.main.invoke(newTrackCommand);
    REQUIRE(a.numTracks() == 2);
    app.main.selection().setTrack(first); // a click on an empty lane
    app.main.invoke(deleteCommand);
    CHECK(a.numTracks() == 2);

    app.main.selection().selectHeader(first); // a click on the header
    app.main.invoke(deleteCommand);
    CHECK(a.numTracks() == 1);
    CHECK_FALSE(a.track(first).has_value());
    app.main.invoke(deleteCommand); // the selection moved on: a second Delete keeps the remaining track
    CHECK(a.numTracks() == 1);
}

TEST_CASE("a header-selected track that disappears (undo of Add track) leaves nothing for Delete to remove", "[app][gui][arrange]")
{
    App app("app-stale-track");
    auto& a = app.arrangement();
    app.main.invoke(newTrackCommand);
    REQUIRE(a.numTracks() == 2);
    const auto added = a.tracks()[1].id;
    app.main.selection().selectHeader(added);
    CHECK(app.enabled(deleteCommand));

    app.main.invoke(undoCommand);
    pump(app.controller); // the view refreshes on the arrangement's change message
    REQUIRE(a.numTracks() == 1);
    CHECK_FALSE(app.main.selection().isHeaderSelected());
    CHECK(app.main.selection().track() == a.tracks()[0].id);
    CHECK_FALSE(app.enabled(deleteCommand));
    app.main.invoke(deleteCommand);
    CHECK(a.numTracks() == 1);
    CHECK(app.controller.commandService().undoName() != "Delete track");
}

TEST_CASE("transport commands: loop toggle, Home and the second stop, Shift+Space start", "[app][gui][arrange]")
{
    App app("app-transport-commands");
    auto& transport = app.controller.project().transport();
    app.controller.commandService().apply(std::make_unique<sampler::commands::SetLoop>(sampler::engine::LoopRange{16.0, 32.0}));

    CHECK_FALSE(transport.isLooping());
    app.main.invoke(loopToggleCommand);
    CHECK(transport.isLooping());
    CHECK(app.controller.commandService().undoName() == "Set loop"); // looping itself is not a step
    transport.locateBeats(40.0);
    app.main.invoke(returnToStartCommand);
    CHECK(transport.positionBeats() == Approx(16.0)); // the loop start
    app.main.invoke(loopToggleCommand);
    app.main.invoke(returnToStartCommand);
    CHECK(transport.positionBeats() == Approx(0.0));
}

TEST_CASE("keys: shortcuts reach the commands, and a focused text field would keep them", "[app][gui][arrange]")
{
    App app("app-keys");
    auto& a = app.arrangement();
    // Space with nothing special focused toggles play (no audio is needed to see it pending or playing).
    auto& transport = app.controller.project().transport();
    CHECK(app.main.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    CHECK((transport.isPlaying() || transport.isPlayPending()));
    app.main.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    transport.stop();

    // Mod+T adds a track through the shortcut table.
    CHECK(app.main.keyPressed(juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)));
    CHECK(a.numTracks() == 2);
    // The browser search box is a text editor with the keyboard focus: the router leaves its keys alone, so a letter types
    // into it and Delete edits its text instead of reaching the arrangement.
    app.main.addToDesktop(0);
    app.main.setVisible(true);
    auto& search = app.main.browser().searchBox();
    search.setText("ab", false);
    search.grabKeyboardFocus();
    REQUIRE(juce::Component::getCurrentlyFocusedComponent() == &search);
    const auto second = a.tracks()[1].id;
    app.main.selection().selectHeader(second); // Delete would remove this track if it leaked past the text field
    const auto steps = app.controller.commandService().undoName();
    const auto serialised = a.serialise();
    const auto position = transport.positionBeats();

    CHECK_FALSE(app.main.keyPressed(juce::KeyPress('z')));
    CHECK_FALSE(app.main.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK_FALSE(app.main.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    // What the focused component does with them, as the real dispatcher would after the window passes them on.
    CHECK(search.keyPressed(juce::KeyPress('z', juce::ModifierKeys(), 'z')));
    CHECK(search.getText().contains("z"));

    CHECK(a.numTracks() == 2);
    CHECK(a.serialise() == serialised);
    CHECK(app.controller.commandService().undoName() == steps);
    CHECK(transport.positionBeats() == position);
    CHECK_FALSE(transport.isPlaying());
    CHECK_FALSE(transport.isPlayPending());

    // The same Delete without the text focus does delete the header-selected track (so the checks above are not vacuous).
    search.giveAwayKeyboardFocus();
    REQUIRE(dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) == nullptr);
    CHECK(app.main.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK(a.numTracks() == 1);
    CHECK_FALSE(a.track(second).has_value());
    app.main.removeFromDesktop();
}
