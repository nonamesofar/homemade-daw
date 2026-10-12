#include "TakeTapPlugin.h"

#include "platform/RealtimeGuard.h"

#include <cmath>

namespace te = tracktion::engine;

namespace sampler::engine
{
const char* TakeTapPlugin::xmlTypeName = "samplerTap";
const juce::Identifier TakeTapPlugin::tapProperty("sampler_tap");

namespace
{
constexpr int scratchFrames = 4096;
constexpr double gainRampSeconds = 0.05; // as VolumeAndPanPlugin::smoothingRampTimeSeconds

void masterGains(const te::VolumeAndPanPlugin& plugin, float& left, float& right)
{
    te::getGainsFromVolumeFaderPositionAndPan(plugin.getSliderPos(), plugin.getPan(), plugin.getPanLaw(), left, right);
    if (plugin.polarity)
    {
        left = -left;
        right = -right;
    }
}
} // namespace

TakeTapPlugin::TakeTapPlugin(te::PluginCreationInfo info) : te::Plugin(info) { scratch.setSize(2, scratchFrames); }

TakeTapPlugin::~TakeTapPlugin()
{
    notifyListenersOfDeletion();
}

void TakeTapPlugin::configure(const platform::TakeOptions& options)
{
    pending = options;
    configured = true;
}

void TakeTapPlugin::followMasterVolume(te::VolumeAndPanPlugin::Ptr plugin) { masterVolume = std::move(plugin); }

void TakeTapPlugin::open(double rate)
{
    if (take.isOpen())
    {
        // The graph was rebuilt for another sample rate: blocks no longer match the file, so the take stops here.
        if (rate > 0.0 && std::abs(rate - rateHz.load(std::memory_order_relaxed)) > 0.5)
            rateChanged.store(true, std::memory_order_release);
        return;
    }
    if (! configured || rate <= 0.0)
        return;
    auto options = pending;
    options.sampleRate = rate;
    options.channels = 2;
    options.fillDroppedWithSilence = true; // Master and track takes stay on the timeline
    rateHz.store(rate, std::memory_order_relaxed);
    if (masterVolume != nullptr)
    {
        float left = 1.0f, right = 1.0f;
        masterGains(*masterVolume, left, right);
        gainL.reset(rate, gainRampSeconds);
        gainR.reset(rate, gainRampSeconds);
        gainL.setCurrentAndTargetValue(left);
        gainR.setCurrentAndTargetValue(right);
    }
    if (take.open(options).wasOk())
        active.store(true, std::memory_order_release);
}

void TakeTapPlugin::initialise(const te::PluginInitialisationInfo& info) { open(info.sampleRate); }
void TakeTapPlugin::initialiseWithoutStopping(const te::PluginInitialisationInfo& info) { open(info.sampleRate); }

void TakeTapPlugin::detach()
{
    // Once only, on the message thread (the first call): later calls (e.g. from the finishing worker) change nothing.
    if (detachDone.exchange(true, std::memory_order_acq_rel))
        return;
    // Sequentially consistent: either the audio thread sees `active` false, or this thread sees its `inCall` count.
    active.store(false, std::memory_order_seq_cst);
    // A block may be in flight on the audio thread: wait until it is done (a block is a few milliseconds).
    for (int i = 0; i < 2000 && inCall.load(std::memory_order_seq_cst) != 0; ++i)
        juce::Thread::sleep(1);
    if (inCall.load(std::memory_order_seq_cst) != 0)
    {
        timedOut.store(true, std::memory_order_release);
        DBG("TakeTapPlugin::detach timed out waiting for the audio thread");
        jassertfalse;
        return;
    }
    masterVolume = nullptr; // no block is running any more: let go of the master fader's plugin
}

void TakeTapPlugin::pushWithMasterGain(const float* const* channels, int numChannels, int frames, int offset, juce::int64 timelineFrame)
{
    float left = 1.0f, right = 1.0f;
    masterGains(*masterVolume, left, right);
    gainL.setTargetValue(left);
    gainR.setTargetValue(right);

    const int out = juce::jmin(2, numChannels);
    for (int done = 0; done < frames; done += scratchFrames)
    {
        const int n = juce::jmin(scratchFrames, frames - done);
        for (int i = 0; i < n; ++i)
        {
            const auto l = gainL.getNextValue();
            const auto r = gainR.getNextValue();
            scratch.setSample(0, i, channels[0][offset + done + i] * l);
            if (out > 1)
                scratch.setSample(1, i, channels[1][offset + done + i] * r);
        }
        take.pushPlanarAt(scratch.getArrayOfReadPointers(), out, n, 0, timelineFrame + done);
    }
}

void TakeTapPlugin::applyToBuffer(const te::PluginRenderContext& context)
{
    if (! active.load(std::memory_order_acquire) || context.destBuffer == nullptr || ! context.isPlaying
        || rateChanged.load(std::memory_order_acquire))
        return;
    const platform::RealtimeScope realtime;

    inCall.fetch_add(1, std::memory_order_seq_cst);
    if (active.load(std::memory_order_seq_cst))
    {
        // Before time zero there is nothing to record (an offline render pre-rolls there; live playback never does).
        const auto start = context.editTime.getStart().inSeconds();
        const auto rate = rateHz.load(std::memory_order_relaxed);
        int skip = 0;
        if (start < 0.0)
            skip = static_cast<int>(std::lround(-start * rate));
        if (skip < context.bufferNumSamples)
        {
            if (firstSeconds.load(std::memory_order_relaxed) < 0.0)
                firstSeconds.store(std::max(0.0, start), std::memory_order_release);
            // The block position lets the take notice blocks the engine skipped (a muted track) and fill them with silence.
            const auto timelineFrame = static_cast<juce::int64>(std::llround(std::max(0.0, start) * rate));
            if (masterVolume != nullptr && masterVolume->isEnabled())
                pushWithMasterGain(context.destBuffer->getArrayOfReadPointers(), context.destBuffer->getNumChannels(),
                                   context.bufferNumSamples - skip, context.bufferStartSample + skip, timelineFrame);
            else
                take.pushPlanarAt(context.destBuffer->getArrayOfReadPointers(), context.destBuffer->getNumChannels(),
                                  context.bufferNumSamples - skip, context.bufferStartSample + skip, timelineFrame);
        }
    }
    inCall.fetch_sub(1, std::memory_order_seq_cst);
}
} // namespace sampler::engine
