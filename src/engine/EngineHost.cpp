#include "EngineHost.h"

#include "EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
struct EngineHost::Impl
{
    explicit Impl(const juce::String& appName) : engine(detail::makeEngine(appName)) {}

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    double lengthSeconds = 0.0;
    bool outputMuted = false;
    float volumeBeforeMute = 0.0f;
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
    impl->outputMuted = false;
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

void EngineHost::setOutputMuted(bool muted)
{
    if (impl->edit == nullptr || muted == impl->outputMuted)
        return;

    if (auto master = impl->edit->getMasterVolumePlugin())
    {
        if (muted)
        {
            impl->volumeBeforeMute = master->getVolumeDb();
            master->setSliderPos(0.0f); // true silence, not -100 dB
        }
        else
        {
            master->setVolumeDb(impl->volumeBeforeMute);
        }
        impl->outputMuted = muted;
    }
}

bool EngineHost::isOutputMuted() const { return impl->outputMuted; }

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
