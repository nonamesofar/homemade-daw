#include "ui/sampleeditor/SampleEditorView.h"

#include "analysis/OnsetDetector.h"
#include "analysis/Slicer.h"
#include "commands/ArrangeCommands.h"
#include "commands/Commands.h"
#include "commands/SliceCommands.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::ui
{
namespace
{
constexpr double fallbackBpm = 120.0;

int modeIndex(model::SliceMode mode)
{
    switch (mode)
    {
        case model::SliceMode::beat: return 0;
        case model::SliceMode::transient: return 1;
        case model::SliceMode::manual: return 2;
    }
    return 2;
}

constexpr model::SliceDivision divisions[] = {model::SliceDivision::quarter, model::SliceDivision::eighth, model::SliceDivision::sixteenth,
                                              model::SliceDivision::thirtySecond};

int divisionIndex(model::SliceDivision division)
{
    for (int i = 0; i < 4; ++i)
        if (divisions[i] == division)
            return i;
    return 0;
}
} // namespace

SampleEditorView::SampleEditorView(EditorContext context, UiState& state)
    : ctx(std::move(context)), ui(state), cancelPeaks(std::make_shared<std::atomic<bool>>(false))
{
    setWantsKeyboardFocus(true);
    for (auto* child : {static_cast<juce::Component*>(&backButton), static_cast<juce::Component*>(&fitButton),
                        static_cast<juce::Component*>(&halfButton), static_cast<juce::Component*>(&doubleButton),
                        static_cast<juce::Component*>(&auditionButton), static_cast<juce::Component*>(&tempoField),
                        static_cast<juce::Component*>(&modeControl), static_cast<juce::Component*>(&divisionControl),
                        static_cast<juce::Component*>(&sensitivityField), static_cast<juce::Component*>(&autoSliceButton),
                        static_cast<juce::Component*>(&slicesToTrackButton),
                        static_cast<juce::Component*>(&strip), static_cast<juce::Component*>(&wave),
                        static_cast<juce::Component*>(&panel), static_cast<juce::Component*>(&pads)})
        addAndMakeVisible(child);

    backButton.onClick = [this] {
        if (ctx.goBack)
            ctx.goBack();
    };
    fitButton.onClick = [this] { wave.fit(); };
    halfButton.setTooltip("Half the tempo");
    doubleButton.setTooltip("Double the tempo");
    halfButton.onClick = [this] { applyTempo(tempo.bpm / 2.0); };
    doubleButton.onClick = [this] { applyTempo(tempo.bpm * 2.0); };
    auditionButton.setClickingTogglesState(false);
    auditionButton.setTooltip("Plays the whole sound from its start; press again to stop");
    auditionButton.onClick = [this] {
        if (ctx.toggleAudition && isOpen())
            ctx.toggleAudition(openId);
    };

    tempoField.setRange({model::minCorrectedBpm, model::maxCorrectedBpm, fallbackBpm, 0.0});
    tempoField.setSuffix(" BPM", 2);
    tempoField.clickToType = true;
    tempoField.dragEnabled = false;
    tempoField.setTooltip("The sound's tempo. Type a new one, or use the half and double buttons");
    tempoField.formatter = [](double v) { return editor::bpmText(v); };
    tempoField.parser = [](const juce::String& text) -> std::optional<double> {
        const auto t = text.trim().upToFirstOccurrenceOf("BPM", false, true).trim();
        if (t.isEmpty() || ! t.containsOnly("0123456789."))
            return std::nullopt;
        return t.getDoubleValue();
    };
    tempoField.onChange = [this](double v) {
        if (! refreshing)
            applyTempo(v);
    };

    modeControl.setItems({"Beat", "Transient", "Manual"});
    modeControl.setSelected(2);
    modeControl.setTooltip("How the sound is chopped");
    modeControl.onChange = [this](int index) {
        if (! refreshing)
            chooseMode(index);
    };
    divisionControl.setItems({"1/4", "1/8", "1/16", "1/32"});
    divisionControl.setTooltip("The grid of Beat mode");
    divisionControl.onChange = [this](int index) {
        if (! refreshing)
            chooseDivision(index);
    };
    sensitivityField.setRange({0.0, 100.0, model::defaultSensitivity, 1.0});
    sensitivityField.setTooltip("Transient mode: higher finds more hits. Drag to preview, then press Auto-slice");
    sensitivityField.formatter = [](double v) { return juce::String(juce::roundToInt(v)); };
    sensitivityField.onChange = [this](double v) {
        if (! refreshing)
            sensitivityChanged(v);
    };
    autoSliceButton.setTooltip("Keeps the slices the Sensitivity setting shows");
    autoSliceButton.onClick = [this] { autoSlice(); };
    slicesToTrackButton.setTooltip("Lays the slices out on a new track from the playhead, one clip each");
    slicesToTrackButton.onClick = [this] { slicesToTrack(); };

    strip.geometry = [this] { return wave.geometry(); };
    strip.onSelectSlice = [this](int index) { selectSlice(index); };
    wave.onSelectSlice = [this](int index) { selectSlice(index); };

    wave.onViewChanged = [this] {
        saveViewState();
        strip.repaint();
    };
    wave.onFirstBeatDragStart = [this] { firstBeatGesture = false; };
    wave.onFirstBeatDrag = [this](juce::int64 frame) {
        if (! isOpen())
            return;
        if (! firstBeatGesture)
        {
            ctx.commands.begin("Move first beat");
            firstBeatGesture = true;
        }
        ctx.commands.update(std::make_unique<commands::MoveFirstBeat>(openId, frame));
    };
    wave.onFirstBeatDragEnd = [this] {
        if (firstBeatGesture)
            ctx.commands.commit();
        firstBeatGesture = false;
    };

    wave.onAddPoint = [this](juce::int64 frame) {
        if (isOpen())
            ctx.commands.apply(std::make_unique<commands::AddSlicePoint>(openId, frame));
    };
    wave.onRemovePoint = [this](int index) {
        if (isOpen())
            ctx.commands.apply(std::make_unique<commands::RemoveSlicePoint>(openId, index));
    };
    wave.onMovePointStart = [this](int) {
        ctx.commands.begin("Move slice point");
        pointGesture = true;
    };
    wave.onMovePoint = [this](int index, juce::int64 frame) {
        if (isOpen() && pointGesture)
            ctx.commands.update(std::make_unique<commands::MoveSlicePoint>(openId, index, frame));
    };
    wave.onMovePointEnd = [this] {
        if (pointGesture)
            ctx.commands.commit();
        pointGesture = false;
    };

    panel.onBoundsTyped = [this](bool isEnd, juce::int64 frame) {
        if (! isOpen())
            return false;
        commands::SetSliceBounds command(openId, selectedIndex, isEnd, frame);
        ctx.commands.apply(command);
        if (! command.wasApplied())
            setNotice("A slice has to stay at least 5 ms long and between its neighbours");
        return command.wasApplied();
    };
    panel.onUnitsChanged = [this](editor::Units u) {
        if (isOpen())
            ui.editorState(openId).units = static_cast<int>(u);
    };
    panel.onParamChanged = [this](editor::SliceParam param, double value) {
        if (! isOpen())
            return;
        commands::SetSliceParam command(openId, selectedIndex,
                                        param == editor::SliceParam::pitch     ? commands::SetSliceParam::Param::pitch
                                        : param == editor::SliceParam::gain    ? commands::SetSliceParam::Param::gain
                                        : param == editor::SliceParam::reverse ? commands::SetSliceParam::Param::reverse
                                                                               : commands::SetSliceParam::Param::playMode,
                                        value);
        if (ctx.commands.isGestureOpen() && paramGesture)
            ctx.commands.update(command);
        else
            ctx.commands.apply(command);
    };
    // A drag on pitch or gain is one undo step from mouse-down to mouse-up.
    panel.pitchField.onDragStart = [this] {
        ctx.commands.begin("Set slice pitch");
        paramGesture = true;
    };
    panel.gainField.onDragStart = [this] {
        ctx.commands.begin("Set slice gain");
        paramGesture = true;
    };
    for (auto* field : {&panel.pitchField, &panel.gainField})
        field->onDragEnd = [this] {
            if (paramGesture)
                ctx.commands.commit();
            paramGesture = false;
        };

    pads.isSounding = [this](int slice) { return ctx.padIsSounding && ctx.padIsSounding(slice); };
    pads.onPadDown = [this](int slice) {
        selectSlice(slice);
        if (ctx.padDown)
            ctx.padDown(slice);
    };
    pads.onPadUp = [this](int slice) {
        if (ctx.padUp)
            ctx.padUp(slice);
    };
    pads.onBankChosen = [this](int bank) {
        if (isOpen())
            ui.editorState(openId).padBank = bank;
    };

    ctx.samplerState.addListener(this);
}

SampleEditorView::~SampleEditorView()
{
    ctx.samplerState.removeListener(this);
    cancelPendingUpdate();
    cancelPeaks->store(true);
    pool.removeAllJobs(true, 10000);
}

model::Source SampleEditorView::source() const { return model::SamplerState(ctx.samplerState).findSourceById(openId); }

std::shared_ptr<const io::SoundAnalysis> SampleEditorView::currentAnalysis() const { return ctx.analysis.find(source()); }

//==============================================================================
void SampleEditorView::open(const juce::String& sourceId)
{
    if (isOpen())
        close();
    openId = sourceId;
    ui.openSourceId = sourceId;
    const auto s = source();
    if (! s.isValid())
    {
        openId = {};
        ui.openSourceId = {};
        return;
    }

    previewActive = false;
    notice = {};
    sliceData = s.sliceSet();
    wave.setPeaks(nullptr);
    wave.clearSelectedMarker();
    wave.setSound(s.frames(), s.sampleRate());
    const auto& state = ui.editorState(sourceId);
    if (state.framesPerPixel > 0.0)
    {
        wave.viewport().framesPerPixel = state.framesPerPixel;
        wave.viewport().startFrame = state.startFrame;
        wave.viewport().clamp();
    }
    else
        wave.viewport().fit();
    selectedIndex = juce::jlimit(0, juce::jmax(0, sliceData.numSlices() - 1), state.selectedSlice);
    loadPeaks(ctx.bundleDir().getChildFile(s.decoded()));
    if (ctx.padsOpen)
        ctx.padsOpen(sourceId);
    refresh();
    resized();
}

void SampleEditorView::close()
{
    if (! isOpen())
        return;
    cancelPendingUpdate();
    saveViewState();
    ui.editorState(openId).selectedSlice = selectedIndex;
    allPadsOff();
    heldSlices.fill(-1);
    if (ctx.padsClose)
        ctx.padsClose();
    cancelPeaks->store(true);
    ++peaksGeneration;
    cancelPeaks = std::make_shared<std::atomic<bool>>(false);
    if (firstBeatGesture || pointGesture || paramGesture)
    {
        ctx.commands.cancel();
        firstBeatGesture = pointGesture = paramGesture = false;
    }
    wave.setPeaks(nullptr);
    openId = {};
    ui.openSourceId = {};
    name = info = counter = notice = {};
}

void SampleEditorView::saveViewState()
{
    if (! isOpen())
        return;
    auto& state = ui.editorState(openId);
    const auto& view = wave.viewport();
    const bool fitted = std::abs(view.framesPerPixel - view.fitFramesPerPixel()) < 1.0e-9;
    state.framesPerPixel = fitted ? 0.0 : view.framesPerPixel;
    state.startFrame = view.startFrame;
}

void SampleEditorView::loadPeaks(const juce::File& wavFile)
{
    const auto generation = ++peaksGeneration;
    const auto cancel = cancelPeaks;
    juce::WeakReference<SampleEditorView> weak(this);
    pool.addJob([weak, cancel, wavFile, generation] {
        auto peaks = editor::WaveformPeaks::build(wavFile, cancel.get());
        if (peaks == nullptr || cancel->load())
            return;
        juce::MessageManager::callAsync([weak, peaks, generation] {
            if (weak != nullptr && weak->peaksGeneration == generation && weak->isOpen())
                weak->wave.setPeaks(peaks);
        });
    });
}

//==============================================================================
void SampleEditorView::refreshSoon()
{
    if (isOpen() && ! refreshing)
        triggerAsyncUpdate();
}

void SampleEditorView::handleAsyncUpdate() { refresh(); }

void SampleEditorView::refresh()
{
    cancelPendingUpdate();
    if (! isOpen())
        return;
    const juce::ScopedValueSetter<bool> guard(refreshing, true);
    const auto s = source();
    if (! s.isValid())
        return;

    const auto result = ctx.analysis.find(s);
    analysisFailed = result == nullptr && ctx.analysis.hasFailed(s);
    if (result == nullptr && ! analysisFailed)
        ctx.analysis.request(s);
    analysing = result == nullptr && ! analysisFailed;

    const double analysedBpm = result != nullptr ? result->bpm() : 0.0;
    const double bpm = s.effectiveBpm(analysedBpm);
    tempo.bpm = bpm > 0.0 ? bpm : fallbackBpm;
    tempo.firstBeatFrame = s.effectiveBeatOffset(result != nullptr ? result->firstBeatFrame : 0);
    tempo.sampleRate = s.sampleRate();

    name = s.name();
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    if (analysisFailed)
        info = "Cannot analyse" + dot + editor::rateText(s.sampleRate()) + dot + (s.channels() == 1 ? "mono" : "stereo");
    else if (analysing)
        info = "Analysing..." + dot + editor::rateText(s.sampleRate()) + dot + (s.channels() == 1 ? "mono" : "stereo");
    else
        info = editor::headerText(tempo, s.frames(), s.channels());

    wave.setSound(s.frames(), s.sampleRate());
    wave.setTempo(tempo);
    tempoField.setValue(tempo.bpm, false);
    const bool canEditTempo = ! analysing;
    tempoField.setEnabled(canEditTempo);
    halfButton.setEnabled(canEditTempo);
    doubleButton.setEnabled(canEditTempo);
    auditionButton.setToggleState(ctx.auditionPlayingId && ctx.auditionPlayingId() == openId, juce::dontSendNotification);

    // The slices. Another change of the slice set (undo, a typed value) ends a sensitivity preview.
    const auto newData = s.sliceSet();
    if (newData != sliceData)
        previewActive = false;
    sliceData = newData;
    shownSliceCount = sliceData.numSlices();
    selectedIndex = juce::jlimit(0, juce::jmax(0, shownSliceCount - 1), selectedIndex);
    wave.setMarkers(sliceData.markers);
    wave.setSelectedSlice(selectedIndex);
    strip.setSelectedSlice(selectedIndex);
    pads.setSliceCount(shownSliceCount);
    pads.setBank(ui.editorState(openId).padBank);
    pads.setSelectedSlice(selectedIndex);

    modeControl.setSelected(modeIndex(sliceData.mode));
    divisionControl.setSelected(divisionIndex(sliceData.division));
    const bool beat = sliceData.mode == model::SliceMode::beat;
    const bool transient = sliceData.mode == model::SliceMode::transient;
    divisionControl.setEnabled(beat);
    divisionControl.setAlpha(beat ? 1.0f : 0.4f);
    const bool sensitive = transient && result != nullptr;
    sensitivityField.setEnabled(sensitive);
    autoSliceButton.setEnabled(sensitive);
    sensitivityField.setAlpha(sensitive ? 1.0f : 0.4f);
    if (! sensitivityField.isDragging() && ! previewActive)
        sensitivityField.setValue(sliceData.sensitivity, false);

    if (previewActive && result != nullptr)
    {
        bool limit = false;
        const auto points = transientPoints(sensitivityField.getValue(), &limit);
        wave.setPreviewMarkers(points);
        const int count = static_cast<int>(points.size()) + 1;
        counter = juce::String(count) + (count == 1 ? " slice" : " slices");
    }
    else
    {
        wave.setPreviewMarkers({});
        counter = juce::String(shownSliceCount) + (shownSliceCount == 1 ? " slice" : " slices");
    }
    updatePanel();
    if (ctx.padsRefresh)
        ctx.padsRefresh(selectedIndex);
    repaint();
}

void SampleEditorView::updatePanel()
{
    editor::SliceView view;
    view.index = selectedIndex;
    view.count = shownSliceCount;
    const auto geometry = wave.geometry();
    view.startFrame = geometry.sliceStart(selectedIndex);
    view.endFrame = geometry.sliceEnd(selectedIndex);
    view.totalFrames = geometry.view.totalFrames;
    view.tempo = tempo;
    if (selectedIndex < static_cast<int>(sliceData.slices.size()))
        view.settings = sliceData.slices[static_cast<size_t>(selectedIndex)];
    panel.setSlice(view, static_cast<editor::Units>(ui.editorState(openId).units));
}

void SampleEditorView::selectSlice(int index)
{
    if (! isOpen())
        return;
    selectedIndex = juce::jlimit(0, juce::jmax(0, shownSliceCount - 1), index);
    ui.editorState(openId).selectedSlice = selectedIndex;
    wave.setSelectedSlice(selectedIndex);
    strip.setSelectedSlice(selectedIndex);
    pads.setSelectedSlice(selectedIndex);
    // The pad bank follows the selection, so the selected slice is always on a pad.
    if (selectedIndex / editor::PadGrid::padsPerBank != pads.bank())
    {
        pads.setBank(selectedIndex / editor::PadGrid::padsPerBank);
        ui.editorState(openId).padBank = pads.bank();
    }
    updatePanel();
    if (ctx.padsRefresh)
        ctx.padsRefresh(selectedIndex);
}

bool SampleEditorView::padKeyDown(int pad)
{
    if (! isOpen())
        return false;
    const int slice = pads.sliceOfPad(pad);
    if (slice < 0)
        return false; // an empty pad plays nothing
    selectSlice(slice);
    heldSlices[static_cast<size_t>(pad)] = slice; // let go as this slice, even if the bank changes meanwhile
    if (ctx.padDown)
        ctx.padDown(slice);
    return true;
}

void SampleEditorView::padKeyUp(int pad)
{
    if (! isOpen())
        return;
    if (! juce::isPositiveAndBelow(pad, editor::PadGrid::padsPerBank))
        return;
    const int slice = heldSlices[static_cast<size_t>(pad)];
    heldSlices[static_cast<size_t>(pad)] = -1;
    if (slice >= 0 && ctx.padUp)
        ctx.padUp(slice);
}

void SampleEditorView::allPadsOff()
{
    if (ctx.padsAllOff)
        ctx.padsAllOff();
}

void SampleEditorView::setNotice(const juce::String& text)
{
    notice = text;
    repaint();
}

void SampleEditorView::applyTempo(double bpm)
{
    if (! isOpen() || analysing)
        return;
    ctx.commands.apply(std::make_unique<commands::SetSourceTempo>(openId, bpm));
}

//==============================================================================
std::vector<juce::int64> SampleEditorView::beatPoints(model::SliceDivision division, bool* limit) const
{
    const auto s = source();
    const auto points = analysis::beatSlicePoints(s.frames(), s.sampleRate(), tempo.bpm, tempo.firstBeatFrame, model::slicesPerBeat(division));
    if (limit != nullptr)
        *limit = points.limitReached;
    return points.frames;
}

std::vector<juce::int64> SampleEditorView::transientPoints(double sensitivity, bool* limit) const
{
    const auto s = source();
    const auto result = currentAnalysis();
    if (result == nullptr)
        return {};
    const auto hits = analysis::selectTransients(result->onsets, static_cast<float>(sensitivity));
    const auto points = analysis::transientSlicePoints(hits, s.frames(), s.sampleRate());
    if (limit != nullptr)
        *limit = points.limitReached;
    return points.frames;
}

void SampleEditorView::chooseMode(int index)
{
    if (! isOpen())
        return;
    const auto division = sliceData.division;
    const auto sensitivity = sliceData.sensitivity;
    previewActive = false;
    notice = {};
    if (index == 2)
    {
        ctx.commands.apply(std::make_unique<commands::SetSliceMode>(openId, model::SliceMode::manual, division, sensitivity));
    }
    else if (analysing)
    {
        setNotice("Wait until the sound has been analysed");
    }
    else if (analysisFailed && index == 1)
    {
        setNotice("The sound could not be analysed, so there are no hits to slice at");
    }
    else
    {
        bool limit = false;
        const auto points = index == 0 ? beatPoints(division, &limit) : transientPoints(sensitivity, &limit);
        ctx.commands.apply(std::make_unique<commands::SetSliceMode>(openId, index == 0 ? model::SliceMode::beat : model::SliceMode::transient,
                                                                    division, sensitivity, points));
        if (limit)
            setNotice("The limit of 256 slices was reached");
    }
    refresh();
}

void SampleEditorView::chooseDivision(int index)
{
    if (! isOpen() || analysing || index < 0 || index > 3)
        return;
    bool limit = false;
    const auto points = beatPoints(divisions[index], &limit);
    notice = {};
    ctx.commands.apply(std::make_unique<commands::SetSliceDivision>(openId, divisions[index], points));
    if (limit)
        setNotice("The limit of 256 slices was reached");
    refresh();
}

void SampleEditorView::sensitivityChanged(double value)
{
    if (! isOpen() || sliceData.mode != model::SliceMode::transient || analysing)
        return;
    previewActive = true;
    bool limit = false;
    const auto points = transientPoints(value, &limit);
    wave.setPreviewMarkers(points);
    const int count = static_cast<int>(points.size()) + 1;
    counter = juce::String(count) + (count == 1 ? " slice" : " slices");
    setNotice(limit ? "The limit of 256 slices was reached" : juce::String());
}

void SampleEditorView::autoSlice()
{
    if (! isOpen() || analysing || sliceData.mode != model::SliceMode::transient)
        return;
    bool limit = false;
    const auto value = sensitivityField.getValue();
    const auto points = transientPoints(value, &limit);
    previewActive = false;
    notice = {};
    ctx.commands.apply(std::make_unique<commands::AutoSlice>(openId, value, points));
    if (limit)
        setNotice("The limit of 256 slices was reached");
    refresh();
}

void SampleEditorView::slicesToTrack()
{
    if (! isOpen())
        return;
    commands::SlicesToTrack command(openId, ctx.playheadBeat ? ctx.playheadBeat() : 0.0);
    ctx.commands.apply(command);
    if (command.resultIds().empty())
        return;
    if (ctx.slicesPlaced)
        ctx.slicesPlaced(command.resultIds(), command.trackId());
}

void SampleEditorView::tick(double)
{
    if (! isOpen())
        return;
    const bool playing = ctx.auditionPlayingId && ctx.auditionPlayingId() == openId;
    if (auditionButton.getToggleState() != playing)
        auditionButton.setToggleState(playing, juce::dontSendNotification);
    const auto frame = playing && ctx.auditionPositionSeconds
                           ? static_cast<juce::int64>(ctx.auditionPositionSeconds() * tempo.sampleRate)
                           : juce::int64{-1};
    wave.setPlayheadFrame(frame);
    pads.tick();
}

//==============================================================================
void SampleEditorView::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    auto header = getLocalBounds().removeFromTop(headerHeight);
    g.setColour(theme::panel);
    g.fillRect(header);
    g.setColour(theme::border);
    g.drawHorizontalLine(headerHeight - 1, 0.0f, static_cast<float>(getWidth()));

    auto text = header.withTrimmedLeft(16 + 112).withTrimmedRight(520).reduced(0, 10);
    g.setColour(theme::text);
    g.setFont(Fonts::sans(17.0f, Fonts::Weight::semiBold));
    g.drawText(name, text.removeFromTop(24), juce::Justification::centredLeft, true);
    g.setColour(theme::muted);
    g.setFont(Fonts::mono(12.0f));
    g.drawText(info, text, juce::Justification::centredLeft, true);

    auto label = Fonts::sans(10.0f);
    label.setExtraKerningFactor(0.08f);
    g.setFont(label);
    g.drawText("TEMPO", tempoField.getBounds().withY(10).withHeight(14), juce::Justification::centredLeft);

    // The toolbar.
    auto toolbar = juce::Rectangle<int>(0, headerHeight, getWidth(), toolbarHeight);
    g.setColour(theme::panel);
    g.fillRect(toolbar);
    g.setColour(theme::border);
    g.drawHorizontalLine(toolbar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    g.setFont(label);
    g.setColour(theme::muted);
    g.drawText("DIVISIONS", divisionControl.getBounds().withY(toolbar.getY() + 2).withHeight(12), juce::Justification::centredLeft);
    g.drawText("SENSITIVITY", sensitivityField.getBounds().withY(toolbar.getY() + 2).withHeight(12).withWidth(90), juce::Justification::centredLeft);

    auto status = toolbar.withLeft(autoSliceButton.getRight() + 20).withRight(slicesToTrackButton.getX() - 12).reduced(0, 8);
    g.setColour(theme::text);
    g.setFont(Fonts::mono(13.0f, true));
    g.drawText(counter, status.removeFromLeft(110), juce::Justification::centredLeft);
    if (notice.isNotEmpty())
    {
        g.setColour(theme::accent);
        g.setFont(Fonts::sans(12.0f));
        g.drawText(notice, status, juce::Justification::centredLeft, true);
    }
}

void SampleEditorView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight).reduced(16, 0);
    backButton.setBounds(header.removeFromLeft(104).withSizeKeepingCentre(104, 34));

    auto right = header;
    fitButton.setBounds(right.removeFromRight(56).withSizeKeepingCentre(56, 34));
    right.removeFromRight(8);
    auditionButton.setBounds(right.removeFromRight(96).withSizeKeepingCentre(96, 34));
    right.removeFromRight(16);
    doubleButton.setBounds(right.removeFromRight(44).withSizeKeepingCentre(44, 34));
    right.removeFromRight(4);
    halfButton.setBounds(right.removeFromRight(44).withSizeKeepingCentre(44, 34));
    right.removeFromRight(8);
    tempoField.setBounds(right.removeFromRight(110).withSizeKeepingCentre(110, 34).withTrimmedTop(6));

    auto toolbar = area.removeFromTop(toolbarHeight).reduced(16, 0).withTrimmedTop(14).withTrimmedBottom(6);
    modeControl.setBounds(toolbar.removeFromLeft(270));
    toolbar.removeFromLeft(24);
    divisionControl.setBounds(toolbar.removeFromLeft(220));
    toolbar.removeFromLeft(24);
    sensitivityField.setBounds(toolbar.removeFromLeft(64));
    toolbar.removeFromLeft(12);
    autoSliceButton.setBounds(toolbar.removeFromLeft(100));
    slicesToTrackButton.setBounds(toolbar.removeFromRight(170));

    auto bottom = area.removeFromBottom(juce::jmin(bottomHeight, area.getHeight() / 2));
    panel.setBounds(bottom.removeFromLeft(juce::jmin(640, bottom.getWidth() * 3 / 5)));
    pads.setBounds(bottom.reduced(16, 12));
    strip.setBounds(area.removeFromTop(editor::SliceStrip::stripHeight));
    wave.setBounds(area);
}
} // namespace sampler::ui
