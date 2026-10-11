#include <catch2/catch_test_macros.hpp>

#include "model/Migrations.h"
#include "model/SamplerState.h"

using namespace sampler::model;

TEST_CASE("the current schema version needs no migration", "[model][migrations]")
{
    auto node = SamplerState::createEmpty();
    CHECK(migrateToCurrent(node) == MigrationResult::upToDate);
    CHECK(SamplerState(node).schemaVersion() == currentSchemaVersion);
}

TEST_CASE("a newer schema version is reported and left alone", "[model][migrations]")
{
    auto node = SamplerState::createEmpty();
    node.setProperty(ids::schemaVersion, currentSchemaVersion + 1, nullptr);
    const auto before = node.toXmlString();
    CHECK(migrateToCurrent(node) == MigrationResult::tooNew);
    CHECK(node.toXmlString() == before);
}

TEST_CASE("nodes without a usable schema version are invalid", "[model][migrations]")
{
    juce::ValueTree noVersion(ids::SAMPLER);
    CHECK(migrateToCurrent(noVersion) == MigrationResult::invalid);

    auto zero = SamplerState::createEmpty();
    zero.setProperty(ids::schemaVersion, 0, nullptr);
    CHECK(migrateToCurrent(zero) == MigrationResult::invalid);

    juce::ValueTree other("OTHER");
    CHECK(migrateToCurrent(other) == MigrationResult::invalid);
}
