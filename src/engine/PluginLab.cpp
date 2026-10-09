#include "PluginLab.h"

#include "EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
namespace
{
using ProcessCountFn = juce::int64 (*)();
using ResetCountFn = void (*)();

/** The module file inside a .vst3 bundle folder (or the file itself for a single-file plugin). */
juce::File moduleFileFor(const juce::File& vst3)
{
    if (vst3.isDirectory())
        return vst3.getChildFile("Contents").getChildFile("x86_64-win").getChildFile(vst3.getFileName());
    return vst3;
}
} // namespace

struct PluginLab::Impl
{
    explicit Impl(const juce::String& appName) : engine(detail::makeEngine(appName)) {}

    ~Impl()
    {
        closeEdit();
    }

    void closeEdit()
    {
        if (edit != nullptr)
        {
            edit->getTransport().stop(false, false);
            auto tracks = te::getAudioTracks(*edit);
            for (size_t i = 0; i < clients.size() && static_cast<int>(i) < tracks.size(); ++i)
                if (auto* meter = tracks[static_cast<int>(i)]->getLevelMeterPlugin())
                    meter->measurer.removeClient(*clients[i]);
            edit->getTransport().freePlaybackContext();
        }
        clients.clear();
        edit.reset();
        plugin = nullptr;
    }

    te::ExternalPlugin* findPlugin() const
    {
        if (edit == nullptr)
            return nullptr;
        for (auto p : edit->getPluginCache().getPlugins())
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(&*p))
                return ext;
        return nullptr;
    }

    /** Reads the VST3 at a fixed path and adds it to the engine's known-plugin list (Tracktion finds plugins there; a scan would fill it). */
    bool registerPlugin(const juce::File& pluginFile, juce::PluginDescription* out = nullptr)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* format : engine->getPluginManager().pluginFormatManager.getFormats())
            if (format->getName() == "VST3")
                format->findAllTypesForFile(found, pluginFile.getFullPathName());
        if (found.isEmpty())
            return false;
        engine->getPluginManager().knownPluginList.addType(*found.getFirst());
        if (out != nullptr)
            *out = *found.getFirst();
        return true;
    }

    void attachModule(const juce::File& vst3)
    {
        library = std::make_unique<juce::DynamicLibrary>();
        library->open(moduleFileFor(vst3).getFullPathName());
        countFn = reinterpret_cast<ProcessCountFn>(library->getFunction("sampler_testplugin_processBlockCount"));
        resetFn = reinterpret_cast<ResetCountFn>(library->getFunction("sampler_testplugin_resetProcessBlockCount"));
    }

    void attachMeters()
    {
        clients.clear();
        for (auto* track : te::getAudioTracks(*edit))
        {
            auto client = std::make_unique<te::LevelMeasurer::Client>();
            if (auto* meter = track->getLevelMeterPlugin())
                meter->measurer.addClient(*client);
            clients.push_back(std::move(client));
        }
    }

    /** Runs the message loop until `done()` or the timeout. */
    template <typename Pred>
    static bool waitFor(Pred done, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
        while (!done())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
        return true;
    }

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<te::Edit> edit;
    te::ExternalPlugin* plugin = nullptr;
    std::unique_ptr<juce::DynamicLibrary> library;
    ProcessCountFn countFn = nullptr;
    ResetCountFn resetFn = nullptr;
    std::vector<std::unique_ptr<te::LevelMeasurer::Client>> clients;
    float masterDbBefore = 0.0f;
};

PluginLab::PluginLab(const juce::String& appName) : impl(std::make_unique<Impl>(appName)) {}
PluginLab::~PluginLab() = default;

juce::Result PluginLab::newEdit(const juce::File& editFile, const juce::File& pluginFile, Layout layout, int numTracks)
{
    impl->closeEdit();
    impl->attachModule(pluginFile);

    juce::PluginDescription description;
    if (!impl->registerPlugin(pluginFile, &description))
        return juce::Result::fail("no VST3 plugin found in " + pluginFile.getFullPathName());

    impl->edit = te::createEmptyEdit(*impl->engine, editFile);
    auto& edit = *impl->edit;
    edit.ensureNumberOfAudioTracks(numTracks);
    edit.tempoSequence.getTempo(0)->setBpm(120.0);

    auto created = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, description);
    auto* external = dynamic_cast<te::ExternalPlugin*>(created.get());
    if (external == nullptr)
        return juce::Result::fail("could not create the plugin");
    external->initialiseFully();
    if (external->getAudioPluginInstance() == nullptr)
        return juce::Result::fail("plugin did not load: " + external->getLoadError());

    auto tracks = te::getAudioTracks(edit);
    if (layout == Layout::direct)
    {
        tracks.getFirst()->pluginList.insertPlugin(created, 0, nullptr);
    }
    else
    {
        te::Plugin::Array wrapped;
        wrapped.add(created);
        auto rack = te::RackType::createTypeToWrapPlugins(wrapped, edit);
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto instance = tracks[i]->pluginList.insertPlugin(te::RackInstance::create(*rack), 0);
            if (auto* ri = dynamic_cast<te::RackInstance*>(instance.get()))
            {
                // Rack pin 0 is MIDI; audio output pins are numbered from 1.
                ri->leftOutputComesFrom = 2 * i + 1;
                ri->rightOutputComesFrom = 2 * i + 2;
            }
        }
    }

    impl->plugin = external;
    impl->attachMeters();
    return detail::saveEditAtomically(edit) ? juce::Result::ok() : juce::Result::fail("could not save the edit");
}

juce::Result PluginLab::open(const juce::File& editFile, const juce::File& pluginFile)
{
    impl->closeEdit();
    impl->attachModule(pluginFile);
    impl->registerPlugin(pluginFile);
    impl->edit = te::loadEditFromFile(*impl->engine, editFile);
    if (impl->edit == nullptr)
        return juce::Result::fail("could not open " + editFile.getFullPathName());

    impl->plugin = impl->findPlugin();
    if (impl->plugin == nullptr)
        return juce::Result::fail("no plugin in the edit");
    impl->plugin->initialiseFully();
    if (impl->plugin->getAudioPluginInstance() == nullptr)
        return juce::Result::fail("plugin did not load: " + impl->plugin->getLoadError());
    impl->attachMeters();
    return juce::Result::ok();
}

bool PluginLab::save() { return impl->edit != nullptr && detail::saveEditAtomically(*impl->edit); }

void PluginLab::closeEdit() { impl->closeEdit(); }

int PluginLab::numTracks() const { return impl->edit != nullptr ? te::getAudioTracks(*impl->edit).size() : 0; }

juce::String PluginLab::pluginName() const { return impl->plugin != nullptr ? impl->plugin->getName() : juce::String(); }

int PluginLab::pluginOutputChannels() const
{
    if (impl->plugin != nullptr)
        if (auto* pi = impl->plugin->getAudioPluginInstance())
            return pi->getTotalNumOutputChannels();
    return 0;
}

int PluginLab::numPluginInstances() const
{
    int count = 0;
    if (impl->edit != nullptr)
        for (auto p : impl->edit->getPluginCache().getPlugins())
            if (dynamic_cast<te::ExternalPlugin*>(&*p) != nullptr)
                ++count;
    return count;
}

bool PluginLab::setParameter(const juce::String& name, float normalised)
{
    if (impl->plugin != nullptr)
        if (auto* pi = impl->plugin->getAudioPluginInstance())
            for (auto* p : pi->getParameters())
                if (p->getName(64) == name)
                {
                    p->setValue(normalised);
                    return true;
                }
    return false;
}

float PluginLab::parameter(const juce::String& name) const
{
    if (impl->plugin != nullptr)
        if (auto* pi = impl->plugin->getAudioPluginInstance())
            for (auto* p : pi->getParameters())
                if (p->getName(64) == name)
                    return p->getValue();
    return -1.0f;
}

std::unique_ptr<juce::AudioProcessorEditor> PluginLab::createEditor()
{
    if (impl->plugin != nullptr)
        if (auto* pi = impl->plugin->getAudioPluginInstance())
            if (pi->hasEditor())
                return std::unique_ptr<juce::AudioProcessorEditor>(pi->createEditorIfNeeded());
    return {};
}

bool PluginLab::renderToWav(const juce::File& dest, double seconds, int onlyTrack, double sampleRate, int blockSize)
{
    if (impl->edit == nullptr)
        return false;

    dest.deleteFile();
    te::Renderer::Parameters params(*impl->edit);
    params.destFile = dest;
    if (onlyTrack >= 0)
    {
        auto tracks = te::getAudioTracks(*impl->edit);
        if (!juce::isPositiveAndBelow(onlyTrack, tracks.size()))
            return false;
        juce::BigInteger bits;
        bits.setBit(tracks[onlyTrack]->getIndexInEditTrackList());
        params.tracksToDo = bits;
    }
    else
    {
        params.tracksToDo = te::toBitSet(te::getAllTracks(*impl->edit));
    }
    params.audioFormat = impl->engine->getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio = blockSize;
    params.canRenderInMono = false;
    params.time = tracktion::TimeRange(tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds(seconds));
    return te::Renderer::renderToFile("Sampler render", params).existsAsFile();
}

juce::int64 PluginLab::processBlockCount() const { return impl->countFn != nullptr ? impl->countFn() : -1; }

void PluginLab::resetProcessBlockCount()
{
    if (impl->resetFn != nullptr)
        impl->resetFn();
}

bool PluginLab::waitForOutputDevice(int timeoutMs)
{
    return Impl::waitFor([&] { return impl->engine->getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr; },
                         timeoutMs);
}

void PluginLab::play()
{
    if (impl->edit != nullptr)
    {
        impl->edit->getTransport().ensureContextAllocated();
        impl->edit->getTransport().setPosition(tracktion::TimePosition());
        impl->edit->getTransport().play(false);
    }
}

void PluginLab::stop()
{
    if (impl->edit != nullptr)
        impl->edit->getTransport().stop(false, false);
}

bool PluginLab::isPlaying() const { return impl->edit != nullptr && impl->edit->getTransport().isPlaying(); }

double PluginLab::positionSeconds() const
{
    return impl->edit != nullptr ? impl->edit->getTransport().getPosition().inSeconds() : 0.0;
}

float PluginLab::takeTrackPeak(int track)
{
    if (!juce::isPositiveAndBelow(track, static_cast<int>(impl->clients.size())))
        return 0.0f;
    auto& client = *impl->clients[static_cast<size_t>(track)];
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax(peak, juce::Decibels::decibelsToGain(client.getAndClearAudioLevel(ch).dB, -100.0f));
    return peak;
}

void PluginLab::setMasterGainDb(float db)
{
    if (impl->edit != nullptr)
        if (auto master = impl->edit->getMasterVolumePlugin())
            master->setVolumeDb(db);
}

juce::String PluginLab::describeAudioSetup() const
{
    auto& dm = impl->engine->getDeviceManager();
    juce::String text;
    if (auto* device = dm.deviceManager.getCurrentAudioDevice())
        text << device->getTypeName() << " / " << device->getName() << ", rate " << device->getCurrentSampleRate()
             << ", buffer " << device->getCurrentBufferSizeSamples();
    else
        text << "no output device";
    return text;
}
} // namespace sampler
