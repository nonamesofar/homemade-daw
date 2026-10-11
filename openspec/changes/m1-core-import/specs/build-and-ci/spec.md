## MODIFIED Requirements

### Requirement: Reproducible Windows build
The project SHALL configure and build from a clean checkout on Windows 10/11 x64 using the documented presets, with all third-party code pinned to exact versions recorded in `external/VERSIONS.md`.

#### Scenario: Clean checkout builds
- **WHEN** a developer clones the repo with submodules and Git LFS files and runs the debug preset configure and build
- **THEN** the app, the developer tools and the test executables build with no errors and no warnings

#### Scenario: Versions are recorded
- **WHEN** a dependency submodule is added or updated
- **THEN** its exact version or commit is listed in `external/VERSIONS.md`

## ADDED Requirements

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
