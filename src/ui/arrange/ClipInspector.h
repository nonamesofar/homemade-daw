#pragma once

#include "ArrangeLogic.h"
#include "commands/CommandService.h"
#include "engine/Arrangement.h"
#include "ui/widgets/Widgets.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>

namespace sampler::ui
{
/**
    The bottom panel (clip-inspector spec): the selected clip's name, track, bars and waveform in the track colour, its
    gain, start and Reverse. FADE IN, FADE OUT and "Open in sample editor" are shown disabled until M4 and M3. With no
    clip or several selected it says so and shows no fields.
*/
class ClipInspector : public juce::Component, private juce::ChangeListener
{
public:
    using ThumbnailFactory = std::function<std::unique_ptr<juce::AudioThumbnailBase>(const juce::String& sourceId, juce::Component&)>;

    ClipInspector(const engine::Arrangement&, commands::CommandService&, SelectionModel&, ThumbnailFactory);
    ~ClipInspector() override;

    /** Re-reads the selection and the clip (also on every change of the arrangement). */
    void refresh();

    /** What the panel shows, for tests: the clip name (or the selection status) and "Track · Bars a–b". */
    juce::String titleText() const { return title; }
    juce::String detailText() const { return detail; }
    bool showsClip() const { return clip.has_value(); }

    /** "Open in sample editor" was pressed for the selected clip's sound. */
    std::function<void(const juce::String& sourceId)> onOpenEditor;

    ValueField gain, start, fadeIn, fadeOut;
    ToggleButton reverse{"Reverse"};
    juce::TextButton openEditor{"Open in sample editor"};

    void paint(juce::Graphics&) override;
    void resized() override;

    /** "Bars 5–8" for a clip covering bars 5 to 8. */
    static juce::String barsText(double startBeat, double endBeat, int beatsPerBar);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    juce::Rectangle<int> waveArea() const;

    const engine::Arrangement& arrangement;
    commands::CommandService& commands;
    SelectionModel& selection;
    ThumbnailFactory makeThumbnail;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnailBase>> thumbnails;
    std::optional<engine::ClipInfo> clip;
    juce::Colour colour;
    juce::String title, detail;
};
} // namespace sampler::ui
