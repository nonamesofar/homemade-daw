#include "Audition.h"

#include "EngineHost.h"
#include "EngineSetup.h"

namespace te = tracktion::engine;

namespace sampler
{
struct Audition::Impl : private juce::Timer
{
    explicit Impl(EngineHost& h) : host(h) {}

    ~Impl() override { stopTimer(); }

    /**
        Starts the transport a moment after the playback context was allocated. Playing in the same message-loop turn
        makes the transport stop itself within 0.15 s (observed: only the first play after a new edit, and only when no
        message ran in between), so the start waits for the loop to run once.
    */
    void startSoon() { startTimer(startDelayMs); }

    void timerCallback() override
    {
        stopTimer();
        if (edit != nullptr)
            edit->getTransport().play(false);
    }

    void stopCurrent()
    {
        stopTimer();
        if (edit != nullptr)
            edit->getTransport().stop(false, false);
        edit.reset();
        playingSourceId = {};
        lengthSeconds = 0.0;
    }

    static constexpr int startDelayMs = 30;

    EngineHost& host;
    std::unique_ptr<te::Edit> edit;
    juce::String playingSourceId;
    double lengthSeconds = 0.0;
};

Audition::Audition(EngineHost& host) : impl(std::make_unique<Impl>(host)) {}

Audition::~Audition() { impl->stopCurrent(); }

bool Audition::play(const juce::File& decodedWav, const juce::String& sourceId)
{
    impl->stopCurrent();

    auto& engine = impl->host.tracktionEngine();
    const te::AudioFile audioFile(engine, decodedWav);
    if (! audioFile.isValid())
        return false;

    auto edit = te::Edit::createEditForPreviewingFile(engine, decodedWav, nullptr, false, false, nullptr, {});
    if (edit == nullptr)
        return false;

    auto& transport = edit->getTransport();
    transport.ensureContextAllocated();
    transport.setPosition(tracktion::TimePosition());

    impl->lengthSeconds = audioFile.getLength();
    impl->playingSourceId = sourceId;
    impl->edit = std::move(edit);
    impl->startSoon();
    return true;
}

void Audition::stop() { impl->stopCurrent(); }

juce::String Audition::playingId() const { return impl->playingSourceId; }

double Audition::positionSeconds() const
{
    return impl->edit != nullptr ? impl->edit->getTransport().getPosition().inSeconds() : 0.0;
}

bool Audition::poll()
{
    if (impl->edit == nullptr)
        return false;
    if (impl->edit->getTransport().getPosition().inSeconds() < impl->lengthSeconds)
        return false;
    impl->stopCurrent();
    return true;
}
} // namespace sampler
