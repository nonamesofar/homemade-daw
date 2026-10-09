#include "EngineSetup.h"

#include <filesystem>
#include <string>

namespace te = tracktion::engine;

namespace sampler::detail
{
namespace
{
/** Runs engine tasks (renders) on a worker thread while pumping the message loop. */
class SamplerUIBehaviour : public te::UIBehaviour
{
public:
    void runTaskWithProgressBar(te::ThreadPoolJobWithProgress& job) override
    {
        TaskRunner runner(job);
        while (runner.isThreadRunning())
            if (!juce::MessageManager::getInstance()->runDispatchLoopUntil(10))
                break;
    }

private:
    struct TaskRunner : juce::Thread
    {
        explicit TaskRunner(te::ThreadPoolJobWithProgress& j) : juce::Thread(j.getJobName()), task(j) { startThread(); }

        ~TaskRunner() override
        {
            task.signalJobShouldExit();
            waitForThreadToExit(10000);
        }

        void run() override
        {
            while (!threadShouldExit())
                if (task.runJob() == juce::ThreadPoolJob::jobHasFinished)
                    break;
        }

        te::ThreadPoolJobWithProgress& task;
    };
};

/**
    Moves `from` over `to` with one rename that replaces the target (MoveFileExW with MOVEFILE_REPLACE_EXISTING on
    Windows, rename(2) on POSIX), so `to` names the old file or the new one at every moment. Retries briefly, since a
    reader without FILE_SHARE_DELETE (a virus scanner, an indexer) can block it for a moment.
    Same code as in src/io/AtomicFile.cpp (engine may not depend on io).
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

/** Input recording arrives at M8; until then no input device is opened. */
class SamplerEngineBehaviour : public te::EngineBehaviour
{
public:
    bool shouldOpenAudioInputByDefault() override { return false; }
};
} // namespace

std::unique_ptr<te::Engine> makeEngine(const juce::String& appName)
{
    return std::make_unique<te::Engine>(appName, std::make_unique<SamplerUIBehaviour>(),
                                        std::make_unique<SamplerEngineBehaviour>());
}

bool saveEditAtomically(te::Edit& edit, const std::function<void(const juce::File&)>& beforeCommit)
{
    const auto editFile = edit.editFileRetriever ? edit.editFileRetriever() : juce::File();
    if (editFile == juce::File() || editFile.isDirectory())
        return false;

    // What EditFileOperations::writeToFile does before serialising. (It also refreshes the edit snapshot and the
    // project item length, which we do not use: no te::Project, no edit browser.)
    edit.getParameterControlMappings().saveToEdit();
    edit.flushState();
    const auto xml = edit.state.createXml();
    if (xml == nullptr)
        return false;

    editFile.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(editFile); // next to the edit file, so the swap stays on one volume
    {
        juce::FileOutputStream out(temporary.getFile());
        if (!out.openedOk())
            return false;
        xml->writeTo(out);
        out.flush(); // FlushFileBuffers on Windows: the bytes are on disk before the swap
        if (out.getStatus().failed())
            return false;
    }

    if (beforeCommit)
        beforeCommit(temporary.getFile());

    // Not juce::TemporaryFile::overwriteTargetFileWithTemporary: on Windows that is ReplaceFile, which renames the old
    // file away before moving the new one in, and another thread can see no edit file in between (measured: missing in
    // 886 of 66,219 polls during 50 saves; tests/engine/integrity.test.cpp). One replacing rename has no such gap.
    if (!replaceByRename(temporary.getFile(), editFile))
        return false;

    edit.resetChangedStatus();
    return true;
}
} // namespace sampler::detail
