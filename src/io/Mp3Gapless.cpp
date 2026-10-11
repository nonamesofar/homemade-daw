#include "Mp3Gapless.h"

#include <cstring>

namespace sampler::io
{
namespace
{
juce::uint32 be32(const unsigned char* p)
{
    return (static_cast<juce::uint32>(p[0]) << 24) | (static_cast<juce::uint32>(p[1]) << 16)
           | (static_cast<juce::uint32>(p[2]) << 8) | p[3];
}
} // namespace

LameTag parseLameTag(const void* rawData, size_t size)
{
    LameTag tag;
    const auto* data = static_cast<const unsigned char*>(rawData);
    size_t pos = 0;

    // Skip an ID3v2 tag: "ID3", version, flags, then a 28-bit "synchsafe" size.
    if (size >= 10 && std::memcmp(data, "ID3", 3) == 0)
        pos = 10 + ((static_cast<size_t>(data[6] & 0x7f) << 21) | (static_cast<size_t>(data[7] & 0x7f) << 14)
                    | (static_cast<size_t>(data[8] & 0x7f) << 7) | (data[9] & 0x7f));

    // Find the first frame header (11 sync bits).
    while (pos + 4 <= size && ! (data[pos] == 0xff && (data[pos + 1] & 0xe0) == 0xe0))
        ++pos;
    if (pos + 4 > size)
        return tag;

    const int versionBits = (data[pos + 1] >> 3) & 3; // 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5
    const int layerBits = (data[pos + 1] >> 1) & 3;   // 1 = layer III
    const bool hasCrc = (data[pos + 1] & 1) == 0;
    const int channelMode = (data[pos + 3] >> 6) & 3; // 3 = mono
    if (versionBits == 1 || layerBits != 1)
        return tag;

    const bool mpeg1 = versionBits == 3;
    const bool mono = channelMode == 3;
    tag.samplesPerFrame = mpeg1 ? 1152 : 576;
    const size_t sideInfo = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    const size_t xing = pos + 4 + (hasCrc ? 2 : 0) + sideInfo;
    if (xing + 8 > size)
        return tag;
    if (std::memcmp(data + xing, "Xing", 4) != 0 && std::memcmp(data + xing, "Info", 4) != 0)
        return tag;

    const juce::uint32 flags = be32(data + xing + 4);
    size_t cursor = xing + 8;
    if (flags & 1) // frames
    {
        if (cursor + 4 > size)
            return tag;
        tag.audioFrames = be32(data + cursor);
        cursor += 4;
    }
    if (flags & 2)
        cursor += 4; // bytes
    if (flags & 4)
        cursor += 100; // table of contents
    if (flags & 8)
        cursor += 4; // quality

    // The LAME extension: 9 bytes "LAME3.100", then 12 bytes, then 3 bytes with delay and padding (12 bits each).
    if (cursor + 24 > size || std::memcmp(data + cursor, "LAME", 4) != 0)
        return tag;
    const auto* d = data + cursor + 21;
    tag.encoderDelay = (d[0] << 4) | (d[1] >> 4);
    tag.encoderPadding = ((d[1] & 0x0f) << 8) | d[2];
    tag.found = true;
    return tag;
}

LameTag readLameTag(const juce::File& file)
{
    juce::FileInputStream in(file);
    if (in.failedToOpen())
        return {};
    // An ID3v2 tag (cover art can make it megabytes) comes first: read its size and jump over it.
    juce::MemoryBlock header;
    in.readIntoMemoryBlock(header, 10);
    juce::int64 start = 0;
    if (const auto* h = static_cast<const unsigned char*>(header.getData());
        header.getSize() == 10 && std::memcmp(h, "ID3", 3) == 0)
    {
        start = 10 + ((static_cast<juce::int64>(h[6] & 0x7f) << 21) | (static_cast<juce::int64>(h[7] & 0x7f) << 14)
                      | (static_cast<juce::int64>(h[8] & 0x7f) << 7) | (h[9] & 0x7f));
        if (h[5] & 0x10)
            start += 10; // footer
    }
    in.setPosition(start);
    juce::MemoryBlock head;
    in.readIntoMemoryBlock(head, 16384); // the first frame and its Xing/LAME header
    return parseLameTag(head.getData(), head.getSize());
}

Mp3Cut mp3CutFor(const LameTag& tag)
{
    Mp3Cut cut;
    if (! tag.found)
    {
        cut.start = taglessStartCut;
        return cut;
    }
    cut.start = tag.encoderDelay + decoderDelayFrames;
    if (tag.audioFrames > 0)
        cut.length = tag.audioFrames * tag.samplesPerFrame - tag.encoderDelay - tag.encoderPadding;
    return cut;
}
} // namespace sampler::io
