#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "TimelineGeometry.h"

namespace sampler::ui
{
/**
    M0 spike timeline: tracks, clips with waveforms, scroll and zoom, clip drag and track reorder.
    Drags paint a preview and change the model on mouse-up. All hit-testing and index maths live in
    TimelineGeometry.h; this class only draws and routes mouse events to it.
*/
class TimelineComponent : public juce::Component
{
public:
    using ThumbnailFactory = std::function<std::unique_ptr<juce::AudioThumbnailBase>(const juce::File&, juce::Component&)>;

    /** `sources[i]` is the audio file for clips with sourceIndex i; `sourceSeconds[i]` its length. */
    TimelineComponent(TimelineModel modelToUse, std::vector<juce::File> sources, std::vector<double> sourceSeconds,
                      ThumbnailFactory factory);
    ~TimelineComponent() override;

    TimelineModel& getModel() { return model; }
    Viewport& getViewport() { return view; }
    const Viewport& getViewport() const { return view; }
    const Layout& getLayout() const { return layout; }

    /** Called after every paint with the time it took, for the frame-rate benchmark. */
    std::function<void(double paintMilliseconds)> onPainted;
    std::function<void()> onModelChanged;

    double bpm = 120.0;
    double gridBeats = 0.25;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct ClipDrag
    {
        ClipHit hit;
        double grabOffsetBeats = 0.0;
        ClipDrop drop;
    };
    struct TrackDrag
    {
        int from = 0;
        int to = 0;
        float mouseY = 0.0f;
    };

    void drawRuler(juce::Graphics&) const;
    void drawTrack(juce::Graphics&, int trackIndex) const;
    void drawClip(juce::Graphics&, const ClipModel&, juce::Rectangle<float> bounds, juce::Colour colour, bool ghost) const;
    juce::Colour colourForTrack(int trackId) const;
    juce::AudioThumbnailBase* thumbnailFor(int sourceIndex) const;
    void clampView();

    TimelineModel model;
    std::vector<juce::File> sourceFiles;
    std::vector<double> sourceLengthSeconds;
    ThumbnailFactory makeThumbnail;
    mutable std::map<int, std::unique_ptr<juce::AudioThumbnailBase>> thumbnails;

    Layout layout;
    Viewport view;
    std::optional<ClipDrag> clipDrag;
    std::optional<TrackDrag> trackDrag;
    std::optional<std::pair<int, int>> selectedClip; // track id, clip id
};
} // namespace sampler::ui
