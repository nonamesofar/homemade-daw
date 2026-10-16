// M3 1.6: an import is analysed in the background; analysis never makes the project dirty or adds an undo step.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "commands/Commands.h"

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
    CHECK(controller.analyser().find(reopened) == nullptr); // nothing is analysed until the sound is opened in the editor
    CHECK_FALSE(controller.commandService().canUndo());
}
