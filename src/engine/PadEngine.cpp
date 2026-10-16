#include "PadEngine.h"

#include "EngineHost.h"
#include "EngineSetup.h"
#include "SlicePadPlugin.h"

namespace te = tracktion::engine;

namespace sampler
{
struct PadEngine::Impl : private juce::Timer
{
    explicit Impl(EngineHost& h) : host(h)
    {
        auto& engine = host.tracktionEngine();
        edit = te::Edit::createSingleTrackEdit(engine);
        edit->setMasterVolumeSliderPos(te::decibelsToVolumeFaderPosition(0.0f));
        auto& master = edit->getMasterPluginList();
        while (master.size() > 0)
            master[0]->removeFromParent();

        if (auto tracks = te::getAudioTracks(*edit); tracks.size() > 0)
        {
            juce::ValueTree v(te::IDs::PLUGIN);
            v.setProperty(te::IDs::type, engine::SlicePadPlugin::xmlTypeName, nullptr);
            tracks.getFirst()->pluginList.state.addChild(v, -1, nullptr);
            plugin = dynamic_cast<engine::SlicePadPlugin*>(edit->getPluginCache().getPluginFor(v).get());
        }
        jassert(plugin != nullptr);

        allocateWhenReady();
        startTimerHz(20); // returns the references the audio thread has let go of to this thread
    }

    ~Impl() override
    {
        stopTimer();
        if (edit != nullptr)
            edit->getTransport().stop(false, false);
        plugin = nullptr;
        edit.reset();
    }

    /**
        The playback context needs an output device that has finished its start-up scan (before that the transport misbehaves,
        see docs/m0-findings.md), so a machine or a test run without audio never allocates it. Done again at every timer tick
        until it worked.
    */
    void allocateWhenReady()
    {
        if (allocated || edit == nullptr || ! host.isAudioReady())
            return;
        edit->getTransport().ensureContextAllocated();
        allocated = true;
    }

    void timerCallback() override
    {
        allocateWhenReady();
        if (plugin != nullptr)
            plugin->padMixer().drainGarbage();
    }

    EngineHost& host;
    std::unique_ptr<te::Edit> edit;
    engine::SlicePadPlugin* plugin = nullptr;
    bool allocated = false;
    bool keepPlaying = false;
};

PadEngine::PadEngine(EngineHost& host) : impl(std::make_unique<Impl>(host)) {}
PadEngine::~PadEngine() = default;

void PadEngine::padOn(int slot)
{
    if (impl->plugin != nullptr)
        impl->plugin->padMixer().pushEvent({engine::PadEvent::Type::on, slot, 1.0f});
}

void PadEngine::padOff(int slot)
{
    if (impl->plugin != nullptr)
        impl->plugin->padMixer().pushEvent({engine::PadEvent::Type::off, slot, 1.0f});
}

void PadEngine::allOff()
{
    if (impl->plugin != nullptr)
        impl->plugin->padMixer().pushEvent({engine::PadEvent::Type::allOff, 0, 1.0f});
}

void PadEngine::stopAll()
{
    if (impl->plugin != nullptr)
        impl->plugin->padMixer().pushEvent({engine::PadEvent::Type::stopAll, 0, 1.0f});
}

void PadEngine::setBank(render::SliceBank::Ptr bank)
{
    if (impl->plugin != nullptr)
        impl->plugin->padMixer().publishBank(std::move(bank));
}

void PadEngine::clearBank() { setBank(new render::SliceBank()); }

int PadEngine::soundingCount(int slot) const
{
    return impl->plugin != nullptr ? impl->plugin->padMixer().soundingCount(slot) : 0;
}

bool PadEngine::isAnySounding() const
{
    for (int slot = 0; slot < render::maxSlices; ++slot)
        if (soundingCount(slot) > 0)
            return true;
    return false;
}

long long PadEngine::blocksProcessed() const
{
    return impl->plugin != nullptr ? static_cast<long long>(impl->plugin->padMixer().blocksProcessed()) : 0;
}

bool PadEngine::usesKeepPlayingFallback() const { return impl->keepPlaying; }
} // namespace sampler
