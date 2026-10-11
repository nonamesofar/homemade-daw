#include <catch2/catch_test_macros.hpp>

#include "model/SamplerState.h"

using namespace sampler::model;

namespace
{
SourceData sample(const juce::String& hash, const juce::String& id)
{
    SourceData d;
    d.id = id;
    d.name = "kick";
    d.original = "audio/originals/kick-" + hash.substring(7, 15) + ".wav";
    d.decoded = "audio/kick-" + hash.substring(7, 15) + ".wav";
    d.originalPath = "C:/samples/kick.wav";
    d.hash = hash;
    d.sampleRate = 48000.0;
    d.channels = 2;
    d.frames = 96000;
    d.category = Category::oneShot;
    return d;
}
} // namespace

TEST_CASE("a new SAMPLER node has schema version 1 and no sources", "[model]")
{
    SamplerState state(SamplerState::createEmpty());
    CHECK(state.isValid());
    CHECK(state.schemaVersion() == 1);
    CHECK(currentSchemaVersion == 1);
    CHECK(state.numSources() == 0);
    CHECK_FALSE(state.sourceAt(0).isValid());
}

TEST_CASE("a Source wrapper reads back what create wrote", "[model]")
{
    const auto data = sample("sha256:0123456789abcdef", "src_01234567");
    Source source(Source::create(data));
    REQUIRE(source.isValid());
    CHECK(source.id() == data.id);
    CHECK(source.name() == "kick");
    CHECK(source.original() == data.original);
    CHECK(source.decoded() == data.decoded);
    CHECK(source.originalPath() == data.originalPath);
    CHECK(source.hash() == data.hash);
    CHECK(source.sampleRate() == 48000.0);
    CHECK(source.channels() == 2);
    CHECK(source.frames() == 96000);
    CHECK(source.category() == Category::oneShot);
    CHECK(source.lengthSeconds() == 2.0);
}

TEST_CASE("a Source with missing properties has safe defaults", "[model]")
{
    Source source{juce::ValueTree(ids::SOURCE)};
    CHECK(source.isValid());
    CHECK(source.id().isEmpty());
    CHECK(source.sampleRate() == 0.0);
    CHECK(source.frames() == 0);
    CHECK(source.lengthSeconds() == 0.0);
    CHECK(source.category() == Category::loop);
    CHECK_FALSE(Source().isValid());
    CHECK_FALSE(Source(juce::ValueTree("OTHER")).isValid());
}

TEST_CASE("categories round trip through their stored text", "[model]")
{
    for (auto category : {Category::loop, Category::oneShot, Category::recording})
        CHECK(categoryFromString(toString(category)) == category);
    CHECK(categoryFromString("nonsense") == Category::loop);
    CHECK(categoryFromString("nonsense", Category::recording) == Category::recording);
}

TEST_CASE("setCategory goes through the undo manager", "[model]")
{
    juce::UndoManager undo;
    Source source(Source::create(sample("sha256:aaaaaaaaaaaaaaaa", "src_aaaaaaaa")));
    undo.beginNewTransaction();
    source.setCategory(Category::loop, &undo);
    CHECK(source.category() == Category::loop);
    undo.undo();
    CHECK(source.category() == Category::oneShot);
}

TEST_CASE("sources are added, found and refused when the id is taken", "[model]")
{
    SamplerState state(SamplerState::createEmpty());
    CHECK(state.addSource(Source::create(sample("sha256:aaaaaaaa11111111", "src_aaaaaaaa")), nullptr));
    CHECK(state.addSource(Source::create(sample("sha256:bbbbbbbb22222222", "src_bbbbbbbb")), nullptr));
    CHECK_FALSE(state.addSource(Source::create(sample("sha256:cccccccc33333333", "src_aaaaaaaa")), nullptr));
    CHECK_FALSE(state.addSource(Source::create(sample("sha256:dddddddd44444444", "")), nullptr));

    CHECK(state.numSources() == 2);
    CHECK(state.sourceAt(1).id() == "src_bbbbbbbb");
    CHECK(state.findSourceById("src_aaaaaaaa").hash() == "sha256:aaaaaaaa11111111");
    CHECK(state.findSourceByHash("sha256:bbbbbbbb22222222").id() == "src_bbbbbbbb");
    CHECK_FALSE(state.findSourceById("src_zzzzzzzz").isValid());
    CHECK_FALSE(state.findSourceByHash("sha256:none").isValid());
}

TEST_CASE("source ids come from the hash and grow on a collision", "[model]")
{
    SamplerState state(SamplerState::createEmpty());
    const juce::String first = "sha256:0123456789abcdef0000";
    // Same first 8 hex characters, different content.
    const juce::String second = "sha256:01234567ffffffff0000";
    const juce::String third = "sha256:012345678fffffff0000";

    const auto firstId = state.makeSourceId(first);
    CHECK(firstId == "src_01234567");
    REQUIRE(state.addSource(Source::create(sample(first, firstId)), nullptr));

    // The same hash again gets the same id (the importer treats it as a duplicate before this point).
    CHECK(state.makeSourceId(first) == firstId);

    const auto secondId = state.makeSourceId(second);
    CHECK(secondId == "src_01234567f");
    REQUIRE(state.addSource(Source::create(sample(second, secondId)), nullptr));
    CHECK(secondId.startsWith("src_01234567"));
    CHECK(secondId != firstId);

    const auto thirdId = state.makeSourceId(third);
    CHECK(thirdId != firstId);
    CHECK(thirdId != secondId);
    CHECK(thirdId.startsWith("src_012345678"));
}

TEST_CASE("the id rule falls back to numbering when every prefix is taken", "[model]")
{
    const auto id = makeSourceId("sha256:abcd", [](const juce::String& candidate) { return candidate == "src_abcd"; });
    CHECK(id == "src_abcd_2");
}
