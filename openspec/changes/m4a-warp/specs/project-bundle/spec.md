# Spec Delta

## MODIFIED Requirements

### Requirement: Derived files live in the cache folder
Generated files (engine proxies, thumbnails, stretched versions of sounds for warped clips) SHALL be written inside the project's cache folder and never next to the user's audio.

#### Scenario: Waveform thumbnails
- **WHEN** clips are shown in the timeline
- **THEN** generated thumbnail and proxy files appear only under the project's cache folder

#### Scenario: Stretched versions
- **WHEN** a project with Beats and Tones clips is played
- **THEN** their stretched versions appear only under the project's cache folder

## ADDED Requirements

### Requirement: Clip warp, loop and fades are part of the project
Saving SHALL store each clip's warp on/off, mode, own tempo, transpose, loop on/off, the looped part of the sound and its fades. Reopening restores them, and the arrangement sounds the same.

#### Scenario: Round trip
- **WHEN** the user makes clips in Beats, Tones and Re-pitch mode, transposes one, loops one over 8 bars, fades two, saves, quits and reopens the project
- **THEN** every clip has the same settings and the arrangement sounds the same as before saving

### Requirement: Stretched versions are remade when missing
When a warped clip's stretched version is missing (for example after the cache folder was deleted), the app SHALL make it again in the background. The clip is not reported as missing; it is drawn as "rendering" until it is ready.

#### Scenario: Delete the cache
- **WHEN** a project with Beats clips is closed, its cache folder deleted, and the project reopened
- **THEN** no file is reported missing, the Beats clips show "rendering" briefly and then play as before

### Requirement: Projects without warp settings still open
Projects saved by earlier versions SHALL open with every clip's WARP off, so they sound as before, without being marked as changed. The schema version SHALL stay the same.

#### Scenario: Open an M3 project
- **WHEN** the user opens a project saved before warping existed
- **THEN** its clips play at their own speed as before, WARP is off on each, and the title shows no unsaved marker
