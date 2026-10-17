#include "render/BeatsRenderer.h"

#include "analysis/OnsetDetector.h"
#include "render/PitchShift.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <new>
#include <memory>
#include <string>

namespace sampler::render
{
namespace
{
/** More than this many output frames per channel (30 minutes at 192 kHz) is refused: a tempo far out of range. */
constexpr juce::int64 maxOutputFrames = 30 * 60 * 192'000;

bool tempoInRange(double bpm) { return std::isfinite(bpm) && bpm >= 20.0 && bpm <= 999.0; }

std::vector<float> mixToMono(const juce::AudioBuffer<float>& audio)
{
    const auto frames = static_cast<size_t>(audio.getNumSamples());
    std::vector<float> mono(frames, 0.0f);
    const auto channels = audio.getNumChannels();
    for (int c = 0; c < channels; ++c)
    {
        const auto* in = audio.getReadPointer(c);
        for (size_t i = 0; i < frames; ++i)
            mono[i] += in[i];
    }
    if (channels > 1)
        for (auto& s : mono)
            s /= static_cast<float>(channels);
    return mono;
}

/** Moves `from` over `to` with one rename; false if `to` could not be replaced (for example a reader holds it open). */
bool renameOver(const juce::File& from, const juce::File& to)
{
    const auto path = [](const juce::File& f)
    { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(f.getFullPathName().toRawUTF8()))); };
    std::error_code error;
    std::filesystem::rename(path(from), path(to), error);
    return ! error;
}
} // namespace

std::vector<juce::int64> beatsCutPoints(const float* mono, juce::int64 numFrames, double rate, double sourceBpm)
{
    std::vector<juce::int64> cuts;
    if (numFrames <= 0 || rate <= 0.0)
        return cuts;

    const auto analysis = analysis::detectOnsets(mono, numFrames, rate);
    auto hits = analysis::selectTransients(analysis.onsets, beatsOnsetSensitivity);
    // The detector scales every sound so that its strongest candidate is 1, so a sustained pad still "has" hits made of
    // noise. A real hit rises far above that: the strongest flux after the very first hop (which is compared with
    // silence) must pass an absolute floor, and one hit alone cannot be a rhythm.
    float strongest = 0.0f;
    for (size_t h = 1; h < analysis.flux.size(); ++h)
        strongest = std::max(strongest, analysis.flux[h]);
    if (strongest < beatsMinHitFlux || hits.size() < 2)
        hits.clear();
    if (! hits.empty())
    {
        cuts.push_back(0);
        for (const auto& hit : hits)
            if (hit.frame > 0 && hit.frame < numFrames)
                cuts.push_back(hit.frame);
        if (cuts.size() > 1 && cuts[1] < static_cast<juce::int64>(0.005 * rate))
            cuts.erase(cuts.begin() + 1);
    }
    else
    {
        const auto step = rate * 60.0 / sourceBpm / beatsGridCutsPerBeat;
        for (int k = 0; static_cast<juce::int64>(std::floor(k * step)) < numFrames; ++k)
            cuts.push_back(static_cast<juce::int64>(std::floor(k * step)));
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    return cuts;
}

juce::AudioBuffer<float> renderBeats(const juce::AudioBuffer<float>& sound, double rate, const BeatsParams& params,
                                    const std::function<bool()>& shouldStop)
{
    const auto numFrames = sound.getNumSamples();
    const auto numChannels = std::min(2, sound.getNumChannels());
    if (numFrames <= 0 || numChannels <= 0 || rate <= 0.0 || ! tempoInRange(params.sourceBpm) || ! tempoInRange(params.projectBpm))
        return {};

    const auto ratio = params.ratio();
    const auto outFrames = static_cast<juce::int64>(std::llround(static_cast<double>(numFrames) * ratio));
    if (outFrames <= 0 || outFrames > maxOutputFrames)
        return {};

    // The sound as it plays: reversed first, so that the pieces and the grid are those of the reversed sound.
    juce::AudioBuffer<float> source(numChannels, numFrames);
    for (int c = 0; c < numChannels; ++c)
    {
        source.copyFrom(c, 0, sound, c, 0, numFrames);
        if (params.reverse)
            std::reverse(source.getWritePointer(c), source.getWritePointer(c) + numFrames);
    }

    const auto mono = mixToMono(source);
    const auto cuts = beatsCutPoints(mono.data(), numFrames, rate, params.sourceBpm);

    juce::AudioBuffer<float> out(numChannels, static_cast<int>(outFrames));
    out.clear();

    const auto transposed = std::abs(params.transposeSemitones) > 0.001f;
    const auto gapFadeFrames = static_cast<juce::int64>(rate * beatsGapFadeMs / 1000.0);
    const auto cutFadeFrames = static_cast<juce::int64>(rate * beatsCutFadeMs / 1000.0);
    const auto smallestGap = std::max<juce::int64>(1, static_cast<juce::int64>(rate * 0.001));

    for (size_t i = 0; i < cuts.size(); ++i)
    {
        if (shouldStop && shouldStop())
            return {};
        const auto from = cuts[i];
        const auto to = i + 1 < cuts.size() ? cuts[i + 1] : static_cast<juce::int64>(numFrames);
        const auto length = to - from;
        const auto destination = static_cast<juce::int64>(std::llround(static_cast<double>(from) * ratio));
        const auto slotEnd = i + 1 < cuts.size() ? static_cast<juce::int64>(std::llround(static_cast<double>(to) * ratio)) : outFrames;
        const auto slot = std::min(slotEnd, outFrames) - destination;
        const auto copy = std::min(length, slot);
        if (copy <= 0)
            continue;

        juce::AudioBuffer<float> piece(numChannels, static_cast<int>(length));
        for (int c = 0; c < numChannels; ++c)
            piece.copyFrom(c, 0, source, c, static_cast<int>(from), static_cast<int>(length));

        if (transposed)
        {
            StereoChannels shifted;
            for (size_t c = 0; c < 2; ++c)
            {
                const auto* in = piece.getReadPointer(std::min(static_cast<int>(c), numChannels - 1));
                shifted[c].assign(in, in + length);
            }
            pitchShift(shifted, params.transposeSemitones, rate);
            for (int c = 0; c < numChannels; ++c)
                std::copy(shifted[static_cast<size_t>(c)].begin(), shifted[static_cast<size_t>(c)].begin() + length,
                          piece.getWritePointer(c));
        }

        // The end of the piece: cut short by the next piece (3 ms), or followed by a gap (10 ms).
        juce::int64 fade = 0;
        if (length > slot)
            fade = std::min(cutFadeFrames, copy / 2);
        else if (slot - length >= smallestGap)
            fade = std::min(gapFadeFrames, copy / 2);
        if (fade > 1)
            for (int c = 0; c < numChannels; ++c)
                piece.applyGainRamp(c, static_cast<int>(copy - fade), static_cast<int>(fade), 1.0f, 0.0f);

        for (int c = 0; c < numChannels; ++c)
            out.copyFrom(c, static_cast<int>(destination), piece, c, 0, static_cast<int>(copy));
    }
    return out;
}

namespace
{
bool renderBeatsToFileImpl(const juce::File& decodedWav, const juce::File& destination, const BeatsParams& params,
                           juce::String& error, const std::function<bool()>& shouldStop)
{
    if (destination.existsAsFile())
        return true;

    juce::WavAudioFormat wav;
    auto stream = decodedWav.createInputStream();
    if (stream == nullptr)
    {
        error = "Cannot read " + decodedWav.getFullPathName();
        return false;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        error = "Cannot decode " + decodedWav.getFileName();
        return false;
    }
    const auto channels = static_cast<int>(std::min<unsigned int>(2, reader->numChannels));
    juce::AudioBuffer<float> sound(channels, static_cast<int>(reader->lengthInSamples));
    if (! reader->read(&sound, 0, sound.getNumSamples(), 0, true, channels > 1))
    {
        error = "Cannot read " + decodedWav.getFileName();
        return false;
    }

    if (shouldStop && shouldStop())
    {
        error = "Stopped";
        return false;
    }
    const auto rendered = renderBeats(sound, reader->sampleRate, params, shouldStop);
    if (shouldStop && shouldStop())
    {
        error = "Stopped";
        return false;
    }
    if (rendered.getNumSamples() == 0)
    {
        error = "Cannot render " + decodedWav.getFileName() + " at these tempos";
        return false;
    }

    // Two workers may make the folder at the same moment: one of them "fails" although the folder is there.
    if (! destination.getParentDirectory().createDirectory() && ! destination.getParentDirectory().isDirectory())
    {
        error = "Cannot create " + destination.getParentDirectory().getFullPathName();
        return false;
    }
    // A name no other job uses, so two workers rendering the same key never write the same temporary file.
    const auto temporary = destination.getSiblingFile(
        destination.getFileName() + "." + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) + ".tmp");
    {
        auto out = temporary.createOutputStream();
        if (out == nullptr)
        {
            error = "Cannot write " + temporary.getFullPathName();
            return false;
        }
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), reader->sampleRate, static_cast<unsigned int>(rendered.getNumChannels()), 32, {}, 0));
        if (writer == nullptr)
        {
            out.reset();
            temporary.deleteFile();
            error = "Cannot write " + temporary.getFullPathName();
            return false;
        }
        out.release(); // the writer owns the stream now
        const auto ok = writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples());
        writer.reset(); // flushes and closes
        // The writer hides a failed flush (a full disk): a file shorter than its samples is never renamed into its key.
        const auto expected = static_cast<juce::int64>(rendered.getNumSamples()) * rendered.getNumChannels() * 4;
        if (! ok || temporary.getSize() < expected)
        {
            temporary.deleteFile();
            error = "Cannot write " + temporary.getFullPathName();
            return false;
        }
    }
    if (! renameOver(temporary, destination))
    {
        temporary.deleteFile();
        if (destination.existsAsFile()) // another worker got there first
            return true;
        error = "Cannot move the render into " + destination.getParentDirectory().getFullPathName();
        return false;
    }
    return true;
}
} // namespace

bool renderBeatsToFile(const juce::File& decodedWav, const juce::File& destination, const BeatsParams& params, juce::String& error,
                       const std::function<bool()>& shouldStop)
{
    // Allocating a long sound can fail; an exception must not leave a worker thread.
    try
    {
        return renderBeatsToFileImpl(decodedWav, destination, params, error, shouldStop);
    }
    catch (const std::bad_alloc&)
    {
        error = "Not enough memory to render " + decodedWav.getFileName();
    }
    catch (const std::exception& e)
    {
        error = "Cannot render " + decodedWav.getFileName() + ": " + e.what();
    }
    return false;
}

juce::String beatsRenderKey(const juce::String& soundContentKey, const BeatsParams& params)
{
    juce::String text;
    text << "beats|v" << beatsRendererVersion << "|" << soundContentKey << "|" << juce::String(params.sourceBpm, 4) << "|"
         << juce::String(params.projectBpm, 4) << "|" << (params.reverse ? "r" : "f") << "|"
         << juce::String(static_cast<double>(params.transposeSemitones), 3);
    return juce::SHA256(text.toUTF8()).toHexString().substring(0, 16);
}
} // namespace sampler::render
