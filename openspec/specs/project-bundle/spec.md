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
