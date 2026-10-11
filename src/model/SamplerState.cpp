#include "SamplerState.h"

namespace sampler::model
{
juce::String toString(Category category)
{
    switch (category)
    {
        case Category::loop: return "loop";
        case Category::oneShot: return "one-shot";
        case Category::recording: return "recording";
    }
    return "loop";
}

Category categoryFromString(const juce::String& text, Category fallback)
{
    if (text == "loop")
        return Category::loop;
    if (text == "one-shot")
        return Category::oneShot;
    if (text == "recording")
        return Category::recording;
    return fallback;
}

juce::ValueTree Source::create(const SourceData& data)
{
    juce::ValueTree node(ids::SOURCE);
    node.setProperty(ids::id, data.id, nullptr);
    node.setProperty(ids::name, data.name, nullptr);
    node.setProperty(ids::original, data.original, nullptr);
    node.setProperty(ids::decoded, data.decoded, nullptr);
    node.setProperty(ids::originalPath, data.originalPath, nullptr);
    node.setProperty(ids::hash, data.hash, nullptr);
    node.setProperty(ids::sampleRate, data.sampleRate, nullptr);
    node.setProperty(ids::channels, data.channels, nullptr);
    node.setProperty(ids::frames, data.frames, nullptr);
    node.setProperty(ids::category, toString(data.category), nullptr);
    return node;
}

void Source::setCategory(Category category, juce::UndoManager* undo)
{
    state.setProperty(ids::category, toString(category), undo);
}

juce::ValueTree SamplerState::createEmpty()
{
    juce::ValueTree node(ids::SAMPLER);
    node.setProperty(ids::schemaVersion, currentSchemaVersion, nullptr);
    return node;
}

int SamplerState::numSources() const
{
    int count = 0;
    for (const auto& child : state)
        if (child.hasType(ids::SOURCE))
            ++count;
    return count;
}

Source SamplerState::sourceAt(int index) const
{
    int seen = 0;
    for (const auto& child : state)
        if (child.hasType(ids::SOURCE) && seen++ == index)
            return Source(child);
    return {};
}

Source SamplerState::findSourceById(const juce::String& id) const
{
    for (const auto& child : state)
        if (child.hasType(ids::SOURCE) && child[ids::id].toString() == id)
            return Source(child);
    return {};
}

Source SamplerState::findSourceByHash(const juce::String& hash) const
{
    for (const auto& child : state)
        if (child.hasType(ids::SOURCE) && child[ids::hash].toString() == hash)
            return Source(child);
    return {};
}

juce::String makeSourceId(const juce::String& hash, const std::function<bool(const juce::String& id)>& isTakenByOther)
{
    const auto hex = hash.fromFirstOccurrenceOf(":", false, false);
    for (int length = 8; length <= hex.length(); ++length)
    {
        const auto candidate = "src_" + hex.substring(0, length);
        if (! isTakenByOther(candidate))
            return candidate;
    }
    // Every prefix is taken (only possible if the hash itself is not unique): number it.
    for (int n = 2;; ++n)
    {
        const auto candidate = "src_" + hex + "_" + juce::String(n);
        if (! isTakenByOther(candidate))
            return candidate;
    }
}

juce::String SamplerState::makeSourceId(const juce::String& hash) const
{
    return model::makeSourceId(hash, [this, &hash](const juce::String& candidate) {
        const auto existing = findSourceById(candidate);
        return existing.isValid() && existing.hash() != hash;
    });
}

bool SamplerState::addSource(const juce::ValueTree& sourceNode, juce::UndoManager* undo)
{
    const auto id = sourceNode[ids::id].toString();
    if (id.isEmpty() || findSourceById(id).isValid())
        return false;
    state.appendChild(sourceNode, undo);
    return true;
}
} // namespace sampler::model
