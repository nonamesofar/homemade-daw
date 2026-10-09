// Drives the real TimelineComponent with synthetic mouse events (no window), covering the spec scenarios for
// clip drag, track reorder and zoom. Waveform drawing is not exercised here (no thumbnails are supplied).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/timeline/TimelineComponent.h"

using namespace sampler::ui;
using Catch::Approx;

namespace
{
TimelineModel makeModel()
{
    TimelineModel m;
    int clipId = 1;
    for (int t = 0; t < 4; ++t)
    {
        TrackModel track;
        track.id = t + 1;
        track.name = "Track " + std::to_string(t + 1);
        for (int c = 0; c < 3; ++c)
            track.clips.push_back({clipId++, c * 8.0, 6.0, 0});
        m.tracks.push_back(track);
    }
    return m;
}

struct Fixture
{
    Fixture()
        : timeline(makeModel(), {juce::File()}, {4.0}, nullptr)
    {
        timeline.setSize(1200, 500);
        timeline.getViewport().pixelsPerBeat = 40.0;
        timeline.getViewport().firstBeat = 0.0;
    }

    juce::MouseEvent event(juce::Point<float> pos, juce::Point<float> downPos, bool dragged, const juce::ModifierKeys& mods = {})
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent(source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &timeline, &timeline, juce::Time::getCurrentTime(),
                                downPos, juce::Time::getCurrentTime(), 1, dragged);
    }

    /** Press at `from`, move through `via`, release at `to`. */
    void drag(juce::Point<float> from, juce::Point<float> via, juce::Point<float> to)
    {
        timeline.mouseDown(event(from, from, false));
        timeline.mouseDrag(event(via, from, true));
        timeline.mouseDrag(event(to, from, true));
        timeline.mouseUp(event(to, from, true));
    }

    float xOfBeat(double beat) const { return static_cast<float>(timeline.getLayout().headerWidth + timeline.getViewport().beatToX(beat)); }
    float yInTrack(int track, float offset = 40.0f) const { return static_cast<float>(timeline.getLayout().trackTop(track)) + offset; }

    TimelineComponent timeline;
};

int totalClips(const TimelineModel& m)
{
    int n = 0;
    for (const auto& t : m.tracks)
        n += static_cast<int>(t.clips.size());
    return n;
}
} // namespace

TEST_CASE("dragging a clip to another track moves it there", "[ui][component][drag]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto& model = f.timeline.getModel();
    const auto movedId = model.tracks[0].clips[0].id; // track 1, starts at beat 0, 6 beats long

    // Grab it 2 beats in, drop on track 3 with the mouse at beat 11 -> clip start 9.
    f.drag({f.xOfBeat(2.0), f.yInTrack(0)}, {f.xOfBeat(5.0), f.yInTrack(1)}, {f.xOfBeat(11.0), f.yInTrack(2)});

    CHECK(totalClips(model) == 12);
    CHECK(model.tracks[0].clips.size() == 2);
    REQUIRE(model.tracks[2].clips.size() == 4);
    const auto& landed = model.tracks[2].clips.back();
    CHECK(landed.id == movedId);
    CHECK(landed.startBeat == Approx(9.0));
    CHECK(landed.lengthBeats == Approx(6.0));
}

TEST_CASE("dragging a clip sideways on its own track just moves it", "[ui][component][drag]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto& model = f.timeline.getModel();

    f.drag({f.xOfBeat(1.0), f.yInTrack(1)}, {f.xOfBeat(3.0), f.yInTrack(1)}, {f.xOfBeat(21.1), f.yInTrack(1)});

    REQUIRE(model.tracks[1].clips.size() == 3);
    const auto& moved = model.tracks[1].clips.back();
    CHECK(moved.startBeat == Approx(20.0)); // grabbed 1 beat in, snapped to the 0.25 grid
}

TEST_CASE("a drag is previewed and the model changes only on mouse-up", "[ui][component][drag]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto& model = f.timeline.getModel();
    const auto before = model.tracks[0].clips[0].startBeat;

    const juce::Point<float> from{f.xOfBeat(2.0), f.yInTrack(0)};
    f.timeline.mouseDown(f.event(from, from, false));
    f.timeline.mouseDrag(f.event({f.xOfBeat(30.0), f.yInTrack(3)}, from, true));
    CHECK(model.tracks[0].clips[0].startBeat == Approx(before)); // still unchanged mid-drag
    CHECK(model.tracks[0].clips.size() == 3);

    f.timeline.mouseUp(f.event({f.xOfBeat(30.0), f.yInTrack(3)}, from, true));
    CHECK(model.tracks[0].clips.size() == 2);
}

TEST_CASE("dragging a track header above the first track reorders the tracks", "[ui][component][reorder]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto& model = f.timeline.getModel();
    const auto clipCount = model.tracks[2].clips.size();

    // Press on the header of track 3, release over track 1.
    f.drag({40.0f, f.yInTrack(2)}, {40.0f, f.yInTrack(1)}, {40.0f, f.yInTrack(0, 10.0f)});

    REQUIRE(model.tracks.size() == 4);
    CHECK(model.tracks[0].id == 3);
    CHECK(model.tracks[0].clips.size() == clipCount); // clips travel with the track
    CHECK(model.tracks[1].id == 1);
    CHECK(model.tracks[2].id == 2);
    CHECK(model.tracks[3].id == 4);
    CHECK(totalClips(model) == 12);
}

TEST_CASE("releasing a track header where it started changes nothing", "[ui][component][reorder]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    bool notified = false;
    f.timeline.onModelChanged = [&] { notified = true; };

    f.drag({40.0f, f.yInTrack(1)}, {40.0f, f.yInTrack(1, 60.0f)}, {40.0f, f.yInTrack(1, 20.0f)});

    CHECK(f.timeline.getModel().tracks[1].id == 2);
    CHECK(!notified);
}

TEST_CASE("clicking empty space changes nothing", "[ui][component]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    const auto snapshot = f.timeline.getModel().tracks[0].clips[0].startBeat;
    f.drag({f.xOfBeat(7.0), f.yInTrack(0)}, {f.xOfBeat(9.0), f.yInTrack(2)}, {f.xOfBeat(12.0), f.yInTrack(2)}); // beat 7 is a gap
    CHECK(totalClips(f.timeline.getModel()) == 12);
    CHECK(f.timeline.getModel().tracks[0].clips[0].startBeat == Approx(snapshot));
}

TEST_CASE("ctrl + wheel zooms around the mouse, plain wheel scrolls", "[ui][component][zoom]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    auto& view = f.timeline.getViewport();

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.5f;

    const juce::Point<float> mouse{600.0f, 200.0f};
    const auto beatUnderMouse = view.xToBeat(mouse.x - f.timeline.getLayout().headerWidth);
    f.timeline.mouseWheelMove(f.event(mouse, mouse, false, juce::ModifierKeys(juce::ModifierKeys::ctrlModifier)), wheel);
    CHECK(view.pixelsPerBeat > 40.0);
    CHECK(view.xToBeat(mouse.x - f.timeline.getLayout().headerWidth) == Approx(beatUnderMouse).margin(1.0e-6));

    // Scrolling right (negative wheel direction) moves the view forward.
    view.pixelsPerBeat = 40.0;
    view.firstBeat = 0.0;
    wheel.deltaY = -0.5f;
    f.timeline.mouseWheelMove(f.event(mouse, mouse, false), wheel);
    CHECK(view.firstBeat > 0.0);
}
