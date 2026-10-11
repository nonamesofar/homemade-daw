// M1: audition. The queue rules are plain logic; playback needs an output device (hardware label).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/Audition.h"
#include "engine/AuditionRequestQueue.h"
#include "engine/EngineHost.h"
#include "engine/ProjectEdit.h"

TEST_CASE("a click before the audio is ready is remembered and played when it is ready", "[audition][queue]")
{
    sampler::AuditionRequestQueue queue;
    CHECK_FALSE(queue.hasPending());
    CHECK_FALSE(queue.takeForPlayback(0).has_value());

    queue.click("a", 1000);
    CHECK(queue.hasPending());
    CHECK(queue.pendingId() == "a");
    const auto played = queue.takeForPlayback(3000);
    REQUIRE(played.has_value());
    CHECK(*played == "a");
    CHECK_FALSE(queue.hasPending());
}

TEST_CASE("the latest click wins", "[audition][queue]")
{
    sampler::AuditionRequestQueue queue;
    queue.click("a", 100);
    queue.click("b", 200);
    CHECK(queue.pendingId() == "b");
    CHECK(*queue.takeForPlayback(300) == "b");
}

TEST_CASE("a second click on the same row cancels the request", "[audition][queue]")
{
    sampler::AuditionRequestQueue queue;
    queue.click("a", 100);
    queue.click("a", 200);
    CHECK_FALSE(queue.hasPending());
    CHECK_FALSE(queue.takeForPlayback(300).has_value());

    // And a third click asks again.
    queue.click("a", 400);
    CHECK(queue.hasPending());
}

TEST_CASE("a request that waits longer than 10 seconds is dropped", "[audition][queue]")
{
    sampler::AuditionRequestQueue queue;
    queue.click("a", 1000);
    CHECK_FALSE(queue.dropIfExpired(10999));
    CHECK(queue.hasPending());
    CHECK(queue.dropIfExpired(11001));
    CHECK_FALSE(queue.hasPending());
    CHECK_FALSE(queue.dropIfExpired(20000)); // nothing left to drop

    // Becoming ready after the timeout plays nothing.
    queue.click("b", 0);
    CHECK_FALSE(queue.takeForPlayback(10001).has_value());
}

TEST_CASE("audition plays a sound to its end while a project edit is attached and leaves the project alone",
          "[audition][engine][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("audition");
    const auto tone = sampler::test::writeToneWav(dir.getChildFile("tone.wav"), 0.6);
    auto root = dir.getChildFile("projects");
    root.deleteRecursively();

    sampler::EngineHost host;
    bool ready = false;
    host.onAudioReady = [&ready] { ready = true; };
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! ready && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    if (! ready)
        SKIP("no audio output device");

    const bool withProject = GENERATE(false, true);
    INFO("with project edit attached: " << withProject);
    juce::String error;
    std::unique_ptr<sampler::ProjectEdit> project;
    if (withProject)
    {
        project = sampler::ProjectEdit::createUntitled(host, root, error);
        REQUIRE(project != nullptr);
        project->prepareForPlayback(); // the project edit has its own playback context while the preview plays
    }
    const auto undoBefore = project != nullptr ? project->undoManager().getUndoDescriptions().size() : 0;

    sampler::Audition audition(host);
    REQUIRE(audition.play(tone, "tone"));
    CHECK(audition.playingId() == "tone");
    CHECK(audition.isPlaying());

    // It plays in real time: the position moves, and it stops by itself at the end.
    double furthest = 0.0;
    bool finished = false;
    const auto playEnd = juce::Time::getMillisecondCounter() + 6000;
    while (juce::Time::getMillisecondCounter() < playEnd)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        furthest = std::max(furthest, audition.positionSeconds());
        if (audition.poll())
        {
            finished = true;
            break;
        }
    }
    CHECK(finished);
    CHECK(furthest > 0.2);
    CHECK_FALSE(audition.isPlaying());

    // Auditioning did not touch the project.
    if (project != nullptr)
    {
        CHECK_FALSE(project->isDirty());
        CHECK(project->undoManager().getUndoDescriptions().size() == undoBefore);
    }
}

TEST_CASE("starting another audition replaces the first and stop silences it", "[audition][engine][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto dir = sampler::test::tempDir("audition2");
    const auto first = sampler::test::writeToneWav(dir.getChildFile("first.wav"), 2.0);
    const auto second = sampler::test::writeToneWav(dir.getChildFile("second.wav"), 2.0);

    sampler::EngineHost host;
    bool ready = false;
    host.onAudioReady = [&ready] { ready = true; };
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! ready && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    if (! ready)
        SKIP("no audio output device");

    sampler::Audition audition(host);
    REQUIRE(audition.play(first, "first"));
    REQUIRE(audition.play(second, "second"));
    CHECK(audition.playingId() == "second");
    audition.stop();
    CHECK(audition.playingId().isEmpty());
    CHECK_FALSE(audition.poll());

    CHECK_FALSE(audition.play(dir.getChildFile("missing.wav"), "missing"));
    CHECK(audition.playingId().isEmpty());
}
