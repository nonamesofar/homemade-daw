// M3 2.1: the SLICESET of a sound: wrappers, split and merge rules, invariants and the saved form.
#include <catch2/catch_test_macros.hpp>

#include "model/SamplerState.h"

using namespace sampler::model;

namespace
{
constexpr juce::int64 frames = 470400;
constexpr double rate = 44100.0;

juce::ValueTree sourceNode()
{
    SourceData d;
    d.id = "src_1";
    d.name = "break";
    d.original = "audio/originals/break.wav";
    d.decoded = "audio/break.wav";
    d.hash = "sha256:00000001";
    d.sampleRate = rate;
    d.channels = 2;
    d.frames = frames;
    return Source::create(d);
}

SliceSettings settings(double pitch, double gain, bool reverse, PlayMode mode)
{
    SliceSettings s;
    s.pitch = pitch;
    s.gainDb = gain;
    s.reverse = reverse;
    s.playMode = mode;
    return s;
}
} // namespace

TEST_CASE("a sound that was never chopped reads as one slice with the defaults", "[model][slices]")
{
    Source source(sourceNode());
    CHECK_FALSE(source.hasSliceSet());
    const auto data = source.sliceSet();
    CHECK(data.numSlices() == 1);
    CHECK(data.markers.empty());
    CHECK(data.mode == SliceMode::manual);
    CHECK(data.slices.size() == 1);
    CHECK(data.slices[0] == SliceSettings{});
    CHECK(data.sensitivity == defaultSensitivity);
    CHECK_FALSE(findSliceSetViolation(data, frames, rate).has_value());
}

TEST_CASE("a slice set round-trips through the node and through saved XML with every attribute", "[model][slices]")
{
    SliceSetData data;
    data.mode = SliceMode::transient;
    data.division = SliceDivision::sixteenth;
    data.sensitivity = 63.5;
    data.markers = {29512, 58912, 88312};
    data.slices = {settings(0, 0, false, PlayMode::oneShot), settings(7, -6.5, true, PlayMode::gate), settings(-12, 3.0, false, PlayMode::loop),
                   settings(24, 24, true, PlayMode::oneShot)};

    auto node = sourceNode();
    Source source(node);
    source.setSliceSet(data, nullptr);
    CHECK(source.hasSliceSet());
    CHECK(source.sliceSet() == data);

    juce::ValueTree root(ids::SAMPLER);
    root.setProperty(ids::schemaVersion, currentSchemaVersion, nullptr);
    root.appendChild(node, nullptr);
    const auto xml = root.createXml();
    REQUIRE(xml != nullptr);
    const auto reopened = juce::ValueTree::fromXml(*xml);
    CHECK(Source(reopened.getChild(0)).sliceSet() == data);
    CHECK(xml->toString().contains("markers=\"29512 58912 88312\""));
}

TEST_CASE("adding a point splits a slice and both halves keep its settings", "[model][slices]")
{
    SliceSetData data;
    data.markers = {100000};
    data.slices = {settings(3, 0, false, PlayMode::oneShot), settings(5, -2, true, PlayMode::loop)};

    REQUIRE(insertSliceMarker(data, 50000, frames, rate));
    CHECK(data.markers == std::vector<juce::int64>{50000, 100000});
    REQUIRE(data.slices.size() == 3);
    CHECK(data.slices[0] == data.slices[1]);
    CHECK(data.slices[0].pitch == 3);
    CHECK(data.slices[2].pitch == 5);

    REQUIRE(insertSliceMarker(data, 200000, frames, rate)); // inside the last slice
    CHECK(data.slices[2] == data.slices[3]);
    CHECK(data.slices[3].playMode == PlayMode::loop);
    CHECK_FALSE(findSliceSetViolation(data, frames, rate).has_value());
}

TEST_CASE("removing a point merges the slices and keeps the left one's settings", "[model][slices]")
{
    SliceSetData data;
    data.markers = {1000, 5000, 9000};
    data.slices = {settings(1, 0, false, PlayMode::oneShot), settings(2, 0, false, PlayMode::gate), settings(3, 0, true, PlayMode::loop),
                   settings(4, 0, false, PlayMode::oneShot)};
    removeSliceMarker(data, 1); // between slice 1 and slice 2
    CHECK(data.markers == std::vector<juce::int64>{1000, 9000});
    REQUIRE(data.slices.size() == 3);
    CHECK(data.slices[1].pitch == 2);
    CHECK(data.slices[1].playMode == PlayMode::gate);
    CHECK(data.slices[2].pitch == 4);
    removeSliceMarker(data, 99); // no such point: nothing happens
    CHECK(data.markers.size() == 2);
}

TEST_CASE("points that would break the rules are refused", "[model][slices]")
{
    SliceSetData data;
    data.markers = {44100};
    data.normalise();
    const auto before = data;
    CHECK_FALSE(insertSliceMarker(data, 44100, frames, rate));       // already there
    CHECK_FALSE(insertSliceMarker(data, 0, frames, rate));           // the first slice starts at 0 anyway
    CHECK_FALSE(insertSliceMarker(data, frames, frames, rate));      // not inside the sound
    CHECK_FALSE(insertSliceMarker(data, 44100 + 100, frames, rate)); // 2.3 ms from its neighbour
    CHECK_FALSE(insertSliceMarker(data, 100, frames, rate));         // 2.3 ms from the start
    CHECK_FALSE(insertSliceMarker(data, frames - 100, frames, rate));
    CHECK(data == before);
    CHECK(insertSliceMarker(data, 44100 + 221, frames, rate)); // 5.01 ms away is allowed
}

TEST_CASE("a sound has at most 256 slices", "[model][slices]")
{
    SliceSetData data;
    for (int i = 1; i < maxSliceCount; ++i)
        data.markers.push_back(static_cast<juce::int64>(i) * 1000);
    data.normalise();
    CHECK(data.numSlices() == 256);
    CHECK_FALSE(findSliceSetViolation(data, frames, rate).has_value());
    CHECK_FALSE(insertSliceMarker(data, 255500, frames, rate));
    data.markers.push_back(300000);
    data.normalise();
    CHECK(findSliceSetViolation(data, frames, rate).has_value());
}

TEST_CASE("every violation of the slice rules is found", "[model][slices]")
{
    SliceSetData ok;
    ok.markers = {10000, 20000};
    ok.normalise();
    REQUIRE_FALSE(findSliceSetViolation(ok, frames, rate).has_value());

    auto d = ok;
    d.markers = {20000, 10000};
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // not increasing
    d.markers = {10000, 10000};
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // equal
    d.markers = {0, 10000};
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // 0 is not a point
    d.markers = {-5, 10000};
    CHECK(findSliceSetViolation(d, frames, rate).has_value());
    d.markers = {10000, 10100};
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // shorter than 5 ms
    d.markers = {10000, frames - 50};
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // last slice shorter than 5 ms
    d = ok;
    d.slices.pop_back();
    CHECK(findSliceSetViolation(d, frames, rate).has_value()); // a slice without settings
    d = ok;
    d.slices[1].pitch = 25.0;
    CHECK(findSliceSetViolation(d, frames, rate).has_value());
    d = ok;
    d.slices[1].gainDb = 30.0;
    CHECK(findSliceSetViolation(d, frames, rate).has_value());
    d = ok;
    d.slices[1].gainDb = silenceGainDb;
    CHECK_FALSE(findSliceSetViolation(d, frames, rate).has_value());
}

TEST_CASE("the enums have stable saved names", "[model][slices]")
{
    for (const auto m : {SliceMode::beat, SliceMode::transient, SliceMode::manual})
        CHECK(sliceModeFromString(toString(m)) == m);
    for (const auto d : {SliceDivision::quarter, SliceDivision::eighth, SliceDivision::sixteenth, SliceDivision::thirtySecond})
        CHECK(sliceDivisionFromString(toString(d)) == d);
    for (const auto p : {PlayMode::oneShot, PlayMode::gate, PlayMode::loop})
        CHECK(playModeFromString(toString(p)) == p);
    CHECK(toString(SliceDivision::sixteenth) == "1/16");
    CHECK(toString(PlayMode::oneShot) == "oneshot");
    CHECK(slicesPerBeat(SliceDivision::thirtySecond) == 8.0);
    CHECK(sliceModeFromString("nonsense") == SliceMode::manual);
}

TEST_CASE("slice points out of order or out of range are repaired when read, and still found as a violation as saved", "[model][slices]")
{
    Source source(sourceNode());
    SliceSetData data;
    data.markers = {1000, 2000};
    data.normalise();
    source.setSliceSet(data, nullptr);
    auto node = source.node().getChildWithName(sliceIds::SLICESET);
    node.setProperty(sliceIds::markers, "88312 29512 29512 0 -5 99999999", nullptr);

    const auto read = source.sliceSet();
    CHECK(read.markers == std::vector<juce::int64>{29512, 88312});
    CHECK(read.slices.size() == 3);
    CHECK_FALSE(findSliceSetViolation(read, frames, rate).has_value());
    CHECK(findSliceSetViolation(source.rawSliceSet(), frames, rate).has_value());

    node.setProperty(sliceIds::markers, "88312 29512", nullptr);
    CHECK(source.rawSliceSet().markers == std::vector<juce::int64>{88312, 29512});
    CHECK(source.sliceSet().markers == std::vector<juce::int64>{29512, 88312});
}

namespace
{
struct RemovalWatcher : juce::ValueTree::Listener
{
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override
    {
        if (child.hasType(sliceIds::SLICESET))
            ++sliceSetRemoved;
    }
    int sliceSetRemoved = 0;
};
} // namespace

TEST_CASE("rewriting a slice set keeps its node, so an observer never sees it missing, and is one undo step", "[model][slices]")
{
    auto tree = sourceNode();
    juce::UndoManager undo;
    Source source(tree);
    SliceSetData three;
    three.markers = {1000, 2000};
    three.normalise();
    three.slices[1].pitch = 3.0;
    source.setSliceSet(three, &undo);
    undo.beginNewTransaction();

    RemovalWatcher watcher;
    tree.addListener(&watcher);
    SliceSetData two;
    two.mode = SliceMode::beat;
    two.markers = {5000};
    two.normalise();
    two.slices[0].gainDb = -3.0;
    source.setSliceSet(two, &undo);
    undo.beginNewTransaction();
    CHECK(watcher.sliceSetRemoved == 0);
    CHECK(source.sliceSet() == two);

    SliceSetData five;
    five.markers = {100, 200, 300, 400};
    five.normalise();
    source.setSliceSet(five, &undo);
    CHECK(watcher.sliceSetRemoved == 0);
    CHECK(source.sliceSet() == five);

    undo.undo();
    CHECK(source.sliceSet() == two);
    undo.undo();
    CHECK(source.sliceSet() == three);
    tree.removeListener(&watcher);
}
