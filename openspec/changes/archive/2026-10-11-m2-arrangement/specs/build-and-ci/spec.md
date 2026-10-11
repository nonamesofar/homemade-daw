# Spec Delta

## ADDED Requirements

### Requirement: Headless render tool
The build SHALL produce a command-line tool that renders a project to a WAV file without opening a window or an audio device, at a given sample rate and block size, and exits with a non-zero code and a message if the project cannot be opened or rendered.

#### Scenario: Render a project
- **WHEN** the tool is run with a project folder and an output path
- **THEN** a WAV of the song from bar 1 to the end of its last clip is written, and the tool exits with code 0

#### Scenario: Bad project
- **WHEN** the tool is run with a folder that is not a project
- **THEN** no WAV is written and the tool exits with a non-zero code naming the problem

### Requirement: Golden render tests
The test suite SHALL render a set of small fixed projects that use no third-party plugins at a fixed sample rate and block size, and compare each render with a stored reference WAV. A render fails when its length differs or the difference from the reference is above -90 dBFS peak. The set SHALL cover at least: clips on several tracks, an overlap trim, a split, a left trim, clip gain, reverse, track volume and pan, mute and solo, the master volume, and a tempo change.

#### Scenario: Golden tests in CI
- **WHEN** CI runs the tests
- **THEN** every golden project is rendered and matches its reference

#### Scenario: Intended change
- **WHEN** a developer changes playback on purpose and a golden test fails
- **THEN** a documented command rewrites the references, and the new references are committed only after listening to them

### Requirement: Manual QA checklist
The repository SHALL contain a manual QA checklist for the things automated tests cannot judge (how it sounds and feels), with the steps of each milestone's exit run, and it SHALL be followed and ticked for each milestone from M2 on.

#### Scenario: Checklist for M2
- **WHEN** the M2 exit run is done
- **THEN** the checklist has an M2 section with its steps and the date it was last run
