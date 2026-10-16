// M3 1.6: background analysis of a sound, its cache file, and that it never touches the project model.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "io/AnalysisService.h"
#include "model/SamplerState.h"

using namespace sampler;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

/** A throwaway bundle with `break-90.wav` copied in as the decoded file of one source. */
struct Bundle
{
    explicit Bundle(const juce::String& name, const char* fixtureName = "break-90.wav")
        : dir(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name + ".sdaw"))
    {
        dir.deleteRecursively();
        dir.getChildFile("audio").createDirectory();
        REQUIRE(fixture(fixtureName).copyFileTo(dir.getChildFile("audio/break-90-abcdef12.wav")));
        model::SourceData data;
        data.id = "src_abcdef12";
        data.name = "break-90";
        data.original = "audio/originals/break-90-abcdef12.wav";
        data.decoded = "audio/break-90-abcdef12.wav";
        data.hash = "sha256:abcdef12abcdef12abcdef12abcdef12abcdef12abcdef12abcdef12abcdef12";
        data.sampleRate = 44100.0;
        data.channels = 2;
        data.frames = 470400;
        source = model::Source(model::Source::create(data));
    }

    ~Bundle() { dir.deleteRecursively(); }

    void wait(io::AnalysisService& service, int timeoutMs = 20000) const
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (service.isBusy() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        REQUIRE_FALSE(service.isBusy());
    }

    juce::File dir;
    model::Source source;
};
} // namespace

TEST_CASE("analysing a sound writes the cache file and reports the tempo and the hits", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-basic");
    io::AnalysisService service([&b] { return b.dir; });
    juce::StringArray notified;
    service.onAnalysed = [&notified](const juce::String& id) { notified.add(id); };

    CHECK(service.find(b.source) == nullptr);
    service.request(b.source);
    CHECK(service.isAnalysing("src_abcdef12"));
    b.wait(service);

    CHECK(notified == juce::StringArray{"src_abcdef12"});
    CHECK_FALSE(service.isAnalysing("src_abcdef12"));
    const auto file = io::AnalysisService::cacheFile(b.dir, b.source);
    CHECK(file.getFullPathName().contains("cache"));
    CHECK(file.getFileName() == "abcdef12abcdef12.json");
    REQUIRE(file.existsAsFile());

    const auto result = service.find(b.source);
    REQUIRE(result != nullptr);
    CHECK(result->bpm() == Catch::Approx(90.0).margin(0.5));
    CHECK(result->onsets.size() == 16);
    CHECK(result->sampleRate == 44100.0);
    CHECK(result->frames == 470400);
    CHECK(service.computedCount() == 1);
}

TEST_CASE("a second request reads the cache and does not compute again", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-cached");
    {
        io::AnalysisService first([&b] { return b.dir; });
        first.request(b.source);
        b.wait(first);
        CHECK(first.computedCount() == 1);
        first.request(b.source); // already there
        CHECK_FALSE(first.isBusy());
        CHECK(first.computedCount() == 1);
    }
    io::AnalysisService second([&b] { return b.dir; }); // empty memory, same cache folder
    const auto result = second.find(b.source);
    REQUIRE(result != nullptr);
    CHECK(result->bpm() == Catch::Approx(90.0).margin(0.5));
    second.request(b.source);
    CHECK_FALSE(second.isBusy());
    CHECK(second.computedCount() == 0);
}

TEST_CASE("a cache file from another algorithm version is recomputed", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-stale");
    {
        io::AnalysisService first([&b] { return b.dir; });
        first.request(b.source);
        b.wait(first);
    }
    const auto file = io::AnalysisService::cacheFile(b.dir, b.source);
    auto json = juce::JSON::parse(file.loadFileAsString());
    REQUIRE(static_cast<int>(json["algorithmVersion"]) == io::analysisAlgorithmVersion);
    json.getDynamicObject()->setProperty("algorithmVersion", 0);
    REQUIRE(file.replaceWithText(juce::JSON::toString(json, true)));

    io::AnalysisService second([&b] { return b.dir; });
    CHECK(second.find(b.source) == nullptr);
    second.request(b.source);
    b.wait(second);
    CHECK(second.computedCount() == 1);
    const auto result = second.find(b.source);
    REQUIRE(result != nullptr);
    CHECK(result->algorithmVersion == io::analysisAlgorithmVersion);
    CHECK(static_cast<int>(juce::JSON::parse(file.loadFileAsString())["algorithmVersion"]) == io::analysisAlgorithmVersion);
}

TEST_CASE("a deleted cache folder is rebuilt", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-deleted");
    {
        io::AnalysisService first([&b] { return b.dir; });
        first.request(b.source);
        b.wait(first);
    }
    REQUIRE(b.dir.getChildFile("cache").deleteRecursively());

    io::AnalysisService second([&b] { return b.dir; });
    CHECK(second.find(b.source) == nullptr);
    second.request(b.source);
    b.wait(second);
    CHECK(second.computedCount() == 1);
    CHECK(io::AnalysisService::cacheFile(b.dir, b.source).existsAsFile());
}

TEST_CASE("a cache file that does not match the sound is ignored", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-mismatch");
    {
        io::AnalysisService first([&b] { return b.dir; });
        first.request(b.source);
        b.wait(first);
    }
    // The same cache file for a sound of another length (a re-recorded take with a reused key) is not trusted.
    auto node = b.source.node().createCopy();
    node.setProperty(model::ids::frames, 1000, nullptr);
    io::AnalysisService second([&b] { return b.dir; });
    CHECK(second.find(model::Source(node)) == nullptr);
}

TEST_CASE("analysis does not change the project model", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-model-clean");
    juce::ValueTree root(model::ids::SAMPLER);
    root.addChild(b.source.node(), -1, nullptr);
    struct Counter : juce::ValueTree::Listener
    {
        void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { ++changes; }
        void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { ++changes; }
        void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { ++changes; }
        int changes = 0;
    } counter;
    root.addListener(&counter);
    juce::UndoManager undo;
    undo.beginNewTransaction();

    io::AnalysisService service([&b] { return b.dir; });
    service.request(b.source);
    b.wait(service);
    CHECK(counter.changes == 0);
    CHECK_FALSE(undo.canUndo());
    root.removeListener(&counter);
}

TEST_CASE("a missing file gives no result and no notification", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-missing");
    REQUIRE(b.dir.getChildFile("audio/break-90-abcdef12.wav").deleteFile());
    io::AnalysisService service([&b] { return b.dir; });
    int notified = 0;
    service.onAnalysed = [&notified](const juce::String&) { ++notified; };
    service.request(b.source);
    b.wait(service);
    CHECK(notified == 0);
    CHECK(service.find(b.source) == nullptr);
}

TEST_CASE("a missing file is reported as failed once and not tried again until the file changes", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-missing-failed");
    const auto wav = b.dir.getChildFile("audio/break-90-abcdef12.wav");
    REQUIRE(wav.deleteFile());
    io::AnalysisService service([&b] { return b.dir; });
    int failures = 0;
    service.onFailed = [&failures](const juce::String&) { ++failures; };
    service.request(b.source);
    b.wait(service);
    CHECK(failures == 1);
    CHECK(service.hasFailed(b.source));
    CHECK(service.find(b.source) == nullptr);

    service.request(b.source); // does not start another job
    CHECK_FALSE(service.isBusy());

    REQUIRE(fixture("break-90.wav").copyFileTo(wav)); // the file is back: tried again
    CHECK_FALSE(service.hasFailed(b.source));
    service.request(b.source);
    b.wait(service);
    CHECK(service.find(b.source) != nullptr);
}

TEST_CASE("a file that is not the length the project records fails instead of being asked for again and again", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-mismatch");
    auto node = b.source.node();
    node.setProperty(model::ids::frames, 470401, nullptr);
    io::AnalysisService service([&b] { return b.dir; });
    int failures = 0, analysed = 0;
    service.onFailed = [&failures](const juce::String&) { ++failures; };
    service.onAnalysed = [&analysed](const juce::String&) { ++analysed; };
    service.request(b.source);
    b.wait(service);
    CHECK(failures == 1);
    CHECK(analysed == 0);
    CHECK(service.hasFailed(b.source));
    CHECK(service.find(b.source) == nullptr);
    service.request(b.source);
    CHECK_FALSE(service.isBusy());
}

TEST_CASE("a take without a content hash is keyed by its file", "[io][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Bundle b("analysis-take");
    auto node = b.source.node().createCopy();
    node.removeProperty(model::ids::hash, nullptr);
    const model::Source take(node);
    const auto key = io::AnalysisService::cacheKey(b.dir, take);
    CHECK(key.length() == 16);
    CHECK(key != "abcdef12abcdef12");
    CHECK(key == io::AnalysisService::cacheKey(b.dir, take)); // stable for the same file
}

TEST_CASE("the JSON form round-trips", "[io][analysis]")
{
    io::SoundAnalysis a;
    a.sampleRate = 48000.0;
    a.frames = 123456;
    a.firstBeatFrame = 4410;
    a.tempo = {{120.0, 0.6f}, {60.0, 0.3f}};
    a.onsets = {{100, 1.0f}, {5000, 0.25f}};
    io::SoundAnalysis b;
    REQUIRE(io::analysisFromJson(io::analysisToJson(a), b));
    CHECK(b.sampleRate == 48000.0);
    CHECK(b.frames == 123456);
    CHECK(b.firstBeatFrame == 4410);
    REQUIRE(b.tempo.size() == 2);
    CHECK(b.tempo[0].bpm == 120.0);
    REQUIRE(b.onsets.size() == 2);
    CHECK(b.onsets[1].frame == 5000);
    CHECK(b.onsets[1].strength == Catch::Approx(0.25f));
    CHECK_FALSE(io::analysisFromJson("not json", b));
}
