#pragma once

#include "ArrangeLogic.h"
#include "InspectorLogic.h"
#include "commands/CommandService.h"
#include "engine/Arrangement.h"
#include "ui/widgets/Widgets.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>
#include <memory>

namespace sampler::ui
{
/**
    The bottom panel (clip-inspector spec): the selected clip's name, track, bars and waveform in the track colour, its
    gain, start, length, loop, fades and Reverse, and a warp row (WARP, MODE, BPM with halve and double, TRANSPOSE) whose
    enabled states come from `InspectorLogic`. With no clip or several selected it says so and shows no fields.
*/
class ClipInspector : public juce::Component, private juce::ChangeListener
{
public:
    using ThumbnailFactory = std::function<std::unique_ptr<juce::AudioThumbnailBase>(const juce::String& sourceId, juce::Component&)>;
    /** The tempo of a sound (the user's correction, else the detected one); 0 or less when not known. */
    using SoundTempo = std::function<double(const juce::String& sourceId)>;

    ClipInspector(const engine::Arrangement&, commands::CommandService&, SelectionModel&, ThumbnailFactory, SoundTempo = nullptr);
    ~ClipInspector() override;

    /** Re-reads the selection and the clip (also on every change of the arrangement). */
    void refresh();

    /** What the panel shows, for tests: the clip name (or the selection status) and "Track · Bars a–b". */
    juce::String titleText() const { return title; }
    juce::String detailText() const { return detail; }
    bool showsClip() const { return clip.has_value(); }

    /** "Open in sample editor" was pressed for the selected clip's sound. */
    std::function<void(const juce::String& sourceId)> onOpenEditor;

    /** The enabled states of the warp row for the selected clip (see InspectorLogic::stateFor). */
    const InspectorLogic::State& warpState() const { return warp; }

    ValueField gain, start, length, fadeIn, fadeOut;
    ToggleButton loopButton{"ON"};
    ToggleButton reverse{"Reverse"};
    juce::TextButton openEditor{"Open in sample editor"};

    // The warp row.
    ToggleButton warpToggle{"WARP"};
    SegmentedControl mode;
    ValueField bpm, transpose;
    juce::TextButton halveButton{juce::String::fromUTF8("\xc2\xbd")}, doubleButton{juce::String::fromUTF8("\xc3\x97" "2")};

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Where the waveform is drawn, for tests. */
    juce::Rectangle<int> waveArea() const;

    /** "Bars 5–8" for a clip covering bars 5 to 8. */
    static juce::String barsText(double startBeat, double endBeat, int beatsPerBar);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    /** A cell of the grid of value fields (3 columns, 2 rows: GAIN START LENGTH / FADE IN FADE OUT LOOP). */
    juce::Rectangle<int> valueCell(int column, int row) const;
    static constexpr int valueGridWidth = 352;
    /** The four cells of the warp row (WARP, MODE, BPM, TRANSPOSE), each a label above its control. */
    std::array<juce::Rectangle<int>, 4> warpCells() const;
    /** Top of the value cells and the waveform, below the header and the warp row. */
    int controlsTop() const;
    /** Sends the clip's warp settings with one value changed. */
    void applyWarp(bool on, engine::WarpMode warpMode, double sourceBpm);
    void refreshWarp();
    /** Every control that is shown only while a clip is selected. */
    std::vector<juce::Component*> fields();

    const engine::Arrangement& arrangement;
    commands::CommandService& commands;
    SelectionModel& selection;
    ThumbnailFactory makeThumbnail;
    SoundTempo soundTempo;
    InspectorLogic::State warp;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnailBase>> thumbnails;
    std::optional<engine::ClipInfo> clip;
    juce::Colour colour;
    juce::String title, detail;
};
} // namespace sampler::ui
