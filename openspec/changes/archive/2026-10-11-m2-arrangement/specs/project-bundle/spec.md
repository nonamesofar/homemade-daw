# Spec Delta

## ADDED Requirements

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
