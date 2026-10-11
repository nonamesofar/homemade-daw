// M1: the project (bundle + edit + SAMPLER section) and the engine's audio-ready signal.
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "commands/CommandService.h"
#include "commands/Commands.h"
#include "engine/Arrangement.h"
#include "engine/EngineHost.h"
#include "engine/ProjectEdit.h"
#include "model/SamplerState.h"

namespace
{
juce::File freshRoot(const juce::String& name)
{
    auto root = sampler::test::tempDir(name);
    root.deleteRecursively();
    root.createDirectory();
    return root;
}
} // namespace

TEST_CASE("a new untitled project has the bundle folders and a SAMPLER section at schema version 1",
          "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-new");
    sampler::EngineHost host;

    juce::String error;
    auto project = sampler::ProjectEdit::createUntitled(host, root, error);
    REQUIRE(project != nullptr);
    CHECK(error.isEmpty());

    CHECK(project->isUntitled());
    CHECK(project->name() == "Untitled");
    CHECK(project->bundleDir().getParentDirectory() == root);
    CHECK(project->bundleDir().hasFileExtension("sdaw"));
    CHECK(project->editFile().existsAsFile());
    CHECK(project->originalsDir().isDirectory());
    CHECK(project->audioDir().isDirectory());
    CHECK(project->cacheDir().isDirectory());

    sampler::model::SamplerState state(project->samplerState());
    REQUIRE(state.isValid());
    CHECK(state.schemaVersion() == 1);
    CHECK(state.numSources() == 0);
    CHECK_FALSE(project->undoManager().canUndo()); // nothing to save yet (dirty state comes from the history, design D-4)

    // The section is in the file on disk, not only in memory.
    const auto xml = juce::XmlDocument::parse(project->editFile());
    REQUIRE(xml != nullptr);
    const auto* section = xml->getChildByName("SAMPLER");
    REQUIRE(section != nullptr);
    CHECK(section->getIntAttribute("schemaVersion") == 1);
}

TEST_CASE("each new untitled project gets its own folder", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-two");
    sampler::EngineHost host;
    juce::String error;
    auto a = sampler::ProjectEdit::createUntitled(host, root, error);
    a.reset();
    auto b = sampler::ProjectEdit::createUntitled(host, root, error);
    REQUIRE(b != nullptr);
    CHECK(root.getNumberOfChildFiles(juce::File::findDirectories) == 2);
}

TEST_CASE("edits to the SAMPLER section mark the project dirty and save clears it, undo history is kept",
          "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-dirty");
    sampler::EngineHost host;
    juce::String error;
    auto project = sampler::ProjectEdit::createUntitled(host, root, error);
    REQUIRE(project != nullptr);

    // Dirty = the history token differs from the one recorded at the last save (design D-4).
    sampler::commands::CommandService commands(project->samplerState(), project->undoManager(), project->bundleDir(),
                                               &project->arrangement());
    auto saved = commands.historyToken();
    const auto dirty = [&] { return commands.historyToken() != saved; };

    sampler::model::SamplerState state(project->samplerState());
    sampler::model::SourceData data;
    data.id = "src_00000001";
    data.hash = "sha256:00000001";
    data.original = "audio/originals/a.wav";
    data.decoded = "audio/a.wav";
    commands.invariantChecksEnabled = false; // the files are not on disk here
    commands.apply(std::make_unique<sampler::commands::AddSources>(std::vector<sampler::model::SourceData>{data}));
    REQUIRE(state.numSources() == 1);
    CHECK(dirty());

    REQUIRE(project->save());
    saved = commands.historyToken();
    CHECK_FALSE(dirty());
    CHECK(commands.canUndo()); // saving does not touch the history

    commands.undo();
    CHECK(dirty()); // undo after save is an unsaved change
    CHECK(state.numSources() == 0);
    commands.redo();
    CHECK_FALSE(dirty()); // back where it was saved

    // The saved file holds the source that was in the section when it was saved.
    const auto xml = juce::XmlDocument::parse(project->editFile());
    REQUIRE(xml != nullptr);
    CHECK(xml->getChildByName("SAMPLER")->getNumChildElements() == 1);
}

TEST_CASE("a bundle folder can be moved by copying when a rename is not possible", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-copy-move");
    const auto source = root.getChildFile("a.sdaw");
    REQUIRE(source.getChildFile("audio/originals").createDirectory());
    REQUIRE(source.getChildFile(sampler::ProjectEdit::editFileName).replaceWithText("<EDIT/>"));
    REQUIRE(source.getChildFile("audio/x.wav").replaceWithText("data"));

    const auto destination = root.getChildFile("b.sdaw");
    REQUIRE(sampler::detail::moveBundleFolder(source, destination, false)); // the cross-drive path
    CHECK_FALSE(source.exists());
    CHECK(destination.getChildFile(sampler::ProjectEdit::editFileName).existsAsFile());
    CHECK(destination.getChildFile("audio/x.wav").loadFileAsString() == "data");

    // A source without a project file is not a bundle: nothing is deleted and the half copy is removed.
    const auto notBundle = root.getChildFile("c");
    REQUIRE(notBundle.getChildFile("sub").createDirectory());
    CHECK_FALSE(sampler::detail::moveBundleFolder(notBundle, root.getChildFile("d.sdaw"), false));
    CHECK(notBundle.exists());
    CHECK_FALSE(root.getChildFile("d.sdaw").exists());
}

TEST_CASE("Save As of an untitled project moves it, and refuses a folder inside the project", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-saveas");
    sampler::EngineHost host;
    juce::String error;
    auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
    REQUIRE(project != nullptr);
    const auto untitledFolder = project->bundleDir();

    CHECK_FALSE(project->saveAs(project->audioDir().getChildFile("inner"), error));
    CHECK(project->isUntitled());
    CHECK(project->bundleDir() == untitledFolder);
    CHECK_FALSE(project->audioDir().getChildFile("inner.sdaw").exists());

    REQUIRE(project->saveAs(root.getChildFile("Saved"), error));
    CHECK_FALSE(project->isUntitled());
    CHECK_FALSE(untitledFolder.exists());
    CHECK(root.getChildFile("Saved.sdaw").getChildFile(sampler::ProjectEdit::editFileName).existsAsFile());
}

TEST_CASE("audio-ready fires once an output device is usable", "[engine][project][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::EngineHost host;
    int calls = 0;
    host.onAudioReady = [&calls] { ++calls; };

    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (calls == 0 && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    if (calls == 0)
        SKIP("no audio output device");
    CHECK(host.isAudioReady());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    CHECK(calls == 1); // once, not repeatedly
}

TEST_CASE("the developer start delay holds back audio-ready", "[engine][project][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::EngineHost host;
    host.holdAudioReadyFor(2500);
    const auto start = juce::Time::getMillisecondCounter();
    bool fired = false;
    host.onAudioReady = [&fired] { fired = true; };

    const auto end = start + 15000;
    while (! fired && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    if (! fired)
        SKIP("no audio output device");
    CHECK(juce::Time::getMillisecondCounter() - start >= 2400);
}
