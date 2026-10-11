#include "ProjectEdit.h"

#include "EngineHost.h"
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

struct ProjectEdit::Impl : private juce::ValueTree::Listener
{
    Impl(EngineHost& h, const juce::File& bundle, bool isUntitled) : host(h), bundleDir(bundle), untitled(isUntitled) {}

    ~Impl() override { samplerNode.removeListener(this); }

    /** Starts counting changes of the SAMPLER section (the user's edits in M1; M2 extends this to tracks and clips). */
    void watch()
    {
        samplerNode = edit->state.getChildWithName(model::ids::SAMPLER);
        samplerNode.addListener(this);
    }

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override { ++changeCount; }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override { ++changeCount; }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override { ++changeCount; }
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override { ++changeCount; }

    EngineHost& host;
    juce::File bundleDir;
    bool untitled;
    std::unique_ptr<te::Edit> edit;
    juce::ValueTree samplerNode;
    int changeCount = 0;
    int savedChangeCount = 0;
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
    if (! detail::saveEditAtomically(*impl->edit))
        return fail("Cannot save the new project file");

    impl->watch();
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

    impl->watch();
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
    impl->edit.reset();
    deleteFolder(impl->bundleDir);
}

juce::ValueTree ProjectEdit::samplerState() const { return impl->samplerNode; }
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

bool ProjectEdit::save()
{
    if (! detail::saveEditAtomically(*impl->edit))
        return false;
    impl->savedChangeCount = impl->changeCount;
    return true;
}

bool ProjectEdit::isDirty() const { return impl->changeCount != impl->savedChangeCount; }
} // namespace sampler
