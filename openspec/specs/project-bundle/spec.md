# project-bundle Specification

## Purpose

Defines how a project is stored as a self-contained folder so it can be moved, reopened and extended with the app's own per-clip data.

## Requirements

### Requirement: Project is a movable folder
Audio files referenced by a project SHALL be stored by path relative to the project folder, so the folder can be moved or renamed and still open.

#### Scenario: Move and reopen
- **WHEN** a saved project folder is moved to a different location and opened
- **THEN** all clips load with their audio and no file is reported missing

### Requirement: App-specific clip data survives save and load
Extra properties the app attaches to a clip SHALL survive a save and reopen unchanged.

#### Scenario: Round trip
- **WHEN** a clip has extra properties for source id, warp mode and source BPM, and the project is saved and reopened
- **THEN** the clip has the same values for all three properties

### Requirement: Derived files live in the cache folder
Generated files (engine proxies, thumbnails) SHALL be written inside the project's cache folder and never next to the user's audio.

#### Scenario: Waveform thumbnails
- **WHEN** clips are shown in the timeline
- **THEN** generated thumbnail and proxy files appear only under the project's cache folder

### Requirement: Original audio is never modified
Opening, playing, editing and saving SHALL NOT modify the audio files.

#### Scenario: File hashes unchanged
- **WHEN** a project is opened, edited, rendered and saved
- **THEN** every audio file has the same content hash as before

### Requirement: Saves are atomic
Saving SHALL NOT leave a half-written project file if interrupted.

#### Scenario: Interrupted save
- **WHEN** the app is killed during a save
- **THEN** the previous project file is still intact and loadable

### Requirement: Create, save and open projects from the app
The app SHALL create a new project, save it to a `.sdaw` folder the user names, save it again in place, save it under a new name and location, and open a saved project.

#### Scenario: Save and reopen
- **WHEN** the user imports three sounds, saves the project as "Beat1", quits and opens "Beat1.sdaw"
- **THEN** the browser lists the same three sounds with the same names and categories, and they play when auditioned

#### Scenario: Save As makes an independent copy
- **WHEN** the user saves "Beat1" as "Beat2" and then imports a sound
- **THEN** after saving, "Beat2" contains the new sound and "Beat1" is unchanged

### Requirement: Untitled projects work before the first save
A new project SHALL accept imports before it has been saved. Its folder SHALL live in the app's own data folder until the first save, which moves it to the place the user chooses; discarding an untitled project SHALL delete that folder.

#### Scenario: Import, then save
- **WHEN** the user starts the app, imports a sound, then saves as "First.sdaw" on the desktop
- **THEN** "First.sdaw" holds the sound's files, opens on its own and its sound plays

#### Scenario: Discard untitled
- **WHEN** the user imports a sound into an untitled project and quits, choosing Discard
- **THEN** no folder for that project remains in the app's data folder

### Requirement: Imported audio layout in the project folder
A project folder SHALL keep imported originals in `audio/originals/`, decoded working copies in `audio/`, and generated files in `cache/`. Deleting `cache/` SHALL lose nothing.

#### Scenario: Folder contents after import
- **WHEN** one MP3 is imported and the project is saved
- **THEN** the folder holds the project file, the MP3 under `audio/originals/` and a WAV under `audio/`

### Requirement: App data carries a schema version
The project file SHALL hold the app's own data in one section marked with an integer schema version, starting at 1.

#### Scenario: Version written
- **WHEN** a new project is saved
- **THEN** its project file contains the app's data section with schema version 1

### Requirement: Newer project files are refused
Opening a project whose schema version is newer than the app supports SHALL be refused with a message saying it was made by a newer version, and the file SHALL NOT be changed.

#### Scenario: Newer file
- **WHEN** the user opens a project with schema version 99
- **THEN** a message says it was created by a newer version, the current project stays open and the file is unchanged

### Requirement: Missing or broken projects are reported
Opening a folder that is not a readable project SHALL show a message and keep the current project open.

#### Scenario: Not a project
- **WHEN** the user opens a `.sdaw` folder whose project file is missing or not valid
- **THEN** a message says the project cannot be opened and the current project is unchanged

### Requirement: The arrangement is saved with the project
Saving SHALL store the tracks (order, name, colour, volume, pan, mute, solo), the clips (track, position, trims, gain, reverse, which sound they play), the tempo, the time signature, the loop range and loop on/off, the metronome setting and the snap value; reopening the project SHALL restore all of them.

#### Scenario: Round trip
- **WHEN** the user builds a 4-track arrangement with trimmed, reversed and split clips, changes the tempo, mixer and loop, saves, quits and reopens the project
- **THEN** the arrangement looks and sounds the same as before saving

### Requirement: Arrangement edits mark the project as changed
Every undoable arrangement edit (and its undo or redo) SHALL mark the project as having unsaved changes. Playing, stopping, moving the playhead, selecting, zooming, scrolling, turning the loop on or off, and auditioning SHALL NOT.

#### Scenario: Move a clip
- **WHEN** a saved project is open and the user moves a clip
- **THEN** the window title shows the unsaved marker

#### Scenario: Play and zoom
- **WHEN** a saved project is open and the user plays it, stops, zooms and selects clips
- **THEN** the window title shows no unsaved marker

### Requirement: Projects from earlier versions still open
Projects saved before the arrangement existed SHALL open without a conversion step: they get one empty audio track, 120 BPM, 4/4, 1-bar snap and no loop range.

#### Scenario: Open an M1 project
- **WHEN** the user opens a project saved by the previous version that has sounds but no tracks
- **THEN** it opens with its sounds in the browser and one empty track, and its file is not rewritten until the user saves

### Requirement: Clips refer to the project's sounds
Every clip SHALL play a sound of the same project, stored by a path relative to the project folder. A clip whose sound file is missing on open SHALL be kept, drawn as missing, and stay silent, and the user is told which files are missing.

#### Scenario: Missing decoded file
- **WHEN** a sound's working copy is deleted from the project folder and the project is opened
- **THEN** its clips are drawn as missing and silent and a message names the missing file, while the other clips play

### Requirement: Takes live in the project folder
Recorded takes SHALL be written to `audio/recordings/` inside the project folder, named `rec-<date>-<time>-<n>.wav`, and referenced relative to the project folder like imported audio, so they move with the project. A take is used as it is: it is not copied a second time. Take files are never modified after they are finished.

#### Scenario: Folder contents after a recording
- **WHEN** the user records one take and saves the project
- **THEN** the folder holds one WAV under `audio/recordings/` and no other copy of it outside `cache/`

#### Scenario: Recording in an untitled project
- **WHEN** the user records in an untitled project and then saves it as "Grab.sdaw"
- **THEN** "Grab.sdaw" holds the take under `audio/recordings/` and it plays after reopening

#### Scenario: Moved project
- **WHEN** a project with a take is moved to another folder and opened
- **THEN** the take plays and is listed under Recordings

### Requirement: Track inputs are part of the project
Each track's input SHALL be saved with the project. Projects saved before track inputs existed SHALL open with every track on System audio of the default device, without being marked as changed.

#### Scenario: Older project
- **WHEN** a project saved by the M2 version is opened
- **THEN** every track's input is System audio on the default device and the title shows no unsaved marker
