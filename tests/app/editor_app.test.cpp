// M3 1.8: the sample editor inside the main window: opening, leaving, the header, tempo and first beat, entry points.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include "EditorFixture.h"
#include "commands/SliceCommands.h"

#include <chrono>

using namespace sampler::test;

TEST_CASE("double-clicking a sound opens it in the sample editor and stops the browser audition", "[app][editor]")
{
    Session s("editor-open");
    REQUIRE(s.main.sampleEditor() != nullptr);
    CHECK_FALSE(s.main.isEditorOpen());
    CHECK(s.main.arrangement()->isVisible());

    // The first click of a double-click auditions the sound (queued if the audio is not ready yet).
    s.main.browser().onRowClicked(s.id);
    s.main.browser().onRowOpened(s.id);

    CHECK(s.main.isEditorOpen());
    CHECK(s.editor().isVisible());
    CHECK(s.editor().sourceId() == s.id);
    CHECK_FALSE(s.main.arrangement()->isVisible());
    CHECK_FALSE(s.main.inspector()->isVisible());
    CHECK(s.controller.playingId().isEmpty());
    CHECK(s.main.browser().openId() == s.id);
    CHECK(s.main.uiState().openSourceId == s.id);
    CHECK(s.main.header()->viewToggle.selected() == -1); // neither Session nor Arrange
    CHECK(s.controller.windowTitle().contains("*")); // the import, nothing else
}

TEST_CASE("the browser menu and the clip panel open the editor too", "[app][editor]")
{
    Session s("editor-entry");
    s.main.browser().openInEditor(s.id); // "Open in sample editor" in the row menu
    CHECK(s.main.isEditorOpen());
    s.main.closeEditor();
    CHECK_FALSE(s.main.isEditorOpen());

    // A clip of the sound on the arrangement: the clip panel button opens the same sound.
    sampler::commands::CreateClipsFromSources place({s.id}, {0, false, 0.0});
    s.controller.commandService().apply(place);
    const auto clips = place.resultIds();
    REQUIRE(clips.size() == 1);
    s.main.selection().setClips(clips);
    CHECK(s.main.inspector()->openEditor.isEnabled());
    s.main.inspector()->openEditor.onClick();
    CHECK(s.main.isEditorOpen());
    CHECK(s.editor().sourceId() == s.id);
}

TEST_CASE("Back shows the arrangement as it was", "[app][editor]")
{
    Session s("editor-back");
    auto* view = s.main.arrangement();
    view->viewport().pixelsPerBeat = 77.0;
    view->viewport().firstBeat = 32.0;
    view->viewChanged();
    s.main.selection().setClips({});
    const auto track = s.main.selection().track();

    s.main.browser().onRowOpened(s.id);
    REQUIRE(s.main.isEditorOpen());
    s.editor().backButton.onClick();

    CHECK_FALSE(s.main.isEditorOpen());
    CHECK(s.main.arrangement() == view);
    CHECK(s.main.arrangement()->isVisible());
    CHECK(s.main.inspector()->isVisible());
    CHECK(view->viewport().pixelsPerBeat == Catch::Approx(77.0));
    CHECK(view->viewport().firstBeat == Catch::Approx(32.0));
    CHECK(s.main.selection().track() == track);
    CHECK(s.main.browser().openId().isEmpty());
    CHECK(s.main.header()->viewToggle.selected() == 1);
}

TEST_CASE("choosing Arrange in the header leaves the editor", "[app][editor]")
{
    Session s("editor-toggle");
    s.main.browser().onRowOpened(s.id);
    REQUIRE(s.main.isEditorOpen());
    s.main.header()->viewToggle.onChange(1);
    CHECK_FALSE(s.main.isEditorOpen());
    CHECK(s.main.arrangement()->isVisible());
    // Session is not available yet: choosing it changes nothing.
    s.main.browser().onRowOpened(s.id);
    s.main.header()->viewToggle.onChange(0);
    CHECK(s.main.isEditorOpen());
    CHECK(s.main.header()->viewToggle.selected() == -1);
}

TEST_CASE("the header shows the facts of the break", "[app][editor]")
{
    Session s("editor-header");
    s.main.browser().onRowOpened(s.id);
    pump();
    CHECK(s.editor().nameText() == "break-90");
    CHECK_FALSE(s.editor().isAnalysing());
    CHECK(s.editor().infoText() == juce::String::fromUTF8("4 bars \xc2\xb7 90 BPM \xc2\xb7 44.1 kHz \xc2\xb7 stereo"));
    CHECK(s.editor().tempoField.getValue() == Catch::Approx(90.0).margin(0.5));
}

TEST_CASE("a sound without analysis shows Analysing... and then the tempo, and the project stays clean", "[app][editor]")
{
    juce::File target;
    {
        Session first("editor-analysing-a");
        target = first.controller.project().bundleDir().getParentDirectory().getChildFile("Saved.sdaw");
        first.controller.chooseSaveLocation = [&target](std::function<void(juce::File)> answer) { answer(target); };
        first.controller.requestSave();
        REQUIRE_FALSE(first.controller.isDirty());
    }
    REQUIRE(target.getChildFile("cache").deleteRecursively());

    // Open the saved project in a new window: the open queues the analysis, and its result is only taken in by the message
    // loop, so the editor opened before the first pump still shows the job running.
    juce::ScopedJuceInitialiser_GUI juceInit;
    sampler::app::AppController controller(Session::options("editor-analysing-b"));
    sampler::app::MainComponent main(controller);
    REQUIRE(controller.openProject(target));
    const auto id = sampler::model::SamplerState(controller.project().samplerState()).sourceAt(0).id();
    REQUIRE_FALSE(controller.isDirty());

    main.openEditor(id);
    CHECK(main.sampleEditor()->isAnalysing());
    CHECK(main.sampleEditor()->infoText().startsWith("Analysing..."));
    pumpUntil([&] { return ! main.sampleEditor()->isAnalysing(); });
    CHECK(main.sampleEditor()->infoText().contains("90 BPM"));
    CHECK_FALSE(controller.isDirty());
    CHECK_FALSE(controller.commandService().canUndo());
    CHECK(controller.windowTitle().contains("*") == false);
}

TEST_CASE("doubling the tempo gives 180 BPM and 8 bars, and Undo brings 90 back", "[app][editor]")
{
    Session s("editor-double");
    s.main.browser().onRowOpened(s.id);
    pump();
    s.editor().doubleButton.onClick();
    pump();
    CHECK(s.editor().infoText().contains("180 BPM"));
    CHECK(s.editor().infoText().startsWith("8 bars")); // twice the tempo, twice the beats in the same time
    CHECK(s.controller.commandService().undoName() == "Set sound tempo");

    s.main.invoke(sampler::app::undoCommand);
    pump();
    CHECK(s.editor().infoText().contains("90 BPM"));
    CHECK(s.editor().infoText().startsWith("4 bars"));
    CHECK(s.main.isEditorOpen()); // Undo does not close the editor
}

TEST_CASE("typing a tempo and halving it", "[app][editor]")
{
    Session s("editor-type-tempo");
    s.main.browser().onRowOpened(s.id);
    pump();
    REQUIRE(s.editor().tempoField.commitText("92"));
    pump();
    CHECK(s.editor().currentTempo().bpm == Catch::Approx(92.0));
    s.editor().halfButton.onClick();
    pump();
    CHECK(s.editor().currentTempo().bpm == Catch::Approx(46.0));
    CHECK_FALSE(s.editor().tempoField.commitText("fast"));
}

TEST_CASE("dragging the 1.1.1 marker is one undo step and moves the ruler", "[app][editor]")
{
    Session s("editor-first-beat");
    s.main.browser().onRowOpened(s.id);
    pump();
    auto& wave = s.editor().waveform();
    const auto before = s.editor().currentTempo().firstBeatFrame;
    const auto stepsBefore = s.controller.commandService().undoName();

    const float startX = static_cast<float>(wave.viewport().frameToX(static_cast<double>(before)));
    const float targetX = static_cast<float>(wave.viewport().frameToX(40000.0));
    const juce::Point<float> from(startX, 10.0f);
    wave.mouseDown(s.event(wave, from, from, false));
    wave.mouseDrag(s.event(wave, {startX + 20.0f, 10.0f}, from, true));
    wave.mouseDrag(s.event(wave, {targetX, 10.0f}, from, true));
    CHECK(wave.isDraggingFirstBeat());
    wave.mouseUp(s.event(wave, {targetX, 10.0f}, from, true));
    pump();

    CHECK_FALSE(wave.isDraggingFirstBeat());
    const auto after = s.editor().currentTempo().firstBeatFrame;
    CHECK(std::abs(after - 40000) <= 2);
    CHECK(s.controller.commandService().undoName() == "Move first beat");
    s.main.invoke(sampler::app::undoCommand);
    pump();
    CHECK(s.editor().currentTempo().firstBeatFrame == before);
    CHECK(s.controller.commandService().undoName() == stepsBefore);
}

TEST_CASE("undoing the import while its editor is open shows the arrangement", "[app][editor]")
{
    Session s("editor-undo-import");
    s.main.browser().onRowOpened(s.id);
    REQUIRE(s.main.isEditorOpen());
    s.main.invoke(sampler::app::undoCommand); // the import is the only step
    pump();
    CHECK_FALSE(s.main.isEditorOpen());
    CHECK(s.main.arrangement()->isVisible());
    CHECK(s.main.browser().openId().isEmpty());
}

TEST_CASE("a new project closes the editor", "[app][editor]")
{
    Session s("editor-new-project");
    s.main.browser().onRowOpened(s.id);
    REQUIRE(s.main.isEditorOpen());
    s.controller.askToSave = [](const juce::String&, std::function<void(sampler::app::AppController::SaveChoice)> answer) {
        answer(sampler::app::AppController::SaveChoice::discard);
    };
    s.controller.requestNew();
    pump();
    CHECK_FALSE(s.main.isEditorOpen());
    REQUIRE(s.main.sampleEditor() != nullptr);
    CHECK_FALSE(s.main.sampleEditor()->isVisible());
    CHECK(s.main.arrangement()->isVisible());
}

TEST_CASE("zooming with Mod+wheel reaches single samples and Fit shows the whole sound", "[app][editor]")
{
    Session s("editor-zoom");
    s.main.browser().onRowOpened(s.id);
    pump();
    auto& wave = s.editor().waveform();
    REQUIRE(wave.getWidth() > 100);
    CHECK_FALSE(wave.viewport().showsSamples());

    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 1.0f;
    auto mods = juce::ModifierKeys(juce::ModifierKeys::commandModifier);
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> at(static_cast<float>(wave.viewport().frameToX(4410.0)), 60.0f);
    for (int i = 0; i < 40; ++i)
        wave.mouseWheelMove(juce::MouseEvent(source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &wave, &wave, juce::Time::getCurrentTime(), at,
                                             juce::Time::getCurrentTime(), 1, false),
                            wheel);
    CHECK(wave.viewport().showsSamples());
    CHECK(wave.viewport().startFrame > 4000.0);
    CHECK(wave.viewport().startFrame < 4500.0);

    s.editor().fitButton.onClick();
    CHECK(wave.viewport().startFrame == 0.0);
    CHECK(wave.viewport().framesPerPixel == Catch::Approx(wave.viewport().fitFramesPerPixel()));
}

TEST_CASE("the zoom of a sound is remembered while the window lives", "[app][editor]")
{
    Session s("editor-remember");
    s.main.browser().onRowOpened(s.id);
    pump();
    auto& wave = s.editor().waveform();
    wave.viewport().zoomAround(100.0, 8.0);
    wave.viewport().scrollByPixels(40.0);
    const auto fpp = wave.viewport().framesPerPixel;
    const auto start = wave.viewport().startFrame;
    wave.onViewChanged();
    s.main.closeEditor();
    s.main.openEditor(s.id);
    CHECK(wave.viewport().framesPerPixel == Catch::Approx(fpp));
    CHECK(wave.viewport().startFrame == Catch::Approx(start));
}

TEST_CASE("painting the waveform of a long sound stays fast", "[app][editor][perf]")
{
    // 5 minutes of audio: the tables are small, so a paint reads a few thousand values.
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sampler-tests/editor-long");
    dir.createDirectory();
    const auto wav = dir.getChildFile("long.wav");
    {
        wav.deleteFile();
        juce::WavAudioFormat format;
        auto stream = wav.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
        stream.release();
        juce::AudioBuffer<float> block(1, 44100);
        for (int second = 0; second < 300; ++second)
        {
            for (int i = 0; i < 44100; ++i)
                block.setSample(0, i, 0.5f * std::sin(0.05f * static_cast<float>(i)) * (second % 7 == 0 ? 1.0f : 0.4f));
            writer->writeFromAudioSampleBuffer(block, 0, 44100);
        }
    }
    const auto peaks = sampler::ui::editor::WaveformPeaks::build(wav);
    REQUIRE(peaks != nullptr);

    sampler::ui::editor::WaveformView view;
    view.setSize(1400, 300);
    view.setSound(peaks->numFrames(), 44100.0);
    view.setPeaks(peaks);
    view.setTempo({120.0, 0, 44100.0});

    juce::Image image(juce::Image::ARGB, 1400, 300, true);
    {
        juce::Graphics g(image); // a first paint loads the fonts and fills caches: not what scrolling costs
        view.paintEntireComponent(g, false);
    }
    double worst = 0.0;
    for (int i = 0; i < 20; ++i)
    {
        view.viewport().zoomAround(700.0, i % 2 == 0 ? 1.5 : 0.7);
        view.viewport().scrollByPixels(35.0);
        juce::Graphics g(image);
        const auto start = std::chrono::steady_clock::now();
        view.paintEntireComponent(g, false);
        worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    INFO("worst paint " << worst << " ms");
#ifdef NDEBUG
    CHECK(worst < 4.0);
#else
    CHECK(worst < 25.0); // debug builds use checked iterators
#endif
}

TEST_CASE("the editor audition and the browser audition never play together", "[app][editor][hardware]")
{
    Session s("editor-audition");
    pumpUntil([&] { return s.controller.engine().isAudioReady(); }, 15000);
    if (! s.controller.engine().isAudioReady())
        SKIP("no audio output on this machine");

    s.controller.importFiles({fixture("kick.wav")});
    pumpUntil([&] { return ! s.controller.importer().isBusy() && sampler::model::SamplerState(s.controller.project().samplerState()).numSources() == 2; });
    pump();
    juce::String kickId;
    for (int i = 0; i < 2; ++i)
        if (const auto other = sampler::model::SamplerState(s.controller.project().samplerState()).sourceAt(i); other.id() != s.id)
            kickId = other.id();
    REQUIRE(kickId.isNotEmpty());

    s.main.openEditor(s.id);
    s.editor().auditionButton.onClick();
    pump(150);
    CHECK(s.controller.playingId() == s.id);
    s.main.tick(16.0);
    CHECK(s.editor().auditionButton.getToggleState());

    s.controller.toggleAudition(kickId); // a click on another sound in the browser
    pump(150);
    s.main.tick(16.0);
    CHECK(s.controller.playingId() == kickId);
    CHECK_FALSE(s.editor().auditionButton.getToggleState());
    s.controller.stopAudition();
}

TEST_CASE("the clip and track commands do nothing while the editor hides the arrangement", "[app][editor]")
{
    Session s("editor-gated");
    sampler::commands::CreateClipsFromSources place({s.id}, {0, false, 0.0});
    s.controller.commandService().apply(place);
    const auto clip = place.resultIds().front();
    s.main.selection().setClips({clip});
    const auto trackId = s.main.selection().track();
    const auto muted = s.controller.project().arrangement().track(trackId)->mute;

    s.main.openEditor(s.id);
    REQUIRE(s.main.isEditorOpen());
    s.main.invoke(sampler::app::deleteCommand);
    s.main.invoke(sampler::app::cutCommand);
    s.main.invoke(sampler::app::muteCommand);
    s.main.invoke(sampler::app::duplicateCommand);
    CHECK(s.controller.project().arrangement().numClips() == 1);
    CHECK(s.controller.project().arrangement().track(trackId)->mute == muted);

    s.main.closeEditor();
    s.main.invoke(sampler::app::deleteCommand);
    CHECK(s.controller.project().arrangement().numClips() == 0);
}

TEST_CASE("a sound whose analysis cannot be made shows Cannot analyse, does not retry, and its tempo can still be typed", "[app][editor]")
{
    Session s("editor-analysis-failed");
    // The project records another length than the file has: the analysis cannot be used for this sound.
    auto node = sampler::model::SamplerState(s.controller.project().samplerState()).sourceAt(0).node();
    node.setProperty(sampler::model::ids::frames, 470401, nullptr);
    pump();
    pumpUntil([&] { return ! s.controller.analyser().isBusy(); });

    s.main.openEditor(s.id);
    pumpUntil([&] { return ! s.editor().isAnalysing(); });
    pump(100);
    CHECK_FALSE(s.editor().isAnalysing());
    CHECK(s.editor().analysisHasFailed());
    CHECK(s.editor().infoText().startsWith("Cannot analyse"));

    const auto computed = s.controller.analyser().computedCount();
    s.controller.commandService().apply(std::make_unique<sampler::commands::AddSlicePoint>(s.id, 50000));
    pump(100);
    CHECK_FALSE(s.controller.analyser().isBusy());
    CHECK(s.controller.analyser().computedCount() == computed);

    REQUIRE(s.editor().tempoField.isEnabled());
    s.editor().tempoField.commitText("100");
    pump(20);
    CHECK(s.editor().currentTempo().bpm == Catch::Approx(100.0));
}

TEST_CASE("a sound whose file is missing is not opened in the sample editor", "[app][editor]")
{
    Session s("editor-missing-file");
    const auto source = sampler::model::SamplerState(s.controller.project().samplerState()).findSourceById(s.id);
    REQUIRE(s.controller.project().bundleDir().getChildFile(source.decoded()).deleteFile());

    s.main.browser().onRowOpened(s.id);
    pump();
    CHECK_FALSE(s.main.isEditorOpen());
    CHECK(s.main.arrangement()->isVisible());
    s.main.openEditor(s.id);
    CHECK_FALSE(s.main.isEditorOpen());
}
