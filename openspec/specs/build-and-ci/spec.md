# build-and-ci Specification

## Purpose

Defines how the project is built, tested and verified on Windows so that every later milestone starts from a reproducible, green baseline.

## Requirements

### Requirement: Reproducible Windows build
The project SHALL configure and build from a clean checkout on Windows 10/11 x64 using the documented presets, with all third-party code pinned to exact versions recorded in `external/VERSIONS.md`.

#### Scenario: Clean checkout builds
- **WHEN** a developer clones the repo with submodules and runs the debug preset configure and build
- **THEN** the spike app and test executables build with no errors and no warnings

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
