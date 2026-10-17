# Spec Delta

## ADDED Requirements

### Requirement: Open a sound in the sample editor from the browser
Double-clicking a sound's row, or choosing "Open in sample editor" from its context menu, SHALL open that sound in the sample editor. A single click keeps auditioning the sound as before.

#### Scenario: Double-click
- **WHEN** the user double-clicks "Break 90"
- **THEN** the sample editor opens "Break 90" and any audition started by the first click stops

#### Scenario: Context menu
- **WHEN** the user right-clicks a recorded take and chooses "Open in sample editor"
- **THEN** the sample editor opens that take

### Requirement: Open sound is marked
The row of the sound open in the sample editor SHALL be marked while the editor shows it.

#### Scenario: Marker follows the editor
- **WHEN** "Break 90" is open in the sample editor and the user goes back to the arrangement
- **THEN** the mark on "Break 90" is gone
