// M2b: arming tracks and choosing their inputs in the track headers.
#include "ViewHarness.h"

#include "ui/KeyboardRouter.h"

using sampler::test::ViewHarness;
namespace cmd = sampler::commands;

namespace
{
void clickButton(juce::Button& button)
{
    button.triggerClick(); // posts a message
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}
} // namespace

TEST_CASE("R arms exclusively; Mod-click adds and removes", "[arrange][view][record]")
{
    ViewHarness h("rv-arm");
    cmd::AddTrack add;
    h.f.commands->apply(add);
    h.f.commands->apply(add);
    h.sync();
    auto& headers = h.view->headers();
    REQUIRE(headers.numHeaders() == 3);

    clickButton(headers.header(0)->armButton);
    CHECK(h.arms.isArmed(h.f.trackId(0)));
    CHECK(headers.header(0)->armButton.getToggleState());

    clickButton(headers.header(1)->armButton); // exclusive
    CHECK_FALSE(h.arms.isArmed(h.f.trackId(0)));
    CHECK(h.arms.isArmed(h.f.trackId(1)));
    CHECK_FALSE(headers.header(0)->armButton.getToggleState());

    juce::ModifierKeys::currentModifiers = juce::ModifierKeys::commandModifier;
    clickButton(headers.header(2)->armButton); // Mod-click: adds
    juce::ModifierKeys::currentModifiers = {};
    CHECK(h.arms.isArmed(h.f.trackId(1)));
    CHECK(h.arms.isArmed(h.f.trackId(2)));

    juce::ModifierKeys::currentModifiers = juce::ModifierKeys::commandModifier;
    clickButton(headers.header(1)->armButton); // Mod-click: removes
    juce::ModifierKeys::currentModifiers = {};
    CHECK_FALSE(h.arms.isArmed(h.f.trackId(1)));
    CHECK(h.arms.isArmed(h.f.trackId(2)));

    // Arming is not an undo step.
    CHECK(h.f.commands->undoName() == "Add track");

    // A deleted track is forgotten.
    h.f.commands->apply(std::make_unique<cmd::DeleteTracks>(std::vector<juce::String>{h.f.trackId(2)}));
    h.sync();
    CHECK_FALSE(h.arms.isAnyArmed());
}

TEST_CASE("the input menu lists devices, Master and the other tracks, and a track cannot choose itself", "[arrange][view][record]")
{
    ViewHarness h("rv-input");
    cmd::AddTrack add;
    h.f.commands->apply(add);
    h.f.commands->apply(add);
    h.sync();
    const auto tracks = h.f.arrangement().tracks();
    const auto choices = h.view->inputChoices(tracks[2]);

    juce::StringArray labels;
    for (const auto& c : choices)
        labels.add(c.label);
    CHECK(labels == juce::StringArray{"System audio (Speakers)", "System audio (Headset)", "Master", tracks[0].name, tracks[1].name});
    CHECK(choices[0].ticked); // new tracks start on the default device
    CHECK(h.view->inputLabel(tracks[2]) == "In: System (Speakers)");
}

TEST_CASE("choosing an input is one undo step and shows in the header", "[arrange][view][record]")
{
    ViewHarness h("rv-input-set");
    cmd::AddTrack add;
    h.f.commands->apply(add);
    h.sync();
    h.view->setTrackInput(h.f.trackId(1), "master");
    h.sync();
    CHECK(h.f.commands->undoName() == "Set track input");
    CHECK(h.view->headers().header(1)->inputText() == "In: Master");

    h.view->setTrackInput(h.f.trackId(1), "track:" + h.f.trackId(0));
    h.sync();
    CHECK(h.view->headers().header(1)->inputText() == "In: " + h.f.arrangement().tracks()[0].name);

    h.f.commands->undo();
    h.sync();
    CHECK(h.view->headers().header(1)->inputText() == "In: Master");

    // A saved device that is not there falls back to the default and says so.
    h.view->setTrackInput(h.f.trackId(1), "system:gone");
    h.sync();
    CHECK(h.view->headers().header(1)->inputText() == "In: System (Speakers), chosen device missing");
}

TEST_CASE("Mod+R is not repeated while held and is left to a text field", "[arrange][view][record]")
{
    using sampler::ui::KeyboardRouter;
    const juce::KeyPress modR('r', juce::ModifierKeys::commandModifier, 'r');
    CHECK(KeyboardRouter::isTransportKey(modR));
    CHECK(KeyboardRouter::route(modR, {false, true}) == KeyboardRouter::Route::leaveToFocused);
    CHECK(KeyboardRouter::route(modR, {false, false}) == KeyboardRouter::Route::shortcuts);

    KeyboardRouter router;
    router.isKeyDown = [](int) { return true; };
    CHECK(router.acceptsTransportKey(modR));
    CHECK_FALSE(router.acceptsTransportKey(modR)); // auto-repeat
}

namespace
{
/** A take with `seconds` of tone (or silence) in it, on the given tracks. */
struct FakeLiveTake
{
    FakeLiveTake(const juce::File& dir, std::vector<juce::String> tracks, double startSeconds, double seconds, bool tone)
    {
        sampler::platform::TakeOptions o;
        o.file = dir.getChildFile("live-" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
        o.sampleRate = 48000.0;
        o.channels = 2;
        o.ringSeconds = 80;
        writer.open(o);
        std::vector<float> block(480 * 2);
        for (int done = 0; done < static_cast<int>(seconds * 100); ++done)
        {
            for (size_t i = 0; i < block.size(); ++i)
                block[i] = tone ? ((i / 2) % 2 == 0 ? 0.5f : -0.5f) : 0.0f;
            writer.pushInterleaved(block.data(), 2, 480);
        }
        take.trackIds = std::move(tracks);
        take.writer = &writer;
        take.startSeconds = startSeconds;
        take.lengthSeconds = seconds;
        take.started = true;
    }
    ~FakeLiveTake() { writer.finish(); }

    sampler::platform::TakeWriter writer;
    sampler::ui::LiveTake take;
};

/** Number of pixels in the lane image whose red part clearly beats the other colours. */
int redPixels(juce::Component& c, juce::Rectangle<int> area)
{
    const auto image = c.createComponentSnapshot(c.getLocalBounds(), false, 1.0f);
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto p = image.getPixelAt(x, y);
            if (p.getRed() > 150 && p.getGreen() < 140 && p.getBlue() < 140)
                ++count;
        }
    return count;
}
} // namespace

TEST_CASE("a growing take is drawn on its track and gains waveform columns as audio arrives", "[arrange][view][record]")
{
    ViewHarness h("rv-live");
    h.sync();
    auto& lanes = h.view->lanes();
    lanes.setSize(1000, 400);
    FakeLiveTake silent(h.f.root, {h.f.trackId(0)}, 0.0, 4.0, false);
    FakeLiveTake loud(h.f.root, {h.f.trackId(0)}, 0.0, 4.0, true);

    h.live = {};
    const auto none = redPixels(lanes, {0, 0, 400, 112});
    h.live = {silent.take};
    const auto withSilent = redPixels(lanes, {0, 0, 400, 112});
    h.live = {loud.take};
    const auto withLoud = redPixels(lanes, {0, 0, 400, 112});
    CHECK(withSilent > none);       // the red clip
    CHECK(withLoud > withSilent + 500); // and its waveform

    // It grows with the playhead: twice as long, about twice as wide.
    auto longer = loud.take;
    longer.lengthSeconds = 8.0;
    h.live = {longer};
    const auto wide = redPixels(lanes, {0, 0, 1000, 112});
    h.live = {loud.take};
    const auto narrow = redPixels(lanes, {0, 0, 1000, 112});
    CHECK(wide > narrow);
}

TEST_CASE("the track meter shows the incoming level while a take grows", "[arrange][view][record]")
{
    ViewHarness h("rv-live-meter");
    h.sync();
    FakeLiveTake loud(h.f.root, {h.f.trackId(0)}, 0.0, 1.0, true);
    h.live = {loud.take};
    h.view->tick(16.0);
    CHECK(h.view->headers().header(0)->meter().levelDb() > -12.0); // a 0.5 tone is -6 dBFS
    h.live = {};
    for (int i = 0; i < 100; ++i)
        h.view->tick(16.0);
    CHECK(h.view->headers().header(0)->meter().levelDb() < -30.0); // it falls at a fixed speed
}

TEST_CASE("the warning and the finishing label ride on the growing take", "[arrange][view][record]")
{
    ViewHarness h("rv-live-warning");
    h.sync();
    auto& lanes = h.view->lanes();
    lanes.setSize(1000, 400);
    FakeLiveTake silent(h.f.root, {h.f.trackId(0)}, 0.0, 6.0, false);
    auto withWarning = silent.take;
    withWarning.warning = "No sound is coming in (Speakers)";
    h.live = {silent.take};
    const auto quiet = lanes.createComponentSnapshot(lanes.getLocalBounds(), false, 1.0f);
    h.live = {withWarning};
    const auto warned = lanes.createComponentSnapshot(lanes.getLocalBounds(), false, 1.0f);
    int different = 0;
    for (int y = 0; y < 112; ++y)
        for (int x = 0; x < 400; ++x)
            if (quiet.getPixelAt(x, y) != warned.getPixelAt(x, y))
                ++different;
    CHECK(different > 100); // the text is there
}

TEST_CASE("painting four growing takes stays fast", "[arrange][view][record][perf][hardware]")
{
    ViewHarness h("rv-live-perf");
    for (int i = 0; i < 3; ++i)
        h.f.commands->apply(std::make_unique<cmd::AddTrack>());
    h.sync();
    auto& lanes = h.view->lanes();
    lanes.setSize(1200, 600);
    h.view->viewport().pixelsPerBeat = 4.0; // a long take on a small zoom: many buckets per pixel
    h.view->viewChanged();
    std::vector<std::unique_ptr<FakeLiveTake>> takes;
    h.live.clear();
    for (int i = 0; i < 4; ++i)
    {
        takes.push_back(std::make_unique<FakeLiveTake>(h.f.root, std::vector<juce::String>{h.f.trackId(i)}, 0.0, 70.0, true));
        h.live.push_back(takes.back()->take);
    }
    juce::Image image(juce::Image::ARGB, 1200, 600, true);
    double worst = 0.0;
    std::vector<double> times;
    for (int i = 0; i < 30; ++i)
    {
        juce::Graphics g(image);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        lanes.paintLiveTakesOnly(g);
        times.push_back(juce::Time::getMillisecondCounterHiRes() - t0);
        worst = std::max(worst, times.back());
    }
    std::sort(times.begin(), times.end());
    const auto ninetieth = times[times.size() * 9 / 10];
    INFO("median " << times[times.size() / 2] << " ms, 90th percentile " << ninetieth << " ms, worst " << worst << " ms");
#if JUCE_DEBUG
    CHECK(ninetieth < 20.0); // debug builds are slow; the release budget is 2 ms
#else
    CHECK(ninetieth < 2.0); // the 90th percentile: one scheduler hiccup in 30 paints is not the paint code
#endif
}
