// Test helper process: writes `bytes` bytes of 0xAB to <destination> atomically, but stops for
// <stallMs> after half of them are written, so the test can kill it mid-save.
//   AtomicWriterProbe <destination> <bytes> <stallMs>
#include <juce_core/juce_core.h>

#include <cstdio>
#include <vector>

#include "io/AtomicFile.h"

int main(int argc, char** argv)
{
    if (argc < 4)
        return 2;

    const juce::File destination(juce::String::fromUTF8(argv[1]));
    const auto bytes = static_cast<size_t>(std::atoll(argv[2]));
    const auto stallMs = std::atoi(argv[3]);

    const std::vector<char> data(bytes, static_cast<char>(0xAB));
    const auto ok = sampler::io::writeFileAtomicallyInTwoParts(destination, data.data(), data.size(),
                                                               [&](const juce::File&)
                                                               {
                                                                   std::printf("half-written\n");
                                                                   std::fflush(stdout);
                                                                   juce::Thread::sleep(stallMs);
                                                               });
    return ok ? 0 : 1;
}
