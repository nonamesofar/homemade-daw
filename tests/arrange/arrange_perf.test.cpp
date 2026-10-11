// Task 3.5: 200 visible clips with waveforms, scroll and zoom while the playhead moves; every paint under 8 ms.
// Release builds only (the UI code is unoptimised in the debug preset). Tagged hardware: it measures wall-clock time, so
// CI leaves it out (ctest -LE hardware); run it locally with `ctest --preset windows-msvc-release -L perf`.
#include "ViewHarness.h"

#include <catch2/catch_test_macros.hpp>

using sampler::test::ViewHarness;
namespace cmd = sampler::commands;

TEST_CASE("200 clips with waveforms paint under 8 ms while scrolling and zooming", "[arrange][perf][hardware]")
{
#if JUCE_DEBUG
    SKIP("performance is measured in the release preset");
#else
    ViewHarness h("perf");
    const auto sound = h.f.addSound("hit", 0.5, "tone");
    for (int i = 0; i < 3; ++i)
        h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.f.commands->invariantChecksEnabled = false;
    for (int t = 0; t < 4; ++t)
        for (int c = 0; c < 50; ++c)
            h.f.place(sound, t, c * 2.0);
    h.sync();

    auto& lanes = h.view->lanes();
    h.view->setSize(1600, 600);
    juce::Image first(juce::Image::RGB, lanes.getWidth(), lanes.getHeight(), true);
    {
        juce::Graphics warmUp(first);
        lanes.paintEntireComponent(warmUp, false); // asks for the pictures
    }
    for (int i = 0; i < 100; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // let them fill in
    {
        juce::Graphics warmUp(first);
        for (int i = 0; i < 120; ++i) // one waveform picture is made per paint: draw until all are there
            lanes.paintEntireComponent(warmUp, false);
    }
    h.view->viewport().pixelsPerBeat = 30.0;
    juce::Image image(juce::Image::RGB, lanes.getWidth(), lanes.getHeight(), true);
    juce::Graphics g(image);

    std::vector<double> times;
    double worst = 0.0;
    int drawn = 0;
    for (int frame = 0; frame < 300; ++frame)
    {
        h.view->viewport().firstBeat = frame * 0.3;
        if (frame % 3 == 0)
            h.view->zoomAround(400.0, frame % 6 == 0 ? 1.03 : 1.0 / 1.03);
        h.view->viewport().scrollY = (frame % 20) * 2.0;
        h.view->viewChanged();
        h.f.project->transport().locateBeats(frame * 0.4); // the playhead moves
        const auto start = juce::Time::getMillisecondCounterHiRes();
        lanes.paintEntireComponent(g, false);
        h.view->tick(16.0);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
        if (elapsed > 8.0)
            std::printf("perf: frame %d took %.2f ms\n", frame, elapsed);
        times.push_back(elapsed);
        worst = std::max(worst, elapsed);
        drawn = std::max(drawn, lanes.clipsDrawnInLastPaint());
    }
    std::sort(times.begin(), times.end());
    std::printf("perf: median %.2f ms, p95 %.2f ms, worst %.2f ms, up to %d clips drawn\n", times[times.size() / 2],
                times[times.size() * 95 / 100], worst, drawn);
    CHECK(worst < 8.0);
#endif
}
