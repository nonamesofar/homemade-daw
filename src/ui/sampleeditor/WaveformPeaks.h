#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <vector>

namespace sampler::ui::editor
{
/**
    Minimum and maximum of a decoded sound at three block sizes (64, 1024 and 16384 frames), so the waveform of a
    5-minute sound can be drawn at any zoom from a few thousand values. Below 64 frames per pixel the samples are read
    from the file on demand. Channels are folded together (min of the mins, max of the maxes). Built on a worker.

    `fillPeaks` and `readSamples` use the file and belong to the message thread; the built tables are immutable.
*/
class WaveformPeaks
{
public:
    struct Range
    {
        float min = 0.0f;
        float max = 0.0f;
    };

    /** Reads the WAV and builds the tables. Null if it cannot be read or `cancel` was set. Any thread. */
    static std::shared_ptr<WaveformPeaks> build(const juce::File& wav, const std::atomic<bool>* cancel = nullptr);

    juce::int64 numFrames() const { return frames; }
    double sampleRate() const { return rate; }
    int numChannels() const { return channels; }

    /** One range per pixel for the part of the sound that starts at `startFrame`, `framesPerPixel` frames per pixel. */
    void fillPeaks(double startFrame, double framesPerPixel, int numPixels, std::vector<Range>& out) const;

    /** The mono mix of `count` frames from `startFrame` (frames outside the sound are 0). */
    void readSamples(juce::int64 startFrame, int count, std::vector<float>& out) const;

    static constexpr int blockSizes[3] = {64, 1024, 16384};

private:
    WaveformPeaks() = default;
    void readRegion(juce::int64 startFrame, int count, juce::AudioBuffer<float>& into) const;

    juce::File file;
    juce::int64 frames = 0;
    double rate = 44100.0;
    int channels = 1;
    std::vector<Range> levels[3];

    mutable juce::CriticalSection readerLock;
    mutable std::unique_ptr<juce::AudioFormatReader> reader;
};
} // namespace sampler::ui::editor
