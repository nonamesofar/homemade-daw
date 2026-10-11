# arrangement-timeline Specification

## Purpose

Defines the multi-track timeline view where audio clips are shown with waveforms and arranged by dragging, proving the custom UI is responsive at realistic size.

## Requirements

### Requirement: Timeline shows tracks and clips
The timeline SHALL show the open project's tracks as horizontal lanes (112 pixels tall) with grid lines that follow the zoom and the snap value, under a bar ruler whose labels adapt to the zoom (bars, then beats). Each clip SHALL be drawn as a rectangle in its track's colour with its name and its waveform, and selected clips with a light ring. Many clips can use the same sound without copying its audio.

#### Scenario: Initial view
- **WHEN** a project with 4 tracks and 14 clips is opened
- **THEN** 4 lanes appear under the ruler and every clip shows its name and waveform in its track's colour

#### Scenario: Same sound reused
- **WHEN** the user places the same sound four times
- **THEN** four clips show the same waveform and the project's audio folder still holds one copy of the sound

### Requirement: Scroll and zoom stay responsive
The timeline SHALL zoom with Mod+wheel around the mouse position, scroll vertically with the wheel and horizontally with Shift+wheel, and have horizontal and vertical scroll bars. Z SHALL zoom to the selection and Shift+Z to the whole song. The scrollable length SHALL grow with the song (at least 16 bars, and 8 bars past the last clip). With 200 clips visible the view SHALL keep redrawing at 60 frames per second while playing and scrolling.

#### Scenario: Zoom while scrolling
- **WHEN** the user zooms and scrolls continuously for 10 seconds with 200 clips visible while the song plays
- **THEN** each redraw takes under 8 ms and waveforms remain visible

#### Scenario: Zoom to the whole song
- **WHEN** the song ends at bar 40 and the user presses Shift+Z
- **THEN** bars 1 to 40 fill the visible width

#### Scenario: Scroll bar
- **WHEN** the user drags the horizontal scroll bar to its end
- **THEN** the view shows the last bars of the song with empty room after it

### Requirement: Drag clips
The user SHALL be able to drag the selected clips to a new position, also onto other tracks; the clips keep their positions relative to each other, and the grabbed clip's start snaps to the grid (Alt bypasses snap). While dragging, a preview shows where the clips will land; the project changes only when the mouse is released, as one undo step. Clips cannot move before bar 1 or below the last track.

#### Scenario: Move to another track
- **WHEN** the user drags a clip from track 1 to track 3 and releases
- **THEN** the clip appears on track 3 at the dropped, snapped position and is gone from track 1

#### Scenario: Move several clips
- **WHEN** two clips on tracks 1 and 2 are selected and the user drags one of them down one track and right one bar
- **THEN** both move down one track and right one bar

#### Scenario: Escape cancels
- **WHEN** the user presses Escape during a drag
- **THEN** the preview disappears and nothing changes

### Requirement: Place a sound from the browser
The user SHALL be able to drag a sound from the browser onto a lane to create a clip there, starting at the snapped drop position and as long as the sound. While hovering, a ghost clip shows where it will land. Dropping below the last lane creates a new track with the clip. Each drop is one undo step.

#### Scenario: Drop on a lane
- **WHEN** the user drags "drums-loop-120" from the browser onto track 2 at about bar 5 with 1-bar snap
- **THEN** a clip "drums-loop-120" starts at bar 5 on track 2 and plays the whole sound

#### Scenario: Drop below the last lane
- **WHEN** the user drops a sound under the last track
- **THEN** a new track is created holding the clip, and one Undo removes both

### Requirement: Place a file from Explorer
Dropping audio files from Explorer onto a lane SHALL import them and place them as clips on that track, one after another from the drop position; dropping below the lanes places them on a new track. While a file imports, a grey placeholder shows where its clip will appear. Import and placement are one undo step. Files dropped on the browser are only imported, as before.

#### Scenario: Two files on a lane
- **WHEN** the user drops two audio files from Explorer onto track 1 at bar 1
- **THEN** both are imported, the first clip starts at bar 1 and the second starts where the first ends, and one Undo removes the clips and the sounds

### Requirement: Select clips
Clicking a clip SHALL select only it; Shift+click SHALL add clips to the selection, and Mod+click SHALL add or remove one clip. Dragging on an empty part of the lanes SHALL draw a box that selects the clips it touches (Shift keeps the earlier selection). Clicking empty lane space SHALL clear the selection. Selecting is not an undo step.

#### Scenario: Box select
- **WHEN** the user drags a box from an empty spot on track 1 across bars 3 to 6 down to track 2
- **THEN** exactly the clips on tracks 1 and 2 that touch bars 3 to 6 are selected

#### Scenario: Toggle one
- **WHEN** three clips are selected and the user Mod+clicks one of them
- **THEN** the other two stay selected and that one is not

### Requirement: Clips never overlap on a track
After any edit, two clips on the same track SHALL NOT overlap. A clip that is moved, pasted, duplicated or placed onto existing clips wins: a clip it partly covers is shortened, a clip it fully covers is removed, and a clip it lands inside is split into two parts around it. This happens in the same undo step as the edit.

#### Scenario: Shorten the clip underneath
- **WHEN** clip A covers bars 1 to 4 and the user moves clip B so that it starts at bar 3 on the same track
- **THEN** A covers bars 1 to 2 (ending where B starts) and B is unchanged; one Undo restores A's length and B's old position

#### Scenario: Land inside
- **WHEN** a one-bar clip is dropped at bar 3 inside a clip covering bars 1 to 8
- **THEN** the long clip becomes two clips, bars 1 to 2 and bars 4 to 8, which still play the right parts of the sound

#### Scenario: Cover completely
- **WHEN** a four-bar clip is moved over a one-bar clip on the same track
- **THEN** the one-bar clip is removed

### Requirement: Trim clip edges
Dragging the left or right edge of a clip SHALL shorten or lengthen it from that side, snapped (Alt bypasses snap), without moving the audio in time: trimming the left edge removes the start of the sound. A clip cannot be made longer than its sound allows or shorter than one grid step (5 milliseconds with snap off). Trimming follows the no-overlap rule and is one undo step. The sound's file is not changed.

#### Scenario: Trim the start
- **WHEN** a clip of a 4-bar loop starts at bar 1 and the user drags its left edge to bar 2
- **THEN** the clip starts at bar 2 and plays from the second bar of the loop, at the same time as before

#### Scenario: Cannot extend past the sound
- **WHEN** the user drags the right edge of an untrimmed clip further right
- **THEN** the clip stays as long as the sound

### Requirement: Split clips
Mod+E SHALL split the selected clips that cross the playhead into two clips at the playhead; with nothing selected it splits every clip that crosses the playhead. Both parts use the same sound and together play exactly what the clip played. The split is one undo step.

#### Scenario: Split at the playhead
- **WHEN** a clip covers bars 1 to 4, it is selected, the playhead is at bar 3 and the user presses Mod+E
- **THEN** there are two clips, bars 1 to 2 and bars 3 to 4, and playback sounds the same as before

### Requirement: Duplicate clips
Mod+D SHALL copy the selected clips and place the copies right after the end of the selection on the same tracks, keeping their spacing; the copies become the selection. Duplicating follows the no-overlap rule and is one undo step.

#### Scenario: Duplicate a bar
- **WHEN** a clip covering bar 1 is selected and the user presses Mod+D twice
- **THEN** the track has clips at bars 1, 2 and 3

### Requirement: Delete clips
Delete and Backspace SHALL remove the selected clips as one undo step. The sounds stay in the project and the browser.

#### Scenario: Delete
- **WHEN** two clips are selected and the user presses Delete
- **THEN** both clips are gone, the sounds are still in the browser, and one Undo brings both back

### Requirement: Copy, cut and paste clips
Mod+C SHALL copy the selected clips, Mod+X SHALL copy and delete them, and Mod+V SHALL paste them at the playhead, with the earliest clip on the selected track and the others keeping their spacing in time and tracks. Clips that would land below the last track go on the last track. Pasting follows the no-overlap rule, selects the pasted clips, and is one undo step. Copying is not an undo step.

#### Scenario: Paste at the playhead
- **WHEN** the user copies a clip from bar 1 of track 1, selects track 3, moves the playhead to bar 9 and presses Mod+V
- **THEN** a copy starts at bar 9 on track 3 and the original is unchanged

### Requirement: Snap
A snap menu in the header SHALL offer Off, 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32 and the triplet values 1/4T, 1/8T and 1/16T, and show the current value. Snap applies to moving, trimming, dropping, the loop range and clicks on the ruler; holding Alt during a gesture ignores it. The value is saved with the project and is not an undo step. A new project snaps to 1 bar.

#### Scenario: Alt bypass
- **WHEN** snap is 1 bar and the user drags a clip with Alt held and releases between bar lines
- **THEN** the clip starts exactly where it was released

#### Scenario: Triplet grid
- **WHEN** snap is 1/8T in 4/4 and a clip is moved
- **THEN** its start lands on a multiple of one third of a beat

### Requirement: Shortcuts act only on the timeline
The editing shortcuts (Delete, Mod+C/X/V/D/E, M, S, Z, Shift+Z, L) SHALL act on the timeline only while no text field has focus and no dialog is open.

#### Scenario: Typing in the browser search
- **WHEN** the browser search box has focus and the user types "z" and presses Delete
- **THEN** the search text changes and the timeline zoom and clips do not
