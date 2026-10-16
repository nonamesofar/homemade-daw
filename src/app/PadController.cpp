#include "PadController.h"

#include <algorithm>

namespace sampler::app
{
namespace
{
render::PlayMode toRenderMode(model::PlayMode mode)
{
    switch (mode)
    {
        case model::PlayMode::oneShot: return render::PlayMode::oneShot;
        case model::PlayMode::gate: return render::PlayMode::gate;
        case model::PlayMode::loop: return render::PlayMode::loop;
    }
    return render::PlayMode::oneShot;
}
} // namespace

PadController::PadController(PadEngine& engine, juce::ValueTree samplerState, std::function<juce::File()> dir, std::function<double()> rate)
    : pads(engine), sampler(std::move(samplerState)), bundleDir(std::move(dir)), deviceRate(std::move(rate)),
      scheduler([](const render::SliceRequest& r) {
          render::SliceParams params = r.params;
          return render::renderFromFile(r.file, r.startFrame, r.endFrame, params);
      })
{
    scheduler.onRendered = [this](int slot, render::SliceBuffer::Ptr buffer) { rendered(slot, std::move(buffer)); };
    scheduler.onEvicted = [this](int slot) { evicted(slot); };
}

PadController::~PadController()
{
    scheduler.onRendered = nullptr;
    scheduler.onEvicted = nullptr;
    stopAll();
    pads.clearBank();
}

void PadController::open(const juce::String& sourceId)
{
    close();
    const auto source = model::SamplerState(sampler).findSourceById(sourceId);
    if (! source.isValid())
        return;
    soundId = sourceId;
    soundFile = bundleDir().getChildFile(source.decoded());
    bank = new render::SliceBank();
    pads.setBank(bank);
}

void PadController::close()
{
    scheduler.cancelAll();
    stopAll();
    if (soundId.isNotEmpty())
        pads.clearBank();
    soundId = {};
    described.clear();
    modes.clear();
    bank = nullptr;
    outputRate = 0.0;
    lastSlot = -1;
}

void PadController::allOff() { pads.allOff(); }

void PadController::stopAll() { pads.stopAll(); }

bool PadController::hasBuffer(int slot) const
{
    return bank != nullptr && juce::isPositiveAndBelow(slot, render::maxSlices) && bank->buffers[static_cast<size_t>(slot)] != nullptr;
}

int PadController::renderedCount() const
{
    int count = 0;
    if (bank != nullptr)
        for (const auto& buffer : bank->buffers)
            count += buffer != nullptr ? 1 : 0;
    return count;
}

void PadController::request(int slot, const Description& d, int priority)
{
    render::SliceRequest r;
    r.slot = slot;
    r.file = soundFile;
    r.startFrame = d.start;
    r.endFrame = d.end;
    r.params.pitchSemitones = static_cast<float>(d.pitch);
    r.params.gainDb = static_cast<float>(d.gainDb);
    r.params.reverse = d.reverse;
    r.params.outputRate = d.rate;
    // sourceRate is read from the file by the renderer.
    scheduler.request(r, priority);
}

void PadController::publish()
{
    if (bank == nullptr)
        return;
    pads.setBank(bank);
}

void PadController::refresh(int selectedSlice)
{
    if (soundId.isEmpty())
        return;
    const auto source = model::SamplerState(sampler).findSourceById(soundId);
    if (! source.isValid())
        return;
    const auto data = source.sliceSet();
    const int count = data.numSlices();
    const double rate = deviceRate ? deviceRate() : 0.0;
    if (rate <= 0.0)
        return; // no output device: the pads have nothing to play to

    // A new device rate makes every earlier render wrong.
    if (std::abs(rate - outputRate) > 0.5)
    {
        outputRate = rate;
        scheduler.cancelAll();
        for (auto& d : described)
            d.valid = false;
        bank = new render::SliceBank();
        pads.setBank(bank);
    }

    described.resize(static_cast<size_t>(count));
    modes.resize(static_cast<size_t>(count), render::PlayMode::oneShot);
    bool bankChanged = false;
    auto next = new render::SliceBank();
    next->buffers = bank->buffers;
    next->modes = bank->modes;
    render::SliceBank::Ptr nextHolder(next);

    // Slices that no longer exist.
    for (int slot = count; slot < render::maxSlices; ++slot)
        if (next->buffers[static_cast<size_t>(slot)] != nullptr)
        {
            next->buffers[static_cast<size_t>(slot)] = nullptr;
            scheduler.forget(slot);
            bankChanged = true;
        }

    juce::int64 start = 0;
    for (int slot = 0; slot < count; ++slot)
    {
        const auto end = slot < static_cast<int>(data.markers.size()) ? data.markers[static_cast<size_t>(slot)] : source.frames();
        const auto& settings = data.slices[static_cast<size_t>(slot)];
        Description d;
        d.start = start;
        d.end = end;
        d.pitch = settings.pitch;
        d.gainDb = settings.gainDb;
        d.reverse = settings.reverse;
        d.rate = rate;
        d.valid = true;
        start = end;

        const auto mode = toRenderMode(settings.playMode);
        if (next->modes[static_cast<size_t>(slot)] != mode)
        {
            next->modes[static_cast<size_t>(slot)] = mode;
            bankChanged = true;
        }
        auto& known = described[static_cast<size_t>(slot)];
        const bool unchanged = known.valid && known.sameRender(d);
        if (! unchanged)
        {
            known = d;
            request(slot, d, slot == selectedSlice ? 10 : 0);
        }
        else if (known.dropped)
        {
            // Left out for memory until a pad asks for it.
        }
        else if (slot == selectedSlice)
            scheduler.prioritise(slot);
    }
    if (bankChanged)
    {
        bank = nextHolder;
        publish();
    }
}

void PadController::rendered(int slot, render::SliceBuffer::Ptr buffer)
{
    if (soundId.isEmpty() || bank == nullptr || ! juce::isPositiveAndBelow(slot, static_cast<int>(described.size())))
        return;
    auto next = new render::SliceBank();
    next->buffers = bank->buffers;
    next->modes = bank->modes;
    next->buffers[static_cast<size_t>(slot)] = std::move(buffer);
    bank = next;
    publish();
}

void PadController::evicted(int slot)
{
    if (bank == nullptr || ! juce::isPositiveAndBelow(slot, static_cast<int>(described.size())))
        return;
    auto next = new render::SliceBank();
    next->buffers = bank->buffers;
    next->modes = bank->modes;
    next->buffers[static_cast<size_t>(slot)] = nullptr;
    bank = next;
    described[static_cast<size_t>(slot)].dropped = true; // rendered again when a pad asks for it
    publish();
}

void PadController::padDown(int slot)
{
    if (soundId.isEmpty() || ! juce::isPositiveAndBelow(slot, static_cast<int>(described.size())))
        return;
    lastSlot = slot;
    scheduler.used(slot);
    if (! hasBuffer(slot))
    {
        // Dropped for memory, or not rendered yet: ask for it at once; it sounds from the next hit.
        auto& d = described[static_cast<size_t>(slot)];
        if (d.dropped)
        {
            d.dropped = false;
            request(slot, d, 20);
        }
        else
            scheduler.prioritise(slot);
        return;
    }
    pads.padOn(slot);
}

void PadController::padUp(int slot)
{
    // Any slot: the bank or the slice count may have changed since the pad went down.
    if (soundId.isEmpty() || ! juce::isPositiveAndBelow(slot, render::maxSlices))
        return;
    pads.padOff(slot);
}
} // namespace sampler::app
