#pragma once

// The arrangement view, header bar and clip inspector over an engine-backed project, driven by synthetic events.
#include "ArrangeFixture.h"

#include "ui/arrange/ArrangementView.h"
#include "ui/arrange/ClipInspector.h"
#include "ui/arrange/HeaderBar.h"

namespace sampler::test
{
struct ViewHarness
{
    explicit ViewHarness(const juce::String& name) : f(name)
    {
        auto& p = *f.project;
        selection.setTrack(p.arrangement().tracks()[0].id);
        ui::ArrangeContext context{p.arrangement(),
                                   p.transport(),
                                   p.levelMeters(),
                                   *f.commands,
                                   selection,
                                   p.samplerState(),
                                   [this](const juce::String& id, juce::Component& target) -> std::unique_ptr<juce::AudioThumbnailBase> {
                                       const auto source = sampler::model::SamplerState(f.project->samplerState()).findSourceById(id);
                                       if (! source.isValid())
                                           return nullptr;
                                       return f.host.createThumbnail(f.project->bundleDir().getChildFile(source.decoded()), target);
                                   },
                                   [this] { return snap; },
                                   [this](const juce::Array<juce::File>& files, commands::PlacementTarget target) {
                                       droppedFiles = files;
                                       droppedTarget = target;
                                   },
                                   [] { return std::vector<ui::Placeholder>{}; },
                                   &arms,
                                   [this] { return devices; },
                                   [this] { return live; },
                                   [this] { return ! live.empty(); },
                                   [this](const juce::String& id) { return defaultWarp ? defaultWarp(id) : engine::WarpSettings{}; }};
        view = std::make_unique<ui::ArrangementView>(context);
        view->setSize(1200, 600);
        ui::HeaderContext header{p.arrangement(), p.transport(), p.levelMeters(), *f.commands, selection,
                                 [this] { return snap; }, [this](ui::SnapValue v) { snap = v; }};
        headerBar = std::make_unique<ui::HeaderBar>(header);
        headerBar->setSize(1100, 60);
        inspector = std::make_unique<ui::ClipInspector>(p.arrangement(), *f.commands, selection, nullptr,
                                                        [this](const juce::String&) { return soundBpm; });
        inspector->setSize(1000, 256);
        selection.onChange = [this] { inspector->refresh(); };
        view->viewport().pixelsPerBeat = 20.0;
        view->viewport().firstBeat = 0.0;
        view->viewChanged();
    }

    ~ViewHarness()
    {
        selection.onChange = nullptr;
        inspector.reset();
        headerBar.reset();
        view.reset();
    }

    /** Lets the arrangement's change message through, so the view shows the new state. */
    void sync()
    {
        ArrangeFixture::pump();
        view->refresh();
        inspector->refresh();
        headerBar->refresh();
    }

    juce::MouseEvent event(juce::Component& target, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                           juce::ModifierKeys mods = {}, int clicks = 1)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        return juce::MouseEvent(source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(),
                                downPos, juce::Time::getCurrentTime(), clicks, dragged);
    }

    /** Press at `from`, move through `via`, release at `to` on a component. */
    void drag(juce::Component& target, juce::Point<float> from, juce::Point<float> via, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        target.mouseDown(event(target, from, from, false, mods));
        target.mouseDrag(event(target, via, from, true, mods));
        target.mouseDrag(event(target, to, from, true, mods));
        target.mouseUp(event(target, to, from, true, mods));
    }

    void click(juce::Component& target, juce::Point<float> at, juce::ModifierKeys mods = {})
    {
        target.mouseDown(event(target, at, at, false, mods));
        target.mouseUp(event(target, at, at, false, mods));
    }

    /** Lane coordinates of a beat and a track (`offset` px into the lane). */
    float xOfBeat(double beat) const { return static_cast<float>(view->viewport().beatToX(beat)); }
    float yInTrack(int track, float offset = 50.0f) const
    {
        return static_cast<float>(view->layout().trackTop(track) - view->viewport().scrollY) + offset;
    }

    void dropSample(const juce::String& sourceId, juce::Point<float> at)
    {
        juce::DragAndDropTarget::SourceDetails details("sample:" + sourceId, nullptr, at.toInt());
        view->lanes().itemDragMove(details);
        view->lanes().itemDropped(details);
    }

    ArrangeFixture f;
    ui::SelectionModel selection;
    ui::ArmState arms;
    std::vector<ui::LiveTake> live;
    std::vector<sampler::platform::PlaybackDevice> devices{{"dev-a", "Speakers", true}, {"dev-b", "Headset", false}};
    ui::SnapValue snap = ui::SnapValue::bar;
    std::unique_ptr<ui::ArrangementView> view;
    std::unique_ptr<ui::HeaderBar> headerBar;
    std::unique_ptr<ui::ClipInspector> inspector;
    std::function<engine::WarpSettings(const juce::String& sourceId)> defaultWarp;
    /** The tempo the inspector is told the sound has (0 = not known). */
    double soundBpm = 0.0;
    juce::Array<juce::File> droppedFiles;
    commands::PlacementTarget droppedTarget;
};
} // namespace sampler::test
