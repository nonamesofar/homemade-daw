# track-recording Specification

## Purpose

Defines recording onto tracks: arming tracks, choosing what each track records from (system audio, the master or another track), starting and stopping a recording, and how takes become clips and sounds in the project.

## Requirements

### Requirement: Arm a track
Each track header SHALL have an R button that arms the track for recording. Clicking R arms that track and disarms all others; Mod-clicking R adds or removes that track without changing the others. An armed track's R button is lit. Arm state is not saved with the project.

#### Scenario: Exclusive arm
- **WHEN** track 1 is armed and the user clicks R on track 2
- **THEN** track 2 is armed and track 1 is not

#### Scenario: Several tracks
- **WHEN** track 1 is armed and the user Mod-clicks R on track 3
- **THEN** tracks 1 and 3 are armed

#### Scenario: Reopen
- **WHEN** the user saves a project with an armed track and reopens it
- **THEN** no track is armed

### Requirement: Choose a track's input
Each track header SHALL have an input menu offering "System audio (<device>)" for each active playback device, "Master", and every other track by name. A track cannot choose itself. The current input is shown in the header. New tracks start on System audio of the Windows default playback device.

#### Scenario: Input list
- **WHEN** the project has tracks Drums, Bass and Vocal and the user opens Vocal's input menu
- **THEN** it lists the playback devices, Master, Drums and Bass, and not Vocal

#### Scenario: New track
- **WHEN** the user adds a track
- **THEN** its input shows System audio with the default playback device

### Requirement: Track inputs are saved and undoable
A track's input SHALL be saved with the project, and choosing it SHALL be one undo step named "Set track input". A saved System audio device that is not present when the project opens falls back to the default device, and the header says so.

#### Scenario: Round trip
- **WHEN** the user sets Bass's input to Master, saves and reopens
- **THEN** Bass's input is Master

#### Scenario: Device gone
- **WHEN** a track's input is System audio on a headset that is unplugged when the project is reopened
- **THEN** the track records from the default device and its input shows that the headset is missing

#### Scenario: Source track deleted
- **WHEN** Vocal records from Drums and the user deletes Drums
- **THEN** Vocal's input becomes System audio on the default device in the same undo step, and Undo restores both

### Requirement: Record button starts and stops recording
The header Record button and Mod+R SHALL start recording on all armed tracks. When the transport is stopped, starting a recording also starts playback from the playhead. Pressing Record again stops recording and playback continues; Stop stops both. The Record button is lit while recording.

#### Scenario: Start while stopped
- **WHEN** the playhead is at bar 5, the transport is stopped, a track is armed and the user presses Record
- **THEN** playback starts at bar 5 and recording starts with it

#### Scenario: Stop recording only
- **WHEN** a recording runs during playback and the user presses Record again
- **THEN** recording stops, the take is added, and playback goes on

#### Scenario: Stop both
- **WHEN** a recording runs and the user presses Stop
- **THEN** playback and recording stop and the take is added

### Requirement: Nothing armed
When no track is armed, pressing Record SHALL NOT start playback or recording and SHALL show a message to arm a track first.

#### Scenario: No armed track
- **WHEN** no track is armed and the user presses Mod+R
- **THEN** nothing starts and a message says to arm a track first

### Requirement: A take becomes a clip and a sound
When a recording stops, each armed track SHALL get one clip of its take starting where its take starts, replacing whatever was under it on that track (the no-overlap rule), and each take SHALL be added as a sound in the category Recording, named "<track name> <date> <time>". The take is one continuous clip even if playback looped while recording.

#### Scenario: Clip at the record position
- **WHEN** Drums is armed, the user starts recording at bar 5 during playback and stops at bar 9
- **THEN** Drums has a new clip from bar 5 to bar 9 and Recordings lists a sound named after Drums and the time

#### Scenario: Recording over a clip
- **WHEN** Drums has a clip covering bars 1 to 16 and the user records on Drums from bar 5 to bar 9
- **THEN** the old clip is split around the take, which plays from bar 5 to bar 9

#### Scenario: Loop on
- **WHEN** the loop covers bars 1 to 4, playback loops while recording from bar 1 for 8 bars
- **THEN** the take is one 8-bar clip starting at bar 1

### Requirement: Record several tracks at once
All armed tracks SHALL record at the same time, each from its own input, and their takes SHALL be added together. Tracks recording System audio from the same device share one take file. Two armed tracks where one records the other SHALL be refused with a message.

#### Scenario: Two inputs
- **WHEN** Vocal (System audio) and Resample (Master) are armed and the user records 4 bars
- **THEN** Vocal gets the browser's sound, Resample gets the mix, and both clips start at the same bar

#### Scenario: Chained arm refused
- **WHEN** Bass records from Drums and both are armed, and the user presses Record
- **THEN** nothing starts and a message says a track cannot record another armed track

### Requirement: Master and track takes are exact
A take from Master or from another track SHALL contain exactly what that output played, after its volume and pan, and SHALL start at the record start position with no trimming, sample-aligned with the song.

#### Scenario: Resample lines up
- **WHEN** the user records 4 bars of Master from bar 1 onto a new track, mutes the other tracks and plays
- **THEN** the take sounds like the song and stays in time with the grid, starting on bar 1

#### Scenario: Muted source
- **WHEN** a track records from Drums while Drums is muted
- **THEN** the take is silent and is not added (see the silent-take rule)

### Requirement: Recording does not play the input back
Recording SHALL NOT play a track's input through that track: the user hears the source itself (the video, the song), and no sound is fed back into a recording.

#### Scenario: No echo
- **WHEN** a track records System audio of the device Sampler plays to
- **THEN** the video is heard once, not twice, and the take has no repeated echo of itself

### Requirement: Live take display
While recording, each armed track SHALL show its take as a growing clip with a waveform of the audio so far, and the track's meter SHALL show the incoming level.

#### Scenario: Growing clip
- **WHEN** the user records System audio of a playing video for 10 seconds
- **THEN** a clip on the armed track grows with the playhead and shows the video's waveform, and the track meter moves with the sound

### Requirement: Silent takes are not added
A take with no sound louder than -60 dBFS SHALL NOT be added and SHALL leave no file; a message SHALL name the track whose take was silent. The other takes of the same recording are still added.

#### Scenario: One silent take
- **WHEN** two tracks record and one take is silent
- **THEN** only the other take is added and a message names the silent track

### Requirement: Recordings are limited to 30 minutes
A recording SHALL stop by itself after 30 minutes, add its takes as usual, and show a message that the limit was reached. Playback continues.

#### Scenario: Limit reached
- **WHEN** a recording runs for 30 minutes
- **THEN** recording stops, the takes are added, and a message says the limit was reached

### Requirement: Edits during a recording
While recording, clip, track, tempo, time signature and track input edits, and Undo and Redo, SHALL be refused with a message to stop recording first. Mixer controls (volume, pan, mute, solo, master volume) keep working and are heard in Master and track takes.

#### Scenario: Edit refused
- **WHEN** a recording runs and the user tries to delete a clip
- **THEN** the clip stays and a message says to stop recording first

#### Scenario: Fader ride while resampling
- **WHEN** a track records Master and the user lowers the Drums volume halfway through
- **THEN** the take's drums get quieter from that point
