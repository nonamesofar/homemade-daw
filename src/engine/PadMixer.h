#pragma once

#include "render/SliceBank.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace sampler::engine
{
/** A pad press or release from the UI. `slot` is the slice index (0..255), not the pad number. */
struct PadEvent
{
    enum class Type : uint8_t
    {
        on,
        off,
        allOff,
        /** Every voice, one-shots too, fades out over 3 ms (the editor closed, the project changed). */
        stopAll
    };

    Type type = Type::on;
    int slot = 0;
    float velocity = 1.0f; // 1.0 until pads take MIDI input (M5)
};

/**
    The pad player's real-time core (design D5): 16 voices that play slice buffers from a published bank. It has no
    Tracktion types, so tests can drive it offline with scripted events; `SlicePadPlugin` is the thin shell that calls
    `process` from the audio thread.

    Threads: the message thread calls `prepare`, `pushEvent`, `publishBank`, `drainGarbage`, `soundingCount`; the audio
    thread calls `process` only. Real-time code (AGENTS.md section 5.1): `process` allocates nothing, takes no lock and
    does no I/O. Buffers are shared by reference counting and are released on the message thread through a garbage FIFO.
*/
class PadMixer
{
public:
    static constexpr int maxVoices = 16;                        // voices that are not fading out
    static constexpr int maxFadingVoices = 16;                  // stolen or released voices still fading
    static constexpr double stealFadeMs = 3.0;                  // oldest voice stolen, or a retriggered voice
    static constexpr double releaseFadeMs = 10.0;               // Gate and Loop on key-up
    static constexpr double loopCrossfadeMs = 3.0;

    PadMixer();
    ~PadMixer();

    PadMixer(const PadMixer&) = delete;
    PadMixer& operator=(const PadMixer&) = delete;

    //==============================================================================
    /** Message thread. Must be called before `process`, and again when the sample rate changes. */
    void prepare(double sampleRate);

    /**
        Message thread. False if the queue is full and the event was dropped. An `off`, `allOff` or `stopAll` is never lost:
        when the queue is full it is carried by an atomic flag that `process` checks after the queued events.
    */
    bool pushEvent(const PadEvent&);

    /** Message thread. Replaces the bank the voices start from; sounding voices keep playing their old buffers. */
    void publishBank(render::SliceBank::Ptr);

    /** Message thread. Releases the references the audio thread has handed back. */
    void drainGarbage();

    /** Number of voices sounding slice `slot` (including fading ones); read from any thread. */
    int soundingCount(int slot) const;
    /** Number of `process` calls so far (diagnostics, tests). */
    juce::int64 blocksProcessed() const { return blocks.load(std::memory_order_relaxed); }

    //==============================================================================
    /** Audio thread. Adds the sounding voices into `channels` (stereo; a mono destination gets the left+right mix). */
    void process(float* const* channels, int numChannels, int numFrames);

private:
    struct Voice
    {
        render::SliceBank* bank = nullptr; // holds one reference, returned through the garbage FIFO
        const render::SliceBuffer* buffer = nullptr;
        int slot = -1;
        render::PlayMode mode = render::PlayMode::oneShot;
        int position = 0;
        juce::uint64 order = 0;
        bool active = false;
        bool releasing = false;
        int fadeLeft = 0;
        int fadeTotal = 0;
    };

    void applyEvents();
    void startVoice(int slot);
    void releaseSlot(int slot, bool all);
    void fadeAll();
    void applyFlags();
    void beginFade(Voice&, double milliseconds);
    void endVoice(Voice&);
    void retire(juce::ReferenceCountedObject*);
    void flushRetired();
    int allocateVoice();
    void mixVoice(Voice&, float* left, float* right, int numFrames);

    static constexpr int totalVoices = maxVoices + maxFadingVoices;
    static constexpr int eventCapacity = 512;
    static constexpr int garbageCapacity = 256;

    double rate = 44100.0;
    int stealFadeSamples = 132;
    int releaseFadeSamples = 441;
    int loopCrossfadeSamples = 132;

    // UI -> audio events.
    juce::AbstractFifo eventFifo{eventCapacity};
    std::array<PadEvent, eventCapacity> events{};

    // Bank exchange: the message thread stores the next bank (one reference), the audio thread takes it at block start.
    std::atomic<render::SliceBank*> incoming{nullptr};
    render::SliceBank* current = nullptr; // audio thread; holds one reference

    // Audio -> message thread: references to release.
    juce::AbstractFifo garbageFifo{garbageCapacity};
    std::array<juce::ReferenceCountedObject*, garbageCapacity> garbage{};
    std::array<juce::ReferenceCountedObject*, totalVoices> retiredLater{}; // when the garbage FIFO is full
    int numRetiredLater = 0;

    // Releases that did not fit the event queue.
    std::array<std::atomic<bool>, render::maxSlices> offFlags{};
    std::atomic<bool> anyOffFlag{false};
    std::atomic<bool> allOffFlag{false};
    std::atomic<bool> stopAllFlag{false};

    std::array<Voice, totalVoices> voices{};
    juce::uint64 nextOrder = 1;
    std::array<std::atomic<uint8_t>, render::maxSlices> counts{};
    std::atomic<juce::int64> blocks{0};
};
} // namespace sampler::engine
