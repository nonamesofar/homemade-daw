#include "TakeWriter.h"

#include "RealtimeGuard.h"
#include "WavTail.h"

#include <algorithm>
#include <cmath>

namespace sampler::platform
{
class TakeWriter::WriterThread final : public juce::Thread
{
public:
    WriterThread(TakeWriter& o, std::unique_ptr<juce::AudioFormatWriter> w)
        : juce::Thread("Take writer"), owner(o), writer(std::move(w))
    {
    }

    void run() override
    {
        juce::AudioBuffer<float> block(owner.opts.channels, 4096);
        bool failedWrite = false;
        for (;;)
        {
            if (owner.held.load() && ! threadShouldExit())
            {
                wait(5); // tests only: the ring is not drained while held
                continue;
            }
            const auto ready = owner.fifo->getNumReady();
            if (ready == 0)
            {
                // Everything delivered is written: finish once the take is over (or we are asked to exit).
                if (owner.finishedFlag.load() || threadShouldExit())
                    break;
                wait(5);
                continue;
            }

            const auto n = juce::jmin(ready, 4096);
            int start1, size1, start2, size2;
            owner.fifo->prepareToRead(n, start1, size1, start2, size2);
            for (int c = 0; c < owner.opts.channels; ++c)
            {
                block.copyFrom(c, 0, owner.ring, c, start1, size1);
                if (size2 > 0)
                    block.copyFrom(c, size1, owner.ring, c, start2, size2);
            }
            owner.fifo->finishedRead(size1 + size2);

            // After a failed write keep emptying the ring (so the producer does not overrun), but stop writing.
            if (! failedWrite)
            {
                const auto failAfter = owner.failAfterFrames.load();
                const auto frames = size1 + size2;
                if ((failAfter >= 0 && owner.writtenFrames.load() + frames > failAfter) || ! writer->writeFromAudioSampleBuffer(block, 0, frames))
                {
                    failedWrite = true;
                    owner.failed.store(true);
                }
                else
                {
                    owner.writtenFrames.fetch_add(frames);
                }
            }
        }
        writer.reset(); // flushes and finalises the WAV header
    }

private:
    TakeWriter& owner;
    std::unique_ptr<juce::AudioFormatWriter> writer;
};

TakeWriter::TakeWriter() = default;

TakeWriter::~TakeWriter()
{
    if (writerThread != nullptr)
        finish();
}

juce::Result TakeWriter::open(const TakeOptions& options)
{
    if (isOpen())
        return juce::Result::fail("take already open");
    if (finalised)
        return juce::Result::fail("take already finished"); // one take per writer: never touch the finished file
    opts = options;
    opts.channels = juce::jlimit(1, 2, opts.channels);
    if (opts.sampleRate <= 0.0)
        return juce::Result::fail("bad sample rate");
    if (opts.maxFrames <= 0 && opts.maxSeconds > 0.0)
        opts.maxFrames = static_cast<juce::int64>(opts.maxSeconds * opts.sampleRate);

    ringSize = static_cast<int>(opts.sampleRate) * juce::jmax(1, opts.ringSeconds);
    fifo = std::make_unique<juce::AbstractFifo>(ringSize);
    ring.setSize(opts.channels, ringSize);
    ring.clear();
    marginFrames = static_cast<int>(opts.sampleRate * opts.marginSeconds);
    preroll.setSize(opts.channels, juce::jmax(1, marginFrames));
    preroll.clear();
    prerollCount = 0;
    loudLinear = juce::Decibels::decibelsToGain(static_cast<float>(opts.loudDb));

    const auto bucketCapacity =
        static_cast<size_t>((opts.maxFrames > 0 ? opts.maxFrames : static_cast<juce::int64>(opts.sampleRate) * 1800) / opts.bucketFrames) + 2;
    buckets.assign(bucketCapacity, {});
    bucketCount = 0;

    started = false;
    receivedLocal = keptLocal = 0;
    droppedPending = segmentStartKept = 0;
    lastTimelineFrame = lastTimelineFrames = 0;
    haveTimeline = false;
    writtenFrames = 0;
    failAfterFrames = -1;
    lastLoudKept = -1;
    bucketFill = 0;
    bucketMin = bucketMax = 0.0f;
    received = 0;
    kept = 0;
    leading = -1;
    lastLoudReceived = -1;
    loudSeen = limitHit = failed = finishedFlag = held = false;
    keepFromFrame = 0;
    overrunCount = 0;
    peak = 0.0f;
    finalised = false;
    finalResult = {};

    fileOut = opts.file;
    fileOut.deleteFile();
    fileOut.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(fileOut.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail("cannot create " + fileOut.getFullPathName());
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), opts.sampleRate, static_cast<unsigned int>(opts.channels), 32, {}, 0));
    if (writer == nullptr)
    {
        stream.reset();
        fileOut.deleteFile();
        return juce::Result::fail("cannot create " + fileOut.getFullPathName());
    }
    stream.release();

    writerThread = std::make_unique<WriterThread>(*this, std::move(writer));
    writerThread->startThread();
    return juce::Result::ok();
}

juce::int64 TakeWriter::framesSinceLoud() const
{
    const auto total = framesReceived();
    return loudSeen.load() ? total - lastLoudReceived.load() : total;
}

float TakeWriter::takePeak() const { return peak.exchange(0.0f); }

void TakeWriter::copyBuckets(int first, std::vector<MinMax>& out) const
{
    out.clear();
    const auto count = numBuckets();
    for (int i = juce::jmax(0, first); i < count; ++i)
        out.push_back(buckets[static_cast<size_t>(i)]);
}

void TakeWriter::pushInterleaved(const float* data, int sourceChannels, int frames)
{
    pushCore(sourceChannels, frames, [data, sourceChannels](int c, int i) { return data[i * sourceChannels + c]; });
}

void TakeWriter::pushPlanar(const float* const* channels, int sourceChannels, int frames, int offset)
{
    pushCore(sourceChannels, frames, [channels, offset](int c, int i) { return channels[c][offset + i]; });
}

void TakeWriter::pushPlanarAt(const float* const* channels, int sourceChannels, int frames, int offset, juce::int64 timelineFrame)
{
    if (frames <= 0 || writerThread == nullptr)
        return;
    if (opts.fillDroppedWithSilence && haveTimeline && ! limitHit.load(std::memory_order_relaxed))
    {
        const auto advance = timelineFrame - lastTimelineFrame;
        if (advance > lastTimelineFrames + 2) // a little slack for rounding of the position
        {
            const auto missing = advance - lastTimelineFrames;
            droppedPending += missing; // written (as silence) beside the next block
            receivedLocal += missing;
            received.store(receivedLocal, std::memory_order_release);
        }
    }
    lastTimelineFrame = timelineFrame;
    lastTimelineFrames = frames;
    haveTimeline = true;
    pushPlanar(channels, sourceChannels, frames, offset);
}

void TakeWriter::flushSilence(int blockFrames)
{
    if (droppedPending <= 0)
        return;
    if (opts.maxFrames > 0)
    {
        // Silence beyond the limit is never written; the limit ends the take.
        const auto room = juce::jmax<juce::int64>(0, opts.maxFrames - keptLocal);
        if (droppedPending > room)
        {
            droppedPending = room;
            limitHit.store(true);
        }
    }
    // Bounded work per call (design: the producer never spends unbounded time here).
    const juce::int64 bound = juce::jmax<juce::int64>(opts.silenceFramesPerCall, 4 * static_cast<juce::int64>(blockFrames));
    const auto n = juce::jmin<juce::int64>(droppedPending, bound, fifo->getFreeSpace());
    if (n > 0)
    {
        writeSilenceToRing(static_cast<int>(n));
        droppedPending -= n;
    }
}

template <typename Get>
bool TakeWriter::writeSegment(int sourceChannels, int fromFrame, int count, Get get)
{
    if (count <= 0)
        return false;
    // Audio goes in only once ALL silence owed before it is written, so it never lands inside a silent span.
    flushSilence(count);
    if (droppedPending > 0)
    {
        overrunCount.fetch_add(1); // this block would land too early: it becomes silence as well
        droppedPending += count;
        return false;
    }
    if (fifo->getFreeSpace() < count)
    {
        overrunCount.fetch_add(1); // the writer cannot keep up: drop rather than block the producer
        if (opts.fillDroppedWithSilence)
            droppedPending += count;
        return false;
    }
    segmentStartKept = keptLocal;
    writeToRing(sourceChannels, fromFrame, count, get);
    return true;
}

void TakeWriter::writeSilenceToRing(int count)
{
    int start1, size1, start2, size2;
    fifo->prepareToWrite(count, start1, size1, start2, size2);
    if (size1 > 0)
        ring.clear(start1, size1);
    if (size2 > 0)
        ring.clear(start2, size2);
    // Waveform buckets, in bulk (the same result as pushing zeros frame by frame).
    for (int remaining = count; remaining > 0;)
    {
        const int step = juce::jmin(remaining, opts.bucketFrames - bucketFill);
        if (bucketFill == 0)
            bucketMin = bucketMax = 0.0f;
        else
        {
            bucketMin = juce::jmin(bucketMin, 0.0f);
            bucketMax = juce::jmax(bucketMax, 0.0f);
        }
        bucketFill += step;
        remaining -= step;
        if (bucketFill >= opts.bucketFrames)
        {
            const auto index = bucketCount.load(std::memory_order_relaxed);
            if (static_cast<size_t>(index) < buckets.size())
            {
                buckets[static_cast<size_t>(index)] = {bucketMin, bucketMax};
                bucketCount.store(index + 1, std::memory_order_release);
            }
            bucketFill = 0;
        }
    }
    fifo->finishedWrite(count);
    keptLocal += count;
}

void TakeWriter::addPendingSilence(juce::int64 frames)
{
    if (frames <= 0 || writerThread == nullptr)
        return;
    const RealtimeScope realtime;

    // Frames before the keep-from mark are counted but never kept (as in pushCore).
    const auto gate = keepFromFrame.load(std::memory_order_acquire);
    if (gate > receivedLocal)
    {
        const auto skipped = juce::jmin<juce::int64>(frames, gate - receivedLocal);
        receivedLocal += skipped;
        frames -= skipped;
    }
    if (frames > 0)
    {
        const auto receivedBefore = receivedLocal;
        receivedLocal += frames;
        if (! started && opts.trimSilence)
        {
            prerollCount = 0; // before the first sound: counted, never kept
        }
        else if (! limitHit.load(std::memory_order_relaxed))
        {
            if (! started)
            {
                started = true;
                leading.store(receivedBefore, std::memory_order_release);
            }
            droppedPending += frames; // written (as silence) in bounded steps, ahead of the next audio
        }
    }
    flushSilence(0);
    received.store(receivedLocal, std::memory_order_release);
    kept.store(keptLocal, std::memory_order_release);
}

template <typename Get>
void TakeWriter::writeToRing(int sourceChannels, int fromFrame, int count, Get get)
{
    const int channels = opts.channels;
    int start1, size1, start2, size2;
    fifo->prepareToWrite(count, start1, size1, start2, size2);
    int done = 0;
    for (int part = 0; part < 2; ++part)
    {
        const int start = part == 0 ? start1 : start2;
        const int size = part == 0 ? size1 : size2;
        for (int i = 0; i < size; ++i, ++done)
        {
            float lo = 0.0f, hi = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                const float v = get(c < sourceChannels ? c : 0, fromFrame + done);
                ring.setSample(c, start + i, v);
                lo = c == 0 ? v : juce::jmin(lo, v);
                hi = c == 0 ? v : juce::jmax(hi, v);
            }
            if (bucketFill == 0)
            {
                bucketMin = lo;
                bucketMax = hi;
            }
            else
            {
                bucketMin = juce::jmin(bucketMin, lo);
                bucketMax = juce::jmax(bucketMax, hi);
            }
            if (++bucketFill >= opts.bucketFrames)
            {
                const auto index = bucketCount.load(std::memory_order_relaxed);
                if (static_cast<size_t>(index) < buckets.size())
                {
                    buckets[static_cast<size_t>(index)] = {bucketMin, bucketMax};
                    bucketCount.store(index + 1, std::memory_order_release);
                }
                bucketFill = 0;
            }
        }
    }
    fifo->finishedWrite(count);
    keptLocal += count;
}

template <typename Get>
void TakeWriter::pushCore(int sourceChannels, int framesDelivered, Get getRaw)
{
    if (framesDelivered <= 0 || writerThread == nullptr || sourceChannels <= 0)
        return;
    const RealtimeScope realtime;

    // Frames before the keep-from mark are counted as delivered but never looked at or kept.
    const auto gate = keepFromFrame.load(std::memory_order_acquire);
    const int skipped = gate > receivedLocal ? static_cast<int>(juce::jmin<juce::int64>(framesDelivered, gate - receivedLocal)) : 0;
    if (skipped >= framesDelivered)
    {
        receivedLocal += framesDelivered;
        received.store(receivedLocal, std::memory_order_release);
        return;
    }
    if (skipped > 0)
    {
        receivedLocal += skipped;
        received.store(receivedLocal, std::memory_order_release);
    }
    const int frames = framesDelivered - skipped;
    const auto get = [&getRaw, skipped](int c, int i) { return getRaw(c, i + skipped); };

    const int channels = opts.channels;

    // Loudness of this block.
    float blockPeak = 0.0f;
    int firstLoud = -1, lastLoud = -1;
    for (int i = 0; i < frames; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < channels; ++c)
            m = juce::jmax(m, std::abs(get(c < sourceChannels ? c : 0, i)));
        blockPeak = juce::jmax(blockPeak, m);
        if (m >= loudLinear)
        {
            if (firstLoud < 0)
                firstLoud = i;
            lastLoud = i;
        }
    }
    for (float old = peak.load(); blockPeak > old && ! peak.compare_exchange_weak(old, blockPeak);)
    {
    }
    if (lastLoud >= 0)
    {
        loudSeen.store(true, std::memory_order_release);
        lastLoudReceived.store(receivedLocal + lastLoud + 1, std::memory_order_release);
    }

    const auto receivedBefore = receivedLocal;
    receivedLocal += frames;

    auto publish = [this]
    {
        received.store(receivedLocal, std::memory_order_release);
        kept.store(keptLocal, std::memory_order_release);
    };

    if (limitHit.load(std::memory_order_relaxed))
    {
        flushSilence(0); // silence owed before the limit is still written
        publish();
        return;
    }

    int startIndex = 0;
    int fromPreroll = 0;
    if (! started)
    {
        if (opts.trimSilence)
        {
            if (firstLoud < 0)
            {
                // Silence so far: remember only the last few frames, for the margin before the first sound.
                if (frames >= marginFrames)
                {
                    for (int c = 0; c < channels; ++c)
                        for (int i = 0; i < marginFrames; ++i)
                            preroll.setSample(c, i, get(c < sourceChannels ? c : 0, frames - marginFrames + i));
                    prerollCount = marginFrames;
                }
                else
                {
                    const int keep = juce::jmin(prerollCount, marginFrames - frames);
                    for (int c = 0; c < channels; ++c)
                    {
                        for (int i = 0; i < keep; ++i)
                            preroll.setSample(c, i, preroll.getSample(c, prerollCount - keep + i));
                        for (int i = 0; i < frames; ++i)
                            preroll.setSample(c, keep + i, get(c < sourceChannels ? c : 0, i));
                    }
                    prerollCount = keep + frames;
                }
                publish();
                return;
            }
            startIndex = juce::jmax(0, firstLoud - marginFrames);
            fromPreroll = juce::jmin(prerollCount, marginFrames - (firstLoud - startIndex));
            fromPreroll = juce::jmax(0, fromPreroll);
        }
        started = true;
        leading.store(receivedBefore + startIndex - fromPreroll, std::memory_order_release);
        if (fromPreroll > 0)
        {
            const int offset = prerollCount - fromPreroll;
            writeSegment(channels, offset, fromPreroll, [this](int c, int i) { return preroll.getSample(c, i); });
        }
    }

    int count = frames - startIndex;
    if (opts.maxFrames > 0)
    {
        const auto room = opts.maxFrames - keptLocal - droppedPending; // the silence owed comes first
        if (room <= count)
        {
            count = static_cast<int>(juce::jmax<juce::int64>(0, room));
            limitHit.store(true);
        }
    }

    if (writeSegment(sourceChannels, startIndex, count, get))
    {
        // The last loud frame inside the part that was kept (a block cut by the limit can have loud frames beyond it).
        int lastKeptLoud = lastLoud < startIndex + count ? lastLoud : -1;
        if (lastLoud >= startIndex + count)
        {
            for (int i = startIndex + count - 1; i >= startIndex && lastKeptLoud < 0; --i)
            {
                float m = 0.0f;
                for (int c = 0; c < channels; ++c)
                    m = juce::jmax(m, std::abs(get(c < sourceChannels ? c : 0, i)));
                if (m >= loudLinear)
                    lastKeptLoud = i;
            }
        }
        if (lastKeptLoud >= startIndex)
            lastLoudKept = segmentStartKept + (lastKeptLoud - startIndex);
    }
    publish();
}

TakeResult TakeWriter::finish()
{
    if (finalised || writerThread == nullptr)
        return finalResult;
    finalised = true;

    held.store(false);
    // The producer is done: write the silence still owed, so the file ends where the timeline did.
    while (droppedPending > 0 && writerThread->isThreadRunning())
    {
        const auto before = droppedPending;
        flushSilence(0);
        if (droppedPending == before)
            juce::Thread::sleep(1); // the ring is full: let the writer drain
    }
    kept.store(keptLocal, std::memory_order_release);
    finishedFlag.store(true);
    writerThread->stopThread(-1); // no limit: the caller is a worker, and a half-written take must not be reported as finished
    writerThread.reset();

    TakeResult result;
    result.file = fileOut;
    result.sampleRate = opts.sampleRate;
    result.channels = opts.channels;
    // After a failed write the file holds less than was handed over.
    const auto inFile = juce::jmin(keptLocal, writtenFrames.load());
    result.frames = inFile;
    result.leadingTrimFrames = leading.load() < 0 ? receivedLocal : leading.load();
    result.framesReceived = receivedLocal;
    result.everLoud = opts.trimSilence ? lastLoudKept >= 0 : loudSeen.load();
    result.limitReached = limitHit.load();
    result.writeFailed = failed.load();
    result.overruns = overrunCount.load();

    if (opts.trimSilence && lastLoudKept >= 0 && ! result.writeFailed)
    {
        const auto keep = juce::jmin(inFile, lastLoudKept + 1 + marginFrames);
        if (keep < inFile && trimWavTail(fileOut, keep).wasOk())
        {
            result.frames = keep;
            result.trimmedTail = true;
        }
    }
    finalResult = result;
    return finalResult;
}
} // namespace sampler::platform
