#include "engine/PadMixer.h"

#include "platform/RealtimeGuard.h"

#include <algorithm>
#include <cmath>

namespace sampler::engine
{
namespace
{
int samplesFor(double milliseconds, double rate) { return std::max(1, static_cast<int>(std::lround(milliseconds * rate / 1000.0))); }
} // namespace

PadMixer::PadMixer()
{
    for (auto& c : counts)
        c.store(0);
    for (auto& f : offFlags)
        f.store(false);
    prepare(rate);
}

PadMixer::~PadMixer()
{
    if (auto* next = incoming.exchange(nullptr))
        next->decReferenceCount();
    if (current != nullptr)
        current->decReferenceCount();
    for (auto& voice : voices)
        if (voice.active && voice.bank != nullptr)
            voice.bank->decReferenceCount();
    for (int i = 0; i < numRetiredLater; ++i)
        retiredLater[static_cast<size_t>(i)]->decReferenceCount();
    drainGarbage();
}

void PadMixer::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    stealFadeSamples = samplesFor(stealFadeMs, rate);
    releaseFadeSamples = samplesFor(releaseFadeMs, rate);
    loopCrossfadeSamples = samplesFor(loopCrossfadeMs, rate);
}

//==============================================================================
// Message thread
bool PadMixer::pushEvent(const PadEvent& event)
{
    if (eventFifo.getFreeSpace() < 1)
    {
        switch (event.type)
        {
            case PadEvent::Type::on: return false;
            case PadEvent::Type::off:
                if (event.slot >= 0 && event.slot < render::maxSlices)
                {
                    offFlags[static_cast<size_t>(event.slot)].store(true, std::memory_order_release);
                    anyOffFlag.store(true, std::memory_order_release);
                }
                break;
            case PadEvent::Type::allOff: allOffFlag.store(true, std::memory_order_release); break;
            case PadEvent::Type::stopAll: stopAllFlag.store(true, std::memory_order_release); break;
        }
        return true;
    }
    const auto scope = eventFifo.write(1);
    if (scope.blockSize1 > 0)
        events[static_cast<size_t>(scope.startIndex1)] = event;
    else
        events[static_cast<size_t>(scope.startIndex2)] = event;
    return true;
}

void PadMixer::publishBank(render::SliceBank::Ptr bank)
{
    auto* raw = bank.get();
    if (raw != nullptr)
        raw->incReferenceCount();
    if (auto* replaced = incoming.exchange(raw, std::memory_order_acq_rel))
        replaced->decReferenceCount(); // never reached the audio thread
}

void PadMixer::drainGarbage()
{
    const auto scope = garbageFifo.read(garbageFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        garbage[static_cast<size_t>(scope.startIndex1 + i)]->decReferenceCount();
    for (int i = 0; i < scope.blockSize2; ++i)
        garbage[static_cast<size_t>(scope.startIndex2 + i)]->decReferenceCount();
}

int PadMixer::soundingCount(int slot) const
{
    if (slot < 0 || slot >= render::maxSlices)
        return 0;
    return counts[static_cast<size_t>(slot)].load(std::memory_order_relaxed);
}

//==============================================================================
// Audio thread
void PadMixer::retire(juce::ReferenceCountedObject* object)
{
    if (object == nullptr)
        return;
    if (garbageFifo.getFreeSpace() > 0)
    {
        const auto scope = garbageFifo.write(1);
        garbage[static_cast<size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = object;
        return;
    }
    if (numRetiredLater < static_cast<int>(retiredLater.size()))
        retiredLater[static_cast<size_t>(numRetiredLater++)] = object; // tried again next block
    else
        jassertfalse; // the message thread has not drained for a long time: this reference leaks
}

void PadMixer::flushRetired()
{
    while (numRetiredLater > 0 && garbageFifo.getFreeSpace() > 0)
    {
        const auto scope = garbageFifo.write(1);
        garbage[static_cast<size_t>(scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] =
            retiredLater[static_cast<size_t>(--numRetiredLater)];
    }
}

void PadMixer::endVoice(Voice& voice)
{
    if (voice.slot >= 0)
        counts[static_cast<size_t>(voice.slot)].fetch_sub(1, std::memory_order_relaxed);
    retire(voice.bank);
    voice = Voice{};
}

void PadMixer::beginFade(Voice& voice, double milliseconds)
{
    const int samples = milliseconds == stealFadeMs ? stealFadeSamples : releaseFadeSamples;
    if (voice.releasing)
    {
        // Already fading: a shorter fade shortens it, a longer one changes nothing.
        if (samples < voice.fadeLeft)
        {
            voice.fadeTotal = samples;
            voice.fadeLeft = samples;
        }
        return;
    }
    voice.releasing = true;
    voice.fadeTotal = samples;
    voice.fadeLeft = samples;
}

int PadMixer::allocateVoice()
{
    for (size_t i = 0; i < voices.size(); ++i)
        if (! voices[i].active)
            return static_cast<int>(i);
    // Every voice is busy: cut the oldest one that is already fading out.
    int oldest = -1;
    for (size_t i = 0; i < voices.size(); ++i)
        if (voices[i].releasing && (oldest < 0 || voices[i].order < voices[static_cast<size_t>(oldest)].order))
            oldest = static_cast<int>(i);
    if (oldest >= 0)
        endVoice(voices[static_cast<size_t>(oldest)]);
    return oldest;
}

void PadMixer::startVoice(int slot)
{
    if (current == nullptr || slot < 0 || slot >= render::maxSlices)
        return;
    const auto* buffer = current->buffers[static_cast<size_t>(slot)].get();
    if (buffer == nullptr || buffer->numFrames() == 0 || std::abs(buffer->sampleRate - rate) > 0.5)
        return;

    // Hitting a pad that still sounds chokes the old sound with a short fade.
    int sounding = 0;
    Voice* oldest = nullptr;
    for (auto& voice : voices)
    {
        if (! voice.active || voice.releasing)
            continue;
        if (voice.slot == slot)
            beginFade(voice, stealFadeMs);
        else
        {
            ++sounding;
            if (oldest == nullptr || voice.order < oldest->order)
                oldest = &voice;
        }
    }
    if (sounding >= maxVoices && oldest != nullptr)
        beginFade(*oldest, stealFadeMs);

    const int index = allocateVoice();
    if (index < 0)
        return;
    auto& voice = voices[static_cast<size_t>(index)];
    voice = Voice{};
    voice.bank = current;
    current->incReferenceCount();
    voice.buffer = buffer;
    voice.slot = slot;
    voice.mode = current->modes[static_cast<size_t>(slot)];
    voice.order = nextOrder++;
    voice.active = true;
    counts[static_cast<size_t>(slot)].fetch_add(1, std::memory_order_relaxed);
}

void PadMixer::releaseSlot(int slot, bool all)
{
    for (auto& voice : voices)
        if (voice.active && ! voice.releasing && (all || voice.slot == slot) && voice.mode != render::PlayMode::oneShot)
            beginFade(voice, releaseFadeMs);
}

void PadMixer::fadeAll()
{
    for (auto& voice : voices)
        if (voice.active)
            beginFade(voice, stealFadeMs);
}

void PadMixer::applyFlags()
{
    if (anyOffFlag.exchange(false, std::memory_order_acq_rel))
        for (int slot = 0; slot < render::maxSlices; ++slot)
            if (offFlags[static_cast<size_t>(slot)].exchange(false, std::memory_order_acq_rel))
                releaseSlot(slot, false);
    if (allOffFlag.exchange(false, std::memory_order_acq_rel))
        releaseSlot(0, true);
    if (stopAllFlag.exchange(false, std::memory_order_acq_rel))
        fadeAll();
}

void PadMixer::applyEvents()
{
    const auto scope = eventFifo.read(eventFifo.getNumReady());
    const auto apply = [this](const PadEvent& event) {
        switch (event.type)
        {
            case PadEvent::Type::on: startVoice(event.slot); break;
            case PadEvent::Type::off: releaseSlot(event.slot, false); break;
            case PadEvent::Type::allOff: releaseSlot(0, true); break;
            case PadEvent::Type::stopAll: fadeAll(); break;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i)
        apply(events[static_cast<size_t>(scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        apply(events[static_cast<size_t>(scope.startIndex2 + i)]);
}

void PadMixer::mixVoice(Voice& voice, float* left, float* right, int numFrames)
{
    const float* src = voice.buffer->data();
    const int length = voice.buffer->numFrames();
    // A loop crossfades its end into its start; a slice too short for that wraps hard.
    const int crossfade = voice.mode == render::PlayMode::loop && length > 4 * loopCrossfadeSamples ? loopCrossfadeSamples : 0;

    int position = voice.position;
    for (int i = 0; i < numFrames; ++i)
    {
        float l, r;
        if (crossfade > 0 && position >= length - crossfade)
        {
            const int q = position - (length - crossfade);
            const float w = static_cast<float>(q) / static_cast<float>(crossfade);
            l = src[2 * position] * (1.0f - w) + src[2 * q] * w;
            r = src[2 * position + 1] * (1.0f - w) + src[2 * q + 1] * w;
        }
        else
        {
            l = src[2 * position];
            r = src[2 * position + 1];
        }

        bool ended = false;
        if (voice.releasing)
        {
            const float gain = static_cast<float>(voice.fadeLeft) / static_cast<float>(voice.fadeTotal);
            l *= gain;
            r *= gain;
            ended = --voice.fadeLeft <= 0;
        }
        if (right != nullptr)
        {
            left[i] += l;
            right[i] += r;
        }
        else
            left[i] += 0.5f * (l + r);

        if (++position >= length)
        {
            if (voice.mode == render::PlayMode::loop)
                position = crossfade;
            else
                ended = true;
        }
        if (ended)
        {
            endVoice(voice);
            return;
        }
    }
    voice.position = position;
}

void PadMixer::process(float* const* channels, int numChannels, int numFrames)
{
    const platform::RealtimeScope realtime;
    blocks.fetch_add(1, std::memory_order_relaxed);

    if (auto* next = incoming.exchange(nullptr, std::memory_order_acq_rel))
    {
        retire(current);
        current = next;
    }
    flushRetired();
    applyEvents();
    applyFlags();

    if (numChannels < 1 || numFrames <= 0)
        return;
    float* left = channels[0];
    float* right = numChannels > 1 ? channels[1] : nullptr;
    for (auto& voice : voices)
        if (voice.active)
            mixVoice(voice, left, right, numFrames);
}
} // namespace sampler::engine
