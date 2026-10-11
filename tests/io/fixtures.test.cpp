#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include "FixtureGenerator.h"

namespace
{
struct Decoded
{
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 frames = 0;
    juce::AudioBuffer<float> samples;
};

bool decode(const juce::File& file, Decoded& out)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    formats.registerFormat(new juce::FlacAudioFormat(), false);
    formats.registerFormat(new juce::OggVorbisAudioFormat(), false);
    formats.registerFormat(new juce::MP3AudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return false;
    out.sampleRate = reader->sampleRate;
    out.channels = static_cast<int>(reader->numChannels);
    out.frames = reader->lengthInSamples;
    out.samples.setSize(out.channels, static_cast<int>(out.frames));
    return reader->read(&out.samples, 0, static_cast<int>(out.frames), 0, true, true);
}
} // namespace

TEST_CASE("regenerated fixtures decode to the committed samples", "[io][fixtures]")
{
    const juce::File committed(SAMPLER_FIXTURES_DIR);
    auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/fixtures-regen");
    temp.deleteRecursively();
    REQUIRE(sampler::fixtures::generateFixtures(temp).isEmpty());

    for (const auto& name : sampler::fixtures::fixtureFileNames())
    {
        INFO(name);
        const auto fresh = temp.getChildFile(name);
        const auto reference = committed.getChildFile(name);
        REQUIRE(reference.existsAsFile());
        REQUIRE(fresh.existsAsFile());

        Decoded a, b;
        const bool freshDecodes = decode(fresh, a);
        const bool referenceDecodes = decode(reference, b);
        REQUIRE(freshDecodes == referenceDecodes);
        if (! freshDecodes)
        {
            // corrupt.wav and not-audio.txt: not audio, so the bytes themselves must match.
            REQUIRE(fresh.loadFileAsString() == reference.loadFileAsString());
            continue;
        }
        REQUIRE(a.sampleRate == b.sampleRate);
        REQUIRE(a.channels == b.channels);
        REQUIRE(a.frames == b.frames);
        bool identical = true;
        for (int c = 0; c < a.channels && identical; ++c)
            identical = std::memcmp(a.samples.getReadPointer(c), b.samples.getReadPointer(c),
                                    sizeof(float) * static_cast<size_t>(a.frames)) == 0;
        CHECK(identical);
    }
    temp.deleteRecursively();
}

TEST_CASE("fixtures have the properties the demos rely on", "[io][fixtures]")
{
    const juce::File dir(SAMPLER_FIXTURES_DIR);
    Decoded loop, kick, bass, five;
    REQUIRE(decode(dir.getChildFile("drums-loop-120.wav"), loop));
    CHECK(loop.sampleRate == 44100.0);
    CHECK(loop.channels == 2);
    CHECK(loop.frames == 176400);
    REQUIRE(decode(dir.getChildFile("kick.wav"), kick));
    CHECK(kick.channels == 1);
    CHECK(kick.frames < 2 * 44100);
    REQUIRE(decode(dir.getChildFile("bass-phrase.flac"), bass));
    CHECK(bass.sampleRate == 48000.0);
    CHECK(bass.channels == 1);
    REQUIRE(decode(dir.getChildFile("5ch-tone.wav"), five));
    CHECK(five.channels == 5);
    Decoded none;
    CHECK_FALSE(decode(dir.getChildFile("corrupt.wav"), none));
    CHECK_FALSE(decode(dir.getChildFile("not-audio.txt"), none));
}
