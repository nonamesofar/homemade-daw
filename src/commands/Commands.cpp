#include "Commands.h"

namespace sampler::commands
{
juce::String AddSources::name() const
{
    if (items.size() == 1)
    {
        // The user's file name with extension, as they know it; the display name if the origin is unknown.
        const auto& origin = items.front().originalPath;
        const auto fileName = juce::File::isAbsolutePath(origin) ? juce::File(origin).getFileName() : juce::String();
        return "Import " + (fileName.isNotEmpty() ? fileName : items.front().name);
    }
    return "Import " + juce::String(static_cast<int>(items.size())) + " files";
}

void AddSources::perform(ModelContext& context)
{
    model::SamplerState state(context.sampler);
    for (const auto& item : items)
        state.addSource(model::Source::create(item), &context.undo);
}

juce::String SetSourceCategory::name() const
{
    return "Set category: " + juce::String(category == model::Category::oneShot ? "One-shot" : category == model::Category::recording ? "Recording" : "Loop");
}

void SetSourceCategory::perform(ModelContext& context)
{
    auto source = model::SamplerState(context.sampler).findSourceById(id);
    if (source.isValid() && source.category() != category)
        source.setCategory(category, &context.undo);
}
} // namespace sampler::commands
