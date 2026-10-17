# Spec Delta

## MODIFIED Requirements

### Requirement: Clips keep their bars when the tempo changes
When the tempo changes, every clip SHALL keep its start bar and beat. A warped clip SHALL also keep its length in bars and play faster or slower to fill it. A clip with WARP off still plays at its own speed, so its length in bars changes with the tempo; if that makes it run into the next clip on its track, the earlier clip is shortened in the same undo step.

#### Scenario: Slower tempo
- **WHEN** a clip starts at bar 5 and the tempo changes from 120 to 90 BPM
- **THEN** the clip still starts at bar 5, and later than before in seconds

#### Scenario: Faster tempo causes overlap
- **WHEN** a one-bar sound with WARP off sits at bar 1 with the next clip at bar 2, and the tempo goes from 120 to 140 BPM
- **THEN** the first clip is shortened to end at bar 2 and the clips do not overlap; Undo restores both the tempo and the clip length

#### Scenario: Warped clip keeps its bars
- **WHEN** a warped clip covers bars 1 to 4 and the tempo changes from 120 to 100 BPM
- **THEN** the clip still covers bars 1 to 4, now lasting longer in seconds, and its hits stay on the same beats
