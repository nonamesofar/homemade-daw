#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/EngineHost.h"

namespace
{
class SpikeWindow : public juce::DocumentWindow
{
public:
    SpikeWindow()
        : DocumentWindow("Sampler Spike", juce::Colours::darkgrey, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        centreWithSize(900, 600);
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class SpikeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Sampler Spike"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    void initialise(const juce::String&) override
    {
        engine = std::make_unique<sampler::EngineHost>();
        window = std::make_unique<SpikeWindow>();
    }

    void shutdown() override
    {
        window.reset();
        engine.reset();
    }

private:
    std::unique_ptr<sampler::EngineHost> engine;
    std::unique_ptr<SpikeWindow> window;
};
} // namespace

START_JUCE_APPLICATION(SpikeApp)
