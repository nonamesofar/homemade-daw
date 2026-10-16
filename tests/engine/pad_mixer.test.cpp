// M3 3.3: the pad player core rendered offline with scripted events (no device, no Tracktion).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/PadMixer.h"
#include "platform/RealtimeGuard.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

using namespace sampler;
using sampler::engine::PadEvent;
using sampler::engine::PadMixer;
using sampler::render::PlayMode;
using sampler::render::SliceBank;
using sampler::render::SliceBuffer;

//==============================================================================
// Counts allocations made inside our real-time scopes (debug builds).
namespace
{
std::atomic<int> gRealtimeAllocations{0};
}

#if JUCE_DEBUG
void* operator new(std::size_t size)
{
    if (sampler::platform::inRealtimeScope())
        gRealtimeAllocations.fetch_add(1);
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace
{
constexpr double rate = 44100.0;
constexpr double pi = 3.14159265358979323846;

/** A slice of a sine (the same on both channels) of `seconds`, level `amplitude`. */
SliceBuffer::Ptr sineSlice(double frequency, double seconds, float amplitude = 0.5f, double sliceRate = rate)
{
    const int frames = static_cast<int>(seconds * sliceRate);
    SliceBuffer::Ptr buffer = new SliceBuffer(sliceRate, frames);
    for (int i = 0; i < frames; ++i)
    {
        const auto v = amplitude * static_cast<float>(std::sin(2.0 * pi * frequency * i / sliceRate));
        buffer->data()[2 * i] = v;
        buffer->data()[2 * i + 1] = v;
    }
    return buffer;
}

/** A slice with a constant level, handy for checking exact fades. */
SliceBuffer::Ptr constantSlice(double seconds, float level)
{
    const int frames = static_cast<int>(seconds * rate);
    SliceBuffer::Ptr buffer = new SliceBuffer(rate, frames);
    std::fill(buffer->data(), buffer->data() + 2 * frames, level);
    return buffer;
}

struct Harness
{
    PadMixer mixer;
    std::vector<float> left;

    Harness() { mixer.prepare(rate); }

    void bank(std::initializer_list<std::pair<int, std::pair<SliceBuffer::Ptr, PlayMode>>> slices)
    {
        SliceBank::Ptr b = new SliceBank();
        for (const auto& [slot, entry] : slices)
        {
            b->buffers[static_cast<size_t>(slot)] = entry.first;
            b->modes[static_cast<size_t>(slot)] = entry.second;
        }
        mixer.publishBank(b);
    }

    void on(int slot) { REQUIRE(mixer.pushEvent({PadEvent::Type::on, slot, 1.0f})); }
    void off(int slot) { REQUIRE(mixer.pushEvent({PadEvent::Type::off, slot, 1.0f})); }
    void allOff() { REQUIRE(mixer.pushEvent({PadEvent::Type::allOff, 0, 1.0f})); }
    void stopAll() { REQUIRE(mixer.pushEvent({PadEvent::Type::stopAll, 0, 1.0f})); }
    /** Fills the event queue with presses of a pad that has no slice, until it refuses more. */
    void fillQueue()
    {
        int pushed = 0;
        while (mixer.pushEvent({PadEvent::Type::on, 200, 1.0f}))
            ++pushed;
        REQUIRE(pushed > 100);
    }

    /** Renders `frames` frames in blocks of `block`, appending the left channel to `left`. */
    void run(int frames, int block = 64)
    {
        std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        for (int done = 0; done < frames; done += block)
        {
            const int n = std::min(block, frames - done);
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            float* channels[2] = {l.data(), r.data()};
            mixer.process(channels, 2, n);
            left.insert(left.end(), l.begin(), l.begin() + n);
        }
    }

    float peak(int from, int to) const
    {
        float p = 0.0f;
        for (size_t i = static_cast<size_t>(std::max(0, from)); i < std::min(static_cast<size_t>(std::max(0, to)), left.size()); ++i)
            p = std::max(p, std::abs(left[i]));
        return p;
    }

    float largestStep(int from, int to) const
    {
        float p = 0.0f;
        for (size_t i = std::max<size_t>(static_cast<size_t>(std::max(0, from)), 1); i < std::min(static_cast<size_t>(std::max(0, to)), left.size()); ++i)
            p = std::max(p, std::abs(left[i] - left[i - 1]));
        return p;
    }
};

int ms(double milliseconds) { return static_cast<int>(milliseconds * rate / 1000.0); }
} // namespace

TEST_CASE("a one-shot plays to its end and the key-up changes nothing", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.1, 0.5f), PlayMode::oneShot}}});
    h.on(0);
    h.run(ms(20));
    h.off(0);
    h.run(ms(200));
    CHECK(h.peak(ms(5), ms(95)) == Catch::Approx(0.5f));
    CHECK(h.peak(ms(101), ms(220)) == 0.0f);
    CHECK(h.mixer.soundingCount(0) == 0);
}

TEST_CASE("a pad without a slice plays nothing", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.1, 0.5f), PlayMode::oneShot}}});
    h.on(5);
    h.run(ms(50));
    CHECK(h.peak(0, static_cast<int>(h.left.size())) == 0.0f);
    CHECK(h.mixer.soundingCount(5) == 0);
}

TEST_CASE("nothing plays before a bank is published", "[pads][mixer]")
{
    Harness h;
    h.on(0);
    h.run(ms(20));
    CHECK(h.peak(0, static_cast<int>(h.left.size())) == 0.0f);
}

TEST_CASE("a gate slice stops 10 ms after the key is let go", "[pads][mixer]")
{
    Harness h;
    h.bank({{1, {constantSlice(1.0, 0.5f), PlayMode::gate}}});
    h.on(1);
    h.run(ms(100), 1); // sample-exact event timing: blocks of one frame
    const int releasedAt = static_cast<int>(h.left.size());
    CHECK(h.mixer.soundingCount(1) == 1);
    h.off(1);
    h.run(ms(60), 1);
    CHECK(h.peak(releasedAt - 100, releasedAt) == Catch::Approx(0.5f));
    // It fades linearly and is silent 10 ms later (plus the one block the event waited for).
    CHECK(h.peak(releasedAt + ms(10) + 2, releasedAt + ms(60)) == 0.0f);
    CHECK(h.peak(releasedAt + ms(2), releasedAt + ms(4)) > 0.1f);
    CHECK(h.mixer.soundingCount(1) == 0);
}

TEST_CASE("a tap on a gate slice plays only its start", "[pads][mixer]")
{
    Harness h;
    h.bank({{1, {constantSlice(1.0, 0.5f), PlayMode::gate}}});
    h.on(1);
    h.run(ms(5), 1);
    h.off(1);
    h.run(ms(200));
    CHECK(h.peak(ms(30), static_cast<int>(h.left.size())) == 0.0f);
}

TEST_CASE("a loop slice repeats while held with no step at the wrap and fades out on release", "[pads][mixer]")
{
    Harness h;
    // 20 ms of a 200 Hz sine: 4 whole periods, so the end meets the start; the 3 ms crossfade must not add a step.
    h.bank({{2, {sineSlice(200.0, 0.02), PlayMode::loop}}});
    h.on(2);
    h.run(ms(200));
    const int releasedAt = static_cast<int>(h.left.size());
    CHECK(h.mixer.soundingCount(2) == 1);
    CHECK(h.peak(ms(100), ms(200)) > 0.3f);
    // Sine at 200 Hz, amplitude 0.5: the largest natural step is 2*pi*200/44100*0.5 = 0.0142.
    CHECK(h.largestStep(ms(2), releasedAt) < 0.03f);
    h.off(2);
    h.run(ms(50));
    CHECK(h.peak(releasedAt + ms(12), static_cast<int>(h.left.size())) == 0.0f);
    CHECK(h.mixer.soundingCount(2) == 0);
}

TEST_CASE("a loop slice repeats three times when held for three lengths", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.05, 0.4f), PlayMode::loop}}});
    h.on(0);
    h.run(ms(150));
    h.off(0);
    h.run(ms(40));
    CHECK(h.peak(ms(5), ms(145)) == Catch::Approx(0.4f));
    CHECK(h.peak(ms(165), static_cast<int>(h.left.size())) == 0.0f);
}

TEST_CASE("the 17th note takes the voice that started first, with a short fade", "[pads][mixer]")
{
    Harness h;
    SliceBank::Ptr bank = new SliceBank();
    for (int s = 0; s < 17; ++s)
    {
        bank->buffers[static_cast<size_t>(s)] = constantSlice(1.0, 0.01f);
        bank->modes[static_cast<size_t>(s)] = PlayMode::oneShot;
    }
    h.mixer.publishBank(bank);
    for (int s = 0; s < 16; ++s)
    {
        h.on(s);
        h.run(64);
    }
    for (int s = 0; s < 16; ++s)
        CHECK(h.mixer.soundingCount(s) == 1);
    h.on(16);
    h.run(64);
    // The oldest (slot 0) is fading; 3 ms later it is gone and the others and the new one still sound.
    h.run(ms(5));
    CHECK(h.mixer.soundingCount(0) == 0);
    for (int s = 1; s <= 16; ++s)
        CHECK(h.mixer.soundingCount(s) == 1);
}

TEST_CASE("hitting a sounding pad again restarts it without a click", "[pads][mixer]")
{
    Harness h;
    h.bank({{3, {sineSlice(100.0, 0.5), PlayMode::oneShot}}});
    h.on(3);
    h.run(ms(105)); // inside the sine, away from a zero crossing
    h.on(3);
    h.run(ms(60));
    // 100 Hz at amplitude 0.5 changes by at most 0.007 per sample; the old voice fades and the new one fades in.
    CHECK(h.largestStep(ms(20), static_cast<int>(h.left.size())) < 0.02f);
    CHECK(h.mixer.soundingCount(3) == 1);
}

TEST_CASE("a bank swap while a voice sounds keeps the old buffer playing", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.2, 0.3f), PlayMode::oneShot}}});
    h.on(0);
    h.run(ms(50));
    h.bank({{0, {constantSlice(0.2, 0.9f), PlayMode::oneShot}}});
    h.run(ms(50));
    // The running voice still plays 0.3; the next hit uses the new bank.
    CHECK(h.peak(ms(60), ms(95)) == Catch::Approx(0.3f));
    h.run(ms(150));
    h.on(0);
    h.run(ms(50));
    CHECK(h.peak(static_cast<int>(h.left.size()) - ms(30), static_cast<int>(h.left.size())) == Catch::Approx(0.9f));
}

TEST_CASE("buffers handed back by the audio thread are released on the message thread", "[pads][mixer]")
{
    SliceBuffer::Ptr kept = constantSlice(0.05, 0.3f);
    {
        Harness h;
        SliceBank::Ptr bank = new SliceBank();
        bank->buffers[0] = kept;
        h.mixer.publishBank(bank);
        h.on(0);
        h.run(ms(100));
        h.mixer.publishBank(new SliceBank());
        h.run(ms(10));
        // The first bank is retired by the audio thread but stays referenced until the message thread drains.
        CHECK(bank->getReferenceCount() >= 2);
        h.mixer.drainGarbage();
        CHECK(bank->getReferenceCount() == 1); // only the local pointer is left
        CHECK(kept->getReferenceCount() == 2);  // `kept` and the bank
        bank = nullptr;
        CHECK(kept->getReferenceCount() == 1);
    }
    CHECK(kept->getReferenceCount() == 1);
}

TEST_CASE("all pads off lets go of gate and loop slices but not one-shots", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.5, 0.2f), PlayMode::gate}},
            {1, {constantSlice(0.5, 0.2f), PlayMode::loop}},
            {2, {constantSlice(0.5, 0.2f), PlayMode::oneShot}}});
    h.on(0);
    h.on(1);
    h.on(2);
    h.run(ms(20));
    h.allOff();
    h.run(ms(40));
    CHECK(h.mixer.soundingCount(0) == 0);
    CHECK(h.mixer.soundingCount(1) == 0);
    CHECK(h.mixer.soundingCount(2) == 1);
}

TEST_CASE("stop all fades every voice out in 3 ms, one-shots too", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.5, 0.4f), PlayMode::oneShot}}, {1, {constantSlice(0.5, 0.4f), PlayMode::loop}}});
    h.on(0);
    h.on(1);
    h.run(ms(20));
    const int at = static_cast<int>(h.left.size());
    h.stopAll();
    h.run(ms(30));
    CHECK(h.mixer.soundingCount(0) == 0);
    CHECK(h.mixer.soundingCount(1) == 0);
    CHECK(h.peak(at + ms(3.5), at + ms(30)) == 0.0f);
    CHECK(h.largestStep(at, at + ms(30)) < 0.05f); // a fade, not a cut
}

TEST_CASE("a release is not lost when the event queue is full", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(1.0, 0.2f), PlayMode::gate}}, {1, {constantSlice(1.0, 0.2f), PlayMode::oneShot}}});
    h.on(0);
    h.on(1);
    h.run(ms(20));
    h.fillQueue();
    CHECK_FALSE(h.mixer.pushEvent({PadEvent::Type::on, 200, 1.0f}));
    CHECK(h.mixer.pushEvent({PadEvent::Type::off, 0, 1.0f})); // carried by a flag
    h.run(ms(40));
    CHECK(h.mixer.soundingCount(0) == 0);
    CHECK(h.mixer.soundingCount(1) == 1); // a one-shot ignores key-up

    h.fillQueue();
    CHECK(h.mixer.pushEvent({PadEvent::Type::stopAll, 0, 1.0f}));
    h.run(ms(40));
    CHECK(h.mixer.soundingCount(1) == 0);
}

TEST_CASE("all pads off is not lost when the event queue is full", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(1.0, 0.2f), PlayMode::loop}}});
    h.on(0);
    h.run(ms(20));
    h.fillQueue();
    CHECK(h.mixer.pushEvent({PadEvent::Type::allOff, 0, 1.0f}));
    h.run(ms(40));
    CHECK(h.mixer.soundingCount(0) == 0);
}

TEST_CASE("a slice rendered at another rate than the device is not played", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {sineSlice(440.0, 0.1, 0.5f, 48000.0), PlayMode::oneShot}}});
    h.on(0);
    h.run(ms(20));
    CHECK(h.peak(0, static_cast<int>(h.left.size())) == 0.0f);
}

TEST_CASE("a mono destination gets the left and right mix", "[pads][mixer]")
{
    Harness h;
    h.bank({{0, {constantSlice(0.1, 0.4f), PlayMode::oneShot}}});
    h.on(0);
    std::vector<float> mono(512, 0.0f);
    float* channels[1] = {mono.data()};
    h.mixer.process(channels, 1, 512);
    CHECK(mono[300] == Catch::Approx(0.4f));
}

#if JUCE_DEBUG
TEST_CASE("playing pads for 30 seconds allocates nothing in the audio code", "[pads][mixer][realtime]")
{
    Harness h;
    SliceBank::Ptr bank = new SliceBank();
    for (int s = 0; s < 20; ++s)
    {
        bank->buffers[static_cast<size_t>(s)] = sineSlice(110.0 + 20.0 * s, 0.3);
        bank->modes[static_cast<size_t>(s)] = static_cast<PlayMode>(s % 3);
    }
    h.mixer.publishBank(bank);
    h.left.reserve(static_cast<size_t>(31 * rate));

    gRealtimeAllocations.store(0);
    const int blocks = static_cast<int>(30.0 * rate / 128.0);
    std::vector<float> l(128), r(128);
    for (int b = 0; b < blocks; ++b)
    {
        if (b % 5 == 0)
            h.on((b / 5) % 20);
        if (b % 7 == 0)
            h.off((b / 7) % 20);
        if (b % 1000 == 0)
            h.mixer.publishBank(bank); // the same bank again: exchanged, the old reference handed back
        if (b % 50 == 0)
            h.mixer.drainGarbage();
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        float* channels[2] = {l.data(), r.data()};
        h.mixer.process(channels, 2, 128);
    }
    CHECK(gRealtimeAllocations.load() == 0);
}
#endif
