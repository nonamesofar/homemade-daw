# track-mixer Specification

## Purpose

Defines the tracks of the arrangement and how they are mixed: adding, deleting, ordering, naming and colouring tracks, their volume, pan, mute and solo, level meters, and the master output.

## Requirements

### Requirement: Add an audio track
The user SHALL be able to add an empty audio track with the "+ Audio track" button below the last lane, Track > New Audio Track or Mod+T. A new track is added after the selected track (at the end if none is selected), is named "Audio N" with the next unused number, and gets the next colour of the 12-colour palette.

#### Scenario: Add with the shortcut
- **WHEN** a project has tracks "Audio 1" and "Audio 2", "Audio 1" is selected, and the user presses Mod+T
- **THEN** a track "Audio 3" appears between them, is selected, and has a different colour from "Audio 2"

#### Scenario: New project
- **WHEN** the user creates a new project
- **THEN** it has one empty audio track "Audio 1"

### Requirement: Delete tracks
The user SHALL be able to delete the selected track, with its clips, from the track header's context menu, Track > Delete Track, or Delete when a track header is selected and no clip is selected. The deletion is one undo step.

#### Scenario: Delete and undo
- **WHEN** the user deletes a track holding three clips and then chooses Undo
- **THEN** the track is back in the same place with its name, colour, mixer settings and all three clips

### Requirement: Reorder tracks by dragging the header
The user SHALL be able to drag a track header up or down to change the track order. The drag starts after the mouse moves 4 pixels, an insertion line shows where the track will land, and the list scrolls when the mouse nears its top or bottom edge. The track keeps its clips and mixer settings.

#### Scenario: Move a track to the top
- **WHEN** the user drags the header of the third track above the first track and releases
- **THEN** that track is first, keeps its clips, and one undo step puts it back third

#### Scenario: Small movement is a click
- **WHEN** the user presses on a header and moves less than 4 pixels before releasing
- **THEN** the track is selected and the order is unchanged

### Requirement: Rename a track
The user SHALL be able to rename a track by double-clicking its name in the header and typing; Enter or clicking elsewhere confirms, Escape cancels. An empty name is not accepted and keeps the old name.

#### Scenario: Rename
- **WHEN** the user double-clicks "Audio 1", types "Drums" and presses Enter
- **THEN** the header shows "Drums" and the Edit menu offers "Undo Rename track"

#### Scenario: Shortcuts do not fire while typing
- **WHEN** the user is typing a track name that contains "m" and "s"
- **THEN** the letters go into the name and no track is muted or soloed

### Requirement: Track colour
The user SHALL be able to change a track's colour from the header's context menu, choosing from the 12-colour palette. The track's clips are drawn in its colour.

#### Scenario: Recolour
- **WHEN** the user picks the teal palette colour for "Bass"
- **THEN** the header stripe and every clip on "Bass" are drawn teal

### Requirement: Track volume
Each track header SHALL show its volume as a dB readout that the user can drag up or down to change, from silence (shown as "-inf") to +6 dB, with a detent at 0 dB. Double-clicking the readout sets it back to 0 dB. A new track starts at 0 dB.

#### Scenario: Drag the readout
- **WHEN** the user drags the volume readout of a track down while it plays
- **THEN** the track gets quieter while dragging, the readout shows the new value, and the whole drag is one undo step

#### Scenario: Limits
- **WHEN** the user keeps dragging up past +6 dB
- **THEN** the volume stays at +6 dB

### Requirement: Track pan
Each track header SHALL have a pan control from full left to full right with a detent at the centre; double-clicking it centres it. A new track is centred.

#### Scenario: Pan left
- **WHEN** the user turns a track's pan fully left and plays a stereo clip on it
- **THEN** the clip is heard only on the left channel

### Requirement: Mute
Each track header SHALL have a mute button; a muted track is not heard. The M key mutes or unmutes the selected track.

#### Scenario: Mute while playing
- **WHEN** the user clicks M on the drum track while the arrangement plays
- **THEN** the drums stop being heard at once and the M button shows as on

### Requirement: Solo
Each track header SHALL have a solo button. While any track is soloed, only soloed tracks are heard. Clicking solo adds or removes that track; Mod+click solos only that track and un-solos the others. The S key solos or un-solos the selected track. Mute wins over solo on the same track.

#### Scenario: Exclusive solo
- **WHEN** tracks 1 and 2 are soloed and the user Mod+clicks solo on track 3
- **THEN** only track 3 is soloed and only track 3 is heard

#### Scenario: Unsolo all
- **WHEN** the only soloed track is un-soloed
- **THEN** every unmuted track is heard again

### Requirement: Mixer changes are saved, and repeated changes join one undo step
Track volume, pan, mute and solo SHALL be saved with the project. Changes to the same control of the same track made within half a second of each other SHALL form one undo step.

#### Scenario: Several quick clicks
- **WHEN** the user clicks a track's mute button three times within half a second and then chooses Undo once
- **THEN** the mute state is what it was before the first click

### Requirement: Track level meters
Each track header SHALL show a level meter of the track's output after volume and pan: the peak level, falling back at 20 dB per second with the highest recent peak held for 1.5 seconds, and the average (RMS over 300 ms) as a darker inner bar.

#### Scenario: Meter follows the sound
- **WHEN** a loud drum clip plays on a track
- **THEN** that track's meter moves with the hits and a silent track's meter stays empty

#### Scenario: Meter after volume
- **WHEN** the user lowers the track volume by 12 dB while it plays
- **THEN** the meter reading drops by about 12 dB

### Requirement: Master volume and meter
The header bar SHALL show the master output's volume (same range and controls as a track volume) and a stereo meter. An over light SHALL turn on when the output goes above 0 dBFS and stay on until the user clicks it. The output is not limited: loud mixes clip, as in other DAWs.

#### Scenario: Over light latches
- **WHEN** the mix goes above 0 dBFS once and then becomes quiet
- **THEN** the over light stays on until the user clicks it

### Requirement: Record arm is not available yet
The track header SHALL show the R (record arm) button from the mock as disabled, with a tooltip that recording from inputs comes in a later version.

#### Scenario: Disabled
- **WHEN** the user clicks R on a track header
- **THEN** nothing happens and the tooltip explains it is not available yet
