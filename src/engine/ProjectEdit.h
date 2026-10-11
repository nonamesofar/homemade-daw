#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace tracktion { inline namespace engine
{
class Edit;
}} // namespace tracktion::engine

namespace sampler
{
class EngineHost;

namespace engine
{
class Arrangement;
class LevelMeters;
class Transport;
} // namespace engine

namespace detail
{
/**
    Moves a bundle folder to `destination` (which must not exist). `tryRename` first tries a plain rename; when that
    fails (a different drive) or is skipped, copies the whole folder, checks that the project file arrived, and only
    then deletes `source`. On failure `destination` is removed and `source` is left as it was.
*/
bool moveBundleFolder(const juce::File& source, const juce::File& destination, bool tryRename = true);
} // namespace detail

/**
    The open project: a Tracktion edit plus its bundle folder (`Name.sdaw/`). The header exposes no Tracktion types.
    What the rest of the app sees is the SAMPLER ValueTree (the document that commands edit), the arrangement facade,
    the transport, the level meters, the edit's undo manager and the bundle folder. (Whether the project has unsaved
    changes comes from the undo history in CommandService, design D-4.)

    Bundle layout (docs/projects.md):
        project.tracktionedit   the edit (Tracktion XML) with our <SAMPLER schemaVersion="1"> section inside
        audio/originals/        copies of the files the user imported
        audio/                  decoded working copies (32-bit float WAV)
        cache/                  disposable: thumbnails, proxies, renders (the engine temp directory points here)

    A project without audio tracks (new, or saved by M1) gets one track "Audio 1" when it is created or opened, outside
    the undo history and without rewriting the file (design D-12).

    Must be destroyed before the EngineHost it was created with. Message thread only.
*/
class ProjectEdit
{
public:
    static constexpr const char* editFileName = "project.tracktionedit";
    static constexpr const char* bundleExtension = ".sdaw";

    /** Where untitled projects live until the first save: `<app data>/Sampler/untitled`. */
    static juce::File defaultUntitledRoot();

    /**
        Creates an empty untitled project in a new `<untitledRoot>/<uuid>.sdaw` folder: saves the edit at once (Tracktion
        needs the file on disk before relative paths can be stored, see docs/m0-findings.md), creates `audio/originals`
        and `cache`, and points the engine's temporary directory at `cache`. Null with `error` set on failure.
    */
    static std::unique_ptr<ProjectEdit> createUntitled(EngineHost&, const juce::File& untitledRoot, juce::String& error);

    /**
        Opens `bundle` (a `Name.sdaw` folder). Before Tracktion sees the file, its SAMPLER section is read: a schema
        version newer than this build is refused (nothing is touched), a file without the section gets an empty one.
        Null with `error` set (a sentence for the user) if the folder is not a readable project.
    */
    static std::unique_ptr<ProjectEdit> open(EngineHost&, const juce::File& bundle, juce::String& error);

    ~ProjectEdit();

    /** The SAMPLER node inside the edit. The same node for the life of the project. */
    juce::ValueTree samplerState() const;
    /** Tracks, clips, mixer, tempo and loop range (design D-1). */
    engine::Arrangement& arrangement();
    const engine::Arrangement& arrangement() const;
    engine::Transport& transport();
    engine::LevelMeters& levelMeters();
    /** The Tracktion edit, for code inside src/engine only (include EngineSetup.h to use it). */
    tracktion::engine::Edit& tracktionEdit();
    /** Names of the decoded files that clips use but that are missing (found when the project was opened). */
    juce::StringArray missingFiles() const;
    /** The edit's own undo manager: our commands record into it. */
    juce::UndoManager& undoManager();

    juce::File bundleDir() const;
    juce::File editFile() const;
    juce::File audioDir() const;
    juce::File originalsDir() const;
    juce::File cacheDir() const;

    /** The bundle name without extension, or "Untitled" for a project that has never been saved. */
    juce::String name() const;
    bool isUntitled() const;

    /** Gives the edit its audio playback context (what pressing play on it will use). */
    void prepareForPlayback();

    /** Points the engine's temporary directory at this project's cache (done on open; redo it after a failed open of another). */
    void activateCache();

    /** Saves the edit atomically. Does not touch the undo history. */
    bool save();

    /**
        Saves the project under a new bundle folder (".sdaw" is added if missing) and makes that its home. Keeps the
        undo history. An untitled project's folder is moved; a saved one is copied (`audio/`, not `cache/`). Fails,
        changing nothing, if the folder already exists, lies inside this project, or the project file cannot be written there.
    */
    bool saveAs(const juce::File& newBundle, juce::String& error);

    /** Closes the edit and deletes the bundle folder of an untitled project. Does nothing for a saved project. */
    void discardUntitled();

private:
    struct Impl;
    explicit ProjectEdit(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
} // namespace sampler
