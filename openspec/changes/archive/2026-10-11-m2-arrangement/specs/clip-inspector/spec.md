# Spec Delta

## Purpose

Defines the bottom panel that shows the selected clip's details and lets the user change its gain, start position and playback direction.

## ADDED Requirements

### Requirement: Inspector shows the selected clip
When exactly one clip is selected, the bottom panel SHALL show its name, its track name, the bars it covers ("Bars 1–4"), its waveform drawn in the track colour, and its gain and start position. With no clip or several clips selected, the panel SHALL say what is selected ("No clip selected", "3 clips selected") and show no clip fields.

#### Scenario: Select a clip
- **WHEN** the user clicks a clip "Break 90" on track "Drums" covering bars 5 to 8
- **THEN** the panel shows "Break 90", "Drums · Bars 5–8" and the clip's waveform

#### Scenario: Follows edits
- **WHEN** the selected clip is moved by two bars in the timeline
- **THEN** the panel's bars and start position update at once

### Requirement: Clip gain
The inspector SHALL let the user set the clip's gain from silence ("-inf") to +24 dB by dragging or typing, default 0.0 dB, heard at once during playback. A drag is one undo step.

#### Scenario: Lower the gain
- **WHEN** the user sets a clip's gain to -6 dB while it plays
- **THEN** the clip is heard 6 dB quieter and its track meter drops accordingly

### Requirement: Clip start position
The inspector SHALL show the clip's start as bar . beat . sixteenth and let the user type a new one. The clip moves there on its track, following the no-overlap rule, as one undo step. Invalid text is refused.

#### Scenario: Type a start
- **WHEN** the user types "9.1.1" in START for a clip at bar 5
- **THEN** the clip moves to bar 9 on the same track

### Requirement: Reverse
The inspector SHALL have a Reverse toggle that plays the clip backwards. The clip's waveform is drawn reversed. Reversing is one undo step and is saved. The sound's file is not changed.

#### Scenario: Reverse a clip
- **WHEN** the user turns Reverse on for a clip of a drum loop and plays it
- **THEN** the loop is heard backwards from its last hit to its first, and the waveform is mirrored

#### Scenario: Audio file untouched
- **WHEN** a reversed clip's project is saved
- **THEN** the sound's files in the project have the same content as before

### Requirement: Features for later milestones are shown disabled
The inspector SHALL show the fade-in and fade-out fields (fixed at 0 ms) and the "Open in sample editor" button from the mock as disabled until the milestones that add them.

#### Scenario: Disabled controls
- **WHEN** a clip is selected
- **THEN** FADE IN, FADE OUT and "Open in sample editor" are visible but cannot be changed or clicked
