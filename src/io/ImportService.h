#pragma once

#include "JobRegistry.h"
#include "commands/ArrangeCommands.h"
#include "commands/CommandService.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace sampler::io
{
/** What the import tells the user: a problem with a file, a note about what was done, or "this one is already in". */
struct ImportMessage
{
    enum class Kind
    {
        error,     // the file was not imported
        info,      // imported, with a note (for example mixed down to stereo)
        duplicate  // already in the project: `sourceId` is the existing sound
    };

    Kind kind = Kind::error;
    juce::String text;
    juce::String sourceId;
};

/**
    Brings audio files into the open project (design D-3). `importFiles` runs on the message thread and starts a
    worker that, per file, checks the type and length, hashes the bytes, recognises duplicates, copies the file into
    `audio/originals/` and decodes it to `audio/<name>-<hash8>.wav` (32-bit float, native rate), each through a
    temporary file and a rename. When the batch is done the message thread records every good file with one
    AddSources command (one undo step). Nothing touches the model before that, and the user's files are only read.
*/
class ImportService
{
public:
    /** `bundleDir` is asked for each batch (Save As of an untitled project moves the bundle). */
    ImportService(juce::ValueTree samplerState, commands::CommandService&, std::function<juce::File()> bundleDir,
                  JobRegistry&);
    /** Cancels a running batch: the worker stops within a block and removes the files it wrote. */
    ~ImportService();

    /**
        Starts importing; returns at once. Message thread only. With a placement (files dropped on the timeline) the
        batch ends with one ImportAndPlace step instead: the sounds and their clips, one after another from the target
        beat (a file already in the project is placed from its existing sound).
    */
    void importFiles(const juce::Array<juce::File>& files, std::optional<commands::PlacementTarget> placement = std::nullopt);

    /** True while any batch is running or waiting for its result to be recorded. */
    bool isBusy() const { return pendingBatches > 0; }

    /** Message thread. Called for every error, note and duplicate of a batch, after the sounds are recorded. */
    std::function<void(const ImportMessage&)> onMessage;

    /**
        Message thread. The warp a clip of a sound that is already in the project starts with (set by the app, which
        owns the analysis). Without it, or for a sound that is new in the batch, the clip starts with WARP off.
    */
    std::function<engine::WarpSettings(const juce::String& sourceId)> defaultWarp;

    /** Longest file accepted, in seconds. */
    static constexpr double maxSeconds = 30.0 * 60.0;

    /** The file extensions that can be imported, lowercase without a dot. */
    static juce::StringArray supportedExtensions();

private:
    struct Prepared;
    void finishBatch(int jobId, std::vector<Prepared> ready, std::vector<ImportMessage> messages,
                     std::vector<juce::String> hashesInOrder, std::optional<commands::PlacementTarget> placement);

    juce::ValueTree sampler;
    commands::CommandService& commands;
    std::function<juce::File()> bundleDir;
    JobRegistry& registry;
    int pendingBatches = 0;
    std::vector<int> activeJobIds;
    std::shared_ptr<std::atomic<bool>> cancelled; // set when the service goes away: the worker stops and cleans up
    juce::ThreadPool pool;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ImportService)
};
} // namespace sampler::io
