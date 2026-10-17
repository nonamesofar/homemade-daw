# sample-editor Specification

## Purpose
Defines the sample editor: the full view where one sound is cut into slices, with its header, waveform, the three ways to chop, slice points, the slice strip, slice start and end, audition, and sending the slices to a new track.

## Requirements

### Requirement: Open a sound in the sample editor
The sample editor SHALL open one sound at a time in the main view area, replacing the arrangement until the user goes back. Opening another sound replaces the one shown. The browser and the header stay visible, and the transport keeps playing.

#### Scenario: Open from the browser
- **WHEN** the user double-clicks "Break 90" in the browser
- **THEN** the main view shows the sample editor for "Break 90"

#### Scenario: Song keeps playing
- **WHEN** the arrangement is playing and the user opens a sound in the sample editor
- **THEN** playback continues without a gap

### Requirement: Go back to the arrangement
The editor header SHALL have a "< Arrange" button that shows the arrangement again with its zoom, scroll and selection as they were.

#### Scenario: Back keeps the view
- **WHEN** the user zoomed into bar 9 and selected two clips, opened a sound in the editor and pressed "< Arrange"
- **THEN** the arrangement shows bar 9 at the same zoom with the same two clips selected

### Requirement: Sound header
The editor header SHALL show the sound's name and "<bars> bars · <tempo> BPM · <rate> kHz · mono|stereo", with bars counted in 4/4 at the sound's tempo and rounded to one decimal place. Until analysis has finished it SHALL show "Analysing..." in place of the bars and tempo.

#### Scenario: Header of a loop
- **WHEN** a 4-bar stereo loop at 90 BPM and 44.1 kHz is opened
- **THEN** the header shows "Break 90" and "4 bars · 90 BPM · 44.1 kHz · stereo"

### Requirement: Waveform with zoom and scroll
The editor SHALL show the sound's waveform with a bar.beat ruler from the first beat. Mod+wheel zooms around the mouse, from the whole sound down to single samples drawn as a line; the wheel and Shift+wheel scroll; a "Fit" button shows the whole sound again.

#### Scenario: Zoom to samples
- **WHEN** the user zooms in on a slice point as far as possible
- **THEN** single samples are visible as a line and the slice point lies on one sample

#### Scenario: Smooth zoom
- **WHEN** the user zooms and scrolls a 5-minute sound with 256 slices
- **THEN** the waveform follows without visible stutter

### Requirement: Slices cover the whole sound
A sound's slices SHALL cover it from its first sample to its last without gaps: the first slice starts at the start of the sound, each slice point starts the next slice, and the last slice ends at the end. A sound without slice points is one slice. There SHALL be at most 256 slices and each SHALL be at least 5 ms long.

#### Scenario: New sound
- **WHEN** a sound that was never chopped is opened
- **THEN** it shows one slice covering the whole sound and the counter says "1 slice"

### Requirement: Choose how to chop
The toolbar SHALL offer Beat, Transient and Manual. Choosing Beat or Transient replaces the slice points with those that way produces, as one undo step. Choosing Manual keeps the slice points. The Divisions control is enabled only in Beat mode and the Sensitivity slider only in Transient mode.

#### Scenario: Switch to Beat
- **WHEN** a 4-bar loop is in Manual mode with 3 slice points and the user chooses Beat with division 1/4
- **THEN** it has 16 slices one beat long each, and one Undo returns the 3 slice points and Manual mode

#### Scenario: Disabled control
- **WHEN** the mode is Manual
- **THEN** both Divisions and Sensitivity are visible but cannot be changed

### Requirement: Beat slicing
In Beat mode the slice points SHALL lie on the chosen division (1/4, 1/8, 1/16 or 1/32 note) at the sound's tempo, counted from the first beat in both directions. Changing the division re-slices at once as one undo step.

#### Scenario: Sixteenths
- **WHEN** a 2-bar loop at 120 BPM is sliced in Beat mode at 1/16
- **THEN** it has 32 slices of 125 ms each

#### Scenario: Pickup before the first beat
- **WHEN** a loop's first beat is 0.25 beats after its start and it is sliced at 1/4
- **THEN** the first slice is the quarter beat before the first beat and the second starts on the first beat

### Requirement: Transient slicing
In Transient mode the slice points SHALL lie on the hits whose strength passes the Sensitivity setting (higher sensitivity, more slices). While the slider moves, the waveform shows the slice points it would make and the counter shows their number without changing the project; "Auto-slice" applies them as one undo step.

#### Scenario: Preview then apply
- **WHEN** the user drags Sensitivity from 30 to 80 on a drum break and releases
- **THEN** preview lines and the counter follow the slider, the project is unchanged, and pressing "Auto-slice" makes those slices as one undo step

#### Scenario: Too many hits
- **WHEN** Transient mode at sensitivity 100 would give more than 256 slices
- **THEN** the 255 strongest hits become slice points and a message says the limit was reached

### Requirement: Edit slice points by hand
The user SHALL be able to add a slice point by double-clicking the waveform, move one by dragging it, and remove one by double-clicking it or selecting it and pressing Delete. Each is one undo step. Editing a slice point while in Beat or Transient mode switches the mode to Manual in the same step.

#### Scenario: Add and remove
- **WHEN** the user double-clicks the middle of slice 3, then double-clicks the new point
- **THEN** a point is added (slice 3 splits in two) and then removed, and two Undos are available

#### Scenario: Drag in Beat mode
- **WHEN** the sound is in Beat mode and the user drags a slice point
- **THEN** the point moves, the mode shows Manual, and one Undo restores both the point and Beat mode

### Requirement: Slice points snap to zero crossings
A slice point that is added or moved SHALL land on the nearest zero crossing within 1 ms, unless Alt is held. It SHALL keep at least 5 ms from its neighbours and the sound's ends.

#### Scenario: Alt drag
- **WHEN** the user drags a slice point with Alt held
- **THEN** it lands exactly under the mouse

#### Scenario: Too close
- **WHEN** the user drags a slice point onto its neighbour
- **THEN** it stops 5 ms before the neighbour

### Requirement: Slice strip and selection
A numbered strip above the waveform SHALL show one cell per slice, aligned with the slices and scrolling and zooming with the waveform. Clicking a cell, a slice in the waveform or a pad selects that slice; the selected slice is highlighted in the strip and shaded in the waveform. Selecting is not an undo step.

#### Scenario: Select from the strip
- **WHEN** the user clicks cell 3 of the strip
- **THEN** slice 3 is shaded in the waveform and pad 3 is highlighted

### Requirement: Slice start and end
The slice panel SHALL show the selected slice's START, END and LENGTH, in bar.beat.sixteenth of the sound (from its first beat) or, after a toggle, in seconds or samples. Typing a new START or END moves the matching slice point, as one undo step. Values outside the neighbours' limits are refused.

#### Scenario: Type an end
- **WHEN** slice 2 of a 90 BPM loop shows START 1.2.1 and END 1.3.1 and the user types END 1.3.3
- **THEN** slice 2 ends and slice 3 starts at 1.3.3

#### Scenario: Show samples
- **WHEN** the user switches the display to samples
- **THEN** START and END show sample numbers of the sound

### Requirement: Audition the sound
The Audition button SHALL play the whole sound from its start at its original speed and pitch, with a moving line in the waveform; pressing it again stops it. It follows the one-sound-at-a-time rule of the browser's audition and does not change the project.

#### Scenario: Audition and browser
- **WHEN** the editor's Audition is playing and the user clicks another sound in the browser
- **THEN** the editor's audition stops and the browser's sound plays

### Requirement: Slices to new track
"Slices to new track" SHALL add one track named after the sound, at the end of the track list, with one clip per slice placed back to back from the playhead, as one undo step. Each clip plays its slice's part of the sound with the slice's gain and direction, and is named "<sound> <n>". The arrangement is then shown with the new clips selected.

#### Scenario: Chop to track
- **WHEN** a break with 16 slices is open, the playhead is at bar 5, and the user presses "Slices to new track"
- **THEN** a track "Break 90" holds 16 clips from bar 5 in slice order, played back they sound like the break, and one Undo removes the track and its clips

#### Scenario: Reversed slice
- **WHEN** slice 4 is reversed and the slices are sent to a new track
- **THEN** clip 4 plays backwards and the other clips forwards

### Requirement: Slice clips are copies
Clips made from slices SHALL NOT change when the slices are edited later, and SHALL remember which slice of which sound they came from. A slice's pitch is stored on its clip and is not heard until clips can be pitched.

#### Scenario: Edit after sending
- **WHEN** the slices were sent to a track and the user then moves slice point 2 in the editor
- **THEN** the clips on the track are unchanged

### Requirement: Slices are saved
A sound's slices (mode, division, sensitivity, slice points and each slice's settings) SHALL be saved with the project and restored on reopen. The sound's audio files SHALL NOT change.

#### Scenario: Round trip
- **WHEN** the user chops a loop into 12 slices with different settings, saves, quits and reopens the project
- **THEN** the loop opens with the same 12 slices and settings, and its files have the same content as before
