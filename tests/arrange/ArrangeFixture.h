#pragma once

// An engine-backed project for arrangement and command tests: a headless engine (no audio device), an untitled project
// in a fresh temp folder, and the CommandService over it with invariant checks on (violations are collected).
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "commands/ArrangeCommands.h"
#include "commands/CommandService.h"
#include "commands/Commands.h"
#include "engine/Arrangement.h"
#include "engine/EngineHost.h"
#include "engine/LevelMeters.h"
#include "engine/ProjectEdit.h"
#include "engine/Transport.h"
#include "model/SamplerState.h"

#include <cmath>
#include <memory>

namespace sampler::test
{
inline juce::File freshTempDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests").getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

/**
    Writes a 32-bit float stereo WAV. `kind`: "ramp" counts up from 0 to 1 over the file (so every sample says where in
    the file it is), "tone" is a 220 Hz sine at -12 dB with a click on every beat at 120 BPM, "dc" is a constant 0.5.
*/
inline void writeTestWav(const juce::File& dest, double seconds, const juce::String& kind = "tone", double rate = 48000.0,
                         float leftGain = 1.0f, float rightGain = 1.0f)
{
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = dest.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 2, 32, {}, 0)); // 32 bits = float
    REQUIRE(writer != nullptr);
    stream.release(); // the writer owns it now
    const auto frames = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(2, frames);
    const auto beatFrames = static_cast<int>(rate * 0.5);
    for (int i = 0; i < frames; ++i)
    {
        float s = 0.0f;
        if (kind == "ramp")
            s = static_cast<float>(i) / static_cast<float>(frames);
        else if (kind == "dc")
            s = 0.5f;
        else
        {
            s = 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate));
            if (i % beatFrames < 240)
                s += 0.5f;
        }
        buffer.setSample(0, i, s * leftGain);
        buffer.setSample(1, i, s * rightGain);
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
}

struct ArrangeFixture
{
    explicit ArrangeFixture(const juce::String& name, bool headless = true)
        : root(freshTempDir(name)), host("SamplerTest", headless)
    {
        juce::String error;
        project = ProjectEdit::createUntitled(host, root.getChildFile("untitled"), error);
        REQUIRE(project != nullptr);
        makeCommands();
    }

    ~ArrangeFixture()
    {
        commands.reset();
        project.reset();
    }

    void makeCommands()
    {
        commands = std::make_unique<commands::CommandService>(project->samplerState(), project->undoManager(),
                                                              project->bundleDir(), &project->arrangement());
        commands->invariantChecksEnabled = true;
        commands->onInvariantViolation = [this](const juce::String& message) { violations.add(message); };
    }

    engine::Arrangement& arrangement() { return project->arrangement(); }

    /** Writes a sound into the bundle and records it (one undo step); returns its id. */
    juce::String addSound(const juce::String& name, double seconds, const juce::String& kind = "tone", float left = 1.0f,
                          float right = 1.0f)
    {
        const auto decoded = "audio/" + name + ".wav";
        const auto original = "audio/originals/" + name + ".wav";
        writeTestWav(project->bundleDir().getChildFile(decoded), seconds, kind, 48000.0, left, right);
        project->bundleDir().getChildFile(decoded).copyFileTo(project->bundleDir().getChildFile(original));
        model::SourceData data;
        data.hash = "sha256:" + juce::String::toHexString(name.hashCode64()).paddedLeft('0', 16);
        data.id = "src_" + name;
        data.name = name;
        data.original = original;
        data.decoded = decoded;
        data.sampleRate = 48000.0;
        data.channels = 2;
        data.frames = static_cast<juce::int64>(seconds * 48000.0);
        commands->apply(std::make_unique<commands::AddSources>(std::vector<model::SourceData>{data}));
        return data.id;
    }

    /** Places a sound at a beat on a track index; returns the clip id. */
    juce::String place(const juce::String& sourceId, int track, double beat)
    {
        commands::CreateClipsFromSources command({sourceId}, {track, false, beat});
        commands->apply(command);
        return command.resultIds().empty() ? juce::String() : command.resultIds().front();
    }

    std::vector<engine::ClipInfo> clipsOn(int track)
    {
        const auto tracks = arrangement().tracks();
        return arrangement().clipsOnTrack(tracks[static_cast<size_t>(track)].id);
    }

    juce::String trackId(int index) { return arrangement().tracks()[static_cast<size_t>(index)].id; }

    /** Lets the arrangement's coalesced change message through. */
    static void pump(int ms = 30) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::File root;
    EngineHost host;
    std::unique_ptr<ProjectEdit> project;
    std::unique_ptr<commands::CommandService> commands;
    juce::StringArray violations;
};

/** Reads a WAV into a buffer (empty on failure). */
inline juce::AudioBuffer<float> readWav(const juce::File& file, double* rate = nullptr)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    if (rate != nullptr)
        *rate = reader->sampleRate;
    return buffer;
}

inline float peakOf(const juce::AudioBuffer<float>& b, int channel, int start, int length)
{
    start = juce::jlimit(0, b.getNumSamples(), start);
    length = juce::jlimit(0, b.getNumSamples() - start, length);
    return length > 0 ? b.getMagnitude(channel, start, length) : 0.0f;
}
} // namespace sampler::test
