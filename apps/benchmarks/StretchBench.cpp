// Console benchmark for the warp options (M0 tasks 7.1 and 7.2).
//
//   StretchBench [--input file.wav] [--ratio 1.2] [--out folder] [--runs 3]
//
// Times Rubber Band (R2 "faster" and R3 "finer", offline), Signalsmith Stretch (default and cheaper presets) and a
// prototype beat-preserving renderer on a 4-bar loop (8 s at 120 BPM) and a 3-minute track. `ratio` is the output
// length divided by the input length (1.2 = 120 BPM slowed to 100 BPM). With --out, the 4-bar results are written as
// WAVs so they can be compared by ear. Without --input a synthetic drum-and-pad loop is used.
//
// The beats renderer here is a throwaway prototype of design 7.6 (split at onsets, place each segment at its stretched
// position, no stretching inside a segment), not the production BeatsRenderer.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <rubberband/RubberBandStretcher.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
constexpr double sampleRate = 44100.0;
constexpr double bpm = 120.0;

struct Stereo
{
    std::vector<float> l, r;
    size_t size() const { return l.size(); }
    void resize(size_t n) { l.assign(n, 0.0f); r.assign(n, 0.0f); }
};

double seconds(Clock::time_point from) { return std::chrono::duration<double>(Clock::now() - from).count(); }

//==============================================================================
// Test material
//==============================================================================
/** A drum loop (kick, snare, hats with noise) over a slow chord pad, `bars` bars at 120 BPM. Deterministic. */
Stereo makeLoop(int bars, int variation)
{
    const auto beat = static_cast<size_t>(sampleRate * 60.0 / bpm);
    Stereo s;
    s.resize(beat * 4 * static_cast<size_t>(bars));
    juce::Random rng(1234 + variation);

    auto add = [&](size_t at, size_t length, const std::function<float(double)>& voice, float gain)
    {
        for (size_t i = 0; i < length && at + i < s.size(); ++i)
        {
            const auto v = gain * voice(static_cast<double>(i) / sampleRate);
            s.l[at + i] += v;
            s.r[at + i] += v;
        }
    };

    const double pi = juce::MathConstants<double>::pi;
    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < 8; ++step) // eighth notes
        {
            const auto at = (static_cast<size_t>(bar) * 8 + static_cast<size_t>(step)) * beat / 2;
            if (step % 4 == 0 || (step == 6 && (bar + variation) % 2 == 0))
                add(at, static_cast<size_t>(0.25 * sampleRate),
                    [&](double t) { return static_cast<float>(std::sin(2 * pi * (55.0 + 90.0 * std::exp(-t * 30.0)) * t) * std::exp(-t * 9.0)); }, 0.8f);
            if (step % 4 == 2)
                add(at, static_cast<size_t>(0.2 * sampleRate),
                    [&](double t) { return (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 18.0)); }, 0.5f);
            add(at, static_cast<size_t>(0.04 * sampleRate),
                [&](double t) { return (rng.nextFloat() * 2.0f - 1.0f) * static_cast<float>(std::exp(-t * 120.0)); }, 0.18f);
        }

    const double chords[4][3] = { { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 }, { 196.0, 246.94, 293.66 }, { 164.81, 207.65, 246.94 } };
    for (size_t i = 0; i < s.size(); ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto& c = chords[(static_cast<size_t>(t / (beat * 4 / sampleRate)) + static_cast<size_t>(variation)) % 4];
        float pad = 0.0f;
        for (double f : c)
            pad += static_cast<float>(std::sin(2 * pi * f * t));
        s.l[i] += 0.05f * pad;
        s.r[i] += 0.05f * pad;
    }
    return s;
}

Stereo makeTrack(double lengthSeconds)
{
    const auto loopBars = 4;
    Stereo track;
    const auto target = static_cast<size_t>(lengthSeconds * sampleRate);
    for (int v = 0; track.size() < target; ++v)
    {
        auto loop = makeLoop(loopBars, v);
        track.l.insert(track.l.end(), loop.l.begin(), loop.l.end());
        track.r.insert(track.r.end(), loop.r.begin(), loop.r.end());
    }
    track.l.resize(target);
    track.r.resize(target);
    return track;
}

bool readWav(const juce::File& file, Stereo& out, double& rate)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr)
        return false;
    juce::AudioBuffer<float> buffer(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    out.l.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
    const auto* right = buffer.getReadPointer(reader->numChannels > 1 ? 1 : 0);
    out.r.assign(right, right + buffer.getNumSamples());
    rate = reader->sampleRate;
    return true;
}

void writeWav(const juce::File& file, const Stereo& s)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
        return;
    stream.release();
    const float* channels[2] = { s.l.data(), s.r.data() };
    writer->writeFromFloatArrays(channels, 2, static_cast<int>(s.size()));
}

//==============================================================================
// Stretchers
//==============================================================================
Stereo rubberBand(const Stereo& in, double ratio, int engineOption)
{
    using RB = RubberBand::RubberBandStretcher;
    RB stretcher(static_cast<size_t>(sampleRate), 2, RB::OptionProcessOffline | RB::OptionThreadingNever | engineOption, ratio, 1.0);
    stretcher.setExpectedInputDuration(in.size());

    const size_t block = 4096;
    const float* inPtr[2];
    // Study pass, then the real pass.
    for (size_t pos = 0; pos < in.size(); pos += block)
    {
        const auto n = std::min(block, in.size() - pos);
        inPtr[0] = in.l.data() + pos;
        inPtr[1] = in.r.data() + pos;
        stretcher.study(inPtr, n, pos + n >= in.size());
    }

    Stereo out;
    std::vector<float> tmpL(block), tmpR(block);
    float* outPtr[2] = { tmpL.data(), tmpR.data() };
    auto drain = [&]
    {
        int avail;
        while ((avail = stretcher.available()) > 0)
        {
            const auto n = stretcher.retrieve(outPtr, std::min<size_t>(block, static_cast<size_t>(avail)));
            out.l.insert(out.l.end(), tmpL.begin(), tmpL.begin() + static_cast<long>(n));
            out.r.insert(out.r.end(), tmpR.begin(), tmpR.begin() + static_cast<long>(n));
        }
    };
    for (size_t pos = 0; pos < in.size(); pos += block)
    {
        const auto n = std::min(block, in.size() - pos);
        inPtr[0] = in.l.data() + pos;
        inPtr[1] = in.r.data() + pos;
        stretcher.process(inPtr, n, pos + n >= in.size());
        drain();
    }
    drain();
    return out;
}

Stereo runSignalsmith(const Stereo& in, double ratio, bool cheaper)
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    if (cheaper)
        stretch.presetCheaper(2, static_cast<float>(sampleRate));
    else
        stretch.presetDefault(2, static_cast<float>(sampleRate));

    const auto outSize = static_cast<size_t>(std::llround(static_cast<double>(in.size()) * ratio));
    Stereo out;
    out.resize(outSize);

    // Process in chunks, keeping the input/output ratio constant per chunk.
    const size_t outBlock = 4096;
    size_t inPos = 0, outPos = 0;
    while (outPos < outSize)
    {
        const auto outN = std::min(outBlock, outSize - outPos);
        const auto inEnd = static_cast<size_t>(std::llround(static_cast<double>(outPos + outN) / ratio));
        const auto inN = std::min(in.size(), inEnd) - inPos;
        const float* inPtr[2] = { in.l.data() + inPos, in.r.data() + inPos };
        float* outPtr[2] = { out.l.data() + outPos, out.r.data() + outPos };
        stretch.process(inPtr, static_cast<int>(inN), outPtr, static_cast<int>(outN));
        inPos += inN;
        outPos += outN;
    }
    return out;
}

//==============================================================================
// Beats renderer prototype
//==============================================================================
/** Spectral-flux onset detector on the mono mix. Returns onset positions in frames. */
std::vector<size_t> detectOnsets(const Stereo& in)
{
    constexpr int order = 10; // 1024-point FFT
    constexpr size_t fftSize = 1u << order;
    constexpr size_t hop = 256;
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> frame(fftSize * 2), prev(fftSize / 2, 0.0f), flux;
    for (size_t pos = 0; pos + fftSize <= in.size(); pos += hop)
    {
        for (size_t i = 0; i < fftSize; ++i)
            frame[i] = 0.5f * (in.l[pos + i] + in.r[pos + i]);
        std::fill(frame.begin() + static_cast<long>(fftSize), frame.end(), 0.0f);
        window.multiplyWithWindowingTable(frame.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(frame.data());

        float sum = 0.0f;
        for (size_t k = 0; k < fftSize / 2; ++k)
        {
            const auto mag = std::log1p(10.0f * frame[k]);
            sum += std::max(0.0f, mag - prev[k]);
            prev[k] = mag;
        }
        flux.push_back(sum);
    }

    // Adaptive threshold: local mean plus a margin; pick local maxima at least 60 ms apart.
    std::vector<size_t> onsets;
    const int w = 16;
    const size_t minGap = static_cast<size_t>(0.06 * sampleRate) / hop;
    size_t lastPick = 0;
    for (size_t i = 1; i + 1 < flux.size(); ++i)
    {
        float mean = 0.0f;
        int count = 0;
        for (int j = -w; j <= w; ++j)
            if (static_cast<int>(i) + j >= 0 && i + static_cast<size_t>(j + w) < flux.size() + static_cast<size_t>(w))
            {
                const auto idx = static_cast<long>(i) + j;
                if (idx >= 0 && static_cast<size_t>(idx) < flux.size())
                {
                    mean += flux[static_cast<size_t>(idx)];
                    ++count;
                }
            }
        mean /= static_cast<float>(std::max(count, 1));
        if (flux[i] > mean * 1.6f + 0.5f && flux[i] >= flux[i - 1] && flux[i] > flux[i + 1] && (onsets.empty() || i - lastPick >= minGap))
        {
            // The flux frame i covers [i*hop, i*hop + fftSize); the attack is near the start of the loud part.
            onsets.push_back(i * hop + fftSize / 4);
            lastPick = i;
        }
    }
    return onsets;
}

/** Cuts the input at the onsets and places each segment at its stretched position without stretching inside it. */
Stereo renderBeats(const Stereo& in, const std::vector<size_t>& onsets, double ratio)
{
    Stereo out;
    out.resize(static_cast<size_t>(std::llround(static_cast<double>(in.size()) * ratio)));
    const auto fade = static_cast<size_t>(0.003 * sampleRate);

    std::vector<size_t> cuts = { 0 };
    cuts.insert(cuts.end(), onsets.begin(), onsets.end());
    cuts.push_back(in.size());

    for (size_t s = 0; s + 1 < cuts.size(); ++s)
    {
        const auto srcStart = cuts[s];
        const auto srcLen = cuts[s + 1] - cuts[s];
        const auto dstStart = static_cast<size_t>(std::llround(static_cast<double>(srcStart) * ratio));
        const auto slot = static_cast<size_t>(std::llround(static_cast<double>(cuts[s + 1]) * ratio)) - dstStart;
        // Faster tempo: the segment is cut to its slot. Slower tempo: it plays fully and silence fills the rest.
        const auto len = std::min(srcLen, slot);
        for (size_t i = 0; i < len && dstStart + i < out.size(); ++i)
        {
            float g = 1.0f;
            if (i < fade && s > 0)
                g = static_cast<float>(i) / static_cast<float>(fade);
            if (len < srcLen && i + fade >= len)
                g = std::min(g, static_cast<float>(len - i) / static_cast<float>(fade));
            out.l[dstStart + i] += in.l[srcStart + i] * g;
            out.r[dstStart + i] += in.r[srcStart + i] * g;
        }
    }
    return out;
}

//==============================================================================
struct Result
{
    std::string name, input;
    double best = 0.0, inputSeconds = 0.0;
    size_t outFrames = 0;
    std::string note;
};

template <typename F>
Result timeIt(const std::string& name, const std::string& inputName, double inputSeconds, int runs, F&& fn, Stereo* keep = nullptr)
{
    Result r;
    r.name = name;
    r.input = inputName;
    r.inputSeconds = inputSeconds;
    r.best = 1e9;
    for (int i = 0; i < runs; ++i)
    {
        const auto t0 = Clock::now();
        auto out = fn();
        r.best = std::min(r.best, seconds(t0));
        r.outFrames = out.size();
        if (keep != nullptr && i == 0)
            *keep = std::move(out);
    }
    return r;
}
} // namespace

int main(int argc, char** argv)
{
    juce::File input, outDir;
    double ratio = 1.2;
    int runs = 3;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--input" && i + 1 < argc)
            input = juce::File(argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            outDir = juce::File(argv[++i]);
        else if (a == "--ratio" && i + 1 < argc)
            ratio = std::atof(argv[++i]);
        else if (a == "--runs" && i + 1 < argc)
            runs = std::atoi(argv[++i]);
    }

    Stereo loop = makeLoop(4, 0), track = makeTrack(180.0);
    std::string loopName = "4-bar loop (8 s)", trackName = "3-minute track";
    if (input != juce::File())
    {
        double rate = 0.0;
        Stereo file;
        if (!readWav(input, file, rate) || std::abs(rate - sampleRate) > 1.0)
        {
            std::fprintf(stderr, "cannot read %s at 44.1 kHz\n", input.getFullPathName().toRawUTF8());
            return 2;
        }
        loop = file;
        loopName = input.getFileName().toStdString();
    }
    if (outDir != juce::File())
        outDir.createDirectory();

    std::printf("stretch ratio %.3f (output length / input length), best of %d runs, 44.1 kHz stereo\n\n", ratio, runs);

    std::vector<Result> results;
    Stereo keepRb2, keepRb3, keepSs, keepSsCheap, keepBeats;
    using RB = RubberBand::RubberBandStretcher;

    struct Case { const char* name; const Stereo* data; std::string label; double secs; bool isLoop; };
    const Case cases[] = { { "", &loop, loopName, static_cast<double>(loop.size()) / sampleRate, true },
                           { "", &track, trackName, static_cast<double>(track.size()) / sampleRate, false } };

    for (const auto& c : cases)
    {
        const auto& in = *c.data;
        results.push_back(timeIt("Rubber Band R2 (faster), offline", c.label, c.secs, runs,
                                 [&] { return rubberBand(in, ratio, RB::OptionEngineFaster); }, c.isLoop ? &keepRb2 : nullptr));
        results.push_back(timeIt("Rubber Band R3 (finer), offline", c.label, c.secs, runs,
                                 [&] { return rubberBand(in, ratio, RB::OptionEngineFiner); }, c.isLoop ? &keepRb3 : nullptr));
        results.push_back(timeIt("Signalsmith Stretch (default)", c.label, c.secs, runs,
                                 [&] { return runSignalsmith(in, ratio, false); }, c.isLoop ? &keepSs : nullptr));
        results.push_back(timeIt("Signalsmith Stretch (cheaper)", c.label, c.secs, runs,
                                 [&] { return runSignalsmith(in, ratio, true); }, c.isLoop ? &keepSsCheap : nullptr));

        std::vector<size_t> onsets;
        auto analysis = timeIt("Beats prototype: onset analysis", c.label, c.secs, runs,
                               [&]
                               {
                                   onsets = detectOnsets(in);
                                   Stereo dummy;
                                   dummy.l.resize(onsets.size());
                                   return dummy;
                               });
        analysis.note = std::to_string(onsets.size()) + " onsets";
        if (input == juce::File()) // synthetic material: a hit on every eighth note, so check against that grid
        {
            const auto grid = sampleRate * 60.0 / bpm / 2.0;
            const auto expected = static_cast<size_t>(static_cast<double>(in.size()) / grid);
            std::vector<bool> hit(expected + 1, false);
            size_t extra = 0;
            for (auto o : onsets)
            {
                const auto nearest = static_cast<size_t>(std::llround(static_cast<double>(o) / grid));
                const auto errorMs = std::abs(static_cast<double>(o) - static_cast<double>(nearest) * grid) / sampleRate * 1000.0;
                if (errorMs <= 15.0 && nearest < hit.size() && !hit[nearest])
                    hit[nearest] = true;
                else
                    ++extra;
            }
            size_t matched = 0;
            for (size_t g = 0; g < expected; ++g)
                matched += hit[g] ? 1u : 0u;
            analysis.note += " (" + std::to_string(matched) + " of " + std::to_string(expected) + " eighth-note hits found within 15 ms, "
                             + std::to_string(extra) + " extra)";
        }
        results.push_back(analysis);
        results.push_back(timeIt("Beats prototype: render (cut + place)", c.label, c.secs, runs,
                                 [&] { return renderBeats(in, onsets, ratio); }, c.isLoop ? &keepBeats : nullptr));
    }

    std::printf("%-40s %-22s %10s %12s %10s  %s\n", "method", "input", "time (s)", "x real time", "out (s)", "note");
    for (const auto& r : results)
        std::printf("%-40s %-22s %10.3f %12.1f %10.2f  %s\n", r.name.c_str(), r.input.c_str(), r.best, r.inputSeconds / r.best,
                    static_cast<double>(r.outFrames) / sampleRate, r.note.c_str());

    if (outDir != juce::File())
    {
        writeWav(outDir.getChildFile("original.wav"), loop);
        writeWav(outDir.getChildFile("rubberband_r2.wav"), keepRb2);
        writeWav(outDir.getChildFile("rubberband_r3.wav"), keepRb3);
        writeWav(outDir.getChildFile("signalsmith_default.wav"), keepSs);
        writeWav(outDir.getChildFile("signalsmith_cheaper.wav"), keepSsCheap);
        writeWav(outDir.getChildFile("beats_prototype.wav"), keepBeats);
        std::printf("\nwrote the 4-bar results to %s\n", outDir.getFullPathName().toRawUTF8());
    }
    return 0;
}
