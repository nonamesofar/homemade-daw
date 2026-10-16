#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AnalysisTestSupport.h"
#include "analysis/TempoEstimator.h"

#include <cmath>
#include <cstdio>

using namespace sampler;
using namespace sampler::analysis;

namespace
{
TempoResult analyse(const test::AnalysisFixtureData& fixture, OnsetAnalysis* keep = nullptr)
{
    auto onsets = detectOnsets(fixture.mono.data(), static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    auto result = estimateTempo(onsets.onsets, static_cast<juce::int64>(fixture.mono.size()), fixture.sampleRate);
    if (keep != nullptr)
        *keep = std::move(onsets);
    return result;
}
} // namespace

TEST_CASE("the 4-bar 90 BPM loop gives 90 BPM", "[analysis][tempo]")
{
    const auto result = analyse(test::loadAnalysisFixture("break-90"));
    REQUIRE(result.valid());
    CHECK(result.bpm() == Catch::Approx(90.0).margin(0.5));
    CHECK(result.candidates.size() <= 3);
}

TEST_CASE("the 7 s pattern without a bar-exact length gives 120 BPM within 1", "[analysis][tempo]")
{
    const auto result = analyse(test::loadAnalysisFixture("pattern-120-7s"));
    REQUIRE(result.valid());
    CHECK(result.bpm() == Catch::Approx(120.0).margin(1.0));
}

TEST_CASE("the 48 kHz 2-bar loop gives 120 BPM", "[analysis][tempo]")
{
    const auto result = analyse(test::loadAnalysisFixture("loop-120-48k"));
    REQUIRE(result.valid());
    CHECK(result.bpm() == Catch::Approx(120.0).margin(1.0));
}

TEST_CASE("the first beat lands on the kick after 100 ms of silence", "[analysis][tempo]")
{
    const auto fixture = test::loadAnalysisFixture("break-90");
    const auto result = analyse(fixture);
    REQUIRE(result.valid());
    const auto tolerance = static_cast<juce::int64>(0.005 * fixture.sampleRate);
    CHECK(std::llabs(result.firstBeatFrame - fixture.firstBeatFrame) <= tolerance);
}

TEST_CASE("nothing to go on gives no candidates", "[analysis][tempo]")
{
    CHECK_FALSE(estimateTempo({}, 0, 44100.0).valid());
    // No hits: a sound has some bar count at some tempo whatever its length, so a length alone proves nothing.
    CHECK_FALSE(estimateTempo({}, 44100 * 3 + 123, 44100.0).valid());
    CHECK_FALSE(estimateTempo({{1000, 1.0f}}, 44100 * 2, 44100.0).valid());
}

TEST_CASE("two hits in a sound of exactly one bar at 120 BPM give 120 BPM", "[analysis][tempo]")
{
    const std::vector<Onset> hits{{0, 1.0f}, {44100, 0.8f}};
    const auto result = estimateTempo(hits, 44100 * 2, 44100.0);
    REQUIRE(result.valid());
    CHECK(result.bpm() == Catch::Approx(120.0).margin(0.1));
}

TEST_CASE("a 7 s pattern is not taken for 4 bars at 137 BPM", "[analysis][tempo]")
{
    const auto result = analyse(test::loadAnalysisFixture("pattern-120-7s"));
    REQUIRE(result.valid());
    for (const auto& candidate : result.candidates)
        CHECK(std::abs(candidate.bpm - 137.14) > 2.0);
}

TEST_CASE("analysis accuracy over the fixture set stays at or above the baseline", "[analysis][tempo][slow]")
{
    int tempoExact = 0, tempoOctave = 0, hitsFound = 0, hitsTotal = 0;
    const auto& names = test::analysisFixtureNames();
    for (const auto& name : names)
    {
        const auto fixture = test::loadAnalysisFixture(name);
        OnsetAnalysis onsets;
        const auto tempo = analyse(fixture, &onsets);

        if (tempo.valid())
        {
            const double ratio = tempo.bpm() / fixture.tempo;
            if (std::abs(ratio - 1.0) <= 0.01)
                ++tempoExact;
            else if (std::abs(ratio - 2.0) <= 0.02 || std::abs(ratio - 0.5) <= 0.01)
                ++tempoOctave;
        }
        const auto tolerance = static_cast<juce::int64>(0.010 * fixture.sampleRate);
        for (const auto truth : fixture.onsets)
        {
            ++hitsTotal;
            for (const auto& onset : onsets.onsets)
                if (std::llabs(onset.frame - truth) <= tolerance)
                {
                    ++hitsFound;
                    break;
                }
        }
    }
    const double tempoAccuracy = 100.0 * tempoExact / static_cast<double>(names.size());
    const double octaveShare = 100.0 * tempoOctave / static_cast<double>(names.size());
    const double hitAccuracy = 100.0 * hitsFound / static_cast<double>(hitsTotal);
    std::printf("analysis accuracy: tempo within 1%%: %d/%zu (%.0f%%), half/double: %d (%.0f%%), hits within 10 ms: %d/%d (%.1f%%)\n",
                tempoExact, names.size(), tempoAccuracy, tempoOctave, octaveShare, hitsFound, hitsTotal, hitAccuracy);

    // Baseline recorded when the suite was first accepted (synthetic fixtures: everything is found).
    constexpr double tempoBaseline = 100.0;
    constexpr double hitBaseline = 100.0;
    CHECK(tempoAccuracy >= tempoBaseline);
    CHECK(hitAccuracy >= hitBaseline);
}

TEST_CASE("with only a weak hit in the first beat the first beat is the start of the sound", "[analysis][tempo]")
{
    const std::vector<Onset> hits{{5000, 0.3f}, {44100, 1.0f}, {88200, 0.8f}};
    const auto result = estimateTempo(hits, 44100 * 3, 44100.0);
    REQUIRE(result.valid());
    CHECK(result.firstBeatFrame == 0);
}
