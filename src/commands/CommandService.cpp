#include "CommandService.h"

#include "model/SamplerState.h"

#include <set>

namespace sampler::commands
{
namespace
{
/** Empty text if `relative` is a relative path that stays inside `bundle`, else what is wrong with it. */
juce::String checkBundlePath(const juce::String& relative, const juce::File& bundle)
{
    if (relative.isEmpty())
        return "is empty";
    if (juce::File::isAbsolutePath(relative))
        return "is not relative";
    if (! bundle.getChildFile(relative).isAChildOf(bundle))
        return "points outside the project folder";
    return {};
}
} // namespace

std::optional<juce::String> findInvariantViolation(const juce::ValueTree& sampler, const juce::File& bundleDir)
{
    using namespace model;
    if (! sampler.isValid() || ! sampler.hasType(ids::SAMPLER))
        return juce::String("there is no SAMPLER section");
    if (! sampler.hasProperty(ids::schemaVersion))
        return juce::String("the SAMPLER section has no schema version");

    std::set<juce::String> seen;
    for (const auto& child : sampler)
    {
        if (! child.hasType(ids::SOURCE))
            continue;
        const auto id = child[ids::id].toString();
        if (id.isEmpty())
            return juce::String("a source has no id");
        if (! seen.insert(id).second)
            return "two sources have the id " + id;
        for (const auto& property : {ids::original, ids::decoded})
            if (const auto problem = checkBundlePath(child[property].toString(), bundleDir); problem.isNotEmpty())
                return "source " + id + ": " + property.toString() + " " + problem;
    }
    return std::nullopt;
}

CommandService::CommandService(juce::ValueTree samplerState, juce::UndoManager& undoManager, juce::File bundleDir)
    : context{std::move(samplerState), undoManager, std::move(bundleDir)}
{
}

void CommandService::apply(Command& command)
{
    const auto name = command.name();
    context.undo.beginNewTransaction(name);
    command.perform(context);
    check(name);
}

void CommandService::check(const juce::String& commandName)
{
    if (! invariantChecksEnabled)
        return;
    if (const auto problem = findInvariantViolation(context.sampler, context.bundleDir))
    {
        const auto message = "Project check failed after \"" + commandName + "\": " + *problem;
        if (onInvariantViolation)
            onInvariantViolation(message);
        else
        {
            juce::Logger::writeToLog(message);
            jassertfalse;
        }
    }
}

bool CommandService::canUndo() const { return context.undo.canUndo(); }
bool CommandService::canRedo() const { return context.undo.canRedo(); }
juce::String CommandService::undoName() const { return context.undo.getUndoDescription(); }
juce::String CommandService::redoName() const { return context.undo.getRedoDescription(); }

void CommandService::undo()
{
    context.undo.undo();
    check("Undo");
}

void CommandService::redo()
{
    context.undo.redo();
    check("Redo");
}
} // namespace sampler::commands
