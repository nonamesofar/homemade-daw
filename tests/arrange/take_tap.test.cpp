// M2b spike and engine tests (tasks 1.1, 2.1, 2.2): taps on a track and on the master, compared with offline renders.
// The offline renderer runs the same plugin chains as live playback, so a tap sees exactly what the device would get.
#include "ArrangeFixture.h"

#include "engine/OfflineRender.h"
#include "engine/TakeRecorder.h"
#include "platform/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

using Catch::Approx;
using sampler::test::ArrangeFixture;
namespace cmd = sampler::commands;

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
juce::File recordingFile(ArrangeFixture& f, const juce::String& name)
{
    return f.project->bundleDir().getChildFile("audio/recordings/" + name);
}

sampler::platform::TakeOptions optionsFor(const juce::File& file)
{
    sampler::platform::TakeOptions o;
    o.file = file;
    o.ringSeconds = 40;
    return o;
}

bool render(ArrangeFixture& f, const juce::File& dest)
{
    juce::String error;
    const auto ok = sampler::engine::renderToWav(*f.project, dest, {}, error);
    INFO(error);
    return ok;
}

/** Largest difference between two buffers over `frames` frames (channel 0 and 1). */
float maxDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int frames)
{
    float worst = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            worst = std::max(worst, std::abs(a.getSample(c, i) - b.getSample(c, i)));
    return worst;
}

struct TwoTracks
{
    explicit TwoTracks(const juce::String& name) : f(name)
    {
        f.commands->apply(std::make_unique<cmd::AddTrack>());
        const auto a = f.addSound("tone-a", 8.0, "tone", 1.0f, 0.5f);
        const auto b = f.addSound("tone-b", 8.0, "ramp", 0.6f, 0.6f);
        f.place(a, 0, 0.0);
        f.place(b, 1, 0.0);
        f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(0), cmd::SetTrackParam::Control::volume, -3.0));
        f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), cmd::SetTrackParam::Control::pan, 0.5));
        ArrangeFixture::pump();
    }
    ArrangeFixture f;
};
} // namespace

TEST_CASE("a Master tap during a render holds what the render holds", "[arrange][record][take]")
{
    TwoTracks t("tap-master");
    auto& f = t.f;
    juce::String error;
    auto tap = f.project->takeRecorder().tapMaster(optionsFor(recordingFile(f, "master.wav")), error);
    REQUIRE(tap != nullptr);
    CHECK_FALSE(tap->isReady());

    const auto rendered = f.root.getChildFile("render.wav");
    REQUIRE(render(f, rendered));
    const auto result = tap->finish();
    REQUIRE(result.frames > 0);

    double rate = 0.0;
    const auto take = sampler::test::readWav(result.file, &rate);
    const auto reference = sampler::test::readWav(rendered);
    REQUIRE(reference.getNumSamples() > 0);
    CHECK(rate == Approx(48000.0));
    CHECK(result.leadingTrimFrames == 0);
    const auto frames = std::min(take.getNumSamples(), reference.getNumSamples());
    CHECK(frames >= 16 * 24000 - 2400); // 8 seconds of music at least
    CHECK(maxDifference(take, reference, frames) < 1.0e-5f);
    CHECK(tap->firstFrameSeconds() == Approx(0.0).margin(0.001));
    CHECK(result.everLoud);
}

TEST_CASE("a track tap holds the track's output after its volume and pan", "[arrange][record][take]")
{
    TwoTracks t("tap-track");
    auto& f = t.f;
    juce::String error;
    auto tap = f.project->takeRecorder().tapTrack(f.trackId(1), optionsFor(recordingFile(f, "track2.wav")), error);
    REQUIRE(tap != nullptr);
    REQUIRE(render(f, f.root.getChildFile("full.wav")));
    const auto result = tap->finish();
    tap.reset();

    // The same track alone: the other one muted, no tap.
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(0), cmd::SetTrackParam::Control::mute, 1.0));
    ArrangeFixture::pump();
    REQUIRE(render(f, f.root.getChildFile("solo.wav")));

    const auto take = sampler::test::readWav(result.file);
    const auto solo = sampler::test::readWav(f.root.getChildFile("solo.wav"));
    const auto frames = std::min(take.getNumSamples(), solo.getNumSamples());
    REQUIRE(frames > 100000);
    CHECK(maxDifference(take, solo, frames) < 1.0e-5f);
    // Pan 0.5 moves the sound to the right: the take is not symmetric.
    CHECK(sampler::test::peakOf(take, 1, 0, frames) > sampler::test::peakOf(take, 0, 0, frames));
}

TEST_CASE("a tap on a muted track records silence", "[arrange][record][take]")
{
    TwoTracks t("tap-muted");
    auto& f = t.f;
    f.commands->apply(std::make_unique<cmd::SetTrackParam>(f.trackId(1), cmd::SetTrackParam::Control::mute, 1.0));
    ArrangeFixture::pump();
    juce::String error;
    auto tap = f.project->takeRecorder().tapTrack(f.trackId(1), optionsFor(recordingFile(f, "muted.wav")), error);
    REQUIRE(tap != nullptr);
    REQUIRE(render(f, f.root.getChildFile("full.wav")));
    const auto result = tap->finish();
    CHECK_FALSE(result.everLoud);
}

TEST_CASE("a tap is not part of a saved file, and recording leaves the history alone", "[arrange][record][take]")
{
    TwoTracks t("tap-save");
    auto& f = t.f;
    const auto token = f.commands->historyToken();
    const auto undoName = f.commands->undoName();
    const auto stepsBefore = f.project->undoManager().getUndoDescriptions().size();
    const auto savedBefore = f.arrangement().serialise();

    REQUIRE(f.project->save());
    const auto withoutTap = f.project->editFile().loadFileAsString();

    juce::String error;
    auto master = f.project->takeRecorder().tapMaster(optionsFor(recordingFile(f, "m.wav")), error);
    auto track = f.project->takeRecorder().tapTrack(f.trackId(0), optionsFor(recordingFile(f, "t.wav")), error);
    REQUIRE(master != nullptr);
    REQUIRE(track != nullptr);
    CHECK(f.arrangement().serialise().contains("samplerTap")); // in the live edit

    REQUIRE(f.project->save());
    const auto fileText = f.project->editFile().loadFileAsString();
    CHECK(fileText == withoutTap); // the file is the same with or without a recording running
    CHECK_FALSE(fileText.contains("samplerTap"));
    CHECK_FALSE(fileText.contains("sampler_tap"));

    master->detach();
    track->detach();
    master->finish();
    track->finish();
    master.reset();
    track.reset();
    ArrangeFixture::pump();
    CHECK(f.commands->historyToken() == token);
    CHECK(f.commands->undoName() == undoName);
    CHECK(f.project->undoManager().getUndoDescriptions().size() == stepsBefore);
    CHECK(f.arrangement().serialise() == savedBefore); // nothing of the tap is left in the edit
    CHECK(f.violations.isEmpty());
}

TEST_CASE("our callback allocates nothing during a long render with taps on", "[arrange][record][take]")
{
    ArrangeFixture f("tap-allocations");
    const auto sound = f.addSound("long", 30.0);
    f.place(sound, 0, 0.0);
    juce::String error;
    auto master = f.project->takeRecorder().tapMaster(optionsFor(recordingFile(f, "m.wav")), error);
    auto track = f.project->takeRecorder().tapTrack(f.trackId(0), optionsFor(recordingFile(f, "t.wav")), error);
    REQUIRE(master != nullptr);
    REQUIRE(track != nullptr);

    gRealtimeAllocations = 0;
    REQUIRE(render(f, f.root.getChildFile("render.wav")));
    const auto a = master->finish();
    const auto b = track->finish();
    CHECK(a.frames >= 29 * 48000);
    CHECK(b.frames >= 29 * 48000);
#if JUCE_DEBUG
    CHECK(gRealtimeAllocations.load() == 0);
#endif
}

TEST_CASE("a Master tap records the mix after the master fader", "[arrange][record][take]")
{
    TwoTracks t("tap-master-volume");
    auto& f = t.f;
    f.commands->apply(std::make_unique<cmd::SetTrackParam>("", cmd::SetTrackParam::Control::volume, -12.0));
    ArrangeFixture::pump();
    juce::String error;
    auto tap = f.project->takeRecorder().tapMaster(optionsFor(recordingFile(f, "master-12.wav")), error);
    REQUIRE(tap != nullptr);

    const auto rendered = f.root.getChildFile("render.wav"); // the offline render includes the master volume
    REQUIRE(render(f, rendered));
    const auto result = tap->finish();
    REQUIRE(result.frames > 0);

    const auto take = sampler::test::readWav(result.file);
    const auto reference = sampler::test::readWav(rendered);
    const auto frames = std::min(take.getNumSamples(), reference.getNumSamples());
    REQUIRE(frames >= 16 * 24000 - 2400);
    CHECK(maxDifference(take, reference, frames) < 1.0e-5f);
    CHECK(sampler::test::peakOf(take, 0, 0, frames) < 0.5f); // about -12 dB of the unity mix
}

TEST_CASE("moving the master fader during a Master take changes the take's level", "[arrange][record][take]")
{
    TwoTracks t("tap-master-fader");
    auto& f = t.f;
    juce::String error;
    auto tap = f.project->takeRecorder().tapMaster(optionsFor(recordingFile(f, "master-fader.wav")), error);
    REQUIRE(tap != nullptr);

    REQUIRE(render(f, f.root.getChildFile("one.wav"))); // master at 0 dB
    f.commands->apply(std::make_unique<cmd::SetTrackParam>("", cmd::SetTrackParam::Control::volume, -12.0));
    ArrangeFixture::pump();
    REQUIRE(render(f, f.root.getChildFile("two.wav"))); // same tap, the fader is down now
    const auto result = tap->finish();

    const auto one = sampler::test::readWav(f.root.getChildFile("one.wav"));
    const auto take = sampler::test::readWav(result.file);
    const auto firstLength = one.getNumSamples();
    REQUIRE(take.getNumSamples() > firstLength + 100000);
    // Skip the 50 ms ramp after the change when measuring the second part.
    const auto firstPeak = sampler::test::peakOf(take, 0, 0, firstLength);
    const auto secondPeak = sampler::test::peakOf(take, 0, firstLength + 4800, take.getNumSamples() - firstLength - 4800);
    CHECK(firstPeak > 0.1f);
    CHECK(secondPeak / firstPeak == Approx(std::pow(10.0f, -12.0f / 20.0f)).margin(0.03));
}
