// The browser list draws waveform pictures lazily for visible rows and keeps at most 64 alive.
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "ui/browser/BrowserPanel.h"

using sampler::ui::BrowserPanel;
using sampler::ui::BrowserRow;

namespace
{
struct Fixture
{
    Fixture()
    {
        formats.registerBasicFormats();
        panel.setSize(280, 640);
        panel.setThumbnailFactory([this](const juce::String&, juce::Component& target) {
            ++created;
            return std::make_unique<juce::AudioThumbnail>(512, formats, cache);
            (void) target;
        });
    }

    void setSources(int count)
    {
        std::vector<BrowserRow> rows;
        for (int i = 0; i < count; ++i)
            rows.push_back({"src_" + juce::String(i), "sound " + juce::String(i), false});
        panel.setRows(std::move(rows));
    }

    /** Text and button notifications are posted as messages; let them arrive. */
    static void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

    void paint()
    {
        juce::Image image(juce::Image::RGB, panel.getWidth(), panel.getHeight(), true);
        juce::Graphics g(image);
        panel.paintEntireComponent(g, true);
    }

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache{8};
    BrowserPanel panel;
    int created = 0;
};
} // namespace

TEST_CASE("only the rows on screen get a picture", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.setSources(200);
    f.paint();

    CHECK(f.panel.liveThumbnailCount() > 0);
    CHECK(f.panel.liveThumbnailCount() <= f.panel.numVisibleRows() + 2);
    CHECK(f.created == f.panel.liveThumbnailCount());
}

TEST_CASE("scrolling through 200 sounds never keeps more than 64 pictures", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.setSources(200);

    int mostAlive = 0;
    for (int row = 0; row < 200; row += 7)
    {
        f.panel.scrollToRow(row);
        f.paint();
        mostAlive = juce::jmax(mostAlive, f.panel.liveThumbnailCount());
        REQUIRE(f.panel.liveThumbnailCount() <= BrowserPanel::maxLiveThumbnails);
    }
    CHECK(f.created > BrowserPanel::maxLiveThumbnails); // 200 rows were drawn, so old pictures were dropped
    CHECK(mostAlive == BrowserPanel::maxLiveThumbnails);
}

TEST_CASE("a picture is reused while its row stays visible", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.setSources(10);
    f.paint();
    const auto createdFirst = f.created;
    f.paint();
    f.paint();
    CHECK(f.created == createdFirst);
}

TEST_CASE("pictures of removed sounds are dropped and a project change clears all", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.setSources(5);
    f.paint();
    REQUIRE(f.panel.liveThumbnailCount() == 5);

    f.setSources(3); // two imports undone
    CHECK(f.panel.liveThumbnailCount() == 3);

    f.panel.clearThumbnails();
    CHECK(f.panel.liveThumbnailCount() == 0);
}

TEST_CASE("rows that are still importing get no picture", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.panel.setRows({{"", "long file", true}, {"src_1", "kick", false}});
    f.paint();
    CHECK(f.panel.liveThumbnailCount() == 1);
}

TEST_CASE("typing in the search box and clicking a place change the rows", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.panel.setRows({{"1", "Kick 01", false, sampler::model::Category::oneShot},
                     {"2", "Snare", false, sampler::model::Category::oneShot},
                     {"3", "kick-long", false, sampler::model::Category::loop},
                     {"4", "Drum loop", false, sampler::model::Category::loop}});
    CHECK(f.panel.rowNames() == juce::StringArray{"Drum loop", "Kick 01", "kick-long", "Snare"});

    f.panel.searchBox().setText("kick", true);
    f.settle();
    CHECK(f.panel.rowNames() == juce::StringArray{"Kick 01", "kick-long"});

    f.panel.choosePlace(sampler::ui::Place::oneShots);
    CHECK(f.panel.rowNames() == juce::StringArray{"Kick 01"});

    f.panel.searchBox().setText("zzz", true);
    f.settle();
    CHECK(f.panel.rows().empty());
    CHECK(f.panel.showsNothingMatchesText());

    f.panel.searchBox().setText("", true);
    f.settle();
    f.panel.choosePlace(sampler::ui::Place::recordings);
    CHECK(f.panel.rows().empty());

    f.panel.choosePlace(sampler::ui::Place::all);
    CHECK(f.panel.rows().size() == 4);
    CHECK(f.panel.countIn(sampler::ui::Place::loops) == 2);
}

TEST_CASE("the place buttons are real buttons: a click on one chooses it", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.panel.setRows({{"1", "Kick", false, sampler::model::Category::oneShot}, {"2", "Loop", false, sampler::model::Category::loop}});
    for (auto* child : f.panel.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child); button != nullptr && button->getButtonText().startsWith("One-shots"))
            button->triggerClick();
    f.settle();
    CHECK(f.panel.place() == sampler::ui::Place::oneShots);
    CHECK(f.panel.rowNames() == juce::StringArray{"Kick"});
}

TEST_CASE("the sound list leaves Home, Enter and Delete to the app but keeps arrow navigation", "[ui][browser]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.setSources(3);
    auto& list = f.panel.listBox();
    CHECK_FALSE(list.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    CHECK_FALSE(list.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    CHECK_FALSE(list.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    CHECK_FALSE(list.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)));
    CHECK(list.keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
}
