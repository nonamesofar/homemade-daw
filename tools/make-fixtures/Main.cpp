// make-fixtures <out-dir>        writes the committed audio fixtures (tests/audio-fixtures) into <out-dir>
// make-fixtures --long <dir>     writes the two large uncommitted cases (10 minutes, 31 minutes) into <dir>
#include <juce_events/juce_events.h>

#include <cstdio>

#include "FixtureGenerator.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const bool longCases = argc == 3 && juce::String(argv[1]) == "--long";
    if (! longCases && argc != 2)
    {
        std::fprintf(stderr, "usage: make-fixtures <out-dir>\n       make-fixtures --long <out-dir>\n");
        return 2;
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[longCases ? 2 : 1]));
    const auto error = longCases ? sampler::fixtures::generateLongFixtures(dir) : sampler::fixtures::generateFixtures(dir);
    if (error.isNotEmpty())
    {
        std::fprintf(stderr, "make-fixtures: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf("wrote fixtures to %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}
