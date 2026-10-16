#include "FixtureGenerator.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <lame.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace sampler::fixtures
{
namespace
{
constexpr double pi = 3.14159265358979323846;

/** Deterministic noise in [-1, 1): xorshift32, no standard-library distribution (those differ between libraries). */
class Noise
{
public:
    explicit Noise(uint32_t seed) : state(seed) {}

    double next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 2147483648.0 - 1.0;
    }

private:
    uint32_t state;
};

using Channels = std::vector<std::vector<double>>;

Channels makeChannels(int numChannels, int frames)
{
    return Channels(static_cast<size_t>(numChannels), std::vector<double>(static_cast<size_t>(frames), 0.0));
}

/** Scales so the loudest sample is `peak`. */
void normalise(Channels& channels, double peak)
{
    double loudest = 0.0;
    for (auto& channel : channels)
        for (auto s : channel)
            loudest = std::max(loudest, std::abs(s));
    if (loudest <= 0.0)
        return;
    for (auto& channel : channels)
        for (auto& s : channel)
            s *= peak / loudest;
}

/** Rounds to 16-bit steps, so every format starts from the same integers. */
juce::AudioBuffer<float> quantise(const Channels& channels)
{
    const auto frames = static_cast<int>(channels.front().size());
    juce::AudioBuffer<float> buffer(static_cast<int>(channels.size()), frames);
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < frames; ++i)
        {
            const auto value = channels[static_cast<size_t>(c)][static_cast<size_t>(i)];
            buffer.setSample(c, i, static_cast<float>(static_cast<double>(std::lround(value * 32767.0)) / 32768.0));
        }
    return buffer;
}

//==============================================================================
// Sounds
void addKick(std::vector<double>& out, int start, double sampleRate, double gain)
{
    const int length = static_cast<int>(0.4 * sampleRate);
    double phase = 0.0;
    for (int i = 0; i < length && start + i < static_cast<int>(out.size()); ++i)
    {
        const double t = i / sampleRate;
        const double frequency = 45.0 + 110.0 * std::exp(-t * 28.0);
        phase += 2.0 * pi * frequency / sampleRate;
        const double attack = std::min(1.0, i / (0.001 * sampleRate));
        out[static_cast<size_t>(start + i)] += gain * attack * std::exp(-t * 9.0) * std::sin(phase);
    }
}

void addSnare(std::vector<double>& out, int start, double sampleRate, double gain, Noise& noise)
{
    const int length = static_cast<int>(0.25 * sampleRate);
    for (int i = 0; i < length && start + i < static_cast<int>(out.size()); ++i)
    {
        const double t = i / sampleRate;
        const double body = std::sin(2.0 * pi * 190.0 * t) * std::exp(-t * 30.0);
        out[static_cast<size_t>(start + i)] += gain * (0.6 * noise.next() * std::exp(-t * 18.0) + 0.4 * body);
    }
}

void addHat(std::vector<double>& out, int start, double sampleRate, double gain, Noise& noise)
{
    const int length = static_cast<int>(0.06 * sampleRate);
    double previous = 0.0;
    for (int i = 0; i < length && start + i < static_cast<int>(out.size()); ++i)
    {
        const double t = i / sampleRate;
        const double white = noise.next();
        const double highPassed = white - previous; // crude high-pass
        previous = white;
        out[static_cast<size_t>(start + i)] += gain * highPassed * std::exp(-t * 70.0);
    }
}

/** Two bars at 120 BPM: kick on 1 and 3, snare on 2 and 4, hats on eighths; exactly 4 s of stereo. */
juce::AudioBuffer<float> makeDrumLoop(double sampleRate)
{
    const int frames = static_cast<int>(4.0 * sampleRate);
    const double beat = 0.5 * sampleRate;
    auto channels = makeChannels(2, frames);
    Noise noise(12345);
    std::vector<double> kick(static_cast<size_t>(frames), 0.0), snare = kick, hat = kick;
    for (int b = 0; b < 8; ++b)
    {
        const int at = static_cast<int>(b * beat);
        if (b % 2 == 0)
            addKick(kick, at, sampleRate, 1.0);
        else
            addSnare(snare, at, sampleRate, 0.8, noise);
    }
    for (int e = 0; e < 16; ++e)
        addHat(hat, static_cast<int>(e * beat / 2.0), sampleRate, e % 2 == 0 ? 0.35 : 0.2, noise);
    for (size_t i = 0; i < static_cast<size_t>(frames); ++i)
    {
        channels[0][i] = kick[i] + 0.9 * snare[i] + 1.2 * hat[i];
        channels[1][i] = kick[i] + 1.1 * snare[i] + 0.8 * hat[i];
    }
    normalise(channels, 0.8);
    return quantise(channels);
}

juce::AudioBuffer<float> makeKick(double sampleRate)
{
    const int frames = static_cast<int>(0.4 * sampleRate);
    auto channels = makeChannels(1, frames);
    addKick(channels[0], 0, sampleRate, 1.0);
    normalise(channels, 0.9);
    return quantise(channels);
}

/** A 4-note bass line at 120 BPM (A1 A1 C2 E2), mono, a few harmonics with a decaying envelope per note. */
juce::AudioBuffer<float> makeBass(double sampleRate)
{
    const int frames = static_cast<int>(2.0 * sampleRate);
    auto channels = makeChannels(1, frames);
    const double notes[] = {55.0, 55.0, 65.41, 82.41};
    for (int n = 0; n < 4; ++n)
    {
        const int start = static_cast<int>(n * 0.5 * sampleRate);
        const int length = static_cast<int>(0.48 * sampleRate);
        for (int i = 0; i < length; ++i)
        {
            const double t = i / sampleRate;
            double sample = 0.0;
            for (int h = 1; h <= 6; ++h)
                sample += std::sin(2.0 * pi * notes[n] * h * t) / h;
            const double envelope = std::min(1.0, i / (0.004 * sampleRate)) * std::exp(-t * 3.0);
            channels[0][static_cast<size_t>(start + i)] += sample * envelope;
        }
    }
    normalise(channels, 0.7);
    return quantise(channels);
}

/** A sustained A minor chord (A3 C4 E4) with slow vibrato, stereo, 3 s with fades. */
juce::AudioBuffer<float> makePad(double sampleRate)
{
    const int frames = static_cast<int>(3.0 * sampleRate);
    auto channels = makeChannels(2, frames);
    const double notes[] = {220.0, 261.63, 329.63};
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / sampleRate;
        const double fade = std::min({1.0, t / 0.3, (3.0 - t) / 0.5});
        double left = 0.0, right = 0.0;
        for (int n = 0; n < 3; ++n)
        {
            const double vibrato = 1.0 + 0.003 * std::sin(2.0 * pi * (4.0 + n) * t);
            left += std::sin(2.0 * pi * notes[n] * 0.998 * vibrato * t);
            right += std::sin(2.0 * pi * notes[n] * 1.002 * vibrato * t);
        }
        channels[0][static_cast<size_t>(i)] = fade * left;
        channels[1][static_cast<size_t>(i)] = fade * right;
    }
    normalise(channels, 0.6);
    return quantise(channels);
}

/** An evolving texture: a slowly gliding tone with swelling noise and (in stereo) a moving image. */
juce::AudioBuffer<float> makeTexture(double sampleRate, double seconds, int numChannels)
{
    const int frames = static_cast<int>(seconds * sampleRate);
    auto channels = makeChannels(numChannels, frames);
    Noise noise(777);
    double phase = 0.0, smoothed = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / sampleRate;
        const double frequency = 110.0 * std::pow(2.0, 2.0 * std::sin(2.0 * pi * t / 20.0));
        phase += 2.0 * pi * frequency / sampleRate;
        smoothed += 0.02 * (noise.next() - smoothed);
        const double swell = 0.5 + 0.5 * std::sin(2.0 * pi * t / 7.0);
        const double mono = 0.6 * std::sin(phase) + swell * 2.0 * smoothed;
        if (numChannels == 1)
        {
            channels[0][static_cast<size_t>(i)] = mono;
            continue;
        }
        const double pan = 0.5 + 0.5 * std::sin(2.0 * pi * t / 13.0);
        channels[0][static_cast<size_t>(i)] = mono * (1.0 - 0.6 * pan);
        channels[1][static_cast<size_t>(i)] = mono * (0.4 + 0.6 * pan);
    }
    normalise(channels, 0.7);
    return quantise(channels);
}

/** Five channels, each a different steady tone, 1 s. */
juce::AudioBuffer<float> makeFiveChannelTone(double sampleRate)
{
    const int frames = static_cast<int>(1.0 * sampleRate);
    auto channels = makeChannels(5, frames);
    for (int c = 0; c < 5; ++c)
        for (int i = 0; i < frames; ++i)
            channels[static_cast<size_t>(c)][static_cast<size_t>(i)] = 0.4 * std::sin(2.0 * pi * (220.0 * (c + 1)) * i / sampleRate);
    return quantise(channels);
}


//==============================================================================
// Analysis fixtures: known tempo and known hit positions (written next to the audio as `<name>.json`).
struct AnalysisFixture
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    double tempo = 120.0;
    std::vector<int> onsets; // first sample of each hit
};

enum class Drum { kick, snare, hat };

void addDrum(std::vector<double>& out, Drum drum, int start, double sampleRate, double gain, Noise& noise)
{
    switch (drum)
    {
        case Drum::kick: addKick(out, start, sampleRate, gain); break;
        case Drum::snare: addSnare(out, start, sampleRate, gain, noise); break;
        case Drum::hat: addHat(out, start, sampleRate, gain, noise); break;
    }
}

double drumGain(Drum drum)
{
    switch (drum)
    {
        case Drum::kick: return 1.0;
        case Drum::snare: return 0.8;
        case Drum::hat: return 0.6;
    }
    return 1.0;
}

/**
    4 bars of 4/4 at 90 BPM, exactly 470400 frames at 44.1 kHz (a loop that is a whole number of bars). 16 hits, one on
    every beat, all shifted by 100 ms of lead silence, so the first beat is the first kick and not the file start.
*/
AnalysisFixture makeBreak90()
{
    constexpr double rate = 44100.0;
    constexpr int frames = 470400;
    constexpr int beatFrames = 29400; // 60 / 90 s
    constexpr int lead = 4410;        // 100 ms
    const char pattern[] = "KHSHKKSHKHSKKHSS";
    AnalysisFixture fixture;
    fixture.sampleRate = rate;
    fixture.tempo = 90.0;
    auto channels = makeChannels(2, frames);
    Noise noise(4242);
    std::vector<double> mix(static_cast<size_t>(frames), 0.0);
    for (int b = 0; b < 16; ++b)
    {
        const auto drum = pattern[b] == 'K' ? Drum::kick : pattern[b] == 'S' ? Drum::snare : Drum::hat;
        const int at = lead + b * beatFrames;
        addDrum(mix, drum, at, rate, drumGain(drum), noise);
        fixture.onsets.push_back(at);
    }
    for (size_t i = 0; i < mix.size(); ++i)
    {
        channels[0][i] = mix[i];
        channels[1][i] = 0.9 * mix[i];
    }
    normalise(channels, 0.8);
    fixture.audio = quantise(channels);
    return fixture;
}

/** 7 s (3.5 bars) of a steady 120 BPM pattern: kick and snare on the quarters, hats on the offbeats. Mono. */
AnalysisFixture makePattern7s()
{
    constexpr double rate = 44100.0;
    constexpr int frames = 308700;
    constexpr int beatFrames = 22050;
    AnalysisFixture fixture;
    fixture.sampleRate = rate;
    fixture.tempo = 120.0;
    auto channels = makeChannels(1, frames);
    Noise noise(9001);
    for (int b = 0; b < 14; ++b)
    {
        const auto drum = b % 2 == 0 ? Drum::kick : Drum::snare;
        addDrum(channels[0], drum, b * beatFrames, rate, drumGain(drum), noise);
        fixture.onsets.push_back(b * beatFrames);
        const int offbeat = b * beatFrames + beatFrames / 2;
        if (offbeat < frames - 2000)
        {
            addDrum(channels[0], Drum::hat, offbeat, rate, drumGain(Drum::hat), noise);
            fixture.onsets.push_back(offbeat);
        }
    }
    std::sort(fixture.onsets.begin(), fixture.onsets.end());
    normalise(channels, 0.8);
    fixture.audio = quantise(channels);
    return fixture;
}

/** The 2-bar 120 BPM kick/snare loop at 48 kHz, exactly 192000 frames: checks that rates other than 44.1 kHz work. */
AnalysisFixture makeLoop48k()
{
    constexpr double rate = 48000.0;
    constexpr int frames = 192000;
    constexpr int beatFrames = 24000;
    AnalysisFixture fixture;
    fixture.sampleRate = rate;
    fixture.tempo = 120.0;
    auto channels = makeChannels(2, frames);
    Noise noise(31337);
    std::vector<double> mix(static_cast<size_t>(frames), 0.0);
    for (int b = 0; b < 8; ++b)
    {
        // kick on 1 and 3, snare on 2 and 4, plus a second kick on the "and" of 3 in each bar.
        const auto drum = b % 2 == 0 ? Drum::kick : Drum::snare;
        addDrum(mix, drum, b * beatFrames, rate, drumGain(drum), noise);
        fixture.onsets.push_back(b * beatFrames);
        if (b % 4 == 2)
        {
            const int extra = b * beatFrames + beatFrames / 2;
            addDrum(mix, Drum::kick, extra, rate, 0.7, noise);
            fixture.onsets.push_back(extra);
        }
    }
    std::sort(fixture.onsets.begin(), fixture.onsets.end());
    for (size_t i = 0; i < mix.size(); ++i)
    {
        channels[0][i] = mix[i];
        channels[1][i] = mix[i];
    }
    normalise(channels, 0.8);
    fixture.audio = quantise(channels);
    return fixture;
}

/** The truth file next to an analysis fixture: tempo, first beat and every hit as a frame at the file's rate. */
juce::String truthJson(const AnalysisFixture& fixture)
{
    juce::String text;
    text << "{\n  \"tempo\": " << juce::String(fixture.tempo, 1) << ",\n  \"sampleRate\": "
         << juce::String(static_cast<int>(fixture.sampleRate)) << ",\n  \"frames\": " << fixture.audio.getNumSamples()
         << ",\n  \"firstBeatFrame\": " << (fixture.onsets.empty() ? 0 : fixture.onsets.front()) << ",\n  \"onsets\": [";
    for (size_t i = 0; i < fixture.onsets.size(); ++i)
        text << (i == 0 ? "" : ", ") << fixture.onsets[i];
    text << "]\n}\n";
    return text;
}

//==============================================================================
// Writers
juce::String writeWithFormat(juce::AudioFormat& format, const juce::File& file, const juce::AudioBuffer<float>& audio,
                             double sampleRate, int bits, int qualityIndex = 0)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return "cannot create " + file.getFullPathName();
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream.get(), sampleRate, static_cast<unsigned int>(audio.getNumChannels()), bits, {}, qualityIndex));
    if (writer == nullptr)
        return "no writer for " + file.getFileName();
    stream.release();
    if (! writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        return "write failed: " + file.getFileName();
    return {};
}

/** LAME-encodes mono or stereo audio. With `writeTag`, the first frame becomes the Xing/Info + LAME tag frame. */
juce::String writeMp3(const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, bool writeTag)
{
    const int channels = audio.getNumChannels();
    const int frames = audio.getNumSamples();
    std::vector<short> interleaved(static_cast<size_t>(frames) * static_cast<size_t>(channels));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            interleaved[static_cast<size_t>(i * channels + c)] =
                static_cast<short>(juce::jlimit(-32768, 32767, juce::roundToInt(audio.getSample(c, i) * 32768.0f)));

    lame_t lame = lame_init();
    if (lame == nullptr)
        return "lame_init failed";
    lame_set_in_samplerate(lame, static_cast<int>(sampleRate));
    lame_set_num_channels(lame, channels);
    lame_set_num_samples(lame, static_cast<unsigned long>(frames));
    lame_set_mode(lame, channels == 1 ? MONO : JOINT_STEREO);
    lame_set_brate(lame, 192);
    lame_set_quality(lame, 2);
    lame_set_bWriteVbrTag(lame, writeTag ? 1 : 0);
    if (lame_init_params(lame) < 0)
    {
        lame_close(lame);
        return "lame_init_params failed";
    }

    std::vector<unsigned char> mp3(static_cast<size_t>(frames) * 2 + 65536);
    int used = lame_encode_buffer_interleaved(lame, interleaved.data(), frames, mp3.data(), static_cast<int>(mp3.size()));
    if (used < 0)
    {
        lame_close(lame);
        return "lame_encode_buffer failed";
    }
    const int flushed = lame_encode_flush(lame, mp3.data() + used, static_cast<int>(mp3.size()) - used);
    if (flushed < 0)
    {
        lame_close(lame);
        return "lame_encode_flush failed";
    }
    used += flushed;

    if (writeTag)
    {
        std::vector<unsigned char> tag(4096);
        const size_t tagSize = lame_get_lametag_frame(lame, tag.data(), tag.size());
        if (tagSize == 0 || tagSize > tag.size() || tagSize > static_cast<size_t>(used))
        {
            lame_close(lame);
            return "no LAME tag frame";
        }
        std::copy(tag.begin(), tag.begin() + static_cast<std::ptrdiff_t>(tagSize), mp3.begin());
    }
    lame_close(lame);

    return file.replaceWithData(mp3.data(), static_cast<size_t>(used)) ? juce::String() : "cannot write " + file.getFullPathName();
}

juce::String writeCorrupt(const juce::File& file)
{
    Noise noise(99);
    std::vector<unsigned char> bytes(2048);
    for (auto& b : bytes)
        b = static_cast<unsigned char>(static_cast<int>((noise.next() + 1.0) * 127.5));
    return file.replaceWithData(bytes.data(), bytes.size()) ? juce::String() : "cannot write " + file.getFullPathName();
}
} // namespace

juce::StringArray fixtureFileNames()
{
    return {"drums-loop-120.wav", "drums-loop-120.aiff", "drums-loop-120.mp3", "drums-loop-120-notag.mp3",
            "kick.wav",           "bass-phrase.flac",    "pad-chord.ogg",      "texture-60s.flac",
            "5ch-tone.wav",       "corrupt.wav",         "not-audio.txt",
            "break-90.wav",       "break-90.json",       "pattern-120-7s.wav", "pattern-120-7s.json",
            "loop-120-48k.wav",   "loop-120-48k.json"};
}

juce::String generateFixtures(const juce::File& dir)
{
    if (! dir.createDirectory())
        return "cannot create " + dir.getFullPathName();

    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    juce::OggVorbisAudioFormat ogg;

    const auto loop = makeDrumLoop(44100.0);

    juce::String error;
    const auto step = [&error](const juce::String& result) {
        if (error.isEmpty())
            error = result;
    };
    step(writeWithFormat(wav, dir.getChildFile("drums-loop-120.wav"), loop, 44100.0, 16));
    step(writeWithFormat(aiff, dir.getChildFile("drums-loop-120.aiff"), loop, 44100.0, 16));
    step(writeMp3(dir.getChildFile("drums-loop-120.mp3"), loop, 44100.0, true));
    step(writeMp3(dir.getChildFile("drums-loop-120-notag.mp3"), loop, 44100.0, false));
    step(writeWithFormat(wav, dir.getChildFile("kick.wav"), makeKick(44100.0), 44100.0, 16));
    step(writeWithFormat(flac, dir.getChildFile("bass-phrase.flac"), makeBass(48000.0), 48000.0, 16));
    step(writeWithFormat(ogg, dir.getChildFile("pad-chord.ogg"), makePad(44100.0), 44100.0, 16, 5));
    step(writeWithFormat(flac, dir.getChildFile("texture-60s.flac"), makeTexture(44100.0, 60.0, 2), 44100.0, 16));
    step(writeWithFormat(wav, dir.getChildFile("5ch-tone.wav"), makeFiveChannelTone(44100.0), 44100.0, 16));
    step(writeCorrupt(dir.getChildFile("corrupt.wav")));
    if (! dir.getChildFile("not-audio.txt").replaceWithText("This is not audio.\n", false, false, "\n"))
        step("cannot write not-audio.txt");

    const std::pair<const char*, AnalysisFixture> analysisFixtures[] = {
        {"break-90", makeBreak90()}, {"pattern-120-7s", makePattern7s()}, {"loop-120-48k", makeLoop48k()}};
    for (const auto& [name, fixture] : analysisFixtures)
    {
        step(writeWithFormat(wav, dir.getChildFile(juce::String(name) + ".wav"), fixture.audio, fixture.sampleRate, 16));
        if (! dir.getChildFile(juce::String(name) + ".json").replaceWithText(truthJson(fixture), false, false, "\n"))
            step(juce::String("cannot write ") + name + ".json");
    }
    return error;
}

juce::String generateTooLongFixture(const juce::File& dir)
{
    if (! dir.createDirectory())
        return "cannot create " + dir.getFullPathName();
    juce::WavAudioFormat wav;
    return writeWithFormat(wav, dir.getChildFile("toolong-31min.wav"), makeTexture(8000.0, 31.0 * 60.0, 1), 8000.0, 16);
}

juce::String generateLongFixtures(const juce::File& dir)
{
    if (! dir.createDirectory())
        return "cannot create " + dir.getFullPathName();
    juce::WavAudioFormat wav;
    const auto error = writeWithFormat(wav, dir.getChildFile("texture-10min.wav"), makeTexture(44100.0, 600.0, 2), 44100.0, 16);
    if (error.isNotEmpty())
        return error;
    return writeWithFormat(wav, dir.getChildFile("toolong-31min.wav"), makeTexture(8000.0, 31.0 * 60.0, 1), 8000.0, 16);
}
} // namespace sampler::fixtures
