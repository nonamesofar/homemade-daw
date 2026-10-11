#pragma once

#include "BrowserModel.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <vector>

namespace sampler::ui
{
/**
    The browser on the left: search box, the places (All samples, Loops, One-shots, Recordings) and the list of
    sounds. It draws what it is given (`setRows`) through a `BrowserModel` and reports clicks; it knows nothing about
    projects or audio.
*/
class BrowserPanel : public juce::Component, private juce::ListBoxModel
{
public:
    BrowserPanel();
    ~BrowserPanel() override;

    /** All rows of the project; the panel filters and sorts them. */
    void setRows(std::vector<BrowserRow> newRows);
    /** The rows shown now (filtered and sorted). */
    const std::vector<BrowserRow>& rows() const { return currentRows; }
    juce::StringArray rowNames() const;

    /** The row showing the "playing" indicator (empty for none). */
    void setPlayingId(const juce::String& sourceId);
    void selectRowById(const juce::String& sourceId);

    juce::TextEditor& searchBox() { return search; }
    void choosePlace(Place);
    Place place() const { return model.place(); }
    /** The count shown beside a place. */
    int countIn(Place p) const { return model.countIn(p); }
    bool showsNothingMatchesText() const { return model.nothingMatches(); }

    /**
        Makes the waveform picture of a sound. `repaintTarget` is repainted as the picture fills in. Called only for
        rows that are on screen, and the panel keeps at most `maxLiveThumbnails` pictures alive (least recently drawn goes).
    */
    using ThumbnailFactory = std::function<std::unique_ptr<juce::AudioThumbnailBase>(const juce::String& sourceId, juce::Component& repaintTarget)>;
    void setThumbnailFactory(ThumbnailFactory);
    /** Drops every picture (the project changed). */
    void clearThumbnails();
    int liveThumbnailCount() const { return static_cast<int>(thumbnails.size()); }
    static constexpr int maxLiveThumbnails = 64;

    /** Scrolls so that the row is the first one visible. */
    void scrollToRow(int row);
    int numVisibleRows() const { return list.getNumRowsOnScreen(); }

    /** Called when the user clicks a ready sound. */
    std::function<void(const juce::String& sourceId)> onRowClicked;
    /** Called when the user picks a category in a row's context menu. */
    std::function<void(const juce::String& sourceId, model::Category)> onCategoryChosen;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return static_cast<int>(currentRows.size()); }
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    juce::AudioThumbnailBase* thumbnailFor(const juce::String& sourceId);
    void applyFilter();
    void showCategoryMenu(const BrowserRow&);

    struct Thumbnail
    {
        std::unique_ptr<juce::AudioThumbnailBase> picture;
        std::list<juce::String>::iterator recency;
    };

    static juce::String tagFor(model::Category);

    static constexpr int rowHeight = 38;
    static constexpr int hintHeight = 62;
    juce::Rectangle<int> placesLabelArea, dropHintArea;
    int dividerY = 0;

    BrowserModel model;
    juce::TextEditor search;
    std::array<juce::TextButton, 4> placeButtons;
    juce::ListBox list{"sounds", this};
    ThumbnailFactory makeThumbnail;
    std::map<juce::String, Thumbnail> thumbnails;
    std::list<juce::String> recentlyDrawn; // front = most recent
    std::vector<BrowserRow> currentRows;
    juce::String playingId;
};
} // namespace sampler::ui
