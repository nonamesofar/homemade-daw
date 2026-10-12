#pragma once

#include "model/SamplerState.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace sampler::ui
{
/** One line of the browser: a sound of the open project, or one that is still being imported. */
struct BrowserRow
{
    juce::String id; // the source id (empty while importing)
    juce::String name;
    bool importing = false;
    model::Category category = model::Category::loop;
    /** Takes only: where and when they were recorded (shown in the tooltip). */
    juce::String recordedFrom, recordedAt;

    /** "Recorded from Master, 2026-10-12 10:15", or empty for a sound that was not recorded. */
    juce::String tooltip() const;
};

/** The places in the browser's list on the left. */
enum class Place
{
    all,
    loops,
    oneShots,
    recordings
};

juce::String toString(Place);

/**
    What the browser shows: all rows, narrowed by the search text and the place, sorted by name. A plain class with
    no Components, so every filter combination is unit tested. Rows that are still importing have no category yet:
    they appear under "All samples" only.
*/
class BrowserModel
{
public:
    /** Rows for every SOURCE of the tree (unsorted). */
    static std::vector<BrowserRow> rowsFrom(const model::SamplerState&);

    void setRows(std::vector<BrowserRow>);
    void setSearchText(const juce::String&);
    void setPlace(Place);

    const juce::String& searchText() const { return search; }
    Place place() const { return currentPlace; }

    /** The rows to show: filtered, then sorted by name ignoring case (stable, so equal names keep their order). */
    std::vector<BrowserRow> visibleRows() const;

    /** True when there are sounds but the search and place hide all of them. */
    bool nothingMatches() const;
    int totalRows() const { return static_cast<int>(allRows.size()); }

    /** How many rows belong to a place (importing rows count under All samples only). */
    int countIn(Place) const;

private:
    bool accepts(const BrowserRow&, Place, const juce::String& text) const;

    std::vector<BrowserRow> allRows;
    juce::String search;
    Place currentPlace = Place::all;
};
} // namespace sampler::ui
