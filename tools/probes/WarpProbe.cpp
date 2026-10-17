// Warp probe (M4a task 1.1, design D2).
//
//   WarpProbe
//
//   (a) Re-pitch by speed ratio on a non-auto-tempo clip: 440 Hz at 100 BPM in a 200 BPM edit -> 880 Hz, half the
//       length, starting sample-exact.
//   (b) Tones: auto-tempo + rubberbandMelodic + LoopInfo BPM keeps the clip length in beats across a tempo change,
//       setPitchChange(7) gives about 659 Hz, two offline renders (separate engines and proxies) are identical.
//   (c) Loop ranges repeat without a gap: setLoopRange on a plain clip, setLoopRangeBeats on a Tones clip.
//   (d) After a WAV that a clip plays is rewritten, which call makes the next render hear the new content.
//
// Prints every check as PASS or FAIL; the results are recorded in docs/m0-findings.md.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <utility>
#include <vector>

#include "engine/testkit/WarpLab.h"

namespace
{
constexpr double pi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const char* what, const juce::String& detail)
{
    std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.toRawUTF8());
    if (! ok)
        ++failures;
}

/** A stereo 32-bit float cosine (a cosine so that the very first sample is not zero). */
juce::File writeTone(const juce::File& dest, double frequency, double seconds, double rate = 44100.0)
{
    dest.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = dest.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 2, 32, {}, 0));
    stream.release();
    const auto frames = static_cast<int>(std::lround(seconds * rate));
    juce::AudioBuffer<float> buffer(2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.5f * static_cast<float>(std::cos(2.0 * pi * frequency * i / rate)));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return dest;
}

struct Rendered
{
    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    int frames() const { return audio.getNumSamples(); }
    const float* left() const { return audio.getReadPointer(0); }
};

Rendered readWav(const juce::File& file)
{
    Rendered r;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr)
        return r;
    r.rate = reader->sampleRate;
    r.audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&r.audio, 0, r.audio.getNumSamples(), 0, true, true);
    return r;
}

/** Frequency from upward zero crossings between two times. */
double frequencyBetween(const Rendered& r, double fromSeconds, double toSeconds)
{
    const auto a = static_cast<int>(fromSeconds * r.rate);
    const auto b = std::min(r.frames(), static_cast<int>(toSeconds * r.rate));
    const auto* x = r.left();
    int first = -1, last = -1, count = 0;
    for (int i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const auto frac = -x[i - 1] / (x[i] - x[i - 1]);
            const auto pos = i - 1 + frac;
            if (first < 0)
                first = static_cast<int>(pos * 1000);
            last = static_cast<int>(pos * 1000);
            ++count;
        }
    if (count < 3)
        return 0.0;
    return (count - 1) * r.rate / ((last - first) / 1000.0);
}

int firstAbove(const Rendered& r, float threshold)
{
    for (int i = 0; i < r.frames(); ++i)
        if (std::abs(r.left()[i]) > threshold)
            return i;
    return -1;
}

int lastAbove(const Rendered& r, float threshold)
{
    for (int i = r.frames() - 1; i >= 0; --i)
        if (std::abs(r.left()[i]) > threshold)
            return i;
    return -1;
}

/** Smallest peak of a sliding window of `windowSeconds` between two times (a gap shows as a small value). */
double minWindowPeak(const Rendered& r, double fromSeconds, double toSeconds, double windowSeconds, double* atSeconds = nullptr)
{
    const auto w = static_cast<int>(windowSeconds * r.rate);
    const auto a = static_cast<int>(fromSeconds * r.rate);
    const auto b = std::min(r.frames(), static_cast<int>(toSeconds * r.rate));
    double smallest = 1e9;
    for (int s = a; s + w <= b; s += w / 2)
    {
        double peak = 0.0;
        for (int i = s; i < s + w; ++i)
            peak = std::max(peak, static_cast<double>(std::abs(r.left()[i])));
        if (peak < smallest)
        {
            smallest = peak;
            if (atSeconds != nullptr)
                *atSeconds = s / r.rate;
        }
    }
    return smallest;
}

/** Largest jump between neighbouring samples (a splice with a phase break shows as a large value). */
double maxStep(const Rendered& r, double fromSeconds, double toSeconds, double* atSeconds = nullptr)
{
    const auto a = static_cast<int>(fromSeconds * r.rate);
    const auto b = std::min(r.frames(), static_cast<int>(toSeconds * r.rate));
    double step = 0.0;
    for (int i = a + 1; i < b; ++i)
        if (const auto d = static_cast<double>(std::abs(r.left()[i] - r.left()[i - 1])); d > step)
        {
            step = d;
            if (atSeconds != nullptr)
                *atSeconds = i / r.rate;
        }
    return step;
}

juce::File freshDir(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-warp-probe").getChildFile(name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

//==============================================================================
void probeA()
{
    std::printf("== (a) Re-pitch by speed ratio: 440 Hz at 100 BPM in a 200 BPM edit\n");
    const auto dir = freshDir("a");
    const auto tone = writeTone(dir.getChildFile("tone440.wav"), 440.0, 9.6); // 4 bars at 100 BPM

    sampler::WarpLab lab;
    lab.newEdit(dir.getChildFile("a.tracktionedit"), 200.0);
    const auto clip = lab.addClip(tone, 4.0, 4.8, 0.0); // beat 4 = 1.2 s at 200 BPM
    lab.setRepitch(clip, 2.0, 4.0, 4.8, 0.0);
    check(lab.renderToWav(dir.getChildFile("out.wav"), 8.0), "render", "ok");

    const auto out = readWav(dir.getChildFile("out.wav"));
    const auto first = firstAbove(out, 1.0e-4f);
    const auto last = lastAbove(out, 1.0e-4f);
    check(std::abs(first - 57600) <= 1, "start sample-exact", "first sample at " + juce::String(first) + ", expected 57600");
    const auto length = (last - first + 1) / out.rate;
    check(std::abs(length - 4.8) < 0.002, "half the length", juce::String(length, 4) + " s, expected 4.8 s");
    const auto f = frequencyBetween(out, 2.0, 5.0);
    check(std::abs(f - 880.0) < 1.0, "pitch 880 Hz", juce::String(f, 2) + " Hz");
}

//==============================================================================
void renderTones(const juce::File& dir, const juce::File& tone, double semitones, double tempo, const juce::File& out, double& beatsBefore,
                 double& beatsAfter, double& secondsAfter)
{
    sampler::WarpLab lab;
    lab.newEdit(dir.getChildFile("edit.tracktionedit"), tempo);
    const auto beats = 16.0;
    const auto clip = lab.addClip(tone, 0.0, beats * 60.0 / tempo, 0.0);
    lab.setTones(clip, 100.0, semitones);
    lab.setClipLengthSeconds(clip, beats * 60.0 / tempo);
    beatsBefore = lab.clipLengthBeats(clip);
    lab.renderToWav(out, beats * 60.0 / tempo + 0.5);
    lab.setTempo(100.0);
    beatsAfter = lab.clipLengthBeats(clip);
    secondsAfter = lab.clipLengthSeconds(clip);
}

void probeB()
{
    std::printf("== (b) Tones: auto-tempo + rubberbandMelodic + LoopInfo BPM, 440 Hz at 100 BPM in a 130 BPM edit\n");
    const auto dir1 = freshDir("b1");
    const auto dir2 = freshDir("b2");
    const auto dir3 = freshDir("b3");
    const auto tone = writeTone(dir1.getChildFile("tone440.wav"), 440.0, 9.6); // 4 bars at 100 BPM

    double beatsBefore = 0, beatsAfter = 0, secondsAfter = 0;
    renderTones(dir1, tone, 0.0, 130.0, dir1.getChildFile("out.wav"), beatsBefore, beatsAfter, secondsAfter);
    const auto out = readWav(dir1.getChildFile("out.wav"));
    const auto f = frequencyBetween(out, 1.0, 6.0);
    check(std::abs(f - 440.0) < 1.0, "pitch kept", juce::String(f, 2) + " Hz");
    const auto expectedLength = 16.0 * 60.0 / 130.0;
    const auto last = lastAbove(out, 0.02f) / out.rate;
    check(std::abs(last - expectedLength) < 0.05, "fills 4 bars at 130 BPM", "audio ends at " + juce::String(last, 3) + " s, expected "
                                                                                + juce::String(expectedLength, 3) + " s");
    check(std::abs(beatsBefore - 16.0) < 1.0e-6 && std::abs(beatsAfter - 16.0) < 1.0e-3, "length in beats across a tempo change",
          juce::String(beatsBefore, 4) + " beats at 130 BPM, " + juce::String(beatsAfter, 4) + " beats after 100 BPM ("
              + juce::String(secondsAfter, 3) + " s)");

    // A second engine and a second edit folder: the proxy is made again, the render must be the same.
    renderTones(dir2, tone, 0.0, 130.0, dir2.getChildFile("out.wav"), beatsBefore, beatsAfter, secondsAfter);
    const auto again = readWav(dir2.getChildFile("out.wav"));
    bool same = again.frames() == out.frames();
    double worst = 0.0;
    for (int i = 0; same && i < out.frames(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(out.left()[i] - again.left()[i])));
    check(same && worst == 0.0, "two renders identical (fresh proxy)", "frames " + juce::String(out.frames()) + "/" + juce::String(again.frames())
                                                                         + ", max difference " + juce::String(worst, 9));

    renderTones(dir3, tone, 7.0, 130.0, dir3.getChildFile("out.wav"), beatsBefore, beatsAfter, secondsAfter);
    const auto up = readWav(dir3.getChildFile("out.wav"));
    const auto fUp = frequencyBetween(up, 1.0, 6.0);
    check(std::abs(fUp - 659.26) < 2.0, "setPitchChange(7) gives ~659 Hz", juce::String(fUp, 2) + " Hz");
}

//==============================================================================
/** Checks a rendered loop of a 440 Hz tone: no gap and no jump between `fromSeconds` and `toSeconds`. */
void checkSeamless(const Rendered& out, const char* label, double fromSeconds, double toSeconds, double relativeTolerance, bool jumpsMustPass)
{
    double reference = 0.0;
    for (int i = static_cast<int>(0.3 * out.rate); i < static_cast<int>(0.5 * out.rate); ++i)
        reference = std::max(reference, static_cast<double>(std::abs(out.left()[i])));
    double gapAt = 0.0, jumpAt = 0.0;
    const auto smallest = minWindowPeak(out, fromSeconds, toSeconds, 0.01, &gapAt);
    check(smallest > reference * (1.0 - relativeTolerance), (juce::String(label) + ": no gap at the repeats").toRawUTF8(),
          "smallest 10 ms peak " + juce::String(smallest, 3) + " of " + juce::String(reference, 3) + " at " + juce::String(gapAt, 3) + " s");
    for (int i = static_cast<int>(fromSeconds * out.rate) + 1, shown = 0; i < static_cast<int>(toSeconds * out.rate) && shown < 8; ++i)
        if (std::abs(out.left()[i] - out.left()[i - 1]) > reference * 2.0 * pi * 440.0 / out.rate * 3.0)
        {
            std::printf("  [INFO] %s: jump of %.3f at %.4f s (%.3f -> %.3f)\n", label, out.left()[i] - out.left()[i - 1], i / out.rate,
                        out.left()[i - 1], out.left()[i]);
            ++shown;
        }
    const auto step = maxStep(out, fromSeconds, toSeconds, &jumpAt);
    const auto expectedStep = reference * 2.0 * pi * 440.0 / out.rate;
    const auto jumpText = "largest step " + juce::String(step, 4) + ", a clean tone has " + juce::String(expectedStep, 4) + ", at " + juce::String(jumpAt, 3) + " s";
    if (jumpsMustPass)
        check(step < expectedStep * (1.0 + relativeTolerance), (juce::String(label) + ": no jump at the repeats").toRawUTF8(), jumpText);
    else // Rubber Band output is not cycle-exact at the loop point: a one-sample phase step is recorded, not failed
        std::printf("  [INFO] %s: %s\n", label, jumpText.toRawUTF8());
}

void probeC()
{
    std::printf("== (c) loop ranges repeat without a gap (2 s of 440 Hz, 880 whole cycles, looped over 6 s)\n");
    {
        const auto dir = freshDir("c1");
        const auto tone = writeTone(dir.getChildFile("tone440.wav"), 440.0, 2.0);
        sampler::WarpLab lab;
        lab.newEdit(dir.getChildFile("c1.tracktionedit"), 120.0);
        const auto clip = lab.addClip(tone, 0.0, 2.0, 0.0);
        lab.setLoopSeconds(clip, 0.0, 2.0);
        lab.setClipLengthSeconds(clip, 6.0);
        check(lab.clipIsLooping(clip), "plain clip: isLooping after setLoopRange", lab.clipIsLooping(clip) ? "yes" : "no");
        check(lab.renderToWav(dir.getChildFile("out.wav"), 7.0), "plain clip: render", "ok");
        const auto out = readWav(dir.getChildFile("out.wav"));
        const auto last = lastAbove(out, 0.01f) / out.rate;
        check(std::abs(last - 6.0) < 0.01, "plain clip: plays 6 s", "audio ends at " + juce::String(last, 3) + " s");
        checkSeamless(out, "plain clip", 0.05, 5.99, 0.1, true);
    }
    {
        const auto dir = freshDir("c2");
        const auto tone = writeTone(dir.getChildFile("tone440.wav"), 440.0, 2.0);
        sampler::WarpLab lab;
        lab.newEdit(dir.getChildFile("c2.tracktionedit"), 120.0);
        const auto clip = lab.addClip(tone, 0.0, 2.0, 0.0);
        lab.setTones(clip, 120.0, 0.0);
        lab.setLoopBeats(clip, 0.0, 4.0);
        lab.setClipLengthSeconds(clip, 6.0);
        const auto looping = lab.clipIsLooping(clip);
        check(looping, "tones clip: isLooping after setLoopRangeBeats", looping ? "yes" : "no");
        check(lab.renderToWav(dir.getChildFile("out.wav"), 7.0), "tones clip: render", "ok");
        const auto out = readWav(dir.getChildFile("out.wav"));
        const auto last = lastAbove(out, 0.01f) / out.rate;
        check(std::abs(last - 6.0) < 0.05, "tones clip: plays 6 s (12 beats)", "audio ends at " + juce::String(last, 3) + " s, length "
                                                                                + juce::String(lab.clipLengthBeats(clip), 3) + " beats");
        checkSeamless(out, "tones clip", 0.2, 5.8, 0.2, false);
        // Tempo change: the looped clip keeps its 12 beats.
        lab.setTempo(100.0);
        check(std::abs(lab.clipLengthBeats(clip) - 12.0) < 1.0e-3, "tones clip: loop keeps 12 beats at 100 BPM",
              juce::String(lab.clipLengthBeats(clip), 4) + " beats");
    }
}

//==============================================================================
/** Writes a tone next to `file` and moves it over `file` with one rename (what the Beats renderer does). */
bool writeThenRename(const juce::File& file, double frequency, double seconds)
{
    const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
    writeTone(temp, frequency, seconds);
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(temp.getFullPathName().toWideCharPointer()),
                            std::filesystem::path(file.getFullPathName().toWideCharPointer()), error);
    return ! error;
}

void probeD()
{
    std::printf("== (d) a file appears or changes under a clip\n");
    const auto dir = freshDir("d");
    const auto measure = [&](sampler::WarpLab& lab)
    {
        lab.renderToWav(dir.getChildFile("out.wav"), 2.0);
        const auto out = readWav(dir.getChildFile("out.wav"));
        return std::make_pair(frequencyBetween(out, 0.2, 1.8), lastAbove(out, 1.0e-4f));
    };

    // The case the Beats clips are in: the clip points at a file that does not exist yet and the render makes it.
    for (const bool notifyClips : {false, true})
    {
        const auto file = dir.getChildFile(notifyClips ? "late-b.wav" : "late-a.wav");
        sampler::WarpLab lab;
        lab.newEdit(dir.getChildFile(notifyClips ? "d2.tracktionedit" : "d1.tracktionedit"), 120.0);
        lab.addClip(file, 0.0, 2.0, 0.0);
        // (an offline render of a clip whose file is missing never ends, so nothing is rendered before the file exists)
        check(writeThenRename(file, 440.0, 2.0), "file made by temp + rename", file.getFileName());
        const auto stale = measure(lab);
        std::printf("  [INFO] file now exists, no reload call: %.1f Hz (last sample %d)\n", stale.first, stale.second);
        lab.reloadFile(file, notifyClips);
        const auto heard = measure(lab);
        check(std::abs(heard.first - 440.0) < 1.0, notifyClips ? "forceFileUpdate + sourceMediaChanged is heard in the next render"
                                                               : "forceFileUpdate is heard in the next render",
              juce::String(heard.first, 1) + " Hz");
    }

    // A file that was played and is replaced by another file of the same name (a re-render that keeps the name).
    {
        const auto file = dir.getChildFile("same-name.wav");
        writeTone(file, 440.0, 2.0);
        sampler::WarpLab lab;
        lab.newEdit(dir.getChildFile("d3.tracktionedit"), 120.0);
        lab.addClip(file, 0.0, 2.0, 0.0);
        check(std::abs(measure(lab).first - 440.0) < 1.0, "same name: first render", "440 Hz");
        const auto renamed = writeThenRename(file, 880.0, 2.0);
        std::printf("  [INFO] replacing a played file by rename: %s\n", renamed ? "worked" : "refused by Windows");
        if (renamed)
        {
            std::printf("  [INFO] same name, no reload call: %.1f Hz\n", measure(lab).first);
            lab.reloadFile(file, true);
            const auto heard = measure(lab);
            check(std::abs(heard.first - 880.0) < 1.0, "same name: reload makes the new content heard", juce::String(heard.first, 1) + " Hz");
        }
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    probeA();
    probeB();
    probeC();
    probeD();
    std::printf("\n%s (%d failed checks)\n", failures == 0 ? "ALL PASS" : "SOME CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
