#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <lame.h>

#include <cmath>
#include <vector>

// Smoke test for the vendored LAME: encode one second of sine and decode it with JUCE's MP3 reader.
TEST_CASE("LAME encodes a sine that JUCE decodes", "[io][lame]")
{
    constexpr int sampleRate = 44100;
    std::vector<short> pcm(sampleRate);
    for (int i = 0; i < sampleRate; ++i)
        pcm[(size_t) i] = (short) std::lround(12000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / sampleRate));

    lame_t lame = lame_init();
    REQUIRE(lame != nullptr);
    lame_set_num_channels(lame, 1);
    lame_set_in_samplerate(lame, sampleRate);
    lame_set_brate(lame, 128);
    lame_set_quality(lame, 5);
    REQUIRE(lame_init_params(lame) >= 0);

    std::vector<unsigned char> mp3(1 << 18);
    int used = lame_encode_buffer(lame, pcm.data(), nullptr, sampleRate, mp3.data(), (int) mp3.size());
    REQUIRE(used >= 0);
    const int flushed = lame_encode_flush(lame, mp3.data() + used, (int) mp3.size() - used);
    REQUIRE(flushed >= 0);
    used += flushed;
    lame_close(lame);
    REQUIRE(used > 1000);

    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-lame-smoke.mp3");
    REQUIRE(file.replaceWithData(mp3.data(), (size_t) used));

    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::MP3AudioFormat(), true);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == sampleRate);
    CHECK(reader->lengthInSamples >= sampleRate);
    CHECK(reader->lengthInSamples < sampleRate + 4000);

    juce::AudioBuffer<float> buffer(1, (int) reader->lengthInSamples);
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false);
    CHECK(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.2f);
    file.deleteFile();
}
