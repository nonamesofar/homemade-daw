#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace sampler::test
{
/** Writes a 16-bit stereo WAV: a sine tone with a short click at every beat. Returns the file. */
inline juce::File writeToneWav(const juce::File& dest, double seconds, double sampleRate = 44100.0, double bpm = 120.0)
{
    dest.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(dest.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 2, 16, {}, 0));
    if (writer == nullptr)
        return {};
    stream.release();

    const auto frames = static_cast<int>(seconds * sampleRate);
    const auto beatFrames = static_cast<int>(sampleRate * 60.0 / bpm);
    juce::AudioBuffer<float> buffer(2, frames);
    for (int i = 0; i < frames; ++i)
    {
        float s = 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate));
        if (i % beatFrames < 200)
            s += 0.5f;
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, s);
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return dest;
}

inline juce::File tempDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests").getChildFile(name);
    dir.createDirectory();
    return dir;
}
} // namespace sampler::test
