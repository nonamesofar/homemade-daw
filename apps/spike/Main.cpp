#include <juce_gui_basics/juce_gui_basics.h>

#include "TimelineTab.h"
#include "engine/EngineHost.h"

namespace
{
/** Player tab of the spike: open a file, play/stop, show position and device facts. */
class PlayerComponent : public juce::Component, private juce::Timer
{
public:
    explicit PlayerComponent(sampler::EngineHost& hostToUse) : host(hostToUse)
    {
        addAndMakeVisible(openButton);
        addAndMakeVisible(playButton);
        addAndMakeVisible(info);
        info.setJustificationType(juce::Justification::topLeft);

        openButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser>("Open audio file", juce::File(),
                                                          "*.wav;*.mp3;*.m4a;*.flac;*.ogg");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this](const juce::FileChooser& fc)
                                 {
                                     if (fc.getResult().existsAsFile())
                                         load(fc.getResult());
                                 });
        };
        playButton.onClick = [this]
        {
            if (host.isPlaying())
                host.stop();
            else
                host.play();
        };

        setSize(600, 300);
        startTimerHz(10);
    }

    void load(const juce::File& file)
    {
        const auto probe = host.probeDecode(file);
        loaded = host.loadFile(file);
        fileText = file.getFileName() + "\n"
                   + (probe.ok ? probe.formatName + ", " + juce::String(probe.sampleRate) + " Hz, "
                                     + juce::String(probe.channels) + " ch, " + juce::String(probe.lengthInFrames)
                                     + " frames"
                               : juce::String("cannot decode"));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16);
        auto row = r.removeFromTop(32);
        openButton.setBounds(row.removeFromLeft(120));
        row.removeFromLeft(8);
        playButton.setBounds(row.removeFromLeft(120));
        r.removeFromTop(12);
        info.setBounds(r);
    }

private:
    void timerCallback() override
    {
        playButton.setButtonText(host.isPlaying() ? "Stop" : "Play");
        playButton.setEnabled(loaded);
        info.setText(fileText + "\nposition: " + juce::String(host.positionSeconds(), 2) + " s / "
                         + juce::String(host.loadedLengthSeconds(), 2) + " s"
                         + "\ndevice: " + host.currentAudioDeviceType() + " / " + host.currentAudioDeviceName()
                         + "\nxruns: " + juce::String(host.xrunCount()),
                     juce::dontSendNotification);
    }

    sampler::EngineHost& host;
    juce::TextButton openButton{"Open..."}, playButton{"Play"};
    juce::Label info;
    juce::String fileText;
    bool loaded = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class SpikeWindow : public juce::DocumentWindow
{
public:
    explicit SpikeWindow(juce::Component& content)
        : DocumentWindow("Sampler Spike", juce::Colours::darkgrey, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

/** `--autoplay <file> <seconds>`: plays unattended, prints xruns, quits. For the dropout check (task 2.1). */
class AutoPlay : private juce::Timer
{
public:
    AutoPlay(sampler::EngineHost& h, const juce::File& f, double s) : host(h), file(f), seconds(s)
    {
        // Give the engine's message-loop work (device list scan) a moment before loading.
        startTimer(4000);
    }

private:
    void start()
    {
        if (!host.loadFile(file))
        {
            std::fprintf(stderr, "autoplay: cannot load %s\n", file.getFullPathName().toRawUTF8());
            juce::JUCEApplication::getInstance()->setApplicationReturnValue(2);
            juce::JUCEApplication::quit();
            return;
        }
        host.play();
        std::printf("autoplay: device=%s / %s\n", host.currentAudioDeviceType().toRawUTF8(),
                    host.currentAudioDeviceName().toRawUTF8());
        std::printf("%s", host.describeAudioSetup().toRawUTF8());
        std::fflush(stdout);
        startedAtMs = juce::Time::getMillisecondCounterHiRes();
        started = true;
        startTimer(500);
    }

    void timerCallback() override
    {
        if (!started)
        {
            start();
            return;
        }
        std::printf("autoplay: %s xruns=%d\n", host.describeTransport().toRawUTF8(), host.xrunCount());
        std::fflush(stdout);
        const auto waitedMs = juce::Time::getMillisecondCounterHiRes() - startedAtMs;
        if (host.positionSeconds() < seconds && waitedMs < (seconds + 10.0) * 1000.0)
            return;
        std::printf("autoplay: position=%.2f s xruns=%d\n", host.positionSeconds(), host.xrunCount());
        std::fflush(stdout);
        host.stop();
        stopTimer();
        juce::JUCEApplication::quit();
    }

    sampler::EngineHost& host;
    juce::File file;
    double seconds;
    double startedAtMs = 0.0;
    bool started = false;
};

/** `--timeline-snapshot <png> [zoomFactor]`: lets the waveforms fill in, saves the timeline as a PNG and quits. */
class Snapshot : private juce::Timer
{
public:
    Snapshot(juce::Component& c, const juce::File& f) : component(c), file(f) { startTimer(3000); }

private:
    void timerCallback() override
    {
        stopTimer();
        auto image = component.createComponentSnapshot(component.getLocalBounds());
        file.deleteFile();
        if (auto out = file.createOutputStream())
            juce::PNGImageFormat().writeImageToStream(image, *out);
        std::printf("snapshot: %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        std::fflush(stdout);
        juce::JUCEApplication::quit();
    }

    juce::Component& component;
    juce::File file;
};

class SpikeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Sampler Spike"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }

    void initialise(const juce::String& commandLine) override
    {
        engine = std::make_unique<sampler::EngineHost>();

        auto args = juce::StringArray::fromTokens(commandLine, true);
        args.trim();
        if (args.size() >= 3 && args[0] == "--autoplay")
        {
            autoPlay = std::make_unique<AutoPlay>(*engine, juce::File(args[1].unquoted()), args[2].getDoubleValue());
            return;
        }

        const auto demoDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-spike-demo");

        if (args.size() >= 2 && args[0] == "--timeline-bench")
        {
            // Timeline only, scripted zoom/scroll, prints the frame rate and quits.
            timeline = spike::makeTimeline(*engine, spike::makeDemoData(demoDir));
            timeline->setSize(1400, 460);
            window = std::make_unique<SpikeWindow>(*timeline);
            bench = std::make_unique<spike::TimelineBench>(*timeline, args[1].getDoubleValue(),
                                                           [] { juce::JUCEApplication::quit(); });
            return;
        }

        if (args.size() >= 2 && args[0] == "--timeline-snapshot")
        {
            timeline = spike::makeTimeline(*engine, spike::makeDemoData(demoDir));
            timeline->setSize(1400, 460);
            if (args.size() >= 3)
                timeline->getViewport().pixelsPerBeat = args[2].getDoubleValue();
            window = std::make_unique<SpikeWindow>(*timeline);
            snapshot = std::make_unique<Snapshot>(*timeline, juce::File(args[1].unquoted()));
            return;
        }

        player = std::make_unique<PlayerComponent>(*engine);
        timeline = spike::makeTimeline(*engine, spike::makeDemoData(demoDir));
        tabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        tabs->addTab("Player", juce::Colours::darkgrey, player.get(), false);
        tabs->addTab("Timeline", juce::Colours::darkgrey, timeline.get(), false);
        tabs->setSize(1400, 560);
        window = std::make_unique<SpikeWindow>(*tabs);
    }

    void shutdown() override
    {
        autoPlay.reset();
        bench.reset();
        snapshot.reset();
        window.reset();
        tabs.reset();
        timeline.reset();
        player.reset();
        engine.reset();
    }

private:
    std::unique_ptr<sampler::EngineHost> engine;
    std::unique_ptr<PlayerComponent> player;
    std::unique_ptr<sampler::ui::TimelineComponent> timeline;
    std::unique_ptr<juce::TabbedComponent> tabs;
    std::unique_ptr<SpikeWindow> window;
    std::unique_ptr<spike::TimelineBench> bench;
    std::unique_ptr<Snapshot> snapshot;
    std::unique_ptr<AutoPlay> autoPlay;
};
} // namespace

START_JUCE_APPLICATION(SpikeApp)
