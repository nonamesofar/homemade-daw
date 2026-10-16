// Pad probe (M3 task 1.1).
//
//   PadProbe <fixtures-dir>
//
//   (a) A SlicePadPlugin on a track of a preview edit gets applyToBuffer calls while that edit's transport is stopped,
//       and its output is heard while another edit plays to the same device (checked on the device output through a
//       loopback capture: a 1 kHz pad slice next to a 440 Hz tone from the project-side player).
//   (b) Signalsmith Stretch on a 2 s stereo slice at +7 st: render time, length, delay of the attack and attack smear.
//   (c) r8brain 48 -> 44.1 kHz on a 2 s slice: time and the pitch of a sine.
//
// Prints every number; the results are recorded in docs/m0-findings.md.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/EngineHost.h"
#include "engine/PadEngine.h"
#include "engine/testkit/FilePlayer.h"
#include "platform/ICaptureSource.h"
#include "render/SliceRenderer.h"

namespace
{
constexpr double pi = 3.14159265358979323846;

void pump(double seconds)
{
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

double amplitudeAt(const float* x, int n, double rate, double freq)
{
    const auto k = 2.0 * std::cos(2.0 * pi * freq / rate);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto s0 = static_cast<double>(x[i]) + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / n;
}

juce::AudioBuffer<float> sine(double frequency, double rate, double seconds)
{
    const auto frames = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> buffer(2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.4f * static_cast<float>(std::sin(2.0 * pi * frequency * i / rate)));
    return buffer;
}

juce::File writeTone(const juce::File& dest, double frequency, double seconds)
{
    dest.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = dest.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 2, 16, {}, 0));
    stream.release();
    const auto buffer = sine(frequency, 44100.0, seconds);
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    return dest;
}

double bestOf(int runs, const std::function<void()>& work)
{
    double best = 1e9;
    for (int i = 0; i < runs; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        work();
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    return best;
}

double measureFrequency(const float* data, int n, int stride, double rate)
{
    int first = -1, last = -1, crossings = 0;
    for (int i = n / 4 + 1; i < 3 * n / 4; ++i)
        if (data[stride * (i - 1)] <= 0.0f && data[stride * i] > 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings < 2 ? 0.0 : (crossings - 1) * rate / (last - first);
}

/** Rise of the first hit: frames from 10% to 90% of its peak level, and the frame where it crosses 10%. */
void attackShape(const juce::AudioBuffer<float>& audio, int channel, int& tenPercentFrame, int& riseFrames)
{
    const int n = audio.getNumSamples();
    // Envelope: peak over 0.25 ms blocks (11 samples).
    const int block = 11;
    std::vector<float> env;
    for (int i = 0; i + block <= n; i += block)
    {
        float p = 0.0f;
        for (int j = 0; j < block; ++j)
            p = std::max(p, std::abs(audio.getSample(channel, i + j)));
        env.push_back(p);
    }
    const auto peak = *std::max_element(env.begin(), env.begin() + std::min<size_t>(env.size(), 2000));
    int a = -1, b = -1;
    for (size_t i = 0; i < env.size(); ++i)
    {
        if (a < 0 && env[i] > 0.1f * peak)
            a = static_cast<int>(i);
        if (b < 0 && env[i] > 0.9f * peak)
            b = static_cast<int>(i);
        if (a >= 0 && b >= 0)
            break;
    }
    tenPercentFrame = a * block;
    riseFrames = (b - a) * block;
}

//==============================================================================
int probeA()
{
    std::printf("== (a) pad plugin in a preview edit\n");
    sampler::EngineHost host;
    pump(4.0);
    if (! host.isAudioReady())
    {
        std::printf("no audio device: (a) cannot run\n");
        return 1;
    }
    const double rate = host.currentSampleRate();
    std::printf("device: %s, %.0f Hz, buffer %d\n", host.currentAudioDeviceName().toRawUTF8(), rate, host.currentBufferSize());

    sampler::PadEngine pads(host);
    pump(0.5);
    const auto idleBefore = pads.blocksProcessed();
    pump(0.5);
    const auto idleAfter = pads.blocksProcessed();
    std::printf("transport stopped, nothing playing: blocks processed in 0.5 s: %lld\n", idleAfter - idleBefore);

    sampler::render::SliceParams params;
    params.sourceRate = params.outputRate = rate;
    sampler::render::SliceBank::Ptr bank = new sampler::render::SliceBank();
    bank->buffers[0] = sampler::render::renderSlice(sine(1000.0, rate, 2.0), params);
    bank->modes[0] = sampler::render::PlayMode::oneShot;
    pads.setBank(bank);
    pump(0.2);

    const auto before = pads.blocksProcessed();
    pads.padOn(0);
    pump(0.3);
    std::printf("pad hit while the preview transport is stopped: sounding=%d, blocks processed in 0.3 s: %lld\n", pads.soundingCount(0),
                pads.blocksProcessed() - before);
    const bool stoppedWorks = pads.soundingCount(0) == 1 && pads.blocksProcessed() - before > 0;
    std::printf("(a) plugin runs on a stopped preview edit: %s\n", stoppedWorks ? "PASS" : "FAIL (use the keep-playing fallback)");

    // Heard next to a playing project-side edit: capture the device output.
    pads.padOff(0);
    pump(2.2);
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests");
    dir.createDirectory();
    const auto tone = writeTone(dir.getChildFile("padprobe-tone.wav"), 440.0, 8.0);
    sampler::FilePlayer player(host);
    if (! player.loadFile(tone))
    {
        std::printf("cannot load the tone\n");
        return 1;
    }
    player.play();
    pump(0.5);

    const auto out = dir.getChildFile("padprobe-capture.wav");
    auto capture = sampler::platform::createCaptureSource();
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false;
    if (capture->start(out, options).failed())
    {
        std::printf("loopback capture could not start\n");
        return 1;
    }
    pump(1.0);
    pads.padOn(0); // a 1 kHz slice for 2 s
    pump(3.0);
    capture->stop();
    player.stop();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(out.createInputStream().release(), true));
    if (reader == nullptr)
        return 1;
    const auto n = static_cast<int>(reader->lengthInSamples);
    const auto capRate = reader->sampleRate;
    juce::AudioBuffer<float> recorded(static_cast<int>(reader->numChannels), n);
    reader->read(&recorded, 0, n, 0, true, true);
    const int half = static_cast<int>(capRate / 2.0);
    const auto at = [&](double seconds, double freq) {
        return amplitudeAt(recorded.getReadPointer(0) + static_cast<int>(seconds * capRate), half, capRate, freq);
    };
    const double tone0 = at(0.3, 440.0), pad0 = at(0.3, 1000.0);
    const double tone1 = at(2.0, 440.0), pad1 = at(2.0, 1000.0);
    std::printf("capture before the pad: 440 Hz %.3f, 1 kHz %.3f   during the pad: 440 Hz %.3f, 1 kHz %.3f\n", tone0, pad0, tone1, pad1);
    const bool together = tone0 > 0.05 && pad0 < 0.01 && tone1 > 0.05 && pad1 > 0.05;
    std::printf("(a) pad heard while the other edit plays to the same device: %s\n", together ? "PASS" : "FAIL");
    return stoppedWorks && together ? 0 : 3;
}

int probeBC(const juce::File& fixtures)
{
    std::printf("== (b) Signalsmith +7 st on a 2 s stereo slice\n");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(fixtures.getChildFile("drums-loop-120.wav")));
    if (reader == nullptr)
    {
        std::printf("fixture drums-loop-120.wav not found in %s\n", fixtures.getFullPathName().toRawUTF8());
        return 1;
    }
    const int frames = static_cast<int>(2.0 * reader->sampleRate);
    juce::AudioBuffer<float> slice(2, frames);
    reader->read(&slice, 0, frames, 0, true, true);

    sampler::render::SliceParams params;
    params.sourceRate = params.outputRate = reader->sampleRate;
    params.pitchSemitones = 7.0f;
    sampler::render::SliceBuffer::Ptr rendered;
    const auto ms = bestOf(5, [&] { rendered = sampler::render::renderSlice(slice, params); });
    std::printf("render time (best of 5): %.1f ms  (budget in the design: 20 ms, spec promise: 100 ms)\n", ms);
    std::printf("length in: %d frames, out: %d frames -> %s\n", frames, rendered->numFrames(),
                rendered->numFrames() == frames ? "kept" : "CHANGED");

    // De-interleave the render for the attack measurement.
    juce::AudioBuffer<float> out(2, rendered->numFrames());
    for (int i = 0; i < rendered->numFrames(); ++i)
    {
        out.setSample(0, i, rendered->data()[2 * i]);
        out.setSample(1, i, rendered->data()[2 * i + 1]);
    }
    int inTen = 0, inRise = 0, outTen = 0, outRise = 0;
    attackShape(slice, 0, inTen, inRise);
    attackShape(out, 0, outTen, outRise);
    std::printf("first kick, 10%% point: in %d frames, out %d frames (delay %.2f ms); rise 10-90%%: in %.2f ms, out %.2f ms (smear %+.2f ms)\n",
                inTen, outTen, 1000.0 * (outTen - inTen) / params.sourceRate, 1000.0 * inRise / params.sourceRate,
                1000.0 * outRise / params.sourceRate, 1000.0 * (outRise - inRise) / params.sourceRate);

    std::printf("== (c) r8brain 48 -> 44.1 kHz on a 2 s slice\n");
    sampler::render::SliceParams resample;
    resample.sourceRate = 48000.0;
    resample.outputRate = 44100.0;
    const auto tone = sine(1000.0, 48000.0, 2.0);
    sampler::render::SliceBuffer::Ptr resampled;
    const auto msResample = bestOf(5, [&] { resampled = sampler::render::renderSlice(tone, resample); });
    std::printf("render time (best of 5): %.1f ms, frames %d (expected %d)\n", msResample, resampled->numFrames(), 88200);
    std::printf("1 kHz sine measured at %.1f Hz after resampling\n", measureFrequency(resampled->data(), resampled->numFrames(), 2, 44100.0));
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto fixtures = juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? juce::String::fromUTF8(argv[1]) : "tests/audio-fixtures");
    const int bc = probeBC(fixtures);
    const int a = probeA();
    return bc != 0 ? bc : a;
}
