// M3 3.2: the render scheduler with a fake renderer, and the memory budget.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "render/RenderScheduler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace sampler::render;

namespace
{
void pumpUntil(const std::function<bool()>& done, int timeoutMs = 10000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (! done() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
}

SliceRequest requestFor(int slot, float pitch = 0.0f)
{
    SliceRequest r;
    r.slot = slot;
    r.startFrame = 0;
    r.endFrame = 1000;
    r.params.pitchSemitones = pitch;
    return r;
}

/** A renderer that records what it was asked and can be held back until the test lets it go. */
struct FakeRenderer
{
    SliceBuffer::Ptr operator()(const SliceRequest& r)
    {
        std::unique_lock<std::mutex> guard(m);
        started.push_back({r.slot, r.params.pitchSemitones});
        cv.notify_all();
        cv.wait(guard, [this] { return ! gated || released > 0; });
        if (gated && released > 0)
            --released;
        SliceBuffer::Ptr buffer = new SliceBuffer(44100.0, frameCount);
        buffer->data()[0] = r.params.pitchSemitones; // tags the result with the request's parameter
        return buffer;
    }

    void waitForStarts(size_t n)
    {
        std::unique_lock<std::mutex> guard(m);
        cv.wait_for(guard, std::chrono::seconds(10), [&] { return started.size() >= n; });
    }

    void release(int n = 1)
    {
        std::lock_guard<std::mutex> guard(m);
        released += n;
        cv.notify_all();
    }

    void openGate()
    {
        std::lock_guard<std::mutex> guard(m);
        gated = false;
        cv.notify_all();
    }

    std::mutex m;
    std::condition_variable cv;
    std::vector<std::pair<int, float>> started;
    bool gated = false;
    int released = 0;
    int frameCount = 100;
};
} // namespace

TEST_CASE("rapid parameter changes deliver only the last result", "[render][scheduler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.gated = true;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1);
    std::vector<std::pair<int, float>> delivered;
    scheduler.onRendered = [&delivered](int slot, SliceBuffer::Ptr b) { delivered.push_back({slot, b->data()[0]}); };

    scheduler.request(requestFor(3, 1.0f)); // starts at once and waits at the gate
    fake.waitForStarts(1);
    for (float pitch : {2.0f, 3.0f, 4.0f, 5.0f}) // while it runs, newer requests replace each other in the queue
        scheduler.request(requestFor(3, pitch));
    CHECK(scheduler.pendingCount() == 1);
    fake.openGate();
    pumpUntil([&] { return scheduler.isIdle(); });
    pumpUntil([&] { return ! delivered.empty(); });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].first == 3);
    CHECK(delivered[0].second == 5.0f);
}

TEST_CASE("requests for different slots all arrive", "[render][scheduler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 2);
    std::vector<int> slots;
    scheduler.onRendered = [&slots](int slot, SliceBuffer::Ptr) { slots.push_back(slot); };
    for (int slot = 0; slot < 16; ++slot)
        scheduler.request(requestFor(slot));
    pumpUntil([&] { return slots.size() == 16; });
    std::sort(slots.begin(), slots.end());
    REQUIRE(slots.size() == 16);
    for (int slot = 0; slot < 16; ++slot)
        CHECK(slots[static_cast<size_t>(slot)] == slot);
}

TEST_CASE("higher priority goes first, then first come first served", "[render][scheduler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.gated = true;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1);
    scheduler.onRendered = [](int, SliceBuffer::Ptr) {};

    scheduler.request(requestFor(0)); // occupies the single worker
    fake.waitForStarts(1);
    scheduler.request(requestFor(1));
    scheduler.request(requestFor(2));
    scheduler.request(requestFor(3), 5); // a played slice
    scheduler.request(requestFor(4));
    scheduler.prioritise(2); // a slice that was just selected jumps ahead of the rest, behind nothing
    fake.release(10);
    pumpUntil([&] { return scheduler.isIdle(); });

    std::vector<int> order;
    for (const auto& s : fake.started)
        order.push_back(s.first);
    REQUIRE(order.size() == 5);
    CHECK(order[0] == 0);
    CHECK(order[1] == 2); // prioritise() beats every earlier priority
    CHECK(order[2] == 3);
    CHECK(order[3] == 1);
    CHECK(order[4] == 4);
}

TEST_CASE("a request with a priority goes before everything queued, prioritised slices included", "[render][scheduler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.gated = true;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1);
    scheduler.onRendered = [](int, SliceBuffer::Ptr) {};

    scheduler.request(requestFor(0)); // occupies the single worker
    fake.waitForStarts(1);
    scheduler.request(requestFor(1));
    scheduler.request(requestFor(2));
    scheduler.prioritise(1);
    scheduler.prioritise(2);
    scheduler.request(requestFor(3), 1); // a played slice
    fake.release(10);
    pumpUntil([&] { return scheduler.isIdle(); });

    REQUIRE(fake.started.size() == 4);
    CHECK(fake.started[1].first == 3);
}

TEST_CASE("cancelling also forgets the memory accounting", "[render][scheduler][budget]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.frameCount = 1000;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1);
    int delivered = 0;
    scheduler.onRendered = [&delivered](int, SliceBuffer::Ptr) { ++delivered; };
    scheduler.request(requestFor(0));
    pumpUntil([&] { return delivered == 1; });
    CHECK(scheduler.bytesInUse() > 0);
    scheduler.cancelAll();
    CHECK(scheduler.bytesInUse() == 0);
}

TEST_CASE("cancelling drops what is queued and ignores a render in flight", "[render][scheduler]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.gated = true;
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1);
    int delivered = 0;
    scheduler.onRendered = [&delivered](int, SliceBuffer::Ptr) { ++delivered; };

    scheduler.request(requestFor(0));
    fake.waitForStarts(1);
    scheduler.request(requestFor(1));
    scheduler.request(requestFor(2));
    scheduler.cancelAll();
    CHECK(scheduler.pendingCount() == 0);
    fake.openGate();
    pumpUntil([&] { return scheduler.isIdle(); });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(delivered == 0);
    CHECK(fake.started.size() == 1);
}

TEST_CASE("a slice rendered when memory is full pushes out the one used least recently", "[render][scheduler][budget]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeRenderer fake;
    fake.frameCount = 1000; // 8000 bytes per buffer
    RenderScheduler scheduler([&fake](const SliceRequest& r) { return fake(r); }, 1, 20000); // room for two
    std::vector<int> evicted;
    int delivered = 0;
    scheduler.onRendered = [&delivered](int, SliceBuffer::Ptr) { ++delivered; };
    scheduler.onEvicted = [&evicted](int slot) { evicted.push_back(slot); };

    for (int slot = 0; slot < 2; ++slot)
    {
        scheduler.request(requestFor(slot));
        pumpUntil([&] { return delivered == slot + 1; });
    }
    CHECK(evicted.empty());
    scheduler.used(0); // slice 0 was played after slice 1 was rendered
    scheduler.request(requestFor(2));
    pumpUntil([&] { return delivered == 3; });
    pumpUntil([&] { return ! evicted.empty(); });
    CHECK(evicted == std::vector<int>{1});
    CHECK(scheduler.bytesInUse() <= 20000);
}

TEST_CASE("the budget evicts in order of use and never the slice just added", "[render][budget]")
{
    RenderBudget budget(100);
    CHECK(budget.add(0, 40).empty());
    CHECK(budget.add(1, 40).empty());
    budget.use(0); // 1 is now the oldest
    CHECK(budget.add(2, 40) == std::vector<int>{1});
    CHECK(budget.total() == 80);
    CHECK_FALSE(budget.contains(1));

    // A slice bigger than the whole budget stays (it is needed to play) and everything else goes.
    CHECK(budget.add(3, 500) == std::vector<int>{0, 2});
    CHECK(budget.contains(3));
    CHECK(budget.total() == 500);

    budget.remove(3);
    CHECK(budget.total() == 0);
    budget.add(5, 10);
    budget.clear();
    CHECK(budget.total() == 0);
}
