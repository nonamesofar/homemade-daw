#pragma once

// Engine-internal: includes Tracktion, so only src/engine/*.cpp may include it.
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>

namespace sampler::detail
{
/** The one place an engine is constructed: our UI behaviour (runs render tasks) and no audio input. */
std::unique_ptr<tracktion::engine::Engine> makeEngine(const juce::String& appName);

/**
    Saves the edit to its own file (the edit's editFileRetriever) so that the file is always either the old version or
    the new one, never missing or partial. Use this for every project save, never te::EditFileOperations::save: that
    writes a temporary file and then calls File::moveFileTo, which deletes the edit file first and only then moves the
    temporary file in, so a crash between the two leaves no project file at all.

    Same steps as Tracktion's EditFileOperations::writeToFile (store control mappings, flush plugin state, serialise
    the state to XML), then: write a temporary file next to the edit file, flush it to disk, and swap it in with one
    rename that replaces the target (std::filesystem::rename: MoveFileExW with MOVEFILE_REPLACE_EXISTING). Not
    ReplaceFile: it leaves a moment with no edit file (see the .cpp). Resets the edit's changed flag on success.

    `beforeCommit` runs after the temporary file is complete and flushed, before the swap (tests only).
*/
bool saveEditAtomically(tracktion::engine::Edit&,
                        const std::function<void(const juce::File& temporaryFile)>& beforeCommit = {});
} // namespace sampler::detail
