# Design

## Context

- See proposal.md for the why. The behaviour is in the specs (`audio-analysis`, `sample-editor`, `slice-pads` and the deltas).
- What exists after M2b: `model::SamplerState` / `Source` over the SAMPLER subtree (no SLICESET yet, schema 1); `CommandService` with one transaction per command, gestures (`begin` / `update` / `commit` / `cancel`) and 500 ms coalescing; `engine::Arrangement` (Tracktion-free facade: `createClip` with `ClipSpec{start beats, lengthSeconds, offsetSeconds, gainDb, reversed}`, tracks, tempo); `CreateClipsFromSources::place` with the no-overlap rule; `Audition`, which plays a file through its own preview `te::Edit` (M1 decision D-6), so it never touches the project edit; `TakeTapPlugin`, which showed how much work a non-saved plugin inside the project edit is (strip on save, keep it out of undo); `KeyboardRouter` (dialog > text field > shortcuts, transport auto-repeat filter); `ImportService` with its own `juce::ThreadPool`; `JobRegistry` for progress.
- `src/analysis/` and `src/render/` do not exist yet. Signalsmith Stretch and Linear are pinned and header-only (used only by the M0 benchmark). r8brain-free-src is planned in AGENTS.md but not added.
- Constraints (AGENTS.md): `analysis` and `render` have no Tracktion and no JUCE GUI; Tracktion only in `engine` and `commands`; every project change through `CommandService`; workers post results to the message thread; `SlicePadPlugin::applyToBuffer` is real-time code (§5.1).

## Goals / Non-Goals

**Goals:**
- Analysis, slicing math and slice rendering are pure functions on buffers, unit tested without an engine or a window.
- The preview the user sees while moving Sensitivity is computed by the same function the command uses, so preview and result never differ.
- Pads never touch the project edit: no hidden track to strip on save, nothing in undo, no effect of solo.
- Each demo leaves a working, tested app.

**Non-Goals:**
- Warp and pitch on clips (M4). A slice's pitch is stored on its clip (`sampler_pitch`) but not applied.
- MIDI input to the pads and recording pad playing (M5). The pad event format leaves room for a velocity and a MIDI source.
- A slice render cache on disk (`cache/slices/`): renders are in memory only until drag-out (M5) needs files.
- Category from analysis: the import rule (under 2 s = One-shot) stays.
- Tracktion's `TempoDetect` as a tie-breaker (§6.3): `analysis` must not link Tracktion. Added later only if the accuracy suite needs it.

## Decisions

### D1. Module layout
```
 analysis/  OnsetDetector, TempoEstimator, Slicer (beat grid, transient selection, limits, zero-crossing snap)
   ^         pure: juce_core, juce_audio_basics, juce_dsp (FFT); no Tracktion, no GUI
 render/    SliceRenderer (read region -> reverse -> Signalsmith pitch -> r8brain resample -> gain -> edge fades)
   ^         RenderScheduler (ThreadPool, latest-wins per slice, priorities)
 model/     SliceSet, Slice wrappers; SOURCE correction properties
 engine/    SlicePadPlugin (te::Plugin, real-time), PadEngine facade (Tracktion-free header)
 commands/  slicing commands, SetSourceTempo, MoveFirstBeat, SlicesToTrack
 io/        AnalysisService (reads the decoded WAV, runs analysis on a worker, cache file per sound)
 ui/sampleeditor/  SampleEditorView, WaveformView + SlicePointLayer, SliceStrip, PadGrid, SliceInspector, WaveformPeaks
 app/       view routing (Arrange <-> editor), entry points, wiring of AnalysisService, PadEngine, RenderScheduler
```
`render` links `analysis` (shared buffer helpers); `engine` links `render` (slice buffer type). This is the direction AGENTS.md already gives.

### D2. Analysis
- Input: the decoded WAV mixed to mono at its native rate. Onsets follow §6.3: STFT 1024 / hop 256, Hann, log-magnitude half-wave-rectified spectral flux, adaptive threshold `median(±8 hops) * k + δ` at the most permissive setting, peak picking with a 30 ms gap, refinement to the energy rise within ±10 ms, zero-crossing snap within 1 ms. Every candidate is stored with a strength normalised to 0..1.
- Sensitivity is a filter over the stored candidates: `selectTransients(onsets, sensitivity)` keeps candidates whose strength is at least `threshold(sensitivity)`, a monotonic mapping tuned on the fixtures (default 50). It is O(n), so the slider preview runs on the message thread at drag rate.
- Tempo: (1) loop heuristic over 1, 2, 4, 8 and 16 bars at 70 to 180 BPM (±0.5%); (2) autocorrelation of the onset-strength envelope over 60 to 200 BPM with comb scoring and penalties for octave errors; top 3 kept with confidences. First beat: first onset with strength above 0.5 inside the first beat period, else frame 0.
- Results go to `cache/analysis/<key>.json` (`algorithmVersion`, sample rate, frames, onsets, tempo candidates, first beat). Key = the source hash (first 16 hex chars) when it has one, else a hash of the decoded file's relative path, size and modification time (takes). A bump of `algorithmVersion` makes old cache files stale.
- `io::AnalysisService` runs jobs on its own pool (one thread, analysis is fast and must not slow imports), deduplicates requests per sound, and posts results to the message thread, where listeners (editor, RenderScheduler) are told. It never writes the edit (spec: analysis does not mark the project as unsaved).
- Triggers: after an import's decode finishes, after `AddTakes`, and when the editor opens a sound without a usable cache file.
- Alternative: store analysis in the SOURCE node (§9.2 as written). Rejected: writing it would dirty the project and need an undo-less write path; onset lists of long takes are large; the data is derived and recomputable. Only the user's decisions are document data (D3).

### D3. Model
```xml
<SOURCE id="src_3fa1c2d9" ... bpm="92.0" beatOffset="112">     <!-- present only when the user corrected them -->
  <SLICESET mode="beat" division="1/16" sensitivity="50" markers="29512 58912 88312">
    <SLICE pitch="0" gainDb="0.0" reverse="0" playMode="oneshot"/>   <!-- markers + 1 SLICE children -->
    ...
  </SLICESET>
</SOURCE>
```
- `markers` are frames at the source rate, strictly increasing, excluding 0 (the first slice always starts at 0, spec "Slices cover the whole sound"). A SOURCE without SLICESET is one slice with defaults, so nothing is written until the first chopping edit.
- Effective tempo and first beat = the SOURCE correction if present, else the analysis result. Beat slicing uses the effective values at the time of the command; the resulting markers are stored, so a later analysis change does not move them.
- Slice parameters on structural edits: adding a point splits a slice and both halves keep its settings; removing a point merges into the left slice's settings; Beat/Transient/Auto-slice and division changes reset every slice to defaults (simple and predictable; one undo restores them).
- Clips from slices carry `sampler_originSource`, `sampler_originSlice` (index at the time) and `sampler_pitch` on Tracktion's clip node, like `sampler_sourceId`.
- Schema stays 1: the additions are optional, older builds keep unknown children in the ValueTree.

### D4. Pads run in a separate preview edit
`engine::PadEngine` owns a `te::Edit` created for previewing (never saved, own undo manager unused), with one audio track whose plugin list holds one `SlicePadPlugin`. The edit's playback context stays allocated once it is made (when the first sound is opened in the editor: making it at start-up slowed the first window) and until the project closes, so the plugin processes blocks whether or not anything plays; spike 1.1 confirms that a plugin on a stopped edit gets `applyToBuffer` calls, otherwise the pad edit's transport is kept playing over a long empty range. Both edits play to the same device, which Tracktion mixes.
- Alternative: a hidden pad track in the project edit (design §7.4). Rejected: it would be saved and has to be stripped, could enter undo, is affected by solo, and appears in renders and golden tests (the M2b tap needed all of that work). Trade-off: pads do not pass through the project's master volume. M5, which records pad playing as MIDI, revisits this (pads as an instrument on a project track). AGENTS.md and design §7.4 are updated in task 4.x.
- The editor's Audition button keeps using `Audition` (whole sound at native speed). `app` enforces one audition at a time across the browser and the editor.

### D5. SlicePadPlugin (real-time)
- 16 voices, each `{slot, buffer ptr, position, gain ramp, state}`. Oldest voice stolen with a 3 ms fade; retriggering a slot fades its sounding voice over 3 ms and starts a new one; Gate and Loop release over 10 ms; Loop crossfades 3 ms at the wrap.
- UI -> audio: `AbstractFifo` of `PadEvent{on|off|allOff, slot, playMode, velocity}` (velocity 1.0 until M5).
- Slice buffers: `SliceBuffer` (ref-counted, interleaved stereo float at the device rate). The message thread publishes a new `SliceBank` (array of 256 buffer pointers + play modes) through a single-slot atomic exchange; the audio thread picks it up at block start and sends the old bank to a garbage FIFO that the message thread drains (§5.1: last release off the audio thread). Voices hold their buffer pointer, so a sounding slice keeps playing its old buffer (spec "does not cut it off").
- UI feedback: per-slot `std::atomic<uint8_t>` sounding counts read by the 60 Hz VBlank for the pad lights.
- Preallocated in `initialise`; debug `operator new` hook covers the callback.

### D6. Slice renders
- `render::SliceRenderer::render(file region, pitch, gainDb, reverse, outputRate) -> SliceBuffer`: read the region from the decoded WAV, reverse in place, Signalsmith Stretch for pitch with the length kept (input padded by the stretcher's latency and flushed, so the output starts on the slice's first sample), r8brain to the output rate when it differs, gain, 1 ms edge fades against clicks. Mono sources are rendered as dual mono.
- `render::RenderScheduler`: a pool with N-1 threads; one pending job per slice (a newer request replaces a queued one; a running one finishes and is discarded if stale by generation number); priority to the slice just played or selected. Finished buffers go to the message thread, which updates the `SliceBank`.
- On editor open all slices are rendered; on close the bank is cleared (pads exist only in the editor). A device sample-rate change re-renders everything.
- Budget: spike 1.1 measured 50 ms (release) for a 2 s stereo slice at +7 st (Signalsmith runs at about 40x real time), plus 21 ms for r8brain when the sound's rate differs from the device's, so the original 20 ms target was dropped; the spec's 100 ms is checked by a benchmark test (`perf` label). Workers render, and the previous buffer keeps playing until the new one is ready.
- Alternative: play slices straight from the source with real-time pitch shifting in the plugin. Rejected: real-time Signalsmith per voice costs CPU and latency and complicates the real-time code; renders are cheap.

### D7. Commands
- Model-only (no Tracktion): `SetSliceMode` (Beat / Transient / Manual; Beat and Transient compute markers with `analysis::Slicer` from the effective tempo or the cached onsets passed in by the caller), `AutoSlice`, `SetSliceDivision`, `AddSlicePoint`, `MoveSlicePoint` (gesture: `begin` on mouse-down, `update` per drag, `commit` on mouse-up), `RemoveSlicePoint`, `SetSliceBounds` (typed START/END), `SetSliceParam` (pitch / gain / reverse / play mode, `coalesceKey` = source + slice + param), `SetSourceTempo`, `MoveFirstBeat` (gesture). Hand edits in Beat or Transient mode set Manual in the same step.
- Names per the undo-redo delta. Each checks the limits (256 slices, 5 ms) itself and refuses (no step) otherwise; `findInvariantViolation` gains SLICESET checks (markers increasing, within the sound, at least 5 ms apart, SLICE count = markers + 1) for the debug post-condition.
- `SlicesToTrack` (arrangement): adds a track at the end named after the sound, then one `createClip` per slice: `offsetSeconds` = slice start (for a reversed slice `sourceSeconds - sliceEnd`, because the offset is into the reversed render), `lengthSeconds` = slice length, gain and reverse from the slice, start beat = previous clip end, converted through the tempo sequence. The new track is empty, so no overlap handling is needed.

### D8. Editor UI and view routing
- `MainComponent` keeps the `ArrangementView` alive and hidden while the editor shows (spec "Back keeps the view"). `UiState` gains `openSourceId`, and per open sound the zoom, scroll, selected slice, pad bank and START/END units; none are undoable.
- The editor listens to the SOURCE node (SLICESET and corrections) and repaints; undo that removes the open sound routes back to Arrange.
- Waveform: `ui::WaveformPeaks` built on a worker from the decoded WAV (min/max per 64 and per 1024 samples); below 64 samples per pixel the view reads samples through a memory-mapped reader and draws a line. Tiles are cached as images per zoom bucket like §6.2. `te::SmartThumbnail` is not used here because `ui` must not touch Tracktion and the editor needs deeper zoom than it offers.
- The slice strip, slice points and selection are hit-tested by plain logic classes (`SliceGeometry`) for unit tests.

### D9. Keyboard
`KeyboardRouter` gains a sample-editor scope: `Focus{dialogOpen, textFieldFocused, sampleEditorFocused}` and `Route::pads` for the 16 pad keys without Mod, Alt or Shift. Key-down comes from `keyPressed` (auto-repeat dropped by a held set), key-up from `keyStateChanged` plus `isKeyDown` for each held pad key (injected for tests). All held pads are released on focus loss, editor hide, app deactivation and project switch.

## Risks / Trade-offs

- [The pad edit does not process plugins while its transport is stopped] -> Spike 1.1 checks first; fallback keeps that transport playing over a long empty range.
- [Signalsmith has latency and smears short attacks] -> Pad and flush so the output lines up; measure transient smear on the kick fixture; pitch 0 skips the stretcher entirely.
- [Key-up events missed on Windows (focus change during a hold)] -> All-off on every focus loss; Gate/Loop are the only modes that depend on key-up.
- [Tempo estimation is wrong for non-loops] -> Typed tempo, ½ and ×2, movable first beat; the accuracy suite tracks regressions; Q13 asks the PO for 20 to 30 real files.
- [Memory for long sounds: rendering all slices of a 30-minute take costs about 0.7 GB at 48 kHz stereo] -> Render on demand beyond a 200 MB budget per sound (least recently played released first); a pad whose render is missing starts it and plays on the next hit.
- [Two edits mixed on one device cost CPU] -> The pad edit has one track and an idle plugin; measured in the spike.
- [Pads skip the project's master volume] -> Accepted for M3, revisited at M5 (D4).

## Migration Plan

None. Schema version stays 1; M2b projects open unchanged and not dirty (fixture test). An M2b build opening an M3 project keeps the SLICESET nodes because it keeps unknown children of SAMPLER; it shows no slices.

## Open Questions

- The exact sensitivity-to-threshold mapping and the default sensitivity: tuned on the fixtures in task 2.x.
- Whether the "1.1.1" marker also snaps to the nearest hit while dragged: decided in demo D1 with the PO, without spec change (snap within 20 ms, Alt to disable, if wanted).
