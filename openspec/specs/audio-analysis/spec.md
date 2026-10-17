# audio-analysis Specification

## Purpose
Defines how Sampler finds the hits (onsets) and the tempo of a sound, when that analysis runs, how the user corrects the tempo and the first beat, and which results are saved with the project.

## Requirements

### Requirement: Sounds are analysed in the background
Each sound SHALL be analysed for its hits and its tempo on a background job, when it is imported or recorded and, for sounds that have no analysis yet (projects from earlier versions, a deleted cache), when it is first opened in the sample editor. The app SHALL stay responsive while analysis runs.

#### Scenario: Import starts analysis
- **WHEN** the user imports a 4-bar drum loop and then opens it in the sample editor
- **THEN** its tempo and hits are shown without a further wait

#### Scenario: Older project
- **WHEN** a project from the previous version is opened and one of its sounds is opened in the sample editor
- **THEN** the editor shows "Analysing..." and then the tempo and hits, and the rest of the app stays usable meanwhile

#### Scenario: Analysis speed
- **WHEN** a 10-second loop at 44.1 kHz is analysed on the reference laptop
- **THEN** the result is available within 1 second

### Requirement: Hits are found
The analysis SHALL find the hits of a sound with a strength for each, at least 30 ms apart, each placed on the start of the hit's rise (not after it).

#### Scenario: Synthetic drum pattern
- **WHEN** a test file has 16 drum hits at known positions
- **THEN** the analysis finds all 16, each within 5 ms before the true start, and no others

### Requirement: Tempo is estimated
The analysis SHALL estimate a tempo between 60 and 200 BPM with a confidence, and keep the next best guesses. A sound whose length is exactly 1, 2, 4, 8 or 16 bars at a tempo between 70 and 180 BPM (within 0.5%) SHALL get that tempo.

#### Scenario: Exact loop
- **WHEN** a loop that is 4 bars long at 90 BPM is analysed
- **THEN** its tempo is 90 BPM

#### Scenario: Pattern without an exact length
- **WHEN** a 7-second recording of a steady 120 BPM drum pattern is analysed
- **THEN** its tempo is within 1 BPM of 120

### Requirement: Analysis accuracy is measured
The project SHALL keep a set of test sounds with known tempo and hits and a test that reports, for the whole set, how many tempos are within 1% (counting half or double as a separate figure) and how many hits are found within 10 ms.

#### Scenario: Accuracy report
- **WHEN** the slow test suite is run
- **THEN** it prints the tempo and hit accuracy over the test set and fails if either falls below the level recorded when it was first accepted

### Requirement: First beat
The analysis SHALL place the first beat ("1.1.1") on the first strong hit within the first beat of the sound, or at the start if there is none.

#### Scenario: Silence before the first hit
- **WHEN** a loop starts with 100 ms of silence before its first kick
- **THEN** its first beat is on that kick

### Requirement: User corrects the tempo
In the sample editor the user SHALL be able to type a sound's tempo (20 to 999 BPM), halve it or double it. The correction is one undo step, is saved with the project, and is kept when the analysis runs again.

#### Scenario: Double the tempo
- **WHEN** a loop was analysed at 60 BPM and the user presses the double button
- **THEN** its tempo shows 120 BPM, its length in bars is recalculated, and Undo returns it to 60 BPM

#### Scenario: Correction survives a deleted cache
- **WHEN** the user typed 92 BPM for a sound, saved, deleted the project's cache folder and reopened the project
- **THEN** the sound's tempo is still 92 BPM

### Requirement: User moves the first beat
In the sample editor the user SHALL be able to drag the "1.1.1" marker to another position. The move is one undo step and is saved with the project.

#### Scenario: Move the first beat
- **WHEN** the user drags the "1.1.1" marker from the start to the first kick and releases
- **THEN** the bar numbers above the waveform start at the kick, and Undo moves the marker back

### Requirement: Analysis results are cache data
Hits and tempo guesses SHALL be stored only in the project's cache folder and SHALL be recomputed when missing; only the user's corrections are stored in the project. Running the analysis SHALL NOT change the project, mark it as unsaved or create an undo step.

#### Scenario: Opening analyses without changing the project
- **WHEN** a saved project from the previous version is opened and a sound is opened in the sample editor and analysed
- **THEN** the window title shows no unsaved marker and Undo is still disabled
