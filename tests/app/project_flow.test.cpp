// New, Open, Save, Save As and Quit with the Save / Discard / Cancel question, driven with stubbed dialogs.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"
#include "model/SamplerState.h"

using Choice = sampler::app::AppController::SaveChoice;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

juce::File freshDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

struct Session
{
    explicit Session(const juce::String& name)
        : root(freshDir(name)), controller(makeOptions(root)), main(controller)
    {
        controller.askToSave = [this](const juce::String&, std::function<void(Choice)> answer) {
            ++questions;
            answer(nextChoice);
        };
        controller.chooseSaveLocation = [this](std::function<void(juce::File)> answer) {
            ++saveDialogs;
            answer(saveTarget);
        };
        controller.chooseProjectToOpen = [this](std::function<void(juce::File)> answer) {
            ++openDialogs;
            answer(openTarget);
        };
        controller.onQuitConfirmed = [this] { ++quits; };
        controller.onToast = [this](const juce::String& text, bool) { toasts.add(text); };
    }

    static sampler::app::AppOptions makeOptions(const juce::File& root)
    {
        sampler::app::AppOptions options;
        options.untitledRoot = root.getChildFile("untitled");
        return options;
    }

    void pump()
    {
        const auto end = juce::Time::getMillisecondCounter() + 20000;
        do
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        while (controller.importer().isBusy() && juce::Time::getMillisecondCounter() < end);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }

    void drop(std::initializer_list<const char*> names)
    {
        juce::StringArray paths;
        for (const auto* n : names)
            paths.add(fixture(n).getFullPathName());
        main.filesDropped(paths, 0, 0);
        pump();
    }

    int untitledFolders() const { return root.getChildFile("untitled").getNumberOfChildFiles(juce::File::findDirectories); }

    juce::File root;
    sampler::app::AppController controller;
    sampler::app::MainComponent main;
    Choice nextChoice = Choice::cancel;
    juce::File saveTarget, openTarget;
    int questions = 0, saveDialogs = 0, openDialogs = 0, quits = 0;
    juce::StringArray toasts;
};
} // namespace

TEST_CASE("Quit with unsaved changes: Cancel keeps the project open", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-cancel");
    s.drop({"kick.wav"});
    REQUIRE(s.controller.isDirty());

    s.nextChoice = Choice::cancel;
    s.controller.requestQuit();
    CHECK(s.questions == 1);
    CHECK(s.quits == 0);
    CHECK(s.controller.isDirty());
    CHECK(s.main.browser().rows().size() == 1);
    CHECK(s.untitledFolders() == 1);
}

TEST_CASE("Quit with unsaved changes: Save then quit saves the project", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-save-quit");
    s.drop({"kick.wav", "drums-loop-120.wav"});

    s.nextChoice = Choice::save;
    s.saveTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestQuit();
    CHECK(s.saveDialogs == 1); // untitled: asks where
    CHECK(s.quits == 1);
    CHECK(s.root.getChildFile("Demo1.sdaw/project.tracktionedit").existsAsFile());
    CHECK(s.root.getChildFile("Demo1.sdaw/audio/originals").getNumberOfChildFiles(juce::File::findFiles) == 2);
    CHECK(s.untitledFolders() == 0);
}

TEST_CASE("Quit with unsaved changes: Save, then cancelling the location, does not quit", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-save-cancel-location");
    s.drop({"kick.wav"});
    s.nextChoice = Choice::save;
    s.saveTarget = juce::File(); // the user cancels the folder dialog
    s.controller.requestQuit();
    CHECK(s.quits == 0);
    CHECK(s.controller.isDirty());
}

TEST_CASE("Quit with Discard deletes the untitled folder", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-discard");
    s.drop({"kick.wav"});
    REQUIRE(s.untitledFolders() == 1);

    s.nextChoice = Choice::discard;
    s.controller.requestQuit();
    CHECK(s.quits == 1);
    CHECK(s.untitledFolders() == 0);
}

TEST_CASE("Quit without changes asks nothing", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-clean");
    s.controller.requestQuit();
    CHECK(s.questions == 0);
    CHECK(s.quits == 1);
    CHECK(s.untitledFolders() == 0); // an empty untitled project leaves nothing behind
}

TEST_CASE("Save names the project, clears the marker and saves in place afterwards", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-save");
    s.drop({"kick.wav", "drums-loop-120.wav"});
    CHECK(s.controller.windowTitle() == "Untitled* - Sampler");

    s.saveTarget = s.root.getChildFile("Demo1");
    s.controller.requestSave();
    CHECK(s.saveDialogs == 1);
    CHECK(s.controller.windowTitle() == "Demo1 - Sampler");
    CHECK_FALSE(s.controller.isDirty());

    s.drop({"5ch-tone.wav"});
    CHECK(s.controller.windowTitle() == "Demo1* - Sampler");
    s.controller.requestSave();
    CHECK(s.saveDialogs == 1); // saved in place, no dialog
    CHECK(s.controller.windowTitle() == "Demo1 - Sampler");

    // The import still writes into the moved folder.
    s.drop({"drums-loop-120.aiff"});
    CHECK(s.root.getChildFile("Demo1.sdaw/audio").getNumberOfChildFiles(juce::File::findFiles) == 4);
}

TEST_CASE("Open brings a saved project back; New then Open", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-open");
    s.drop({"kick.wav", "drums-loop-120.wav"});
    s.saveTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestSave();

    s.nextChoice = Choice::cancel; // must not be asked: the project is clean
    s.controller.requestNew();
    s.pump();
    CHECK(s.questions == 0);
    CHECK(s.main.browser().rows().empty());
    CHECK(s.controller.windowTitle() == "Untitled - Sampler");
    CHECK(s.untitledFolders() == 1);

    s.openTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestOpen();
    s.pump();
    CHECK(s.main.browser().rowNames() == juce::StringArray{"drums-loop-120", "kick"});
    CHECK(s.controller.windowTitle() == "Demo1 - Sampler");
    CHECK(s.untitledFolders() == 0); // the empty untitled project was dropped
    CHECK_FALSE(s.controller.commandService().canUndo());
}

TEST_CASE("Open of a bad folder keeps the current project", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-open-bad");
    s.drop({"kick.wav"});
    s.nextChoice = Choice::discard;

    const auto notAProject = s.root.getChildFile("Nothing.sdaw");
    notAProject.createDirectory();
    s.openTarget = notAProject;
    s.controller.requestOpen();
    s.pump();

    CHECK(s.toasts.size() == 1);
    CHECK(s.toasts[0].contains("is not a Sampler project"));
    CHECK(s.main.browser().rows().size() == 1); // still there, even though Discard was chosen
    CHECK(s.controller.isDirty());
    CHECK(s.untitledFolders() == 1);
}

TEST_CASE("New and Open with unsaved changes ask first", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-new-dirty");
    s.drop({"kick.wav"});

    s.nextChoice = Choice::cancel;
    s.controller.requestNew();
    s.controller.requestOpen();
    CHECK(s.questions == 2);
    CHECK(s.openDialogs == 0);
    CHECK(s.main.browser().rows().size() == 1);

    s.nextChoice = Choice::discard;
    s.controller.requestNew();
    s.pump();
    CHECK(s.questions == 3);
    CHECK(s.main.browser().rows().empty());
    CHECK(s.untitledFolders() == 1); // the old one is gone, a new one exists
}

TEST_CASE("Save As keeps the old project intact and independent", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-saveas");
    s.drop({"kick.wav", "drums-loop-120.wav"});
    s.saveTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestSave();

    s.saveTarget = s.root.getChildFile("Demo2.sdaw");
    s.controller.requestSaveAs();
    CHECK(s.controller.windowTitle() == "Demo2 - Sampler");
    s.drop({"5ch-tone.wav"});
    s.controller.requestSave();

    s.nextChoice = Choice::discard;
    s.openTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestOpen();
    s.pump();
    CHECK(s.main.browser().rows().size() == 2); // Demo1 still has two sounds
}

TEST_CASE("Save As is refused while an import runs", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-saveas-busy");
    s.saveTarget = s.root.getChildFile("Busy.sdaw");
    juce::Array<juce::File> files;
    files.add(fixture("drums-loop-120.wav"));
    s.controller.importFiles(files);
    REQUIRE(s.controller.importer().isBusy());

    s.controller.requestSaveAs();
    CHECK(s.saveDialogs == 0);
    CHECK_FALSE(s.root.getChildFile("Busy.sdaw").exists());
    CHECK(s.controller.project().isUntitled());
    REQUIRE(s.toasts.size() > 0);
    CHECK(s.toasts[0] == "Wait for the import to finish");

    s.pump();
    s.controller.requestSaveAs(); // after the import it works
    CHECK(s.saveDialogs == 1);
    CHECK(s.root.getChildFile("Busy.sdaw/project.tracktionedit").existsAsFile());
}

TEST_CASE("Opening the project that is already open changes nothing", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-open-self");
    s.drop({"kick.wav"});
    const auto folder = s.controller.project().bundleDir();

    CHECK(s.controller.openProject(folder));
    CHECK(folder.isDirectory());
    CHECK(s.controller.project().bundleDir() == folder);
    CHECK(s.controller.project().isUntitled());
    CHECK(s.untitledFolders() == 1);
    CHECK(s.main.browser().rows().size() == 1);
}

TEST_CASE("Opening the saved project that is open again reverts its unsaved changes", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-revert");
    s.drop({"kick.wav"});
    s.saveTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestSave();
    s.drop({"drums-loop-120.wav"});
    REQUIRE(s.controller.isDirty());
    REQUIRE(s.main.browser().rows().size() == 2);

    s.nextChoice = Choice::discard;
    s.openTarget = s.root.getChildFile("Demo1.sdaw");
    s.controller.requestOpen();
    s.pump();
    CHECK(s.main.browser().rows().size() == 1);
    CHECK_FALSE(s.controller.isDirty());
    CHECK(s.root.getChildFile("Demo1.sdaw/project.tracktionedit").existsAsFile());
}

TEST_CASE("New, Open and Quit are refused while an import runs", "[app][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Session s("flow-busy-refuse");
    juce::Array<juce::File> files;
    files.add(fixture("drums-loop-120.wav"));
    s.controller.importFiles(files);
    REQUIRE(s.controller.importer().isBusy());

    s.controller.requestNew();
    s.controller.requestOpen();
    s.controller.requestQuit();
    CHECK(s.questions == 0);
    CHECK(s.openDialogs == 0);
    CHECK(s.quits == 0);
    REQUIRE(s.toasts.size() == 3);
    CHECK(s.toasts[0] == "Wait for the import to finish");

    s.pump(); // the import was not cancelled
    CHECK(s.main.browser().rows().size() == 1);
    s.nextChoice = Choice::discard;
    s.controller.requestQuit();
    CHECK(s.quits == 1);
}
