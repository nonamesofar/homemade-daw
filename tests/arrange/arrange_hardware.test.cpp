// M2 tests that need a real audio output (label hardware: CI leaves them out): playing from a bar, looping, meters.
#include "ArrangeFixture.h"

#include <catch2/catch_approx.hpp>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;

namespace
{
/** Waits for the audio output; false (test skipped) when this machine has none. */
bool waitForAudio(ArrangeFixture& f)
{
    const auto end = juce::Time::getMillisecondCounter() + 15000;
    while (! f.host.isAudioReady() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    return f.host.isAudioReady();
}

void runFor(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

bool waitPlaying(sampler::engine::Transport& t, int ms = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (! t.isPlaying() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    return t.isPlaying();
}
} // namespace

TEST_CASE("play from bar 3: the position advances, and stop keeps it", "[arrange][transport][hardware]")
{
    ArrangeFixture f("hw-play", false);
    if (! waitForAudio(f))
        SKIP("no audio output");
    const auto sound = f.addSound("tone", 20.0);
    f.place(sound, 0, 0.0);
    auto& t = f.project->transport();

    t.locateBeats(8.0); // bar 3
    t.play();
    REQUIRE(waitPlaying(t));
    const auto start = t.positionBeats();
    CHECK(start >= 8.0);
    runFor(1000);
    const auto later = t.positionBeats();
    CHECK(later - start == Approx(2.0).margin(0.6)); // about one second at 120 BPM
    t.stop();
    runFor(100);
    CHECK_FALSE(t.isPlaying());
    const auto stopped = t.positionBeats();
    CHECK(stopped >= later);
    runFor(300);
    CHECK(t.positionBeats() == Approx(stopped)); // stays where it stopped
}

TEST_CASE("looping wraps from the loop end to its start", "[arrange][transport][loop][hardware]")
{
    ArrangeFixture f("hw-loop", false);
    if (! waitForAudio(f))
        SKIP("no audio output");
    const auto sound = f.addSound("tone", 20.0);
    f.place(sound, 0, 0.0);
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{4.0, 8.0})); // two seconds
    auto& t = f.project->transport();
    t.setLooping(true);
    t.locateBeats(6.0);
    t.play();
    REQUIRE(waitPlaying(t));

    double lowest = 100.0, highest = 0.0;
    int wraps = 0;
    double previous = t.positionBeats();
    const auto end = juce::Time::getMillisecondCounter() + 5000;
    while (juce::Time::getMillisecondCounter() < end)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        const auto now = t.positionBeats();
        if (now < previous - 1.0)
            ++wraps;
        previous = now;
        lowest = std::min(lowest, now);
        highest = std::max(highest, now);
    }
    t.stop();
    CHECK(wraps >= 2);          // 5 s of a 2 s loop
    CHECK(highest < 8.2);       // never runs past the loop end
    CHECK(lowest >= 3.9);       // and stays inside the loop
}

TEST_CASE("looping turned on after the loop end plays on to the end of the song", "[arrange][transport][loop][hardware]")
{
    ArrangeFixture f("hw-loop-after", false);
    if (! waitForAudio(f))
        SKIP("no audio output");
    const auto sound = f.addSound("tone", 20.0);
    f.place(sound, 0, 0.0);
    f.commands->apply(std::make_unique<cmd::SetLoop>(sampler::engine::LoopRange{4.0, 8.0}));
    auto& t = f.project->transport();
    t.locateBeats(12.0); // after the loop
    t.play();
    REQUIRE(waitPlaying(t));
    t.setLooping(true);
    runFor(1500);
    CHECK(t.positionBeats() > 12.0); // did not jump back into the loop
    t.stop();
}

TEST_CASE("track meters read their own track after the volume, and the master meter and over flag work",
          "[arrange][meters][hardware]")
{
    ArrangeFixture f("hw-meters", false);
    if (! waitForAudio(f))
        SKIP("no audio output");
    const auto loud = f.addSound("loud", 10.0, "dc");
    f.commands->apply(std::make_unique<cmd::AddTrack>());
    f.place(loud, 0, 0.0); // only the first track has sound
    auto& t = f.project->transport();
    auto& meters = f.project->levelMeters();
    const auto first = f.trackId(0), second = f.trackId(1);

    const auto peakOf = [&](const juce::String& id) {
        float peak = -100.0f;
        meters.readTrack(id); // clear what came before
        runFor(400);
        const auto reading = meters.readTrack(id);
        peak = std::max(reading.peakDb[0], reading.peakDb[1]);
        return peak;
    };

    meters.readTrack(first);
    meters.readTrack(second);
    meters.readMaster();
    t.play();
    REQUIRE(waitPlaying(t));
    const auto level = peakOf(first);
    CHECK(level == Approx(-6.0).margin(1.5)); // a constant 0.5
    CHECK(peakOf(second) < -60.0);            // the other track stays empty

    f.commands->apply(std::make_unique<cmd::SetTrackParam>(first, cmd::SetTrackParam::Control::volume, -12.0));
    ArrangeFixture::pump();
    runFor(1500); // the live volume follows through a short ramp
    const auto lowered = peakOf(first);
    INFO("level " << level << " lowered " << lowered << " volumeDb " << f.arrangement().tracks()[0].volumeDb);
    CHECK(level - lowered == Approx(12.0).margin(1.5));

    // Master over: +6 dB on the track and +6 dB on the master takes 0.5 over 0 dBFS.
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(first, cmd::SetTrackParam::Control::volume, 6.0));
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(juce::String(), cmd::SetTrackParam::Control::volume, 6.0));
    ArrangeFixture::pump();
    runFor(1500);
    meters.readMaster();
    runFor(400);
    const auto master = meters.readMaster();
    CHECK(master.peakDb[0] > 0.0f);
    CHECK(master.over);
    t.stop();
}
