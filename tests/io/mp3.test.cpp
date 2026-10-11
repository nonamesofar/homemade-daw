// MP3 gapless trim: the LAME tag parser and the calibration of JUCE's MP3 decoder against a known WAV.
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

#include "io/Mp3Gapless.h"

using namespace sampler::io;

namespace
{
juce::File fixture(const char* name) { return juce::File(SAMPLER_FIXTURES_DIR).getChildFile(name); }

/** Left channel of a file decoded with the formats we register for import. */
std::vector<float> decodeLeft(const juce::File& file, double* sampleRate = nullptr)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::MP3AudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    if (sampleRate != nullptr)
        *sampleRate = reader->sampleRate;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return {buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples()};
}

/** The offset into `decoded` at which it best lines up with the start of `reference`. */
int bestLag(const std::vector<float>& decoded, const std::vector<float>& reference, int maxLag)
{
    constexpr int window = 20000;
    int best = 0;
    double bestScore = -1e30;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double score = 0.0;
        for (int i = 0; i < window; ++i)
            score += static_cast<double>(decoded[static_cast<size_t>(lag + i)]) * reference[static_cast<size_t>(i)];
        if (score > bestScore)
        {
            bestScore = score;
            best = lag;
        }
    }
    return best;
}
} // namespace

TEST_CASE("the LAME tag of the fixture is found and the tagless one has none", "[io][mp3]")
{
    const auto tagged = readLameTag(fixture("drums-loop-120.mp3"));
    REQUIRE(tagged.found);
    CHECK(tagged.encoderDelay == 576); // LAME default
    CHECK(tagged.encoderPadding > 0);
    CHECK(tagged.samplesPerFrame == 1152);
    // 176400 frames of audio + delay + padding fill a whole number of MPEG frames.
    CHECK((176400 + tagged.encoderDelay + tagged.encoderPadding) % 1152 == 0);
    CHECK(tagged.audioFrames * 1152 == 176400 + tagged.encoderDelay + tagged.encoderPadding);

    CHECK_FALSE(readLameTag(fixture("drums-loop-120-notag.mp3")).found);
    CHECK_FALSE(readLameTag(fixture("drums-loop-120.wav")).found);
    CHECK_FALSE(readLameTag(fixture("not-audio.txt")).found);
}

TEST_CASE("the parser skips an ID3v2 tag in front of the first frame", "[io][mp3]")
{
    juce::MemoryBlock file;
    juce::MemoryOutputStream out(file, false);
    // ID3v2 header declaring 20 bytes of tag body (synchsafe), then the real file.
    const unsigned char id3[10] = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 20};
    out.write(id3, sizeof(id3));
    for (int i = 0; i < 20; ++i)
        out.writeByte(0);
    juce::MemoryBlock original;
    fixture("drums-loop-120.mp3").loadFileAsData(original);
    out.write(original.getData(), original.getSize());
    out.flush();

    const auto tag = parseLameTag(file.getData(), file.getSize());
    const auto direct = readLameTag(fixture("drums-loop-120.mp3"));
    REQUIRE(tag.found);
    CHECK(tag.encoderDelay == direct.encoderDelay);
    CHECK(tag.encoderPadding == direct.encoderPadding);
}

TEST_CASE("an ID3v2 tag of 2 MiB (cover art) does not hide the LAME tag", "[io][mp3]")
{
    constexpr juce::uint32 bodySize = 2u * 1024u * 1024u;
    const auto path = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/big-id3.mp3");
    path.getParentDirectory().createDirectory();
    {
        juce::FileOutputStream out(path);
        REQUIRE(out.openedOk());
        out.setPosition(0);
        out.truncate();
        const unsigned char id3[10] = {'I', 'D', '3', 3, 0, 0, static_cast<unsigned char>((bodySize >> 21) & 0x7f),
                                       static_cast<unsigned char>((bodySize >> 14) & 0x7f),
                                       static_cast<unsigned char>((bodySize >> 7) & 0x7f),
                                       static_cast<unsigned char>(bodySize & 0x7f)};
        out.write(id3, sizeof(id3));
        for (juce::uint32 i = 0; i < bodySize; ++i)
            out.writeByte(0);
        juce::MemoryBlock original;
        fixture("drums-loop-120.mp3").loadFileAsData(original);
        out.write(original.getData(), original.getSize());
    }

    const auto tag = readLameTag(path);
    const auto direct = readLameTag(fixture("drums-loop-120.mp3"));
    REQUIRE(tag.found);
    CHECK(tag.encoderDelay == direct.encoderDelay);
    CHECK(tag.encoderPadding == direct.encoderPadding);
    CHECK(tag.audioFrames == direct.audioFrames);
    path.deleteFile();
}

TEST_CASE("the decoder delay of JUCE's MP3 reader is 529 frames on top of the encoder delay", "[io][mp3]")
{
    const auto wav = decodeLeft(fixture("drums-loop-120.wav"));
    const auto tagged = decodeLeft(fixture("drums-loop-120.mp3"));
    const auto tagless = decodeLeft(fixture("drums-loop-120-notag.mp3"));
    const auto tag = readLameTag(fixture("drums-loop-120.mp3"));

    // The decoder returns whole MPEG frames and not the tag frame itself.
    CHECK(static_cast<juce::int64>(tagged.size()) == tag.audioFrames * 1152);
    CHECK(tagless.size() == tagged.size());

    CHECK(bestLag(tagged, wav, 4000) == tag.encoderDelay + decoderDelayFrames);
    CHECK(bestLag(tagless, wav, 4000) == taglessStartCut);
}

TEST_CASE("cutting the tagged MP3 gives the WAV's length and alignment", "[io][mp3]")
{
    const auto wav = decodeLeft(fixture("drums-loop-120.wav"));
    const auto decoded = decodeLeft(fixture("drums-loop-120.mp3"));
    const auto cut = mp3CutFor(readLameTag(fixture("drums-loop-120.mp3")));

    REQUIRE(cut.length >= 0);
    CHECK(std::abs(cut.length - static_cast<juce::int64>(wav.size())) <= 1);
    REQUIRE(cut.start + cut.length <= static_cast<juce::int64>(decoded.size()));

    // Lossy, so not equal, but the error is a small part of the signal (a one-frame shift would be far larger).
    double signal = 0.0, error = 0.0;
    for (size_t i = 0; i < wav.size(); ++i)
    {
        const double d = decoded[static_cast<size_t>(cut.start) + i] - wav[i];
        signal += static_cast<double>(wav[i]) * wav[i];
        error += d * d;
    }
    CHECK(error / signal < 0.1); // measured 0.04 (192 kbps, noisy drums)

    // The first loud sample (the kick) is within one frame of the WAV's.
    const auto firstLoud = [](const std::vector<float>& v, size_t from) {
        for (size_t i = from; i < v.size(); ++i)
            if (std::abs(v[i]) > 0.2f)
                return static_cast<long long>(i - from);
        return -1LL;
    };
    CHECK(std::abs(firstLoud(decoded, static_cast<size_t>(cut.start)) - firstLoud(wav, 0)) <= 1);
}

TEST_CASE("an MP3 without a tag loses 1105 frames at the start", "[io][mp3]")
{
    const auto cut = mp3CutFor(readLameTag(fixture("drums-loop-120-notag.mp3")));
    CHECK(cut.start == 1105);
    CHECK(cut.length == -1); // the end is unknown without a tag
}
