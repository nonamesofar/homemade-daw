#pragma once

#include "engine/PadEngine.h"
#include "model/SamplerState.h"
#include "render/RenderScheduler.h"

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <vector>

namespace sampler::app
{
/**
    Keeps the pad player supplied with the slices of the sound open in the sample editor (design D6): when a slice is new
    or its pitch, gain, reverse or points change it is rendered on a worker at the device's rate and swapped into the
    bank when ready; until then the pad plays the slice as it was. A device rate change renders everything again, and
    when the memory budget is full a pad whose slice was dropped renders it on demand (it sounds from the next hit).
    Message thread only. Pads never touch the project.
*/
class PadController
{
public:
    /** `deviceRate` is asked at every refresh (0 while no device is open). */
    PadController(PadEngine&, juce::ValueTree samplerState, std::function<juce::File()> bundleDir, std::function<double()> deviceRate);
    ~PadController();

    /** Starts supplying the pads with this sound's slices (replaces the sound before). */
    void open(const juce::String& sourceId);
    /** Silences the pads and empties the bank. */
    void close();
    const juce::String& openId() const { return soundId; }

    /** The project changed (or the selection did): render what is new or changed, selected slice first. */
    void refresh(int selectedSlice);

    /** A pad was pressed or let go; `slot` is the slice index (bank * 16 + pad). */
    void padDown(int slot);
    void padUp(int slot);
    /** Lets go of every pad. */
    void allOff();
    /** Fades out everything that sounds, One-shots too. */
    void stopAll();

    bool isSounding(int slot) const { return pads.soundingCount(slot) > 0; }
    bool hasBuffer(int slot) const;
    int renderedCount() const;
    bool isIdle() const { return scheduler.isIdle(); }
    /** Slices that have a render waiting or running. */
    int pendingRenders() const { return scheduler.pendingCount(); }

    /** The rate the current renders were made for. */
    double renderRate() const { return outputRate; }

    /** The most recent pad press, with the slot (for the pad lights and tests). */
    int lastPlayedSlot() const { return lastSlot; }

private:
    struct Description
    {
        juce::int64 start = 0;
        juce::int64 end = 0;
        double pitch = 0.0;
        double gainDb = 0.0;
        bool reverse = false;
        double rate = 0.0;
        bool valid = false;   // a render was requested for exactly this
        bool dropped = false; // its buffer was dropped for memory: rendered again only when a pad asks

        bool sameRender(const Description& other) const
        {
            return start == other.start && end == other.end && pitch == other.pitch && gainDb == other.gainDb && reverse == other.reverse
                   && rate == other.rate;
        }
    };

    void request(int slot, const Description&, int priority);
    void publish();
    void rendered(int slot, render::SliceBuffer::Ptr);
    void evicted(int slot);

    PadEngine& pads;
    juce::ValueTree sampler;
    std::function<juce::File()> bundleDir;
    std::function<double()> deviceRate;

    juce::String soundId;
    juce::File soundFile;
    double outputRate = 0.0;
    std::vector<Description> described;
    std::vector<render::PlayMode> modes;
    render::SliceBank::Ptr bank;
    int lastSlot = -1;
    render::RenderScheduler scheduler;
};
} // namespace sampler::app
