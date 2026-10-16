// M3 1.7: the user's tempo and first-beat corrections of a sound.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "commands/CommandService.h"
#include "commands/Commands.h"

using namespace sampler;
using namespace sampler::commands;

namespace
{
model::SourceData loop()
{
    model::SourceData d;
    d.hash = "sha256:0000000000000001";
    d.id = "src_00000001";
    d.name = "break";
    d.originalPath = "C:/music/break.wav";
    d.original = "audio/originals/break.wav";
    d.decoded = "audio/break.wav";
    d.sampleRate = 44100.0;
    d.channels = 2;
    d.frames = 470400;
    return d;
}

struct Fixture
{
    juce::File bundle = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/correction.sdaw");
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    CommandService service{tree, undo, bundle};
    model::SamplerState state{tree};
    juce::StringArray violations;

    Fixture()
    {
        service.onInvariantViolation = [this](const juce::String& m) { violations.add(m); };
        service.apply(std::make_unique<AddSources>(std::vector{loop()}));
    }

    model::Source source() const { return state.sourceAt(0); }
};
} // namespace

TEST_CASE("a sound has no corrections until the user makes one", "[commands][correction]")
{
    Fixture f;
    CHECK_FALSE(f.source().hasTempoCorrection());
    CHECK_FALSE(f.source().hasBeatOffsetCorrection());
    CHECK(f.source().effectiveBpm(90.0) == 90.0);
    CHECK(f.source().effectiveBeatOffset(4410) == 4410);
}

TEST_CASE("setting the tempo is one named undo step and the correction wins over the analysis", "[commands][correction]")
{
    Fixture f;
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 92.0));
    CHECK(f.service.undoName() == "Set sound tempo");
    CHECK(f.source().tempoCorrection() == 92.0);
    CHECK(f.source().effectiveBpm(90.0) == 92.0);
    f.service.undo();
    CHECK_FALSE(f.source().hasTempoCorrection());
    CHECK(f.source().effectiveBpm(90.0) == 90.0);
    f.service.redo();
    CHECK(f.source().tempoCorrection() == 92.0);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("doubling the tempo and undoing returns to the first correction", "[commands][correction]")
{
    Fixture f;
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 60.0));
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 120.0));
    CHECK(f.source().tempoCorrection() == 120.0);
    f.service.undo();
    CHECK(f.source().tempoCorrection() == 60.0);
}

TEST_CASE("the tempo is clamped to 20 to 999 BPM and an unchanged tempo makes no step", "[commands][correction]")
{
    Fixture f;
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 5.0));
    CHECK(f.source().tempoCorrection() == 20.0);
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 5000.0));
    CHECK(f.source().tempoCorrection() == 999.0);

    const auto token = f.service.historyToken();
    const auto xml = f.tree.toXmlString();
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 999.0));
    CHECK(f.tree.toXmlString() == xml);
    f.service.apply(std::make_unique<SetSourceTempo>("src_nope", 100.0));
    CHECK(f.tree.toXmlString() == xml);
    juce::ignoreUnused(token);
}

TEST_CASE("the corrected tempo survives a save and reopen of the tree", "[commands][correction]")
{
    Fixture f;
    f.service.apply(std::make_unique<SetSourceTempo>("src_00000001", 92.0));
    f.service.apply(std::make_unique<MoveFirstBeat>("src_00000001", 4410));

    const auto xml = f.tree.createXml();
    REQUIRE(xml != nullptr);
    const auto reopened = juce::ValueTree::fromXml(*xml);
    const model::SamplerState state(reopened);
    CHECK(state.sourceAt(0).tempoCorrection() == 92.0);
    CHECK(state.sourceAt(0).beatOffsetCorrection() == 4410);
    CHECK_FALSE(findInvariantViolation(reopened, f.bundle).has_value());
}

TEST_CASE("dragging the first beat is one undo step named Move first beat", "[commands][correction]")
{
    Fixture f;
    f.service.begin("Move first beat");
    for (const juce::int64 frame : {100, 2000, 4410})
        f.service.update(std::make_unique<MoveFirstBeat>("src_00000001", frame));
    f.service.commit();
    CHECK(f.source().beatOffsetCorrection() == 4410);
    CHECK(f.service.undoName() == "Move first beat");
    f.service.undo();
    CHECK_FALSE(f.source().hasBeatOffsetCorrection()); // one Undo puts the marker back to the analysed place
    CHECK(f.service.undoName() == "Import break.wav");
}

TEST_CASE("the first beat is kept inside the sound", "[commands][correction]")
{
    Fixture f;
    f.service.apply(std::make_unique<MoveFirstBeat>("src_00000001", -50));
    CHECK(f.source().beatOffsetCorrection() == 0);
    f.service.apply(std::make_unique<MoveFirstBeat>("src_00000001", 99999999));
    CHECK(f.source().beatOffsetCorrection() == 470399);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a correction outside the allowed range is reported by the invariant check", "[commands][correction]")
{
    Fixture f;
    auto node = f.state.sourceAt(0).node();
    node.setProperty(model::ids::bpm, 5.0, nullptr);
    CHECK(findInvariantViolation(f.tree, f.bundle).has_value());
    node.setProperty(model::ids::bpm, 90.0, nullptr);
    node.setProperty(model::ids::beatOffset, 999999999, nullptr);
    CHECK(findInvariantViolation(f.tree, f.bundle).has_value());
}
