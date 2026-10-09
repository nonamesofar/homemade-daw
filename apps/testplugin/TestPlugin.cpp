#include "TestPlugin.h"

namespace
{
std::atomic<int64_t> processBlockCount { 0 };

float oscillator(int waveform, double phase) // phase in cycles
{
    const auto p = static_cast<float>(phase - std::floor(phase));
    switch (waveform)
    {
        case 1: return 4.0f * std::abs(p - 0.5f) - 1.0f;
        case 2: return p < 0.5f ? 1.0f : -1.0f;
        case 3: return 2.0f * p - 1.0f;
        default: return std::sin(juce::MathConstants<float>::twoPi * p);
    }
}
} // namespace

// Counters the host test reads through the loaded module, to prove how often the host calls processBlock.
// The test plugin is Windows-only (it stands in for a VST3 on the dev machine); this is the one place that spells
// the export attribute. On another platform it would be __attribute__((visibility("default"))).
#define SAMPLER_TESTPLUGIN_EXPORT extern "C" __declspec(dllexport)

SAMPLER_TESTPLUGIN_EXPORT int64_t sampler_testplugin_processBlockCount() { return processBlockCount.load(); }
SAMPLER_TESTPLUGIN_EXPORT void sampler_testplugin_resetProcessBlockCount() { processBlockCount.store(0); }

//==============================================================================
class TestPluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TestPluginEditor(TestPluginProcessor& p) : juce::AudioProcessorEditor(p), proc(p)
    {
        setSize(360, 200);

        for (auto* s : { &tuneSlider, &levelSlider })
        {
            s->setSliderStyle(juce::Slider::LinearHorizontal);
            s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
            addAndMakeVisible(*s);
        }

        tuneSlider.setRange(0.5, 2.0, 0.01);
        tuneSlider.setValue(proc.tune->get(), juce::dontSendNotification);
        tuneSlider.onValueChange = [this] { *proc.tune = static_cast<float>(tuneSlider.getValue()); };

        levelSlider.setRange(0.0, 1.0, 0.01);
        levelSlider.setValue(proc.level->get(), juce::dontSendNotification);
        levelSlider.onValueChange = [this] { *proc.level = static_cast<float>(levelSlider.getValue()); };

        waveButton.onClick = [this]
        {
            *proc.waveform = (proc.waveform->get() + 1) % 4;
            updateText();
        };
        addAndMakeVisible(waveButton);
        updateText();
        startTimerHz(10);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff20242b));
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("Tune", 10, 12, 60, 24, juce::Justification::centredLeft);
        g.drawText("Level", 10, 44, 60, 24, juce::Justification::centredLeft);
        g.drawText(info, 10, 130, getWidth() - 20, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        tuneSlider.setBounds(70, 12, getWidth() - 80, 24);
        levelSlider.setBounds(70, 44, getWidth() - 80, 24);
        waveButton.setBounds(10, 86, 160, 28);
    }

private:
    void timerCallback() override
    {
        // Keep the controls in step with the parameters (they can change from the host or from loaded state).
        tuneSlider.setValue(proc.tune->get(), juce::dontSendNotification);
        levelSlider.setValue(proc.level->get(), juce::dontSendNotification);
        updateText();
        repaint();
    }

    void updateText()
    {
        static const char* names[] = { "sine", "triangle", "square", "saw" };
        waveButton.setButtonText(juce::String("Waveform: ") + names[proc.waveform->get() % 4]);
        info = "Blocks processed: " + juce::String(processBlockCount.load());
    }

    TestPluginProcessor& proc;
    juce::Slider tuneSlider, levelSlider;
    juce::TextButton waveButton;
    juce::String info;
};

//==============================================================================
TestPluginProcessor::TestPluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Out 1", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Out 4", juce::AudioChannelSet::stereo(), true))
{
    addParameter(tune = new juce::AudioParameterFloat({ "tune", 1 }, "Tune", 0.5f, 2.0f, 1.0f));
    addParameter(level = new juce::AudioParameterFloat({ "level", 1 }, "Level", 0.0f, 1.0f, 0.5f));
    addParameter(waveform = new juce::AudioParameterInt({ "wave", 1 }, "Waveform", 0, 3, 0));
}

void TestPluginProcessor::prepareToPlay(double sr, int) { sampleRate = sr; }

bool TestPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    for (const auto& bus : layouts.outputBuses)
        if (!bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void TestPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    ++processBlockCount;
    buffer.clear();

    double ppq = 0.0, bpm = 120.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            ppq = pos->getPpqPosition().orFallback(0.0);
            bpm = pos->getBpm().orFallback(120.0);
        }

    if (!playing)
        return;

    const auto ppqPerSample = bpm / 60.0 / sampleRate;
    const auto secondsPerBeat = 60.0 / bpm;
    const auto freqScale = static_cast<double>(tune->get());
    const auto gain = level->get();
    const auto wave = waveform->get();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto position = ppq + i * ppqPerSample;
        if (position < 0.0)
            continue; // pre-roll or latency compensation: no beat has started yet (and a negative beat has no output)
        const auto beat = std::floor(position);
        const auto t = (position - beat) * secondsPerBeat; // seconds since the beat started
        if (t >= burstSeconds)
            continue;

        const auto output = static_cast<int>(static_cast<int64_t>(beat) % numOutputs);
        const auto envelope = static_cast<float>(1.0 - t / burstSeconds);
        const auto value = gain * envelope * oscillator(wave, 220.0 * (output + 1) * freqScale * t);

        const auto firstChannel = output * 2;
        if (firstChannel + 1 < buffer.getNumChannels())
        {
            buffer.setSample(firstChannel, i, value);
            buffer.setSample(firstChannel + 1, i, value);
        }
    }
}

juce::AudioProcessorEditor* TestPluginProcessor::createEditor() { return new TestPluginEditor(*this); }

void TestPluginProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::XmlElement xml("TESTPLUGIN");
    xml.setAttribute("tune", static_cast<double>(tune->get()));
    xml.setAttribute("level", static_cast<double>(level->get()));
    xml.setAttribute("wave", waveform->get());
    copyXmlToBinary(xml, dest);
}

void TestPluginProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("TESTPLUGIN"))
        {
            *tune = static_cast<float>(xml->getDoubleAttribute("tune", 1.0));
            *level = static_cast<float>(xml->getDoubleAttribute("level", 0.5));
            *waveform = xml->getIntAttribute("wave", 0);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestPluginProcessor(); }
