// M3 2.3: the chopping commands: named undo steps, hand edits switching to Manual, refusals, random sequences.
#include <catch2/catch_test_macros.hpp>

#include "analysis/Slicer.h"
#include "commands/CommandService.h"
#include "commands/Commands.h"
#include "commands/SliceCommands.h"

using namespace sampler;
using namespace sampler::commands;

namespace
{
constexpr juce::int64 frames = 470400;
constexpr double rate = 44100.0;

model::SourceData breakSource()
{
    model::SourceData d;
    d.hash = "sha256:0000000000000001";
    d.id = "src_00000001";
    d.name = "break";
    d.originalPath = "C:/music/break.wav";
    d.original = "audio/originals/break.wav";
    d.decoded = "audio/break.wav";
    d.sampleRate = rate;
    d.channels = 2;
    d.frames = frames;
    return d;
}

const juce::String id = "src_00000001";

struct Fixture
{
    juce::File bundle = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/slice-commands.sdaw");
    juce::ValueTree tree = model::SamplerState::createEmpty();
    juce::UndoManager undo;
    CommandService service{tree, undo, bundle};
    model::SamplerState state{tree};
    juce::StringArray violations;

    Fixture()
    {
        service.onInvariantViolation = [this](const juce::String& m) { violations.add(m); };
        service.apply(std::make_unique<AddSources>(std::vector{breakSource()}));
    }

    model::SliceSetData data() const { return state.sourceAt(0).sliceSet(); }
    std::vector<juce::int64> beatPoints(model::SliceDivision d) const
    {
        return analysis::beatSlicePoints(frames, rate, 90.0, 4410, model::slicesPerBeat(d)).frames;
    }
    void apply(std::unique_ptr<Command> c) { service.apply(std::move(c)); }
};
} // namespace

TEST_CASE("choosing Beat slices on the grid in one named undo step", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    CHECK(f.service.undoName() == "Slice by beat");
    CHECK(f.data().mode == model::SliceMode::beat);
    CHECK(f.data().numSlices() == 17); // 16 beats, the first one starting 100 ms in leaves a short first slice
    CHECK(f.data().slices.size() == 17);
    f.service.undo();
    CHECK_FALSE(f.state.sourceAt(0).hasSliceSet());
    f.service.redo();
    CHECK(f.data().numSlices() == 17);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("another division re-slices at once, also one step", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    f.apply(std::make_unique<SetSliceDivision>(id, model::SliceDivision::sixteenth, f.beatPoints(model::SliceDivision::sixteenth)));
    CHECK(f.service.undoName() == "Slice by beat");
    CHECK(f.data().division == model::SliceDivision::sixteenth);
    CHECK(f.data().numSlices() == 65);
    f.service.undo();
    CHECK(f.data().division == model::SliceDivision::quarter);
    CHECK(f.data().numSlices() == 17);
}

TEST_CASE("Transient and Auto-slice take the points the caller computed and reset the slice settings", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0));
    CHECK(f.data().numSlices() == 1);
    f.apply(std::make_unique<AddSlicePoint>(id, 100000));
    auto data = f.data();
    data.slices[0].pitch = 5.0;
    f.state.sourceAt(0).setSliceSet(data, nullptr); // a slice with its own settings

    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::transient, model::SliceDivision::quarter, 70.0,
                                           std::vector<juce::int64>{20000, 40000, 60000}));
    CHECK(f.service.undoName() == "Slice by transient");
    CHECK(f.data().markers == std::vector<juce::int64>{20000, 40000, 60000});
    CHECK(f.data().sensitivity == 70.0);
    CHECK(f.data().slices[0].pitch == 0.0); // reset to the defaults

    f.apply(std::make_unique<AutoSlice>(id, 80.0, std::vector<juce::int64>{20000, 30000, 40000, 60000}));
    CHECK(f.service.undoName() == "Auto-slice");
    CHECK(f.data().numSlices() == 5);
    CHECK(f.data().sensitivity == 80.0);
    f.service.undo();
    CHECK(f.data().numSlices() == 4);
    CHECK(f.data().sensitivity == 70.0);
}

TEST_CASE("choosing Manual keeps the points and the settings", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    auto data = f.data();
    data.slices[3].gainDb = -6.0;
    f.state.sourceAt(0).setSliceSet(data, nullptr);

    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0));
    CHECK(f.service.undoName() == "Slice manually");
    CHECK(f.data().mode == model::SliceMode::manual);
    CHECK(f.data().markers == data.markers);
    CHECK(f.data().slices[3].gainDb == -6.0);
}

TEST_CASE("Beat to Manual with three points: Undo restores both", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0));
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0, std::vector<juce::int64>{})); // no-op
    // Three points by hand from a clean slate.
    Fixture g;
    g.apply(std::make_unique<AddSlicePoint>(id, 50000));
    g.apply(std::make_unique<AddSlicePoint>(id, 150000));
    g.apply(std::make_unique<AddSlicePoint>(id, 300000));
    g.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, g.beatPoints(model::SliceDivision::quarter)));
    REQUIRE(g.data().mode == model::SliceMode::beat);
    g.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0));
    g.service.undo(); // back to Beat
    CHECK(g.data().mode == model::SliceMode::beat);
    g.service.undo(); // back to three manual points
    CHECK(g.data().mode == model::SliceMode::manual);
    CHECK(g.data().markers == std::vector<juce::int64>{50000, 150000, 300000});
}

TEST_CASE("a hand edit in Beat mode switches to Manual in the same step", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    const auto beatData = f.data();

    f.apply(std::make_unique<AddSlicePoint>(id, 100000));
    CHECK(f.service.undoName() == "Add slice point");
    CHECK(f.data().mode == model::SliceMode::manual);
    CHECK(f.data().numSlices() == beatData.numSlices() + 1);
    f.service.undo();
    CHECK(f.data() == beatData); // the points and Beat mode come back together

    f.apply(std::make_unique<RemoveSlicePoint>(id, 2));
    CHECK(f.service.undoName() == "Remove slice point");
    CHECK(f.data().mode == model::SliceMode::manual);
    CHECK(f.data().numSlices() == beatData.numSlices() - 1);
    f.service.undo();
    CHECK(f.data() == beatData);

    f.service.begin("Move slice point");
    for (const juce::int64 to : {60000, 61000, 62000})
        f.service.update(std::make_unique<MoveSlicePoint>(id, 1, to));
    f.service.commit();
    CHECK(f.service.undoName() == "Move slice point");
    CHECK(f.data().mode == model::SliceMode::manual);
    CHECK(f.data().markers[1] == 62000);
    f.service.undo(); // one step for the whole drag
    CHECK(f.data() == beatData);
}

TEST_CASE("add and remove: a double-click adds a point, a second one removes it, two undos", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<SetSliceMode>(id, model::SliceMode::beat, model::SliceDivision::quarter, 50.0, f.beatPoints(model::SliceDivision::quarter)));
    const auto before = f.data();
    f.apply(std::make_unique<AddSlicePoint>(id, 100000));
    const auto markersNow = f.data().markers;
    const int added = static_cast<int>(std::find(markersNow.begin(), markersNow.end(), 100000) - markersNow.begin());
    f.apply(std::make_unique<RemoveSlicePoint>(id, added));
    CHECK(f.data().markers == before.markers);
    CHECK(f.service.undoName() == "Remove slice point");
    f.service.undo();
    CHECK(f.service.undoName() == "Add slice point");
    f.service.undo();
    CHECK(f.service.undoName() == "Slice by beat");
}

TEST_CASE("a point dragged onto its neighbour stops 5 ms before it", "[commands][slices]")
{
    Fixture f;
    f.apply(std::make_unique<AddSlicePoint>(id, 100000));
    f.apply(std::make_unique<AddSlicePoint>(id, 200000));
    f.apply(std::make_unique<MoveSlicePoint>(id, 0, 200000));
    CHECK(f.data().markers[0] == 200000 - 221);
    f.apply(std::make_unique<MoveSlicePoint>(id, 1, 1));
    CHECK(f.data().markers[1] == 200000 - 221 + 221);
    f.apply(std::make_unique<MoveSlicePoint>(id, 0, -500));
    CHECK(f.data().markers[0] == 221);
    CHECK(f.violations.isEmpty());
}

TEST_CASE("typed bounds move the matching point and are refused outside what the neighbours allow", "[commands][slices]")
{
    Fixture f;
    for (const juce::int64 at : {50000, 100000, 150000})
        f.apply(std::make_unique<AddSlicePoint>(id, at));
    // Slice 2 spans 100000..150000. END of slice 2 is the point at 150000 (index 2); START is the point at 100000 (index 1).
    SetSliceBounds end(id, 2, true, 130000);
    f.service.apply(end);
    CHECK(end.wasApplied());
    CHECK(f.service.undoName() == "Set slice end");
    CHECK(f.data().markers == std::vector<juce::int64>{50000, 100000, 130000});
    f.apply(std::make_unique<SetSliceBounds>(id, 2, false, 80000));
    CHECK(f.service.undoName() == "Set slice start");
    CHECK(f.data().markers[1] == 80000);

    const auto before = f.data();
    const auto depth = f.undo.getUndoDescriptions().size();
    for (const juce::int64 bad : {40000, 130000 - 10, 10})
    {
        SetSliceBounds refused(id, 2, false, bad); // outside neighbours (50000 and 130000, 5 ms apart)
        f.service.apply(refused);
        if (bad != 130000 - 10)
            CHECK_FALSE(refused.wasApplied());
    }
    f.apply(std::make_unique<SetSliceBounds>(id, 0, false, 5000)); // the start of the sound
    f.apply(std::make_unique<SetSliceBounds>(id, 3, true, 400000)); // the end of the sound
    CHECK(f.data().markers[0] == 50000);
    CHECK(f.data().markers.size() == before.markers.size());
    CHECK(f.undo.getUndoDescriptions().size() == depth); // refusals leave no step
}

TEST_CASE("refused commands leave no step", "[commands][slices]")
{
    Fixture f;
    const auto depth = f.undo.getUndoDescriptions().size();
    f.apply(std::make_unique<RemoveSlicePoint>(id, 0));
    f.apply(std::make_unique<MoveSlicePoint>(id, 3, 1000));
    f.apply(std::make_unique<AddSlicePoint>(id, 0));
    f.apply(std::make_unique<AddSlicePoint>(id, frames));
    f.apply(std::make_unique<AddSlicePoint>(id, frames - 10));
    f.apply(std::make_unique<AddSlicePoint>("src_nope", 1000));
    CHECK(f.undo.getUndoDescriptions().size() == depth);
    CHECK_FALSE(f.state.sourceAt(0).hasSliceSet());
}

TEST_CASE("random sequences of chopping commands keep the invariants, undo exactly and survive a save", "[commands][slices][property]")
{
    for (int seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        juce::Random random(seed);
        Fixture f;
        f.service.invariantChecksEnabled = true;

        for (int step = 0; step < 25; ++step)
        {
            std::unique_ptr<Command> command;
            const auto markers = f.data().markers;
            switch (random.nextInt(9))
            {
                case 0:
                {
                    const auto division = static_cast<model::SliceDivision>(random.nextInt(4));
                    command = std::make_unique<SetSliceMode>(id, model::SliceMode::beat, division, 50.0, f.beatPoints(division));
                    break;
                }
                case 1:
                {
                    std::vector<analysis::Onset> hits;
                    const int n = random.nextInt(40);
                    for (int i = 0; i < n; ++i)
                        hits.push_back({static_cast<juce::int64>(random.nextInt(static_cast<int>(frames))), random.nextFloat()});
                    std::sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
                    command = std::make_unique<AutoSlice>(id, random.nextDouble() * 100.0,
                                                          analysis::transientSlicePoints(hits, frames, rate).frames);
                    break;
                }
                case 2: command = std::make_unique<SetSliceMode>(id, model::SliceMode::manual, model::SliceDivision::quarter, 50.0); break;
                case 3:
                case 4: command = std::make_unique<AddSlicePoint>(id, static_cast<juce::int64>(random.nextInt(static_cast<int>(frames)))); break;
                case 5: command = std::make_unique<MoveSlicePoint>(id, random.nextInt(static_cast<int>(markers.size()) + 1), random.nextInt(static_cast<int>(frames))); break;
                case 6: command = std::make_unique<RemoveSlicePoint>(id, random.nextInt(static_cast<int>(markers.size()) + 1)); break;
                case 7:
                    command = std::make_unique<SetSliceBounds>(id, random.nextInt(static_cast<int>(markers.size()) + 2), random.nextBool(),
                                                               random.nextInt(static_cast<int>(frames)));
                    break;
                default: command = std::make_unique<SetSliceDivision>(id, model::SliceDivision::eighth, f.beatPoints(model::SliceDivision::eighth)); break;
            }

            const auto before = f.tree.toXmlString();
            f.service.apply(std::move(command));
            const auto after = f.tree.toXmlString();
            REQUIRE(f.violations.isEmpty());
            if (after != before)
            {
                f.service.undo();
                REQUIRE(f.tree.toXmlString() == before); // undo(apply(x)) == x on the serialised tree
                f.service.redo();
                REQUIRE(f.tree.toXmlString() == after);
            }
        }

        // Save and reopen: the slice set is the same.
        const auto xml = f.tree.createXml();
        const auto reopened = juce::ValueTree::fromXml(*xml);
        REQUIRE(model::Source(reopened.getChild(0)).sliceSet() == f.data());
        REQUIRE_FALSE(findInvariantViolation(reopened, f.bundle).has_value());
    }
}
