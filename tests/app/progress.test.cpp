// A running job shows a progress indicator in the header and an "importing" row in the browser.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"

namespace
{
void pump(int ms = 150) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
} // namespace

TEST_CASE("a slow job shows the indicator and the importing row until it finishes", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-progress");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);
    sampler::app::MainComponent main(controller);

    CHECK_FALSE(main.isShowingProgress());

    // A fake job: what the import service registers while a long file decodes.
    auto& jobs = controller.jobs();
    const int id = jobs.add("Importing long-take.wav", {"long-take"});
    pump();
    CHECK(main.isShowingProgress());
    CHECK(main.progressText() == "Importing long-take.wav");
    REQUIRE(main.browser().rows().size() == 1);
    CHECK(main.browser().rows()[0].importing);
    CHECK(main.browser().rows()[0].name == "long-take");

    jobs.setProgress(id, 0.5);
    pump();
    CHECK(main.progressFraction() == 0.5);

    // Clicking an importing row does nothing (it has no sound yet).
    bool clicked = false;
    main.browser().onRowClicked = [&clicked](const juce::String&) { clicked = true; };
    CHECK(main.browser().rows()[0].id.isEmpty());

    jobs.nameDone(id, "long-take");
    jobs.remove(id);
    pump();
    CHECK_FALSE(main.isShowingProgress());
    CHECK(main.browser().rows().empty());
    CHECK_FALSE(clicked);
}

TEST_CASE("a real import shows progress and clears it when done", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/app-progress2");
    options.untitledRoot.deleteRecursively();
    sampler::app::AppController controller(options);
    sampler::app::MainComponent main(controller);

    juce::Array<juce::File> files;
    files.add(juce::File(SAMPLER_FIXTURES_DIR).getChildFile("texture-60s.flac"));
    controller.importFiles(files);
    pump(20);
    CHECK(main.isShowingProgress());

    const auto end = juce::Time::getMillisecondCounter() + 30000;
    while (controller.importer().isBusy() && juce::Time::getMillisecondCounter() < end)
        pump(20);
    pump();
    CHECK_FALSE(main.isShowingProgress());
    REQUIRE(main.browser().rows().size() == 1);
    CHECK_FALSE(main.browser().rows()[0].importing);
}
