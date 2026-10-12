# Spec Delta

## MODIFIED Requirements

### Requirement: Record computer audio to a file
A track recording System audio SHALL record what the computer plays on the chosen playback device into a 32-bit float WAV file inside the open project, at the device's own sample rate, with stereo kept as stereo and more than two channels mixed down to stereo.

#### Scenario: Thirty-second recording
- **WHEN** a video plays in the browser and the user records an armed System audio track for 30 seconds
- **THEN** a WAV file of about 30 seconds is saved in the project that contains the video's audio

#### Scenario: Pick a device
- **WHEN** the computer has speakers and a headset, and the armed track's input is "System audio (Headset)"
- **THEN** the take contains what plays on the headset and not what plays only on the speakers

#### Scenario: Surround output device
- **WHEN** the chosen playback device runs with 6 channels
- **THEN** the take is stereo and contains the sound of all channels

### Requirement: Own output is not recorded back
When the app's own sound cannot be left out of a System audio take and the app plays to the device being recorded, the app SHALL mute its own output while recording, while the transport keeps running. The mute SHALL NOT change the project, its undo history or the mixer settings.

#### Scenario: App playing during capture
- **WHEN** the song plays while a System audio take of the same device is recorded and Sampler cannot be left out
- **THEN** the take does not contain the song, and the song is silent until recording stops

#### Scenario: Different output device
- **WHEN** the app plays to the speakers while a track records System audio of the headset
- **THEN** the app's output is not muted

#### Scenario: Mute is not saved
- **WHEN** the user saves the project during a muted recording and reopens it after the recording stopped
- **THEN** all volumes and mute buttons are as before the recording

## REMOVED Requirements

### Requirement: Process loopback support is probed
**Reason**: The M0 probe is done (process loopback works on build 19045; recorded in `docs/m0-findings.md`). Its user-facing behaviour is now covered by "Sampler is left out of System audio takes".
**Migration**: None for users. The probe tool stays as a developer check.

## ADDED Requirements

### Requirement: Sampler is left out of System audio takes
For System audio on the Windows default playback device, the app SHALL first try to record everything except its own sound, and SHALL fall back to recording the whole device when that is not possible, without failing (the mute rule then applies). The song stays audible when exclusion works.

#### Scenario: Exclusion works
- **WHEN** process-level capture is available and the song plays while a track records System audio of the default device
- **THEN** the take does not contain the song and the song stays audible

#### Scenario: Exclusion unavailable
- **WHEN** process-level capture is unavailable
- **THEN** the recording still starts, records the whole device, and the mute rule applies

### Requirement: System audio takes are trimmed
A System audio take SHALL begin shortly before the first sound louder than -60 dBFS and end shortly after the last one; silence in the middle is kept. A margin of a few milliseconds is kept at each end. Its clip starts where that first sound was heard.

#### Scenario: Late start
- **WHEN** recording starts at bar 5, the video's first sound comes 2 seconds later and recording stops 3 seconds after the video ends
- **THEN** the take starts with the video's first sound, and its clip starts 2 seconds after bar 5

#### Scenario: Pause kept
- **WHEN** the video is paused for 5 seconds in the middle of the recording
- **THEN** those 5 seconds are silence in the take

### Requirement: System audio takes line up with what was heard
A System audio take's clip SHALL be placed at the moment its sound was played, correcting for the delay the device reports, so it lines up with the song to within the device's latency.

#### Scenario: Recording along with the song
- **WHEN** the song plays and a click track video plays in the browser in time with it while recording
- **THEN** the take's clicks are within a few milliseconds of the song's beats

### Requirement: No-signal warning
When a System audio take has heard nothing louder than -60 dBFS for 3 seconds, its growing clip SHALL show "No sound is coming in" with the device name, and SHALL hide it as soon as sound arrives.

#### Scenario: Wrong device
- **WHEN** a track records a device nothing plays on
- **THEN** after about 3 seconds the growing clip shows the warning naming the device

#### Scenario: Sound arrives
- **WHEN** the warning is shown and the video starts playing
- **THEN** the warning disappears

### Requirement: Failed System audio takes keep what was recorded
When the recorded device disappears or the take file cannot be written, the app SHALL stop recording, add what was recorded so far (if it is not silent), and show a message saying why. If audio was lost because the app could not keep up, the take is kept and a message says so. A take that cannot start SHALL stop the whole recording from starting, leave no file and show the reason.

#### Scenario: Headset unplugged
- **WHEN** the user unplugs the recorded headset 20 seconds into a recording
- **THEN** recording stops, the 20 seconds are added and a message says the device went away

#### Scenario: Cannot start
- **WHEN** the armed track's device cannot be opened
- **THEN** no recording starts, playback does not start, no file is left and a message names the device and the reason
