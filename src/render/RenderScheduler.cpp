#include "render/RenderScheduler.h"

#include <algorithm>
#include <thread>

namespace sampler::render
{
//==============================================================================
std::vector<int> RenderBudget::add(int slot, size_t bytes)
{
    entries[slot] = {bytes, ++tick};
    std::vector<int> evicted;
    while (total() > limit && entries.size() > 1)
    {
        auto oldest = entries.end();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->first != slot && (oldest == entries.end() || it->second.lastUse < oldest->second.lastUse))
                oldest = it;
        if (oldest == entries.end())
            break;
        evicted.push_back(oldest->first);
        entries.erase(oldest);
    }
    return evicted;
}

void RenderBudget::use(int slot)
{
    if (const auto it = entries.find(slot); it != entries.end())
        it->second.lastUse = ++tick;
}

void RenderBudget::remove(int slot) { entries.erase(slot); }

void RenderBudget::clear() { entries.clear(); }

size_t RenderBudget::total() const
{
    size_t sum = 0;
    for (const auto& [slot, entry] : entries)
        sum += entry.bytes;
    return sum;
}

//==============================================================================
class RenderScheduler::Runner : public juce::ThreadPoolJob
{
public:
    // Made on the message thread: creating a WeakReference's master is not thread-safe, so the worker only copies it.
    explicit Runner(RenderScheduler& s) : juce::ThreadPoolJob("slice render"), scheduler(s), weak(&s) {}

    JobStatus runJob() override
    {
        Pending next;
        if (! scheduler.popNext(next))
            return jobHasFinished;
        auto buffer = scheduler.renderer(next.request);
        const auto slot = next.request.slot;
        const auto generation = next.generation;
        const auto target = weak;
        {
            std::lock_guard<std::mutex> guard(scheduler.lock);
            --scheduler.running;
        }
        juce::MessageManager::callAsync([target, slot, generation, buffer] {
            if (target != nullptr)
                target->deliver(slot, generation, buffer);
        });
        return jobHasFinished;
    }

private:
    RenderScheduler& scheduler;
    juce::WeakReference<RenderScheduler> weak;
};

RenderScheduler::RenderScheduler(Renderer render, int numThreads, size_t budgetBytes)
    : renderer(std::move(render)), budget(budgetBytes),
      pool(numThreads > 0 ? numThreads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1))
{
}

RenderScheduler::~RenderScheduler()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        pending.clear();
        latest.clear();
    }
    pool.removeAllJobs(true, 20000);
}

void RenderScheduler::request(const SliceRequest& request, int priority)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        Pending item;
        item.request = request;
        item.priority = priority > 0 ? ++maxPriority : priority; // a prioritised request goes before everything queued
        item.sequence = nextSequence++;
        item.generation = nextGeneration++;
        latest[request.slot] = item.generation;
        pending[request.slot] = item;
    }
    pool.addJob(new Runner(*this), true);
}

void RenderScheduler::prioritise(int slot)
{
    std::lock_guard<std::mutex> guard(lock);
    if (const auto it = pending.find(slot); it != pending.end())
        it->second.priority = ++maxPriority;
}

void RenderScheduler::used(int slot) { budget.use(slot); }

void RenderScheduler::cancelAll()
{
    std::lock_guard<std::mutex> guard(lock);
    pending.clear();
    latest.clear();
    budget.clear();
}

void RenderScheduler::forget(int slot) { budget.remove(slot); }

int RenderScheduler::pendingCount() const
{
    std::lock_guard<std::mutex> guard(lock);
    return static_cast<int>(pending.size());
}

bool RenderScheduler::isIdle() const
{
    std::lock_guard<std::mutex> guard(lock);
    return pending.empty() && running == 0;
}

bool RenderScheduler::popNext(Pending& out)
{
    std::lock_guard<std::mutex> guard(lock);
    if (pending.empty())
        return false;
    auto best = pending.begin();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        if (it->second.priority > best->second.priority
            || (it->second.priority == best->second.priority && it->second.sequence < best->second.sequence))
            best = it;
    out = best->second;
    pending.erase(best);
    ++running;
    return true;
}

void RenderScheduler::deliver(int slot, juce::uint64 generation, SliceBuffer::Ptr buffer)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        const auto it = latest.find(slot);
        if (it == latest.end() || it->second != generation)
            return; // overtaken by a newer request, or cancelled
    }
    if (buffer == nullptr)
        return;
    const auto evicted = budget.add(slot, buffer->sizeInBytes());
    if (onRendered)
        onRendered(slot, buffer);
    for (const auto old : evicted)
        if (onEvicted)
            onEvicted(old);
}
} // namespace sampler::render
