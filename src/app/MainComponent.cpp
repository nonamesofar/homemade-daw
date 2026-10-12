#include "MainComponent.h"

#include "commands/Commands.h"
#include "engine/Arrangement.h"
#include "engine/LevelMeters.h"
#include "engine/Transport.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

#include <set>

namespace sampler::app
{
namespace
{
constexpr int menuBarHeight = 24;
constexpr int headerHeight = 60;
constexpr int browserWidth = 264;
constexpr int brandWidth = 184;
constexpr int inspectorHeight = 200;

std::set<juce::String> clipIdSet(const engine::Arrangement& a)
{
    std::set<juce::String> ids;
    for (const auto& c : a.clips())
        ids.insert(c.id);
    return ids;
}
} // namespace

MainComponent::MainComponent(AppController& appController) : controller(appController), menuBar(this)
{
    commands.registerAllCommandsForTarget(this);
    commands.setFirstCommandTarget(this);
    setApplicationCommandManagerToWatch(&commands);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(menuBar);
    addAndMakeVisible(browserPanel);
    addChildComponent(toast);

    browserPanel.setThumbnailFactory([this](const juce::String& id, juce::Component& target) { return controller.makeThumbnail(id, target); });
    browserPanel.onRowClicked = [this](const juce::String& id) { controller.toggleAudition(id); };
    browserPanel.onCategoryChosen = [this](const juce::String& id, model::Category category) {
        if (controller.hasProject())
            controller.commandService().apply(std::make_unique<commands::SetSourceCategory>(id, category));
    };
    controller.onToast = [this](const juce::String& text, bool isError) {
        toast.show(text, isError ? ui::Toast::Kind::error : ui::Toast::Kind::info);
    };
    controller.onSelectSource = [this](const juce::String& id) { browserPanel.selectRowById(id); };
    controller.onTakesAdded = [this](const std::vector<juce::String>& ids) { selectionModel.setClips(ids); };
    controller.onProjectClosing = [this] { dropProjectViews(); };
    controller.onProjectAttached = [this] { buildProjectViews(); };
    controller.addChangeListener(this);
    selectionModel.onChange = [this] { selectionChanged(); };
    installDialogs();

    buildProjectViews();
    vblank = std::make_unique<juce::VBlankAttachment>(this, [this](double seconds) {
        const auto elapsed = lastVBlank > 0.0 ? (seconds - lastVBlank) * 1000.0 : 16.7;
        lastVBlank = seconds;
        tick(juce::jlimit(0.0, 100.0, elapsed));
    });

    setSize(1440, 900);
    refresh();
}

MainComponent::~MainComponent()
{
    vblank.reset();
    galleryWindow.reset();
    setApplicationCommandManagerToWatch(nullptr);
    controller.removeChangeListener(this);
    controller.onToast = nullptr;
    controller.onSelectSource = nullptr;
    controller.onTakesAdded = nullptr;
    controller.onProjectClosing = nullptr;
    controller.onProjectAttached = nullptr;
    controller.askToSave = nullptr;
    controller.chooseSaveLocation = nullptr;
    controller.chooseProjectToOpen = nullptr;
    selectionModel.onChange = nullptr;
    dropProjectViews();
}

//==============================================================================
void MainComponent::buildProjectViews()
{
    dropProjectViews();
    if (! controller.hasProject())
        return;
    auto& project = controller.project();
    selectionModel.setClips({});
    selectionModel.setTrack(project.arrangement().numTracks() > 0 ? project.arrangement().tracks()[0].id : juce::String());

    ui::HeaderContext header{project.arrangement(), project.transport(), project.levelMeters(), controller.commandService(), selectionModel,
                             [this] { return ui::Snap::fromName(controller.snapName()); },
                             [this](ui::SnapValue v) {
                                 controller.setSnapName(ui::Snap::name(v));
                                 if (arrangementView != nullptr)
                                     arrangementView->viewChanged();
                             },
                             [this] { controller.recording().toggleRecord(); },
                             [this] { return controller.recording().isRecording(); },
                             [this] { controller.recording().transportStopped(); }};
    headerBar = std::make_unique<ui::HeaderBar>(header);
    addAndMakeVisible(*headerBar);

    ui::ArrangeContext context{project.arrangement(),
                               project.transport(),
                               project.levelMeters(),
                               controller.commandService(),
                               selectionModel,
                               project.samplerState(),
                               [this](const juce::String& id, juce::Component& target) { return controller.makeThumbnail(id, target); },
                               [this] { return ui::Snap::fromName(controller.snapName()); },
                               [this](const juce::Array<juce::File>& files, commands::PlacementTarget target) {
                                   controller.importAndPlace(files, target);
                               },
                               [this] {
                                   std::vector<ui::Placeholder> list;
                                   for (const auto& job : controller.jobs().jobs())
                                       if (job.placement.has_value())
                                           list.push_back({*job.placement, std::max(1, job.pendingNames.size())});
                                   return list;
                               },
                               &controller.recording().armState(),
                               [] { return platform::listPlaybackDevices(); },
                               [this] { return controller.recording().liveTakes(); },
                               [this] { return controller.recording().isRecording(); }};
    arrangementView = std::make_unique<ui::ArrangementView>(context);
    addAndMakeVisible(*arrangementView);

    clipInspector = std::make_unique<ui::ClipInspector>(project.arrangement(), controller.commandService(), selectionModel,
                                                        [this](const juce::String& id, juce::Component& target) {
                                                            return controller.makeThumbnail(id, target);
                                                        });
    addAndMakeVisible(*clipInspector);
    resized();
}

void MainComponent::dropProjectViews()
{
    // These hold references into the project: they go before it does.
    clipInspector.reset();
    arrangementView.reset();
    headerBar.reset();
    copied.clear();
}

void MainComponent::selectionChanged()
{
    if (arrangementView != nullptr)
        arrangementView->lanes().repaint();
    if (clipInspector != nullptr)
        clipInspector->refresh();
    commands.commandStatusChanged();
}

void MainComponent::refresh()
{
    if (shownGeneration != controller.projectGeneration())
    {
        shownGeneration = controller.projectGeneration();
        browserPanel.clearThumbnails(); // pictures belong to the project they were made for
    }
    browserPanel.setRows(controller.browserRows());
    browserPanel.setPlayingId(controller.playingId());
    audioStatus = controller.audioStatusText();
    progressLabel = {};
    progressValue = -1.0;
    if (const auto& running = controller.jobs().jobs(); ! running.empty())
    {
        progressLabel = running.front().label + (running.size() > 1 ? " (+" + juce::String(running.size() - 1) + ")" : juce::String());
        progressValue = running.front().progress;
    }
    if (headerBar != nullptr)
        headerBar->refresh();
    if (arrangementView != nullptr)
        arrangementView->lanes().repaint(); // placeholders of running imports
    if (auto* window = getTopLevelComponent())
        window->setName(controller.windowTitle());
    commands.commandStatusChanged();
    menuItemsChanged();
    repaint(getLocalBounds().withHeight(menuBarHeight + headerHeight));
}

void MainComponent::tick(double elapsedMs)
{
    if (headerBar != nullptr)
        headerBar->tick(elapsedMs);
}

void MainComponent::installDialogs()
{
    controller.askToSave = [](const juce::String& name, std::function<void(AppController::SaveChoice)> answer) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Sampler")
                                         .withMessage("Save changes to " + name + " before closing?")
                                         .withButton("Save")
                                         .withButton("Discard")
                                         .withButton("Cancel"),
                                     [answer](int result) {
                                         answer(result == 1   ? AppController::SaveChoice::save
                                                : result == 2 ? AppController::SaveChoice::discard
                                                              : AppController::SaveChoice::cancel);
                                     });
    };

    controller.chooseSaveLocation = [this](std::function<void(juce::File)> answer) {
        const auto start = juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile("Untitled.sdaw");
        saveChooser = std::make_unique<juce::FileChooser>("Save project as", start, "*.sdaw");
        saveChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [answer](const juce::FileChooser& fc) { answer(fc.getResult()); });
    };

    controller.chooseProjectToOpen = [this](std::function<void(juce::File)> answer) {
        chooser = std::make_unique<juce::FileChooser>("Open project", juce::File::getSpecialLocation(juce::File::userDesktopDirectory));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [answer](const juce::FileChooser& fc) { answer(fc.getResult()); });
    };
}

void MainComponent::showWidgetGallery()
{
    if (galleryWindow == nullptr)
    {
        struct GalleryWindow : juce::DocumentWindow
        {
            GalleryWindow() : DocumentWindow("Widget gallery", ui::theme::panel, DocumentWindow::closeButton)
            {
                setUsingNativeTitleBar(true);
                setContentOwned(new ui::WidgetGallery(), true);
                centreWithSize(getWidth(), getHeight());
            }
            void closeButtonPressed() override { setVisible(false); }
        };
        galleryWindow = std::make_unique<GalleryWindow>();
    }
    galleryWindow->setVisible(true);
    galleryWindow->toFront(true);
}

void MainComponent::chooseFilesToImport()
{
    juce::String pattern;
    for (const auto& extension : io::ImportService::supportedExtensions())
        pattern << (pattern.isEmpty() ? "" : ";") << "*." << extension;

    chooser = std::make_unique<juce::FileChooser>("Import audio", juce::File(), pattern);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
                         [this](const juce::FileChooser& fc) {
                             if (! fc.getResults().isEmpty())
                                 controller.importFiles(fc.getResults());
                         });
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds(area.removeFromTop(menuBarHeight));
    auto header = area.removeFromTop(headerHeight);
    header.removeFromLeft(brandWidth);
    header.removeFromRight(190); // progress and audio status are painted there
    if (headerBar != nullptr)
        headerBar->setBounds(header);
    browserPanel.setBounds(area.removeFromLeft(browserWidth));
    if (clipInspector != nullptr)
        clipInspector->setBounds(area.removeFromBottom(std::min(inspectorHeight, area.getHeight() / 3)));
    if (arrangementView != nullptr)
        arrangementView->setBounds(area);
    toast.setBounds(toast.boundsFor(getLocalBounds()));
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(ui::theme::background);

    auto area = getLocalBounds();
    area.removeFromTop(menuBarHeight);
    auto header = area.removeFromTop(headerHeight);
    g.setColour(ui::theme::panel);
    g.fillRect(header);
    g.setColour(ui::theme::border);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // The mark: five bars, then the name.
    {
        const auto origin = header.getPosition().toFloat() + juce::Point<float>(16.0f, 19.0f);
        juce::Path bars;
        const float heights[] = {4.0f, 10.0f, 16.0f, 8.0f, 4.0f};
        for (int i = 0; i < 5; ++i)
        {
            const float x = origin.x + 3.0f + 4.0f * static_cast<float>(i);
            const float mid = origin.y + 11.0f;
            bars.startNewSubPath(x, mid - heights[i] * 0.5f);
            bars.lineTo(x, mid + heights[i] * 0.5f);
        }
        g.setColour(ui::theme::accent);
        g.strokePath(bars, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto name = ui::Fonts::sans(16.0f, ui::Fonts::Weight::semiBold);
        name.setExtraKerningFactor(0.02f);
        g.setColour(ui::theme::text);
        g.setFont(name);
        g.drawText("Sampler", header.withTrimmedLeft(48).withWidth(brandWidth - 48), juce::Justification::centredLeft);
    }

    auto status = header.removeFromRight(190).reduced(8, 0);
    if (progressLabel.isNotEmpty())
    {
        auto box = status.withSizeKeepingCentre(status.getWidth(), 30);
        g.setColour(ui::theme::text);
        g.setFont(ui::Fonts::sans(12.0f));
        g.drawText(progressLabel, box.removeFromTop(16), juce::Justification::centredRight, true);
        auto bar = box.withTrimmedTop(4).withHeight(4).toFloat();
        g.setColour(ui::theme::background);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(ui::theme::accent);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(progressValue >= 0.0 ? juce::jlimit(0.0, 1.0, progressValue) : 0.25)),
                               2.0f);
    }
    else
    {
        g.setColour(ui::theme::muted);
        g.setFont(ui::Fonts::mono(12.0f));
        g.drawText(audioStatus, status, juce::Justification::centredRight, true);
    }
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour(ui::theme::accent.withAlpha(0.8f));
        g.drawRect(getLocalBounds(), 3);
    }
}

void MainComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void MainComponent::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    juce::Array<juce::File> list;
    for (const auto& path : files)
        list.add(juce::File(path));
    controller.importFiles(list);
}

//==============================================================================
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    ui::KeyboardRouter::Focus focus;
    focus.dialogOpen = juce::Component::getCurrentlyModalComponent() != nullptr;
    focus.textFieldFocused = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    if (ui::KeyboardRouter::route(key, focus) == ui::KeyboardRouter::Route::leaveToFocused)
        return false;
    if (! keyRouter.acceptsTransportKey(key))
        return true; // an auto-repeat of a held transport key
    return commands.getKeyMappings()->keyPressed(key, this);
}

bool MainComponent::keyStateChanged(bool)
{
    keyRouter.keysChanged();
    return false;
}

//==============================================================================
std::vector<engine::ClipInfo> MainComponent::selectedClips() const
{
    std::vector<engine::ClipInfo> result;
    if (! controller.hasProject())
        return result;
    for (const auto& id : selectionModel.clips())
        if (auto clip = const_cast<AppController&>(controller).project().arrangement().clip(id))
            result.push_back(*clip);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return result;
}

std::vector<juce::String> MainComponent::clipsUnderPlayhead(bool selectedOnly) const
{
    std::vector<juce::String> ids;
    if (! controller.hasProject())
        return ids;
    auto& project = const_cast<AppController&>(controller).project();
    const auto beat = project.transport().positionBeats();
    for (const auto& c : project.arrangement().clips())
        if (c.start < beat - 1.0e-6 && c.end() > beat + 1.0e-6 && (! selectedOnly || selectionModel.isSelected(c.id)))
            ids.push_back(c.id);
    return ids;
}

void MainComponent::copySelection()
{
    auto& arrangement = controller.project().arrangement();
    copied = ui::Clipboard::recordsFor(selectedClips(), [&arrangement](const juce::String& id) { return arrangement.trackIndex(id); });
}

void MainComponent::deleteSelection()
{
    if (! selectionModel.isEmpty())
    {
        commands::DeleteClips command(selectionModel.clips());
        controller.commandService().apply(command);
        selectionModel.clearClips();
    }
    else if (selectionModel.isHeaderSelected())
    {
        commands::DeleteTracks command({selectionModel.track()});
        controller.commandService().apply(command);
        const auto& tracks = controller.project().arrangement().tracks();
        selectionModel.setTrack(tracks.empty() ? juce::String() : tracks.front().id);
    }
}

void MainComponent::paste()
{
    if (copied.empty())
        return;
    auto& project = controller.project();
    const auto track = std::max(0, project.arrangement().trackIndex(selectionModel.track()));
    commands::PasteClips command(copied, track, project.transport().positionBeats());
    controller.commandService().apply(command);
    selectionModel.setClips(command.resultIds());
}

void MainComponent::undoOrRedo(bool redo)
{
    auto& service = controller.commandService();
    if (service.isRecordingLocked())
    {
        redo ? service.redo() : service.undo(); // refused with a message
        return;
    }
    if (redo ? ! service.canRedo() : ! service.canUndo())
        return;
    const auto before = clipIdSet(controller.project().arrangement());
    redo ? service.redo() : service.undo();
    selectionModel.afterHistoryChange(before, clipIdSet(controller.project().arrangement()));
}

void MainComponent::toggleTrackParam(commands::SetTrackParam::Control control)
{
    const auto track = controller.project().arrangement().track(selectionModel.track());
    if (! track)
        return;
    const bool now = control == commands::SetTrackParam::Control::mute ? track->mute : track->solo;
    commands::SetTrackParam command(track->id, control, now ? 0.0 : 1.0);
    controller.commandService().apply(command);
}

bool MainComponent::invoke(juce::CommandID id) { return commands.invokeDirectly(id, false); }

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames() { return {"File", "Edit", "Track", "Help"}; }

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addCommandItem(&commands, newCommand);
        menu.addCommandItem(&commands, openCommand);
        menu.addSeparator();
        menu.addCommandItem(&commands, saveCommand);
        menu.addCommandItem(&commands, saveAsCommand);
        menu.addSeparator();
        menu.addCommandItem(&commands, importAudioCommand);
        menu.addSeparator();
        menu.addCommandItem(&commands, quitCommand);
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commands, undoCommand);
        menu.addCommandItem(&commands, redoCommand);
        menu.addSeparator();
        for (auto id : {cutCommand, copyCommand, pasteCommand, duplicateCommand, splitCommand, deleteCommand})
            menu.addCommandItem(&commands, id);
        menu.addSeparator();
        menu.addCommandItem(&commands, selectAllCommand);
    }
    else if (index == 2)
    {
        menu.addCommandItem(&commands, newTrackCommand);
        menu.addCommandItem(&commands, deleteTrackCommand);
        menu.addSeparator();
        menu.addCommandItem(&commands, recordCommand);
    }
    else
    {
        menu.addCommandItem(&commands, widgetGalleryCommand);
    }
    return menu;
}

void MainComponent::menuItemSelected(int, int) {}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& ids)
{
    ids.addArray({newCommand, openCommand, saveCommand, saveAsCommand, importAudioCommand, quitCommand, undoCommand, redoCommand,
                  widgetGalleryCommand, cutCommand, copyCommand, pasteCommand, duplicateCommand, splitCommand, deleteCommand,
                  selectAllCommand, newTrackCommand, deleteTrackCommand, playStopCommand, playFromSelectionCommand,
                  returnToStartCommand, loopToggleCommand, loopToSelectionCommand, muteCommand, soloCommand,
                  zoomToSelectionCommand, zoomToSongCommand, recordCommand});
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto mod = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const bool project = controller.hasProject();
    const bool gesture = project && controller.commandService().isGestureOpen();
    const bool hasSelection = project && ! selectionModel.isEmpty();
    switch (id)
    {
        case newCommand:
            info.setInfo("New", "Starts a new project", "File", 0);
            info.addDefaultKeypress('n', mod);
            break;
        case openCommand:
            info.setInfo("Open...", "Opens a saved project", "File", 0);
            info.addDefaultKeypress('o', mod);
            break;
        case saveCommand:
            info.setInfo("Save", "Saves the project", "File", 0);
            info.addDefaultKeypress('s', mod);
            break;
        case saveAsCommand:
            info.setInfo("Save As...", "Saves the project under a new name", "File", 0);
            info.addDefaultKeypress('s', mod | shift);
            break;
        case importAudioCommand:
            info.setInfo("Import Audio...", "Adds audio files to the project", "File", 0);
            info.addDefaultKeypress('i', mod);
            break;
        case quitCommand:
            info.setInfo("Quit", "Closes the app", "File", 0);
            info.addDefaultKeypress('q', mod);
            break;
        case widgetGalleryCommand: info.setInfo("Widget gallery", "Shows every widget", "Help", 0); break;
        case undoCommand:
        {
            const auto name = project ? controller.commandService().undoName() : juce::String();
            info.setInfo(name.isEmpty() ? "Undo" : "Undo " + name, "Undoes the last change", "Edit", 0);
            info.setActive(project && controller.commandService().canUndo());
            info.addDefaultKeypress('z', mod);
            break;
        }
        case redoCommand:
        {
            const auto name = project ? controller.commandService().redoName() : juce::String();
            info.setInfo(name.isEmpty() ? "Redo" : "Redo " + name, "Redoes the last undone change", "Edit", 0);
            info.setActive(project && controller.commandService().canRedo());
            info.addDefaultKeypress('z', mod | shift);
            info.addDefaultKeypress('y', mod);
            break;
        }
        case cutCommand:
            info.setInfo("Cut", "Copies the selected clips and deletes them", "Edit", 0);
            info.setActive(hasSelection && ! gesture);
            info.addDefaultKeypress('x', mod);
            break;
        case copyCommand:
            info.setInfo("Copy", "Copies the selected clips", "Edit", 0);
            info.setActive(hasSelection);
            info.addDefaultKeypress('c', mod);
            break;
        case pasteCommand:
            info.setInfo("Paste", "Pastes at the playhead on the selected track", "Edit", 0);
            info.setActive(project && ! copied.empty() && ! gesture);
            info.addDefaultKeypress('v', mod);
            break;
        case duplicateCommand:
            info.setInfo("Duplicate", "Copies the selected clips right after them", "Edit", 0);
            info.setActive(hasSelection && ! gesture);
            info.addDefaultKeypress('d', mod);
            break;
        case splitCommand:
            info.setInfo("Split", "Splits clips at the playhead", "Edit", 0);
            info.setActive(project && ! gesture && ! clipsUnderPlayhead(hasSelection).empty());
            info.addDefaultKeypress('e', mod);
            break;
        case deleteCommand:
            info.setInfo("Delete", "Deletes the selected clips (or the selected track)", "Edit", 0);
            info.setActive(project && ! gesture && (hasSelection || selectionModel.isHeaderSelected()));
            info.addDefaultKeypress(juce::KeyPress::deleteKey, 0);
            info.addDefaultKeypress(juce::KeyPress::backspaceKey, 0);
            break;
        case selectAllCommand:
            info.setInfo("Select All", "Selects every clip", "Edit", 0);
            info.setActive(project && controller.project().arrangement().numClips() > 0);
            info.addDefaultKeypress('a', mod);
            break;
        case newTrackCommand:
            info.setInfo("New Audio Track", "Adds an audio track after the selected one", "Track", 0);
            info.setActive(project && ! gesture);
            info.addDefaultKeypress('t', mod);
            break;
        case deleteTrackCommand:
            info.setInfo("Delete Track", "Deletes the selected track with its clips", "Track", 0);
            info.setActive(project && ! gesture && controller.project().arrangement().track(selectionModel.track()).has_value());
            break;
        case recordCommand:
            info.setInfo("Record", "Records onto the armed tracks, or ends the recording", "Transport", 0);
            info.setActive(project && ! gesture);
            info.addDefaultKeypress('r', mod);
            break;
        case playStopCommand:
            info.setInfo("Play / Stop", "Starts or stops playback", "Transport", 0);
            info.addDefaultKeypress(juce::KeyPress::spaceKey, 0);
            break;
        case playFromSelectionCommand:
            info.setInfo("Play from selection", "Plays from the start of the selection", "Transport", 0);
            info.addDefaultKeypress(juce::KeyPress::spaceKey, shift);
            break;
        case returnToStartCommand:
            info.setInfo("Go to start", "Moves the playhead to the loop start or the song start", "Transport", 0);
            info.addDefaultKeypress(juce::KeyPress::homeKey, 0);
            info.addDefaultKeypress(juce::KeyPress::returnKey, 0);
            break;
        case loopToggleCommand:
            info.setInfo("Loop", "Turns looping on or off", "Transport", 0);
            info.addDefaultKeypress('l', 0);
            break;
        case loopToSelectionCommand:
            info.setInfo("Loop selection", "Sets the loop to the selected clips", "Transport", 0);
            info.setActive(hasSelection && ! gesture);
            info.addDefaultKeypress('l', mod);
            break;
        case muteCommand:
            info.setInfo("Mute track", "Mutes or unmutes the selected track", "Track", 0);
            info.addDefaultKeypress('m', 0);
            break;
        case soloCommand:
            info.setInfo("Solo track", "Solos or un-solos the selected track", "Track", 0);
            info.addDefaultKeypress('s', 0);
            break;
        case zoomToSelectionCommand:
            info.setInfo("Zoom to selection", "Shows the selected clips", "View", 0);
            info.addDefaultKeypress('z', 0);
            break;
        case zoomToSongCommand:
            info.setInfo("Zoom to song", "Shows the whole song", "View", 0);
            info.addDefaultKeypress('z', shift);
            break;
        default: break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case newCommand: controller.requestNew(); return true;
        case openCommand: controller.requestOpen(); return true;
        case saveCommand: controller.requestSave(); return true;
        case saveAsCommand: controller.requestSaveAs(); return true;
        case importAudioCommand: chooseFilesToImport(); return true;
        case quitCommand: juce::JUCEApplication::getInstance()->systemRequestedQuit(); return true;
        case widgetGalleryCommand: showWidgetGallery(); return true;
        default: break;
    }
    if (! controller.hasProject())
        return false;
    auto& project = controller.project();
    auto& service = controller.commandService();
    if (service.isGestureOpen() && info.commandID != playStopCommand && info.commandID != returnToStartCommand)
        return true; // nothing edits the project while a drag is in progress

    switch (info.commandID)
    {
        case undoCommand: undoOrRedo(false); return true;
        case redoCommand: undoOrRedo(true); return true;
        case cutCommand:
            if (! selectionModel.isEmpty())
            {
                copySelection();
                deleteSelection();
            }
            return true;
        case copyCommand: copySelection(); return true;
        case pasteCommand: paste(); return true;
        case duplicateCommand:
            if (! selectionModel.isEmpty())
            {
                commands::DuplicateClips command(selectionModel.clips());
                service.apply(command);
                selectionModel.setClips(command.resultIds());
            }
            return true;
        case splitCommand:
            if (const auto ids = clipsUnderPlayhead(! selectionModel.isEmpty()); ! ids.empty())
            {
                commands::SplitClips command(ids, project.transport().positionBeats());
                service.apply(command);
            }
            return true;
        case deleteCommand: deleteSelection(); return true;
        case selectAllCommand:
        {
            std::vector<juce::String> ids;
            for (const auto& c : project.arrangement().clips())
                ids.push_back(c.id);
            selectionModel.setClips(ids);
            return true;
        }
        case newTrackCommand:
        {
            commands::AddTrack command(selectionModel.track());
            service.apply(command);
            if (! command.resultIds().empty())
                selectionModel.setTrack(command.resultIds().front());
            return true;
        }
        case deleteTrackCommand:
            if (selectionModel.track().isNotEmpty())
            {
                commands::DeleteTracks command({selectionModel.track()});
                service.apply(command);
                const auto& tracks = project.arrangement().tracks();
                selectionModel.setTrack(tracks.empty() ? juce::String() : tracks.front().id);
            }
            return true;
        case recordCommand: controller.recording().toggleRecord(); return true;
        case playStopCommand:
            if (headerBar != nullptr)
                headerBar->togglePlay();
            return true;
        case playFromSelectionCommand:
        {
            const auto clips = selectedClips();
            const auto from = ui::TransportLogic::playFromSelection(
                clips.empty() ? std::nullopt : std::optional<double>(clips.front().start), project.transport().positionBeats());
            if (project.transport().locateBeats(from))
                project.transport().play();
            return true;
        }
        case returnToStartCommand:
            if (headerBar != nullptr)
                headerBar->returnToStart();
            return true;
        case loopToggleCommand:
            project.transport().setLooping(! project.transport().isLooping());
            if (headerBar != nullptr)
                headerBar->refresh();
            if (arrangementView != nullptr)
                arrangementView->ruler().repaint();
            return true;
        case loopToSelectionCommand:
            if (const auto clips = selectedClips(); ! clips.empty())
            {
                double start = clips.front().start, end = clips.front().end();
                for (const auto& c : clips)
                {
                    start = std::min(start, c.start);
                    end = std::max(end, c.end());
                }
                commands::SetLoop command({start, end});
                service.apply(command);
            }
            return true;
        case muteCommand: toggleTrackParam(commands::SetTrackParam::Control::mute); return true;
        case soloCommand: toggleTrackParam(commands::SetTrackParam::Control::solo); return true;
        case zoomToSelectionCommand:
            if (arrangementView != nullptr)
                arrangementView->zoomToSelection();
            return true;
        case zoomToSongCommand:
            if (arrangementView != nullptr)
                arrangementView->zoomToSong();
            return true;
        default: return false;
    }
}
} // namespace sampler::app
