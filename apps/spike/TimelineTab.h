#pragma once

// Spike-only: demo audio and clips for the timeline, and the frame-rate benchmark driver.
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/EngineHost.h"
#include "ui/timeline/TimelineComponent.h"

namespace spike
{
struct DemoData
{
    std::vector<juce::File> files;
    std::vector<double> seconds;
    sampler::ui::TimelineModel model;
};

/** Five short drum-ish loops (noisy bursts on every beat) so the waveforms have visible structure. */
inline void writeDemoLoop(const juce::File& file, double seconds, int seed)
{
    constexpr double rate = 44100.0;
    const auto frames = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(2, frames);
    juce::Random random(seed);
    const auto beatFrames = static_cast<int>(rate * 0.5); // 120 BPM

    for (int i = 0; i < frames; ++i)
    {
        const auto inBeat = i % beatFrames;
        const auto beat = i / beatFrames;
        const auto t = inBeat / rate;
        const auto kick = std::sin(2.0 * juce::MathConstants<double>::pi * (60.0 + 140.0 * std::exp(-t * 30.0)) * t)
                          * std::exp(-t * 9.0) * (beat % 2 == 0 ? 0.9 : 0.35 + 0.1 * seed);
        const auto hat = (random.nextFloat() * 2.0f - 1.0f) * std::exp(-((i % (beatFrames / 2)) / rate) * 60.0) * 0.25;
        const auto s = static_cast<float>(std::clamp(kick + hat, -1.0, 1.0));
        buffer.setSample(0, i, s);
        buffer.setSample(1, i, s * 0.9f);
    }

    file.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 2, 16, {}, 0));
    if (writer != nullptr)
    {
        stream.release();
        writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    }
}

/** 4 tracks, 50 clips, laid out end to end with small gaps. */
inline DemoData makeDemoData(const juce::File& dir)
{
    dir.createDirectory();
    DemoData data;
    const double lengths[] = {4.0, 6.0, 8.0, 10.0, 12.0};
    for (int i = 0; i < 5; ++i)
    {
        const auto file = dir.getChildFile("demo-loop-" + juce::String(i) + ".wav");
        if (!file.existsAsFile())
            writeDemoLoop(file, lengths[i], i + 1);
        data.files.push_back(file);
        data.seconds.push_back(lengths[i]);
    }

    const int clipsPerTrack[] = {13, 13, 12, 12}; // 50 in total
    int clipId = 1;
    for (int t = 0; t < 4; ++t)
    {
        sampler::ui::TrackModel track;
        track.id = t + 1;
        track.name = "Track " + std::to_string(t + 1);
        double cursor = t * 1.0;
        for (int c = 0; c < clipsPerTrack[t]; ++c)
        {
            const int source = (c + t * 2) % 5;
            const double fullBeats = data.seconds[static_cast<size_t>(source)] * 2.0;
            const double beats = c % 3 == 2 ? fullBeats * 0.5 : fullBeats; // some clips are trimmed
            track.clips.push_back({clipId++, cursor, beats, source});
            cursor += beats + (c % 4 == 3 ? 4.0 : 0.0);
        }
        data.model.tracks.push_back(std::move(track));
    }
    return data;
}

inline std::unique_ptr<sampler::ui::TimelineComponent> makeTimeline(sampler::EngineHost& host, DemoData data)
{
    auto timeline = std::make_unique<sampler::ui::TimelineComponent>(
        std::move(data.model), std::move(data.files), std::move(data.seconds),
        [&host](const juce::File& file, juce::Component& repaintTarget) { return host.createThumbnail(file, repaintTarget); });
    return timeline;
}

/** Zooms in and out and scrolls back and forth for a while, repainting every tick, then reports the frame rate. */
class TimelineBench : private juce::Timer
{
public:
    TimelineBench(sampler::ui::TimelineComponent& t, double seconds, std::function<void()> whenDone)
        : timeline(t), duration(seconds), done(std::move(whenDone))
    {
        timeline.onPainted = [this](double ms) { paintTimes.push_back(ms); };
        startedAt = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }

    ~TimelineBench() override { timeline.onPainted = nullptr; }

private:
    void timerCallback() override
    {
        const auto t = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0;
        if (t >= duration)
        {
            stopTimer();
            report(t);
            if (done)
                done();
            return;
        }

        auto& view = timeline.getViewport();
        const auto centre = view.widthPx * 0.5;
        const auto anchorBeat = view.xToBeat(centre);
        view.pixelsPerBeat = 40.0 * std::pow(2.0, std::sin(t * 0.9) * 2.0); // about 10 to 160 px per beat
        view.firstBeat = anchorBeat - centre / view.pixelsPerBeat;
        view.scrollByPixels(std::sin(t * 0.5) * 60.0);
        view.clamp(timeline.getModel().endBeat());
        timeline.repaint();
    }

    void report(double wallSeconds)
    {
        auto sorted = paintTimes;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (auto v : sorted)
            sum += v;
        const auto n = sorted.size();
        std::printf("timeline-bench: %.1f s, %zu paints, %.1f fps (timer caps at 60), paint avg %.2f ms, p95 %.2f ms, max %.2f ms\n",
                    wallSeconds, n, static_cast<double>(n) / wallSeconds, n ? sum / static_cast<double>(n) : 0.0,
                    n ? sorted[static_cast<size_t>(static_cast<double>(n) * 0.95)] : 0.0, n ? sorted.back() : 0.0);
        std::fflush(stdout);
    }

    sampler::ui::TimelineComponent& timeline;
    double duration;
    std::function<void()> done;
    double startedAt = 0.0;
    std::vector<double> paintTimes;
};
} // namespace spike
