#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sampler::engine
{
class Arrangement;
}

namespace sampler::commands
{
/**
    What a command may touch: the SAMPLER subtree, the arrangement (tracks, clips, tempo; null in the model-only tests),
    the undo manager everything records into, and the bundle folder (to check paths and find the sounds' files).
*/
struct ModelContext
{
    juce::ValueTree sampler;
    juce::UndoManager& undo;
    juce::File bundleDir;
    engine::Arrangement* arrangement = nullptr;
};

/** One user action on the project. `perform` edits the model through `ModelContext` and nothing else. */
class Command
{
public:
    virtual ~Command() = default;

    /** The undo step name shown in the Edit menu, for example "Import kick.wav". */
    virtual juce::String name() const = 0;
    virtual void perform(ModelContext&) = 0;

    /**
        Commands with the same non-empty key applied less than 500 ms apart, with nothing else in between, share one
        undo step (repeated mixer clicks on the same control of the same track).
    */
    virtual juce::String coalesceKey() const { return {}; }
};

/**
    Describes what is wrong with the model after an edit, or nothing if it is consistent: unique source ids, relative
    paths inside the bundle, the schema version present. Pure function, also used by tests.
*/
std::optional<juce::String> findInvariantViolation(const juce::ValueTree& sampler, const juce::File& bundleDir);

/**
    The arrangement checks: no two clips overlap on a track, each clip's region lies within its sound, clip ids are
    unique, and every clip's sound is in the project.
*/
std::optional<juce::String> findArrangementViolation(const engine::Arrangement&, const juce::ValueTree& sampler);

/**
    The single path from the UI to the project model (design D-2 of M1, D-3 of M2). Each `apply` is one undo transaction
    named after the command. A gesture (`begin`, any number of `update`s, `commit`) is one transaction too; `cancel`
    removes it. In debug builds every apply is followed by an invariant check that stops with a message naming the
    command.

    The history token (design D-4) names the current position in the undo history: it changes with every edit, undo
    and redo, and comes back to an earlier value when the history returns to that point. The project has unsaved
    changes when the token differs from the one recorded at the last save.
*/
class CommandService
{
public:
    CommandService(juce::ValueTree samplerState, juce::UndoManager&, juce::File bundleDir,
                   engine::Arrangement* arrangement = nullptr);

    /** Runs the command as one undo step (or as part of the previous one, see Command::coalesceKey). */
    void apply(Command&);
    void apply(std::unique_ptr<Command> command) { apply(*command); }

    /** Opens a gesture: everything applied with `update` until `commit` is one undo step named `name`. */
    void begin(const juce::String& name);
    /** Applies within the open gesture (heard at once). Without an open gesture it is a plain `apply`. */
    void update(Command&);
    void update(std::unique_ptr<Command> command) { update(*command); }
    void commit();
    /** Reverts what the open gesture changed and leaves no undo step. */
    void cancel();
    bool isGestureOpen() const { return gestureOpen; }

    /** The bundle folder moved (Save As of an untitled project). */
    void setBundleDir(juce::File dir) { context.bundleDir = std::move(dir); }
    const ModelContext& modelContext() const { return context; }

    /** Undo and redo are not available while a gesture is open. */
    bool canUndo() const;
    bool canRedo() const;
    /** Name of the step Undo would revert, or an empty string. */
    juce::String undoName() const;
    juce::String redoName() const;
    void undo();
    void redo();

    /** The position in the undo history (0 at the start). */
    int historyToken() const;
    /** The next command never joins the previous step (called on save). */
    void breakCoalescing() { lastKey = {}; }

    /** Milliseconds for the coalescing window (tests replace it). */
    std::function<juce::uint32()> clock = [] { return juce::Time::getMillisecondCounter(); };
    static constexpr juce::uint32 coalesceWindowMs = 500;

    /** Debug builds check after every apply; tests can switch it on in other builds. */
    bool invariantChecksEnabled =
#if JUCE_DEBUG
        true;
#else
        false;
#endif

    /** Called with a message when a check fails. The default logs it and stops (jassertfalse); tests replace it. */
    std::function<void(const juce::String& message)> onInvariantViolation;

private:
    void check(const juce::String& commandName);
    int undoDepth() const;
    /** Updates the history serials after a change; `sameStep` when it joined the current step. */
    void recordChange(bool sameStep);

    ModelContext context;
    bool gestureOpen = false;
    juce::String gestureName;
    juce::String lastKey;
    juce::uint32 lastTime = 0;
    std::vector<int> serials; // one per step of the history (undoable and redoable)
    std::vector<int> stashedSerials; // the redo steps a new step pushed aside (back if a gesture is cancelled)
    int position = 0;         // steps that can be undone
    int nextSerial = 1;
};
} // namespace sampler::commands
