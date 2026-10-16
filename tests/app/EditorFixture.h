#pragma once

// Shared by the sample editor tests: a window with the 4-bar 90 BPM break imported and analysed.
#include <juce_events/juce_events.h>

#include "app/AppController.h"
#include "app/MainComponent.h"
#include "commands/ArrangeCommands.h"
#include "commands/Commands.h"

#include <functional>

namespace sampler::test
{
inline juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

inline void pumpUntil(const std::function<bool()>& done, int timeoutMs = 20000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (! done() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

inline void pump(int ms = 100) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

/** A window with the 4-bar 90 BPM break imported and analysed. */
struct Session
{
    explicit Session(const juce::String& name, bool analysed = true, double padRate = 0.0)
        : controller(options(name, padRate)), main(controller)
    {
        main.setSize(1440, 900);
        controller.importFiles({fixture("break-90.wav")});
        pumpUntil([this] { return ! controller.importer().isBusy() && controller.project().samplerState().getNumChildren() > 0; });
        if (analysed)
            pumpUntil([this] { return ! controller.analyser().isBusy(); });
        pump();
        id = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0).id();
    }

    static sampler::app::AppOptions options(const juce::String& name, double padRate = 0.0)
    {
        sampler::app::AppOptions o;
        o.padOutputRate = padRate;
        o.untitledRoot = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/" + name);
        o.untitledRoot.deleteRecursively();
        return o;
    }

    sampler::ui::SampleEditorView& editor() { return *main.sampleEditor(); }

    juce::MouseEvent event(juce::Component& target, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent(source, pos, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), downPos,
                                juce::Time::getCurrentTime(), 1, dragged);
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller;
    sampler::app::MainComponent main;
    juce::String id;
};
} // namespace sampler::test
