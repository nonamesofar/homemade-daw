// M3 3.5: pitch, gain, reverse and play mode of a slice.
#include <catch2/catch_test_macros.hpp>

#include "commands/CommandService.h"
#include "commands/Commands.h"
#include "commands/SliceCommands.h"

using namespace sampler;
using namespace sampler::commands;
using Param = SetSliceParam::Param;

namespace
{
model::SourceData source()
{
    model::SourceData d;
    d.hash = "sha256:0000000000000001";
    d.id = "src_00000001";
    d.name = "break";
    d.original = "audio/originals/break.wav";
    d.decoded = "audio/break.wav";
    d.sampleRate = 44100.0;
    d.channels = 2;
    d.frames = 470400;
    return d;
}

struct Fixture
{
    juce::File bundle = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/slice-params.sdaw");
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    CommandService service{tree, undo, bundle};
    model::SamplerState state{tree};
    juce::StringArray violations;
    juce::uint32 now = 1000;

    Fixture()
    {
        service.onInvariantViolation = [this](const juce::String& m) { violations.add(m); };
        service.clock = [this] { return now; };
        service.apply(std::make_unique<AddSources>(std::vector{source()}));
        for (const juce::int64 at : {50000, 100000, 150000})
            service.apply(std::make_unique<AddSlicePoint>("src_00000001", at));
        now += 1000;
    }

    model::SliceSettings slice(int i) const { return state.sourceAt(0).sliceSet().slices[static_cast<size_t>(i)]; }
    void set(int i, Param p, double v) { service.apply(std::make_unique<SetSliceParam>("src_00000001", i, p, v)); }
};
} // namespace

TEST_CASE("each setting has its own undo step name", "[commands][slices][params]")
{
    Fixture f;
    f.set(1, Param::pitch, 7);
    CHECK(f.service.undoName() == "Set slice pitch");
    f.now += 1000;
    f.set(1, Param::gain, -6);
    CHECK(f.service.undoName() == "Set slice gain");
    f.now += 1000;
    f.set(1, Param::reverse, 1);
    CHECK(f.service.undoName() == "Reverse slice");
    f.now += 1000;
    f.set(1, Param::playMode, 2);
    CHECK(f.service.undoName() == "Set play mode");

    const auto s = f.slice(1);
    CHECK(s.pitch == 7.0);
    CHECK(s.gainDb == -6.0);
    CHECK(s.reverse);
    CHECK(s.playMode == model::PlayMode::loop);
    CHECK(f.slice(0) == model::SliceSettings{});
    CHECK(f.violations.isEmpty());

    f.service.undo();
    CHECK(f.slice(1).playMode == model::PlayMode::oneShot);
    f.service.undo();
    CHECK_FALSE(f.slice(1).reverse);
}

TEST_CASE("two gain changes within 500 ms are one step, a later one is another", "[commands][slices][params]")
{
    Fixture f;
    f.set(2, Param::gain, -3);
    f.now += 200;
    f.set(2, Param::gain, -6);
    f.now += 200;
    f.set(2, Param::gain, -9);
    CHECK(f.slice(2).gainDb == -9.0);
    f.service.undo();
    CHECK(f.slice(2).gainDb == 0.0); // all three together

    f.service.redo();
    f.now += 800;
    f.set(2, Param::gain, -12);
    f.service.undo();
    CHECK(f.slice(2).gainDb == -9.0);
}

TEST_CASE("another slice or another setting never joins the step", "[commands][slices][params]")
{
    Fixture f;
    f.set(1, Param::gain, -3);
    f.now += 100;
    f.set(2, Param::gain, -3);
    f.now += 100;
    f.set(2, Param::pitch, 5);
    f.service.undo();
    CHECK(f.slice(2).pitch == 0.0);
    CHECK(f.slice(2).gainDb == -3.0);
    f.service.undo();
    CHECK(f.slice(2).gainDb == 0.0);
    CHECK(f.slice(1).gainDb == -3.0);
}

TEST_CASE("values outside the range are clamped", "[commands][slices][params]")
{
    Fixture f;
    f.set(0, Param::pitch, 99);
    CHECK(f.slice(0).pitch == 24.0);
    f.now += 1000;
    f.set(0, Param::pitch, -99);
    CHECK(f.slice(0).pitch == -24.0);
    f.now += 1000;
    f.set(0, Param::gain, 99);
    CHECK(f.slice(0).gainDb == 24.0);
    f.now += 1000;
    f.set(0, Param::gain, -500);
    CHECK(f.slice(0).gainDb == model::silenceGainDb);
    f.now += 1000;
    f.set(0, Param::playMode, 9);
    CHECK(f.slice(0).playMode == model::PlayMode::loop);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("an unchanged value, an unknown slice or an unknown sound makes no step", "[commands][slices][params]")
{
    Fixture f;
    const auto depth = f.undo.getUndoDescriptions().size();
    f.set(1, Param::pitch, 0);
    f.set(9, Param::pitch, 3);
    f.set(-1, Param::pitch, 3);
    f.service.apply(std::make_unique<SetSliceParam>("src_nope", 0, Param::pitch, 3));
    CHECK(f.undo.getUndoDescriptions().size() == depth);
}

TEST_CASE("the first setting of a sound that was never chopped creates its slice set", "[commands][slices][params]")
{
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    CommandService service(tree, undo, juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/slice-params2.sdaw"));
    service.apply(std::make_unique<AddSources>(std::vector{source()}));
    const model::SamplerState state(tree);
    REQUIRE_FALSE(state.sourceAt(0).hasSliceSet());
    service.apply(std::make_unique<SetSliceParam>("src_00000001", 0, Param::pitch, 12));
    REQUIRE(state.sourceAt(0).hasSliceSet());
    CHECK(state.sourceAt(0).sliceSet().numSlices() == 1);
    CHECK(state.sourceAt(0).sliceSet().slices[0].pitch == 12.0);
    service.undo();
    CHECK_FALSE(state.sourceAt(0).hasSliceSet());
}

TEST_CASE("slice settings survive a save and a reopen, and splitting keeps them", "[commands][slices][params]")
{
    Fixture f;
    f.set(1, Param::pitch, 7);
    f.now += 1000;
    f.set(1, Param::reverse, 1);
    f.service.apply(std::make_unique<AddSlicePoint>("src_00000001", 75000)); // splits slice 1 (50000..100000)
    CHECK(f.slice(1) == f.slice(2));
    CHECK(f.slice(2).pitch == 7.0);

    const auto xml = f.tree.createXml();
    const auto reopened = juce::ValueTree::fromXml(*xml);
    CHECK(model::Source(reopened.getChild(0)).sliceSet() == f.state.sourceAt(0).sliceSet());
    CHECK_FALSE(findInvariantViolation(reopened, f.bundle).has_value());
}

TEST_CASE("a setting goes to the slice-th SLICE node even when the slice set has other children", "[commands][slices][params]")
{
    Fixture f;
    auto node = f.state.sourceAt(0).node().getChildWithName(model::sliceIds::SLICESET);
    node.addChild(juce::ValueTree("NOTE"), 0, nullptr); // not a slice: the slice nodes are now at 1..4
    f.set(2, Param::pitch, 7);
    CHECK(f.slice(2).pitch == 7.0);
    CHECK(f.slice(1).pitch == 0.0);
    CHECK(f.slice(3).pitch == 0.0);
    CHECK(f.violations.isEmpty());
}
