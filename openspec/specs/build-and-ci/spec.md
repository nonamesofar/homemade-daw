# build-and-ci Specification

## Purpose

Defines how the project is built, tested and verified on Windows so that every later milestone starts from a reproducible, green baseline.

## Requirements

### Requirement: Reproducible Windows build
The project SHALL configure and build from a clean checkout on Windows 10/11 x64 using the documented presets, with all third-party code pinned to exact versions recorded in `external/VERSIONS.md`.

#### Scenario: Clean checkout builds
- **WHEN** a developer clones the repo with submodules and Git LFS files and runs the debug preset configure and build
- **THEN** the app, the developer tools and the test executables build with no errors and no warnings

#### Scenario: Versions are recorded
- **WHEN** a dependency submodule is added or updated
- **THEN** its exact version or commit is listed in `external/VERSIONS.md`

### Requirement: Engine code is optimised in debug builds
The engine library and the Tracktion module SHALL be compiled with optimisation even in the debug preset.

#### Scenario: Debug preset still plays audio without dropouts
- **WHEN** the debug build plays a stereo WAV for 60 seconds
- **THEN** no audio dropouts are reported by the device callback

### Requirement: Tests run through one command
The project SHALL run all Catch2 tests through the ctest preset, with slow tests selectable by label.

#### Scenario: Tests execute
- **WHEN** `ctest` is run with the debug preset
- **THEN** at least one test executes and passes

### Requirement: Continuous integration on Windows
Every push SHALL be built and tested on a Windows CI runner.

#### Scenario: CI green
- **WHEN** a commit is pushed to the repository
- **THEN** the Windows CI job builds and runs the tests, and reports success

### Requirement: Written verification report
The spike SHALL produce a findings document listing every "verify at M0" item with a pass or fail result and, for each failure, a written decision.

#### Scenario: Failed assumption has a decision
- **WHEN** a verify-at-M0 item fails
- **THEN** the findings document states the chosen workaround or the use of the fallback engine for that area

#### Scenario: Every item is covered
- **WHEN** the findings document is reviewed
- **THEN** it contains a row for each item in the M0 exit criteria

### Requirement: Reproducible audio fixtures
The repository SHALL contain a fixed set of synthesised audio files for demos and tests (WAV, AIFF, FLAC, OGG and a LAME-encoded MP3, plus mono and multichannel cases), stored in Git LFS, and a tool that regenerates them with the same audio content.

#### Scenario: Regenerate gives the same audio
- **WHEN** a developer runs the fixture generator on a clean checkout
- **THEN** every file it writes decodes to exactly the same samples as the committed one

#### Scenario: Large cases are not committed
- **WHEN** a test needs a file longer than 30 minutes
- **THEN** the test generates it in a temporary folder at run time and it is not stored in the repository

### Requirement: Developer executables live outside the product code
Probes, benchmarks and fixture generators SHALL build from `tools/`, the hosting test plugin from `tests/`, and the app from `src/app`; there SHALL be no separate spike application.

#### Scenario: One app
- **WHEN** the debug preset is built
- **THEN** exactly one GUI application is produced, the Sampler app

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
