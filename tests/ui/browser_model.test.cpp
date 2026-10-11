// The browser's filtering and sorting on plain data: search text, place, name order.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "ui/browser/BrowserModel.h"

using sampler::model::Category;
using sampler::ui::BrowserModel;
using sampler::ui::BrowserRow;
using sampler::ui::Place;

namespace
{
juce::StringArray names(const std::vector<BrowserRow>& rows)
{
    juce::StringArray result;
    for (const auto& r : rows)
        result.add(r.name);
    return result;
}

BrowserModel sample()
{
    BrowserModel model;
    model.setRows({{"1", "Kick 01", false, Category::oneShot},
                   {"2", "Snare", false, Category::oneShot},
                   {"3", "kick-long", false, Category::loop},
                   {"4", "Drum loop", false, Category::loop},
                   {"5", "rec-1", false, Category::recording},
                   {"", "big file", true, Category::loop}});
    return model;
}
} // namespace

TEST_CASE("all rows are shown sorted by name ignoring case", "[ui][browser][model]")
{
    const auto model = sample();
    CHECK(names(model.visibleRows()) == juce::StringArray{"big file", "Drum loop", "Kick 01", "kick-long", "rec-1", "Snare"});
    CHECK_FALSE(model.nothingMatches());
}

TEST_CASE("search keeps names that contain the text, ignoring case", "[ui][browser][model]")
{
    auto model = sample();
    model.setSearchText("kick");
    CHECK(names(model.visibleRows()) == juce::StringArray{"Kick 01", "kick-long"});
    model.setSearchText("KICK-");
    CHECK(names(model.visibleRows()) == juce::StringArray{"kick-long"});
    model.setSearchText("");
    CHECK(model.visibleRows().size() == 6);
}

TEST_CASE("each place shows its category; sounds still importing show under All samples only", "[ui][browser][model]")
{
    auto model = sample();
    model.setPlace(Place::oneShots);
    CHECK(names(model.visibleRows()) == juce::StringArray{"Kick 01", "Snare"});
    model.setPlace(Place::loops);
    CHECK(names(model.visibleRows()) == juce::StringArray{"Drum loop", "kick-long"});
    model.setPlace(Place::recordings);
    CHECK(names(model.visibleRows()) == juce::StringArray{"rec-1"});
    model.setPlace(Place::all);
    CHECK(model.visibleRows().size() == 6);

    CHECK(model.countIn(Place::all) == 6);
    CHECK(model.countIn(Place::loops) == 2);
    CHECK(model.countIn(Place::oneShots) == 2);
    CHECK(model.countIn(Place::recordings) == 1);
}

TEST_CASE("search and place combine", "[ui][browser][model]")
{
    auto model = sample();
    model.setPlace(Place::loops);
    model.setSearchText("kick");
    CHECK(names(model.visibleRows()) == juce::StringArray{"kick-long"});
    model.setPlace(Place::oneShots);
    CHECK(names(model.visibleRows()) == juce::StringArray{"Kick 01"});
    model.setPlace(Place::recordings);
    CHECK(model.visibleRows().empty());
    CHECK(model.nothingMatches());
}

TEST_CASE("an empty project is not 'nothing matches'; hidden sounds are", "[ui][browser][model]")
{
    BrowserModel model;
    model.setSearchText("x");
    CHECK(model.visibleRows().empty());
    CHECK_FALSE(model.nothingMatches()); // the drop hint shows instead

    auto full = sample();
    full.setSearchText("zzz");
    CHECK(full.nothingMatches());
}

TEST_CASE("rows come from the SOURCE nodes of the tree with their categories", "[ui][browser][model]")
{
    auto tree = sampler::model::SamplerState::createEmpty();
    sampler::model::SamplerState state(tree);
    for (int i = 0; i < 3; ++i)
    {
        sampler::model::SourceData d;
        d.id = "src_" + juce::String(i);
        d.name = "s" + juce::String(i);
        d.category = i == 1 ? Category::oneShot : Category::loop;
        state.addSource(sampler::model::Source::create(d), nullptr);
    }
    const auto rows = BrowserModel::rowsFrom(state);
    REQUIRE(rows.size() == 3);
    CHECK(rows[1].id == "src_1");
    CHECK(rows[1].category == Category::oneShot);
    CHECK_FALSE(rows[1].importing);
}

TEST_CASE("filtering 1,000 sounds takes under 5 ms", "[ui][browser][model]")
{
    BrowserModel model;
    std::vector<BrowserRow> rows;
    for (int i = 0; i < 1000; ++i)
        rows.push_back({"src_" + juce::String(i), (i % 3 == 0 ? "Kick " : "Pad ") + juce::String((i * 7919) % 1000), false,
                        i % 2 == 0 ? Category::oneShot : Category::loop});
    model.setRows(std::move(rows));
    model.setPlace(Place::oneShots);

    double worst = 0.0;
    for (const char* text : {"k", "ki", "kic", "kick", "kick 1"})
    {
        model.setSearchText(text);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto visible = model.visibleRows();
        worst = juce::jmax(worst, juce::Time::getMillisecondCounterHiRes() - t0);
        CHECK_FALSE(visible.empty());
    }
    INFO("worst filter time " << worst << " ms");
    CHECK(worst < 5.0);
}
