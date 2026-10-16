#pragma once

#include "AppController.h"
#include "commands/ArrangeCommands.h"
#include "ui/KeyboardRouter.h"
#include "ui/arrange/ArrangeLogic.h"
#include "ui/arrange/ArrangementView.h"
#include "ui/arrange/ClipInspector.h"
#include "ui/arrange/HeaderBar.h"
#include "ui/UiState.h"
#include "ui/browser/BrowserPanel.h"
#include "ui/sampleeditor/SampleEditorView.h"
#include "ui/widgets/Toast.h"
#include "ui/widgets/WidgetGallery.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sampler::app
{
/** Ids of the commands in the menus and the shortcut tables. */
enum CommandIds : int
{
    newCommand = 0x1000,
    openCommand,
    saveCommand,
    saveAsCommand,
    importAudioCommand,
    quitCommand,
    undoCommand,
    redoCommand,
    widgetGalleryCommand,
    cutCommand,
    copyCommand,
    pasteCommand,
    duplicateCommand,
    splitCommand,
    deleteCommand,
    selectAllCommand,
    newTrackCommand,
    deleteTrackCommand,
    playStopCommand,
    playFromSelectionCommand,
    returnToStartCommand,
    loopToggleCommand,
    loopToSelectionCommand,
    muteCommand,
    soloCommand,
    zoomToSelectionCommand,
    zoomToSongCommand,
    recordCommand
};

/**
    The content of the main window: menu bar, header (brand, transport and project controls), sample browser on the
    left, the arrangement in the main area and the clip inspector below it, and the toast overlay. Holds no logic of
    its own beyond routing: it shows what the controller and the project say and turns the user's actions into
    commands. Keys go through the KeyboardRouter (design D-9): a dialog or a text field gets them first.
*/
class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropContainer,
                      public juce::MenuBarModel, public juce::ApplicationCommandTarget, private juce::ChangeListener,
                      private juce::FocusChangeListener
{
public:
    explicit MainComponent(AppController&);
    ~MainComponent() override;

    ui::BrowserPanel& browser() { return browserPanel; }
    ui::Toast& toasts() { return toast; }
    /** The parts that belong to the open project (null while there is none). */
    ui::ArrangementView* arrangement() { return arrangementView.get(); }
    ui::HeaderBar* header() { return headerBar.get(); }
    ui::ClipInspector* inspector() { return clipInspector.get(); }
    /** The sample editor (null while there is no project); it shows when a sound is open in it. */
    ui::SampleEditorView* sampleEditor() { return editorView.get(); }
    ui::UiState& uiState() { return viewState; }
    ui::KeyboardRouter& router() { return keyRouter; }
    bool isEditorOpen() const { return editorView != nullptr && editorView->isOpen(); }
    /** Shows a sound in the sample editor in place of the arrangement (browser, clip panel). */
    void openEditor(const juce::String& sourceId);
    /** "< Arrange": shows the arrangement as it was. */
    void closeEditor();

    /**
        Lets go of every pad: the keys that are held, the mouse-held ones and the lights. Done when the sample editor loses
        keyboard focus, is hidden, the app is deactivated or another project is opened.
    */
    void releaseAllPads();
    /** Tests replace how the window finds out whether the sample editor has keyboard focus / the app is in front. */
    std::function<bool()> editorFocusProbe, appActiveProbe;
    ui::SelectionModel& selection() { return selectionModel; }
    const std::vector<commands::ClipRecord>& clipboard() const { return copied; }

    juce::ApplicationCommandManager& commandManager() { return commands; }

    /** The running job shown in the header (empty when nothing runs) and how far it is (negative if unknown). */
    const juce::String& progressText() const { return progressLabel; }
    double progressFraction() const { return progressValue; }
    bool isShowingProgress() const { return progressLabel.isNotEmpty(); }

    /** Help > Widget gallery: a window with every widget. */
    void showWidgetGallery();
    bool isWidgetGalleryOpen() const { return galleryWindow != nullptr && galleryWindow->isVisible(); }

    /** Opens the file chooser of File > Import Audio. */
    void chooseFilesToImport();

    /** Re-reads everything from the controller (also done on every change message). */
    void refresh();
    /** One display frame (the VBlankAttachment calls it; tests call it directly). */
    void tick(double elapsedMs);

    /** Runs a menu command as if chosen (tests and the shortcut tables). */
    bool invoke(juce::CommandID);

    //==============================================================================
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>&) override;
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform(const InvocationInfo&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void globalFocusChanged(juce::Component*) override;
    bool editorHasFocus() const;
    void installDialogs();
    void buildProjectViews();
    void dropProjectViews();
    void selectionChanged();

    // Edit operations on the selection.
    std::vector<engine::ClipInfo> selectedClips() const;
    std::vector<juce::String> clipsUnderPlayhead(bool selectedOnly) const;
    void copySelection();
    void deleteSelection();
    void paste();
    void undoOrRedo(bool redo);
    void toggleTrackParam(commands::SetTrackParam::Control);

    AppController& controller;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menuBar;
    ui::BrowserPanel browserPanel;
    ui::Toast toast;
    juce::TooltipWindow tooltips{this, 600};
    ui::SelectionModel selectionModel;
    std::unique_ptr<ui::HeaderBar> headerBar;
    std::unique_ptr<ui::ArrangementView> arrangementView;
    std::unique_ptr<ui::ClipInspector> clipInspector;
    std::unique_ptr<ui::SampleEditorView> editorView;
    ui::UiState viewState;
    std::vector<commands::ClipRecord> copied;
    ui::KeyboardRouter keyRouter;
    juce::String audioStatus;
    juce::String progressLabel;
    double progressValue = -1.0;
    int shownGeneration = -1;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser, saveChooser;
    std::unique_ptr<juce::DocumentWindow> galleryWindow;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    double lastVBlank = 0.0;
    bool wasAppActive = true;
};
} // namespace sampler::app
