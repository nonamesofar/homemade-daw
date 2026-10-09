// Decode probe (tasks 2.2 and 2.3): what does the engine's decoder make of a file?
//   DecodeProbe <file>...
// Prints format, rate, channels, frames and the first audible frame. For MP3 it also reads the LAME/Xing
// tag, so decoded length and leading silence can be compared with the encoder delay the file declares.
#include <cstdio>
#include <cstring>

#include <juce_events/juce_events.h>

#include "engine/EngineHost.h"
#include "platform/windows/MediaFoundationProbe.h"

namespace
{
struct LameTag
{
    bool found = false;
    juce::String encoder;
    int delay = 0;
    int padding = 0;
    juce::int64 xingFrames = 0;
};

int syncsafe(const juce::uint8* p) { return (p[0] << 21) | (p[1] << 14) | (p[2] << 7) | p[3]; }

LameTag readLameTag(const juce::File& file)
{
    LameTag tag;
    juce::MemoryBlock head;
    if (auto in = file.createInputStream())
        in->readIntoMemoryBlock(head, 4096);
    const auto* d = static_cast<const juce::uint8*>(head.getData());
    const auto n = static_cast<int>(head.getSize());

    int pos = 0;
    if (n > 10 && std::memcmp(d, "ID3", 3) == 0)
        pos = 10 + syncsafe(d + 6);
    if (pos >= n)
    {
        // The ID3v2 tag is larger than what we read: read again further in.
        juce::MemoryBlock more;
        if (auto in = file.createInputStream())
        {
            in->setPosition(pos);
            in->readIntoMemoryBlock(more, 4096);
        }
        head = more;
        d = static_cast<const juce::uint8*>(head.getData());
        pos = 0;
    }
    const auto size = static_cast<int>(head.getSize());

    for (int i = pos; i + 200 < size; ++i)
    {
        if ((std::memcmp(d + i, "Xing", 4) == 0 || std::memcmp(d + i, "Info", 4) == 0) && i > 20)
        {
            const auto flags = d[i + 7];
            int p = i + 8;
            if (flags & 1)
            {
                tag.xingFrames = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3];
                p += 4;
            }
            if (flags & 2) p += 4;
            if (flags & 4) p += 100;
            if (flags & 8) p += 4;
            tag.encoder = juce::String::fromUTF8(reinterpret_cast<const char*>(d + p), 9);
            if (tag.encoder.startsWith("LAME") || tag.encoder.startsWith("Lavf") || tag.encoder.startsWith("Lavc"))
            {
                const auto* dp = d + p + 21;
                tag.delay = (dp[0] << 4) | (dp[1] >> 4);
                tag.padding = ((dp[1] & 0x0F) << 8) | dp[2];
                tag.found = true;
            }
            break;
        }
    }
    return tag;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    sampler::EngineHost host;

    for (int a = 1; a < argc; ++a)
    {
        const juce::File file(juce::String::fromUTF8(argv[a]));
        std::printf("\n%s\n", file.getFileName().toRawUTF8());

        auto reader = host.openReader(file);
        if (reader == nullptr)
        {
            std::printf("  NOT DECODED by any engine decoder\n");

            // Does Windows itself cope when asked directly (our own reader, not JUCE's wrapper)?
            const auto mf = sampler::platform::decodeWithMediaFoundation(file);
            if (mf.ok)
                std::printf("  Media Foundation directly: OK rate=%.0f ch=%d frames=%lld (%.3f s, declared %.3f s) "
                            "firstAudibleFrame=%lld\n",
                            mf.sampleRate, mf.channels, static_cast<long long>(mf.frames), mf.frames / mf.sampleRate,
                            mf.declaredDurationSeconds, static_cast<long long>(mf.firstAudibleFrame));
            else
                std::printf("  Media Foundation directly: FAILED %s\n", mf.error.toRawUTF8());
            continue;
        }

        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), 8192);
        juce::int64 firstAudible = -1;
        for (juce::int64 pos = 0; pos < reader->lengthInSamples && firstAudible < 0; pos += 8192)
        {
            const auto num = static_cast<int>(juce::jmin<juce::int64>(8192, reader->lengthInSamples - pos));
            buffer.clear();
            reader->read(&buffer, 0, num, pos, true, true);
            for (int i = 0; i < num && firstAudible < 0; ++i)
                for (int c = 0; c < buffer.getNumChannels(); ++c)
                    if (std::abs(buffer.getSample(c, i)) > 1.0e-4f)
                    {
                        firstAudible = pos + i;
                        break;
                    }
        }

        std::printf("  format=%s rate=%.0f ch=%u frames=%lld (%.3f s) firstAudibleFrame=%lld\n",
                    reader->getFormatName().toRawUTF8(), reader->sampleRate, reader->numChannels,
                    static_cast<long long>(reader->lengthInSamples), reader->lengthInSamples / reader->sampleRate,
                    static_cast<long long>(firstAudible));

        if (file.hasFileExtension("mp3"))
        {
            const auto tag = readLameTag(file);
            if (!tag.found)
            {
                std::printf("  no LAME tag (encoder=%s)\n", tag.encoder.toRawUTF8());
                continue;
            }
            // Decoder delay is 529 samples, so the real audio starts at delay + 529 (+1 for the first frame).
            const auto expectedStart = tag.delay + 529 + 1;
            const auto expectedLength = tag.xingFrames * 1152 - tag.delay - tag.padding - 529 - 1;
            std::printf("  tag: encoder=%s xingFrames=%lld delay=%d padding=%d -> expected start %d, expected length %lld "
                        "(decoded - expected = %lld frames)\n",
                        tag.encoder.toRawUTF8(), static_cast<long long>(tag.xingFrames), tag.delay, tag.padding,
                        expectedStart, static_cast<long long>(expectedLength),
                        static_cast<long long>(reader->lengthInSamples - expectedLength));
        }
    }
    return 0;
}
