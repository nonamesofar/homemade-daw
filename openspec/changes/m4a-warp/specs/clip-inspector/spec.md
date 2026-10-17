# Spec Delta

## MODIFIED Requirements

### Requirement: Features for later milestones are shown disabled
The inspector SHALL show the Texture warp mode from the mock as disabled until the milestone that adds it. Every other field of the inspector can be changed.

#### Scenario: Disabled controls
- **WHEN** a warped clip is selected
- **THEN** Texture is visible in MODE but cannot be chosen, and FADE IN and FADE OUT can be changed

## ADDED Requirements

### Requirement: Warp controls
The inspector SHALL show for the selected clip: WARP (on/off), MODE (Beats, Tones, Texture, Re-pitch), BPM (the clip's own tempo, two decimals, with halve and double buttons) and TRANSPOSE (semitones). MODE and BPM are disabled while WARP is off. Each change is one undo step and is heard at once, or as soon as the clip's stretched version is ready.

#### Scenario: Switch mode
- **WHEN** the user selects a Beats clip and chooses Tones in MODE
- **THEN** the clip plays in Tones mode and one Undo returns it to Beats

#### Scenario: Warp off greys the warp fields
- **WHEN** the user turns WARP off
- **THEN** MODE, BPM and TRANSPOSE are greyed out and keep their values

### Requirement: Clip length
The inspector SHALL show the clip's length in bars and beats ("4 bars", "1 bar 2 beats") and let the user type a new length. The clip keeps its start; it can only become longer than its sound when LOOP is on. A change follows the no-overlap rule and is one undo step.

#### Scenario: Shorten by typing
- **WHEN** the user types "2" in LENGTH for a 4-bar clip
- **THEN** the clip covers its first 2 bars and plays only the first half of the loop

#### Scenario: Too long without loop
- **WHEN** LOOP is off and the user types a length longer than the sound
- **THEN** the clip becomes as long as the sound

### Requirement: Clip loop
The inspector SHALL have a LOOP toggle. With LOOP on, the clip repeats the part of the sound it played when LOOP was turned on, for as long as the clip is. Turning LOOP off makes a clip that is longer than one pass as long as one pass. Each change is one undo step.

#### Scenario: Loop a bar over eight bars
- **WHEN** the user turns LOOP on for a 1-bar clip and types "8" in LENGTH
- **THEN** the clip covers 8 bars and the bar is heard eight times, with no gap at the joins

#### Scenario: Loop off shortens
- **WHEN** LOOP is turned off on that 8-bar clip
- **THEN** the clip covers 1 bar again

### Requirement: Clip fades
The inspector SHALL let the user set FADE IN and FADE OUT in milliseconds, from 0 up to the clip's length (fade in and fade out together cannot exceed it), by dragging or typing. Fades are linear, heard at once during playback and drawn on the clip's waveform in the inspector and the timeline. A drag is one undo step. A fade of 0 still keeps the short click-free fade every clip has.

#### Scenario: Fade in while playing
- **WHEN** the user sets FADE IN to 500 ms on a clip while the arrangement plays it
- **THEN** the clip's first half second rises from silence, and the fade is drawn as a slope over the waveform

#### Scenario: Fades cannot cross
- **WHEN** a clip lasts 1000 ms with FADE OUT 600 ms and the user types 800 ms in FADE IN
- **THEN** FADE IN becomes 400 ms
