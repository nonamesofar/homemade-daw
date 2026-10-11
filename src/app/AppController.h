#pragma once

#include "commands/CommandService.h"
#include "engine/Audition.h"
#include "engine/AuditionRequestQueue.h"
#include "engine/EngineHost.h"
#include "engine/ProjectEdit.h"
#include "io/ImportService.h"
#include "io/JobRegistry.h"
#include "ui/browser/BrowserPanel.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <vector>

namespace sampler::app
{
struct AppOptions
{
    /** Developer flag `--audio-start-delay <ms>`: holds back the audio-ready signal. */
    int audioStartDelayMs = 0;
    juce::File untitledRoot = ProjectEdit::defaultUntitledRoot();
};

/**
    Owns the engine, the open project and the services around it, and turns their state into what the window shows.
    No drawing here: the window asks for rows, text and state and listens for changes (a coalesced async broadcast on
    the message thread). All user actions of the window come in as calls on this class.
*/
class AppController : public juce::ChangeBroadcaster, private juce::ChangeListener, private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    explicit AppController(AppOptions options = {});
    ~AppController() override;

    /** Empty when the project could be created. */
    const juce::String& startupError() const { return startError; }

    EngineHost& engine() { return host; }
    /** False only if no project could be made at start-up; the window must not be built then. */
    bool hasProject() const { return edit != nullptr && commands != nullptr; }
    ProjectEdit& project() { return *edit; }
    commands::CommandService& commandService() { return *commands; }
    io::JobRegistry& jobs() { return registry; }
    io::ImportService& importer() { return *importService; }

    //==============================================================================
    void importFiles(const juce::Array<juce::File>& files);
    /** Files dropped on the timeline: imported, then placed as clips (one undo step with the import). */
    void importAndPlace(const juce::Array<juce::File>& files, commands::PlacementTarget target);

    /** The snap value, saved in the project (SAMPLER/UI) without an undo step and without marking it changed. */
    juce::String snapName() const;
    void setSnapName(const juce::String&);
    /** The user clicked a sound: start it, stop it, or (before the audio is ready) queue the request. */
    void toggleAudition(const juce::String& sourceId);

    //==============================================================================
    /** Sounds sorted by name, then the ones still importing. */
    std::vector<ui::BrowserRow> browserRows() const;
    juce::String playingId() const { return audition ? audition->playingId() : juce::String(); }
    /** The waveform picture for a sound of the open project (null if the sound or its file is missing). */
    std::unique_ptr<juce::AudioThumbnailBase> makeThumbnail(const juce::String& sourceId, juce::Component& repaintTarget);
    /** Changes whenever another project becomes the open one (cached pictures belong to one project). */
    int projectGeneration() const { return generation; }
    juce::String audioStatusText() const;
    juce::String windowTitle() const;

    /** A message for the user: text and whether it is an error. */
    std::function<void(const juce::String& text, bool isError)> onToast;
    /** An import found the sound already in the project: show that row. */
    std::function<void(const juce::String& sourceId)> onSelectSource;

    //==============================================================================
    // Project lifecycle. The window supplies the three dialogs below; tests supply stubs that answer at once.
    enum class SaveChoice { save, discard, cancel };

    /** Asks "save changes to <name>?" and calls back with the answer. */
    std::function<void(const juce::String& projectName, std::function<void(SaveChoice)> answer)> askToSave;
    /** Asks where to save; calls back with the chosen folder, or an empty File if cancelled. */
    std::function<void(std::function<void(juce::File)> answer)> chooseSaveLocation;
    /** Asks which project folder to open; calls back with it, or an empty File if cancelled. */
    std::function<void(std::function<void(juce::File)> answer)> chooseProjectToOpen;
    /** Called when the app may quit (after the unsaved-changes question, if there was one). */
    std::function<void()> onQuitConfirmed;
    /**
        Called synchronously just before the open project goes away, and just after another one became the open one:
        views that hold references into the project drop them in the first and rebuild in the second.
    */
    std::function<void()> onProjectClosing, onProjectAttached;

    /** Unsaved changes: the undo history is not where it was at the last save (design D-4). */
    bool isDirty() const { return commands != nullptr && commands->historyToken() != savedToken; }
    void requestNew();
    void requestOpen();
    void requestSave();
    void requestSaveAs();
    void requestQuit();
    /** Opens a project folder now (after the caller dealt with unsaved changes). Keeps the current project on failure. */
    bool openProject(const juce::File& bundle);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { sendChangeMessage(); }
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { sendChangeMessage(); }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { sendChangeMessage(); }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { sendChangeMessage(); }
    void timerCallback() override;

    void handleImportMessage(const io::ImportMessage&);
    void playNow(const juce::String& sourceId);
    void toast(const juce::String& text, bool isError) const;

    void attachProject(std::unique_ptr<ProjectEdit>);
    /** Stops what uses the open project and, for an untitled one, deletes its folder. */
    void closeProject();
    /** Runs `proceed` at once if the project is clean, else after the user chose Save or Discard. */
    void guardUnsaved(std::function<void()> proceed);
    /** Saves (asking for a location if untitled), then runs `next` if it worked. */
    void saveThen(std::function<void()> next);
    void saveAsThen(std::function<void()> next);
    /** True (after a toast) while an import runs: New, Open and Quit would cancel it. */
    bool refuseWhileImporting();

    EngineHost host;
    std::unique_ptr<ProjectEdit> edit;
    std::unique_ptr<commands::CommandService> commands;
    io::JobRegistry registry;
    std::unique_ptr<io::ImportService> importService;
    std::unique_ptr<Audition> audition;
    AuditionRequestQueue queue;
    juce::String startError;
    juce::File untitledRoot;
    int generation = 0;
    int savedToken = 0;
    juce::uint32 startedAtMs = 0;
    juce::String lastPlayingId;
    juce::ValueTree watchedState; // the listener is registered on this instance, so it must outlive the call
};
} // namespace sampler::app
