#include "BrowserModel.h"

#include <algorithm>

namespace sampler::ui
{
juce::String toString(Place place)
{
    switch (place)
    {
        case Place::all: return "All samples";
        case Place::loops: return "Loops";
        case Place::oneShots: return "One-shots";
        case Place::recordings: return "Recordings";
    }
    return "All samples";
}

std::vector<BrowserRow> BrowserModel::rowsFrom(const model::SamplerState& state)
{
    std::vector<BrowserRow> rows;
    for (const auto& child : state.node())
        if (child.hasType(model::ids::SOURCE))
        {
            const model::Source source(child);
            rows.push_back({source.id(), source.name(), false, source.category()});
        }
    return rows;
}

void BrowserModel::setRows(std::vector<BrowserRow> rows) { allRows = std::move(rows); }
void BrowserModel::setSearchText(const juce::String& text) { search = text; }
void BrowserModel::setPlace(Place place) { currentPlace = place; }

bool BrowserModel::accepts(const BrowserRow& row, Place place, const juce::String& text) const
{
    if (text.isNotEmpty() && ! row.name.containsIgnoreCase(text))
        return false;
    if (place == Place::all)
        return true;
    if (row.importing)
        return false;
    switch (place)
    {
        case Place::loops: return row.category == model::Category::loop;
        case Place::oneShots: return row.category == model::Category::oneShot;
        case Place::recordings: return row.category == model::Category::recording;
        case Place::all: break;
    }
    return true;
}

std::vector<BrowserRow> BrowserModel::visibleRows() const
{
    std::vector<BrowserRow> rows;
    for (const auto& row : allRows)
        if (accepts(row, currentPlace, search))
            rows.push_back(row);
    std::stable_sort(rows.begin(), rows.end(), [](const BrowserRow& a, const BrowserRow& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return rows;
}

bool BrowserModel::nothingMatches() const
{
    return ! allRows.empty() && std::none_of(allRows.begin(), allRows.end(), [this](const BrowserRow& r) { return accepts(r, currentPlace, search); });
}

int BrowserModel::countIn(Place place) const
{
    return static_cast<int>(std::count_if(allRows.begin(), allRows.end(), [&](const BrowserRow& r) { return accepts(r, place, {}); }));
}
} // namespace sampler::ui
