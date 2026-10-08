# Spec Delta

## Purpose

Defines the baseline ability to decode audio files and play them through the Windows audio device, including transport control, tempo behaviour and offline rendering.

## ADDED Requirements

### Requirement: Play WAV and MP3 files
The app SHALL play WAV and MP3 files through the default Windows audio device using shared-mode WASAPI.

#### Scenario: WAV plays
- **WHEN** the user opens a WAV file and presses play
- **THEN** the audio is heard at the correct pitch and speed

#### Scenario: MP3 plays without encoder delay
- **WHEN** the user opens an MP3 file and presses play
- **THEN** the audio is heard at the correct pitch and speed, and the start is not preceded by the encoder padding

### Requirement: M4A decoder coverage is reported
The spike SHALL determine which M4A files the platform media decoder can read and record the result.

#### Scenario: Coverage recorded
- **WHEN** a set of AAC and ALAC M4A test files is opened
- **THEN** the findings document lists which decode and which do not

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
