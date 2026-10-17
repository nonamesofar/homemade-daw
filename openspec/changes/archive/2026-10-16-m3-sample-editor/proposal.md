# Proposal

## What this means for the user (plain language)

After this change Sampler can chop sounds. Any sound in the project (an imported loop, a one-shot or a recorded take) opens in a full-screen **sample editor**. There the user cuts it into slices, plays the slices from 16 pads with the mouse or the computer keyboard, and lays them out on a new track in the arrangement.

With it the user can:

- **Open a sound in the sample editor** by double-clicking it in the browser, choosing "Open in sample editor" from its menu, or pressing "Open in sample editor" in the clip panel for a selected clip. "< Arrange" goes back to the arrangement as it was.
- **See what the sound is**: its name, its length in bars, its tempo (BPM), its sample rate and whether it is mono or stereo. Sampler works out the tempo and where the hits are by itself, in the background, within a second or two for a typical loop. If the tempo guess is off, the user types the right one or halves or doubles it, and can move the "1.1.1" marker to where the first beat really is.
- **Zoom into the waveform** down to the single samples and scroll through it.
- **Chop it** in one of three ways:
  - **Beat**: slices on a musical grid (1/4, 1/8, 1/16 or 1/32 notes) at the sound's tempo, starting from the first beat.
  - **Transient**: a slice at every hit. The Sensitivity slider (0 to 100) shows how many slices it will make as it moves, and "Auto-slice" keeps them.
  - **Manual**: double-click to add a slice point, drag one to move it, double-click it or press Delete to remove it.
  The slider or the grid choice that does not apply to the current way is greyed out. A counter shows how many slices there are (up to 256). The numbered slice strip above the waveform selects a slice; the selected slice is shaded.
- **Play slices from 16 pads** laid out like an MPC (pad 1 bottom left). Click a pad or press its key (1 2 3 4 / Q W E R / A S D F / Z X C V, Z is pad 1); the pad lights while it sounds and selects its slice. More than 16 slices are reached through pad banks A, B, C and so on. Pads can be played over the running song and play several slices at once, like a drum machine. The pad keys only play while the sample editor is in front and no text box is being typed in; elsewhere the keys keep their usual jobs.
- **Shape each slice**: change its start and end (shown in bars.beats.sixteenths of the sound, or in seconds or samples), its pitch (-24 to +24 semitones, length unchanged), its gain, play it backwards, and choose how its pad plays: **One-shot** (to the end whatever the key does), **Gate** (stops when the key is let go) or **Loop** (repeats while held). Changes are heard on the next pad hit.
- **Audition** the whole sound or the selected slice with the Audition button.
- **Send the slices to a new track**: "Slices to new track" makes a track named after the sound with one clip per slice, back to back from the playhead, ready to rearrange. The clips are copies: changing the slices later does not change them.
- **Undo** any chopping step (choosing a way to chop, Auto-slice, moving, adding or removing slice points, changing a slice, changing the tempo or first beat, sending slices to a track). Slices are saved with the project, so a reopened project has the same chops.

The sound's audio file is never changed: slices are only marks and settings.

The work is split into small demos that the product owner checks one by one on the same branch (listed in `tasks.md`). At the end the product owner runs the exit test: chop a drum break into 16 slices, play them from the pads, and lay the slices out in the arrangement.

Not in this change: stretching sounds to the song tempo (warp modes, M4), so slice clips play at their own speed and a slice's pitch is not yet heard on its clip (it is kept and applied when M4 adds pitch to clips); fades on slices; snapping a slice point to the nearest hit in Beat mode; playing pads from a MIDI controller and recording pad playing (M5); sending slices to Session slots (M6); dragging a slice out to Explorer (M5); guessing a sound's category from the analysis (the import rule stays); macOS (TODO-MAC).

## Why

Chopping is the core of the product: the MVP is "import or capture -> chop -> arrange -> export". M1 to M2b give the sounds (imported and recorded), the arrangement and the transport; M3 adds the step in the middle. It also brings the onset and tempo analysis that M4's Beats warp mode needs. Design §2.4, §6.3 and §7 describe it; §15 sets the exit test.

## What Changes

- **Analysis**: an onset detector (spectral flux) and a tempo estimator (loop-length heuristic plus onset autocorrelation), run on a worker for each sound when it is imported or first opened. Results are derived data in the cache; only the user's tempo and first-beat corrections are saved in the project.
- **Slice model**: one slice set per sound in our part of the project file (mode, division, sensitivity, slice points in frames, per-slice pitch, gain, reverse and play mode), with commands for every change, each one undo step.
- **Sample editor view** in the main area: header (name, metadata, tempo fields, Audition, Back), slicing toolbar, slice strip, zoomable waveform with slice points, pad grid with banks, slice inspector, "Slices to new track".
- **Pad engine**: our own real-time pad player (16 voices, voice stealing, the three play modes) inside an always-on preview engine that plays alongside the project, fed by the UI through a lock-free queue.
- **Slice renders**: each slice's pitch, reverse and gain are baked into a buffer at the device sample rate on a worker; the previous buffer keeps playing until the new one is ready.
- **Keyboard**: a sample-editor scope for the pad keys, with key-up tracking, auto-repeat ignored and all pads off when focus leaves.
- **Entry points**: browser double-click and context menu, the clip panel's "Open in sample editor" (enabled).
- No **BREAKING** changes. Projects from M2b open unchanged; the schema version stays 1 (the slice set and the corrections are optional additions).

## Capabilities

### New Capabilities
- `audio-analysis`: finding the hits and the tempo of a sound, when it runs, how the user corrects tempo and first beat, and where the results live.
- `sample-editor`: the sample editor view: opening and leaving it, the header, the waveform and zoom, the three ways to chop, slice points, the slice strip, slice start/end, Audition, and sending slices to a new track.
- `slice-pads`: the 16 pads and their keys, banks, voices, play modes, per-slice pitch, gain and reverse, and how quickly changes are heard.

### Modified Capabilities
- `app-shell`: the main view shows the arrangement or the sample editor; the keyboard scopes gain the sample editor (pad keys).
- `clip-inspector`: "Open in sample editor" is enabled (fades stay disabled until M4).
- `sample-browser`: a sound opens in the sample editor by double-click or its menu.
- `undo-redo`: chopping edits are undo steps with their names; opening the editor, zooming, selecting a slice and playing pads are not.
- `project-bundle`: slice sets and tempo corrections are saved with the project; analysis results are cache data.

## Impact

- New `src/analysis/` (pure DSP: onset detector, tempo estimator, slicer grid and transient selection) and `src/render/` (slice renderer with Signalsmith Stretch and r8brain resampling, render scheduler), following the dependency direction in AGENTS.md.
- `src/model`: SliceSet and Slice wrappers, and correction properties on SOURCE.
- `src/engine`: `SlicePadPlugin` (real-time code, §5.1) and a pad engine facade (Tracktion-free header) hosting it in a never-saved preview edit.
- `src/commands`: slicing commands and `SlicesToTrack` (reuses the M2 placement and no-overlap code).
- `src/ui`: a `sampleeditor/` folder (view, waveform and slice points, slice strip, pad grid, slice inspector) and the KeyboardRouter's sample-editor scope; `src/app` view switching and entry points.
- New dependency: r8brain-free-src (pinned submodule, MIT), already planned in AGENTS.md for offline resampling. Signalsmith Stretch moves from benchmark-only to product code.
- Docs: `ROADMAP.md` (M3 `Change:` line, decisions), `docs/projects.md` (SLICESET), `docs/qa-checklist.md` (M3 section), `external/VERSIONS.md`, AGENTS.md and design §7.4 (the pad player runs in a preview edit, not on a hidden track of the project; see design.md D4).
