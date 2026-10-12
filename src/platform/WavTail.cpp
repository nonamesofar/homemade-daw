#include "WavTail.h"

#include <cstring>

namespace sampler::platform
{
namespace
{
struct Layout
{
    bool rf64 = false;
    juce::int64 fileSize = 0;
    int blockAlign = 0;
    juce::int64 dataStart = 0;      // first byte of the audio
    juce::int64 dataSize = 0;       // bytes of audio
    juce::int64 dataSizeField = 0;  // where the data chunk's 4-byte size field sits
    juce::int64 ds64Position = -1;  // start of the ds64 payload (RF64 only)
};

juce::uint32 readU32(juce::FileInputStream& in) { return static_cast<juce::uint32>(in.readInt()); }

/** Walks the chunks; false if the file is not a layout we trim. */
bool parse(juce::FileInputStream& in, Layout& layout)
{
    layout.fileSize = in.getTotalLength();
    if (layout.fileSize < 44)
        return false;

    char head[4];
    if (in.read(head, 4) != 4)
        return false;
    layout.rf64 = std::memcmp(head, "RF64", 4) == 0;
    if (! layout.rf64 && std::memcmp(head, "RIFF", 4) != 0)
        return false;
    in.skipNextBytes(4);
    char wave[4];
    if (in.read(wave, 4) != 4 || std::memcmp(wave, "WAVE", 4) != 0)
        return false;

    juce::int64 rf64DataSize = -1;
    bool haveFmt = false;
    while (in.getPosition() + 8 <= layout.fileSize)
    {
        char id[4];
        if (in.read(id, 4) != 4)
            return false;
        const juce::int64 size = readU32(in);
        const auto payload = in.getPosition();

        if (std::memcmp(id, "ds64", 4) == 0)
        {
            if (size < 16)
                return false;
            layout.ds64Position = payload;
            in.skipNextBytes(8); // RIFF size
            rf64DataSize = in.readInt64();
        }
        else if (std::memcmp(id, "fmt ", 4) == 0)
        {
            if (size < 16)
                return false;
            in.skipNextBytes(12);
            layout.blockAlign = static_cast<unsigned short>(in.readShort());
            haveFmt = true;
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            if (! haveFmt || layout.blockAlign <= 0)
                return false;
            layout.dataSizeField = payload - 4;
            layout.dataStart = payload;
            layout.dataSize = (layout.rf64 && size == 0xffffffffLL) ? rf64DataSize : size;
            if (layout.dataSize < 0 || layout.dataStart + layout.dataSize > layout.fileSize)
                return false;
            // The data chunk must be the last one (a pad byte after an odd size is allowed).
            const auto after = layout.dataStart + layout.dataSize;
            return after == layout.fileSize || (after + 1 == layout.fileSize && (layout.dataSize & 1) != 0);
        }

        const auto next = payload + size + (size & 1);
        if (next > layout.fileSize || ! in.setPosition(next))
            return false;
    }
    return false;
}

void writeU32(juce::FileOutputStream& out, juce::int64 position, juce::uint32 value)
{
    out.setPosition(position);
    out.writeInt(static_cast<int>(value));
}
} // namespace

juce::Result trimWavTail(const juce::File& file, juce::int64 keepFrames)
{
    if (keepFrames < 0)
        return juce::Result::fail("negative length");

    Layout layout;
    {
        juce::FileInputStream in(file);
        if (in.failedToOpen())
            return juce::Result::fail("cannot open " + file.getFileName());
        if (! parse(in, layout))
            return juce::Result::fail(file.getFileName() + " is not a WAV layout that can be trimmed");
    }

    const auto keepBytes = keepFrames * layout.blockAlign;
    if (keepBytes >= layout.dataSize)
        return juce::Result::ok();

    const bool pad = (keepBytes & 1) != 0;
    const auto newSize = layout.dataStart + keepBytes + (pad ? 1 : 0);

    juce::FileOutputStream out(file);
    if (out.failedToOpen())
        return juce::Result::fail("cannot write " + file.getFileName());

    // Sizes first (a crash in between leaves a header that is too long, which readers tolerate), then the cut.
    if (layout.rf64)
    {
        out.setPosition(layout.ds64Position);
        out.writeInt64(newSize - 8);
        out.writeInt64(keepBytes);
    }
    else
    {
        writeU32(out, 4, static_cast<juce::uint32>(newSize - 8));
        writeU32(out, layout.dataSizeField, static_cast<juce::uint32>(keepBytes));
    }

    out.setPosition(layout.dataStart + keepBytes);
    if (pad)
        out.writeByte(0);
    if (! out.truncate().wasOk())
        return juce::Result::fail("cannot shorten " + file.getFileName());
    out.flush();
    return juce::Result::ok();
}
} // namespace sampler::platform
