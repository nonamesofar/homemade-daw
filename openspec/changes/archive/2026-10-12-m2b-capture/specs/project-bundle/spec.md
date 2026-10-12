# Spec Delta

## ADDED Requirements

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
