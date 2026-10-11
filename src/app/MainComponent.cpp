#include "MainComponent.h"

#include "commands/Commands.h"
#include "ui/look/Fonts.h"
#include "ui/look/Theme.h"

namespace sampler::app
{
namespace
{
constexpr int menuBarHeight = 24;
constexpr int headerHeight = 60;
constexpr int browserWidth = 264;

} // namespace

MainComponent::MainComponent(AppController& appController) : controller(appController), menuBar(this)
{
    commands.registerAllCommandsForTarget(this);
    commands.setFirstCommandTarget(this);
    setApplicationCommandManagerToWatch(&commands);
    addKeyListener(commands.getKeyMappings());
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
    controller.addChangeListener(this);
    installDialogs();

    setSize(1100, 700);
    refresh();
}

MainComponent::~MainComponent()
{
    galleryWindow.reset();
    removeKeyListener(commands.getKeyMappings());
    setApplicationCommandManagerToWatch(nullptr);
    controller.removeChangeListener(this);
    controller.onToast = nullptr;
    controller.onSelectSource = nullptr;
    controller.askToSave = nullptr;
    controller.chooseSaveLocation = nullptr;
    controller.chooseProjectToOpen = nullptr;
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
    if (auto* window = getTopLevelComponent())
        window->setName(controller.windowTitle());
    commands.commandStatusChanged();
    menuItemsChanged();
    repaint();
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
    area.removeFromTop(headerHeight);
    browserPanel.setBounds(area.removeFromLeft(browserWidth));
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
        g.drawText("Sampler", header.withTrimmedLeft(48), juce::Justification::centredLeft);
    }

    if (progressLabel.isNotEmpty())
    {
        auto box = header.withSizeKeepingCentre(320, 30);
        g.setColour(ui::theme::text);
        g.setFont(ui::Fonts::sans(13.0f));
        g.drawText(progressLabel, box.removeFromTop(18), juce::Justification::centred, true);
        auto bar = box.withTrimmedTop(4).withHeight(4).toFloat();
        g.setColour(ui::theme::background);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(ui::theme::accent);
        if (progressValue >= 0.0)
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, progressValue))), 2.0f);
        else
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * 0.25f), 2.0f);
    }

    g.setColour(ui::theme::muted);
    g.setFont(ui::Fonts::mono(12.0f));
    g.drawText(audioStatus, header.withTrimmedRight(16), juce::Justification::centredRight);

    area.removeFromLeft(browserWidth); // the browser panel paints itself

    // The main view area: a placeholder until the arrangement exists (M2).
    g.setColour(ui::theme::muted.withAlpha(0.6f));
    g.setFont(ui::Fonts::sans(16.0f));
    g.drawText("The arrangement arrives in the next milestone", area, juce::Justification::centred);
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

juce::StringArray MainComponent::getMenuBarNames() { return {"File", "Edit", "Help"}; }

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
    ids.addArray({newCommand, openCommand, saveCommand, saveAsCommand, importAudioCommand, quitCommand, undoCommand, redoCommand, widgetGalleryCommand});
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto mod = juce::ModifierKeys::commandModifier;
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
            info.addDefaultKeypress('s', mod | juce::ModifierKeys::shiftModifier);
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
            const auto name = controller.hasProject() ? controller.commandService().undoName() : juce::String();
            info.setInfo(name.isEmpty() ? "Undo" : "Undo " + name, "Undoes the last change", "Edit", 0);
            info.setActive(controller.hasProject() && controller.commandService().canUndo());
            info.addDefaultKeypress('z', mod);
            break;
        }
        case redoCommand:
        {
            const auto name = controller.hasProject() ? controller.commandService().redoName() : juce::String();
            info.setInfo(name.isEmpty() ? "Redo" : "Redo " + name, "Redoes the last undone change", "Edit", 0);
            info.setActive(controller.hasProject() && controller.commandService().canRedo());
            info.addDefaultKeypress('z', mod | juce::ModifierKeys::shiftModifier);
            info.addDefaultKeypress('y', mod);
            break;
        }
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
        case undoCommand:
            if (controller.hasProject() && controller.commandService().canUndo())
                controller.commandService().undo();
            return true;
        case redoCommand:
            if (controller.hasProject() && controller.commandService().canRedo())
                controller.commandService().redo();
            return true;
        default: return false;
    }
}
} // namespace sampler::app
