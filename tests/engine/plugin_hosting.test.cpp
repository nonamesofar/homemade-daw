// M0 plugin-hosting checks against the JUCE test plugin (4 stereo outputs, deterministic pattern):
// editor, transport sync, 4 outputs on 4 tracks through a rack, "runs once", state round trip, offline render.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

#include "TestAudio.h"
#include "engine/PluginLab.h"
#include "platform/ICaptureSource.h"
#include "platform/NativeWindowTools.h"

namespace
{
const juce::File pluginFile{TEST_PLUGIN_VST3_PATH};

constexpr double bpm = 120.0;
constexpr double beatSeconds = 60.0 / bpm;
constexpr double burstSeconds = 0.1;
constexpr int sixteenBars = 16 * 4;
constexpr double sixteenBarsSeconds = sixteenBars * beatSeconds; // 32 s

/** Stereo float samples of a WAV. */
struct Samples
{
    juce::AudioBuffer<float> data;
    double sampleRate = 0.0;
    int frames() const { return data.getNumSamples(); }
    double seconds() const { return sampleRate > 0.0 ? frames() / sampleRate : 0.0; }
};

Samples readWav(const juce::File& file)
{
    Samples s;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr)
        return s;
    s.sampleRate = reader->sampleRate;
    s.data.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&s.data, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    return s;
}

struct BeatReport
{
    std::vector<float> burstPeak;      // peak over the 100 ms burst window of each beat
    std::vector<float> gapPeak;        // peak over the rest of the beat
    std::vector<double> onsetErrorMs;  // first sample above threshold, relative to the beat start (NaN if none)
};

BeatReport analyseBeats(const Samples& s, int numBeats, float onsetThreshold = 0.02f)
{
    BeatReport r;
    for (int b = 0; b < numBeats; ++b)
    {
        const auto start = static_cast<int>(std::lround(b * beatSeconds * s.sampleRate));
        const auto burstEnd = static_cast<int>(std::lround((b * beatSeconds + burstSeconds) * s.sampleRate));
        const auto end = static_cast<int>(std::lround((b + 1) * beatSeconds * s.sampleRate));
        float burst = 0.0f, gap = 0.0f;
        double onset = std::nan("");
        for (int i = start; i < std::min(end, s.frames()); ++i)
        {
            float v = 0.0f;
            for (int ch = 0; ch < s.data.getNumChannels(); ++ch)
                v = std::max(v, std::abs(s.data.getSample(ch, i)));
            if (i < burstEnd)
            {
                burst = std::max(burst, v);
                if (std::isnan(onset) && v > onsetThreshold)
                    onset = (i - b * beatSeconds * s.sampleRate) / s.sampleRate * 1000.0;
            }
            else
                gap = std::max(gap, v);
        }
        r.burstPeak.push_back(burst);
        r.gapPeak.push_back(gap);
        r.onsetErrorMs.push_back(onset);
    }
    return r;
}

/** Checks that bursts are on beats where (beat mod 4 == output) only, on time, and silence elsewhere. */
void checkPattern(const BeatReport& r, int output)
{
    int own = 0;
    double worstOnset = 0.0;
    for (size_t b = 0; b < r.burstPeak.size(); ++b)
    {
        INFO("beat " << b << " output " << output);
        CHECK(r.gapPeak[b] < 1e-4f);
        if (static_cast<int>(b % 4) == output)
        {
            ++own;
            CHECK(r.burstPeak[b] > 0.3f);
            REQUIRE_FALSE(std::isnan(r.onsetErrorMs[b]));
            worstOnset = std::max(worstOnset, std::abs(r.onsetErrorMs[b]));
        }
        else
        {
            CHECK(r.burstPeak[b] < 1e-4f);
        }
    }
    CHECK(own == static_cast<int>(r.burstPeak.size()) / 4);
    std::printf("output %d: %d bursts, worst onset error %.4f ms\n", output, own, worstOnset);
    INFO("worst onset error " << worstOnset << " ms");
    CHECK(worstOnset < 1.0);
}

struct Fixture
{
    explicit Fixture(const char* name) : dir(sampler::test::tempDir(name))
    {
        dir.deleteRecursively();
        dir.createDirectory();
    }

    juce::File dir;
};
} // namespace

TEST_CASE("test plugin loads with four stereo outputs", "[engine][plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f("plugin-load");
    sampler::PluginLab lab;
    const auto result = lab.newEdit(f.dir.getChildFile("p.tracktionedit"), pluginFile, sampler::PluginLab::Layout::direct, 1);
    INFO(result.getErrorMessage());
    REQUIRE(result.wasOk());
    CHECK(lab.pluginName() == "Sampler Test Plugin");
    CHECK(lab.pluginOutputChannels() == 8);
    CHECK(lab.numPluginInstances() == 1);
}

TEST_CASE("plugin editor opens, renders and answers a mouse click", "[engine][plugin][gui]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f("plugin-editor");
    sampler::PluginLab lab;
    REQUIRE(lab.newEdit(f.dir.getChildFile("p.tracktionedit"), pluginFile, sampler::PluginLab::Layout::direct, 1).wasOk());

    auto editor = lab.createEditor();
    REQUIRE(editor != nullptr);

    juce::DocumentWindow window("Test plugin", juce::Colours::black, juce::DocumentWindow::closeButton, true);
    window.setUsingNativeTitleBar(false);
    window.setContentNonOwned(editor.get(), true);
    window.setTopLeftPosition(80, 80);
    window.setVisible(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(800);

    CHECK(editor->getWidth() == 360);
    CHECK(editor->getHeight() == 200);

    auto* peer = window.getPeer();
    REQUIRE(peer != nullptr);
    auto* handle = peer->getNativeHandle();

    const auto before = sampler::platform::NativeWindowTools::snapshot(handle);
    REQUIRE(before.isValid());
    {
        auto out = f.dir.getChildFile("editor-before.png").createOutputStream();
        juce::PNGImageFormat().writeImageToStream(before, *out);
    }
    // Not a blank view: the editor paints a dark background, text and controls.
    int distinct = 0;
    juce::Colour first = before.getPixelAt(1, 1);
    for (int y = 0; y < before.getHeight(); y += 4)
        for (int x = 0; x < before.getWidth(); x += 4)
            if (before.getPixelAt(x, y) != first)
                ++distinct;
    CHECK(distinct > 100);

    // Click the "Waveform" button (10..170 x 86..114 in the editor).
    REQUIRE(lab.parameter("Waveform") == Catch::Approx(0.0f));
    REQUIRE(sampler::platform::NativeWindowTools::postClick(handle, window.getLocalPoint(editor.get(), juce::Point<int>(60, 100))));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

    INFO("waveform parameter after click: " << lab.parameter("Waveform"));
    CHECK(lab.parameter("Waveform") == Catch::Approx(1.0f / 3.0f).margin(0.01));

    const auto after = sampler::platform::NativeWindowTools::snapshot(handle);
    if (after.isValid())
    {
        auto out = f.dir.getChildFile("editor-after.png").createOutputStream();
        juce::PNGImageFormat().writeImageToStream(after, *out);
    }
    std::printf("editor snapshots in %s\n", f.dir.getFullPathName().toRawUTF8());

    window.clearContentComponent();
    editor.reset();
}

TEST_CASE("plugin follows the host transport: 16 bars, one burst per beat, on time", "[engine][plugin][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f("plugin-sync");
    sampler::PluginLab lab;
    REQUIRE(lab.newEdit(f.dir.getChildFile("p.tracktionedit"), pluginFile, sampler::PluginLab::Layout::direct, 1).wasOk());

    const auto wav = f.dir.getChildFile("direct.wav");
    const auto renderStart = juce::Time::getMillisecondCounterHiRes();
    REQUIRE(lab.renderToWav(wav, sixteenBarsSeconds));
    std::printf("offline render of 16 bars with the plugin took %.2f s\n", (juce::Time::getMillisecondCounterHiRes() - renderStart) / 1000.0);
    const auto s = readWav(wav);
    REQUIRE(s.frames() > 0);
    CHECK(s.seconds() == Catch::Approx(sixteenBarsSeconds).margin(0.05));

    // Track 0 plays output pair 1: a burst on beats 0, 4, 8 ... and nothing else.
    checkPattern(analyseBeats(s, sixteenBars), 0);
}

TEST_CASE("rack routes four plugin outputs to four tracks, each only its own", "[engine][plugin][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f("plugin-rack");
    sampler::PluginLab lab;
    REQUIRE(lab.newEdit(f.dir.getChildFile("p.tracktionedit"), pluginFile, sampler::PluginLab::Layout::rack, 4).wasOk());
    REQUIRE(lab.numTracks() == 4);
    CHECK(lab.numPluginInstances() == 1);

    for (int track = 0; track < 4; ++track)
    {
        INFO("track " << track);
        const auto wav = f.dir.getChildFile("stem" + juce::String(track) + ".wav");
        REQUIRE(lab.renderToWav(wav, sixteenBarsSeconds, track));
        const auto s = readWav(wav);
        REQUIRE(s.frames() > 0);
        checkPattern(analyseBeats(s, sixteenBars), track);
    }
}

TEST_CASE("rack with four output tracks runs the plugin once per block", "[engine][plugin][render]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f("plugin-once");

    constexpr int blockSize = 512;
    const auto expectedBlocks = static_cast<juce::int64>(std::ceil(sixteenBarsSeconds * 44100.0 / blockSize));

    juce::int64 directCount = 0, rackAllCount = 0, rackOneTrackCount = 0;
    {
        sampler::PluginLab lab;
        REQUIRE(lab.newEdit(f.dir.getChildFile("direct.tracktionedit"), pluginFile, sampler::PluginLab::Layout::direct, 1).wasOk());
        lab.resetProcessBlockCount();
        REQUIRE(lab.renderToWav(f.dir.getChildFile("direct.wav"), sixteenBarsSeconds, -1, 44100.0, blockSize));
        directCount = lab.processBlockCount();
    }
    {
        sampler::PluginLab lab;
        REQUIRE(lab.newEdit(f.dir.getChildFile("rack.tracktionedit"), pluginFile, sampler::PluginLab::Layout::rack, 4).wasOk());
        lab.resetProcessBlockCount();
        REQUIRE(lab.renderToWav(f.dir.getChildFile("rack-all.wav"), sixteenBarsSeconds, -1, 44100.0, blockSize));
        rackAllCount = lab.processBlockCount();

        lab.resetProcessBlockCount();
        REQUIRE(lab.renderToWav(f.dir.getChildFile("rack-one.wav"), sixteenBarsSeconds, 2, 44100.0, blockSize));
        rackOneTrackCount = lab.processBlockCount();
    }

    std::printf("processBlock calls for 16 bars at block %d: expected ~%lld, direct %lld, rack with 4 tracks %lld, rack one track %lld\n",
                blockSize, static_cast<long long>(expectedBlocks), static_cast<long long>(directCount),
                static_cast<long long>(rackAllCount), static_cast<long long>(rackOneTrackCount));

    REQUIRE(directCount > 0);
    CHECK(directCount >= expectedBlocks);
    // Four tracks share one plugin: the count matches a single instance, not four times as many.
    CHECK(rackAllCount <= directCount * 105 / 100);
    CHECK(rackAllCount >= directCount * 95 / 100);
    CHECK(rackOneTrackCount <= directCount * 105 / 100);
}

TEST_CASE("plugin state survives save and reopen, directly and inside a rack", "[engine][plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (auto layout : {sampler::PluginLab::Layout::direct, sampler::PluginLab::Layout::rack})
    {
        const bool rack = layout == sampler::PluginLab::Layout::rack;
        INFO(std::string(rack ? "rack" : "direct"));
        Fixture f(rack ? "plugin-state-rack" : "plugin-state-direct");
        const auto editFile = f.dir.getChildFile("p.tracktionedit");

        {
            sampler::PluginLab lab;
            REQUIRE(lab.newEdit(editFile, pluginFile, layout, rack ? 4 : 1).wasOk());
            REQUIRE(lab.parameter("Tune") >= 0.0f); // the parameter exists
            REQUIRE(lab.setParameter("Tune", 0.9f));
            REQUIRE(lab.setParameter("Level", 0.25f));
            REQUIRE(lab.setParameter("Waveform", 2.0f / 3.0f)); // square
            REQUIRE(lab.save());
            REQUIRE(lab.renderToWav(f.dir.getChildFile("before.wav"), 4.0, 0));
        }
        {
            sampler::PluginLab lab;
            REQUIRE(lab.open(editFile, pluginFile).wasOk());
            CHECK(lab.parameter("Tune") == Catch::Approx(0.9f).margin(0.01));
            CHECK(lab.parameter("Level") == Catch::Approx(0.25f).margin(0.01));
            CHECK(lab.parameter("Waveform") == Catch::Approx(2.0f / 3.0f).margin(0.01));
            REQUIRE(lab.renderToWav(f.dir.getChildFile("after.wav"), 4.0, 0));
        }

        const auto before = readWav(f.dir.getChildFile("before.wav"));
        const auto after = readWav(f.dir.getChildFile("after.wav"));
        REQUIRE(before.frames() > 0);
        REQUIRE(before.frames() == after.frames());
        float worst = 0.0f;
        for (int ch = 0; ch < before.data.getNumChannels(); ++ch)
            for (int i = 0; i < before.frames(); ++i)
                worst = std::max(worst, std::abs(before.data.getSample(ch, i) - after.data.getSample(ch, i)));
        INFO("largest sample difference " << worst);
        CHECK(worst < 1e-6f);
        CHECK(analyseBeats(before, 4).burstPeak[0] > 0.1f); // the changed state still makes sound
    }
}

TEST_CASE("real-time playback: transport sync, independent meters and timing against the offline render",
          "[engine][plugin][realtime][hardware]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Fixture f("plugin-realtime");
    sampler::PluginLab lab;
    REQUIRE(lab.newEdit(f.dir.getChildFile("p.tracktionedit"), pluginFile, sampler::PluginLab::Layout::rack, 4).wasOk());

    if (!lab.waitForOutputDevice(8000))
        SKIP("no audio output device");
    // Let the engine finish its startup device scan before playing (it stops itself otherwise).
    juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);
    std::printf("real-time device: %s\n", lab.describeAudioSetup().toRawUTF8());

    lab.setMasterGainDb(-12.0f);
    auto capture = sampler::platform::createCaptureSource();
    const auto captureFile = f.dir.getChildFile("capture.wav");
    sampler::platform::CaptureOptions options;
    options.excludeOwnProcess = false; // endpoint loopback: this process is what we want to hear
    if (const auto started = capture->start(captureFile, options); started.failed())
        SKIP("cannot capture: " << started.getErrorMessage());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

    std::printf("capture started, playing\n");
    const auto wallStart = juce::Time::getMillisecondCounterHiRes();
    lab.play();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    REQUIRE(lab.isPlaying());

    // Poll the four track meters while the 16 bars play.
    std::array<int, 4> hotPolls{};
    std::array<float, 4> maxPeak{};
    int polls = 0, overlapPolls = 0, hotAny = 0;
    const auto playEnd = wallStart + (sixteenBarsSeconds + 0.5) * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < playEnd)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        int hot = 0;
        for (int t = 0; t < 4; ++t)
        {
            const auto peak = lab.takeTrackPeak(t);
            maxPeak[static_cast<size_t>(t)] = std::max(maxPeak[static_cast<size_t>(t)], peak);
            if (peak > 0.05f)
            {
                ++hotPolls[static_cast<size_t>(t)];
                ++hot;
            }
        }
        ++polls;
        hotAny += hot > 0 ? 1 : 0;
        overlapPolls += hot > 1 ? 1 : 0;
    }
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - wallStart) / 1000.0;
    const auto position = lab.positionSeconds();
    lab.stop();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    REQUIRE(capture->stop().wasOk());

    std::printf("meters: polls=%d hot per track = %d %d %d %d, overlap polls %d, max peaks %.3f %.3f %.3f %.3f\n", polls,
                hotPolls[0], hotPolls[1], hotPolls[2], hotPolls[3], overlapPolls, static_cast<double>(maxPeak[0]),
                static_cast<double>(maxPeak[1]), static_cast<double>(maxPeak[2]), static_cast<double>(maxPeak[3]));
    std::printf("transport position %.3f s after %.3f s of wall time\n", position, elapsed);

    // The transport ran in real time, so the 16-bar pattern (a burst every beat) kept the host's pace.
    CHECK(position == Catch::Approx(elapsed - 0.3).margin(0.5));

    // Each track's meter sees its own bursts.
    for (int t = 0; t < 4; ++t)
    {
        INFO("track " << t);
        CHECK(maxPeak[static_cast<size_t>(t)] > 0.2f);
        CHECK(hotPolls[static_cast<size_t>(t)] > 0);
    }
    CHECK(overlapPolls * 10 <= std::max(hotAny, 1)); // meters of different tracks are rarely hot together

    // Timing: onsets in the real-time capture sit on a 0.5 s grid, like the offline render.
    const auto cap = readWav(captureFile);
    REQUIRE(cap.frames() > 0);
    float captured = 0.0f;
    for (int ch = 0; ch < cap.data.getNumChannels(); ++ch)
        captured = std::max(captured, cap.data.getMagnitude(ch, 0, cap.frames()));
    std::printf("capture: %.2f s, peak %.5f, mode %s\n", cap.seconds(), static_cast<double>(captured),
                capture->stats().mode.toRawUTF8());
    if (captured < 1e-4f)
        SKIP("the output device played silence (system volume at zero?)");

    // Burst starts: a sample above 25% of the capture peak after at least 0.3 s of quiet.
    const auto threshold = captured * 0.25f;
    std::vector<double> onsets;
    int lastLoud = -1000000;
    for (int i = 0; i < cap.frames(); ++i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < cap.data.getNumChannels(); ++ch)
            v = std::max(v, std::abs(cap.data.getSample(ch, i)));
        if (v > threshold)
        {
            if (i - lastLoud > static_cast<int>(0.3 * cap.sampleRate))
                onsets.push_back(i / cap.sampleRate);
            lastLoud = i;
        }
    }
    std::printf("capture onsets found: %zu\n", onsets.size());
    REQUIRE(onsets.size() >= static_cast<size_t>(sixteenBars - 2));

    // Least-squares fit of onset = offset + beat * period; report the period and the worst deviation.
    const auto n = static_cast<double>(onsets.size());
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t b = 0; b < onsets.size(); ++b)
    {
        sx += static_cast<double>(b);
        sy += onsets[b];
        sxx += static_cast<double>(b * b);
        sxy += static_cast<double>(b) * onsets[b];
    }
    const auto period = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    const auto offset = (sy - period * sx) / n;
    double worst = 0.0;
    for (size_t b = 0; b < onsets.size(); ++b)
        worst = std::max(worst, std::abs(onsets[b] - (offset + static_cast<double>(b) * period)));
    std::printf("real-time capture: beat period %.4f s (offline grid %.4f s), worst onset deviation from a straight grid %.2f ms\n",
                period, beatSeconds, worst * 1000.0);

    CHECK(period == Catch::Approx(beatSeconds).margin(0.002));
    CHECK(worst < 0.010);

    std::printf("checks done, closing\n");
    capture.reset();
    std::printf("capture closed\n");
    lab.closeEdit();
    std::printf("edit closed\n");
}
