#pragma once

#include <juce_core/juce_core.h>

namespace sampler::fixtures
{
/** Names of the committed fixtures, in the order they are written. */
juce::StringArray fixtureFileNames();

/**
    Writes every fixture into `dir` (created if needed). The audio is synthesised from fixed formulas and a fixed noise
    seed, so the decoded samples are the same on every run. Returns an error text, or an empty string on success.
*/
juce::String generateFixtures(const juce::File& dir);

/**
    Writes the two large cases that are never committed: `texture-10min.wav` (a 10-minute stereo file that takes a
    moment to decode) and `toolong-31min.wav` (31 minutes, mono 8 kHz, over the import limit).
*/
juce::String generateLongFixtures(const juce::File& dir);
/** Writes only `toolong-31min.wav` (mono 8 kHz, about 30 MB), the file the 30-minute limit test needs. */
juce::String generateTooLongFixture(const juce::File& dir);
} // namespace sampler::fixtures
