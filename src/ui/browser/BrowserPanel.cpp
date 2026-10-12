#include "BrowserPanel.h"

#include "ui/look/Fonts.h"
#include "ui/look/SamplerLookAndFeel.h"
#include "ui/look/Theme.h"

#include <algorithm>

namespace sampler::ui
{
BrowserPanel::BrowserPanel()
{
    search.setTextToShowWhenEmpty("Search samples", theme::muted);
    search.setFont(Fonts::sans(13.0f));
    search.setIndents(12, 11);
    search.setBorder({0, 0, 0, 0});
    search.setSelectAllWhenFocused(true);
    search.onTextChange = [this] {
        model.setSearchText(search.getText());
        applyFilter();
    };
    addAndMakeVisible(search);

    for (size_t i = 0; i < placeButtons.size(); ++i)
    {
        auto& button = placeButtons[i];
        button.setRadioGroupId(1);
        button.setClickingTogglesState(true);
        button.getProperties().set(SamplerLookAndFeel::listEntryProperty, true);
        button.onClick = [this, i] { choosePlace(static_cast<Place>(i)); };
        addAndMakeVisible(button);
    }
    placeButtons[0].setToggleState(true, juce::dontSendNotification);

    list.setRowHeight(rowHeight);
    list.setRowSelectedOnMouseDown(false); // a click acts on mouse up, so dragging a row to the timeline does not audition it
    list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(list);
}

BrowserPanel::~BrowserPanel() { list.setModel(nullptr); }

void BrowserPanel::setRows(std::vector<BrowserRow> newRows)
{
    model.setRows(std::move(newRows));
    applyFilter();
}

void BrowserPanel::choosePlace(Place p)
{
    model.setPlace(p);
    placeButtons[static_cast<size_t>(p)].setToggleState(true, juce::dontSendNotification);
    applyFilter();
}

void BrowserPanel::applyFilter()
{
    currentRows = model.visibleRows();
    for (size_t i = 0; i < placeButtons.size(); ++i)
        placeButtons[i].setButtonText(toString(static_cast<Place>(i)) + "   " + juce::String(model.countIn(static_cast<Place>(i))));
    // Pictures of sounds that are gone (undo of an import) are dropped.
    for (auto it = thumbnails.begin(); it != thumbnails.end();)
    {
        const auto id = it->first;
        if (std::none_of(currentRows.begin(), currentRows.end(), [&id](const BrowserRow& r) { return r.id == id; }))
        {
            recentlyDrawn.erase(it->second.recency);
            it = thumbnails.erase(it);
        }
        else
        {
            ++it;
        }
    }
    list.updateContent();
    list.repaint();
    repaint();
}

juce::StringArray BrowserPanel::rowNames() const
{
    juce::StringArray names;
    for (const auto& row : currentRows)
        names.add(row.name);
    return names;
}

void BrowserPanel::setPlayingId(const juce::String& sourceId)
{
    if (playingId == sourceId)
        return;
    playingId = sourceId;
    list.repaint();
}

void BrowserPanel::selectRowById(const juce::String& sourceId)
{
    for (size_t i = 0; i < currentRows.size(); ++i)
        if (currentRows[i].id == sourceId)
        {
            list.selectRow(static_cast<int>(i));
            return;
        }
}

void BrowserPanel::setThumbnailFactory(ThumbnailFactory factory)
{
    clearThumbnails();
    makeThumbnail = std::move(factory);
}

void BrowserPanel::clearThumbnails()
{
    thumbnails.clear();
    recentlyDrawn.clear();
    list.repaint();
}

void BrowserPanel::scrollToRow(int row)
{
    if (auto* viewport = list.getViewport())
        viewport->setViewPosition(0, row * list.getRowHeight());
}

juce::String BrowserPanel::tagFor(model::Category category)
{
    switch (category)
    {
        case model::Category::loop: return "loop";
        case model::Category::oneShot: return "one-shot";
        case model::Category::recording: return "recording";
    }
    return {};
}

juce::AudioThumbnailBase* BrowserPanel::thumbnailFor(const juce::String& sourceId)
{
    if (! makeThumbnail || sourceId.isEmpty())
        return nullptr;

    if (const auto it = thumbnails.find(sourceId); it != thumbnails.end())
    {
        recentlyDrawn.splice(recentlyDrawn.begin(), recentlyDrawn, it->second.recency);
        return it->second.picture.get();
    }

    auto picture = makeThumbnail(sourceId, list);
    if (picture == nullptr)
        return nullptr;
    recentlyDrawn.push_front(sourceId);
    auto* raw = picture.get();
    thumbnails[sourceId] = {std::move(picture), recentlyDrawn.begin()};

    while (static_cast<int>(thumbnails.size()) > maxLiveThumbnails)
    {
        thumbnails.erase(recentlyDrawn.back());
        recentlyDrawn.pop_back();
    }
    return raw;
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds().withTrimmedRight(1);
    area.removeFromTop(16);
    search.setBounds(area.removeFromTop(40).reduced(16, 0));
    area.removeFromTop(14);
    placesLabelArea = area.removeFromTop(20).withTrimmedLeft(16);
    for (auto& button : placeButtons)
        button.setBounds(area.removeFromTop(34).reduced(8, 0));
    area.removeFromTop(14);
    dividerY = area.getY();
    area.removeFromTop(11);
    dropHintArea = area.removeFromBottom(hintHeight + 16).withTrimmedBottom(16).reduced(16, 0);
    area.removeFromBottom(10);
    list.setBounds(area);
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
    g.setColour(theme::border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));

    auto label = Fonts::sans(10.0f);
    label.setExtraKerningFactor(0.08f);
    g.setColour(theme::muted);
    g.setFont(label);
    g.drawText("PLACES", placesLabelArea, juce::Justification::centredLeft);
    g.drawHorizontalLine(dividerY, 16.0f, static_cast<float>(getWidth() - 17));

    if (currentRows.empty() && model.nothingMatches())
    {
        g.setFont(Fonts::sans(13.0f));
        g.drawText("Nothing matches", list.getBounds(), juce::Justification::centred);
    }

    const auto box = dropHintArea.toFloat().reduced(0.5f);
    juce::Path outline;
    outline.addRoundedRectangle(box, 8.0f);
    juce::Path dashedOutline;
    const float dashes[] = {4.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashedOutline, outline, dashes, 2);
    g.setColour(theme::dashed);
    g.fillPath(dashedOutline);
    g.setColour(theme::muted);
    g.setFont(Fonts::sans(13.0f));
    g.drawFittedText("Drop audio here to start sampling",
                     dropHintArea.reduced(12, 8), juce::Justification::centred, 2);
}

void BrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow(row, static_cast<int>(currentRows.size())))
        return;
    const auto& item = currentRows[static_cast<size_t>(row)];

    if (selected)
    {
        g.setColour(theme::selected);
        g.fillRect(0, 0, width, height);
    }

    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(16, 0);
    const bool playing = ! item.importing && item.id == playingId && playingId.isNotEmpty();
    auto iconSlot = area.removeFromLeft(14);
    if (playing)
    {
        juce::Path triangle;
        const auto c = iconSlot.toFloat().getCentre();
        triangle.addTriangle(c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
        g.setColour(theme::accent);
        g.fillPath(triangle);
    }
    area.removeFromLeft(8);

    // The tag on the right: the category, or "importing" while the sound is not ready.
    const auto tag = item.importing ? juce::String("importing") : tagFor(item.category);
    g.setColour(theme::muted);
    g.setFont(Fonts::sans(11.0f));
    auto tagArea = area.removeFromRight(juce::jmax(46, juce::GlyphArrangement::getStringWidthInt(Fonts::sans(11.0f), tag) + 4));
    g.drawText(tag, tagArea, juce::Justification::centredRight);
    area.removeFromRight(8);

    if (! item.importing)
    {
        if (auto* picture = thumbnailFor(item.id))
        {
            auto pictureArea = area.removeFromLeft(48).reduced(0, 12);
            g.setColour(playing ? theme::accent : theme::muted);
            picture->drawChannels(g, pictureArea, 0.0, picture->getTotalLength(), 1.0f);
            area.removeFromLeft(10);
        }
    }

    g.setColour(item.importing ? theme::muted : theme::text);
    g.setFont(Fonts::sans(13.0f));
    g.drawText(item.name, area, juce::Justification::centredLeft, true);
}

juce::String BrowserPanel::getTooltipForRow(int row)
{
    return juce::isPositiveAndBelow(row, static_cast<int>(currentRows.size())) ? currentRows[static_cast<size_t>(row)].tooltip() : juce::String();
}

juce::var BrowserPanel::getDragSourceDescription(const juce::SparseSet<int>& rowsToDrag)
{
    if (rowsToDrag.isEmpty())
        return {};
    const auto row = rowsToDrag[0];
    if (! juce::isPositiveAndBelow(row, static_cast<int>(currentRows.size())))
        return {};
    const auto& r = currentRows[static_cast<size_t>(row)];
    return r.importing || r.id.isEmpty() ? juce::var() : juce::var("sample:" + r.id);
}

void BrowserPanel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow(row, static_cast<int>(currentRows.size())))
        return;
    const auto& item = currentRows[static_cast<size_t>(row)];
    if (item.importing)
        return;
    if (e.mods.isPopupMenu())
        showCategoryMenu(item);
    else if (onRowClicked)
        onRowClicked(item.id);
}

void BrowserPanel::showCategoryMenu(const BrowserRow& item)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Loop", true, item.category == model::Category::loop);
    menu.addItem(2, "One-shot", true, item.category == model::Category::oneShot);
    menu.addItem(3, "Recording", true, item.category == model::Category::recording);
    menu.showMenuAsync({}, [this, id = item.id](int choice) {
        if (choice != 0 && onCategoryChosen)
            onCategoryChosen(id, choice == 1 ? model::Category::loop : choice == 2 ? model::Category::oneShot : model::Category::recording);
    });
}
} // namespace sampler::ui
