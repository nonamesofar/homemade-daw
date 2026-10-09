# arrangement-timeline Specification

## Purpose

Defines the multi-track timeline view where audio clips are shown with waveforms and arranged by dragging, proving the custom UI is responsive at realistic size.

## Requirements

### Requirement: Timeline shows tracks and clips
The timeline SHALL display 4 tracks holding 50 audio clips in total, each clip drawn with its waveform.

#### Scenario: Initial view
- **WHEN** the spike project is opened
- **THEN** 4 track lanes appear and every clip shows its waveform

### Requirement: Scroll and zoom stay responsive
The timeline SHALL scroll horizontally and zoom in and out smoothly with 50 clips loaded.

#### Scenario: Zoom while scrolling
- **WHEN** the user zooms and scrolls continuously for 10 seconds
- **THEN** the view redraws at 30 frames per second or better and waveforms remain visible

### Requirement: Drag clips
The user SHALL be able to drag a clip to a new position, including onto another track.

#### Scenario: Move to another track
- **WHEN** the user drags a clip from track 1 to track 3 and releases
- **THEN** the clip appears on track 3 at the dropped position and is gone from track 1

### Requirement: Reorder tracks by dragging
The user SHALL be able to reorder tracks by dragging a track header; the track's clips move with it.

#### Scenario: Move track up
- **WHEN** the user drags track 3 above track 1
- **THEN** track 3 becomes the first lane and keeps all its clips
