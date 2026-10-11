# Spec Delta

## ADDED Requirements

### Requirement: Main view shows the arrangement
The main view area SHALL show the arrangement: the track headers on the left, the ruler and lanes on the right, and the clip inspector in a bottom panel. The "Drop audio here" hint is shown in the lanes while the project has no clips, with the text "Drag a sample from the browser onto the timeline to create a clip". The Session / Arrange toggle shows Arrange; Session is disabled until the Session view exists.

#### Scenario: Empty project
- **WHEN** the user starts the app
- **THEN** the main view shows one empty track, the ruler, the hint text and an empty bottom panel, and Session cannot be chosen

### Requirement: Header transport and project controls
The header bar SHALL show, from left to right as in the mock: the view toggle, Stop, Play, Record (disabled until capture exists), TEMPO, TIME SIG, POSITION, Click, Loop, the snap menu, the master volume and meter, and the audio status.

#### Scenario: Header controls present
- **WHEN** the main window is shown
- **THEN** each of these controls is visible in the header, and Record cannot be clicked

### Requirement: Edit and Track menus
The Edit menu SHALL also offer Cut, Copy, Paste, Duplicate, Split, Delete and Select All (Mod+A selects every clip), and a Track menu SHALL offer New Audio Track (Mod+T) and Delete Track. A menu item is disabled when its action cannot apply (for example Paste with an empty clipboard).

#### Scenario: Disabled paste
- **WHEN** nothing has been copied yet
- **THEN** Edit > Paste is disabled

### Requirement: Keyboard shortcuts follow focus
Key presses SHALL go to the most specific active scope: an open dialog first, then a focused text field, then the focused view, then global shortcuts. Global shortcuts (Space, Home, Enter, L, Mod+L, Mod+T, the File and Edit menu shortcuts) work from any view, but not while a text field or dialog takes the key. Holding a key down does not repeat transport actions.

#### Scenario: Space in a text field
- **WHEN** the user is renaming a track and presses Space
- **THEN** a space is typed into the name and playback does not start

#### Scenario: Space in the browser
- **WHEN** the browser list (not its search box) has focus and the user presses Space
- **THEN** playback starts or stops
