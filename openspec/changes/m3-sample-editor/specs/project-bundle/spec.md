# Spec Delta

## ADDED Requirements

### Requirement: Slices and tempo corrections are part of the project
Saving SHALL store, for each sound that was chopped or corrected, its slice mode, division, sensitivity, slice points (as sample positions of the sound), each slice's pitch, gain, reverse and play mode, and the user's tempo and first-beat corrections. Clips made from slices SHALL store which sound and slice they came from and the slice's pitch. Reopening restores all of it.

#### Scenario: Round trip
- **WHEN** the user chops two sounds, corrects one tempo, sends slices to a track, saves, quits and reopens
- **THEN** both sounds have the same slices and settings, the tempo is the corrected one, and the slice clips are unchanged

### Requirement: Analysis lives in the cache folder
Analysis results (hits and tempo guesses) SHALL be written only inside the project's cache folder, keyed by the sound's content, and SHALL be recreated when that folder is deleted.

#### Scenario: Delete the cache
- **WHEN** the project's cache folder is deleted and a chopped sound is opened in the sample editor
- **THEN** it shows its saved slices at once and its tempo after analysis, and Transient mode works again once analysis has finished

### Requirement: Projects without slices still open
Projects saved by the previous version SHALL open unchanged and not marked as unsaved; their sounds have no slices until the user chops them. The schema version SHALL stay the same.

#### Scenario: Open an M2b project
- **WHEN** the user opens a project saved before the sample editor existed
- **THEN** it opens with no unsaved marker, and each sound opens in the editor as one slice
