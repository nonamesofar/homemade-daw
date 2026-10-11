# Spec Delta

## Purpose

Defines previewing a sound through the audio output: starting and stopping it with the mouse, how it relates to the project, and what happens when the audio system is not ready yet.

## ADDED Requirements

### Requirement: Click to hear a sound
Clicking a sound's row in the browser SHALL play it from the start through the audio output; clicking the same row while it plays SHALL stop it.

#### Scenario: Play and stop
- **WHEN** the user clicks "Drums loop" once, then again while it plays
- **THEN** it starts playing on the first click and stops on the second

#### Scenario: Plays to the end
- **WHEN** the user clicks a sound and does nothing else
- **THEN** it plays once to the end and stops

### Requirement: One sound at a time
Starting an audition SHALL stop any audition already playing.

#### Scenario: Switch sound
- **WHEN** "Drums loop" is playing and the user clicks "Bass phrase"
- **THEN** "Drums loop" stops and "Bass phrase" starts

### Requirement: Playing state is visible
The row being auditioned SHALL show a playing indicator until playback stops.

#### Scenario: Indicator clears
- **WHEN** an auditioned sound reaches its end
- **THEN** its row no longer shows the playing indicator

### Requirement: Audition plays at the original speed and pitch
Audition SHALL play the sound at its original speed and pitch, whatever the project tempo or the device sample rate.

#### Scenario: Different sample rate
- **WHEN** a 48 kHz sound is auditioned on a 44.1 kHz device
- **THEN** it is heard at the correct pitch and speed

### Requirement: Audition does not change the project
Auditioning SHALL NOT change the project, mark it as unsaved or create an undo step.

#### Scenario: Saved state kept
- **WHEN** the user auditions sounds in a saved project
- **THEN** the window title shows no unsaved marker

### Requirement: Clicks during start-up are queued
If the user clicks a sound before the audio system is ready after start-up, the app SHALL remember the latest such click and play that sound once the audio system is ready.

#### Scenario: Early click
- **WHEN** the user clicks a sound while the audio system is still starting
- **THEN** that sound plays as soon as the audio system is ready

#### Scenario: Latest click wins
- **WHEN** the user clicks two different sounds while the audio system is still starting
- **THEN** only the second one plays when it is ready

#### Scenario: Stop before ready
- **WHEN** the user clicks a sound and then clicks it again while the audio system is still starting
- **THEN** nothing plays when it becomes ready

### Requirement: Audio problems are reported
If no audio output device can be opened, auditioning SHALL show a message instead of failing silently.

#### Scenario: No device
- **WHEN** no output device is available and the user clicks a sound
- **THEN** a toast says that no audio output is available
