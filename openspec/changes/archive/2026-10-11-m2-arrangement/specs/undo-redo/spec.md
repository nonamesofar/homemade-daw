# Spec Delta

## ADDED Requirements

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
