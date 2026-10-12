#include "Transport.h"

#include "EngineHost.h"
#include "EngineSetup.h"

#include <tracktion_graph/tracktion_graph.h>

namespace te = tracktion::engine;

namespace sampler::engine
{
namespace
{
const juce::Identifier loopOnProperty("sampler_loopOn");
}

struct Transport::Impl : private juce::Timer
{
    Impl(te::Edit& e, EngineHost& h) : edit(e), host(h)
    {
        edit.clickTrackEmphasiseBars = true;
        startTimerHz(30);
    }

    ~Impl() override { stopTimer(); }

    te::TransportControl& transport() const { return edit.getTransport(); }
    bool loopOn() const { return transport().state[loopOnProperty]; }

    double positionSeconds() const
    {
        if (transport().isPlaying())
            if (auto* context = edit.getCurrentPlaybackContext())
                return context->getAudibleTimelineTime().inSeconds();
        return transport().getPosition().inSeconds();
    }

    /** Issues the play once the output is ready and the playback context exists (from the timer). */
    void tryStart()
    {
        if (! pendingPlay || ! host.isAudioReady())
            return;
        if (edit.getCurrentPlaybackContext() == nullptr)
        {
            transport().ensureContextAllocated();
            return; // next tick: never play in the turn that allocated the context
        }
        pendingPlay = false;
        transport().play(false);
        applyLoop(transport().getPosition().inSeconds());
        reapplyLoopTicks = 3; // the play itself pushes loop times to the playhead; apply ours again just after
    }

    /**
        Looping on and before the loop end: the playhead rolls into the loop (from before it) or loops (inside it).
        After the loop end: looping stays off in the engine so playback runs on to the end.
    */
    void applyLoop(double position)
    {
        const auto range = transport().getLoopRange();
        const bool wanted = loopOn() && range.getLength() > tracktion::TimeDuration::fromSeconds(0.001);
        const bool engageNow = wanted && position < range.getEnd().inSeconds();
        if (transport().looping.get() != engageNow)
            transport().looping = engageNow;

        auto* context = edit.getCurrentPlaybackContext();
        auto* playHead = context != nullptr ? context->getNodePlayHead() : nullptr;
        if (playHead == nullptr)
            return;
        const auto rate = context->getSampleRate();
        if (! engageNow)
        {
            playHead->setLoopRange(false, {}, true);
            return;
        }
        // The same range TransportControl pushes on its own timer, so its pushes change nothing afterwards.
        const auto loop = range.withEnd(std::max(range.getEnd(), range.getStart() + tracktion::TimeDuration::fromSeconds(0.001)));
        playHead->setLoopRange(true, tracktion::toSamples(loop, rate), false);
        if (position < range.getStart().inSeconds() && transport().isPlaying())
            playHead->setRollInToLoop(tracktion::toSamples(tracktion::TimePosition::fromSeconds(position), rate));
    }

    void timerCallback() override
    {
        if (pendingPlay)
            tryStart();
        if (reapplyLoopTicks > 0 && --reapplyLoopTicks == 0)
            applyLoop(positionSeconds());
        const auto range = transport().getLoopRange();
        if (range != lastRange)
        {
            lastRange = range;
            applyLoop(positionSeconds());
        }
    }

    te::Edit& edit;
    EngineHost& host;
    bool pendingPlay = false;
    int reapplyLoopTicks = 0;
    tracktion::TimeRange lastRange;
};

Transport::Transport(te::Edit& edit, EngineHost& host) : impl(std::make_unique<Impl>(edit, host)) {}
Transport::~Transport() = default;

void Transport::play()
{
    if (isPlaying())
        return;
    impl->pendingPlay = true;
    impl->tryStart();
}

void Transport::stop()
{
    impl->pendingPlay = false;
    if (impl->transport().isPlaying())
    {
        const auto at = impl->positionSeconds();
        impl->transport().stop(false, false);
        impl->transport().setPosition(tracktion::TimePosition::fromSeconds(at)); // stop where the playhead was heard
    }
}

bool Transport::isPlaying() const { return impl->transport().isPlaying(); }
bool Transport::isPlayPending() const { return impl->pendingPlay; }

double Transport::positionBeats() const
{
    return impl->edit.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(impl->positionSeconds())).inBeats();
}

bool Transport::locateBeats(double beats)
{
    if (locateLocked)
    {
        if (onLocateRefused)
            onLocateRefused();
        return false;
    }
    const auto t = impl->edit.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(std::max(0.0, beats)));
    impl->applyLoop(t.inSeconds()); // before the jump, so a jump into the loop is not clipped by an old range
    impl->transport().setPosition(t);
    if (impl->transport().isPlaying())
        impl->reapplyLoopTicks = 2;
    return true;
}

bool Transport::isLooping() const { return impl->loopOn(); }

void Transport::setLooping(bool on)
{
    impl->transport().state.setProperty(loopOnProperty, on, nullptr);
    impl->applyLoop(impl->positionSeconds());
}

bool Transport::isClickOn() const { return impl->edit.clickTrackEnabled.get(); }
void Transport::setClickOn(bool on) { impl->edit.clickTrackEnabled = on; }

void Transport::loopRangeChanged() { impl->applyLoop(impl->positionSeconds()); }
} // namespace sampler::engine
