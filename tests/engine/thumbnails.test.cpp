// M1 D5: waveform pictures are generated data: they live only under cache/ and come back after cache/ is deleted.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_events/juce_events.h>

#include "TestAudio.h"
#include "engine/EngineHost.h"
#include "engine/ProjectEdit.h"

namespace
{
juce::StringArray listing(const juce::File& root)
{
    juce::StringArray paths;
    for (const auto& f : root.findChildFiles(juce::File::findFiles, true))
        paths.add(f.getRelativePathFrom(root).replaceCharacter('\\', '/'));
    paths.sort(false);
    return paths;
}

/** Builds the picture of `file` and waits until it is complete. */
bool buildPicture(sampler::EngineHost& host, const juce::File& file)
{
    juce::Component target;
    auto picture = host.createThumbnail(file, target);
    if (picture == nullptr)
        return false;
    const auto end = juce::Time::getMillisecondCounter() + 20000;
    while (! (picture->isFullyLoaded() && picture->getTotalLength() > 0.0) && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200); // let it write its cache file
    return picture->isFullyLoaded() && picture->getTotalLength() > 0.0;
}
} // namespace

TEST_CASE("thumbnail files appear only under cache/ and are rebuilt after cache/ is deleted", "[engine][thumbnails]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto root = sampler::test::tempDir("thumbs-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()));
    root.deleteRecursively();
    root.createDirectory();
    const auto bundle = root.getChildFile("Pics.sdaw");

    juce::String error;
    juce::File audio;
    {
        sampler::EngineHost host;
        auto project = sampler::ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        audio = project->audioDir().getChildFile("tone.wav");
        REQUIRE(sampler::test::writeToneWav(audio, 5.0).existsAsFile());
        const bool saved = project->saveAs(bundle, error);
        INFO(error);
        REQUIRE(saved);
        audio = project->audioDir().getChildFile("tone.wav");

        const auto before = listing(bundle);
        REQUIRE(buildPicture(host, audio));
        const auto after = listing(bundle);

        int added = 0;
        for (const auto& path : after)
            if (! before.contains(path))
            {
                ++added;
                CHECK(path.startsWith("cache/")); // nothing new anywhere else in the project
            }
        CHECK(added > 0); // the picture really was stored, so the check above means something
    }

    // Delete cache/ with the app closed (a new engine, as after a restart), reopen, and the pictures come back.
    bundle.getChildFile("cache").deleteRecursively();
    sampler::EngineHost host;
    auto reopened = sampler::ProjectEdit::open(host, bundle, error);
    REQUIRE(reopened != nullptr);
    REQUIRE(reopened->cacheDir().isDirectory());
    const auto emptyCache = listing(bundle);

    REQUIRE(buildPicture(host, reopened->audioDir().getChildFile("tone.wav")));
    int rebuilt = 0;
    for (const auto& path : listing(bundle))
        if (! emptyCache.contains(path))
        {
            ++rebuilt;
            CHECK(path.startsWith("cache/"));
        }
    CHECK(rebuilt > 0);
    root.deleteRecursively();
}
