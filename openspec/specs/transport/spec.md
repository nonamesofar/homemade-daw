# transport Specification

## Purpose

Defines how the arrangement is played: play, stop and locate, the position display, the loop range and loop toggle, the metronome, and the project tempo and time signature.

## Requirements

### Requirement: Play and stop
The header SHALL have Play and Stop buttons, and Space SHALL toggle between playing and stopped. Play starts from the playhead position. The Play button is highlighted only while the transport is actually playing.

#### Scenario: Space toggles
- **WHEN** the arrangement is stopped at bar 3 and the user presses Space, waits, and presses Space again
- **THEN** playback starts at bar 3 and stops where the playhead was when Space was pressed the second time

#### Scenario: Highlight follows the real state
- **WHEN** the user presses Play while no audio output is ready yet
- **THEN** the Play button is not highlighted until playback really starts

### Requirement: Second stop returns to the start
Pressing Stop while already stopped SHALL move the playhead to the loop start if looping is on, otherwise to the start of the song. Home and Enter SHALL do the same at any time.

#### Scenario: Double stop
- **WHEN** looping is off, playback stops at bar 9, and the user presses Stop again
- **THEN** the playhead is at bar 1

#### Scenario: Return to loop start
- **WHEN** looping is on with the loop at bars 5 to 8 and the user presses Home
- **THEN** the playhead is at bar 5

### Requirement: Play from the selection
Shift+Space SHALL start playback from the start of the current selection (the earliest selected clip, or the time selection), or from the playhead if nothing is selected.

#### Scenario: Selected clip
- **WHEN** a clip starting at bar 7 is selected, the playhead is at bar 1, and the user presses Shift+Space
- **THEN** playback starts at bar 7

### Requirement: Locate by clicking the ruler
Clicking the ruler SHALL move the playhead to the clicked position, snapped to the current grid (Alt bypasses snap), whether playing or stopped. During playback, playback continues from the new position.

#### Scenario: Click while playing
- **WHEN** the arrangement plays and the user clicks the ruler at bar 12
- **THEN** playback jumps to bar 12 and continues

### Requirement: Playhead
A playhead line SHALL be drawn over all lanes and the ruler at the current playback position, updated smoothly at the display rate while playing.

#### Scenario: Moves with playback
- **WHEN** the arrangement plays at 120 BPM in 4/4
- **THEN** the playhead advances one bar every 2 seconds and stays aligned with the audio heard

### Requirement: Position display
The header SHALL show the playhead position as bar . beat . sixteenth, counting from 1 (the start is "1 . 1 . 1"), updated while playing. Clicking it SHALL let the user type a position in the same form (missing parts count as 1); Enter moves the playhead there, Escape cancels, invalid text is refused.

#### Scenario: Type a position
- **WHEN** the user clicks the position display, types "5.2", and presses Enter
- **THEN** the playhead moves to bar 5, beat 2 and the display shows "5 . 2 . 1"

#### Scenario: Display while playing
- **WHEN** playback has run for 9 seconds from the start at 120 BPM in 4/4
- **THEN** the display shows "5 . 3 . 1"

### Requirement: Loop range
A loop bar above the ruler SHALL show the loop range, brighter when looping is on. The user SHALL be able to drag its start edge, end edge or body (the body moves the whole range), snapped to the grid with Alt bypassing snap. Dragging in an empty part of the loop bar draws a new range. Mod+L sets the loop to the selection. The range is at least one grid step long (one beat with snap off), is saved, and each change is one undo step.

#### Scenario: Drag the end edge
- **WHEN** the loop is bars 5 to 8 with 1-bar snap and the user drags its end edge to the right by about two bars
- **THEN** the loop is bars 5 to 10

#### Scenario: Loop to selection
- **WHEN** clips covering bars 9 to 12 are selected and the user presses Mod+L
- **THEN** the loop range becomes bars 9 to 12

### Requirement: Looping playback
The Loop button and the L key SHALL turn looping on and off. While looping is on and the playhead is inside the range, playback jumps from the loop end to the loop start with no gap or click. Turning looping on or off is not an undo step.

#### Scenario: Seamless wrap
- **WHEN** looping is on for bars 5 to 8 and playback reaches the end of bar 8
- **THEN** it continues at the start of bar 5 without a gap, and a clip that crosses the loop end is cut off at the loop end

#### Scenario: Playhead after the loop
- **WHEN** looping is turned on while the playhead is after the loop end
- **THEN** playback continues to the end of the song without jumping back

### Requirement: Metronome
The header SHALL have a Click button that turns a metronome on and off. It clicks on every beat of the time signature, with a higher accent on beat 1 of each bar, only while playing. The setting is saved with the project and is not an undo step.

#### Scenario: Accent
- **WHEN** the metronome is on in 3/4 and the arrangement plays
- **THEN** every third click, starting with the first beat of bar 1, is the accented one

### Requirement: Tempo
The header SHALL show the project tempo with two decimals and let the user change it from 20 to 999 BPM by dragging the field or typing a value; out-of-range values are clamped. A drag is one undo step. A new project is 120 BPM.

#### Scenario: Type a tempo
- **WHEN** the user types "90" in the tempo field and presses Enter
- **THEN** the field shows "90.00", the metronome and grid follow 90 BPM, and one undo step restores 120.00

### Requirement: Clips keep their bars when the tempo changes
When the tempo changes, every clip SHALL keep its start bar and beat. Clips still play at their own speed in this version, so a clip's length in bars changes with the tempo; if that makes a clip run into the next clip on its track, the earlier clip is shortened in the same undo step.

#### Scenario: Slower tempo
- **WHEN** a clip starts at bar 5 and the tempo changes from 120 to 90 BPM
- **THEN** the clip still starts at bar 5, and later than before in seconds

#### Scenario: Faster tempo causes overlap
- **WHEN** a one-bar sound sits at bar 1 with the next clip at bar 2, and the tempo goes from 120 to 140 BPM
- **THEN** the first clip is shortened to end at bar 2 and the clips do not overlap; Undo restores both the tempo and the clip length

### Requirement: Time signature
The header SHALL show the time signature and let the user set the numerator from 1 to 32 and the denominator to 2, 4, 8 or 16. The ruler, bar numbers, snap to bars, the position display and the metronome accent follow it. A change is one undo step; clips keep their position in beats. A new project is 4/4.

#### Scenario: Change to 3/4
- **WHEN** the user sets the time signature to 3/4
- **THEN** the ruler shows a bar line every 3 beats and the position display counts 3 beats per bar
