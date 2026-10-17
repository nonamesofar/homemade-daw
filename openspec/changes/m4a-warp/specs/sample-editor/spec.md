# Spec Delta

## MODIFIED Requirements

### Requirement: Slices to new track
"Slices to new track" SHALL add one track named after the sound, at the end of the track list, with one clip per slice placed back to back from the playhead, as one undo step. Each clip plays its slice's part of the sound with the slice's gain, direction and pitch, and is named "<sound> <n>". When the sound's tempo is known, the clips are warped in Beats mode with that tempo, so they line up with the project's bars; otherwise they have WARP off. The arrangement is then shown with the new clips selected.

#### Scenario: Chop to track
- **WHEN** a break with 16 slices is open, the playhead is at bar 5, and the user presses "Slices to new track"
- **THEN** a track "Break 90" holds 16 clips from bar 5 in slice order, played back they sound like the break, and one Undo removes the track and its clips

#### Scenario: Reversed slice
- **WHEN** slice 4 is reversed and the slices are sent to a new track
- **THEN** clip 4 plays backwards and the other clips forwards

#### Scenario: Slices fit another tempo
- **WHEN** a 90 BPM break chopped into 16 quarter-note slices is sent to a track in a 120 BPM project at bar 1
- **THEN** the 16 clips cover bars 1 to 4 exactly, each starting on a beat

#### Scenario: Pitched slice
- **WHEN** slice 3 has a pitch of +5 and the slices are sent to a track
- **THEN** clip 3 has a transpose of +5 and is heard 5 semitones higher, with the same length as the other quarter-note slices

### Requirement: Slice clips are copies
Clips made from slices SHALL NOT change when the slices are edited later, and SHALL remember which slice of which sound they came from. A slice's pitch becomes its clip's transpose.

#### Scenario: Edit after sending
- **WHEN** the slices were sent to a track and the user then moves slice point 2 in the editor
- **THEN** the clips on the track are unchanged
