#include "EditTakeRecorder.h"

#include "TakeTapPlugin.h"

namespace te = tracktion::engine;

namespace sampler::engine
{
namespace
{
class EngineTap final : public TakeTap
{
public:
    EngineTap(te::PluginList& l, te::Plugin::Ptr p, TakeTapPlugin& t, juce::ValueTree s)
        : list(l), holder(std::move(p)), tap(t), state(std::move(s))
    {
    }

    ~EngineTap() override { detach(); }

    const platform::TakeWriter& take() const override { return tap.writer(); }
    double firstFrameSeconds() const override { return tap.firstFrameSeconds(); }
    bool isReady() const override { return tap.isReady(); }
    bool endedByRateChange() const override { return tap.endedByRateChange(); }

    void detach() override
    {
        if (detached)
            return;
        detached = true;
        tap.detach();
        list.state.removeChild(state, nullptr); // not an undo step
    }

    platform::TakeResult finish() override
    {
        tap.detach(); // a no-op after the first call
        if (tap.detachTimedOut())
        {
            // The audio thread may still be inside the writer: finishing it now would race. The take is reported as
            // failed and its file is left as it is.
            platform::TakeResult failed;
            failed.file = tap.writer().options().file;
            failed.sampleRate = tap.writer().options().sampleRate;
            failed.channels = tap.writer().options().channels;
            failed.writeFailed = true;
            return failed;
        }
        return tap.writer().finish();
    }

private:
    te::PluginList& list;
    te::Plugin::Ptr holder; // keeps the plugin (and the take inside it) alive after it left the edit
    TakeTapPlugin& tap;
    juce::ValueTree state;
    bool detached = false;
};

std::unique_ptr<TakeTap> addTap(te::Edit& edit, te::PluginList& list, const platform::TakeOptions& options, juce::String& error,
                                  te::VolumeAndPanPlugin::Ptr followVolume = {})
{
    juce::ValueTree v(te::IDs::PLUGIN);
    v.setProperty(te::IDs::type, TakeTapPlugin::xmlTypeName, nullptr);
    v.setProperty(TakeTapPlugin::tapProperty, true, nullptr);
    list.state.addChild(v, -1, nullptr); // no undo manager: recording leaves the history alone

    auto plugin = edit.getPluginCache().getPluginFor(v);
    auto* tap = dynamic_cast<TakeTapPlugin*>(plugin.get());
    if (tap == nullptr)
    {
        list.state.removeChild(v, nullptr);
        error = "the recording tap could not be added";
        return nullptr;
    }
    tap->followMasterVolume(std::move(followVolume));
    tap->configure(options);
    return std::make_unique<EngineTap>(list, plugin, *tap, v);
}
} // namespace

EditTakeRecorder::EditTakeRecorder(te::Edit& e) : edit(e) {}

std::unique_ptr<TakeTap> EditTakeRecorder::tapTrack(const juce::String& trackId, const platform::TakeOptions& options, juce::String& error)
{
    for (auto* track : te::getAudioTracks(edit))
        if (track->itemID.toString() == trackId)
            return addTap(edit, track->pluginList, options, error);
    error = "the track is gone";
    return nullptr;
}

std::unique_ptr<TakeTap> EditTakeRecorder::tapMaster(const platform::TakeOptions& options, juce::String& error)
{
    return addTap(edit, edit.getMasterPluginList(), options, error, edit.getMasterVolumePlugin());
}
} // namespace sampler::engine
