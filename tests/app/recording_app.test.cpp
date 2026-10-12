// M2b: recording through the main window and the project lifecycle, with a fake System source.
#include "FakeCapture.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"
#include "commands/Commands.h"
#include "engine/Arrangement.h"
#include "ui/browser/BrowserModel.h"

using Catch::Approx;
using namespace sampler::app;
using sampler::test::FakeCaptureSource;

namespace
{
void clickButton(juce::Button& button)
{
    button.triggerClick(); // posts a message
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

/** A project folder name no earlier run used: the engine remembers the last project's cache folder and re-creates it. */
juce::File uniqueBundle(const juce::File& root, const juce::String& name)
{
    return root.getChildFile(name + "-" + juce::Uuid().toString().substring(0, 8) + ".sdaw");
}

struct RecApp
{
    explicit RecApp(const juce::String& name)
        : root(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name)),
          controller(makeOptions()),
          main(controller)
    {
        REQUIRE(controller.startupError().isEmpty());
        main.setSize(1440, 900);
        script.clockMs = [this] { return clock; };
        controller.onToast = [this](const juce::String& text, bool) { toasts.add(text); };
    }

    AppOptions makeOptions()
    {
        root.deleteRecursively();
        AppOptions o;
        o.untitledRoot = root.getChildFile("untitled");
        o.adjustRecordingEnv = [this](RecordingEnv& env) {
            env.isPlaying = [this] { return playing; };
            env.isPlayPending = [] { return false; };
            env.positionBeats = [this] { return beat; };
            env.play = [this] { playing = true; };
            env.clockMs = [this] { return clock; };
            env.makeCapture = [this] {
                auto fake = std::make_unique<FakeCaptureSource>(script);
                made.push_back(fake.get());
                return std::unique_ptr<sampler::platform::ICaptureSource>(std::move(fake));
            };
            env.listDevices = [] { return std::vector<sampler::platform::PlaybackDevice>{{"dev-a", "Fake Speakers", true}}; };
            env.setOutputMuted = [this](bool m) { muted = m; };
            env.outputDeviceType = [] { return juce::String("Windows Audio"); };
            env.outputDeviceName = [this] { return muteWanted ? juce::String("Fake Speakers") : juce::String("Other"); };
        };
        return o;
    }

    sampler::engine::Arrangement& arrangement() { return controller.project().arrangement(); }
    RecordingController& rec() { return controller.recording(); }

    void armFirstTrack() { clickButton(main.arrangement()->headers().header(0)->armButton); }

    void advance(double seconds, bool tone)
    {
        for (int i = 0; i < static_cast<int>(seconds * 10.0 + 0.5); ++i)
        {
            if (! made.empty() && rec().isRecording())
                made.back()->push(0.1, tone);
            clock += 100.0;
            if (playing)
                beat += 0.2;
            rec().tick();
        }
    }

    void settle()
    {
        for (int i = 0; i < 400 && rec().state() != RecordingController::State::idle; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    }

    bool toasted(const juce::String& text) const
    {
        for (const auto& t : toasts)
            if (t.contains(text))
                return true;
        return false;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::File root;
    FakeCaptureSource::Script script;
    std::vector<FakeCaptureSource*> made;
    juce::StringArray toasts;
    double clock = 1000.0, beat = 0.0;
    bool playing = false, muted = false, muteWanted = false;
    AppController controller;
    MainComponent main;
};
} // namespace

TEST_CASE("Mod+R records onto the armed track: one clip at the playhead and one Recordings row", "[app][gui][record]")
{
    RecApp app("rec-app-modr");
    app.beat = 8.0; // bar 3
    app.armFirstTrack();
    CHECK(app.rec().armState().isArmed(app.arrangement().tracks()[0].id));

    REQUIRE(app.main.invoke(recordCommand));
    CHECK(app.rec().isRecording());
    CHECK(app.main.header()->recordButton.isEnabled());
    app.main.tick(16.0);
    CHECK(app.main.header()->recordButton.isLit());
    app.advance(3.0, true);
    REQUIRE(app.main.invoke(recordCommand));
    app.settle();
    app.main.tick(16.0);
    CHECK_FALSE(app.main.header()->recordButton.isLit());

    const auto clips = app.arrangement().clips();
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == Approx(8.0).margin(0.1));
    CHECK(app.controller.commandService().undoName() == "Record take");

    sampler::ui::BrowserModel model;
    model.setRows(app.controller.browserRows());
    CHECK(model.countIn(sampler::ui::Place::recordings) == 1);
    CHECK(model.countIn(sampler::ui::Place::loops) == 0);
    const auto rows = app.controller.browserRows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].tooltip().startsWith("Recorded from System audio (Fake Speakers)"));

    // Setting the take to Loop moves it out of Recordings.
    app.controller.commandService().apply(
        std::make_unique<sampler::commands::SetSourceCategory>(rows[0].id, sampler::model::Category::loop));
    model.setRows(app.controller.browserRows());
    CHECK(model.countIn(sampler::ui::Place::recordings) == 0);
    CHECK(model.countIn(sampler::ui::Place::loops) == 1);
}

TEST_CASE("a take recorded in an untitled project moves with the first Save", "[app][gui][record]")
{
    RecApp app("rec-app-untitled");
    app.armFirstTrack();
    REQUIRE(app.rec().start());
    app.advance(2.0, true);
    app.rec().stop();
    app.settle();
    REQUIRE(app.arrangement().numClips() == 1);

    const auto target = uniqueBundle(app.root, "Grab");
    app.controller.chooseSaveLocation = [target](std::function<void(juce::File)> answer) { answer(target); };
    app.controller.requestSaveAs();
    REQUIRE(target.isDirectory());
    CHECK(target.getChildFile("audio/recordings").findChildFiles(juce::File::findFiles, false, "rec-*.wav").size() == 1);

    REQUIRE(app.controller.openProject(target));
    REQUIRE(app.arrangement().numClips() == 1);
    CHECK(app.arrangement().clips()[0].missing == false);
}

TEST_CASE("project actions during a recording", "[app][gui][record]")
{
    RecApp app("rec-app-actions");
    bool askedToSave = false, quit = false, savePlaceAsked = false;
    app.controller.askToSave = [&](const juce::String&, std::function<void(AppController::SaveChoice)> answer) {
        askedToSave = true;
        answer(AppController::SaveChoice::cancel);
    };
    app.controller.onQuitConfirmed = [&] { quit = true; };
    app.controller.chooseSaveLocation = [&](std::function<void(juce::File)>) { savePlaceAsked = true; };

    app.armFirstTrack();
    REQUIRE(app.rec().start());
    app.advance(1.0, true);

    SECTION("Save As and an untitled Save are refused")
    {
        app.controller.requestSaveAs();
        CHECK(app.toasted("Stop recording first"));
        CHECK_FALSE(savePlaceAsked);
        app.toasts.clear();
        app.controller.requestSave();
        CHECK(app.toasted("Stop recording first"));
        CHECK_FALSE(savePlaceAsked);
        CHECK(app.rec().isRecording());
        app.rec().stop();
        app.settle();
    }
    SECTION("Quit finishes the recording into the project, then asks about unsaved changes")
    {
        app.controller.requestQuit();
        app.settle();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
        CHECK(askedToSave);
        CHECK(app.arrangement().numClips() == 1); // the take was added before the question
        CHECK_FALSE(quit);                        // the user cancelled
    }
    SECTION("New does the same")
    {
        app.controller.requestNew();
        app.settle();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
        CHECK(askedToSave);
        CHECK(app.arrangement().numClips() == 1);
    }
}

TEST_CASE("plain Save works during a recording in a saved project", "[app][gui][record]")
{
    RecApp app("rec-app-save");
    const auto target = uniqueBundle(app.root, "Saved");
    app.controller.chooseSaveLocation = [target](std::function<void(juce::File)> answer) { answer(target); };
    app.controller.requestSaveAs();
    REQUIRE(target.isDirectory());
    const auto editFile = target.getChildFile("project.tracktionedit");
    REQUIRE(editFile.existsAsFile());

    app.armFirstTrack();
    REQUIRE(app.rec().start());
    app.advance(1.0, true);
    // A mixer change is allowed while recording: it makes the project dirty, and Save must write it.
    app.controller.commandService().apply(std::make_unique<sampler::commands::SetTrackParam>(
        app.arrangement().tracks()[0].id, sampler::commands::SetTrackParam::Control::volume, -6.0));
    REQUIRE(app.controller.isDirty());
    const auto before = editFile.loadFileAsString();
    app.toasts.clear();
    app.controller.requestSave();
    CHECK_FALSE(app.toasted("Stop recording first"));
    CHECK(app.rec().isRecording());
    CHECK_FALSE(app.controller.isDirty());

    const auto saved = editFile.loadFileAsString();
    CHECK(saved != before); // the file was written
    CHECK_FALSE(saved.contains("samplerTap"));
    CHECK_FALSE(saved.contains("sampler_tap"));
    CHECK_FALSE(saved.contains("rec-")); // the unfinished take is not in the saved edit
    CHECK(app.arrangement().numClips() == 0);

    app.rec().stop();
    app.settle();
    CHECK(app.arrangement().numClips() == 1);
    CHECK(app.controller.isDirty()); // the take is a change after the save
}

TEST_CASE("saving during a muted recording leaves the volumes alone", "[app][gui][record]")
{
    RecApp app("rec-app-muted-save");
    app.script.mode = "endpoint-loopback";
    app.muteWanted = true;
    const auto target = uniqueBundle(app.root, "Muted");
    app.controller.chooseSaveLocation = [target](std::function<void(juce::File)> answer) { answer(target); };
    app.controller.requestSaveAs();
    REQUIRE(target.isDirectory());
    app.controller.commandService().apply(std::make_unique<sampler::commands::SetTrackParam>(
        app.arrangement().tracks()[0].id, sampler::commands::SetTrackParam::Control::volume, -6.0));
    app.controller.requestSave();
    const auto before = app.arrangement().serialise();

    app.armFirstTrack();
    REQUIRE(app.rec().start());
    CHECK(app.muted);
    app.advance(1.0, true);
    app.controller.requestSave();
    CHECK(app.arrangement().tracks()[0].volumeDb == Approx(-6.0).margin(0.1));
    CHECK(app.arrangement().serialise() == before); // the mute is below the edit
    app.rec().stop();
    CHECK_FALSE(app.muted);
    app.settle();
    CHECK(app.arrangement().tracks()[0].volumeDb == Approx(-6.0).margin(0.1));
}

TEST_CASE("an import and a recording cannot overlap", "[app][gui][record]")
{
    RecApp app("rec-app-import");
    juce::Array<juce::File> files;
    files.add(juce::File(SAMPLER_FIXTURES_DIR).getChildFile("drums-loop-120.wav"));

    SECTION("an import is refused while recording")
    {
        app.armFirstTrack();
        REQUIRE(app.rec().start());
        app.advance(1.0, true);
        app.controller.importFiles(files);
        CHECK(app.toasted("Stop recording first"));
        CHECK_FALSE(app.controller.importer().isBusy());
        app.toasts.clear();
        app.controller.importAndPlace(files, {0, false, 0.0});
        CHECK(app.toasted("Stop recording first"));
        CHECK_FALSE(app.controller.importer().isBusy());
        app.rec().stop();
        app.settle();
        CHECK(app.arrangement().numClips() == 1); // the take only; nothing was imported
    }
    SECTION("an import is refused while the takes are being finished")
    {
        app.armFirstTrack();
        REQUIRE(app.rec().start());
        app.advance(1.0, true);
        app.rec().stop();
        REQUIRE(app.rec().state() == RecordingController::State::finishing);
        app.controller.importFiles(files);
        CHECK(app.toasted("Stop recording first"));
        CHECK_FALSE(app.controller.importer().isBusy());
        app.settle();
    }
    SECTION("Record is refused while an import runs")
    {
        app.controller.importFiles(files);
        REQUIRE(app.controller.importer().isBusy());
        app.armFirstTrack();
        CHECK_FALSE(app.rec().start());
        CHECK(app.toasted("Wait for the import to finish"));
        CHECK(app.made.empty());
        CHECK_FALSE(app.rec().isActive());
        const auto end = juce::Time::getMillisecondCounter() + 20000;
        while (app.controller.importer().isBusy() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
}
