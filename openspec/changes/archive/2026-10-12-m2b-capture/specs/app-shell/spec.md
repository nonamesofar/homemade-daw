# Spec Delta

## MODIFIED Requirements

### Requirement: Header transport and project controls
The header bar SHALL show, from left to right as in the mock: the view toggle, Stop, Play, Record, TEMPO, TIME SIG, POSITION, Click, Loop, the snap menu, the master volume and meter, and the audio status. Record is lit while a recording runs.

#### Scenario: Header controls present
- **WHEN** the main window is shown
- **THEN** each of these controls is visible in the header, and Record can be clicked

#### Scenario: Recording state in the header
- **WHEN** a recording is running
- **THEN** the Record button is lit, and after recording stops it is no longer lit

## ADDED Requirements

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
