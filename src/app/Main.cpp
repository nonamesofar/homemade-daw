// Sampler: the app. Developer flags: --audio-start-delay <ms> holds back the "audio ready" signal; --open <project> opens
// a project at start; --window-size <W>x<H>; --snapshot <png> saves the window content as a picture after a short wait
// (--snapshot-delay <ms>, default 2500) and quits; --import <file or folder> imports it at start.
#include "AppController.h"
#include "MainWindow.h"
#include "ui/look/SamplerLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class SamplerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        sampler::app::AppOptions options;
        const auto args = juce::StringArray::fromTokens(commandLine, true);
        juce::String openPath, importPath;
        int width = 0, height = 0;
        for (int i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == "--audio-start-delay")
                options.audioStartDelayMs = args[i + 1].getIntValue();
            else if (args[i] == "--open")
                openPath = args[i + 1].unquoted();
            else if (args[i] == "--import")
                importPath = args[i + 1].unquoted();
            else if (args[i] == "--snapshot")
                snapshotPath = args[i + 1].unquoted();
            else if (args[i] == "--snapshot-delay")
                snapshotDelayMs = args[i + 1].getIntValue();
            else if (args[i] == "--window-size")
            {
                width = args[i + 1].upToFirstOccurrenceOf("x", false, false).getIntValue();
                height = args[i + 1].fromFirstOccurrenceOf("x", false, false).getIntValue();
            }
        }

        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

        controller = std::make_unique<sampler::app::AppController>(options);
        controller->onQuitConfirmed = [] { juce::JUCEApplication::quit(); };
        if (controller->startupError().isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Sampler",
                                                   controller->startupError(), "Quit", nullptr,
                                                   juce::ModalCallbackFunction::create([](int) { juce::JUCEApplication::quit(); }));
            return;
        }
        if (openPath.isNotEmpty())
            controller->openProject(juce::File(openPath));
        window = std::make_unique<sampler::app::MainWindow>(*controller);
        if (importPath.isNotEmpty())
        {
            const juce::File target(importPath);
            juce::Array<juce::File> files;
            if (target.isDirectory())
                files = target.findChildFiles(juce::File::findFiles, false);
            else
                files.add(target);
            controller->importFiles(files);
        }
        if (width > 0 && height > 0)
            window->setContentComponentSize(width, height);
        if (snapshotPath.isNotEmpty())
            snapshotTimer = std::make_unique<SnapshotTimer>(*this);
    }

    void saveSnapshot()
    {
        if (window != nullptr && window->getContentComponent() != nullptr)
        {
            auto* content = window->getContentComponent();
            const auto image = content->createComponentSnapshot(content->getLocalBounds(), true, 1.0f);
            juce::File file(snapshotPath);
            file.deleteFile();
            if (juce::FileOutputStream out(file); out.openedOk())
                juce::PNGImageFormat().writeImageToStream(image, out);
        }
        quit();
    }

    void shutdown() override
    {
        snapshotTimer.reset();
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        controller.reset();
    }

    void systemRequestedQuit() override
    {
        if (controller == nullptr)
            quit();
        else
            controller->requestQuit();
    }

private:
    struct SnapshotTimer : juce::Timer
    {
        explicit SnapshotTimer(SamplerApplication& o) : owner(o) { startTimer(juce::jmax(100, owner.snapshotDelayMs)); }
        void timerCallback() override
        {
            stopTimer();
            owner.saveSnapshot();
        }
        SamplerApplication& owner;
    };

    sampler::ui::SamplerLookAndFeel lookAndFeel;
    juce::String snapshotPath;
    int snapshotDelayMs = 2500;
    std::unique_ptr<SnapshotTimer> snapshotTimer;
    std::unique_ptr<sampler::app::AppController> controller;
    std::unique_ptr<sampler::app::MainWindow> window;
};
} // namespace

START_JUCE_APPLICATION(SamplerApplication)
