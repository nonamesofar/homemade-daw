#pragma once

#include "render/SliceRenderer.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace sampler::render
{
/** One slice to render: which pad slot it is for, which part of the sound, and how it should sound. */
struct SliceRequest
{
    int slot = 0;
    juce::File file; // the decoded WAV of the sound
    juce::int64 startFrame = 0;
    juce::int64 endFrame = 0; // exclusive
    SliceParams params;
};

/**
    Which rendered slices stay in memory (design D6: 200 MB per sound). When the total passes the limit the slice used
    least recently goes first; the slice that was just added is never the one evicted. Plain logic, unit tested.
*/
class RenderBudget
{
public:
    explicit RenderBudget(size_t limitBytes) : limit(limitBytes) {}

    void setLimit(size_t limitBytes) { limit = limitBytes; }
    size_t limitBytes() const { return limit; }

    /** A slice was rendered (or re-rendered): returns the slots to drop to get back under the limit, oldest use first. */
    std::vector<int> add(int slot, size_t bytes);
    /** The slice was played or selected. */
    void use(int slot);
    void remove(int slot);
    void clear();

    size_t total() const;
    bool contains(int slot) const { return entries.count(slot) != 0; }

private:
    struct Entry
    {
        size_t bytes = 0;
        juce::uint64 lastUse = 0;
    };
    std::map<int, Entry> entries;
    size_t limit;
    juce::uint64 tick = 0;
};

/**
    Renders slices on worker threads (design D6). One pending request per slot: a newer request for the same slot replaces
    one that has not started, and the result of one that was overtaken while it ran is dropped, so rapid parameter changes
    deliver only the last result. Requests for slices that were just played or selected go first. Results are handed back
    on the message thread. The renderer is injected, so the scheduler is tested with a fake one.
*/
class RenderScheduler
{
public:
    using Renderer = std::function<SliceBuffer::Ptr(const SliceRequest&)>;

    /** `numThreads` 0 = one fewer than the cores (at least one). */
    explicit RenderScheduler(Renderer, int numThreads = 0, size_t budgetBytes = 200u * 1024u * 1024u);
    /** Drops what is queued and waits for the renders that are running. */
    ~RenderScheduler();

    /** Message thread. Replaces a request for the slot that has not started. Higher priority first, then first come. */
    void request(const SliceRequest&, int priority = 0);
    /** Moves a queued request for the slot to the front (a pad was hit, a slice was selected). */
    void prioritise(int slot);
    /** The slice was played or selected: it is the last to go when the memory budget is full. */
    void used(int slot);
    /** Drops everything queued and ignores renders that are running. */
    void cancelAll();
    /** Forgets the slot's memory accounting (the owner dropped its buffer). */
    void forget(int slot);

    int pendingCount() const;
    /** Nothing queued and nothing running. */
    bool isIdle() const;
    size_t bytesInUse() const { return budget.total(); }
    void setBudgetBytes(size_t bytes) { budget.setLimit(bytes); }

    /** Message thread: a render finished and is still wanted. */
    std::function<void(int slot, SliceBuffer::Ptr)> onRendered;
    /** Message thread: the memory budget made room; the owner drops the slot's buffer (it can be requested again). */
    std::function<void(int slot)> onEvicted;

private:
    struct Pending
    {
        SliceRequest request;
        int priority = 0;
        juce::uint64 sequence = 0;
        juce::uint64 generation = 0;
    };
    class Runner;

    bool popNext(Pending& out);
    void deliver(int slot, juce::uint64 generation, SliceBuffer::Ptr);

    Renderer renderer;
    RenderBudget budget;

    mutable std::mutex lock;
    std::map<int, Pending> pending;      // by slot, not started
    std::map<int, juce::uint64> latest;  // by slot: the generation whose result is wanted
    juce::uint64 nextGeneration = 1;
    juce::uint64 nextSequence = 1;
    int running = 0;
    int maxPriority = 0;

    juce::ThreadPool pool;
    JUCE_DECLARE_WEAK_REFERENCEABLE(RenderScheduler)
};
} // namespace sampler::render
