# Spec Delta

## Purpose

Defines how audio files enter a project: which files are accepted, how they are copied and decoded without ever changing the user's originals, and how problem files are reported.

## ADDED Requirements

### Requirement: Ways to import
The app SHALL import audio files dropped from the operating system onto the main window and files chosen with File > Import Audio, one or many at a time.

#### Scenario: Drop several files
- **WHEN** the user drops three supported files from Explorer onto the window
- **THEN** all three appear in the browser

#### Scenario: Import from the menu
- **WHEN** the user chooses File > Import Audio and selects a file
- **THEN** it appears in the browser

### Requirement: Supported formats
The app SHALL import WAV, AIFF, FLAC, MP3 and OGG Vorbis files, and SHALL reject other files with a message naming the file.

#### Scenario: Each format imports
- **WHEN** the user imports one file of each supported format
- **THEN** each one is listed in the browser and plays when auditioned

#### Scenario: Unsupported file
- **WHEN** the user drops a `.txt` or `.m4a` file
- **THEN** a toast says the file type is not supported and nothing is added

### Requirement: Original files are copied and never changed
Importing SHALL copy the file into the project's original-audio folder and SHALL NOT modify, move or lock the user's file.

#### Scenario: User file untouched
- **WHEN** a file is imported
- **THEN** the user's file has the same content hash as before, and a copy is in the project

#### Scenario: Name is kept readable
- **WHEN** "Break 90 (final).wav" is imported
- **THEN** the copy's file name starts with a cleaned-up form of that name followed by a short content hash

### Requirement: Duplicates are recognised by content
Importing a file whose content matches a sound already in the project SHALL NOT add a second copy; the existing sound is selected in the browser instead.

#### Scenario: Same file twice
- **WHEN** the user imports the same file twice
- **THEN** the browser lists it once and the project holds one copy

#### Scenario: Same content, different name
- **WHEN** the user imports two files with different names but identical bytes
- **THEN** the browser lists one sound

### Requirement: Decoded once to a working copy
Each imported sound SHALL be decoded once to an uncompressed 32-bit float WAV at its original sample rate, and playback and waveforms SHALL use that working copy.

#### Scenario: Sample rate is kept
- **WHEN** a 48 kHz FLAC is imported into a project played at 44.1 kHz
- **THEN** the working copy is 48 kHz and audition plays at the correct pitch

### Requirement: MP3 files have no leading gap
When decoding an MP3, the app SHALL remove the encoder delay and end padding recorded in the file's LAME header, and SHALL remove 1105 frames from the start when there is no such header.

#### Scenario: LAME-encoded loop
- **WHEN** an MP3 made by LAME from a WAV of exactly N frames is imported
- **THEN** the working copy has N frames, give or take one, and starts at the same sample as the WAV

#### Scenario: No LAME header
- **WHEN** an MP3 without a LAME header is imported
- **THEN** the first 1105 frames of the decoded audio are removed

### Requirement: Length limit
The app SHALL reject files longer than 30 minutes with a message, without copying them into the project.

#### Scenario: Too long
- **WHEN** the user imports a 31-minute file
- **THEN** a toast says it is longer than 30 minutes and the project folder is unchanged

### Requirement: Channel policy
Mono files SHALL stay mono and stereo files stereo; files with more than two channels SHALL be mixed down to stereo, with a message saying so.

#### Scenario: Five channels
- **WHEN** a 5-channel WAV is imported
- **THEN** the working copy is stereo and a toast says it was mixed down

### Requirement: Import does not block the app
Decoding SHALL run in the background; the window SHALL stay responsive and the new sound SHALL show as "importing" until it is ready.

#### Scenario: Long file
- **WHEN** a 10-minute file is being imported
- **THEN** the user can scroll and audition other sounds meanwhile, and the new row shows an importing state until ready

### Requirement: Failed imports are reported and leave no trace
A file that cannot be decoded SHALL produce a toast naming the file, and SHALL leave no sound, no undo step and no copied files in the project.

#### Scenario: Corrupt file
- **WHEN** the user imports a file with a `.wav` name that is not valid audio
- **THEN** a toast names the file and the project is unchanged

#### Scenario: One bad file among good ones
- **WHEN** the user drops two good files and one corrupt file together
- **THEN** the two good files are imported as one undo step and a toast names the corrupt one

### Requirement: Sounds get a category on import
Each imported sound SHALL get the category One-shot if it is shorter than 2 seconds and Loop otherwise, until tempo analysis exists.

#### Scenario: Short sound
- **WHEN** a 0.4-second kick is imported
- **THEN** its category is One-shot
