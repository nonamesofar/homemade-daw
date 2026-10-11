#include "Migrations.h"

#include "SamplerState.h"

#include <vector>

namespace sampler::model
{
namespace
{
using Step = void (*)(juce::ValueTree&);

/** steps[i] upgrades schema version i + 1 to i + 2. Empty until schema 2 exists. */
const std::vector<Step>& steps()
{
    static const std::vector<Step> chain;
    return chain;
}
} // namespace

MigrationResult migrateToCurrent(juce::ValueTree& samplerNode)
{
    if (! samplerNode.isValid() || ! samplerNode.hasType(ids::SAMPLER) || ! samplerNode.hasProperty(ids::schemaVersion))
        return MigrationResult::invalid;

    int version = samplerNode[ids::schemaVersion];
    if (version < 1)
        return MigrationResult::invalid;
    if (version > currentSchemaVersion)
        return MigrationResult::tooNew;
    if (version == currentSchemaVersion)
        return MigrationResult::upToDate;

    while (version < currentSchemaVersion)
    {
        steps().at(static_cast<size_t>(version - 1))(samplerNode);
        ++version;
        samplerNode.setProperty(ids::schemaVersion, version, nullptr);
    }
    return MigrationResult::migrated;
}
} // namespace sampler::model
