#include "ProjectEdit.h"

#include "Arrangement.h"
#include "EditTakeRecorder.h"
#include "EngineHost.h"
#include "LevelMeters.h"
#include "Transport.h"
#include "EngineSetup.h"
#include "model/Migrations.h"
#include "model/SamplerState.h"

namespace te = tracktion::engine;

namespace sampler
{
namespace
{
/** Deletes a folder, trying again briefly: a scanner or a thumbnail may still hold a file for a moment. */
bool deleteFolder(const juce::File& folder)
{
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (! folder.exists() || folder.deleteRecursively())
            return true;
        juce::Thread::sleep(50);
    }
    juce::Logger::writeToLog("Could not delete " + folder.getFullPathName());
    return false;
}
} // namespace

namespace detail
{
bool moveBundleFolder(const juce::File& source, const juce::File& destination, bool tryRename)
{
    if (tryRename && source.moveFileTo(destination))
        return true;
    // A rename cannot cross volumes: copy everything, check the project file arrived, then remove the original.
    if (! source.copyDirectoryTo(destination) || ! destination.getChildFile(ProjectEdit::editFileName).existsAsFile())
    {
        deleteFolder(destination);
        return false;
    }
    deleteFolder(source);
    return true;
}
} // namespace detail

struct ProjectEdit::Impl
{
    Impl(EngineHost& h, const juce::File& bundle, bool isUntitled) : host(h), bundleDir(bundle), untitled(isUntitled) {}

    ~Impl()
    {
        // The facades refer to the edit: they go first.
        recorder.reset();
        meters.reset();
        transport.reset();
        arrangement.reset();
    }

    /** Builds the facades over the loaded edit, adds the default track and notes missing files. */
    void attach()
    {
        samplerNode = edit->state.getChildWithName(model::ids::SAMPLER);
        arrangement = std::make_unique<engine::Arrangement>(*edit);
        arrangement->addDefaultTrackIfEmpty();
        transport = std::make_unique<engine::Transport>(*edit, host);
        meters = std::make_unique<engine::LevelMeters>(*edit);
        recorder = std::make_unique<engine::EditTakeRecorder>(*edit);
        for (const auto& clip : arrangement->clips())
        {
            if (! clip.missing)
                continue;
            const auto source = model::SamplerState(samplerNode).findSourceById(clip.sourceId);
            missing.addIfNotAlreadyThere(source.isValid() ? juce::File(source.decoded()).getFileName() : clip.name);
        }
    }

    EngineHost& host;
    juce::File bundleDir;
    bool untitled;
    std::unique_ptr<te::Edit> edit;
    juce::ValueTree samplerNode;
    std::unique_ptr<engine::Arrangement> arrangement;
    std::unique_ptr<engine::Transport> transport;
    std::unique_ptr<engine::LevelMeters> meters;
    std::unique_ptr<engine::EditTakeRecorder> recorder;
    juce::StringArray missing;
};

ProjectEdit::ProjectEdit(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
ProjectEdit::~ProjectEdit() = default;

juce::File ProjectEdit::defaultUntitledRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Sampler").getChildFile("untitled");
}

std::unique_ptr<ProjectEdit> ProjectEdit::createUntitled(EngineHost& host, const juce::File& untitledRoot, juce::String& error)
{
    const auto bundle = untitledRoot.getChildFile(juce::Uuid().toDashedString() + bundleExtension);
    auto impl = std::make_unique<Impl>(host, bundle, true);
    const auto fail = [&](const juce::String& reason) -> std::unique_ptr<ProjectEdit> {
        impl->recorder.reset();
        impl->meters.reset();
        impl->transport.reset();
        impl->arrangement.reset();
        impl->edit.reset(); // the edit must let go of its files before the half-made folder goes
        deleteFolder(bundle);
        error = reason;
        return nullptr;
    };

    for (const auto& dir : {bundle, bundle.getChildFile("audio/originals"), bundle.getChildFile("cache")})
        if (! dir.createDirectory())
            return fail("Cannot create the project folder " + dir.getFullPathName());

    auto& engine = host.tracktionEngine();
    if (! engine.getTemporaryFileManager().setTempDirectory(bundle.getChildFile("cache")))
        return fail("Cannot use the project's cache folder");

    impl->edit = te::createEmptyEdit(engine, bundle.getChildFile(editFileName));
    if (impl->edit == nullptr)
        return fail("Cannot create the project");
    impl->edit->state.appendChild(model::SamplerState::createEmpty(), nullptr);
    // Tracktion starts a new edit's master at -3 dB; ours starts at 0 dB like the tracks (not an undo step).
    if (auto master = impl->edit->getMasterVolumePlugin())
        master->setVolumeDb(0.0f);
    impl->edit->getUndoManager().clearUndoHistory();
    impl->attach(); // with "Audio 1", before the first save
    if (! detail::saveEditAtomically(*impl->edit))
        return fail("Cannot save the new project file");

    return std::unique_ptr<ProjectEdit>(new ProjectEdit(std::move(impl)));
}

std::unique_ptr<ProjectEdit> ProjectEdit::open(EngineHost& host, const juce::File& bundle, juce::String& error)
{
    const auto name = bundle.getFileName();
    const auto editPath = bundle.getChildFile(editFileName);
    if (! bundle.isDirectory() || ! editPath.existsAsFile())
    {
        error = name + " is not a Sampler project (" + editFileName + " is missing).";
        return nullptr;
    }

    // Read the SAMPLER section ourselves first, so that a newer file is refused without Tracktion touching it.
    const auto xml = juce::XmlDocument::parse(editPath);
    if (xml == nullptr || ! xml->hasTagName("EDIT"))
    {
        error = name + " cannot be opened: the project file is damaged.";
        return nullptr;
    }
    if (const auto* section = xml->getChildByName("SAMPLER"))
    {
        auto probe = juce::ValueTree::fromXml(*section);
        switch (model::migrateToCurrent(probe))
        {
            case model::MigrationResult::tooNew:
                error = name + " was created by a newer version of Sampler and cannot be opened.";
                return nullptr;
            case model::MigrationResult::invalid:
                error = name + " cannot be opened: its Sampler data is damaged.";
                return nullptr;
            default: break;
        }
    }

    for (const auto& dir : {bundle.getChildFile("audio/originals"), bundle.getChildFile("cache")})
        dir.createDirectory();

    auto& engine = host.tracktionEngine();
    if (! engine.getTemporaryFileManager().setTempDirectory(bundle.getChildFile("cache")))
    {
        error = "Cannot use the cache folder of the project";
        return nullptr;
    }

    auto impl = std::make_unique<Impl>(host, bundle, false);
    impl->edit = te::loadEditFromFile(engine, editPath);
    if (impl->edit == nullptr)
    {
        error = name + " cannot be opened: the project file could not be loaded.";
        return nullptr;
    }

    auto section = impl->edit->state.getChildWithName(model::ids::SAMPLER);
    if (! section.isValid())
        impl->edit->state.appendChild(model::SamplerState::createEmpty(), nullptr); // an edit made before M1
    else
        model::migrateToCurrent(section);

    impl->attach();
    return std::unique_ptr<ProjectEdit>(new ProjectEdit(std::move(impl)));
}

bool ProjectEdit::saveAs(const juce::File& requested, juce::String& error)
{
    auto destination = requested;
    if (! destination.getFileName().endsWithIgnoreCase(bundleExtension))
        destination = destination.getSiblingFile(destination.getFileName() + bundleExtension);

    const auto source = impl->bundleDir;
    if (destination == source || destination.isAChildOf(source))
    {
        error = "Choose a folder outside the project itself.";
        return false;
    }
    if (destination.exists())
    {
        error = destination.getFileName() + " already exists.";
        return false;
    }
    if (! destination.getParentDirectory().createDirectory())
    {
        error = "Cannot create " + destination.getParentDirectory().getFullPathName();
        return false;
    }

    const bool wasUntitled = impl->untitled;
    if (wasUntitled)
    {
        if (! detail::moveBundleFolder(source, destination))
        {
            error = "Cannot move the project to " + destination.getFullPathName();
            return false;
        }
    }
    else
    {
        // The project file is written by the save below; the audio is copied, the cache is not.
        if (! destination.createDirectory() || ! source.getChildFile("audio").copyDirectoryTo(destination.getChildFile("audio")))
        {
            deleteFolder(destination);
            error = "Cannot copy the project to " + destination.getFullPathName();
            return false;
        }
        destination.getChildFile("audio/originals").createDirectory();
        destination.getChildFile("cache").createDirectory();
    }

    // Switch to the new folder; go back if the project file cannot be written there.
    const auto newEditFile = destination.getChildFile(editFileName);
    impl->bundleDir = destination;
    impl->untitled = false;
    impl->edit->editFileRetriever = [newEditFile] { return newEditFile; };
    impl->host.tracktionEngine().getTemporaryFileManager().setTempDirectory(destination.getChildFile("cache"));

    if (! save())
    {
        const auto oldEditFile = source.getChildFile(editFileName);
        impl->bundleDir = source;
        impl->untitled = wasUntitled;
        impl->edit->editFileRetriever = [oldEditFile] { return oldEditFile; };
        if (wasUntitled)
            detail::moveBundleFolder(destination, source); // the folder that was moved goes back
        else
            deleteFolder(destination);
        impl->host.tracktionEngine().getTemporaryFileManager().setTempDirectory(source.getChildFile("cache"));
        error = "Cannot write the project file.";
        return false;
    }
    return true;
}

void ProjectEdit::discardUntitled()
{
    if (! impl->untitled)
        return;
    impl->recorder.reset();
    impl->meters.reset();
    impl->transport.reset();
    impl->arrangement.reset();
    impl->edit.reset();
    deleteFolder(impl->bundleDir);
}

juce::ValueTree ProjectEdit::samplerState() const { return impl->samplerNode; }
engine::Arrangement& ProjectEdit::arrangement() { return *impl->arrangement; }
const engine::Arrangement& ProjectEdit::arrangement() const { return *impl->arrangement; }
engine::Transport& ProjectEdit::transport() { return *impl->transport; }
engine::LevelMeters& ProjectEdit::levelMeters() { return *impl->meters; }
engine::TakeRecorder& ProjectEdit::takeRecorder() { return *impl->recorder; }
te::Edit& ProjectEdit::tracktionEdit() { return *impl->edit; }
juce::StringArray ProjectEdit::missingFiles() const { return impl->missing; }
juce::UndoManager& ProjectEdit::undoManager() { return impl->edit->getUndoManager(); }

juce::File ProjectEdit::bundleDir() const { return impl->bundleDir; }
juce::File ProjectEdit::editFile() const { return impl->bundleDir.getChildFile(editFileName); }
juce::File ProjectEdit::audioDir() const { return impl->bundleDir.getChildFile("audio"); }
juce::File ProjectEdit::originalsDir() const { return impl->bundleDir.getChildFile("audio/originals"); }
juce::File ProjectEdit::cacheDir() const { return impl->bundleDir.getChildFile("cache"); }

juce::String ProjectEdit::name() const
{
    return impl->untitled ? juce::String("Untitled") : impl->bundleDir.getFileNameWithoutExtension();
}

bool ProjectEdit::isUntitled() const { return impl->untitled; }

void ProjectEdit::activateCache()
{
    impl->host.tracktionEngine().getTemporaryFileManager().setTempDirectory(impl->bundleDir.getChildFile("cache"));
}

void ProjectEdit::prepareForPlayback() { impl->edit->getTransport().ensureContextAllocated(); }

bool ProjectEdit::save() { return detail::saveEditAtomically(*impl->edit); }
} // namespace sampler
