#include <catch2/catch_test_macros.hpp>

#include "AnalysisTestSupport.h"
#include "analysis/OnsetDetector.h"

#include <chrono>
#include <cmath>

using namespace sampler;
using namespace sampler::analysis;

TEST_CASE("the 16-hit break gives 16 onsets, each 0 to 5 ms before its true start (plus the silent first sample)", "[analysis][onsets]")
{
    const auto fixture = test::loadAnalysisFixture("break-90");
    REQUIRE(fixture.mono.size() == 470400);
    const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);

    REQUIRE(result.onsets.size() == fixture.onsets.size());
    const auto tolerance = static_cast<juce::int64>(0.005 * fixture.sampleRate);
    for (size_t i = 0; i < fixture.onsets.size(); ++i)
    {
        INFO("hit " << i << " truth " << fixture.onsets[i] << " found " << result.onsets[i].frame);
        const auto early = fixture.onsets[i] - result.onsets[i].frame;
        CHECK(early >= -2); // a hit's first sample is silent, so the first non-zero sample may be one or two later

        CHECK(early <= tolerance);
    }
}

TEST_CASE("every analysis fixture has all hits found within 10 ms", "[analysis][onsets]")
{
    for (const auto& name : test::analysisFixtureNames())
    {
        INFO(name);
        const auto fixture = test::loadAnalysisFixture(name);
        const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
        const auto tolerance = static_cast<juce::int64>(0.010 * fixture.sampleRate);
        int found = 0;
        for (const auto truth : fixture.onsets)
            for (const auto& onset : result.onsets)
                if (std::llabs(onset.frame - truth) <= tolerance)
                {
                    ++found;
                    break;
                }
        CHECK(found == static_cast<int>(fixture.onsets.size()));
        CHECK(result.onsets.size() <= fixture.onsets.size() + 1);
    }
}

TEST_CASE("silence gives no onsets", "[analysis][onsets]")
{
    std::vector<float> silence(44100 * 3, 0.0f);
    const auto result = detectOnsets(silence.data(), static_cast<juce::int64>(silence.size()), 44100.0);
    CHECK(result.onsets.empty());
}

TEST_CASE("a sound shorter than one analysis window gives no onsets and does not crash", "[analysis][onsets]")
{
    std::vector<float> tiny(300, 0.5f);
    CHECK(detectOnsets(tiny.data(), static_cast<juce::int64>(tiny.size()), 44100.0).onsets.empty());
    CHECK(detectOnsets(tiny.data(), 0, 44100.0).onsets.empty());
}

TEST_CASE("onsets are at least 30 ms apart and strengths lie in 0..1", "[analysis][onsets]")
{
    const auto fixture = test::loadAnalysisFixture("pattern-120-7s");
    const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    const auto gap = static_cast<juce::int64>(minOnsetGapSeconds * fixture.sampleRate);
    float strongest = 0.0f;
    for (size_t i = 0; i < result.onsets.size(); ++i)
    {
        CHECK(result.onsets[i].strength >= 0.0f);
        CHECK(result.onsets[i].strength <= 1.0f);
        strongest = std::max(strongest, result.onsets[i].strength);
        if (i > 0)
            CHECK(result.onsets[i].frame - result.onsets[i - 1].frame >= gap);
    }
    CHECK(strongest == 1.0f);
}

TEST_CASE("a higher sensitivity never gives fewer onsets", "[analysis][onsets]")
{
    const auto fixture = test::loadAnalysisFixture("pattern-120-7s");
    const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    size_t previous = 0;
    for (int sensitivity = 0; sensitivity <= 100; sensitivity += 5)
    {
        const auto selected = selectTransients(result.onsets, static_cast<float>(sensitivity));
        INFO("sensitivity " << sensitivity);
        CHECK(selected.size() >= previous);
        previous = selected.size();
    }
    CHECK(previous == result.onsets.size());
    CHECK(selectTransients(result.onsets, 0.0f).size() <= 1);
    CHECK(transientThreshold(100.0f) == 0.0f);
}

TEST_CASE("the default sensitivity keeps every hit of the break", "[analysis][onsets]")
{
    const auto fixture = test::loadAnalysisFixture("break-90");
    const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    CHECK(selectTransients(result.onsets, defaultSensitivity).size() == 16);
}

TEST_CASE("analysing 10 seconds takes under a second", "[analysis][onsets][perf]")
{
    const auto fixture = test::loadAnalysisFixture("break-90"); // 10.67 s
    const auto start = std::chrono::steady_clock::now();
    const auto result = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    INFO("elapsed " << elapsed << " s");
    CHECK(result.onsets.size() == 16);
    CHECK(elapsed < 1.0);
}

TEST_CASE("a weak ghost note shortly before a strong hit does not replace it", "[analysis][onsets]")
{
    constexpr double rate = 44100.0;
    std::vector<float> mono(static_cast<size_t>(rate * 3), 0.0f);
    juce::uint32 seed = 12345;
    const auto noise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24) * 2.0f - 1.0f;
    };
    // A quiet click at 1.000 s, then a loud hit with a 15 ms attack starting at 1.020 s.
    for (int i = 0; i < static_cast<int>(0.010 * rate); ++i)
        mono[static_cast<size_t>(rate + i)] += 0.05f * noise() * std::exp(-i / (0.003f * static_cast<float>(rate)));
    const int attack = static_cast<int>(0.015 * rate);
    for (int i = 0; i < static_cast<int>(0.4 * rate); ++i)
    {
        const float env = i < attack ? static_cast<float>(i) / static_cast<float>(attack) : std::exp(-(i - attack) / (0.08f * static_cast<float>(rate)));
        mono[static_cast<size_t>(1.020 * rate) + static_cast<size_t>(i)] += 0.9f * env * noise();
    }
    const auto result = detectOnsets(mono.data(), static_cast<juce::int64>(mono.size()), rate);

    int near = 0;
    float strength = 0.0f;
    for (const auto& onset : result.onsets)
        if (onset.frame > static_cast<juce::int64>(0.99 * rate) && onset.frame < static_cast<juce::int64>(1.07 * rate))
        {
            ++near;
            strength = std::max(strength, onset.strength);
        }
    CHECK(near == 1);
    CHECK(strength > 0.9f);
}
