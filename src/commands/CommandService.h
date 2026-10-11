#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>
#include <memory>
#include <optional>

namespace sampler::commands
{
/** What a command may touch: the SAMPLER subtree, the undo manager it records into, and the bundle folder (to check paths). */
struct ModelContext
{
    juce::ValueTree sampler;
    juce::UndoManager& undo;
    juce::File bundleDir;
};

/** One user action on the project. `perform` edits the model through `ModelContext` and nothing else. */
class Command
{
public:
    virtual ~Command() = default;

    /** The undo step name shown in the Edit menu, for example "Import kick.wav". */
    virtual juce::String name() const = 0;
    virtual void perform(ModelContext&) = 0;
};

/**
    Describes what is wrong with the model after an edit, or nothing if it is consistent: unique source ids, relative
    paths inside the bundle, the schema version present. Pure function, also used by tests.
*/
std::optional<juce::String> findInvariantViolation(const juce::ValueTree& sampler, const juce::File& bundleDir);

/**
    The single path from the UI to the project model (design D-2). Each `apply` is one undo transaction named after the
    command. In debug builds every apply is followed by an invariant check that stops with a message naming the command.
*/
class CommandService
{
public:
    CommandService(juce::ValueTree samplerState, juce::UndoManager&, juce::File bundleDir);

    /** Runs the command as one undo step. */
    void apply(Command&);
    void apply(std::unique_ptr<Command> command) { apply(*command); }

    /** The bundle folder moved (Save As of an untitled project). */
    void setBundleDir(juce::File dir) { context.bundleDir = std::move(dir); }

    bool canUndo() const;
    bool canRedo() const;
    /** Name of the step Undo would revert, or an empty string. */
    juce::String undoName() const;
    juce::String redoName() const;
    void undo();
    void redo();

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

    ModelContext context;
};
} // namespace sampler::commands
