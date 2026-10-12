// WavTail and TakeWriter: the take pipeline without any audio device.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "platform/TakeWriter.h"
#include "platform/WavTail.h"

#include <cmath>
#include <limits>

namespace
{
using namespace sampler::platform;

juce::File testDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests").getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

/** Writes `frames` frames where sample = (frame + 1) / 1000 on every channel (never 0). */
void writeRamp(const juce::File& file, int channels, int frames)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(file.createOutputStream().release(), 48000.0,
                                                                   static_cast<unsigned>(channels), 32, {}, 0));
    REQUIRE(w != nullptr);
    juce::AudioBuffer<float> b(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            b.setSample(c, i, static_cast<float>(i + 1) / 1000.0f);
    REQUIRE(w->writeFromAudioSampleBuffer(b, 0, frames));
}

juce::int64 frameCount(const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(file.createInputStream().release(), true));
    return r != nullptr ? r->lengthInSamples : -1;
}

constexpr int kRate = 48000;

/** Pushes `seconds` of tone (amplitude 0.5) or silence in 480-frame blocks. */
void push(TakeWriter& w, double seconds, bool tone, int channels = 2)
{
    std::vector<float> block(480 * static_cast<size_t>(channels));
    const int blocks = static_cast<int>(seconds * 100.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (size_t i = 0; i < block.size(); ++i)
            block[i] = tone ? 0.5f * ((i / static_cast<size_t>(channels)) % 2 == 0 ? 1.0f : -1.0f) : 0.0f;
        w.pushInterleaved(block.data(), channels, 480);
    }
}

TakeOptions options(const juce::File& file, bool trim)
{
    TakeOptions o;
    o.file = file;
    o.sampleRate = kRate;
    o.channels = 2;
    o.trimSilence = trim;
    o.ringSeconds = 40;
    return o;
}
} // namespace

TEST_CASE("trimWavTail cuts a file to N frames", "[platform][take]")
{
    const auto dir = testDir("wavtail");
    for (int channels : {1, 2})
    {
        const auto f = dir.getChildFile("a" + juce::String(channels) + ".wav");
        writeRamp(f, channels, 1000);
        REQUIRE(trimWavTail(f, 300).wasOk());
        REQUIRE(frameCount(f) == 300);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
        juce::AudioBuffer<float> b(channels, 300);
        r->read(&b, 0, 300, 0, true, true);
        for (int i = 0; i < 300; ++i)
            REQUIRE(b.getSample(channels - 1, i) == static_cast<float>(i + 1) / 1000.0f);
    }
}

TEST_CASE("trimWavTail: no-op, zero frames, foreign files", "[platform][take]")
{
    const auto dir = testDir("wavtail2");
    const auto f = dir.getChildFile("a.wav");
    writeRamp(f, 2, 500);
    const auto size = f.getSize();

    CHECK(trimWavTail(f, 500).wasOk());
    CHECK(trimWavTail(f, 9999).wasOk());
    CHECK(f.getSize() == size);

    CHECK(trimWavTail(f, 0).wasOk());
    CHECK(frameCount(f) == 0);

    const auto foreign = dir.getChildFile("not.wav");
    REQUIRE(foreign.replaceWithText("this is not a wav file, just text padded out to be longer than a header would be"));
    const auto before = foreign.getSize();
    CHECK(trimWavTail(foreign, 1).failed());
    CHECK(foreign.getSize() == before);
    CHECK(trimWavTail(dir.getChildFile("missing.wav"), 1).failed());
}

TEST_CASE("TakeWriter untrimmed keeps every frame", "[platform][take]")
{
    const auto dir = testDir("take-untrimmed");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("t.wav"), false)).wasOk());
    push(w, 1.0, false);
    push(w, 1.0, true);
    const auto r = w.finish();
    CHECK(r.frames == 2 * kRate);
    CHECK(r.leadingTrimFrames == 0);
    CHECK(r.everLoud);
    CHECK(frameCount(r.file) == 2 * kRate);
    CHECK(r.overruns == 0);
}

TEST_CASE("TakeWriter trims silence at both ends and keeps a pause inside", "[platform][take]")
{
    const auto dir = testDir("take-trim");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("t.wav"), true)).wasOk());
    push(w, 4.0, false);
    push(w, 3.0, true);
    push(w, 5.0, false); // a pause inside
    push(w, 7.0, true);
    push(w, 3.0, false);
    const auto r = w.finish();
    const int margin = static_cast<int>(kRate * 0.005);
    REQUIRE(r.everLoud);
    CHECK(r.leadingTrimFrames == 4 * kRate - margin);
    CHECK(r.frames == (3 + 5 + 7) * kRate + 2 * margin);
    CHECK(frameCount(r.file) == r.frames);
    CHECK(r.framesReceived == 22 * kRate);
    CHECK(r.trimmedTail);
}

TEST_CASE("TakeWriter reports a take that was never loud", "[platform][take]")
{
    const auto dir = testDir("take-silent");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("t.wav"), true)).wasOk());
    push(w, 2.0, false);
    CHECK_FALSE(w.everLoud());
    CHECK(w.framesSinceLoud() == 2 * kRate);
    const auto r = w.finish();
    CHECK_FALSE(r.everLoud);
    CHECK(r.frames == 0);
}

TEST_CASE("TakeWriter stops at the frame limit", "[platform][take]")
{
    const auto dir = testDir("take-limit");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.maxFrames = 2 * kRate;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    push(w, 3.0, true);
    CHECK(w.limitReached());
    const auto r = w.finish();
    CHECK(r.frames == 2 * kRate);
    CHECK(r.limitReached);
}

TEST_CASE("TakeWriter counts an overrun when the writer cannot keep up", "[platform][take]")
{
    const auto dir = testDir("take-overrun");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.ringSeconds = 1;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    w.holdWriter(true);
    push(w, 2.0, true);
    CHECK(w.overruns() > 0);
    const auto r = w.finish();
    CHECK(r.overruns > 0);
    CHECK(r.frames <= kRate);
}

TEST_CASE("TakeWriter live taps: peak, buckets, mono source", "[platform][take]")
{
    const auto dir = testDir("take-taps");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("t.wav"), false)).wasOk());
    push(w, 0.5, true, 1); // a mono source into a stereo take
    CHECK(w.takePeak() == 0.5f);
    CHECK(w.takePeak() == 0.0f);
    CHECK(w.numBuckets() == (kRate / 2) / w.bucketFramesPerBucket());
    std::vector<MinMax> b;
    w.copyBuckets(0, b);
    REQUIRE_FALSE(b.empty());
    CHECK(b[0].max == 0.5f);
    CHECK(b[0].min == -0.5f);
    const auto r = w.finish();
    CHECK(frameCount(r.file) == kRate / 2);
}

namespace
{
/** Sample (channel 0) at `frame` of a finished take. */
float sampleAt(const juce::File& file, juce::int64 frame)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(file.createInputStream().release(), true));
    juce::AudioBuffer<float> b(2, 1);
    r->read(&b, 0, 1, frame, true, true);
    return b.getSample(0, 0);
}

/** One block of constant 0.5 on both channels, stamped with its timeline position. */
void pushAt(TakeWriter& w, juce::int64 timelineFrame, int frames = 480)
{
    std::vector<float> data(static_cast<size_t>(frames), 0.5f);
    const float* channels[2] = {data.data(), data.data()};
    w.pushPlanarAt(channels, 2, frames, 0, timelineFrame);
}
} // namespace

TEST_CASE("a tap that misses blocks (muted track) keeps the later audio on the timeline", "[platform][take]")
{
    const auto dir = testDir("take-gap");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.fillDroppedWithSilence = true;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    for (int b = 0; b < 10; ++b)
        pushAt(w, b * 480); // 0 .. 4800
    for (int b = 20; b < 30; ++b)
        pushAt(w, b * 480); // the blocks 10 to 19 never came
    const auto r = w.finish();
    CHECK(r.frames == 30 * 480);
    CHECK(r.framesReceived == 30 * 480);
    CHECK(sampleAt(r.file, 4799) == 0.5f);
    CHECK(sampleAt(r.file, 4800) == 0.0f);
    CHECK(sampleAt(r.file, 9599) == 0.0f);
    CHECK(sampleAt(r.file, 9600) == 0.5f);
}

TEST_CASE("a jump back on the timeline (loop) just goes on appending", "[platform][take]")
{
    const auto dir = testDir("take-loop-wrap");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.fillDroppedWithSilence = true;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    for (int b = 0; b < 5; ++b)
        pushAt(w, b * 480);
    for (int b = 0; b < 5; ++b)
        pushAt(w, b * 480); // wrapped to the start
    const auto r = w.finish();
    CHECK(r.frames == 10 * 480);
}

TEST_CASE("TakeWriter: frames dropped for lack of room are replaced by silence when asked", "[platform][take]")
{
    const auto dir = testDir("take-drop-fill");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.ringSeconds = 1;
    o.fillDroppedWithSilence = true;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    w.holdWriter(true);
    push(w, 2.0, true); // the second second does not fit
    CHECK(w.overruns() > 0);
    w.holdWriter(false);
    for (int block = 0; block < 50; ++block) // one block at a time, each once the writer has caught up
    {
        for (int i = 0; i < 400 && w.freeFrames() < kRate - 1; ++i)
            juce::Thread::sleep(5);
        push(w, 0.01, true);
    }
    const auto r = w.finish();
    CHECK(r.frames == r.framesReceived);
    CHECK(r.frames == 2 * kRate + kRate / 2);
    CHECK(sampleAt(r.file, kRate + 100) == 0.0f);                // the dropped second is silent
    CHECK(sampleAt(r.file, 2 * kRate + 20000) != 0.0f);        // the later audio is back after the silence
}

TEST_CASE("TakeWriter: a failed write does not claim frames that are not in the file", "[platform][take]")
{
    const auto dir = testDir("take-write-fail");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("t.wav"), false)).wasOk());
    w.failWritesAfterFrames(kRate / 2);
    push(w, 1.0, true);
    const auto r = w.finish();
    CHECK(r.writeFailed);
    CHECK(r.frames <= kRate / 2);
    CHECK(r.frames == frameCount(r.file));
}

TEST_CASE("TakeWriter: a finished take is never opened again", "[platform][take]")
{
    const auto dir = testDir("take-reopen");
    const auto file = dir.getChildFile("t.wav");
    TakeWriter w;
    REQUIRE(w.open(options(file, false)).wasOk());
    push(w, 0.5, true);
    const auto r = w.finish();
    REQUIRE(r.frames == kRate / 2);
    CHECK(w.open(options(file, false)).failed());
    CHECK_FALSE(w.isOpen());
    CHECK(frameCount(file) == kRate / 2); // the finished file is still there
}

TEST_CASE("TakeWriter: loud frames inside a block cut by the limit count as heard", "[platform][take]")
{
    const auto dir = testDir("take-limit-loud");
    auto o = options(dir.getChildFile("t.wav"), true);
    o.maxFrames = 100;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    std::vector<float> block(480 * 2, 0.5f); // the whole block is loud, the limit cuts it after 100 frames
    w.pushInterleaved(block.data(), 2, 480);
    CHECK(w.limitReached());
    const auto r = w.finish();
    CHECK(r.everLoud);
    CHECK(r.frames == 100);
    CHECK(frameCount(r.file) == 100);
}

TEST_CASE("TakeWriter: the tail trim keeps loud audio in a block cut by the limit", "[platform][take]")
{
    const auto dir = testDir("take-limit-tail");
    auto o = options(dir.getChildFile("t.wav"), true);
    o.maxFrames = 4800 + 240; // 240 frames of the last block fit
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    push(w, 0.1, true); // 4800 frames of tone
    std::vector<float> block(480 * 2, 0.5f);
    w.pushInterleaved(block.data(), 2, 480);
    const auto r = w.finish();
    CHECK(r.frames == 4800 + 240);
    CHECK(sampleAt(r.file, r.frames - 1) == 0.5f); // not cut shorter than the loud audio
}

TEST_CASE("frames before the keep-from mark are counted but never kept", "[platform][take]")
{
    const auto dir = testDir("take-keep-from");
    TakeWriter w;
    REQUIRE(w.open(options(dir.getChildFile("take.wav"), false)).wasOk());
    w.keepFrom(std::numeric_limits<juce::int64>::max()); // waiting for the song: nothing is kept
    push(w, 1.0, true);
    CHECK(w.framesReceived() == kRate);
    CHECK(w.framesKept() == 0);
    CHECK_FALSE(w.everLoud()); // dropped frames are not looked at
    w.keepFrom(w.framesReceived() + 2400); // the song starts 50 ms after the last frame
    push(w, 0.5, true);
    const auto result = w.finish();
    CHECK(result.framesReceived == kRate + kRate / 2);
    CHECK(result.leadingTrimFrames == kRate + 2400); // counting the dropped ones: where the first kept frame lies
    CHECK(result.frames == kRate / 2 - 2400);
    CHECK(result.everLoud);
    CHECK(frameCount(result.file) == kRate / 2 - 2400);
}

namespace
{
/** Every channel-0 sample of a finished take. */
std::vector<float> readAll(const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(file.createInputStream().release(), true));
    std::vector<float> out;
    if (r == nullptr)
        return out;
    juce::AudioBuffer<float> b(2, static_cast<int>(r->lengthInSamples));
    r->read(&b, 0, b.getNumSamples(), 0, true, true);
    out.assign(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
    return out;
}
} // namespace

TEST_CASE("TakeWriter: a long skipped span is silence, then the audio, in order and on the timeline", "[platform][take]")
{
    const auto dir = testDir("take-long-skip");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.ringSeconds = 10;
    o.fillDroppedWithSilence = true;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    for (int b = 0; b < 10; ++b)
        pushAt(w, b * 480); // 0 .. 4800
    const juce::int64 jump = 4800 + 15 * kRate; // 15 s the engine never delivered (longer than the 10 s ring)
    for (int b = 0; b < 120; ++b)
        pushAt(w, jump + b * 480);
    const auto r = w.finish();
    CHECK(r.framesReceived == jump + 120 * 480);
    CHECK(r.frames == r.framesReceived);

    const auto all = readAll(r.file);
    REQUIRE(static_cast<juce::int64>(all.size()) == r.frames);
    for (int i = 0; i < 4800; ++i)
        REQUIRE(all[static_cast<size_t>(i)] == 0.5f);
    size_t firstAudio = 4800;
    while (firstAudio < all.size() && all[firstAudio] == 0.0f)
        ++firstAudio;
    CHECK(static_cast<juce::int64>(firstAudio) >= jump); // no audio inside the silent span
    REQUIRE(firstAudio < all.size());                    // and the audio came back
    for (size_t i = firstAudio; i < all.size(); ++i)     // one run of audio up to the end of the timeline
        REQUIRE(all[i] == 0.5f);
}

TEST_CASE("TakeWriter: silence work per producer call is bounded", "[platform][take]")
{
    const auto dir = testDir("take-silence-bound");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.fillDroppedWithSilence = true;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    pushAt(w, 0);
    const auto before = w.framesKept();
    pushAt(w, 600 * kRate); // ten minutes missing
    CHECK(w.framesKept() - before <= 16384);
    CHECK(w.framesReceived() > 600 * kRate);
    const auto mid = w.framesKept();
    w.addPendingSilence(300 * kRate);
    CHECK(w.framesKept() - mid <= 16384);
    w.finish();
}

TEST_CASE("TakeWriter: pending silence is replaced in order and a System take ignores it before the first sound", "[platform][take]")
{
    const auto dir = testDir("take-pending-silence");
    auto o = options(dir.getChildFile("t.wav"), true);
    o.silenceFramesPerCall = 1 << 20;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    w.addPendingSilence(5 * kRate); // before the first sound: only counted
    CHECK(w.framesKept() == 0);
    push(w, 0.5, true);
    w.addPendingSilence(kRate);
    push(w, 0.5, true);
    const auto r = w.finish();
    CHECK(r.leadingTrimFrames == 5 * kRate);   // nothing kept before the sound
    CHECK(r.framesReceived == 7 * kRate);
    CHECK(r.frames == kRate / 2 * 2 + kRate);
    const auto all = readAll(r.file);
    CHECK(all[0] == 0.5f);
    CHECK(all[static_cast<size_t>(kRate / 2)] == 0.0f);
    CHECK(all[static_cast<size_t>(kRate / 2 + kRate)] == 0.5f);
}

TEST_CASE("TakeWriter: pending silence that does not fit the limit ends the take", "[platform][take]")
{
    const auto dir = testDir("take-silence-limit");
    auto o = options(dir.getChildFile("t.wav"), false);
    o.maxFrames = 2 * kRate;
    TakeWriter w;
    REQUIRE(w.open(o).wasOk());
    push(w, 1.0, true);
    w.addPendingSilence(5 * kRate);
    push(w, 1.0, true); // behind the silence, beyond the limit
    const auto r = w.finish();
    CHECK(r.limitReached);
    CHECK(r.frames == 2 * kRate);
    const auto all = readAll(r.file);
    CHECK(all[static_cast<size_t>(kRate - 1)] != 0.0f);
    CHECK(all[static_cast<size_t>(kRate)] == 0.0f);
    CHECK(all.back() == 0.0f);
}
