#include "LevelMeters.h"

#include "EngineSetup.h"

#include <map>

namespace te = tracktion::engine;

namespace sampler::engine
{
namespace
{
LevelReading readClient(te::LevelMeasurer::Client& client)
{
    LevelReading reading;
    for (int channel = 0; channel < 2; ++channel)
        reading.peakDb[channel] = client.getAndClearAudioLevel(channel).dB;
    if (client.getNumChannelsUsed() == 1)
        reading.peakDb[1] = reading.peakDb[0];
    client.getAndClearOverload(); // (it reports a request to clear, not an overload: the flag is derived from the peaks)
    reading.over = reading.peakDb[0] > 0.0f || reading.peakDb[1] > 0.0f;
    return reading;
}
} // namespace

struct LevelMeters::Impl
{
    explicit Impl(te::Edit& e) : edit(e) {}

    ~Impl()
    {
        for (auto& [id, meter] : tracks)
            meter.plugin->measurer.removeClient(*meter.client);
        if (masterContext != nullptr && masterContext == edit.getCurrentPlaybackContext())
            masterContext->masterLevels.removeClient(masterClient);
    }

    struct TrackMeter
    {
        te::Plugin::Ptr pluginHolder; // keeps the measurer alive until the client is removed
        te::LevelMeterPlugin* plugin = nullptr;
        std::unique_ptr<te::LevelMeasurer::Client> client;
    };

    te::LevelMeterPlugin* meterPluginFor(const juce::String& trackId) const
    {
        for (auto* t : te::getAudioTracks(edit))
            if (t->itemID.toString() == trackId)
                return t->getLevelMeterPlugin();
        return nullptr;
    }

    te::Edit& edit;
    std::map<juce::String, TrackMeter> tracks;
    te::LevelMeasurer::Client masterClient;
    te::EditPlaybackContext* masterContext = nullptr;
};

LevelMeters::LevelMeters(te::Edit& edit) : impl(std::make_unique<Impl>(edit)) {}
LevelMeters::~LevelMeters() = default;

LevelReading LevelMeters::readTrack(const juce::String& trackId)
{
    auto* plugin = impl->meterPluginFor(trackId);
    auto existing = impl->tracks.find(trackId);
    if (existing != impl->tracks.end() && existing->second.plugin != plugin)
    {
        existing->second.plugin->measurer.removeClient(*existing->second.client); // the track went (or its meter changed)
        impl->tracks.erase(existing);
        existing = impl->tracks.end();
    }
    if (plugin == nullptr)
        return {};
    if (existing == impl->tracks.end())
    {
        Impl::TrackMeter meter;
        meter.pluginHolder = plugin;
        meter.plugin = plugin;
        meter.client = std::make_unique<te::LevelMeasurer::Client>();
        plugin->measurer.addClient(*meter.client);
        existing = impl->tracks.emplace(trackId, std::move(meter)).first;
    }
    return readClient(*existing->second.client);
}

LevelReading LevelMeters::readMaster()
{
    auto* context = impl->edit.getCurrentPlaybackContext();
    if (context != impl->masterContext)
    {
        // A replaced context took its measurer (and our registration) with it.
        impl->masterClient.reset();
        impl->masterContext = context;
        if (context != nullptr)
            context->masterLevels.addClient(impl->masterClient);
    }
    if (context == nullptr)
        return {};
    return readClient(impl->masterClient);
}
} // namespace sampler::engine
