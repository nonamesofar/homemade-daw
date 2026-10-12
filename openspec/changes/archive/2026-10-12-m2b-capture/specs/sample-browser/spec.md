# Spec Delta

## MODIFIED Requirements

### Requirement: Category filter
The browser SHALL offer the places All samples, Loops, One-shots and Recordings; choosing one shows only sounds of that category, combined with the search text. Recordings lists the takes recorded onto tracks.

#### Scenario: One-shots only
- **WHEN** the user chooses One-shots
- **THEN** only sounds with category One-shot are listed

#### Scenario: Recordings is empty for now
- **WHEN** the user chooses Recordings in a project without recorded takes
- **THEN** the list is empty

#### Scenario: Recordings lists takes
- **WHEN** the project has two imported loops and one recorded take, and the user chooses Recordings
- **THEN** only the take is listed

### Requirement: User can change a sound's category
The user SHALL be able to set a sound's category to Loop, One-shot or Recording from the row's context menu, as an undoable step.

#### Scenario: Reclassify
- **WHEN** the user sets "Pad chord" from Loop to One-shot
- **THEN** it appears under One-shots, the change survives save and reopen, and Undo sets it back to Loop

#### Scenario: Take becomes a loop
- **WHEN** the user sets a take's category to Loop
- **THEN** it leaves Recordings and appears under Loops

## ADDED Requirements

### Requirement: Takes show where they came from
A take's row SHALL show, in its tooltip, the input it was recorded from (the device, Master or the track name) and when.

#### Scenario: Tooltip
- **WHEN** the user hovers over a take recorded from Master
- **THEN** the tooltip says it was recorded from Master, with the date and time
