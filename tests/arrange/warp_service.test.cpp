// M4a task 1.6: WarpService makes the Beats renders in the background and the offline render waits for them.
#include "WarpTestSupport.h"

#include <catch2/catch_approx.hpp>

#include <chrono>

using Catch::Approx;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;
using namespace sampler::test;

namespace
{
WarpSettings beatsAt(double bpm, double transpose = 0.0)
{
    WarpSettings w;
    w.on = true;
    w.mode = WarpMode::beats;
    w.sourceBpm = bpm;
    w.transpose = transpose;
    return w;
}
} // namespace

TEST_CASE("a new Beats clip is rendering until the service has made its file, then it is not", "[arrange][warp][service]")
{
    ArrangeFixture f("service-basic");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, beatsAt(90.0)));
    CHECK(f.arrangement().clip(clipId)->rendering);
    CHECK_FALSE((f.project->warpService().isIdle() && numBeatsFiles(f) > 0));

    // The arrangement change reaches the service on the next message: it queues the job, then the file appears.
    ArrangeFixture::pump(30);
    CHECK_FALSE(f.project->warpService().isIdle());
    REQUIRE(waitForBeatsRenders(f));
    CHECK(f.project->warpService().isIdle());
    CHECK(f.project->warpService().numFailures() == 0);
    CHECK_FALSE(f.arrangement().clip(clipId)->rendering);
    CHECK(numBeatsFiles(f) == 1);
    CHECK(f.project->bundleDir().getChildFile("cache/beats").findChildFiles(juce::File::findFiles, false, "*.tmp").isEmpty());
}

TEST_CASE("a project whose cache folder was deleted renders the same WAV as with the cache", "[arrange][warp][service]")
{
    ArrangeFixture f("service-cache-deleted");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    const auto clipId = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, beatsAt(90.0, 0.0)));
    const auto second = f.arrangement().createClip(f.trackId(0), specFor(f, sound, 16.0, beatsAt(90.0, 3.0)));
    REQUIRE(f.project->save());
    const auto withCache = renderProject(f, "with-cache");
    REQUIRE(withCache.getNumSamples() > 0);
    CHECK(numBeatsFiles(f) == 2);
    CHECK(lastAbove(withCache, 0.01f) / 48000.0 > 15.0); // both clips are heard (bars 1 to 8)

    const auto bundle = f.project->bundleDir();
    f.commands.reset();
    f.project.reset();
    // The engine may hold a file of the closed project for a moment.
    bool deleted = false;
    for (int attempt = 0; attempt < 40 && ! deleted; ++attempt)
    {
        deleted = bundle.getChildFile("cache").deleteRecursively();
        if (! deleted)
            juce::Thread::sleep(50);
    }
    if (! deleted)
        for (const auto& left : bundle.getChildFile("cache").findChildFiles(juce::File::findFilesAndDirectories, true))
            std::printf("left in cache: %s\n", left.getFullPathName().toRawUTF8());
    REQUIRE(deleted);

    juce::String error;
    f.project = sampler::ProjectEdit::open(f.host, bundle, error);
    REQUIRE(f.project != nullptr);
    f.makeCommands();
    CHECK(f.project->missingFiles().isEmpty()); // no file is reported missing
    for (const auto& clip : f.arrangement().clips())
    {
        CHECK_FALSE(clip.missing);
        CHECK(clip.rendering); // until the service has made it
    }
    CHECK(f.arrangement().clip(clipId).has_value());
    CHECK(f.arrangement().clip(second).has_value());

    const auto withoutCache = renderProject(f, "without-cache"); // waits for the renders
    REQUIRE(withoutCache.getNumSamples() == withCache.getNumSamples());
    float worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < withCache.getNumSamples(); ++i)
            worst = std::max(worst, std::abs(withCache.getSample(c, i) - withoutCache.getSample(c, i)));
    CHECK(worst == 0.0f);
    CHECK(numBeatsFiles(f) == 2);
}

TEST_CASE("a copy made by Save As gets its Beats renders made again", "[arrange][warp][service]")
{
    ArrangeFixture f("service-save-as");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, beatsAt(90.0)));
    REQUIRE(f.project->save());
    REQUIRE(waitForBeatsRenders(f));
    CHECK(numBeatsFiles(f) == 1);

    juce::String error;
    const auto copy = f.root.getChildFile("copy.sdaw");
    REQUIRE(f.project->saveAs(copy, error));
    REQUIRE(waitForBeatsRenders(f));
    CHECK(copy.getChildFile("cache/beats").findChildFiles(juce::File::findFiles, false, "*.wav").size() == 1);
    CHECK(f.arrangement().allRendersReady());
}

TEST_CASE("a Beats clip whose sound cannot be read fails once and offline render reports it", "[arrange][warp][service]")
{
    ArrangeFixture f("service-failure");
    const auto sound = addFixtureSound(f, "break-90.wav");
    f.project->undoManager().beginNewTransaction("create");
    f.arrangement().createClip(f.trackId(0), specFor(f, sound, 0.0, beatsAt(90.0)));
    // A sound that is not audio: the decoded file is there but unreadable.
    const auto decoded = f.project->bundleDir().getChildFile("audio/break-90.wav");
    decoded.replaceWithText("not a wav file");
    ArrangeFixture::pump(300);
    auto& service = f.project->warpService();
    const auto end = juce::Time::getMillisecondCounter() + 10000;
    while (! (service.isIdle() && service.numFailures() > 0) && juce::Time::getMillisecondCounter() < end)
        ArrangeFixture::pump(20);
    CHECK(service.numFailures() == 1);
    CHECK(service.lastError().isNotEmpty());

    juce::String error;
    CHECK_FALSE(sampler::engine::renderToWav(*f.project, f.root.getChildFile("fail.wav"), {}, error));
    CHECK(error.contains("stretched version"));
}

TEST_CASE("sixteen 4-bar Beats clips are all ready soon after a tempo change", "[arrange][warp][service][perf]")
{
    ArrangeFixture f("service-sixteen");
    // Sixteen different sounds (so sixteen different renders), each on its own track.
    std::vector<juce::String> sounds;
    const auto source = juce::File(SAMPLER_FIXTURES_DIR).getChildFile("break-90.wav");
    for (int i = 0; i < 16; ++i)
    {
        const auto base = "break-" + juce::String(i);
        const auto decoded = "audio/" + base + ".wav";
        f.project->bundleDir().getChildFile("audio/originals").createDirectory();
        REQUIRE(source.copyFileTo(f.project->bundleDir().getChildFile(decoded)));
        source.copyFileTo(f.project->bundleDir().getChildFile("audio/originals/" + base + ".wav"));
        sampler::model::SourceData data;
        data.hash = "sha256:" + juce::String(i).paddedLeft('0', 16);
        data.id = "src_" + base;
        data.name = base;
        data.original = "audio/originals/" + base + ".wav";
        data.decoded = decoded;
        data.sampleRate = 44100.0;
        data.channels = 2;
        data.frames = 470400;
        f.commands->apply(std::make_unique<sampler::commands::AddSources>(std::vector<sampler::model::SourceData>{data}));
        sounds.push_back(data.id);
    }
    for (int i = 0; i < 16; ++i)
    {
        f.commands->apply(std::make_unique<sampler::commands::AddTrack>());
        f.project->undoManager().beginNewTransaction("clip");
        f.arrangement().createClip(f.trackId(i + 1), specFor(f, sounds[static_cast<size_t>(i)], 0.0, beatsAt(90.0)));
    }
    const bool first = waitForBeatsRenders(f, 60000);
    INFO("files " << numBeatsFiles(f) << ", failures " << f.project->warpService().numFailures() << ": " << f.project->warpService().lastError()
         << ", idle " << (f.project->warpService().isIdle() ? 1 : 0) << ", ready " << (f.arrangement().allRendersReady() ? 1 : 0));
    REQUIRE(first);
    CHECK(numBeatsFiles(f) == 16);

    f.commands->apply(std::make_unique<sampler::commands::SetTempo>(121.0));
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(waitForBeatsRenders(f));
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    INFO("sixteen renders took " << seconds << " s");
    CHECK(numBeatsFiles(f) == 32);
    CHECK(f.project->warpService().numFailures() == 0);
#if JUCE_DEBUG
    CHECK(seconds < 5.0);
#else
    CHECK(seconds < 0.5);
#endif
}
