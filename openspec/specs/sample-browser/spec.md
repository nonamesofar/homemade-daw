# sample-browser Specification

## Purpose

Defines the browser panel that lists the current project's sounds, lets the user find them by name and category, and shows a waveform for each one.

## Requirements

### Requirement: Lists the current project's sounds
The browser SHALL list every sound in the open project, one row each, with a waveform thumbnail, the name and the category, sorted by name. It SHALL NOT list sounds from other projects.

#### Scenario: Reopened project
- **WHEN** a project with four sounds is opened
- **THEN** the browser lists exactly those four sounds

#### Scenario: Empty project
- **WHEN** a new project is created
- **THEN** the browser is empty and shows the "Drop audio here" hint

### Requirement: Search by name
The browser SHALL filter the list as the user types, keeping sounds whose name contains the typed text, ignoring case. Clearing the text shows all sounds again.

#### Scenario: Incremental filter
- **WHEN** the project has "Kick 01", "Snare" and "kick-long" and the user types "kick"
- **THEN** the list shows "Kick 01" and "kick-long" only

#### Scenario: No match
- **WHEN** the typed text matches no sound
- **THEN** the list is empty and says that nothing matches

### Requirement: Category filter
The browser SHALL offer the places All samples, Loops, One-shots and Recordings; choosing one shows only sounds of that category, combined with the search text. Recordings lists the takes recorded onto tracks.

#### Scenario: One-shots only
- **WHEN** the user chooses One-shots
- **THEN** only sounds with category One-shot are listed

#### Scenario: Recordings is empty for now
- **WHEN** the user chooses Recordings in a project without recorded takes
- **THEN** the list is empty

#### Scenario: Recordings lists takes
- **WHEN** the project has two imported loops and one recorded take, and the user chooses Recordings
- **THEN** only the take is listed

### Requirement: User can change a sound's category
The user SHALL be able to set a sound's category to Loop, One-shot or Recording from the row's context menu, as an undoable step.

#### Scenario: Reclassify
- **WHEN** the user sets "Pad chord" from Loop to One-shot
- **THEN** it appears under One-shots, the change survives save and reopen, and Undo sets it back to Loop

#### Scenario: Take becomes a loop
- **WHEN** the user sets a take's category to Loop
- **THEN** it leaves Recordings and appears under Loops

### Requirement: Waveform thumbnails
Each row SHALL show a small waveform of the sound, drawn once it is available, and generated waveform data SHALL be stored only in the project's cache folder.

#### Scenario: Thumbnail appears
- **WHEN** a sound finishes importing
- **THEN** its row shows its waveform shortly after

#### Scenario: Cache can be deleted
- **WHEN** the project's cache folder is deleted and the project is reopened
- **THEN** every row shows its waveform again

### Requirement: Browser stays fast with many sounds
The browser SHALL scroll smoothly and filter without visible delay with at least 1,000 sounds in a project.

#### Scenario: Large project
- **WHEN** a project has 1,000 sounds and the user types into the search box
- **THEN** the list updates with each key press without a noticeable pause

### Requirement: Takes show where they came from
A take's row SHALL show, in its tooltip, the input it was recorded from (the device, Master or the track name) and when.

#### Scenario: Tooltip
- **WHEN** the user hovers over a take recorded from Master
- **THEN** the tooltip says it was recorded from Master, with the date and time
