#pragma once

#include "commands/CommandService.h"
#include "io/AnalysisService.h"
#include "model/SamplerState.h"
#include "ui/UiState.h"
#include "ui/sampleeditor/PadGrid.h"
#include "ui/sampleeditor/SlicePanel.h"
#include "ui/sampleeditor/SliceStrip.h"
#include "ui/sampleeditor/WaveformView.h"
#include "ui/widgets/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <functional>

namespace sampler::ui
{
/** What the sample editor needs from the app; everything it changes goes through `commands`. */
struct EditorContext
{
    juce::ValueTree samplerState;
    commands::CommandService& commands;
    io::AnalysisService& analysis;
    std::function<juce::File()> bundleDir;
    /** Starts the whole sound from its beginning, or stops it (one audition at a time across the browser and the editor). */
    std::function<void(const juce::String& sourceId)> toggleAudition;
    std::function<juce::String()> auditionPlayingId;
    std::function<double()> auditionPositionSeconds;
    /** "< Arrange". */
    std::function<void()> goBack;

    // The pad player (set by the app; unset in tests that only look at the view).
    /** The editor opened / closed a sound: the pads are supplied with its slices / emptied. */
    std::function<void(const juce::String& sourceId)> padsOpen;
    std::function<void()> padsClose;
    /** The project or the selected slice changed: render what is new. */
    std::function<void(int selectedSlice)> padsRefresh;
    /** A pad was pressed or let go (`slice` is the slice index); all pads off. */
    std::function<void(int slice)> padDown, padUp;
    std::function<void()> padsAllOff;
    std::function<bool(int slice)> padIsSounding;

    /** The playhead in beats: "Slices to new track" lays the clips out from there. */
    std::function<double()> playheadBeat;
    /** The slices were laid out on a new track: show the arrangement with these clips selected. */
    std::function<void(const std::vector<juce::String>& clipIds, const juce::String& trackId)> slicesPlaced;
};

/**
    The sample editor (sample-editor spec): the sound's name and facts, its tempo, the toolbar to chop it (Beat, Transient,
    Manual, Divisions, Sensitivity, Auto-slice), the slice strip, the waveform with zoom, the draggable first beat and the
    slice points, the slice panel with START and END, and Audition. Pads come with a later task. Message thread only.
*/
class SampleEditorView : public juce::Component, private juce::ValueTree::Listener, private juce::AsyncUpdater
{
public:
    SampleEditorView(EditorContext, UiState&);
    ~SampleEditorView() override;

    /** Shows a sound (loads its waveform, asks for its analysis). */
    void open(const juce::String& sourceId);
    /** Remembers the view state of the open sound and lets go of its waveform. */
    void close();
    const juce::String& sourceId() const { return openId; }
    bool isOpen() const { return openId.isNotEmpty(); }

    /** Re-reads the sound, its analysis and the project's corrections (also on every change of the project). */
    void refresh();
    /** Runs a refresh that a change of the project has asked for and that has not happened yet (tests). */
    void flushPendingRefresh() { handleUpdateNowIfNeeded(); }
    /** One display frame: moves the audition line. */
    void tick(double elapsedMs);

    //==============================================================================
    /** Header texts, for tests. */
    juce::String nameText() const { return name; }
    juce::String infoText() const { return info; }
    bool isAnalysing() const { return analysing; }
    /** The sound could not be analysed (unreadable file, or not the length the project records): tempo can still be typed. */
    bool analysisHasFailed() const { return analysisFailed; }
    /** The tempo in use (corrected, else analysed, else 120) and the frame of the first beat. */
    editor::Tempo currentTempo() const { return tempo; }

    //==============================================================================
    /** "16 slices" (the preview's count while the Sensitivity slider has been moved and not applied). */
    juce::String counterText() const { return counter; }
    int sliceCount() const { return shownSliceCount; }
    /** A short message under the toolbar (the 256 limit, a refused value); empty when there is none. */
    juce::String noticeText() const { return notice; }
    bool isPreviewing() const { return previewActive; }
    int selectedSlice() const { return selectedIndex; }
    /** Selects a slice (strip, waveform, pad); not an undo step. */
    void selectSlice(int index);
    /** The units of START and END. */
    editor::Units units() const { return static_cast<editor::Units>(ui.editorState(openId).units); }

    editor::WaveformView& waveform() { return wave; }
    editor::SliceStrip& sliceStrip() { return strip; }
    editor::PadGrid& padGrid() { return pads; }

    //==============================================================================
    /**
        A pad key went down / up (0 = pad 1, bottom left, to 15). The pad plays slice `bank * 16 + pad` of the bank shown;
        a pad without a slice does nothing. Pressing selects the slice. Returns whether a slice was played.
    */
    bool padKeyDown(int pad);
    void padKeyUp(int pad);
    void allPadsOff();
    /** The pad bank shown (A = 0). */
    int padBank() const { return pads.bank(); }
    editor::SlicePanel& slicePanel() { return panel; }

    juce::TextButton backButton{"< Arrange"}, fitButton{"Fit"}, halfButton{juce::String::fromUTF8("\xc2\xbd")},
        doubleButton{juce::String::fromUTF8("\xc3\x97" "2")}, autoSliceButton{"Auto-slice"}, slicesToTrackButton{"Slices to new track"};
    ToggleButton auditionButton{"Audition"};
    ValueField tempoField, sensitivityField;
    SegmentedControl modeControl, divisionControl;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int headerHeight = 64;
    static constexpr int toolbarHeight = 52;
    static constexpr int bottomHeight = 260;

private:
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { refreshSoon(); }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { refreshSoon(); }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { refreshSoon(); }
    /** Changes of the project are coalesced: the editor refreshes once, after the command that made them has finished. */
    void refreshSoon();
    void handleAsyncUpdate() override;
    void applyTempo(double bpm);
    void saveViewState();
    void loadPeaks(const juce::File& wav);
    model::Source source() const;

    // Chopping.
    std::vector<juce::int64> beatPoints(model::SliceDivision, bool* limit = nullptr) const;
    std::vector<juce::int64> transientPoints(double sensitivity, bool* limit = nullptr) const;
    void chooseMode(int index);
    void chooseDivision(int index);
    void sensitivityChanged(double value);
    void autoSlice();
    void slicesToTrack();
    void setNotice(const juce::String&);
    void updatePanel();
    std::shared_ptr<const io::SoundAnalysis> currentAnalysis() const;

    EditorContext ctx;
    UiState& ui;
    editor::WaveformView wave;
    editor::SliceStrip strip;
    editor::SlicePanel panel;
    editor::PadGrid pads;
    juce::ThreadPool pool{1};
    std::shared_ptr<std::atomic<bool>> cancelPeaks;
    int peaksGeneration = 0;

    juce::String openId;
    juce::String name, info, counter, notice;
    bool analysing = false;
    bool analysisFailed = false;
    editor::Tempo tempo;
    bool refreshing = false;
    bool firstBeatGesture = false;
    bool pointGesture = false;
    bool paramGesture = false;

    model::SliceSetData sliceData;
    int shownSliceCount = 1;
    int selectedIndex = 0;
    bool previewActive = false;
    std::array<int, editor::PadGrid::padsPerBank> heldSlices = [] {
        std::array<int, editor::PadGrid::padsPerBank> a{};
        a.fill(-1);
        return a;
    }();

    JUCE_DECLARE_WEAK_REFERENCEABLE(SampleEditorView)
};
} // namespace sampler::ui
