// CaptureGap: when a loopback capture inserts silence (scripted packets, no device).
#include <catch2/catch_test_macros.hpp>

#include "platform/CaptureGap.h"

#include <cmath>

using sampler::platform::CaptureGap;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kPacket = 480; // 10 ms

/** Plays packets at their QPC position (device clock `drift` slower than QPC) and adds up what would be inserted. */
struct Script
{
    juce::int64 inserted = 0;
    juce::int64 previousEnd = 0;
    double qpcFrames = 0.0;

    void packet(bool discontinuity = false)
    {
        const auto at = static_cast<juce::int64>(std::llround(qpcFrames));
        inserted += CaptureGap::framesToInsert(previousEnd, at, discontinuity, kRate);
        previousEnd = at + kPacket;
        qpcFrames += kPacket * (1.0 + drift);
    }
    void silence(double seconds) { qpcFrames += seconds * kRate; }

    double drift = 0.0;
};
} // namespace

TEST_CASE("50 ppm of clock drift over 30 minutes inserts no silence", "[platform][capture]")
{
    Script s;
    s.drift = 50.0e-6;
    const int packets = 30 * 60 * 100;
    for (int i = 0; i < packets; ++i)
        s.packet();
    CHECK(s.inserted == 0);
    // Measured against the frames delivered, the drift was about 90 ms: the old rule would have inserted it.
    CHECK((s.qpcFrames - static_cast<double>(packets) * kPacket) / kRate > 0.08);
}

TEST_CASE("a real 5 second pause between packets is filled", "[platform][capture]")
{
    Script s;
    s.drift = 50.0e-6;
    for (int i = 0; i < 1000; ++i)
        s.packet();
    s.silence(5.0);
    s.packet();
    for (int i = 0; i < 1000; ++i)
        s.packet();
    CHECK(s.inserted >= 5 * 48000 - 100);
    CHECK(s.inserted <= 5 * 48000 + 100);
}

TEST_CASE("jitter and short holes are not silence, a flagged discontinuity is", "[platform][capture]")
{
    CHECK(CaptureGap::framesToInsert(1000, 1000 + 2400, false, kRate) == 0); // 50 ms
    CHECK(CaptureGap::framesToInsert(1000, 900, false, kRate) == 0);         // a packet stamped slightly early
    CHECK(CaptureGap::framesToInsert(1000, 1000 + 4801, false, kRate) == 4801); // just over 100 ms
    CHECK(CaptureGap::framesToInsert(1000, 1000 + 2400, true, kRate) == 2400);  // the device says it lost data
    CHECK(CaptureGap::framesToInsert(1000, 1000, true, kRate) == 0);
}
