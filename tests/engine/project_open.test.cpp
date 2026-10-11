// M1 D3: open, save, save as and discard of project bundles.
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "commands/CommandService.h"
#include "commands/Commands.h"
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

sampler::model::SourceData soundData(int n)
{
    sampler::model::SourceData d;
    d.hash = "sha256:" + juce::String::toHexString(n).paddedLeft('0', 16);
    d.id = "src_" + d.hash.getLastCharacters(8);
    d.name = "sound" + juce::String(n);
    d.original = "audio/originals/sound" + juce::String(n) + ".wav";
    d.decoded = "audio/sound" + juce::String(n) + ".wav";
    d.sampleRate = 44100.0;
    d.channels = 2;
    d.frames = 1000;
    return d;
}

/** Adds `count` sounds (with small stand-in files on disk), each as its own undo step. */
void addSounds(sampler::ProjectEdit& project, int count, int firstNumber = 1)
{
    sampler::model::SamplerState state(project.samplerState());
    for (int i = 0; i < count; ++i)
    {
        const auto data = soundData(firstNumber + i);
        project.bundleDir().getChildFile(data.original).replaceWithText("original " + data.name);
        project.bundleDir().getChildFile(data.decoded).replaceWithText("decoded " + data.name);
        project.undoManager().beginNewTransaction("Add " + data.name);
        state.addSource(sampler::model::Source::create(data), &project.undoManager());
    }
}

juce::String hashOf(const juce::File& f) { return juce::SHA256(f).toHexString(); }
} // namespace

TEST_CASE("a saved project reopens with its three sounds", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-reopen");
    sampler::EngineHost host;
    juce::String error;
    const auto target = root.getChildFile("Beat1.sdaw");
    {
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        addSounds(*project, 3);
        REQUIRE(project->saveAs(target, error));
        CHECK(project->name() == "Beat1");
        CHECK_FALSE(project->isUntitled());
        CHECK_FALSE(project->isDirty());
    }
    CHECK(root.getChildFile("untitled").getNumberOfChildFiles(juce::File::findDirectories) == 0); // moved, not copied

    auto reopened = sampler::ProjectEdit::open(host, target, error);
    REQUIRE(reopened != nullptr);
    sampler::model::SamplerState state(reopened->samplerState());
    REQUIRE(state.numSources() == 3);
    CHECK(state.sourceAt(2).name() == "sound3");
    CHECK(state.schemaVersion() == 1);
    CHECK_FALSE(reopened->isDirty());
    CHECK_FALSE(reopened->undoManager().canUndo()); // the history is not saved
    CHECK(reopened->name() == "Beat1");
    CHECK(target.getChildFile("audio/sound1.wav").existsAsFile());
    CHECK(target.getChildFile("cache").isDirectory());
}

TEST_CASE("a category changed by the user survives save and reopen", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-category");
    sampler::EngineHost host;
    juce::String error;
    const auto target = root.getChildFile("Cats.sdaw");
    {
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        addSounds(*project, 2);
        sampler::commands::CommandService service(project->samplerState(), project->undoManager(), project->bundleDir());
        service.apply(std::make_unique<sampler::commands::SetSourceCategory>("src_00000002", sampler::model::Category::oneShot));
        REQUIRE(project->saveAs(target, error));
    }
    auto reopened = sampler::ProjectEdit::open(host, target, error);
    REQUIRE(reopened != nullptr);
    sampler::model::SamplerState state(reopened->samplerState());
    CHECK(state.findSourceById("src_00000001").category() == sampler::model::Category::loop);
    CHECK(state.findSourceById("src_00000002").category() == sampler::model::Category::oneShot);
}

TEST_CASE("an edit made before M1 (no SAMPLER section) opens with an empty one", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-m0");
    sampler::EngineHost host;
    juce::String error;
    const auto bundle = root.getChildFile("Old.sdaw");
    {
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        REQUIRE(project->saveAs(bundle, error));
    }
    // Strip the section from the file, as an M0 test edit would have it.
    auto xml = juce::XmlDocument::parse(bundle.getChildFile("project.tracktionedit"));
    REQUIRE(xml != nullptr);
    xml->removeChildElement(xml->getChildByName("SAMPLER"), true);
    REQUIRE(xml->writeTo(bundle.getChildFile("project.tracktionedit")));

    auto project = sampler::ProjectEdit::open(host, bundle, error);
    REQUIRE(project != nullptr);
    sampler::model::SamplerState state(project->samplerState());
    CHECK(state.isValid());
    CHECK(state.schemaVersion() == 1);
    CHECK(state.numSources() == 0);
}

TEST_CASE("a project from a newer version is refused and its file is not touched", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-newer");
    sampler::EngineHost host;
    juce::String error;
    const auto bundle = root.getChildFile("Future.sdaw");
    {
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project->saveAs(bundle, error));
    }
    const auto file = bundle.getChildFile("project.tracktionedit");
    auto xml = juce::XmlDocument::parse(file);
    xml->getChildByName("SAMPLER")->setAttribute("schemaVersion", 99);
    REQUIRE(xml->writeTo(file));
    const auto before = hashOf(file);

    CHECK(sampler::ProjectEdit::open(host, bundle, error) == nullptr);
    CHECK(error.contains("newer version"));
    CHECK(hashOf(file) == before);
}

TEST_CASE("folders that are not projects are reported", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-bad");
    sampler::EngineHost host;
    juce::String error;

    const auto empty = root.getChildFile("Empty.sdaw");
    empty.createDirectory();
    CHECK(sampler::ProjectEdit::open(host, empty, error) == nullptr);
    CHECK(error.contains("is not a Sampler project"));

    const auto broken = root.getChildFile("Broken.sdaw");
    broken.createDirectory();
    broken.getChildFile("project.tracktionedit").replaceWithText("this is not xml <<<");
    error = {};
    CHECK(sampler::ProjectEdit::open(host, broken, error) == nullptr);
    CHECK(error.contains("damaged"));

    CHECK(sampler::ProjectEdit::open(host, root.getChildFile("Missing.sdaw"), error) == nullptr);
}

TEST_CASE("Save As makes an independent copy and keeps undo", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-saveas");
    sampler::EngineHost host;
    juce::String error;
    const auto beat1 = root.getChildFile("Beat1.sdaw");
    const auto beat2 = root.getChildFile("Beat2.sdaw");

    auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
    addSounds(*project, 2);
    REQUIRE(project->saveAs(beat1, error));
    const auto hash1 = hashOf(beat1.getChildFile("audio/originals/sound1.wav"));

    REQUIRE(project->saveAs(beat2, error)); // a saved project is copied
    CHECK(project->name() == "Beat2");
    CHECK(beat1.isDirectory());
    CHECK(hashOf(beat2.getChildFile("audio/originals/sound1.wav")) == hash1);
    CHECK(hashOf(beat1.getChildFile("audio/originals/sound1.wav")) == hash1); // the first copy is unchanged
    CHECK(project->undoManager().canUndo());                                  // undo history survives

    // Importing after Save As changes only the new copy.
    addSounds(*project, 1, 3);
    REQUIRE(project->save());
    project.reset();
    auto one = sampler::ProjectEdit::open(host, beat1, error);
    REQUIRE(one != nullptr);
    CHECK(sampler::model::SamplerState(one->samplerState()).numSources() == 2);
    one.reset();
    auto two = sampler::ProjectEdit::open(host, beat2, error);
    REQUIRE(two != nullptr);
    CHECK(sampler::model::SamplerState(two->samplerState()).numSources() == 3);
    two.reset();

    // An existing folder is never overwritten.
    auto third = sampler::ProjectEdit::open(host, beat1, error);
    REQUIRE(third != nullptr);
    CHECK_FALSE(third->saveAs(beat2, error));
    CHECK(error.contains("already exists"));
}

TEST_CASE("Save As of an untitled project keeps the sounds, the history and a clean state", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-untitled-move");
    sampler::EngineHost host;
    juce::String error;
    auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
    addSounds(*project, 2);
    CHECK(project->isDirty());

    REQUIRE(project->saveAs(root.getChildFile("First"), error)); // ".sdaw" is added
    CHECK(project->bundleDir() == root.getChildFile("First.sdaw"));
    CHECK(project->editFile().existsAsFile());
    CHECK(project->bundleDir().getChildFile("audio/sound2.wav").existsAsFile());
    CHECK_FALSE(project->isDirty());
    CHECK(project->undoManager().canUndo());

    project->undoManager().undo(); // undo after save is an unsaved change
    CHECK(project->isDirty());
}

TEST_CASE("discarding an untitled project deletes its folder, a saved one is kept", "[engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = freshRoot("project-discard");
    sampler::EngineHost host;
    juce::String error;
    auto untitled = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
    const auto folder = untitled->bundleDir();
    REQUIRE(folder.isDirectory());
    untitled->discardUntitled();
    CHECK_FALSE(folder.exists());

    auto saved = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
    REQUIRE(saved->saveAs(root.getChildFile("Keep.sdaw"), error));
    saved->discardUntitled();
    CHECK(root.getChildFile("Keep.sdaw").isDirectory());
}
