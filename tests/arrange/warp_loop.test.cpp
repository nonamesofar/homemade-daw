// M4a task 3.1: clip loop, clip length and fades, as commands on a real headless engine (offline renders where sound matters).
#include "WarpTestSupport.h"

#include <catch2/catch_approx.hpp>

#include <map>

using Catch::Approx;
using sampler::engine::WarpMode;
using sampler::engine::WarpSettings;
using namespace sampler::test;
namespace cmd = sampler::commands;

namespace
{
/** Applies a command that is made on the spot. */
template <typename Command>
void run(ArrangeFixture& f, Command command)
{
    f.commands->apply(command);
}

WarpSettings warpOn(WarpMode mode, double bpm, double transpose = 0.0)
{
    WarpSettings w;
    w.on = true;
    w.mode = mode;
    w.sourceBpm = bpm;
    w.transpose = transpose;
    return w;
}

juce::String placeWarped(ArrangeFixture& f, const juce::String& sound, double beat, const WarpSettings& w)
{
    cmd::CreateClipsFromSources command({sound}, {0, false, beat}, {w});
    f.commands->apply(command);
    REQUIRE(command.resultIds().size() == 1);
    return command.resultIds().front();
}

/** The left channel at a time (seconds); 0 outside the buffer. */
float at(const juce::AudioBuffer<float>& b, double rate, double seconds)
{
    const auto i = static_cast<int>(std::lround(seconds * rate));
    return i >= 0 && i < b.getNumSamples() ? b.getSample(0, i) : 0.0f;
}

/** Root mean square of the left channel between two times. */
double rms(const juce::AudioBuffer<float>& b, double rate, double from, double to)
{
    const auto a = std::max(0, static_cast<int>(from * rate));
    const auto z = std::min(b.getNumSamples(), static_cast<int>(to * rate));
    double sum = 0.0;
    for (int i = a; i < z; ++i)
        sum += static_cast<double>(b.getSample(0, i)) * b.getSample(0, i);
    return z > a ? std::sqrt(sum / (z - a)) : 0.0;
}

/** A 4 s ramp (every sample says where in the file it is) trimmed to its second second [1 s, 2 s) at beats 2 to 4. */
struct RampSecond
{
    explicit RampSecond(const juce::String& name) : f(name)
    {
        sound = f.addSound("ramp", 4.0, "ramp");
        clip = f.place(sound, 0, 0.0);
        run(f, cmd::TrimClip(clip, 2.0, 4.0, 0.01));
        REQUIRE(f.violations.isEmpty());
    }
    ArrangeFixture f;
    juce::String sound, clip;
};

/** Expected ramp value of a pass of [1 s, 2 s) at `x` seconds into the pass. */
double rampAt(double x) { return 0.25 + 0.25 * std::fmod(x, 1.0); }
} // namespace

TEST_CASE("a one-bar clip with LOOP on and length 8 bars repeats its bar eight times", "[arrange][warp][loop]")
{
    ArrangeFixture f("wl-eight");
    const auto sound = f.addSound("ramp", 8.0, "ramp"); // 4 bars at 120 BPM
    const auto clip = f.place(sound, 0, 0.0);
    run(f, cmd::TrimClip(clip, 4.0, 8.0, 0.01)); // bar 2 only: [2 s, 4 s) of the ramp
    REQUIRE(f.arrangement().clip(clip)->length == Approx(4.0));

    run(f, cmd::SetClipLoop(clip, true));
    CHECK(f.commands->undoName() == "Set clip loop");
    auto info = *f.arrangement().clip(clip);
    CHECK(info.loop);
    CHECK(info.loopStart == Approx(2.0));
    CHECK(info.loopLength == Approx(2.0));
    CHECK(info.length == Approx(4.0)); // still one pass

    run(f, cmd::SetClipLength(clip, 32.0)); // 8 bars
    CHECK(f.commands->undoName() == "Set clip length");
    info = *f.arrangement().clip(clip);
    CHECK(info.length == Approx(32.0));
    CHECK(info.start == Approx(4.0));
    CHECK(info.offsetSeconds == Approx(2.0));
    CHECK(f.violations.isEmpty());

    double rate = 0.0;
    const auto out = renderProject(f, "wl-eight", &rate);
    REQUIRE(out.getNumSamples() > 0);
    const double clipStart = 2.0; // beat 4 at 120 BPM
    for (int pass = 0; pass < 8; ++pass)
        for (const double x : {0.1, 0.5, 1.0, 1.5, 1.9})
        {
            INFO("pass " << pass << " at " << x);
            // The ramp of [2 s, 4 s) climbs from 0.25 to 0.5 across a pass.
            CHECK(at(out, rate, clipStart + 2.0 * pass + x) == Approx(0.25 + 0.25 * (x / 2.0)).margin(0.003));
        }
    CHECK(at(out, rate, clipStart + 16.5) == Approx(0.0).margin(1.0e-4)); // nothing after the eighth pass
}

TEST_CASE("LOOP off shortens a looped clip back to one pass, and Undo restores the loop", "[arrange][warp][loop]")
{
    RampSecond r("wl-off");
    auto& f = r.f;
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::SetClipLength(r.clip, 8.0)); // four passes of one second
    REQUIRE(f.arrangement().clip(r.clip)->length == Approx(8.0));
    const auto looped = f.arrangement().serialise();

    run(f, cmd::SetClipLoop(r.clip, false));
    auto info = *f.arrangement().clip(r.clip);
    CHECK_FALSE(info.loop);
    CHECK(info.length == Approx(2.0));
    CHECK(info.start == Approx(2.0));
    CHECK(info.offsetSeconds == Approx(1.0));
    CHECK(f.violations.isEmpty());

    f.commands->undo();
    INFO(firstDifference(looped, f.arrangement().serialise()));
    CHECK(firstDifference(looped, f.arrangement().serialise()).isEmpty());
    CHECK(f.arrangement().clip(r.clip)->loop);
    CHECK(f.arrangement().clip(r.clip)->length == Approx(8.0));
}

TEST_CASE("a looped clip repeats the ramp second without a gap, in a render", "[arrange][warp][loop]")
{
    RampSecond r("wl-ramp");
    auto& f = r.f;
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::SetClipLength(r.clip, 8.0)); // beats 2 to 10 = seconds 1 to 5
    double rate = 0.0;
    const auto out = renderProject(f, "wl-ramp", &rate);
    for (int pass = 0; pass < 4; ++pass)
        for (const double x : {0.05, 0.25, 0.5, 0.75, 0.95})
        {
            INFO("pass " << pass << " at " << x);
            CHECK(at(out, rate, 1.0 + pass + x) == Approx(rampAt(x)).margin(0.003));
        }
    // Both sides of a join are the end and the start of the loop: nothing in between.
    for (int pass = 1; pass < 4; ++pass)
    {
        const auto join = static_cast<int>(std::lround((1.0 + pass) * rate));
        CHECK(out.getSample(0, join - 2) == Approx(0.5).margin(0.003));
        CHECK(out.getSample(0, join + 2) == Approx(0.25).margin(0.003));
    }
    CHECK(f.violations.isEmpty());
}

TEST_CASE("LENGTH shortens a clip, and without LOOP it cannot pass the end of its sound", "[arrange][warp][loop]")
{
    ArrangeFixture f("wl-length");
    const auto sound = f.addSound("ramp", 8.0, "ramp"); // 16 beats = 4 bars
    const auto clip = f.place(sound, 0, 0.0);
    REQUIRE(f.arrangement().clip(clip)->length == Approx(16.0));

    run(f, cmd::SetClipLength(clip, 8.0)); // "2" bars
    auto info = *f.arrangement().clip(clip);
    CHECK(info.length == Approx(8.0));
    CHECK(info.offsetSeconds == Approx(0.0)); // the first half of the sound

    run(f, cmd::SetClipLength(clip, 400.0));
    info = *f.arrangement().clip(clip);
    CHECK(info.length == Approx(16.0)); // as long as the sound
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a longer looped clip shortens the next clip on its track", "[arrange][warp][loop]")
{
    RampSecond r("wl-overlap");
    auto& f = r.f;
    const auto next = f.place(r.sound, 0, 8.0);
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::SetClipLength(r.clip, 8.0)); // beats 2 to 10 runs into the clip at beat 8
    CHECK(f.arrangement().clip(r.clip)->length == Approx(8.0));
    CHECK(f.arrangement().clip(next)->start == Approx(10.0));
    CHECK(f.violations.isEmpty());
    f.commands->undo();
    CHECK(f.arrangement().clip(next)->start == Approx(8.0));
}

TEST_CASE("fades: stored in seconds, cut to the clip, in first; Undo removes them", "[arrange][warp][fades]")
{
    ArrangeFixture f("wf-clamp");
    const auto sound = f.addSound("tone", 1.0, "tone"); // a 1 s clip
    const auto clip = f.place(sound, 0, 0.0);
    REQUIRE(f.arrangement().clip(clip)->lengthSeconds == Approx(1.0));
    const auto plain = f.arrangement().serialise();

    run(f, cmd::SetClipFades(clip, 0.5, 0.2));
    CHECK(f.commands->undoName() == "Set clip fades");
    auto info = *f.arrangement().clip(clip);
    CHECK(info.fadeIn == Approx(0.5));
    CHECK(info.fadeOut == Approx(0.2));

    // "800 ms in 1000 ms with a fade out of 600": the panel passes 400 and 600; asked for 800 and 600 the command keeps the in.
    run(f, cmd::SetClipFades(clip, 0.4, 0.6));
    info = *f.arrangement().clip(clip);
    CHECK(info.fadeIn == Approx(0.4));
    CHECK(info.fadeOut == Approx(0.6));
    run(f, cmd::SetClipFades(clip, 0.8, 0.6));
    info = *f.arrangement().clip(clip);
    CHECK(info.fadeIn == Approx(0.8));
    CHECK(info.fadeOut == Approx(0.2));
    CHECK(info.fadeIn + info.fadeOut <= info.lengthSeconds + 1.0e-9);
    CHECK(f.violations.isEmpty());

    run(f, cmd::SetClipFades(clip, 0.0, 0.0));
    CHECK(f.arrangement().clip(clip)->fadeIn == 0.0);
    f.commands->undo();
    f.commands->undo();
    f.commands->undo();
    f.commands->undo();
    CHECK(firstDifference(plain, f.arrangement().serialise()).isEmpty());
}

TEST_CASE("a fade in is heard in a render, and without fades only the short declick is there", "[arrange][warp][fades]")
{
    ArrangeFixture f("wf-render");
    const auto sound = f.addSound("dc", 4.0, "dc"); // a constant 0.5
    const auto clip = f.place(sound, 0, 0.0);
    run(f, cmd::SetClipFades(clip, 1.0, 0.5));
    double rate = 0.0;
    const auto out = renderProject(f, "wf-render", &rate);
    CHECK(at(out, rate, 0.0) == Approx(0.0).margin(0.01));
    CHECK(at(out, rate, 0.5) == Approx(0.25).margin(0.02)); // half way up
    CHECK(at(out, rate, 1.0) == Approx(0.5).margin(0.02));
    CHECK(at(out, rate, 2.0) == Approx(0.5).margin(0.005));
    CHECK(at(out, rate, 3.75) == Approx(0.25).margin(0.02)); // half way down
    CHECK(at(out, rate, 3.99) < 0.05f);

    run(f, cmd::SetClipFades(clip, 0.0, 0.0));
    const auto plain = renderProject(f, "wf-render-plain", &rate);
    CHECK(at(plain, rate, 0.5) == Approx(0.5).margin(0.005));
    CHECK(at(plain, rate, 0.0005) < 0.45f); // the 3 ms declick is still there
    CHECK(at(plain, rate, 0.01) == Approx(0.5).margin(0.005));
}

TEST_CASE("a split keeps the fade in on the first part and the fade out on the second", "[arrange][warp][fades]")
{
    ArrangeFixture f("wf-split");
    const auto sound = f.addSound("tone", 4.0, "tone"); // 8 beats
    const auto clip = f.place(sound, 0, 0.0);
    run(f, cmd::SetClipFades(clip, 0.5, 0.75));
    cmd::SplitClips split({clip}, 4.0);
    f.commands->apply(split);
    const auto clips = f.clipsOn(0);
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].fadeIn == Approx(0.5));
    CHECK(clips[0].fadeOut == 0.0);
    CHECK(clips[1].fadeIn == 0.0);
    CHECK(clips[1].fadeOut == Approx(0.75));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a split looped clip sounds the same, also when the cut falls inside a pass", "[arrange][warp][loop]")
{
    for (const double splitBeat : {6.0, 5.5}) // on a join of the loop, and a quarter of a pass after one
    {
        INFO("split at beat " << splitBeat);
        RampSecond r("wl-split-" + juce::String(static_cast<int>(splitBeat * 10)));
        auto& f = r.f;
        run(f, cmd::SetClipLoop(r.clip, true));
        run(f, cmd::SetClipLength(r.clip, 6.0)); // beats 2 to 8 = seconds 1 to 4: three passes
        double rate = 0.0;
        const auto before = renderProject(f, "wl-split-before-" + juce::String(static_cast<int>(splitBeat * 10)), &rate);

        run(f, cmd::SplitClips({r.clip}, splitBeat));
        const auto clips = f.clipsOn(0);
        REQUIRE(clips.size() == 2);
        CHECK(clips[0].loop);
        CHECK(clips[1].loop);
        CHECK(clips[1].loopStart == Approx(1.0));
        CHECK(clips[1].loopLength == Approx(1.0));
        CHECK(f.violations.isEmpty());
        const auto after = renderProject(f, "wl-split-after-" + juce::String(static_cast<int>(splitBeat * 10)), &rate);

        const auto cut = splitBeat / 2.0;
        int differing = 0;
        for (double t = 1.0; t < 4.0; t += 0.0137)
            if (std::abs(t - cut) > 0.02 && std::abs(at(before, rate, t) - at(after, rate, t)) > 0.003)
                ++differing;
        CHECK(differing == 0);
    }
}

TEST_CASE("a Tones clip loops at its pitch without a gap", "[arrange][warp][loop]")
{
    ArrangeFixture f("wl-tones");
    const auto sound = addFixtureSound(f, "tone-440-100.wav"); // 9.6 s, 4 bars at 100 BPM
    const auto clip = placeWarped(f, sound, 0.0, warpOn(WarpMode::tones, 100.0));
    REQUIRE(f.arrangement().clip(clip)->length == Approx(16.0));
    run(f, cmd::TrimClip(clip, 0.0, 4.0, 0.01)); // the first bar
    run(f, cmd::SetClipLoop(clip, true));
    run(f, cmd::SetClipLength(clip, 16.0));
    auto info = *f.arrangement().clip(clip);
    CHECK(info.length == Approx(16.0));
    CHECK(info.loop);
    CHECK(f.violations.isEmpty());

    REQUIRE(waitForBeatsRenders(f));
    double rate = 0.0;
    const auto out = renderProject(f, "wl-tones", &rate);
    CHECK(frequencyBetween(out, rate, 0.5, 7.5) == Approx(440.0).margin(1.5));
    for (double t = 0.2; t < 7.6; t += 0.1)
        CHECK(rms(out, rate, t, t + 0.02) > 0.02); // no gap anywhere in the eight seconds
    CHECK(lastAbove(out, 0.02f) / rate == Approx(8.0).margin(0.1));

    // The tempo changes: the clip keeps 4 bars.
    run(f, cmd::SetTempo(100.0));
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    CHECK(f.violations.isEmpty());
    CHECK(waitForBeatsRenders(f));
}

TEST_CASE("a Beats clip loops its bar: the passes are the same sound", "[arrange][warp][loop]")
{
    ArrangeFixture f("wl-beats");
    const auto sound = addFixtureSound(f, "break-90.wav"); // 4 bars at 90 BPM
    const auto clip = placeWarped(f, sound, 0.0, warpOn(WarpMode::beats, 90.0));
    REQUIRE(waitForBeatsRenders(f));
    run(f, cmd::TrimClip(clip, 0.0, 4.0, 0.01)); // the first bar
    run(f, cmd::SetClipLoop(clip, true));
    run(f, cmd::SetClipLength(clip, 16.0));
    REQUIRE(waitForBeatsRenders(f));
    CHECK(f.arrangement().clip(clip)->length == Approx(16.0));
    CHECK(f.violations.isEmpty());

    double rate = 0.0;
    const auto out = renderProject(f, "wl-beats", &rate);
    // A bar is 2 s at 120 BPM: bars 2 to 4 are copies of bar 1 (the kick on beat 1 of each).
    const auto first = rms(out, rate, 0.0, 1.9);
    REQUIRE(first > 0.005);
    // The passes line up to the sample (the hits are where they were). Every second pass starts half way through an audio
    // block, so the resampler places the sharp attacks slightly differently: close, not bit for bit.
    for (int bar = 1; bar < 4; ++bar)
    {
        std::map<int, double> diffAtLag;
        for (int lag = -2; lag <= 2; ++lag)
        {
            double d = 0.0;
            for (int i = static_cast<int>(0.05 * rate); i < static_cast<int>(1.9 * rate); ++i)
                d += std::abs(out.getSample(0, i + static_cast<int>(2.0 * rate * bar) + lag) - out.getSample(0, i));
            diffAtLag[lag] = d / (1.85 * rate);
        }
        INFO("bar " << bar);
        for (int lag = -2; lag <= 2; ++lag)
            if (lag != 0)
                CHECK(diffAtLag[0] < diffAtLag[lag]);
        CHECK(diffAtLag[0] < 0.06 * first);
    }
}

TEST_CASE("loop settings are kept by Copy and Paste, and by Duplicate", "[arrange][warp][loop]")
{
    RampSecond r("wl-paste");
    auto& f = r.f;
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::SetClipLength(r.clip, 6.0));
    run(f, cmd::SetClipFades(r.clip, 0.25, 0.5));

    cmd::DuplicateClips duplicate({r.clip});
    f.commands->apply(duplicate);
    REQUIRE(duplicate.resultIds().size() == 1);
    const auto copy = *f.arrangement().clip(duplicate.resultIds().front());
    CHECK(copy.loop);
    CHECK(copy.loopStart == Approx(1.0));
    CHECK(copy.length == Approx(6.0));
    CHECK(copy.fadeIn == Approx(0.25));
    CHECK(copy.fadeOut == Approx(0.5));

    const auto original = *f.arrangement().clip(r.clip);
    cmd::ClipRecord record;
    record.sourceId = original.sourceId;
    record.name = original.name;
    record.lengthSeconds = original.lengthSeconds;
    record.offsetSeconds = original.offsetSeconds;
    record.lengthBeats = original.length;
    record.loopStart = original.loopStart;
    record.loopLength = original.loopLength;
    record.fadeIn = original.fadeIn;
    record.fadeOut = original.fadeOut;
    cmd::PasteClips paste({record}, 0, 40.0);
    f.commands->apply(paste);
    REQUIRE(paste.resultIds().size() == 1);
    const auto pasted = *f.arrangement().clip(paste.resultIds().front());
    CHECK(pasted.loop);
    CHECK(pasted.length == Approx(6.0));
    CHECK(pasted.offsetSeconds == Approx(1.0));
    CHECK(pasted.fadeIn == Approx(0.25));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("the right edge of a looped clip can be dragged past its sound; a clip without loop stops at the sound", "[arrange][warp][loop]")
{
    RampSecond r("wl-trim");
    auto& f = r.f;
    run(f, cmd::TrimClip(r.clip, 2.0, 40.0, 0.01)); // not looped: stops at the end of the sound (beat 8)
    CHECK(f.arrangement().clip(r.clip)->end() == Approx(8.0));
    f.commands->undo();
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::TrimClip(r.clip, 2.0, 16.0, 0.01));
    CHECK(f.arrangement().clip(r.clip)->end() == Approx(16.0));
    // The left edge of a looped clip does not go earlier.
    run(f, cmd::TrimClip(r.clip, 0.0, 16.0, 0.01));
    CHECK(f.arrangement().clip(r.clip)->start == Approx(2.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("reversing and warping a looped clip keep the loop valid", "[arrange][warp][loop]")
{
    RampSecond r("wl-reverse");
    auto& f = r.f;
    run(f, cmd::SetClipLoop(r.clip, true));
    run(f, cmd::SetClipLength(r.clip, 6.0));
    run(f, cmd::SetClipReversed(r.clip, true));
    auto info = *f.arrangement().clip(r.clip);
    CHECK(info.loop);
    CHECK(info.loopStart == Approx(4.0 - 1.0 - 1.0)); // the second second of 4, seen from the other end
    CHECK(info.loopLength == Approx(1.0));
    CHECK(f.violations.isEmpty());
    f.commands->undo();
    CHECK(f.arrangement().clip(r.clip)->loopStart == Approx(1.0));
    CHECK(f.violations.isEmpty());
}

TEST_CASE("a pasted plain clip plays its fade in, not only the declick", "[arrange][warp][fades]")
{
    ArrangeFixture f("wf-paste-plain");
    const auto sound = f.addSound("dc", 4.0, "dc"); // a constant 0.5
    cmd::ClipRecord record;
    record.sourceId = sound;
    record.name = "dc";
    record.lengthSeconds = 4.0;
    record.fadeIn = 0.25;
    cmd::PasteClips paste({record}, 0, 0.0);
    f.commands->apply(paste);
    REQUIRE(paste.resultIds().size() == 1);
    CHECK(f.arrangement().clip(paste.resultIds().front())->fadeIn == Approx(0.25));
    double rate = 0.0;
    const auto out = renderProject(f, "wf-paste-plain", &rate);
    CHECK(at(out, rate, 0.125) == Approx(0.25).margin(0.02)); // half way up the 0.25 s fade
    CHECK(at(out, rate, 1.0) == Approx(0.5).margin(0.005));
}
