# Spec Delta

## Purpose

Defines how changes to a project are made and reversed: every edit is one named, undoable step, applied through one path, and the project stays consistent after every step.

## ADDED Requirements

### Requirement: Every project edit is one undo step
Each user action that changes the project SHALL be recorded as exactly one undo step with a readable name, however many internal changes it makes.

#### Scenario: Import is one step
- **WHEN** the user imports one file and then chooses Undo once
- **THEN** the sound is gone from the project and the browser, and the project is the same as before the import

#### Scenario: Multi-file drop is one step
- **WHEN** the user drops three files at once and then chooses Undo once
- **THEN** all three sounds are removed

### Requirement: Redo
After an undo, the app SHALL redo the undone step until a new edit is made, which clears the redo history.

#### Scenario: Redo restores
- **WHEN** the user undoes an import and then chooses Redo
- **THEN** the sound is back in the project and the browser with the same name, category and waveform

#### Scenario: New edit clears redo
- **WHEN** the user undoes an import and then changes another sound's category
- **THEN** Redo is disabled

### Requirement: Undo restores the exact previous state
Undoing a step SHALL return the saved form of the project to exactly what it was before that step.

#### Scenario: Round trip on the saved form
- **WHEN** any sequence of edits is applied and then undone step by step
- **THEN** after each undo the project's saved form equals its saved form before the matching edit

### Requirement: What is not undoable
Saving, opening, auditioning, browser search text, category filter selection and scrolling SHALL NOT create undo steps.

#### Scenario: Audition is not undone
- **WHEN** the user auditions a sound and then chooses Undo
- **THEN** the undo reverts the last edit made before the audition, not the audition

### Requirement: Undo history is per project and not saved
The undo history SHALL start empty when a project is created or opened and SHALL NOT be stored in the project.

#### Scenario: Reopen starts fresh
- **WHEN** the user imports a file, saves, closes and reopens the project
- **THEN** Undo is disabled

### Requirement: Saving does not change undo history
Saving SHALL keep the undo and redo history unchanged; undoing after a save SHALL mark the project as having unsaved changes.

#### Scenario: Undo after save
- **WHEN** the user imports a file, saves, then chooses Undo
- **THEN** the sound is removed and the window title shows the unsaved marker

### Requirement: Consistency checks in development builds
In development builds, the app SHALL check after every edit that ids are unique and every sound's files are inside the project, and stop with a clear message if not.

#### Scenario: Duplicate id is caught
- **WHEN** a development build applies an edit that would leave two sounds with the same id
- **THEN** the check fails at once, naming the edit
