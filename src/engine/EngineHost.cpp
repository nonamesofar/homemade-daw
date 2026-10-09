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

struct EngineHost::Impl
{
    explicit Impl(const juce::String& appName) : engine(detail::makeEngine(appName))
    {
        // The device manager owns it; it lives as long as the engine. Installing it takes the audio callback lock.
        auto processor = std::make_unique<OutputMuteProcessor>();
        mute = processor.get();
        engine->getDeviceManager().setGlobalOutputAudioProcessor(std::move(processor));
    }

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    OutputMuteProcessor* mute = nullptr;
    double lengthSeconds = 0.0;
};

EngineHost::EngineHost(const juce::String& appName) : impl(std::make_unique<Impl>(appName)) {}

EngineHost::~EngineHost()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);
}

bool EngineHost::loadFile(const juce::File& file)
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);

    impl->edit.reset();
    impl->lengthSeconds = 0.0;

    te::AudioFile audioFile(*impl->engine, file);
    if (!audioFile.isValid())
        return false;

    auto edit = te::Edit::createSingleTrackEdit(*impl->engine);
    auto tracks = te::getAudioTracks(*edit);
    if (tracks.isEmpty())
        return false;

    const auto length = audioFile.getLength();
    auto clip = tracks.getFirst()->insertWaveClip(
        file.getFileNameWithoutExtension(), file,
        {{tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds(length)}, {}}, false);
    if (clip == nullptr)
        return false;

    impl->lengthSeconds = length;
    impl->edit = std::move(edit);
    impl->edit->getTransport().ensureContextAllocated();
    return true;
}

double EngineHost::loadedLengthSeconds() const { return impl->lengthSeconds; }

void EngineHost::play()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().play(false);
}

void EngineHost::stop()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);
}

bool EngineHost::isPlaying() const { return impl->edit != nullptr && impl->edit->getTransport().isPlaying(); }

double EngineHost::positionSeconds() const
{
    return impl->edit != nullptr ? impl->edit->getTransport().getPosition().inSeconds() : 0.0;
}

void EngineHost::setPositionSeconds(double s)
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().setPosition(tracktion::TimePosition::fromSeconds(s));
}

void EngineHost::setTempo(double bpm)
{
    if (impl->edit != nullptr)
        impl->edit->tempoSequence.getTempo(0)->setBpm(bpm);
}

void EngineHost::setOutputMuted(bool muted) { impl->mute->muted.store(muted); }

bool EngineHost::isOutputMuted() const { return impl->mute->muted.load(); }

float EngineHost::masterVolumeDb() const
{
    if (impl->edit != nullptr)
        if (auto master = impl->edit->getMasterVolumePlugin())
            return master->getVolumeDb();
    return 0.0f;
}

juce::String EngineHost::editStateXml() const
{
    return impl->edit != nullptr ? impl->edit->state.toXmlString() : juce::String();
}

juce::String EngineHost::undoHistoryDescription() const
{
    if (impl->edit == nullptr)
        return {};
    auto& undo = impl->edit->getUndoManager();
    return undo.getUndoDescriptions().joinIntoString("|") + " pending=" + juce::String(undo.getNumActionsInCurrentTransaction());
}

bool EngineHost::renderToWav(const juce::File& dest, double startSeconds, double lengthSeconds, double sampleRate)
{
    if (impl->edit == nullptr)
        return false;

    dest.deleteFile();

    te::Renderer::Parameters params(*impl->edit);
    params.destFile = dest;
    params.tracksToDo = te::toBitSet(te::getAllTracks(*impl->edit));
    params.audioFormat = impl->engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = 512;
    params.canRenderInMono = false;
    params.time = tracktion::TimeRange(tracktion::TimePosition::fromSeconds(startSeconds),
                                       tracktion::TimeDuration::fromSeconds(lengthSeconds));

    return te::Renderer::renderToFile("Sampler render", params).existsAsFile();
}

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

juce::String EngineHost::describeTransport() const
{
    if (impl->edit == nullptr)
        return "no edit";
    auto& t = impl->edit->getTransport();
    return juce::String("playing=") + (t.isPlaying() ? "1" : "0") + " stopping=" + (t.isStopping() ? "1" : "0")
           + " context=" + (t.isPlayContextActive() ? "1" : "0") + " pos=" + juce::String(t.getPosition().inSeconds(), 3)
           + " cpu=" + juce::String(impl->engine->getDeviceManager().getCpuUsage(), 3);
}

int EngineHost::xrunCount() const
{
    if (auto* device = impl->engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
        return device->getXRunCount();
    return -1;
}
} // namespace sampler
