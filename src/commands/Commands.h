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
} // namespace sampler::commands
