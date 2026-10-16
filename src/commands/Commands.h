#pragma once

#include "CommandService.h"
#include "model/SamplerState.h"

#include <vector>

namespace sampler::commands
{
/**
    Adds imported sounds to the project as one undo step ("Import kick.wav", "Import 3 files"). The files are already
    in the bundle; this only records them. A source whose id is already in the project is skipped.
*/
class AddSources final : public Command
{
public:
    explicit AddSources(std::vector<model::SourceData> sources) : items(std::move(sources)) {}

    juce::String name() const override;
    void perform(ModelContext&) override;

private:
    std::vector<model::SourceData> items;
};

/** Changes the category of one sound ("Set category: One-shot"). An unknown id or an unchanged category does nothing. */
class SetSourceCategory final : public Command
{
public:
    SetSourceCategory(juce::String sourceId, model::Category newCategory) : id(std::move(sourceId)), category(newCategory) {}

    juce::String name() const override;
    void perform(ModelContext&) override;

private:
    juce::String id;
    model::Category category;
};

/**
    The user's tempo for a sound ("Set sound tempo"): typed, halved or doubled. Kept in the project (the analysis result
    is cache data), so it survives a deleted cache. Values are clamped to 20..999 BPM; an unknown sound or an unchanged
    tempo does nothing.
*/
class SetSourceTempo final : public Command
{
public:
    SetSourceTempo(juce::String sourceId, double newBpm) : id(std::move(sourceId)), bpm(newBpm) {}

    juce::String name() const override { return "Set sound tempo"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    double bpm;
};

/**
    Moves the first beat ("1.1.1") of a sound to a frame ("Move first beat"). Meant for a gesture: `begin("Move first
    beat")`, an `update` per mouse move, `commit` on mouse-up. The frame is clamped to the sound.
*/
class MoveFirstBeat final : public Command
{
public:
    MoveFirstBeat(juce::String sourceId, juce::int64 newFrame) : id(std::move(sourceId)), frame(newFrame) {}

    juce::String name() const override { return "Move first beat"; }
    void perform(ModelContext&) override;

private:
    juce::String id;
    juce::int64 frame;
};
} // namespace sampler::commands
