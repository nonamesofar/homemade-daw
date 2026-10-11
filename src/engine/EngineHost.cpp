#include "EngineHost.h"

#include "EngineSetup.h"

#include <atomic>

namespace te = tracktion::engine;

namespace sampler
{
namespace
{
/**
    Runs on the audio thread after the engine has filled the device output (te::DeviceManager's global output
    processor) and silences it while `muted` is set. Lives outside the edit, so muting never touches the document,
    the undo history or a saved project, and it outlasts loading another edit. No allocation and no locks in
    processBlock: one atomic load and a clear.
*/
class OutputMuteProcessor final : public juce::AudioProcessor
{
public:
    std::atomic<bool> muted{false};

    const juce::String getName() const override { return "Sampler output mute"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (!muted.load(std::memory_order_relaxed))
            return;
        auto* const* channels = buffer.getArrayOfWritePointers();
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            if (channels[c] != nullptr)
                juce::FloatVectorOperations::clear(channels[c], buffer.getNumSamples());
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};
} // namespace

struct EngineHost::Impl : private juce::Timer
{
    Impl(EngineHost& o, const juce::String& appName, bool noDevice)
        : owner(o), engine(detail::makeEngine(appName, noDevice)), headless(noDevice)
    {
        // The device manager owns it; it lives as long as the engine. Installing it takes the audio callback lock.
        auto processor = std::make_unique<OutputMuteProcessor>();
        mute = processor.get();
        engine->getDeviceManager().setGlobalOutputAudioProcessor(std::move(processor));
        startedAtMs = juce::Time::getMillisecondCounter();
        if (! headless)
            startTimer(50);
    }

    ~Impl() override { stopTimer(); }

    /** An output device is open and the engine knows an enabled default wave output (its start-up scan has run). */
    bool deviceUsable() const
    {
        auto& dm = engine->getDeviceManager();
        if (dm.deviceManager.getCurrentAudioDevice() == nullptr)
            return false;
        auto* out = dm.getDefaultWaveOutDevice();
        return out != nullptr && out->isEnabled();
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (! deviceUsable())
        {
            usableSinceMs = 0;
            return;
        }
        if (usableSinceMs == 0)
            usableSinceMs = now;
        // A short settle after the scan result appears, then the optional developer hold.
        if (now - usableSinceMs < settleMs || now - startedAtMs < holdMs)
            return;

        ready = true;
        stopTimer();
        if (owner.onAudioReady)
            owner.onAudioReady();
    }

    static constexpr juce::uint32 settleMs = 300;

    EngineHost& owner;
    std::unique_ptr<te::Engine> engine;
    OutputMuteProcessor* mute = nullptr;
    bool headless = false;
    bool ready = false;
    juce::uint32 startedAtMs = 0, usableSinceMs = 0, holdMs = 0;
};

EngineHost::EngineHost(const juce::String& appName, bool headless) : impl(std::make_unique<Impl>(*this, appName, headless)) {}

bool EngineHost::isAudioReady() const { return impl->ready; }
bool EngineHost::isHeadless() const { return impl->headless; }

void EngineHost::holdAudioReadyFor(int milliseconds) { impl->holdMs = static_cast<juce::uint32>(juce::jmax(0, milliseconds)); }

te::Engine& EngineHost::tracktionEngine() const { return *impl->engine; }

EngineHost::~EngineHost() = default;

void EngineHost::setOutputMuted(bool muted) { impl->mute->muted.store(muted); }

bool EngineHost::isOutputMuted() const { return impl->mute->muted.load(); }

std::unique_ptr<juce::AudioThumbnailBase> EngineHost::createThumbnail(const juce::File& file,
                                                                      juce::Component& repaintTarget) const
{
    return std::make_unique<te::SmartThumbnail>(*impl->engine, te::AudioFile(*impl->engine, file), repaintTarget, nullptr);
}

std::unique_ptr<juce::AudioFormatReader> EngineHost::openReader(const juce::File& file) const
{
    // Same lookup the engine itself uses when it opens a clip's source.
    return std::unique_ptr<juce::AudioFormatReader>(
        impl->engine->getAudioFileFormatManager().readFormatManager.createReaderFor(file));
}

DecodeInfo EngineHost::probeDecode(const juce::File& file) const
{
    DecodeInfo info;
    auto reader = openReader(file);
    if (reader == nullptr)
        return info;

    info.ok = true;
    info.lengthInFrames = reader->lengthInSamples;
    info.sampleRate = reader->sampleRate;
    info.channels = static_cast<int>(reader->numChannels);
    info.formatName = reader->getFormatName();
    return info;
}

double EngineHost::currentSampleRate() const
{
    if (auto* device = impl->engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();
    return 0.0;
}

int EngineHost::currentBufferSize() const
{
    if (auto* device = impl->engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();
    return 0;
}

juce::String EngineHost::currentAudioDeviceType() const
{
    return impl->engine->getDeviceManager().deviceManager.getCurrentAudioDeviceType();
}

juce::String EngineHost::currentAudioDeviceName() const
{
    if (auto* device = impl->engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getName();
    return {};
}

juce::String EngineHost::describeAudioSetup() const
{
    auto& dm = impl->engine->getDeviceManager();
    juce::String text;
    text << "sampleRate=" << dm.getSampleRate() << " blockSize=" << dm.getBlockSize() << "\n";
    for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
        if (auto* out = dm.getWaveOutDevice(i))
            text << "waveOut[" << i << "] " << out->getName() << " enabled=" << (out->isEnabled() ? "yes" : "no") << "\n";
    text << "defaultWaveOut=" << (dm.getDefaultWaveOutDevice() != nullptr ? dm.getDefaultWaveOutDevice()->getName() : "none")
         << "\n";
    if (auto* device = dm.deviceManager.getCurrentAudioDevice())
        text << "device: " << device->getName() << " outChannels=" << device->getActiveOutputChannels().countNumberOfSetBits()
             << " bufferSize=" << device->getCurrentBufferSizeSamples() << " rate=" << device->getCurrentSampleRate() << "\n";
    return text;
}

} // namespace sampler
