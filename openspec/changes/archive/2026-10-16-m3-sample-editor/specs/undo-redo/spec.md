# Spec Delta

## ADDED Requirements

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
