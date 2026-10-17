# Spec Delta

## MODIFIED Requirements

### Requirement: Headless render tool
The build SHALL produce a command-line tool that renders a project to a WAV file without opening a window or an audio device, at a given sample rate and block size, and exits with a non-zero code and a message if the project cannot be opened or rendered. It SHALL wait until the stretched versions of warped clips are ready, making missing ones first.

#### Scenario: Render a project
- **WHEN** the tool is run with a project folder and an output path
- **THEN** a WAV of the song from bar 1 to the end of its last clip is written, and the tool exits with code 0

#### Scenario: Bad project
- **WHEN** the tool is run with a folder that is not a project
- **THEN** no WAV is written and the tool exits with a non-zero code naming the problem

#### Scenario: Warped project without a cache
- **WHEN** the tool renders a project with Beats clips whose cache folder was deleted
- **THEN** the Beats clips are heard in the WAV, the same as with the cache present

### Requirement: Golden render tests
The test suite SHALL render a set of small fixed projects that use no third-party plugins at a fixed sample rate and block size, and compare each render with a stored reference WAV. A render fails when its length differs or the difference from the reference is above -90 dBFS peak. The set SHALL cover at least: clips on several tracks, an overlap trim, a split, a left trim, clip gain, reverse, track volume and pan, mute and solo, the master volume, a tempo change, each warp mode (Beats, Tones, Re-pitch), a transposed Beats clip, a looped clip, and fades.

#### Scenario: Golden tests in CI
- **WHEN** CI runs the tests
- **THEN** every golden project is rendered and matches its reference

#### Scenario: Intended change
- **WHEN** a developer changes playback on purpose and a golden test fails
- **THEN** a documented command rewrites the references, and the new references are committed only after listening to them
