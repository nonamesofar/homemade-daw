#include "AtomicFile.h"

#include <filesystem>
#include <string>

namespace sampler::io
{
namespace
{
/**
    Moves `from` over `to` with one rename that replaces the target (MoveFileExW with MOVEFILE_REPLACE_EXISTING on
    Windows, rename(2) on POSIX), so `to` names the old file or the new one at every moment. Not ReplaceFile
    (juce::TemporaryFile::overwriteTargetFileWithTemporary), which renames the old file away first and leaves a moment
    with no file. Retries briefly, since a reader without FILE_SHARE_DELETE can block it for a moment.
    Same code as in src/engine/EngineSetup.cpp (engine may not depend on io).
*/
bool replaceByRename(const juce::File& from, const juce::File& to)
{
    const auto path = [](const juce::File& f)
    { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(f.getFullPathName().toRawUTF8()))); };

    for (int attempt = 0; attempt < 5; ++attempt)
    {
        std::error_code error;
        std::filesystem::rename(path(from), path(to), error);
        if (!error)
            return true;
        juce::Thread::sleep(100);
    }
    return false;
}

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

    return replaceByRename(temporary.getFile(), destination);
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
