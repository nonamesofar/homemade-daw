// sampler-cli render <bundle> <out.wav> [--rate 48000] [--block 512]
//   Renders a project from bar 1 to the end of its last clip, without the metronome and without opening a window or an
//   audio device. Exit code 0 on success; 1 with a message on stderr when the project cannot be opened or rendered;
//   2 for a wrong command line.
#include "engine/EngineHost.h"
#include "engine/OfflineRender.h"
#include "engine/ProjectEdit.h"

#include <juce_events/juce_events.h>

#include <cstdio>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 4 || juce::String(argv[1]) != "render")
    {
        std::fprintf(stderr, "usage: sampler-cli render <bundle> <out.wav> [--rate 48000] [--block 512]\n");
        return 2;
    }

    sampler::engine::RenderOptions options;
    if ((argc - 4) % 2 != 0)
    {
        std::fprintf(stderr, "sampler-cli: option %s needs a value\n", argv[argc - 1]);
        return 2;
    }
    for (int i = 4; i + 1 < argc; i += 2)
    {
        const juce::String flag(argv[i]);
        if (flag == "--rate")
            options.sampleRate = juce::String(argv[i + 1]).getDoubleValue();
        else if (flag == "--block")
            options.blockSize = juce::String(argv[i + 1]).getIntValue();
        else
        {
            std::fprintf(stderr, "sampler-cli: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (options.sampleRate < 8000.0 || options.blockSize < 16)
    {
        std::fprintf(stderr, "sampler-cli: --rate must be at least 8000 and --block at least 16\n");
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto bundle = cwd.getChildFile(juce::String::fromUTF8(argv[2]));
    const auto out = cwd.getChildFile(juce::String::fromUTF8(argv[3]));

    sampler::EngineHost host("SamplerCli", true);
    juce::String error;
    auto project = sampler::ProjectEdit::open(host, bundle, error);
    if (project == nullptr)
    {
        std::fprintf(stderr, "sampler-cli: %s\n", error.toRawUTF8());
        return 1;
    }
    if (! sampler::engine::renderToWav(*project, out, options, error))
    {
        std::fprintf(stderr, "sampler-cli: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}
