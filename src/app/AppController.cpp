#include "AppController.h"

#include "engine/Arrangement.h"
#include "engine/LevelMeters.h"
#include "engine/Transport.h"
#include "model/SamplerState.h"

#include <algorithm>

namespace sampler::app
{
AppController::AppController(AppOptions options)
{
    startedAtMs = juce::Time::getMillisecondCounter();
    if (options.audioStartDelayMs > 0)
        host.holdAudioReadyFor(options.audioStartDelayMs);

    untitledRoot = options.untitledRoot;
    audition = std::make_unique<Audition>(host);
    registry.addChangeListener(this);
    host.onAudioReady = [this] {
        if (const auto id = queue.takeForPlayback(juce::Time::getMillisecondCounter()))
            playNow(*id);
        sendChangeMessage();
    };

    attachProject(ProjectEdit::createUntitled(host, untitledRoot, startError));
    if (edit != nullptr)
        startTimerHz(30);
}

AppController::~AppController()
{
    stopTimer();
    host.onAudioReady = nullptr;
    registry.removeChangeListener(this);
    watchedState.removeListener(this);
    // Cancels a running import, then deletes an untitled project's folder (also on the early-exit paths of Main).
    closeProject();
}

void AppController::attachProject(std::unique_ptr<ProjectEdit> project)
{
    if (project == nullptr)
        return;
    edit = std::move(project);
    ++generation;
    commands = std::make_unique<commands::CommandService>(edit->samplerState(), edit->undoManager(), edit->bundleDir(),
                                                          &edit->arrangement());
    savedToken = commands->historyToken();
    edit->arrangement().addChangeListener(this);
    importService = std::make_unique<io::ImportService>(edit->samplerState(), *commands, [this] { return edit->bundleDir(); }, registry);
    importService->onMessage = [this](const io::ImportMessage& message) { handleImportMessage(message); };
    watchedState = edit->samplerState();
    watchedState.addListener(this);
    if (const auto missing = edit->missingFiles(); ! missing.isEmpty())
        toast("Missing audio: " + missing.joinIntoString(", ") + ". Those clips stay silent.", true);
    if (onProjectAttached)
        onProjectAttached();
    sendChangeMessage();
}

void AppController::closeProject()
{
    if (edit == nullptr)
        return;
    if (onProjectClosing)
        onProjectClosing();
    audition->stop();
    edit->transport().stop();
    edit->arrangement().removeChangeListener(this);
    watchedState.removeListener(this);
    watchedState = {};
    importService.reset(); // waits for a running import
    commands.reset();
    edit->discardUntitled(); // an untitled project leaves nothing behind; a saved one is only closed
    edit.reset();
}

void AppController::guardUnsaved(std::function<void()> proceed)
{
    if (edit == nullptr || ! isDirty() || ! askToSave)
    {
        proceed();
        return;
    }
    askToSave(edit->name(), [this, proceed](SaveChoice choice) {
        if (choice == SaveChoice::discard)
            proceed();
        else if (choice == SaveChoice::save)
            saveThen(proceed);
    });
}

void AppController::saveThen(std::function<void()> next)
{
    if (edit->isUntitled())
    {
        saveAsThen(std::move(next));
        return;
    }
    if (edit->save())
    {
        savedToken = commands->historyToken();
        commands->breakCoalescing();
        sendChangeMessage();
        if (next)
            next();
    }
    else
    {
        toast("The project could not be saved", true);
    }
}

void AppController::saveAsThen(std::function<void()> next)
{
    if (! chooseSaveLocation)
        return;
    if (importService != nullptr && importService->isBusy())
    {
        toast("Wait for the import to finish", true); // the worker writes into the folder that would move
        return;
    }
    chooseSaveLocation([this, next](juce::File target) {
        if (target == juce::File())
            return;
        if (importService != nullptr && importService->isBusy()) // an import may have started while the dialog was open
        {
            toast("Wait for the import to finish", true);
            return;
        }
        audition->stop(); // it may be playing from the folder that is about to move
        juce::String error;
        if (! edit->saveAs(target, error))
        {
            toast(error, true);
            return;
        }
        commands->setBundleDir(edit->bundleDir());
        savedToken = commands->historyToken();
        commands->breakCoalescing();
        sendChangeMessage();
        if (next)
            next();
    });
}

void AppController::requestSave()
{
    if (edit != nullptr)
        saveThen({});
}

void AppController::requestSaveAs()
{
    if (edit != nullptr)
        saveAsThen({});
}

bool AppController::refuseWhileImporting()
{
    if (importService == nullptr || ! importService->isBusy())
        return false;
    toast("Wait for the import to finish", true); // closing the project would cancel it
    return true;
}

void AppController::requestNew()
{
    if (refuseWhileImporting())
        return;
    guardUnsaved([this] {
        // The new project is made beside the old one; the old one is only closed once the new one exists.
        juce::String error;
        auto created = ProjectEdit::createUntitled(host, untitledRoot, error);
        if (created == nullptr)
        {
            if (edit != nullptr)
                edit->activateCache();
            toast(error, true);
            return;
        }
        closeProject();
        attachProject(std::move(created));
        sendChangeMessage();
    });
}

void AppController::requestOpen()
{
    if (refuseWhileImporting())
        return;
    guardUnsaved([this] {
        if (! chooseProjectToOpen)
            return;
        chooseProjectToOpen([this](juce::File bundle) {
            if (bundle != juce::File())
                openProject(bundle);
        });
    });
}

bool AppController::openProject(const juce::File& bundle)
{
    if (edit != nullptr && edit->isUntitled() && bundle == edit->bundleDir())
        return true; // closing "the old one" would delete the untitled folder it still uses (a saved folder is never deleted)

    // The new project is opened beside the old one; the old one is only closed once the new one is in.
    juce::String error;
    auto opened = ProjectEdit::open(host, bundle, error);
    if (opened == nullptr)
    {
        if (edit != nullptr)
            edit->activateCache();
        toast(error, true);
        return false;
    }
    closeProject();
    attachProject(std::move(opened));
    sendChangeMessage();
    return true;
}

void AppController::requestQuit()
{
    if (refuseWhileImporting())
        return;
    guardUnsaved([this] {
        closeProject();
        if (onQuitConfirmed)
            onQuitConfirmed();
    });
}

void AppController::importFiles(const juce::Array<juce::File>& files)
{
    if (importService != nullptr)
        importService->importFiles(files);
}

void AppController::importAndPlace(const juce::Array<juce::File>& files, commands::PlacementTarget target)
{
    if (importService != nullptr)
        importService->importFiles(files, target);
}

namespace
{
const juce::Identifier uiNode("UI");
const juce::Identifier snapProperty("snap");
} // namespace

juce::String AppController::snapName() const
{
    if (edit == nullptr)
        return "1 bar";
    const auto ui = edit->samplerState().getChildWithName(uiNode);
    return ui.isValid() && ui.hasProperty(snapProperty) ? ui[snapProperty].toString() : juce::String("1 bar");
}

void AppController::setSnapName(const juce::String& name)
{
    if (edit == nullptr)
        return;
    // A view setting: no undo manager, so it is not an undo step and does not change the history (not dirty).
    auto state = edit->samplerState();
    state.getOrCreateChildWithName(uiNode, nullptr).setProperty(snapProperty, name, nullptr);
    sendChangeMessage();
}

void AppController::toggleAudition(const juce::String& sourceId)
{
    if (edit == nullptr)
        return;
    const auto now = juce::Time::getMillisecondCounter();

    if (! host.isAudioReady())
    {
        queue.click(sourceId, now);
        sendChangeMessage();
        return;
    }
    if (audition->playingId() == sourceId)
    {
        audition->stop();
        sendChangeMessage();
        return;
    }
    playNow(sourceId);
}

void AppController::playNow(const juce::String& sourceId)
{
    if (edit == nullptr)
        return;
    const auto source = model::SamplerState(edit->samplerState()).findSourceById(sourceId);
    if (! source.isValid())
        return;
    if (! audition->play(edit->bundleDir().getChildFile(source.decoded()), sourceId))
        toast(source.name() + " could not be played", true);
    sendChangeMessage();
}

void AppController::timerCallback()
{
    const bool ended = audition->poll();
    if (queue.dropIfExpired(juce::Time::getMillisecondCounter()))
    {
        toast("No audio output is available", true);
        sendChangeMessage();
    }
    if (ended || lastPlayingId != audition->playingId())
    {
        lastPlayingId = audition->playingId();
        sendChangeMessage();
    }
}

void AppController::handleImportMessage(const io::ImportMessage& message)
{
    toast(message.text, message.kind == io::ImportMessage::Kind::error);
    if (message.kind == io::ImportMessage::Kind::duplicate && message.sourceId.isNotEmpty() && onSelectSource)
        onSelectSource(message.sourceId);
}

void AppController::toast(const juce::String& text, bool isError) const
{
    if (onToast)
        onToast(text, isError);
}

std::vector<ui::BrowserRow> AppController::browserRows() const
{
    std::vector<ui::BrowserRow> rows;
    if (edit == nullptr)
        return rows;

    rows = ui::BrowserModel::rowsFrom(model::SamplerState(edit->samplerState()));
    for (const auto& name : registry.allPendingNames())
        rows.push_back({{}, name, true});
    return rows; // the browser filters and sorts them
}

std::unique_ptr<juce::AudioThumbnailBase> AppController::makeThumbnail(const juce::String& sourceId, juce::Component& repaintTarget)
{
    if (edit == nullptr)
        return nullptr;
    const auto source = model::SamplerState(edit->samplerState()).findSourceById(sourceId);
    if (! source.isValid())
        return nullptr;
    const auto file = edit->bundleDir().getChildFile(source.decoded());
    return file.existsAsFile() ? host.createThumbnail(file, repaintTarget) : nullptr;
}

juce::String AppController::audioStatusText() const
{
    if (host.isAudioReady())
    {
        const auto rate = host.currentSampleRate();
        const auto latencyMs = rate > 0.0 ? 1000.0 * host.currentBufferSize() / rate : 0.0;
        return "Audio: " + juce::String(rate / 1000.0, 1) + " kHz, " + juce::String(latencyMs, 1) + " ms";
    }
    if (juce::Time::getMillisecondCounter() - startedAtMs > AuditionRequestQueue::timeoutMs && host.currentAudioDeviceName().isEmpty())
        return "Audio: no output device";
    return "Audio: starting...";
}

juce::String AppController::windowTitle() const
{
    const auto name = edit != nullptr ? edit->name() : juce::String("Sampler");
    return name + (isDirty() ? "*" : "") + " - Sampler";
}
} // namespace sampler::app
