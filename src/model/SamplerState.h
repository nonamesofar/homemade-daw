#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace sampler::model
{
/** Property and node names of the SAMPLER subtree that lives inside the Tracktion edit. */
namespace ids
{
#define SAMPLER_MODEL_ID(name) inline const juce::Identifier name(#name)
SAMPLER_MODEL_ID(SAMPLER);
SAMPLER_MODEL_ID(SOURCE);
SAMPLER_MODEL_ID(schemaVersion);
SAMPLER_MODEL_ID(id);
SAMPLER_MODEL_ID(name);
SAMPLER_MODEL_ID(original);
SAMPLER_MODEL_ID(decoded);
SAMPLER_MODEL_ID(originalPath);
SAMPLER_MODEL_ID(hash);
SAMPLER_MODEL_ID(sampleRate);
SAMPLER_MODEL_ID(channels);
SAMPLER_MODEL_ID(frames);
SAMPLER_MODEL_ID(category);
SAMPLER_MODEL_ID(recordedFrom);
SAMPLER_MODEL_ID(recordedAt);
#undef SAMPLER_MODEL_ID
} // namespace ids

/** The schema version this build writes and the newest one it can read. Bump on a breaking change (with a migration). */
constexpr int currentSchemaVersion = 1;

enum class Category
{
    loop,
    oneShot,
    recording
};

/** Stored form: "loop", "one-shot", "recording". */
juce::String toString(Category);
Category categoryFromString(const juce::String&, Category fallback = Category::loop);

/** Plain values of one imported sound; `Source::create` turns them into a SOURCE node. */
struct SourceData
{
    juce::String id;
    juce::String name;         // display name, the file name without extension
    juce::String original;     // path of the copy of the user's file, relative to the bundle
    juce::String decoded;      // path of the decoded 32-bit float WAV, relative to the bundle
    juce::String originalPath; // where the user's file was when imported (informational only)
    juce::String hash;         // "sha256:<hex>" of the user's file bytes
    double sampleRate = 0.0;
    int channels = 0;
    juce::int64 frames = 0;
    Category category = Category::loop;
    juce::String recordedFrom; // takes only: "System audio (Speakers)", "Master" or a track name
    juce::String recordedAt;   // takes only: ISO 8601 local time
};

/** Thin wrapper over one SOURCE node. Holds no state of its own; copies refer to the same node. */
class Source
{
public:
    Source() = default;
    explicit Source(juce::ValueTree node) : state(std::move(node)) {}

    static juce::ValueTree create(const SourceData&);

    bool isValid() const { return state.isValid() && state.hasType(ids::SOURCE); }
    const juce::ValueTree& node() const { return state; }

    juce::String id() const { return state[ids::id].toString(); }
    juce::String name() const { return state[ids::name].toString(); }
    juce::String original() const { return state[ids::original].toString(); }
    juce::String decoded() const { return state[ids::decoded].toString(); }
    juce::String originalPath() const { return state[ids::originalPath].toString(); }
    juce::String hash() const { return state[ids::hash].toString(); }
    double sampleRate() const { return state[ids::sampleRate]; }
    int channels() const { return state[ids::channels]; }
    juce::int64 frames() const { return state[ids::frames]; }
    Category category() const { return categoryFromString(state[ids::category].toString()); }
    juce::String recordedFrom() const { return state[ids::recordedFrom].toString(); }
    juce::String recordedAt() const { return state[ids::recordedAt].toString(); }
    double lengthSeconds() const { return sampleRate() > 0.0 ? static_cast<double>(frames()) / sampleRate() : 0.0; }

    void setCategory(Category, juce::UndoManager*);

private:
    juce::ValueTree state;
};

/**
    Wrapper over the SAMPLER node. The node itself is the document: this class only reads and edits it, so any number
    of wrappers over the same tree stay consistent.
*/
class SamplerState
{
public:
    explicit SamplerState(juce::ValueTree samplerNode) : state(std::move(samplerNode)) {}

    /** A new SAMPLER node with the current schema version and no sources. */
    static juce::ValueTree createEmpty();

    const juce::ValueTree& node() const { return state; }
    bool isValid() const { return state.isValid() && state.hasType(ids::SAMPLER); }

    int schemaVersion() const { return state[ids::schemaVersion]; }

    int numSources() const;
    Source sourceAt(int index) const;
    Source findSourceById(const juce::String& id) const;
    Source findSourceByHash(const juce::String& hash) const;

    /**
        An id for a new source: "src_" plus the first 8 hex characters of the hash (after "sha256:"), extended one
        character at a time while another source (with a different hash) already uses it.
    */
    juce::String makeSourceId(const juce::String& hash) const;

    /** Adds a SOURCE node at the end (a no-op returning false if the id is empty or already in use). */
    bool addSource(const juce::ValueTree& sourceNode, juce::UndoManager*);

private:
    juce::ValueTree state;
};

/** The id rule on its own: `isTakenByOther(id)` says whether another source already uses that id. */
juce::String makeSourceId(const juce::String& hash, const std::function<bool(const juce::String& id)>& isTakenByOther);
} // namespace sampler::model
