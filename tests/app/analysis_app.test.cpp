// M3 1.6: an import is analysed in the background; analysis never makes the project dirty or adds an undo step.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "EditorFixture.h"
#include "app/AppController.h"
#include "commands/Commands.h"

#include <algorithm>
#include "io/WarpDefaults.h"

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

void pumpUntil(const std::function<bool()>& done, int timeoutMs = 20000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (! done() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}
} // namespace

TEST_CASE("an imported sound is analysed in the background without touching the project", "[app][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);
    REQUIRE(controller.hasProject());

    controller.importFiles({fixture("break-90.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
    const auto sources = sampler::model::SamplerState(controller.project().samplerState());
    REQUIRE(sources.numSources() == 1);
    const auto source = sources.sourceAt(0);

    // The import itself is one undo step; remember where the history stands before analysis finishes.
    const auto tokenBefore = controller.commandService().historyToken();
    const bool dirtyBefore = controller.isDirty();
    pumpUntil([&] { return ! controller.analyser().isBusy(); });

    const auto result = controller.analyser().find(source);
    REQUIRE(result != nullptr);
    CHECK(result->bpm() == Catch::Approx(90.0).margin(0.5));
    CHECK(result->onsets.size() == 16);
    CHECK(sampler::io::AnalysisService::cacheFile(controller.project().bundleDir(), source).existsAsFile());

    CHECK(controller.commandService().historyToken() == tokenBefore);
    CHECK(controller.isDirty() == dirtyBefore);
}

TEST_CASE("undoing the import and redoing it does not analyse twice", "[app][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis2");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);

    controller.importFiles({fixture("loop-120-48k.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    REQUIRE(controller.analyser().computedCount() == 1);

    controller.commandService().undo();
    controller.commandService().redo();
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    CHECK(controller.analyser().computedCount() == 1);
}

TEST_CASE("a saved project reopens not dirty, and a tempo correction survives a deleted cache folder", "[app][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis3");
    options.untitledRoot.deleteRecursively();
    const auto target = options.untitledRoot.getChildFile("Saved.sdaw");
    sampler::app::AppController controller(options);
    controller.chooseSaveLocation = [&target](std::function<void(juce::File)> answer) { answer(target); };

    controller.importFiles({fixture("break-90.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    const auto id = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0).id();
    controller.commandService().apply(std::make_unique<sampler::commands::SetSourceTempo>(id, 92.0));
    controller.requestSave();
    REQUIRE_FALSE(controller.isDirty());
    REQUIRE(target.getChildFile("cache/analysis").isDirectory());

    // Reopen with the cache folder gone: the correction is part of the project, the analysis is not.
    REQUIRE(target.getChildFile("cache").deleteRecursively());
    REQUIRE(controller.openProject(target));
    CHECK_FALSE(controller.isDirty());
    const auto reopened = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0);
    CHECK(reopened.tempoCorrection() == 92.0);
    CHECK_FALSE(reopened.hasBeatOffsetCorrection());
    CHECK_FALSE(controller.commandService().canUndo());

    // The sound is analysed again in the background after the reopen, without opening it in the editor and without a dirty mark.
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    const auto again = controller.analyser().find(reopened);
    REQUIRE(again != nullptr);
    CHECK(again->bpm() == Catch::Approx(90.0).margin(0.5));
    CHECK_FALSE(controller.isDirty());
    CHECK_FALSE(controller.commandService().canUndo());
}

TEST_CASE("a sound that is analysed after import gets Beats at its tempo when dropped, without the sample editor", "[app][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis4");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);

    controller.importFiles({fixture("break-90.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    const auto source = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0);

    const auto warp = sampler::io::defaultWarpFor(source, controller.analyser());
    CHECK(warp.on);
    CHECK(warp.mode == sampler::engine::WarpMode::beats);
    CHECK(warp.sourceBpm == Catch::Approx(90.0).margin(0.5));
}

TEST_CASE("a file dropped again from Explorer gets its default warp when the sound is analysed, WARP off when it is new", "[app][analysis][warp]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis6");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);

    controller.importFiles({fixture("break-90.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
    pumpUntil([&] { return ! controller.analyser().isBusy(); });

    // The known break (analysed) and a new loop (not analysed yet) dropped together from Explorer.
    controller.importAndPlace({fixture("break-90.wav"), fixture("loop-120-48k.wav")}, {0, false, 0.0});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().arrangement().numClips() == 2; });
    const auto sources = sampler::model::SamplerState(controller.project().samplerState());
    REQUIRE(sources.numSources() == 2);
    auto clips = controller.project().arrangement().clips();
    REQUIRE(clips.size() == 2);
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) { return a.start < b.start; });

    CHECK(clips[0].sourceId == sources.sourceAt(0).id());
    CHECK(clips[0].warp.on);
    CHECK(clips[0].warp.mode == sampler::engine::WarpMode::beats);
    CHECK(clips[0].warp.sourceBpm == Catch::Approx(90.0).margin(0.5));
    CHECK(clips[1].sourceId == sources.sourceAt(1).id());
    CHECK_FALSE(clips[1].warp.on);
}

TEST_CASE("the clip inspector enables WARP when the analysis of the selected clip's sound finishes", "[app][analysis][warp]")
{
    sampler::test::Session s("analysis-inspector", false);
    sampler::commands::CreateClipsFromSources place({s.id}, {0, false, 0.0});
    s.controller.commandService().apply(place);
    REQUIRE(place.resultIds().size() == 1);
    s.main.selection().setClips(place.resultIds());
    REQUIRE(s.main.inspector() != nullptr);

    sampler::test::pumpUntil([&] { return ! s.controller.analyser().isBusy(); });
    sampler::test::pump(); // the change message of the finished analysis
    REQUIRE(s.controller.analyser().find(sampler::model::SamplerState(s.controller.project().samplerState()).findSourceById(s.id)) != nullptr);
    CHECK(s.main.inspector()->warpToggle.isEnabled()); // without reselecting the clip
}

TEST_CASE("sounds of a reopened project with no analysis are queued without blocking", "[app][analysis]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-analysis5");
    options.untitledRoot.deleteRecursively();
    const auto target = options.untitledRoot.getChildFile("Many.sdaw");
    sampler::app::AppController controller(options);
    controller.chooseSaveLocation = [&target](std::function<void(juce::File)> answer) { answer(target); };

    controller.importFiles({fixture("break-90.wav"), fixture("loop-120-48k.wav")});
    pumpUntil([&] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() == 2; });
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    controller.requestSave();
    REQUIRE(target.getChildFile("cache").deleteRecursively());
    REQUIRE(controller.openProject(target));

    // The open call returned with the jobs only queued; they finish on the worker.
    pumpUntil([&] { return ! controller.analyser().isBusy(); });
    const auto state = sampler::model::SamplerState(controller.project().samplerState());
    REQUIRE(state.numSources() == 2);
    for (int i = 0; i < state.numSources(); ++i)
        CHECK(controller.analyser().find(state.sourceAt(i)) != nullptr);
}
