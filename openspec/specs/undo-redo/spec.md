# undo-redo Specification

## Purpose

Defines how changes to a project are made and reversed: every edit is one named, undoable step, applied through one path, and the project stays consistent after every step.

## Requirements

### Requirement: Every project edit is one undo step
Each user action that changes the project SHALL be recorded as exactly one undo step with a readable name, however many internal changes it makes.

#### Scenario: Import is one step
- **WHEN** the user imports one file and then chooses Undo once
- **THEN** the sound is gone from the project and the browser, and the project is the same as before the import

#### Scenario: Multi-file drop is one step
- **WHEN** the user drops three files at once and then chooses Undo once
- **THEN** all three sounds are removed

### Requirement: Redo
After an undo, the app SHALL redo the undone step until a new edit is made, which clears the redo history.

#### Scenario: Redo restores
- **WHEN** the user undoes an import and then chooses Redo
- **THEN** the sound is back in the project and the browser with the same name, category and waveform

#### Scenario: New edit clears redo
- **WHEN** the user undoes an import and then changes another sound's category
- **THEN** Redo is disabled

### Requirement: Undo restores the exact previous state
Undoing a step SHALL return the saved form of the project to exactly what it was before that step.

#### Scenario: Round trip on the saved form
- **WHEN** any sequence of edits is applied and then undone step by step
- **THEN** after each undo the project's saved form equals its saved form before the matching edit

### Requirement: What is not undoable
Saving, opening, auditioning, browser search text, category filter selection and scrolling SHALL NOT create undo steps.

#### Scenario: Audition is not undone
- **WHEN** the user auditions a sound and then chooses Undo
- **THEN** the undo reverts the last edit made before the audition, not the audition

### Requirement: Undo history is per project and not saved
The undo history SHALL start empty when a project is created or opened and SHALL NOT be stored in the project.

#### Scenario: Reopen starts fresh
- **WHEN** the user imports a file, saves, closes and reopens the project
- **THEN** Undo is disabled

### Requirement: Saving does not change undo history
Saving SHALL keep the undo and redo history unchanged; undoing after a save SHALL mark the project as having unsaved changes.

#### Scenario: Undo after save
- **WHEN** the user imports a file, saves, then chooses Undo
- **THEN** the sound is removed and the window title shows the unsaved marker

### Requirement: Consistency checks in development builds
In development builds, the app SHALL check after every edit that ids are unique and every sound's files are inside the project, and stop with a clear message if not.

#### Scenario: Duplicate id is caught
- **WHEN** a development build applies an edit that would leave two sounds with the same id
- **THEN** the check fails at once, naming the edit

### Requirement: A gesture is one undo step
A continuous gesture (dragging clips, a clip edge, the loop range, a track header, a volume or pan control, the tempo or the clip gain) SHALL form one undo step from mouse-down to mouse-up, however many intermediate values it passed through. A gesture cancelled with Escape SHALL leave no undo step.

#### Scenario: Volume drag
- **WHEN** the user drags a track's volume from 0 dB through many values to -12 dB and releases, then chooses Undo once
- **THEN** the volume is 0 dB again

### Requirement: Undo step names describe the edit
Arrangement undo steps SHALL have names the Edit menu shows, such as "Move clip", "Move 3 clips", "Trim clip", "Split clip", "Duplicate 2 clips", "Delete clip", "Paste 4 clips", "Add track", "Delete track", "Move track", "Rename track", "Set track colour", "Set volume", "Set pan", "Mute", "Solo", "Set loop", "Set tempo", "Set time signature", "Set clip gain", "Set clip start", "Reverse clip".

#### Scenario: Menu shows the name
- **WHEN** the user moves three clips
- **THEN** the Edit menu shows "Undo Move 3 clips"

### Requirement: Selection after undo and redo
Undo and redo SHALL keep selected those clips that still exist afterwards and drop the others from the selection; clips that undo or redo brings back are selected. Selection changes themselves are not undo steps.

#### Scenario: Undo a delete
- **WHEN** the user deletes two selected clips and then chooses Undo
- **THEN** both clips are back and selected

### Requirement: Arrangement actions that are not undoable
Play, stop, moving the playhead, turning looping or the metronome on or off, choosing the snap value, zooming, scrolling, selecting and copying SHALL NOT create undo steps.

#### Scenario: Undo skips transport
- **WHEN** the user moves a clip, then plays, stops, toggles the loop and zooms, then chooses Undo
- **THEN** the clip moves back and the loop, zoom and playhead stay as they are

### Requirement: Undo restores the exact arrangement
Undoing any arrangement edit SHALL return the saved form of the project to exactly what it was before that edit, and redo SHALL return it to exactly what it was after.

#### Scenario: Random edits
- **WHEN** any sequence of arrangement edits is applied and then undone step by step
- **THEN** after each undo the saved form equals the saved form before the matching edit, and clips never overlap at any step

### Requirement: A recording is one undo step
Adding the takes of one recording (their sounds and clips, and any clips they shortened or split) SHALL be one undo step named "Record take" or "Record N takes". Undoing it removes the sounds and clips and restores what was under them; the take files stay in the project folder so Redo brings them back.

#### Scenario: Undo a take
- **WHEN** a take was recorded over an existing clip and the user chooses Undo once
- **THEN** the take's sound and clip are gone, the old clip is whole again, and the Edit menu had shown "Undo Record take"

#### Scenario: Redo
- **WHEN** the user then chooses Redo
- **THEN** the take and its clip are back and play

### Requirement: Recording controls are not undoable
Arming and disarming tracks, starting and stopping a recording, and the output mute during a recording SHALL NOT create undo steps. Setting a track's input is undoable ("Set track input").

#### Scenario: Undo skips arm and record
- **WHEN** the user moves a clip, then arms a track and records a silent take, then chooses Undo
- **THEN** the clip moves back and the track stays armed

### Requirement: Chopping edits are named undo steps
Each chopping edit SHALL be one undo step with a name the Edit menu shows: "Slice by beat", "Slice by transient", "Slice manually", "Auto-slice", "Add slice point", "Move slice point", "Remove slice point", "Set slice start", "Set slice end", "Set slice pitch", "Set slice gain", "Reverse slice", "Set play mode", "Set sound tempo", "Move first beat", "Slices to new track". Dragging a slice point or the first beat is one step from mouse-down to mouse-up.

#### Scenario: Menu shows the name
- **WHEN** the user drags a slice point and releases it
- **THEN** the Edit menu shows "Undo Move slice point", and one Undo puts the point back

#### Scenario: Undo from the editor
- **WHEN** the sample editor has focus, the user removes a slice point and presses Mod+Z
- **THEN** the point is back, and the editor stays open on the same sound

### Requirement: Sample editor actions that are not undoable
Opening and leaving the sample editor, zooming and scrolling the waveform, selecting a slice, moving the Sensitivity slider before "Auto-slice", choosing a pad bank, switching the START/END units, playing pads and auditioning SHALL NOT create undo steps or mark the project as unsaved.

#### Scenario: Undo skips the editor view
- **WHEN** the user moves a clip, opens a sound in the editor, zooms, selects slice 5 and plays three pads, then chooses Undo
- **THEN** the clip moves back and the editor stays open at the same zoom with slice 5 selected

### Requirement: Undo that removes the open sound closes the editor
If an undo or redo removes the sound open in the sample editor, the app SHALL show the arrangement instead.

#### Scenario: Undo the import
- **WHEN** the user imports a file, opens it in the sample editor and chooses Undo
- **THEN** the sound is removed and the arrangement is shown
