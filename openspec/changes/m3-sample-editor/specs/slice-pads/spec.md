# Spec Delta

## Purpose

Defines playing slices from the sample editor's 16 pads with the mouse and the computer keyboard: pad layout and keys, banks, how voices start, stop and overlap, the play modes, and the per-slice pitch, gain and reverse that pads play.

## ADDED Requirements

### Requirement: Pad grid
The sample editor SHALL show 16 pads in a 4x4 grid laid out like an MPC: pad 1 bottom left, pad 4 bottom right, pad 13 top left. Each pad shows its number and its key. Pad n plays slice n of the current bank; a pad without a slice is drawn empty and plays nothing.

#### Scenario: Fewer slices than pads
- **WHEN** the sound has 6 slices
- **THEN** pads 1 to 6 are active and pads 7 to 16 are drawn empty and silent

### Requirement: Pads play with the mouse
Pressing a pad with the mouse SHALL start its slice and select it; releasing the mouse SHALL count as letting go of the pad. The pad lights while its slice sounds.

#### Scenario: Click a pad
- **WHEN** the user clicks pad 3
- **THEN** slice 3 sounds, pad 3 lights until it ends, and slice 3 is selected in the strip and the waveform

### Requirement: Pads play from the keyboard
While the sample editor has focus and no text field is being typed in, the keys 1 2 3 4 / Q W E R / A S D F / Z X C V SHALL play pads 13 to 16 / 9 to 12 / 5 to 8 / 1 to 4 (Z is pad 1). Pressing a key starts the pad and releasing it lets go. Holding a key down does not retrigger it. Keys with Mod held keep their shortcuts (Mod+R records).

#### Scenario: Key down and up
- **WHEN** the editor has focus and the user presses and holds Z, then releases it
- **THEN** pad 1 starts on the press, does not retrigger while held, and is let go on the release

#### Scenario: Typing a tempo
- **WHEN** the user is typing in the editor's tempo field and presses Q
- **THEN** a "q" is typed and no pad plays

#### Scenario: Keys outside the editor
- **WHEN** the arrangement is shown and the user presses S with a track selected
- **THEN** the track is soloed and no pad plays

### Requirement: Pads stop when focus leaves
When the sample editor loses keyboard focus or is closed, every pad SHALL be let go, so no slice keeps sounding because its key-up was missed.

#### Scenario: Switch window while holding
- **WHEN** a Gate slice is held with its key and the user switches to another application
- **THEN** the slice stops with its release fade

### Requirement: Pad banks
When a sound has more than 16 slices, bank buttons A, B, C and so on SHALL appear above the pads, one per 16 slices; bank B's pad 1 plays slice 17. Changing the bank is not an undo step.

#### Scenario: Bank B
- **WHEN** a sound has 40 slices and the user chooses bank C and presses Z
- **THEN** slice 33 plays

### Requirement: Pads play together
Pads SHALL play at once and over the running song, with up to 16 slices sounding together. A 17th SHALL replace the one that started first, with a short fade. Hitting a pad that is still sounding SHALL restart it from the beginning, with a short fade on the old sound.

#### Scenario: Finger drumming
- **WHEN** the song plays and the user presses Z, X and C together
- **THEN** slices 1, 2 and 3 sound together over the song

#### Scenario: Retrigger
- **WHEN** the user hits pad 1 twice in quick succession
- **THEN** slice 1 is heard restarting, without a click

### Requirement: Play modes
Each slice SHALL have a play mode, chosen in the slice panel: One-shot (default) plays to the end of the slice whatever happens to the pad; Gate plays while the pad is held and fades out over 10 ms when it is let go; Loop repeats the slice while held, without a click at the loop point, and fades out when let go.

#### Scenario: Gate
- **WHEN** slice 2 is set to Gate and the user taps its pad briefly
- **THEN** only the start of slice 2 is heard

#### Scenario: Loop
- **WHEN** slice 5 is set to Loop and the user holds its pad for three times the slice's length
- **THEN** slice 5 is heard three times in a row and stops when the pad is released

### Requirement: Slice pitch, gain and reverse
The slice panel SHALL let the user set the selected slice's pitch (-24 to +24 semitones, keeping its length), its gain (silence to +24 dB) and Reverse. Each change is one undo step (repeated changes to one value within half a second join one step) and is saved. Pads play the slice with these settings.

#### Scenario: Pitch up
- **WHEN** the user sets slice 3's pitch to +12 st and hits pad 3
- **THEN** slice 3 sounds an octave higher and lasts as long as before

#### Scenario: Reverse a slice
- **WHEN** the user turns Reverse on for slice 1 and hits pad 1
- **THEN** slice 1 plays backwards

### Requirement: Changes are heard quickly
A change to a slice's pitch, gain, reverse or slice points SHALL be heard on the next pad hit once ready, within 100 ms for slices up to 2 seconds. Until then the pad plays the slice as it was. Changing these settings while a slice sounds does not cut it off.

#### Scenario: Pitch change while playing
- **WHEN** a Loop slice is held and the user changes its pitch
- **THEN** the held sound continues unchanged, and the next hit plays the new pitch

### Requirement: Pads play at the right speed
Pads SHALL play slices at their original speed and at the pitch set for them, whatever the sound's sample rate and the output's sample rate.

#### Scenario: 48 kHz sound on a 44.1 kHz output
- **WHEN** a 48 kHz sound's slice with pitch 0 is played from a pad on a 44.1 kHz output
- **THEN** it is heard at its original pitch and length

### Requirement: Playing pads does not change the project
Playing pads, choosing a bank and the pads' lit state SHALL NOT change the project, mark it as unsaved or create an undo step. Pads are not affected by track mute or solo in the arrangement.

#### Scenario: Saved state kept
- **WHEN** the user plays pads in a saved project
- **THEN** the window title shows no unsaved marker
