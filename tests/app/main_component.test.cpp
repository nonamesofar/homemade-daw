// The main window content driven without a window: drop a file on it and see the row appear.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"
#include "model/SamplerState.h"

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

void pumpUntilIdle(sampler::app::AppController& controller, int timeoutMs = 20000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    do
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    while (controller.importer().isBusy() && juce::Time::getMillisecondCounter() < end);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100); // the change message after the last import
}

sampler::app::AppOptions testOptions(const juce::String& name)
{
    sampler::app::AppOptions options;
    options.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name);
    options.untitledRoot.deleteRecursively();
    return options;
}
} // namespace

TEST_CASE("a new window shows an empty browser and an untitled project", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-empty"));
    REQUIRE(controller.startupError().isEmpty());
    sampler::app::MainComponent main(controller);

    CHECK(main.browser().rows().empty());
    CHECK(controller.windowTitle() == "Untitled - Sampler");
    CHECK(controller.audioStatusText().startsWith("Audio:"));
}

TEST_CASE("dropping a file on the window adds its row", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-drop"));
    REQUIRE(controller.startupError().isEmpty());
    sampler::app::MainComponent main(controller);

    main.filesDropped({fixture("drums-loop-120.wav").getFullPathName()}, 10, 10);
    pumpUntilIdle(controller);

    REQUIRE(main.browser().rows().size() == 1);
    CHECK(main.browser().rows()[0].name == "drums-loop-120");
    CHECK_FALSE(main.browser().rows()[0].importing);
    CHECK(controller.windowTitle() == "Untitled* - Sampler"); // the import is an unsaved change

    // Dropping the same file again keeps one row and says so.
    juce::String toastText;
    controller.onToast = [&toastText](const juce::String& text, bool) { toastText = text; };
    juce::String selected;
    controller.onSelectSource = [&selected](const juce::String& id) { selected = id; };
    main.filesDropped({fixture("drums-loop-120.wav").getFullPathName()}, 10, 10);
    pumpUntilIdle(controller);
    CHECK(main.browser().rows().size() == 1);
    CHECK(toastText.contains("already in the project"));
    CHECK(selected == main.browser().rows()[0].id);
}

TEST_CASE("rows are sorted by name and rejected files are reported", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-sorted"));
    sampler::app::MainComponent main(controller);

    juce::StringArray errors;
    controller.onToast = [&errors](const juce::String& text, bool isError) {
        if (isError)
            errors.add(text);
    };
    main.filesDropped({fixture("kick.wav").getFullPathName(), fixture("drums-loop-120.wav").getFullPathName(),
                       fixture("not-audio.txt").getFullPathName()},
                      0, 0);
    pumpUntilIdle(controller);

    CHECK(main.browser().rowNames() == juce::StringArray{"drums-loop-120", "kick"});
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].startsWith("not-audio.txt"));
}

TEST_CASE("a click before the audio is ready is queued and plays when it becomes ready", "[app][gui][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto options = testOptions("app-queue");
    options.audioStartDelayMs = 2500;
    sampler::app::AppController controller(options);
    sampler::app::MainComponent main(controller);

    main.filesDropped({fixture("kick.wav").getFullPathName()}, 0, 0);
    pumpUntilIdle(controller);
    REQUIRE(main.browser().rows().size() == 1);
    const auto id = main.browser().rows()[0].id;

    if (controller.engine().isAudioReady())
        SKIP("audio was ready too early to test the queue");
    controller.toggleAudition(id);
    CHECK(controller.playingId().isEmpty());
    CHECK(controller.audioStatusText() == "Audio: starting...");

    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (controller.playingId().isEmpty() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    if (controller.playingId().isEmpty())
        SKIP("no audio output device");
    CHECK(controller.playingId() == id);
    CHECK(controller.audioStatusText().contains("kHz"));
}

TEST_CASE("Edit menu commands name the step and follow what can be undone", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-undo"));
    sampler::app::MainComponent main(controller);
    auto& manager = main.commandManager();

    const auto info = [&manager](juce::CommandID id) {
        juce::ApplicationCommandInfo i(id);
        manager.getTargetForCommand(id, i);
        return i;
    };

    // Nothing to undo yet: both items are disabled.
    CHECK((info(sampler::app::undoCommand).flags & juce::ApplicationCommandInfo::isDisabled) != 0);
    CHECK((info(sampler::app::redoCommand).flags & juce::ApplicationCommandInfo::isDisabled) != 0);

    main.filesDropped({fixture("drums-loop-120.wav").getFullPathName()}, 0, 0);
    pumpUntilIdle(controller);
    main.filesDropped({fixture("kick.wav").getFullPathName(), fixture("bass-phrase.flac").getFullPathName(),
                       fixture("drums-loop-120.aiff").getFullPathName()},
                      0, 0);
    pumpUntilIdle(controller);
    CHECK(info(sampler::app::undoCommand).shortName.startsWith("Undo Import"));
    CHECK((info(sampler::app::undoCommand).flags & juce::ApplicationCommandInfo::isDisabled) == 0);

    const auto before = main.browser().rows().size();
    REQUIRE(manager.invokeDirectly(sampler::app::undoCommand, false));
    pumpUntilIdle(controller);
    CHECK(main.browser().rows().size() < before);
    CHECK((info(sampler::app::redoCommand).flags & juce::ApplicationCommandInfo::isDisabled) == 0);
    CHECK(info(sampler::app::redoCommand).shortName.startsWith("Redo Import"));

    manager.invokeDirectly(sampler::app::redoCommand, false);
    pumpUntilIdle(controller);
    CHECK(main.browser().rows().size() == before);

    // A new edit clears redo.
    manager.invokeDirectly(sampler::app::undoCommand, false);
    main.filesDropped({fixture("kick.wav").getFullPathName()}, 0, 0);
    pumpUntilIdle(controller);
    CHECK((info(sampler::app::redoCommand).flags & juce::ApplicationCommandInfo::isDisabled) != 0);
}

TEST_CASE("a multi-file drop is one undo step", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-three"));
    sampler::app::MainComponent main(controller);

    // Three different WAV sounds: the loop, the kick and a copy of the loop with other content (the 5-channel tone).
    main.filesDropped({fixture("drums-loop-120.wav").getFullPathName(), fixture("kick.wav").getFullPathName(),
                       fixture("5ch-tone.wav").getFullPathName()},
                      0, 0);
    pumpUntilIdle(controller);
    REQUIRE(main.browser().rows().size() == 3);
    CHECK(controller.commandService().undoName() == "Import 3 files");

    controller.commandService().undo();
    pumpUntilIdle(controller);
    CHECK(main.browser().rows().empty());
    CHECK_FALSE(controller.commandService().canUndo());
}

TEST_CASE("Help > Widget gallery opens the gallery window", "[app][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(testOptions("app-gallery"));
    sampler::app::MainComponent main(controller);

    CHECK(main.getMenuBarNames() == juce::StringArray{"File", "Edit", "Track", "Help"});
    CHECK_FALSE(main.isWidgetGalleryOpen());
    main.commandManager().invokeDirectly(sampler::app::widgetGalleryCommand, false);
    CHECK(main.isWidgetGalleryOpen());
}
