#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace sampler::test
{
/** A fixture decoded to mono, with the truth file next to it (tempo and hit frames). */
struct AnalysisFixtureData
{
    juce::String name;
    double sampleRate = 0.0;
    int channels = 0;
    std::vector<float> mono;
    double tempo = 0.0;
    juce::int64 firstBeatFrame = 0;
    std::vector<juce::int64> onsets;
};

inline juce::File fixturesDir()
{
    return juce::File(SAMPLER_FIXTURES_DIR);
}

inline bool readMono(const juce::File& file, std::vector<float>& mono, double& rate, int& channels)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return false;
    rate = reader->sampleRate;
    channels = static_cast<int>(reader->numChannels);
    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(channels, frames);
    reader->read(&buffer, 0, frames, 0, true, true);
    mono.assign(static_cast<size_t>(frames), 0.0f);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            mono[static_cast<size_t>(i)] += buffer.getSample(c, i) / static_cast<float>(channels);
    return true;
}

inline AnalysisFixtureData loadAnalysisFixture(const juce::String& name)
{
    AnalysisFixtureData data;
    data.name = name;
    readMono(fixturesDir().getChildFile(name + ".wav"), data.mono, data.sampleRate, data.channels);
    const auto truth = juce::JSON::parse(fixturesDir().getChildFile(name + ".json"));
    data.tempo = truth["tempo"];
    data.firstBeatFrame = static_cast<juce::int64>(truth["firstBeatFrame"]);
    if (const auto* onsets = truth["onsets"].getArray())
        for (const auto& v : *onsets)
            data.onsets.push_back(static_cast<juce::int64>(v));
    return data;
}

inline const std::vector<juce::String>& analysisFixtureNames()
{
    static const std::vector<juce::String> names{"break-90", "pattern-120-7s", "loop-120-48k"};
    return names;
}
} // namespace sampler::test
