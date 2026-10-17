# Spec Delta

## Purpose

Defines how a clip in the arrangement follows the project tempo: warp on or off, the stretch modes, the clip's own tempo and transpose, the defaults for new clips, and what happens while a stretched version is being made.

## ADDED Requirements

### Requirement: Warped clips play in the project tempo
A clip with WARP on SHALL play its sound so that the sound's own tempo matches the project tempo: a clip of a sound at T BPM in a project at P BPM covers the same number of bars as the sound has at T BPM, and its hits land on the same beats of the bar. A clip with WARP off SHALL play at the sound's own speed, as before.

#### Scenario: A 90 BPM loop in a 120 BPM song
- **WHEN** a 4-bar loop at 90 BPM is placed at bar 1 with WARP on in a 120 BPM project
- **THEN** the clip covers bars 1 to 4 exactly, and its kick on beat 1 of each bar of the loop is heard on beat 1 of bars 1 to 4

#### Scenario: Warp off
- **WHEN** the same loop is placed with WARP off
- **THEN** the clip lasts as long as the sound (10.67 seconds), which is 5.33 bars at 120 BPM

### Requirement: Beats mode keeps the hits crisp
In Beats mode the clip's sound SHALL be cut at its hits and each piece SHALL be moved to its new position in time without being stretched inside, so every hit keeps its attack. When the tempo is faster than the sound's, each piece is cut short with a short fade-out before the next hit; when it is slower, the gap after a piece is faded to silence. A sound with no hits is cut on a 1/16-note grid.

#### Scenario: Attack is kept
- **WHEN** a drum loop at 90 BPM plays in Beats mode at 120 BPM and is rendered offline
- **THEN** each hit starts within 1 ms of its beat position and its first 5 ms match the original sound (no smearing)

#### Scenario: Sound without hits
- **WHEN** a sustained pad sound with no detected hits is set to Beats mode
- **THEN** it still plays in the project tempo, cut into 1/16-note pieces

### Requirement: Tones mode keeps pitch and smooth sound
In Tones mode the clip SHALL be stretched continuously to the project tempo with its pitch unchanged, suited to melodic and chordal material.

#### Scenario: Pitch is kept
- **WHEN** a sustained 440 Hz tone recorded at 100 BPM plays in Tones mode at 130 BPM
- **THEN** it is heard at 440 Hz (within 1 Hz) and fits the clip's length in bars

### Requirement: Re-pitch mode changes speed and pitch together
In Re-pitch mode the clip SHALL be played faster or slower like a turntable, so its pitch follows the speed: a sound at 100 BPM in a 200 BPM project plays twice as fast and one octave higher.

#### Scenario: Faster and higher
- **WHEN** a 440 Hz tone at 100 BPM plays in Re-pitch mode at 200 BPM
- **THEN** it is heard at 880 Hz (within 1 Hz) and lasts half as long as the sound

### Requirement: Texture mode is not available yet
The Texture mode SHALL be shown in the mode choice but cannot be chosen in this version.

#### Scenario: Texture disabled
- **WHEN** the user opens the mode choice of a warped clip
- **THEN** Texture is visible and greyed out, and Beats, Tones and Re-pitch can be chosen

### Requirement: The clip's own tempo
Each warped clip SHALL have its own tempo (the sound's tempo as this clip uses it), from 20 to 999 BPM. It starts as the sound's tempo (the user's correction if there is one, else the detected tempo) and can be typed, halved or doubled per clip. Changing it is one undo step and changes the clip's length in bars, keeping its start.

#### Scenario: Double a wrong tempo
- **WHEN** a loop was detected at 60 BPM, its clip covers 4 bars at 120 BPM, and the user presses the double button on the clip
- **THEN** the clip's tempo shows 120 BPM, the clip covers 8 bars (the same sound holds twice as many beats at twice the tempo), and Undo restores 60 BPM and 4 bars

#### Scenario: Other clips of the same sound are not changed
- **WHEN** two clips play the same sound and the user types 92 BPM on one of them
- **THEN** the other clip keeps its tempo

### Requirement: Default warp for new clips
A new clip SHALL get WARP on in Beats mode when its sound's tempo is known and the sound is not a one-shot; otherwise it SHALL get WARP off. This applies to clips placed from the browser or from Explorer, pasted copies keep the settings of the clip they copy, and takes keep WARP off.

#### Scenario: Loop with a known tempo
- **WHEN** the user drops an analysed 90 BPM loop from the browser onto a track in a 120 BPM project
- **THEN** the clip has WARP on in Beats mode with a tempo of 90 BPM

#### Scenario: One-shot
- **WHEN** the user drops a 0.5-second kick onto a track
- **THEN** the clip has WARP off

#### Scenario: Tempo not known yet
- **WHEN** a file is dropped from Explorer onto a track before its analysis has finished
- **THEN** the clip has WARP off, and the user can turn it on once the tempo is known

### Requirement: Warp needs a tempo
WARP SHALL NOT be turned on for a clip whose sound has no known tempo and no tempo typed on the clip; the control says why ("Tempo not known").

#### Scenario: Sound without a tempo
- **WHEN** a clip's sound has no detected tempo and the clip has none
- **THEN** its WARP toggle is disabled with the hint "Tempo not known"

### Requirement: Changing warp settings
Turning WARP on or off and choosing a mode SHALL each be one undo step that keeps the clip's start. Turning WARP on gives the clip its length in bars at the project tempo; turning it off gives it its length in seconds at the sound's own speed. The no-overlap rule applies when the clip gets longer.

#### Scenario: Turn warp off
- **WHEN** a warped 4-bar clip of a 90 BPM loop at 120 BPM has WARP turned off
- **THEN** it starts at the same bar, plays at its own speed and covers 5.33 bars; one Undo brings back the warped 4-bar clip

#### Scenario: Longer clip shortens the next one
- **WHEN** WARP is turned off on a clip that then runs into the next clip on its track
- **THEN** the next clip is shortened from its start, in the same undo step

### Requirement: Transpose
A clip SHALL have a transpose from -24 to +24 semitones, default 0, available in Beats and Tones mode, that changes the pitch without changing the clip's length or timing. In Re-pitch mode and with WARP off the control is disabled and the clip plays without transpose; its value is kept. A change is one undo step.

#### Scenario: Up a fifth
- **WHEN** the user sets transpose to +7 on a Tones clip of a 440 Hz tone
- **THEN** it is heard at about 659 Hz and covers the same bars as before

#### Scenario: Re-pitch ignores transpose
- **WHEN** a clip with transpose +7 is switched to Re-pitch mode
- **THEN** TRANSPOSE is greyed out showing +7, and the clip is heard without the +7

### Requirement: Stretched versions are made in the background
Beats and Tones clips SHALL be prepared in the background. While a clip's stretched version is not ready, the clip is drawn as "rendering" and is silent; it becomes audible on its own, without stopping playback, once ready. Sixteen 4-bar Beats clips SHALL all be ready within 0.5 s of a change on the target machine.

#### Scenario: Change tempo while playing
- **WHEN** the arrangement plays sixteen 4-bar Beats clips and the user types a new tempo
- **THEN** within 0.5 s all of them are heard at the new tempo, in time

#### Scenario: Undo is instant
- **WHEN** the user changes the tempo, waits for the clips to be ready, and presses Undo
- **THEN** the clips are heard at the old tempo without a new "rendering" phase

### Requirement: Warp does not change the sound's file
Warping, transposing and Beats renders SHALL NOT change the sound's files in the project; stretched versions are derived files that can be deleted and are made again.

#### Scenario: Files unchanged
- **WHEN** a sound is used by clips in all three modes and the project is saved
- **THEN** the sound's original and working files have the same content as before
