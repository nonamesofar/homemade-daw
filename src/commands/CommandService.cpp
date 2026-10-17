#include "CommandService.h"

#include "engine/Arrangement.h"
#include "model/SamplerState.h"

#include <map>
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
        const Source source(child);
        if (source.hasTempoCorrection() && (source.tempoCorrection() < minCorrectedBpm || source.tempoCorrection() > maxCorrectedBpm))
            return "source " + id + ": the tempo correction is outside 20 to 999 BPM";
        if (source.hasBeatOffsetCorrection() && (source.beatOffsetCorrection() < 0 || source.beatOffsetCorrection() > source.frames()))
            return "source " + id + ": the first beat lies outside the sound";
        if (source.hasSliceSet())
            if (const auto problem = findSliceSetViolation(source.rawSliceSet(), source.frames(), source.sampleRate()))
                return "source " + id + ": " + *problem;
    }
    return std::nullopt;
}

std::optional<juce::String> findArrangementViolation(const engine::Arrangement& arrangement, const juce::ValueTree& sampler)
{
    constexpr double toleranceSeconds = 1.0e-4;
    const model::SamplerState state(sampler);
    std::set<juce::String> ids;
    std::map<juce::String, std::vector<engine::ClipInfo>> byTrack;
    for (const auto& clip : arrangement.clips())
    {
        if (! ids.insert(clip.id).second)
            return "two clips have the id " + clip.id;
        if (! state.findSourceById(clip.sourceId).isValid())
            return "clip " + clip.name + " plays a sound that is not in the project (" + clip.sourceId + ")";
        if (clip.offsetSeconds < -toleranceSeconds)
            return "clip " + clip.name + " starts before its sound";
        if (clip.loop)
        {
            // A looped clip repeats its loop, so only the loop has to lie inside the sound, and the clip starts inside it.
            if (clip.sourceSeconds > 0.0 && clip.loopStart + clip.loopLength > clip.sourceSeconds + toleranceSeconds)
                return "clip " + clip.name + " loops past the end of its sound";
            if (clip.offsetSeconds < clip.loopStart - toleranceSeconds || clip.offsetSeconds > clip.loopStart + clip.loopLength + toleranceSeconds)
                return "clip " + clip.name + " starts outside its loop";
        }
        else if (clip.sourceSeconds > 0.0 && clip.offsetSeconds + clip.coveredSeconds() > clip.sourceSeconds + toleranceSeconds)
            return "clip " + clip.name + " runs past the end of its sound";
        if (clip.warp.on && ! clip.warp.active())
            return "clip " + clip.name + " is warped without a tempo in range (" + juce::String(clip.warp.sourceBpm) + ")";
        if (clip.fadeIn < 0.0 || clip.fadeOut < 0.0 || clip.fadeIn + clip.fadeOut > clip.lengthSeconds + toleranceSeconds)
            return "clip " + clip.name + " has fades longer than the clip";
        if (clip.lengthSeconds <= 0.0)
            return "clip " + clip.name + " has no length";
        byTrack[clip.trackId].push_back(clip);
    }
    for (const auto& [track, clips] : byTrack) // each track's clips come sorted by start
        for (size_t i = 0; i + 1 < clips.size(); ++i)
            if (clips[i].startSeconds + clips[i].lengthSeconds > clips[i + 1].startSeconds + toleranceSeconds)
                return "clips " + clips[i].name + " and " + clips[i + 1].name + " overlap";
    return std::nullopt;
}

CommandService::CommandService(juce::ValueTree samplerState, juce::UndoManager& undoManager, juce::File bundleDir,
                               engine::Arrangement* arrangement)
    : context{std::move(samplerState), undoManager, std::move(bundleDir), arrangement}
{
}

int CommandService::undoDepth() const { return context.undo.getUndoDescriptions().size(); }

void CommandService::recordChange(bool sameStep)
{
    // Steps recorded behind our back (none are expected) still get serials, so the bookkeeping never runs past its end.
    while (static_cast<int>(serials.size()) < position)
        serials.push_back(nextSerial++);
    if (sameStep)
    {
        if (position > 0)
            serials[static_cast<size_t>(position - 1)] = nextSerial++;
        return;
    }
    // A new step: the redo steps go (kept aside in case a gesture is cancelled); old steps may have been dropped.
    stashedSerials.assign(serials.begin() + position, serials.end());
    serials.resize(static_cast<size_t>(position));
    const auto depth = undoDepth();
    if (const auto dropped = position + 1 - depth; dropped > 0)
        serials.erase(serials.begin(), serials.begin() + std::min(dropped, static_cast<int>(serials.size())));
    serials.push_back(nextSerial++);
    position = depth;
}

void CommandService::refuse()
{
    if (onRefused)
        onRefused("Stop recording first");
}

bool CommandService::refuseWhileRecording(const Command& command)
{
    if (! recordingLock || command.allowedWhileRecording())
        return false;
    refuse();
    return true;
}

void CommandService::apply(Command& command)
{
    if (refuseWhileRecording(command))
        return;
    if (gestureOpen)
    {
        update(command);
        return;
    }
    const auto name = command.name();
    const auto key = command.coalesceKey();
    const auto now = clock();
    const bool join = key.isNotEmpty() && key == lastKey && now - lastTime < coalesceWindowMs && position > 0
                      && context.undo.getNumActionsInCurrentTransaction() > 0;
    if (! join)
        context.undo.beginNewTransaction(name);
    command.perform(context);
    if (context.undo.getNumActionsInCurrentTransaction() > 0)
        recordChange(join);
    lastKey = key;
    lastTime = now;
    check(name);
}

void CommandService::begin(const juce::String& name)
{
    if (gestureOpen)
        commit();
    context.undo.beginNewTransaction(name);
    gestureOpen = true;
    gestureName = name;
    lastKey = {};
}

void CommandService::update(Command& command)
{
    if (refuseWhileRecording(command))
        return;
    if (! gestureOpen)
    {
        apply(command);
        return;
    }
    const auto before = context.undo.getNumActionsInCurrentTransaction();
    command.perform(context);
    const auto after = context.undo.getNumActionsInCurrentTransaction();
    if (before == 0 && after > 0)
        recordChange(false);
    else if (after > 0)
        recordChange(true);
}

void CommandService::commit()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    check(gestureName);
}

void CommandService::cancel()
{
    if (! gestureOpen)
        return;
    gestureOpen = false;
    if (context.undo.getNumActionsInCurrentTransaction() > 0 && context.undo.undoCurrentTransactionOnly())
    {
        // The gesture's step is gone and the redo steps it had pushed aside are back.
        serials.resize(static_cast<size_t>(std::max(0, position - 1)));
        serials.insert(serials.end(), stashedSerials.begin(), stashedSerials.end());
        position = undoDepth();
    }
    context.undo.beginNewTransaction();
}

void CommandService::check(const juce::String& commandName)
{
    if (! invariantChecksEnabled)
        return;
    auto problem = findInvariantViolation(context.sampler, context.bundleDir);
    if (! problem && context.arrangement != nullptr)
        problem = findArrangementViolation(*context.arrangement, context.sampler);
    if (problem)
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

bool CommandService::canUndo() const { return ! gestureOpen && ! recordingLock && context.undo.canUndo(); }
bool CommandService::canRedo() const { return ! gestureOpen && ! recordingLock && context.undo.canRedo(); }
juce::String CommandService::undoName() const { return context.undo.getUndoDescription(); }
juce::String CommandService::redoName() const { return context.undo.getRedoDescription(); }

void CommandService::undo()
{
    if (recordingLock)
    {
        refuse();
        return;
    }
    if (! canUndo())
        return;
    context.undo.undo();
    position = undoDepth();
    lastKey = {};
    check("Undo");
}

void CommandService::redo()
{
    if (recordingLock)
    {
        refuse();
        return;
    }
    if (! canRedo())
        return;
    context.undo.redo();
    position = undoDepth();
    lastKey = {};
    check("Redo");
}

int CommandService::historyToken() const
{
    return position > 0 && position <= static_cast<int>(serials.size()) ? serials[static_cast<size_t>(position - 1)] : 0;
}
} // namespace sampler::commands
