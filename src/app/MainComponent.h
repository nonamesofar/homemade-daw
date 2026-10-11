#pragma once

#include "AppController.h"
#include "ui/browser/BrowserPanel.h"
#include "ui/widgets/Toast.h"
#include "ui/widgets/WidgetGallery.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sampler::app
{
/** Ids of the commands in the menus. */
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
    widgetGalleryCommand
};

/**
    The content of the main window: menu bar, header, sample browser on the left, the main view area (a placeholder
    until the arrangement exists) and the toast overlay. Holds no logic of its own: it shows what the controller says
    and passes the user's actions to it.
*/
class MainComponent : public juce::Component, public juce::FileDragAndDropTarget, public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget, private juce::ChangeListener
{
public:
    explicit MainComponent(AppController&);
    ~MainComponent() override;

    ui::BrowserPanel& browser() { return browserPanel; }
    ui::Toast& toasts() { return toast; }

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

    //==============================================================================
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

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

    AppController& controller;
    juce::ApplicationCommandManager commands;
    juce::MenuBarComponent menuBar;
    ui::BrowserPanel browserPanel;
    ui::Toast toast;
    juce::String audioStatus;
    juce::String progressLabel;
    double progressValue = -1.0;
    int shownGeneration = -1;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser, saveChooser;
    void installDialogs();
    std::unique_ptr<juce::DocumentWindow> galleryWindow;
};
} // namespace sampler::app
