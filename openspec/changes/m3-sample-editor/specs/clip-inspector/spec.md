# Spec Delta

## MODIFIED Requirements

### Requirement: Features for later milestones are shown disabled
The inspector SHALL show the fade-in and fade-out fields (fixed at 0 ms) from the mock as disabled until the milestone that adds them.

#### Scenario: Disabled controls
- **WHEN** a clip is selected
- **THEN** FADE IN and FADE OUT are visible but cannot be changed

## ADDED Requirements

### Requirement: Open the clip's sound in the sample editor
The inspector's "Open in sample editor" button SHALL open the selected clip's sound in the sample editor. It is disabled when the clip's sound file is missing.

#### Scenario: Open from a clip
- **WHEN** the user selects a clip of "Break 90" and presses "Open in sample editor"
- **THEN** the sample editor shows "Break 90"

#### Scenario: Missing sound
- **WHEN** the selected clip is drawn as missing
- **THEN** "Open in sample editor" cannot be clicked
