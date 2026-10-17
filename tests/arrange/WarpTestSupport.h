#pragma once

// Helpers for the M4a warp tests: fixture sounds in a project, Beats renders made by hand, and audio measurements.
#include "ArrangeFixture.h"

#include "engine/OfflineRender.h"
#include "engine/WarpService.h"
#include "render/BeatsRenderer.h"

#include <algorithm>
#include <cmath>

namespace sampler::test
{
/** Copies a fixture (tests/audio-fixtures) into the project like an import would and records it; returns the source id. */
inline juce::String addFixtureSound(ArrangeFixture& f, const juce::String& fileName, model::Category category = model::Category::loop)
{
    const juce::File fixture = juce::File(SAMPLER_FIXTURES_DIR).getChildFile(fileName);
    REQUIRE(fixture.existsAsFile());
    const auto base = fixture.getFileNameWithoutExtension();
    const auto decoded = "audio/" + base + ".wav";
    const auto original = "audio/originals/" + base + ".wav";
    f.project->bundleDir().getChildFile("audio/originals").createDirectory();
    fixture.copyFileTo(f.project->bundleDir().getChildFile(decoded));
    fixture.copyFileTo(f.project->bundleDir().getChildFile(original));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(fixture));
    REQUIRE(reader != nullptr);

    model::SourceData data;
    data.hash = "sha256:" + juce::String::toHexString(base.hashCode64()).paddedLeft('0', 16);
    data.id = "src_" + base;
    data.name = base;
    data.original = original;
    data.decoded = decoded;
    data.sampleRate = reader->sampleRate;
    data.channels = static_cast<int>(reader->numChannels);
    data.frames = reader->lengthInSamples;
    data.category = category;
    f.commands->apply(std::make_unique<commands::AddSources>(std::vector<model::SourceData>{data}));
    return data.id;
}

/** A clip spec for a sound of the project, covering all of it. */
inline engine::ClipSpec specFor(ArrangeFixture& f, const juce::String& sourceId, double startBeat, const engine::WarpSettings& warp = {})
{
    const auto source = model::SamplerState(f.project->samplerState()).findSourceById(sourceId);
    REQUIRE(source.isValid());
    engine::ClipSpec spec;
    spec.sourceId = source.id();
    spec.name = source.name();
    spec.file = f.project->bundleDir().getChildFile(source.decoded());
    spec.start = startBeat;
    spec.lengthSeconds = source.lengthSeconds();
    spec.warp = warp;
    return spec;
}

/** Lets the message loop run until WarpService has made every render the clips wait for (false after `timeoutMs`). */
inline bool waitForBeatsRenders(ArrangeFixture& f, int timeoutMs = 30000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    f.project->warpService().scan();
    while (! (f.project->warpService().isIdle() && f.arrangement().allRendersReady()))
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

/** Number of Beats render files in the project's cache. */
inline int numBeatsFiles(ArrangeFixture& f)
{
    return f.project->bundleDir().getChildFile("cache/beats").findChildFiles(juce::File::findFiles, false, "*.wav").size();
}

/** The first line where two serialised edits differ (both lines shown), or an empty string when they are equal. */
inline juce::String firstDifference(const juce::String& a, const juce::String& b)
{
    // The transport node gains zero loop points when a tempo change is undone (before M4a too): not what these tests look at.
    const auto withoutTransport = [](const juce::String& text) {
        auto lines = juce::StringArray::fromLines(text);
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].trim().startsWith("<TRANSPORT"))
                lines.remove(i);
        return lines;
    };
    const auto left = withoutTransport(a);
    const auto right = withoutTransport(b);
    for (int i = 0; i < std::max(left.size(), right.size()); ++i)
        if (left[i] != right[i])
            return "line " + juce::String(i) + ": [" + left[i].trim() + "] != [" + right[i].trim() + "]";
    return {};
}

/** Renders the project offline into a temp file and reads it back (stereo, 48 kHz). */
inline juce::AudioBuffer<float> renderProject(ArrangeFixture& f, const juce::String& name, double* rate = nullptr)
{
    const auto dest = f.root.getChildFile(name + ".wav");
    engine::RenderOptions options;
    options.sampleRate = 48000.0;
    juce::String error;
    REQUIRE(engine::renderToWav(*f.project, dest, options, error));
    return readWav(dest, rate);
}

/** Frequency from the rising zero crossings of channel 0 between two times (seconds). 0 when there are too few. */
inline double frequencyBetween(const juce::AudioBuffer<float>& b, double rate, double fromSeconds, double toSeconds)
{
    const auto from = static_cast<int>(fromSeconds * rate);
    const auto to = std::min(b.getNumSamples(), static_cast<int>(toSeconds * rate));
    const auto* x = b.getReadPointer(0);
    double first = -1.0, last = -1.0;
    int count = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double position = (i - 1) + (-x[i - 1] / (x[i] - x[i - 1]));
            if (first < 0.0)
                first = position;
            last = position;
            ++count;
        }
    return count < 3 ? 0.0 : (count - 1) * rate / (last - first);
}

/** First and last frame (channel 0) whose magnitude is above the threshold; -1 when there is none. */
inline int firstAbove(const juce::AudioBuffer<float>& b, float threshold, int from = 0)
{
    for (int i = from; i < b.getNumSamples(); ++i)
        if (std::abs(b.getSample(0, i)) > threshold)
            return i;
    return -1;
}

inline int lastAbove(const juce::AudioBuffer<float>& b, float threshold)
{
    for (int i = b.getNumSamples() - 1; i >= 0; --i)
        if (std::abs(b.getSample(0, i)) > threshold)
            return i;
    return -1;
}
} // namespace sampler::test
