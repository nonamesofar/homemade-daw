# Spec Delta

## ADDED Requirements

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
