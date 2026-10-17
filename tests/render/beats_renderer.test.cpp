#include <catch2/catch_test_macros.hpp>

#include "AnalysisTestSupport.h"
#include "render/BeatsRenderer.h"

#include <chrono>
#include <cmath>

using namespace sampler::render;

namespace
{
/** Frame where the hit that starts near `expected` really rises: first sample above 10% of the peak of the next 50 ms. */
juce::int64 attackFrame(const float* x, juce::int64 frames, juce::int64 searchFrom, double rate)
{
    const auto window = static_cast<juce::int64>(0.05 * rate);
    float peak = 0.0f;
    for (juce::int64 i = searchFrom; i < std::min(frames, searchFrom + window); ++i)
        peak = std::max(peak, std::abs(x[i]));
    for (juce::int64 i = searchFrom; i < std::min(frames, searchFrom + window); ++i)
        if (std::abs(x[i]) >= 0.1f * peak)
            return i;
    return -1;
}

BeatsParams params(double source, double project, bool reverse = false, float transpose = 0.0f)
{
    BeatsParams p;
    p.sourceBpm = source;
    p.projectBpm = project;
    p.reverse = reverse;
    p.transposeSemitones = transpose;
    return p;
}

struct Sound
{
    juce::AudioBuffer<float> audio;
    double rate = 44100.0;
};

Sound load(const juce::String& name)
{
    Sound s;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name)));
    REQUIRE(reader != nullptr);
    s.rate = reader->sampleRate;
    s.audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&s.audio, 0, s.audio.getNumSamples(), 0, true, true);
    return s;
}

std::vector<float> mono(const juce::AudioBuffer<float>& audio)
{
    std::vector<float> m(static_cast<size_t>(audio.getNumSamples()), 0.0f);
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            m[static_cast<size_t>(i)] += audio.getSample(c, i) / static_cast<float>(audio.getNumChannels());
    return m;
}
} // namespace

TEST_CASE("break-90 at 120 BPM: every hit lands within 1 ms of its beat and keeps its attack", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    const auto truth = sampler::test::loadAnalysisFixture("break-90");
    const auto rendered = renderBeats(sound.audio, sound.rate, params(90.0, 120.0));

    REQUIRE(rendered.getNumChannels() == 2);
    CHECK(rendered.getNumSamples() == 352800); // 4 bars at 120 BPM
    REQUIRE(truth.onsets.size() == 16);

    const auto ratio = 0.75;
    const auto* in = sound.audio.getReadPointer(0);
    const auto* out = rendered.getReadPointer(0);
    for (size_t i = 0; i < truth.onsets.size(); ++i)
    {
        INFO("hit " << i);
        const auto at = truth.onsets[i];
        const auto sourceAttack = attackFrame(in, sound.audio.getNumSamples(), at - 441, sound.rate);
        const auto expected = static_cast<juce::int64>(std::llround(ratio * static_cast<double>(sourceAttack)));
        const auto renderAttack = attackFrame(out, rendered.getNumSamples(), expected - 441, sound.rate);
        CHECK(std::abs(renderAttack - expected) <= 44); // 1 ms
    }

    // The first 5 ms of every piece are the source, sample for sample.
    const auto monoIn = mono(sound.audio);
    const auto cuts = beatsCutPoints(monoIn.data(), static_cast<juce::int64>(monoIn.size()), sound.rate, 90.0);
    REQUIRE(cuts.size() >= 16);
    const auto five = static_cast<juce::int64>(0.005 * sound.rate);
    for (const auto cut : cuts)
    {
        const auto destination = static_cast<juce::int64>(std::llround(ratio * static_cast<double>(cut)));
        bool identical = true;
        for (juce::int64 k = 0; k < five && identical; ++k)
            identical = out[destination + k] == in[cut + k];
        CHECK(identical);
    }
}

TEST_CASE("the same tempo gives the sound back unchanged", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    const auto rendered = renderBeats(sound.audio, sound.rate, params(90.0, 90.0));
    REQUIRE(rendered.getNumSamples() == sound.audio.getNumSamples());
    bool identical = true;
    for (int c = 0; c < 2 && identical; ++c)
        identical = std::memcmp(rendered.getReadPointer(c), sound.audio.getReadPointer(c), sizeof(float) * static_cast<size_t>(rendered.getNumSamples())) == 0;
    CHECK(identical);
}

TEST_CASE("slower than the sound: pieces end with a short fade and silence follows", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    const auto rendered = renderBeats(sound.audio, sound.rate, params(90.0, 60.0)); // ratio 1.5
    CHECK(rendered.getNumSamples() == 705600);
    // The gap before the second beat (a piece of 0.667 s plays for 0.667 s of a 1 s slot): silence just before it.
    const auto truth = sampler::test::loadAnalysisFixture("break-90");
    const auto beat2 = static_cast<juce::int64>(std::llround(1.5 * static_cast<double>(truth.onsets[1])));
    float peak = 0.0f;
    for (auto i = beat2 - 400; i < beat2 - 40; ++i)
        peak = std::max(peak, std::abs(rendered.getSample(0, static_cast<int>(i))));
    CHECK(peak < 1.0e-6f);
}

TEST_CASE("a sound without hits is cut on the 1/16 grid and still follows the tempo", "[render][beats]")
{
    const auto sound = load("pad-sustain-100.wav");
    const auto m = mono(sound.audio);
    const auto cuts = beatsCutPoints(m.data(), static_cast<juce::int64>(m.size()), sound.rate, 100.0);
    REQUIRE(cuts.size() == 64); // 4 bars x 16 sixteenth notes
    for (size_t k = 0; k < cuts.size(); ++k)
        CHECK(cuts[k] == static_cast<juce::int64>(k) * 6615);

    const auto rendered = renderBeats(sound.audio, sound.rate, params(100.0, 130.0));
    CHECK(rendered.getNumSamples() == static_cast<int>(std::llround(423360.0 * 100.0 / 130.0)));
}

TEST_CASE("a reversed render is the render of the reversed sound", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    auto flipped = sound.audio;
    for (int c = 0; c < flipped.getNumChannels(); ++c)
        std::reverse(flipped.getWritePointer(c), flipped.getWritePointer(c) + flipped.getNumSamples());

    const auto direct = renderBeats(flipped, sound.rate, params(90.0, 120.0));
    const auto reversed = renderBeats(sound.audio, sound.rate, params(90.0, 120.0, true));
    REQUIRE(reversed.getNumSamples() == 352800); // still 4 bars at 120 BPM
    REQUIRE(direct.getNumSamples() == reversed.getNumSamples());
    bool identical = true;
    for (int c = 0; c < 2 && identical; ++c)
        identical = std::memcmp(direct.getReadPointer(c), reversed.getReadPointer(c), sizeof(float) * static_cast<size_t>(direct.getNumSamples())) == 0;
    CHECK(identical);
    CHECK(reversed.getMagnitude(0, reversed.getNumSamples()) > 0.3f);
}

TEST_CASE("transposing keeps the length and changes the sound", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    const auto plain = renderBeats(sound.audio, sound.rate, params(90.0, 120.0));
    const auto up = renderBeats(sound.audio, sound.rate, params(90.0, 120.0, false, 5.0f));
    REQUIRE(up.getNumSamples() == plain.getNumSamples());
    CHECK(up.getMagnitude(0, up.getNumSamples()) > 0.3f);
    double difference = 0.0;
    for (int i = 0; i < plain.getNumSamples(); ++i)
        difference += std::abs(plain.getSample(0, i) - up.getSample(0, i));
    CHECK(difference > 100.0);
}

TEST_CASE("a mono sound renders mono, and bad tempos render nothing", "[render][beats]")
{
    const auto sound = load("pattern-120-7s.wav");
    REQUIRE(sound.audio.getNumChannels() == 1);
    const auto rendered = renderBeats(sound.audio, sound.rate, params(120.0, 100.0));
    CHECK(rendered.getNumChannels() == 1);
    CHECK(rendered.getNumSamples() == static_cast<int>(std::llround(308700.0 * 1.2)));

    CHECK(renderBeats(sound.audio, sound.rate, params(0.0, 100.0)).getNumSamples() == 0);
    CHECK(renderBeats(sound.audio, sound.rate, params(120.0, 5000.0)).getNumSamples() == 0);
    CHECK(renderBeats(juce::AudioBuffer<float>(), 44100.0, params(120.0, 100.0)).getNumSamples() == 0);
}

TEST_CASE("the render key changes with each input and not with anything else", "[render][beats]")
{
    const auto base = beatsRenderKey("sound-a", params(90.0, 120.0));
    CHECK(base.length() == 16);
    CHECK(base == beatsRenderKey("sound-a", params(90.0, 120.0)));
    CHECK(base != beatsRenderKey("sound-b", params(90.0, 120.0)));
    CHECK(base != beatsRenderKey("sound-a", params(92.0, 120.0)));
    CHECK(base != beatsRenderKey("sound-a", params(90.0, 121.0)));
    CHECK(base != beatsRenderKey("sound-a", params(90.0, 120.0, true)));
    CHECK(base != beatsRenderKey("sound-a", params(90.0, 120.0, false, 2.0f)));
    // 4 decimals of a tempo are part of the key, the 5th is not.
    CHECK(base == beatsRenderKey("sound-a", params(90.00001, 120.0)));
}

TEST_CASE("renderBeatsToFile writes the key-named file once, through a temporary file", "[render][beats]")
{
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/beats-file");
    dir.deleteRecursively();
    const auto wav = juce::File(SAMPLER_FIXTURES_DIR).getChildFile("break-90.wav");
    const auto destination = dir.getChildFile("beats").getChildFile("abc.wav");

    juce::String error;
    REQUIRE(renderBeatsToFile(wav, destination, params(90.0, 120.0), error));
    REQUIRE(destination.existsAsFile());
    CHECK(destination.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.tmp").isEmpty());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(destination));
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == 352800);
    CHECK(reader->sampleRate == 44100.0);
    CHECK(reader->usesFloatingPointData);
    reader.reset();

    // A second call leaves the existing file alone.
    const auto stamp = destination.getLastModificationTime();
    juce::Thread::sleep(20);
    REQUIRE(renderBeatsToFile(wav, destination, params(90.0, 120.0), error));
    CHECK(destination.getLastModificationTime() == stamp);

    CHECK_FALSE(renderBeatsToFile(dir.getChildFile("missing.wav"), dir.getChildFile("x.wav"), params(90.0, 120.0), error));
    CHECK(error.isNotEmpty());
    dir.deleteRecursively();
}

TEST_CASE("a 4-bar Beats render takes under 50 ms", "[render][beats][perf]")
{
    const auto sound = load("break-90.wav");
    double best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto rendered = renderBeats(sound.audio, sound.rate, params(90.0, 120.0));
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        REQUIRE(rendered.getNumSamples() > 0);
    }
    INFO("best of 3: " << best << " ms");
    CHECK(best < 50.0);
}

TEST_CASE("transposing does not move the hits: each attack is within 2 ms of the untransposed one", "[render][beats]")
{
    const auto sound = load("break-90.wav");
    const auto truth = sampler::test::loadAnalysisFixture("break-90");
    const auto plain = renderBeats(sound.audio, sound.rate, params(90.0, 120.0));
    REQUIRE(truth.onsets.size() == 16);
    for (const float semitones : {5.0f, -7.0f})
    {
        const auto shifted = renderBeats(sound.audio, sound.rate, params(90.0, 120.0, false, semitones));
        REQUIRE(shifted.getNumSamples() == plain.getNumSamples());
        for (size_t i = 0; i < truth.onsets.size(); ++i)
        {
            INFO("hit " << i << " at " << semitones << " semitones");
            const auto expected = static_cast<juce::int64>(std::llround(0.75 * static_cast<double>(truth.onsets[i])));
            const auto plainAttack = attackFrame(plain.getReadPointer(0), plain.getNumSamples(), expected - 441, sound.rate);
            const auto shiftedAttack = attackFrame(shifted.getReadPointer(0), shifted.getNumSamples(), expected - 441, sound.rate);
            CHECK(std::abs(shiftedAttack - plainAttack) <= 88); // 2 ms

            // And there is sound in the first 5 ms of the piece, not a gap that the stretcher left.
            float peak = 0.0f;
            for (juce::int64 k = 0; k < 220; ++k)
                peak = std::max(peak, std::abs(shifted.getSample(0, static_cast<int>(plainAttack + k))));
            CHECK(peak > 0.05f);
        }
    }
}
