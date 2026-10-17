# app-shell Specification

## Purpose

Defines the main application window: its layout, menus, look, window title and how the app protects unsaved work when closing or switching projects.

## Requirements

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

### Requirement: Main view shows the arrangement
The main view area SHALL show the arrangement: the track headers on the left, the ruler and lanes on the right, and the clip inspector in a bottom panel. The "Drop audio here" hint is shown in the lanes while the project has no clips, with the text "Drag a sample from the browser onto the timeline to create a clip". The Session / Arrange toggle shows Arrange; Session is disabled until the Session view exists. While a sound is open in the sample editor, the main view area shows the sample editor in place of the arrangement and the clip inspector, until the user goes back; the Session / Arrange toggle then shows neither, and choosing Arrange goes back.

#### Scenario: Empty project
- **WHEN** the user starts the app
- **THEN** the main view shows one empty track, the ruler, the hint text and an empty bottom panel, and Session cannot be chosen

#### Scenario: Arrange from the toggle
- **WHEN** the sample editor is shown and the user clicks Arrange in the header toggle
- **THEN** the arrangement is shown as it was before the editor opened

#### Scenario: New project closes the editor
- **WHEN** the sample editor is shown and the user creates or opens another project
- **THEN** the main view shows that project's arrangement

### Requirement: Header transport and project controls
The header bar SHALL show, from left to right as in the mock: the view toggle, Stop, Play, Record, TEMPO, TIME SIG, POSITION, Click, Loop, the snap menu, the master volume and meter, and the audio status. Record is lit while a recording runs.

#### Scenario: Header controls present
- **WHEN** the main window is shown
- **THEN** each of these controls is visible in the header, and Record can be clicked

#### Scenario: Recording state in the header
- **WHEN** a recording is running
- **THEN** the Record button is lit, and after recording stops it is no longer lit

### Requirement: Edit and Track menus
The Edit menu SHALL also offer Cut, Copy, Paste, Duplicate, Split, Delete and Select All (Mod+A selects every clip), and a Track menu SHALL offer New Audio Track (Mod+T) and Delete Track. A menu item is disabled when its action cannot apply (for example Paste with an empty clipboard).

#### Scenario: Disabled paste
- **WHEN** nothing has been copied yet
- **THEN** Edit > Paste is disabled

### Requirement: Keyboard shortcuts follow focus
Key presses SHALL go to the most specific active scope: an open dialog first, then a focused text field, then the sample editor's pad keys (while the sample editor has focus), then the focused view, then global shortcuts. Global shortcuts (Space, Home, Enter, L, Mod+L, Mod+T, Mod+R, the File and Edit menu shortcuts) work from any view, but not while a text field or dialog takes the key. Holding a key down does not repeat transport actions.

#### Scenario: Space in a text field
- **WHEN** the user is renaming a track and presses Space
- **THEN** a space is typed into the name and playback does not start

#### Scenario: Space in the browser
- **WHEN** the browser list (not its search box) has focus and the user presses Space
- **THEN** playback starts or stops

#### Scenario: Pad key in the sample editor
- **WHEN** the sample editor has focus and the user presses L
- **THEN** looping is toggled, because L is not a pad key, while pressing A plays pad 5 instead of any other action

#### Scenario: Undo in the sample editor
- **WHEN** the sample editor has focus and the user presses Mod+Z
- **THEN** the last edit is undone and no pad plays

### Requirement: Record shortcut
Mod+R SHALL do what the Record button does, from any view, under the same focus rules as the other global shortcuts. Holding the key down does not repeat it.

#### Scenario: Start and stop by key
- **WHEN** a track is armed, the arrangement has focus, and the user presses Mod+R, waits, and presses Mod+R again
- **THEN** a recording starts and then stops and its take is added

#### Scenario: Typing a track name
- **WHEN** the user is renaming a track and presses Mod+R
- **THEN** no recording starts

### Requirement: Project actions during a recording
While a recording runs or its takes are being finished, Save As SHALL be refused with a message. New, Open and Quit SHALL first stop the recording and add its takes to the current project, then ask about unsaved changes as usual. Save SHALL work and saves the project without the unfinished takes, except in an untitled project, where Save asks for a location like Save As and is refused the same way.

#### Scenario: Quit while recording
- **WHEN** a recording runs in a saved project and the user chooses Quit
- **THEN** the recording stops, its takes are added, and the app asks whether to save the changes

#### Scenario: Save As refused
- **WHEN** a recording runs and the user chooses Save As
- **THEN** a message says to stop recording first and no dialog opens
