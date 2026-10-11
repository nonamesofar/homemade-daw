#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace sampler
{
/**
    Remembers the click on a sound that arrived before the audio system was ready (design D-6). Plain logic with an
    injected clock (milliseconds), so the rules are testable:
      - the latest click wins: a click on another row replaces the pending one;
      - a second click on the pending row cancels it (the same "click again to stop" as while playing);
      - a request older than `timeoutMs` is dropped when the queue is asked, and the caller tells the user.
*/
class AuditionRequestQueue
{
public:
    static constexpr juce::uint32 timeoutMs = 10000;

    /** A click while the audio is not ready. */
    void click(const juce::String& sourceId, juce::uint32 nowMs)
    {
        if (pending.has_value() && pending->sourceId == sourceId)
        {
            pending.reset();
            return;
        }
        pending = Request{sourceId, nowMs};
    }

    /** The audio became ready: the sound to start now, if a request is waiting and still fresh. Clears the queue. */
    std::optional<juce::String> takeForPlayback(juce::uint32 nowMs)
    {
        if (! pending.has_value())
            return std::nullopt;
        const auto request = *pending;
        pending.reset();
        if (nowMs - request.clickedAtMs > timeoutMs)
            return std::nullopt;
        return request.sourceId;
    }

    /** True if a request has waited longer than the timeout; it is dropped. The caller shows "no audio output". */
    bool dropIfExpired(juce::uint32 nowMs)
    {
        if (pending.has_value() && nowMs - pending->clickedAtMs > timeoutMs)
        {
            pending.reset();
            return true;
        }
        return false;
    }

    bool hasPending() const { return pending.has_value(); }
    juce::String pendingId() const { return pending.has_value() ? pending->sourceId : juce::String(); }

private:
    struct Request
    {
        juce::String sourceId;
        juce::uint32 clickedAtMs = 0;
    };
    std::optional<Request> pending;
};
} // namespace sampler
