// M2 D1: the arrangement facade over the edit (tasks 1.1, 1.2), dirty state from the history (1.6) and projects without
// tracks (1.7).
#include "ArrangeFixture.h"

#include <catch2/catch_approx.hpp>

#include <juce_cryptography/juce_cryptography.h>

using Catch::Approx;
using sampler::test::ArrangeFixture;

TEST_CASE("a new project has one empty track and the default tempo, signature and no loop", "[arrange][engine]")
{
    ArrangeFixture f("arr-new");
    auto& a = f.arrangement();
    REQUIRE(a.numTracks() == 1);
    CHECK(a.tracks()[0].name == "Audio 1");
    CHECK(a.tracks()[0].colour == sampler::engine::trackPaletteColour(0));
    CHECK(a.tracks()[0].volumeDb == Approx(0.0).margin(0.01));
    CHECK(a.tracks()[0].pan == Approx(0.0));
    CHECK(a.numClips() == 0);
    CHECK(a.tempo() == Approx(120.0));
    CHECK(a.timeSig().numerator == 4);
    CHECK(a.timeSig().denominator == 4);
    CHECK(a.loopRange().isEmpty());
    CHECK(a.masterVolumeDb() == Approx(0.0).margin(0.01));
    CHECK_FALSE(f.project->undoManager().canUndo()); // the default track is not an undo step
}

TEST_CASE("tracks and clips read back as plain snapshots, and the change callback fires once per batch",
          "[arrange][engine]")
{
    ArrangeFixture f("arr-snapshot");
    auto& a = f.arrangement();
    const auto source = f.addSound("loop", 4.0);

    int changes = 0;
    struct Counter : juce::ChangeListener
    {
        explicit Counter(int& n) : count(n) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
        int& count;
    } counter(changes);
    a.addChangeListener(&counter);
    ArrangeFixture::pump();
    changes = 0;
    const auto generationBefore = a.generation();

    // One batch: a track and a clip at beat 8 in one go.
    f.project->undoManager().beginNewTransaction("batch");
    const auto trackId = a.addTrack(1, "Bass", sampler::engine::trackPaletteColour(1));
    sampler::engine::ClipSpec spec;
    spec.sourceId = source;
    spec.name = "loop";
    spec.file = f.project->bundleDir().getChildFile("audio/loop.wav");
    spec.start = 8.0;
    spec.lengthSeconds = 4.0;
    const auto clipId = a.createClip(trackId, spec);
    REQUIRE(clipId.isNotEmpty());
    ArrangeFixture::pump();
    CHECK(changes == 1);
    CHECK(a.generation() == generationBefore + 1);
    a.removeChangeListener(&counter);

    REQUIRE(a.numTracks() == 2);
    CHECK(a.trackIndex(trackId) == 1);
    const auto clip = a.clip(clipId);
    REQUIRE(clip.has_value());
    CHECK(clip->trackId == trackId);
    CHECK(clip->sourceId == source);
    CHECK(clip->name == "loop");
    CHECK(clip->start == Approx(8.0));
    CHECK(clip->length == Approx(8.0)); // 4 s at 120 BPM
    CHECK(clip->startSeconds == Approx(4.0));
    CHECK(clip->sourceSeconds == Approx(4.0));
    CHECK_FALSE(clip->missing);
    CHECK(a.endBeat() == Approx(16.0));
    CHECK(a.beatsToSeconds(9.0) == Approx(4.5));
    CHECK(a.secondsToBeats(4.5) == Approx(9.0));

    // The clip refers to the sound by a path relative to the project file.
    REQUIRE(f.project->save());
    const auto text = f.project->editFile().loadFileAsString();
    CHECK(text.contains("sampler_sourceId=\"" + source + "\""));
    CHECK_FALSE(text.contains(f.project->bundleDir().getFullPathName()));

    f.project->undoManager().undo();
    CHECK(a.numTracks() == 1);
    CHECK(a.numClips() == 0);
}

TEST_CASE("Tracktion behaviour: a native-speed clip after a tempo change, and an undone loop range",
          "[arrange][engine][assumption]")
{
    SECTION("(a) the raw tempo change keeps the clip's start beat; SetTempo keeps its length in seconds")
    {
        ArrangeFixture f("arr-a-tempo");
        auto& a = f.arrangement();
        const auto source = f.addSound("one-bar", 2.0); // one bar at 120 BPM
        const auto clipId = f.place(source, 0, 8.0);
        REQUIRE(clipId.isNotEmpty());

        f.project->undoManager().beginNewTransaction("raw");
        a.setTempo(90.0);
        const auto raw = a.clip(clipId);
        REQUIRE(raw.has_value());
        std::printf("Task 1.2a: after 120 -> 90 BPM Tracktion gives start %.4f beats, length %.4f beats = %.4f s\n",
                    raw->start, raw->length, raw->lengthSeconds);
        CHECK(raw->start == Approx(8.0)); // M0 A1 still holds
        f.project->undoManager().undo();

        f.commands->apply(std::make_unique<sampler::commands::SetTempo>(90.0));
        const auto clip = a.clip(clipId);
        REQUIRE(clip.has_value());
        CHECK(clip->start == Approx(8.0));
        CHECK(clip->lengthSeconds == Approx(2.0)); // what the code relies on: native speed, length in seconds
        CHECK(clip->length == Approx(3.0));        // 2 s at 90 BPM
        CHECK(f.violations.isEmpty());
    }

    SECTION("(b) an undo of the loop range written through the edit's undo manager reaches the transport")
    {
        ArrangeFixture f("arr-b-loop");
        auto& a = f.arrangement();
        f.commands->apply(std::make_unique<sampler::commands::SetLoop>(sampler::engine::LoopRange{16.0, 32.0}));
        CHECK(a.loopRange().start == Approx(16.0));
        CHECK(a.loopRange().end == Approx(32.0));
        f.commands->apply(std::make_unique<sampler::commands::SetLoop>(sampler::engine::LoopRange{4.0, 8.0}));
        CHECK(a.loopRange().start == Approx(4.0));

        f.commands->undo();
        CHECK(a.loopRange().start == Approx(16.0)); // read back from Tracktion's transport
        CHECK(a.loopRange().end == Approx(32.0));
        f.commands->undo();
        CHECK(a.loopRange().isEmpty());
        f.commands->redo();
        CHECK(a.loopRange().end == Approx(32.0));
    }
}

TEST_CASE("dirty state follows the undo history: edits dirty, transport and view actions do not", "[arrange][engine][project]")
{
    ArrangeFixture f("arr-dirty");
    auto saved = f.commands->historyToken();
    const auto dirty = [&] { return f.commands->historyToken() != saved; };
    CHECK_FALSE(dirty());

    const auto source = f.addSound("kick", 0.5);
    REQUIRE(f.project->save());
    saved = f.commands->historyToken();
    CHECK_FALSE(dirty());

    // Play, locate, loop on, click on: no undo step, not dirty.
    auto& transport = f.project->transport();
    transport.locateBeats(8.0);
    transport.setLooping(true);
    transport.setClickOn(true);
    transport.play(); // headless: stays pending, never plays
    transport.stop();
    ArrangeFixture::pump();
    CHECK_FALSE(dirty());
    CHECK(transport.positionBeats() == Approx(8.0));

    // Adding a clip dirties; undo back to the saved point is clean again; undo past it dirties.
    f.place(source, 0, 0.0);
    CHECK(dirty());
    f.commands->undo();
    CHECK_FALSE(dirty());
    f.commands->undo(); // removes the sound, which was saved
    CHECK(dirty());
    f.commands->redo();
    CHECK_FALSE(dirty());
}

TEST_CASE("a project saved without tracks opens with one empty track, clean and with its file untouched",
          "[arrange][engine][project]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto root = sampler::test::freshTempDir("arr-m1-project");
    const auto bundle = root.getChildFile("Old.sdaw");
    sampler::EngineHost host("SamplerTest", true);
    {
        // Make an "M1" project: sounds, no audio tracks in the file.
        juce::String error;
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        REQUIRE(project->saveAs(bundle, error));
    }
    {
        auto xml = juce::XmlDocument::parse(bundle.getChildFile("project.tracktionedit"));
        REQUIRE(xml != nullptr);
        while (auto* track = xml->getChildByName("TRACK"))
            xml->removeChildElement(track, true);
        REQUIRE(xml->writeTo(bundle.getChildFile("project.tracktionedit")));
    }
    const auto hashBefore = juce::SHA256(bundle.getChildFile("project.tracktionedit")).toHexString();

    juce::String error;
    auto project = sampler::ProjectEdit::open(host, bundle, error);
    REQUIRE(project != nullptr);
    REQUIRE(project->arrangement().numTracks() == 1);
    CHECK(project->arrangement().tracks()[0].name == "Audio 1");
    CHECK_FALSE(project->undoManager().canUndo()); // so the project is not dirty
    sampler::commands::CommandService commands(project->samplerState(), project->undoManager(), project->bundleDir(),
                                               &project->arrangement());
    CHECK(commands.historyToken() == 0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(juce::SHA256(bundle.getChildFile("project.tracktionedit")).toHexString() == hashBefore);
}
