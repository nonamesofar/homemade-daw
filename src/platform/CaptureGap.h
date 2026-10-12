#pragma once

#include <juce_core/juce_core.h>

namespace sampler::platform
{
/**
    When a loopback capture inserts silence (portable logic, tested without a device). A loopback device sends no
    packets while nothing plays, so a pause shows as a hole between the end of one packet and the start of the next on
    the QPC clock. The QPC clock and the device clock drift apart by tens of ppm: measured against the frames
    delivered so far, that drift would add up to tens of milliseconds over a long take. So the hole is measured from
    packet to packet (no drift builds up there) and only a real one, or a flagged discontinuity, is filled.
*/
struct CaptureGap
{
    /** A hole shorter than this between two packets is timestamp jitter, not silence. */
    static constexpr double minimumSeconds = 0.1;

    /**
        Frames of silence to insert before a packet. `previousEndFrame` is where the previous packet ended (its QPC
        position plus its length, in frames since the stream started; 0 before the first packet), `packetFrame` the QPC
        position of this packet.
    */
    static juce::int64 framesToInsert(juce::int64 previousEndFrame, juce::int64 packetFrame, bool discontinuity, double sampleRate)
    {
        const auto hole = packetFrame - previousEndFrame;
        if (hole <= 0)
            return 0;
        return discontinuity || static_cast<double>(hole) > minimumSeconds * sampleRate ? hole : 0;
    }
};
} // namespace sampler::platform
