#include <catch2/catch_test_macros.hpp>

#include "commands/CommandService.h"
#include "commands/Commands.h"

using namespace sampler;
using namespace sampler::commands;

namespace
{
model::SourceData source(int n)
{
    model::SourceData d;
    d.hash = "sha256:" + juce::String::toHexString(n).paddedLeft('0', 16);
    d.id = "src_" + d.hash.getLastCharacters(8);
    d.name = "sound" + juce::String(n);
    d.originalPath = "C:/music/sound" + juce::String(n) + ".wav";
    d.original = "audio/originals/" + d.name + ".wav";
    d.decoded = "audio/" + d.name + ".wav";
    d.sampleRate = 44100.0;
    d.channels = 2;
    d.frames = 44100;
    return d;
}

struct Fixture
{
    juce::File bundle = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/commands.sdaw");
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    CommandService service{tree, undo, bundle};
    model::SamplerState state{tree};
    juce::StringArray violations;

    Fixture() { service.onInvariantViolation = [this](const juce::String& m) { violations.add(m); }; }
};

/** Breaks the model on purpose: appends a SOURCE with an id that is already in use. */
class AddDuplicateId final : public Command
{
public:
    juce::String name() const override { return "Break things"; }
    void perform(ModelContext& context) override
    {
        auto dup = model::Source::create(source(1));
        context.sampler.appendChild(dup, &context.undo);
    }
};
} // namespace

TEST_CASE("SetSourceCategory is one undoable step", "[commands]")
{
    Fixture f;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    const auto id = f.state.sourceAt(0).id();
    REQUIRE(f.state.sourceAt(0).category() == model::Category::loop);

    f.service.apply(std::make_unique<SetSourceCategory>(id, model::Category::oneShot));
    CHECK(f.state.sourceAt(0).category() == model::Category::oneShot);
    CHECK(f.service.undoName() == "Set category: One-shot");

    f.service.undo();
    CHECK(f.state.sourceAt(0).category() == model::Category::loop);
    f.service.redo();
    CHECK(f.state.sourceAt(0).category() == model::Category::oneShot);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("SetSourceCategory with an unknown id or the same category changes nothing", "[commands]")
{
    Fixture f;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    f.service.apply(std::make_unique<SetSourceCategory>("src_nothere", model::Category::oneShot));
    f.service.apply(std::make_unique<SetSourceCategory>(f.state.sourceAt(0).id(), model::Category::loop));
    CHECK(f.service.undoName() == "Import sound1.wav"); // no empty steps were added
}

TEST_CASE("AddSources is one undo step with a readable name", "[commands]")
{
    Fixture f;
    CHECK_FALSE(f.service.canUndo());

    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    CHECK(f.state.numSources() == 1);
    CHECK(f.service.undoName() == "Import sound1.wav");

    f.service.apply(std::make_unique<AddSources>(std::vector{source(2), source(3), source(4)}));
    CHECK(f.state.numSources() == 4);
    CHECK(f.service.undoName() == "Import 3 files");

    f.service.undo(); // one step removes all three
    CHECK(f.state.numSources() == 1);
    CHECK(f.service.redoName() == "Import 3 files");
    f.service.undo();
    CHECK(f.state.numSources() == 0);
    CHECK_FALSE(f.service.canUndo());
    CHECK(f.service.canRedo());

    f.service.redo();
    f.service.redo();
    CHECK(f.state.numSources() == 4);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a new command clears redo", "[commands]")
{
    Fixture f;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    f.service.undo();
    REQUIRE(f.service.canRedo());
    f.service.apply(std::make_unique<AddSources>(std::vector{source(2)}));
    CHECK_FALSE(f.service.canRedo());
}

TEST_CASE("a source already in the project is not added twice", "[commands]")
{
    Fixture f;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1), source(2)}));
    CHECK(f.state.numSources() == 2);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("the invariant check names the command that broke the model", "[commands]")
{
    Fixture f;
    f.service.invariantChecksEnabled = true;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    REQUIRE(f.violations.isEmpty());

    f.service.apply(std::make_unique<AddDuplicateId>());
    REQUIRE(f.violations.size() == 1);
    CHECK(f.violations[0].contains("Break things"));
    CHECK(f.violations[0].contains("two sources have the id"));
}

TEST_CASE("the invariant check can be switched off", "[commands]")
{
    Fixture f;
    f.service.invariantChecksEnabled = false;
    f.service.apply(std::make_unique<AddSources>(std::vector{source(1)}));
    f.service.apply(std::make_unique<AddDuplicateId>());
    CHECK(f.violations.isEmpty());
}

TEST_CASE("findInvariantViolation covers ids, paths and the schema version", "[commands]")
{
    const auto bundle = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/inv.sdaw");
    auto tree = model::SamplerState::createEmpty();
    CHECK_FALSE(findInvariantViolation(tree, bundle).has_value());

    auto good = model::Source::create(source(1));
    tree.appendChild(good, nullptr);
    CHECK_FALSE(findInvariantViolation(tree, bundle).has_value());

    SECTION("absolute path")
    {
        good.setProperty(model::ids::decoded, "C:/elsewhere/a.wav", nullptr);
        const auto problem = findInvariantViolation(tree, bundle);
        REQUIRE(problem.has_value());
        CHECK(problem->contains("not relative"));
    }
    SECTION("path outside the bundle")
    {
        good.setProperty(model::ids::original, "../outside/a.wav", nullptr);
        const auto problem = findInvariantViolation(tree, bundle);
        REQUIRE(problem.has_value());
        CHECK(problem->contains("outside"));
    }
    SECTION("no id")
    {
        good.setProperty(model::ids::id, "", nullptr);
        CHECK(findInvariantViolation(tree, bundle).has_value());
    }
    SECTION("no schema version")
    {
        tree.removeProperty(model::ids::schemaVersion, nullptr);
        CHECK(findInvariantViolation(tree, bundle).has_value());
    }
    SECTION("wrong root")
    {
        CHECK(findInvariantViolation(juce::ValueTree("OTHER"), bundle).has_value());
    }
}

TEST_CASE("random command sequences keep the invariants and undo restores the saved form", "[commands][property]")
{
    for (int seed = 1; seed <= 500; ++seed)
    {
        INFO("seed " << seed);
        juce::Random random(seed);
        Fixture f;
        f.service.invariantChecksEnabled = true;
        int nextSound = 1;

        for (int step = 0; step < 25; ++step)
        {
            const auto choice = random.nextInt(10);
            if (choice < 6)
            {
                std::vector<model::SourceData> batch;
                const int count = 1 + random.nextInt(3);
                for (int i = 0; i < count; ++i)
                    batch.push_back(source(random.nextInt(8) == 0 && nextSound > 1 ? random.nextInt(nextSound - 1) + 1 : nextSound++));
                const auto before = f.tree.toXmlString();
                f.service.apply(std::make_unique<AddSources>(batch));
                const auto afterApply = f.tree.toXmlString();
                if (afterApply != before) // a batch of only known sounds changes nothing
                {
                    f.service.undo();
                    REQUIRE(f.tree.toXmlString() == before); // undo(apply(x)) == x on the serialised tree
                    f.service.redo();
                    REQUIRE(f.tree.toXmlString() == afterApply);
                }
            }
            else if (choice < 8 && f.service.canUndo())
            {
                f.service.undo();
            }
            else if (f.service.canRedo())
            {
                f.service.redo();
            }
            REQUIRE(f.violations.isEmpty());
            REQUIRE_FALSE(findInvariantViolation(f.tree, f.bundle).has_value());
        }
    }
}
