#include "AtomicFile.h"

namespace sampler::io
{
namespace
{
bool writeAndSwap(const juce::File& destination, const void* data, size_t size,
                  const std::function<void(const juce::File&)>& midWrite,
                  const std::function<void(const juce::File&)>& beforeCommit)
{
    destination.getParentDirectory().createDirectory();

    juce::TemporaryFile temporary(destination);
    {
        juce::FileOutputStream out(temporary.getFile());
        if (out.failedToOpen())
            return false;

        const auto firstPart = midWrite ? size / 2 : size;
        if (!out.write(data, firstPart))
            return false;

        if (midWrite)
        {
            out.flush();
            midWrite(temporary.getFile());
            if (!out.write(static_cast<const char*>(data) + firstPart, size - firstPart))
                return false;
        }

        out.flush(); // FlushFileBuffers on Windows: the bytes are on disk before the swap
        if (out.getStatus().failed())
            return false;
    }

    if (beforeCommit)
        beforeCommit(temporary.getFile());

    return temporary.overwriteTargetFileWithTemporary();
}
} // namespace

bool writeFileAtomically(const juce::File& destination, const void* data, size_t size,
                         const std::function<void(const juce::File&)>& beforeCommit)
{
    return writeAndSwap(destination, data, size, {}, beforeCommit);
}

bool writeFileAtomicallyInTwoParts(const juce::File& destination, const void* data, size_t size,
                                   const std::function<void(const juce::File&)>& midWrite)
{
    return writeAndSwap(destination, data, size, midWrite, {});
}
} // namespace sampler::io
