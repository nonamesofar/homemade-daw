# Spec Delta

## MODIFIED Requirements

### Requirement: Trim clip edges
Dragging the left or right edge of a clip SHALL shorten or lengthen it from that side, snapped (Alt bypasses snap), without moving the audio in time: trimming the left edge removes the start of the sound. A clip cannot be made longer than its sound allows or shorter than one grid step (5 milliseconds with snap off), except that the right edge of a clip with LOOP on can be dragged past the end of its sound, and the clip then repeats its loop. Trimming follows the no-overlap rule and is one undo step. The sound's file is not changed. Trimming a warped clip keeps the audio on the same beats.

#### Scenario: Trim the start
- **WHEN** a clip of a 4-bar loop starts at bar 1 and the user drags its left edge to bar 2
- **THEN** the clip starts at bar 2 and plays from the second bar of the loop, at the same time as before

#### Scenario: Cannot extend past the sound
- **WHEN** the user drags the right edge of an untrimmed clip with LOOP off further right
- **THEN** the clip stays as long as the sound

#### Scenario: Extend a looped clip
- **WHEN** a 1-bar clip at bar 1 has LOOP on and the user drags its right edge to the end of bar 4
- **THEN** the clip covers bars 1 to 4 and plays its bar four times

#### Scenario: Trim a warped clip
- **WHEN** a warped 4-bar loop at 90 BPM sits at bar 1 of a 120 BPM project and the user drags its left edge to bar 2
- **THEN** the clip starts at bar 2 with the loop's second bar, and every hit is on the same beat as before

### Requirement: Split clips
Mod+E SHALL split the selected clips that cross the playhead into two clips at the playhead; with nothing selected it splits every clip that crosses the playhead. Both parts use the same sound and keep the clip's warp, loop and gain settings, and together play exactly what the clip played. The fade-in stays on the first part and the fade-out on the second. The split is one undo step.

#### Scenario: Split at the playhead
- **WHEN** a clip covers bars 1 to 4, it is selected, the playhead is at bar 3 and the user presses Mod+E
- **THEN** there are two clips, bars 1 to 2 and bars 3 to 4, and playback sounds the same as before

#### Scenario: Split a looped clip
- **WHEN** a 1-bar loop extended to 4 bars is split at bar 3
- **THEN** both parts still repeat the same bar, and playback sounds the same as before

## ADDED Requirements

### Requirement: Fade handles
Each clip SHALL show a small handle at its top-left and top-right corner when the mouse is over the clip. Dragging a handle sideways sets the fade-in or fade-out length (not snapped), up to the clip's length, as one undo step. The fade is drawn as a curve over the clip and the change is heard at once.

#### Scenario: Drag a fade-out
- **WHEN** the user drags the top-right handle of a 2-bar clip one bar to the left
- **THEN** the clip has a one-bar fade-out drawn over its second bar, and Undo removes it

#### Scenario: Handles do not move the clip
- **WHEN** the user drags a fade handle
- **THEN** the clip's start, end and track do not change

### Requirement: Warped and looped clips are drawn as they sound
The timeline SHALL draw a warped clip's waveform stretched to the clip's length in bars, a looped clip's waveform repeated with a thin mark at each repeat, a clip waiting for its stretched version with a "rendering" label, and each clip's fades as curves over the waveform.

#### Scenario: Warped waveform
- **WHEN** a 4-bar loop at 90 BPM is warped into a 120 BPM project
- **THEN** its waveform fills bars 1 to 4 and the kicks in the drawing line up with the bar lines

#### Scenario: Loop marks
- **WHEN** a 1-bar loop is extended to 4 bars
- **THEN** the clip shows the same waveform four times with a mark at bars 2, 3 and 4
