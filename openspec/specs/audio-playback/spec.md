# audio-playback Specification

## Purpose

Defines the baseline ability to decode audio files and play them through the Windows audio device, including transport control, tempo behaviour and offline rendering.

## Requirements

### Requirement: Play WAV and MP3 files
The app SHALL play WAV and MP3 files through the default Windows audio device using shared-mode WASAPI.

#### Scenario: WAV plays
- **WHEN** the user opens a WAV file and presses play
- **THEN** the audio is heard at the correct pitch and speed

#### Scenario: MP3 plays at the correct speed
- **WHEN** the user opens an MP3 file and presses play
- **THEN** the audio is heard at the correct pitch and speed

### Requirement: MP3 encoder delay behaviour is reported
The spike SHALL determine whether the engine's decoder removes the MP3 encoder delay and padding, and record the result and the planned handling.

#### Scenario: Delay measured
- **WHEN** a LAME-encoded MP3 is decoded by the engine
- **THEN** the findings document states whether its decoded length matches the true audio length, and that trimming is done at import (M1) if it does not

### Requirement: M4A is out of scope for M0
M4A decoding SHALL NOT be a required result of this milestone; any M4A findings are recorded for information only.

#### Scenario: Not required
- **WHEN** M0 exit criteria are checked
- **THEN** no M4A result is needed to pass

### Requirement: Clips keep beat positions on tempo change
Changing the project tempo SHALL keep each clip at the same beat position, so the clip moves in time.

#### Scenario: Tempo change
- **WHEN** a clip starts at beat 8 and the tempo changes from 120 to 90 BPM
- **THEN** the clip still starts at beat 8 and its start time in seconds has increased

### Requirement: Offline render
The app SHALL render a range of the project to a WAV file faster than real time.

#### Scenario: Sixteen bars render
- **WHEN** 16 bars at 120 BPM are rendered offline
- **THEN** a 32-second stereo WAV is produced in less time than 32 seconds

### Requirement: Warp engine benchmarks are recorded
The spike SHALL measure the cost of stretching with the chosen stretch library and of a prototype beat-preserving render, and record the numbers.

#### Scenario: Benchmarks recorded
- **WHEN** a 4-bar loop and a 3-minute track are processed
- **THEN** the findings document lists the time taken for each
