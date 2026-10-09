# Spec Delta

## Purpose

Defines recording of the audio the computer is currently playing (for example a browser video) into an audio file, without a virtual cable.

## ADDED Requirements

### Requirement: Record computer audio to a file
The app SHALL record the audio the computer is playing through the default output device and save it as a WAV file.

#### Scenario: Thirty-second recording
- **WHEN** a video plays in the browser and the user records for 30 seconds
- **THEN** a WAV file of about 30 seconds is saved that contains the video's audio

### Requirement: Silence is preserved as time
When nothing is playing during a recording, the recording SHALL contain silence for that period so its length matches the elapsed time.

#### Scenario: Pause in playback
- **WHEN** the video is paused for 5 seconds in the middle of a 30-second recording
- **THEN** the file is still about 30 seconds long and the paused part is silent

### Requirement: Capture does not alter other applications
Capturing SHALL use shared mode and SHALL NOT silence or change the volume of other applications.

#### Scenario: Browser keeps playing
- **WHEN** a recording is running
- **THEN** the browser audio is still audible on the speakers (unless the app's own output is muted by policy)

### Requirement: Process loopback support is probed
The app SHALL try to capture while excluding its own process audio, and when that is unsupported SHALL fall back to whole-device capture without failing.

#### Scenario: Unsupported on this Windows build
- **WHEN** process-level capture is unavailable on Windows build 19045
- **THEN** the app falls back to device capture and the findings document records that process loopback is unavailable

### Requirement: Own output is not recorded back
While capturing in device mode, the app's own playback SHALL be muted by default so it does not appear in the recording.

#### Scenario: App playing during capture
- **WHEN** the app plays a clip while a device-mode capture is running
- **THEN** the recording does not contain that clip
