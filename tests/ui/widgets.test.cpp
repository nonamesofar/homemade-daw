// The look: fonts from the program's own data, and the widget maths and behaviour on plain values.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "ui/look/Fonts.h"
#include "ui/look/SamplerLookAndFeel.h"
#include "ui/widgets/WidgetGallery.h"
#include "ui/widgets/WidgetMath.h"

using Catch::Approx;
using namespace sampler::ui;

TEST_CASE("the IBM Plex typefaces load from the data inside the program", "[ui][look]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto weight : {Fonts::Weight::regular, Fonts::Weight::medium, Fonts::Weight::semiBold})
    {
        const auto face = Fonts::sansTypeface(weight);
        REQUIRE(face != nullptr);
        CHECK(face->getName().containsIgnoreCase("Plex"));
        CHECK(face->getName().containsIgnoreCase("Sans"));
    }
    for (const bool medium : {false, true})
    {
        const auto face = Fonts::monoTypeface(medium);
        REQUIRE(face != nullptr);
        CHECK(face->getName().containsIgnoreCase("Mono"));
    }
    // A font made from it measures text (the glyphs are really there) and the look-and-feel uses it by default.
    CHECK(juce::GlyphArrangement::getStringWidthInt(Fonts::sans(13.0f), "Sampler") > 20);
    SamplerLookAndFeel look;
    const auto fallback = look.getTypefaceForFont(juce::Font(juce::FontOptions(14.0f)));
    REQUIRE(fallback != nullptr);
    CHECK(fallback->getName().containsIgnoreCase("Plex"));
}

TEST_CASE("value ranges map to 0..1 and back, straight or logarithmic", "[ui][widgets]")
{
    const ValueRange db{-60.0, 6.0, 0.0, 0.1, false};
    CHECK(db.toNormalised(-60.0) == Approx(0.0));
    CHECK(db.toNormalised(6.0) == Approx(1.0));
    CHECK(db.toNormalised(100.0) == Approx(1.0)); // clamped
    CHECK(db.fromNormalised(0.5) == Approx(-27.0));
    CHECK(db.fromNormalised(0.123456) == Approx(-51.9)); // snapped to 0.1

    const ValueRange hz{20.0, 20000.0, 1000.0, 0.0, true};
    CHECK(hz.toNormalised(20.0) == Approx(0.0));
    CHECK(hz.toNormalised(20000.0) == Approx(1.0));
    CHECK(hz.toNormalised(632.4555) == Approx(0.5).margin(0.001)); // the geometric middle
    CHECK(hz.fromNormalised(0.5) == Approx(632.4555).margin(0.01));
}

TEST_CASE("a step snaps counted from the minimum", "[ui][widgets]")
{
    const ValueRange bars{1.0, 8.0, 4.0, 1.0, false};
    CHECK(bars.snap(3.4) == 3.0);
    CHECK(bars.snap(3.6) == 4.0);
    CHECK(bars.snap(0.0) == 1.0);
    CHECK(bars.snap(12.0) == 8.0);
}

TEST_CASE("dragging up raises the value; shift makes it ten times finer; the ends hold", "[ui][widgets]")
{
    CHECK(normalisedAfterDrag(0.5, 40.0, 160.0, false) == Approx(0.75));
    CHECK(normalisedAfterDrag(0.5, -40.0, 160.0, false) == Approx(0.25));
    CHECK(normalisedAfterDrag(0.5, 40.0, 160.0, true) == Approx(0.525));
    CHECK(normalisedAfterDrag(0.9, 100.0, 160.0, false) == Approx(1.0));
    CHECK(normalisedAfterDrag(0.1, -100.0, 160.0, false) == Approx(0.0));
    CHECK(normalisedAfterDrag(0.4, 50.0, 0.0, false) == Approx(0.4)); // no travel: unchanged
}

TEST_CASE("meter maths", "[ui][widgets]")
{
    CHECK(dbToFraction(0.0) == Approx(1.0));
    CHECK(dbToFraction(-30.0) == Approx(0.5));
    CHECK(dbToFraction(-60.0) == Approx(0.0));
    CHECK(dbToFraction(-90.0) == Approx(0.0));
    CHECK(dbToFraction(6.0) == Approx(1.0));
    CHECK(gainToDb(1.0) == Approx(0.0));
    CHECK(gainToDb(0.5) == Approx(-6.0206).margin(0.001));
    CHECK(gainToDb(0.0) == -120.0);
}

TEST_CASE("the peak is held, then falls at a fixed speed", "[ui][widgets]")
{
    PeakHold hold(500.0, 20.0);
    CHECK(hold.update(-10.0, 33.0) == Approx(-10.0));
    CHECK(hold.update(-40.0, 300.0) == Approx(-10.0)); // still held
    CHECK(hold.update(-40.0, 300.0) == Approx(-12.0).margin(0.01)); // 600 ms: 100 ms past the hold falls 2 dB
    hold.update(-40.0, 5000.0);
    CHECK(hold.peakDb() == Approx(-40.0)); // never below the live level
    CHECK(hold.update(-5.0, 33.0) == Approx(-5.0)); // a new high jumps up at once
}

TEST_CASE("a click lands in the right segment of a segmented control", "[ui][widgets]")
{
    CHECK(segmentAt(0.0, 200.0, 2) == 0);
    CHECK(segmentAt(99.9, 200.0, 2) == 0);
    CHECK(segmentAt(100.0, 200.0, 2) == 1);
    CHECK(segmentAt(199.9, 200.0, 2) == 1);
    CHECK(segmentAt(-1.0, 200.0, 2) == -1);
    CHECK(segmentAt(200.0, 200.0, 2) == -1);
    CHECK(segmentAt(10.0, 200.0, 0) == -1);
}

TEST_CASE("segmented control, toggle, value field and pad react to the user", "[ui][widgets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    SegmentedControl segmented;
    segmented.setSize(200, 40);
    segmented.setItems({"Session", "Arrange"});
    int chosen = -1;
    segmented.onChange = [&chosen](int i) { chosen = i; };
    CHECK(segmented.itemAt({150, 10}) == 1);
    segmented.setSelected(1, true);
    CHECK(chosen == 1);
    CHECK(segmented.selected() == 1);

    ValueField field;
    field.setRange({20.0, 240.0, 120.0, 0.01, false});
    field.setSuffix(" bpm", 1);
    double lastValue = 0.0;
    field.onChange = [&lastValue](double v) { lastValue = v; };
    CHECK(field.commitText("96.5 bpm"));
    CHECK(lastValue == Approx(96.5));
    CHECK(field.text() == "96.5 bpm");
    CHECK_FALSE(field.commitText("fast"));
    CHECK(field.getValue() == Approx(96.5)); // a bad entry keeps the value
    CHECK(field.commitText("1000"));
    CHECK(field.getValue() == Approx(240.0)); // clamped

    Pad pad("1");
    float velocity = 0.0f;
    pad.onTrigger = [&velocity](float v) { velocity = v; };
    pad.trigger(0.8f);
    CHECK(velocity == Approx(0.8f));
    CHECK(pad.flash() == Approx(0.8f));

    Knob knob;
    knob.setRange({0.0, 1.0, 0.25, 0.0, false});
    knob.setValue(0.9);
    knob.setValue(knob.getValue());
    CHECK(knob.getValue() == Approx(0.9));
}

TEST_CASE("the widget gallery builds with every widget and survives a few animation ticks", "[ui][widgets]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    WidgetGallery gallery;
    CHECK(gallery.getWidth() > 300);
    juce::Image image(juce::Image::RGB, gallery.getWidth(), gallery.getHeight(), true);
    juce::Graphics g(image);
    gallery.paintEntireComponent(g, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    gallery.pads[0].trigger(1.0f);
    CHECK(gallery.pads[0].flash() > 0.0f);
}
