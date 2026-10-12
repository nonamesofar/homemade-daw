# Proposal

## What this means for the user (plain language)

After this change Sampler records onto tracks, the way Ableton Live does. Each track has a record button (R) and a choice of what it records from. The user arms a track, presses the red Record button in the header, and the take appears as a clip on that track where recording started. Every take is also kept as a sound under Recordings in the browser, ready to reuse and (from the next milestone) to chop.

A track can record from:

- **System audio**: what the computer is playing on a playback device, for example a YouTube video in the browser. The user picks the device (speakers, headphones, ...); the Windows default device comes first. Sampler's own sound is left out.
- **Master**: the whole mix Sampler is playing ("resampling"), to turn a part of the song into one new sound.
- **Another track**: what that track plays, after its volume and pan, to bounce one part on its own.

With it the user can:

- Arm a track with R. Arming a track disarms the others; Mod-click arms several, and each records from its own source at the same time.
- Choose a track's source from the input menu in its track header. New tracks start on System audio. The choice is saved with the project and can be undone like other track changes.
- Press Record (or Mod+R). If the song is stopped, it starts playing from the playhead and recording starts with it. Press Record again to stop recording while the song keeps playing, or Stop to stop both.
- Watch the take grow as a red clip on each armed track while recording, with the track's meter showing the incoming level. If a System audio take hears nothing for 3 seconds, the growing clip says "No sound is coming in" (usually the wrong device or a paused video).
- Get the take as a clip that replaces whatever was under it on that track, and as a sound under Recordings named after the track and the time. All takes of one recording are one undo step. Arming, choosing a source and starting or stopping are not undo steps.
- Rely on clean takes: Master and track takes line up exactly with the song. System audio takes are lined up as closely as Windows allows, and the silence at their start and end is cut off. A take that is silent all the way through is not added, and a message says so.
- Keep a take that ended badly: if the playback device is unplugged or the disk fills up, what was recorded so far is kept and a message says why it stopped. Recording stops by itself after 30 minutes.

When Windows cannot leave Sampler out of a System audio take, Sampler goes silent while recording (the song keeps running) so it does not end up in the take. While recording, Save As is refused; New, Open and Quit first stop and keep the recording.

The work is split into small demos that the product owner checks one by one on the same branch (listed in `tasks.md`). At the end the product owner runs the exit test: play a YouTube video, record 1 minute onto an armed track, and see it in Recordings and as a clip at the playhead, with no Sampler sound in it; then resample 4 bars of the song from Master onto a new track.

Not in this change: recording only one app (moved to M7); recording microphones or interface inputs, and listening to the input while recording (monitoring) (M8); count-in, punch in/out and loop takes (later); keeping the seconds before Record was pressed (pre-roll, later); opening a take in the sample editor (M3); macOS (TODO-MAC).

## Why

Capturing computer audio is a hard requirement of the MVP (design §2.7). The PO wants it in the familiar Ableton shape (arm a track, pick its input, press Record) rather than a separate capture button, and wants the same mechanism to resample the master and other tracks, which is the quickest way to build new loops from a song. M0 proved loopback capture on the user's Windows 10 build, including leaving our own process out, and M2 gives tracks, the transport and the no-overlap rule to land takes on. This pulls track arm forward from M8; microphone and interface inputs stay in M8.

## What Changes

- **Track inputs and arm**: each track gets an input (System audio on a device, Master, or another track) saved in the project, and a working arm button (exclusive, Mod-click for several, not saved).
- **Recording session**: the header Record button and Mod+R start and stop recording on all armed tracks, starting the transport if needed; one recording produces one take per armed track.
- **Three take sources behind one pipeline**: the M0 loopback capture for System audio; a listener on another track's output; a tap on the master output (a spike decides how). All feed the same writer into `audio/recordings/`.
- **Placing takes**: one undoable command adds the take sounds (category Recording) and their clips at the record start, following the no-overlap rule. System takes are corrected for the device latency and trimmed; Master and track takes are sample-exact and untrimmed.
- **"Don't record Sampler"** for System audio: exclude our process when Windows allows it, else mute our output while recording.
- **UI**: arm button and input menu in the track header, growing take clips with a live waveform, input level on the track meter, the no-signal warning, Record enabled in the header.
- No **BREAKING** changes. Projects from M2 open unchanged (tracks get System audio as their input); schema version stays 1.

## Capabilities

### New Capabilities
- `track-recording`: arming tracks, choosing a track's input (System audio, Master, another track), starting and stopping a recording from the header, takes as clips and as Recordings, recording several tracks at once, and what is undoable and saved.

### Modified Capabilities
- `computer-audio-capture`: the M0 spike requirements become the System audio input: device choice, leaving Sampler out or muting it, trimming, the no-signal warning, the 30-minute limit and failures.
- `track-mixer`: the disabled record-arm requirement is removed (arm now lives in `track-recording`).
- `app-shell`: the header Record button is enabled and records armed tracks, Mod+R, and New/Open/Quit/Save As during a recording.
- `sample-browser`: the Recordings place lists takes; Recording in the category menu.
- `undo-redo`: one undo step per recording; arm and start/stop are not undoable; setting a track's input is.
- `project-bundle`: takes live in `audio/recordings/`; track inputs are saved.

## Impact

- `src/platform`: the capture interface and the WASAPI class (choose a device, live level, trim, limit) and a portable capture pipeline shared by all three sources.
- `src/engine`: a Tracktion-free `TakeRecorder` facade: listeners on track outputs (Tracktion's per-track input devices), the master tap, the record-start position on the audio clock, live levels for the UI. Its audio-thread code is real-time code (AGENTS.md §5.1).
- `src/commands`: `SetTrackInput` and `AddTakes`, reusing the M2 placement code.
- `src/app`: a recording controller (states, policy, finishing on a worker) wired into `AppController`, Mod+R, project-switch guards.
- `src/ui`: arm and input menu in the track header, growing take clips, the Record button.
- No new third-party dependency.
- Docs: `ROADMAP.md` (M2b scope gains track arm and resampling, loses one-app; decisions log), design doc §2.7 and §15 (same scope change), `docs/qa-checklist.md`, `docs/projects.md`.
