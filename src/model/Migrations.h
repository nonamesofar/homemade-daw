#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace sampler::model
{
enum class MigrationResult
{
    upToDate, // already the current schema version
    migrated, // upgraded in memory to the current version
    tooNew,   // written by a newer version of the app: must not be opened
    invalid   // not a SAMPLER node, or no usable schema version
};

/**
    Brings a SAMPLER node up to `currentSchemaVersion` by running migrate_vN_to_vN1 steps in order. Version 1 is the
    first, so the chain has no steps yet; each breaking change adds a step here with a fixture test.
*/
MigrationResult migrateToCurrent(juce::ValueTree& samplerNode);
} // namespace sampler::model
