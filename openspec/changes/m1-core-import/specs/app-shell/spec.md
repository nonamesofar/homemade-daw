# Spec Delta

## Purpose

Defines the main application window: its layout, menus, look, window title and how the app protects unsaved work when closing or switching projects.

## ADDED Requirements

### Requirement: Main window layout
The app SHALL open one main window containing a header bar, a sample browser panel on the left and a main view area that shows the arrangement placeholder with a hint to drop audio.

#### Scenario: First start
- **WHEN** the user starts the app with no project open
- **THEN** a new untitled project is open and the window shows the header bar, the browser panel and the main view area

### Requirement: Look follows the mock
The app SHALL draw its window, panels and widgets with the dark colour scheme and the IBM Plex Sans/Mono fonts from the UX mocks, without needing fonts installed on the machine.

#### Scenario: Fonts without installation
- **WHEN** the app runs on a Windows machine without IBM Plex installed
- **THEN** text is drawn in IBM Plex Sans

#### Scenario: Dark scheme
- **WHEN** the main window is shown
- **THEN** the background, panels, borders, text and accent colours match the mock's colour tokens

### Requirement: Menu bar
The app SHALL provide a File menu (New, Open, Save, Save As, Import Audio, Quit) and an Edit menu (Undo, Redo), with the platform's standard shortcuts (Mod+N, Mod+O, Mod+S, Mod+Shift+S, Mod+I, Mod+Z, Mod+Shift+Z and Mod+Y).

#### Scenario: Shortcut and menu do the same thing
- **WHEN** the user presses Mod+S or chooses File > Save
- **THEN** the project is saved in both cases

#### Scenario: Undo item names the step
- **WHEN** the last undoable step was an import of "kick.wav"
- **THEN** the Edit menu shows an enabled Undo item that names the import

### Requirement: Window title shows the project and its state
The window title SHALL show the project name, or "Untitled" for a project never saved, and a marker when there are unsaved changes.

#### Scenario: Unsaved marker
- **WHEN** the user imports a file into a saved project
- **THEN** the title shows the unsaved marker until the project is saved again

### Requirement: Unsaved changes are never lost silently
Before quitting, opening another project or creating a new one while the current project has unsaved changes, the app SHALL ask whether to save, discard or cancel.

#### Scenario: Cancel keeps the project open
- **WHEN** the project has unsaved changes and the user chooses Quit, then Cancel
- **THEN** the app stays open with the project and its changes unchanged

#### Scenario: Save then quit
- **WHEN** the project has unsaved changes and the user chooses Quit, then Save
- **THEN** the project is saved and the app quits

### Requirement: Job progress and error messages
The app SHALL show running background jobs (such as imports) with a progress indicator, and SHALL show errors as a toast message that disappears on its own.

#### Scenario: Import progress
- **WHEN** a long file is being imported
- **THEN** a progress indicator is visible until the import finishes

#### Scenario: Error toast
- **WHEN** an import fails
- **THEN** a toast names the file and the reason, and disappears after a few seconds
