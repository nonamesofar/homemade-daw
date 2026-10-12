#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace sampler::platform
{
/** Settings of one take file. */
struct TakeOptions
{
    juce::File file;
    double sampleRate = 48000.0;
    int channels = 2;
    /** Stop accepting audio after this many frames (0 = no limit); `limitReached` is set then. */
    juce::int64 maxFrames = 0;
    /** The same limit in seconds, used when `maxFrames` is 0 (the caller may not know the rate). */
    double maxSeconds = 0.0;
    /**
        System audio trim (design D5): frames before the first sound above `loudDb` are not written (a short margin is
        kept), and after the last one the file is cut at `finish()`. Off for Master and track takes, which must start
        exactly where the record position was.
    */
    bool trimSilence = false;
    double loudDb = -60.0;
    double marginSeconds = 0.005;
    int ringSeconds = 10;
    /** Frames per waveform bucket (min/max pair) for the live display. */
    int bucketFrames = 512;
    /**
        A block that does not fit in the ring is dropped (counted as an overrun). With this on, the same number of silent
        frames is written as soon as there is room, so that the later audio stays on the timeline (Master and track
        takes). System takes leave it off: their capture inserts silence for gaps itself.
    */
    bool fillDroppedWithSilence = false;
    /**
        Most silent frames written per producer call (more of it is owed until later calls; audio waits for it, so it
        is never shuffled into the silence). Keeps the producer's work per call bounded.
    */
    int silenceFramesPerCall = 16384;
};

/** One bucket of the live waveform: lowest and highest sample of `bucketFrames` frames over all channels. */
struct MinMax
{
    float min = 0.0f;
    float max = 0.0f;
};

/** What a finished take is. */
struct TakeResult
{
    juce::File file;
    double sampleRate = 0.0;
    int channels = 0;
    /** Frames in the finished file (after the trim). */
    juce::int64 frames = 0;
    /** Frames the producer delivered before the first one that was written (System trim). */
    juce::int64 leadingTrimFrames = 0;
    /** Frames the producer delivered in all, including the ones that were not kept. */
    juce::int64 framesReceived = 0;
    bool everLoud = false;
    bool limitReached = false;
    bool writeFailed = false;
    bool trimmedTail = false;
    int overruns = 0;
};

/**
    The one take pipeline (design D1): a single producer thread (a capture thread or the audio thread) pushes blocks
    into an SPSC ring; a writer thread drains it into a 32-bit float WAV. The producer path never allocates, locks or
    blocks (AGENTS.md section 5.1); when the ring is full the block is dropped and counted as an overrun.

    Besides the file it keeps what the live display needs: the peak since the last read, min/max buckets of the audio
    written so far, whether and when sound was heard, and the optional System trim.
*/
class TakeWriter
{
public:
    TakeWriter();
    ~TakeWriter();

    TakeWriter(const TakeWriter&) = delete;
    TakeWriter& operator=(const TakeWriter&) = delete;

    /** Creates the file and starts the writer thread. Fails (nothing left behind) if the file cannot be created. */
    juce::Result open(const TakeOptions&);
    bool isOpen() const { return writerThread != nullptr; }

    //==============================================================================
    // Producer thread (one thread only).

    /** Interleaved frames; `sourceChannels` may differ from the take's (extra channels dropped, a mono source is copied). */
    void pushInterleaved(const float* data, int sourceChannels, int frames);
    /** `offset` is the first frame to take from each channel. */
    void pushPlanar(const float* const* channels, int sourceChannels, int frames, int offset = 0);
    /**
        Like pushPlanar, for a producer that knows where on the timeline each block lies (an engine tap). When the
        timeline moved forward by more than the previous block (the engine skipped blocks, e.g. on a muted track), the
        skipped span is filled with silence, so later audio stays on the timeline. A jump back (loop, seek) just goes on
        appending. Needs `fillDroppedWithSilence`.
    */
    void pushPlanarAt(const float* const* channels, int sourceChannels, int frames, int offset, juce::int64 timelineFrame);
    /**
        Owes the take `frames` of silence at the current end (a hole the producer knows about, e.g. nothing played for
        minutes). It is written in bounded steps by this and the following calls, and what remains at `finish()`; audio
        pushed meanwhile waits behind it (it becomes silence too), so the timeline length stays right. Before the first
        sound of a trimmed take it is only counted, never kept. Returns at once: it never blocks.
    */
    void addPendingSilence(juce::int64 frames);
    /** Room for this many more frames right now (used to wait while inserting silence). */
    int freeFrames() const { return fifo != nullptr ? fifo->getFreeSpace() : 0; }

    //==============================================================================
    // Any thread.

    /** Frames the producer delivered so far, kept or not. */
    juce::int64 framesReceived() const { return received.load(std::memory_order_acquire); }
    /** Frames written into the ring so far (after the trim). */
    juce::int64 framesKept() const { return kept.load(std::memory_order_acquire); }
    /** -1 until the first kept frame; then the number of frames delivered before it. */
    juce::int64 leadingTrimFrames() const { return leading.load(std::memory_order_acquire); }
    bool everLoud() const { return loudSeen.load(std::memory_order_acquire); }
    /** Frames since the last sound above the threshold (all of them if there was none). */
    juce::int64 framesSinceLoud() const;
    bool limitReached() const { return limitHit.load(); }
    bool writeFailed() const { return failed.load(); }
    int overruns() const { return overrunCount.load(); }

    /** Peak (linear, 0..1+) since the previous call. */
    float takePeak() const;

    /** Waveform buckets completed so far, and a copy of those from `first` on. */
    int numBuckets() const { return bucketCount.load(std::memory_order_acquire); }
    int bucketFramesPerBucket() const { return opts.bucketFrames; }
    void copyBuckets(int first, std::vector<MinMax>& out) const;
    /** One completed bucket (`index` below numBuckets()). */
    MinMax bucketAt(int index) const { return buckets[static_cast<size_t>(index)]; }

    /**
        System takes that must not keep what was captured before playback began: frames delivered before the
        `frame`-th one are counted as received but dropped (no loudness, no trim, not written). Any thread; takes
        effect for blocks pushed after the call.
    */
    void keepFrom(juce::int64 frame) { keepFromFrame.store(frame, std::memory_order_release); }

    /** Tests only: while held, the writer thread does not drain the ring (provokes an overrun). */
    void holdWriter(bool hold) { held.store(hold); }
    /** Tests only: writes fail once this many frames are in the file (negative = never). */
    void failWritesAfterFrames(juce::int64 frames) { failAfterFrames.store(frames); }

    /**
        Ends the take: lets the writer drain, closes the file, applies the tail trim and reports. Safe to call twice
        (the second call returns the same result). Does not delete the file; the caller drops silent takes.
    */
    TakeResult finish();
    const TakeOptions& options() const { return opts; }

private:
    class WriterThread;

    template <typename Get>
    void pushCore(int sourceChannels, int frames, Get get);
    template <typename Get>
    bool writeSegment(int sourceChannels, int fromFrame, int count, Get get);
    void flushSilence(int blockFrames);
    void writeSilenceToRing(int count);
    template <typename Get>
    void writeToRing(int sourceChannels, int fromFrame, int count, Get get);

    TakeOptions opts;
    int ringSize = 0;
    std::unique_ptr<juce::AbstractFifo> fifo;
    juce::AudioBuffer<float> ring;
    std::unique_ptr<WriterThread> writerThread;
    juce::File fileOut;

    // Producer-only state.
    bool started = false;
    juce::int64 receivedLocal = 0, keptLocal = 0, lastLoudKept = -1;
    juce::int64 droppedPending = 0;   // frames dropped for lack of room and not yet replaced by silence
    juce::int64 lastTimelineFrame = 0, lastTimelineFrames = 0;
    bool haveTimeline = false;
    juce::int64 segmentStartKept = 0; // keptLocal where the audio of the last writeSegment began
    int marginFrames = 0, prerollCount = 0;
    juce::AudioBuffer<float> preroll;
    float bucketMin = 0.0f, bucketMax = 0.0f;
    int bucketFill = 0;
    float loudLinear = 0.0f;

    std::vector<MinMax> buckets;
    std::atomic<int> bucketCount{0};

    std::atomic<juce::int64> received{0}, kept{0}, leading{-1}, lastLoudReceived{-1};
    std::atomic<bool> loudSeen{false}, limitHit{false}, failed{false}, finishedFlag{false}, held{false};
    std::atomic<int> overrunCount{0};
    std::atomic<juce::int64> keepFromFrame{0};
    std::atomic<juce::int64> writtenFrames{0}, failAfterFrames{-1};
    mutable std::atomic<float> peak{0.0f};

    TakeResult finalResult;
    bool finalised = false;
};
} // namespace sampler::platform
