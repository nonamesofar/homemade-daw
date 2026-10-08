# Spec Delta

## Purpose

Defines hosting of third-party VST3 instruments, with Native Instruments Maschine 3 as the acceptance plugin, including editor window, sync, multiple outputs, saved state and rendering.

## ADDED Requirements

### Requirement: Load Maschine 3 and show its editor
The app SHALL load the Maschine 3 VST3 on an instrument track and open its editor window.

#### Scenario: Editor opens
- **WHEN** the user loads Maschine 3 on a track and opens the plugin window
- **THEN** the Maschine interface is shown and responds to the mouse

### Requirement: Plugin follows host transport
The plugin SHALL play in time with the app's transport at 120 BPM.

#### Scenario: Pattern sync
- **WHEN** a Maschine pattern is set to play and the user presses play in the app
- **THEN** the pattern starts with the transport and stays in time for 16 bars

### Requirement: Multiple stereo outputs on separate tracks
The app SHALL route four stereo outputs of the plugin to four separate tracks, each with its own level control, while the plugin processes audio only once per block.

#### Scenario: Four outputs
- **WHEN** four Maschine groups are assigned to outputs 1 to 4
- **THEN** each group is heard only on its own track and each track meters independently

#### Scenario: Single plugin instance of processing
- **WHEN** the four output tracks play
- **THEN** CPU use is consistent with one plugin running, not four

### Requirement: Plugin state survives save and reopen
Saving and reopening the project SHALL restore the plugin's state.

#### Scenario: Reopen
- **WHEN** a project with a changed Maschine pattern is saved, closed and reopened
- **THEN** the same pattern and sounds are present

### Requirement: Offline render includes the plugin
The app SHALL render 16 bars containing the plugin to a WAV file offline.

#### Scenario: Render matches playback
- **WHEN** 16 bars are rendered offline
- **THEN** the file contains the plugin's output with correct timing, and the result is recorded in the findings document

### Requirement: Plugin crash isolation is not required at this stage
Plugins SHALL run inside the app process in this milestone; a plugin failure MAY close the app.

#### Scenario: Documented limitation
- **WHEN** the findings document is written
- **THEN** it states that in-process hosting was used and notes any instability seen with Maschine
