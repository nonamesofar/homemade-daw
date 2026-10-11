# Design

## Context

See proposal.md for motivation and specs/ for the required behaviour. State after M1 (on `main`):

- `engine` exposes a Tracktion-free `ProjectEdit` (SAMPLER tree, the edit's `UndoManager`, bundle folders, `save`/`saveAs`, `isDirty`, `prepareForPlayback`) and `EngineHost`. Tracktion is linked PRIVATE into `sampler_engine` only (`sampler_tracktion`), so CMake enforces "Tracktion only in `src/engine`".
- `commands` links only `sampler_model`: `CommandService::apply` opens one transaction per command, `ModelContext` holds the SAMPLER tree, the undo manager and the bundle folder. Debug invariant checks cover sources only. Commands: `AddSources`, `SetSourceCategory`.
- Dirty tracking in `ProjectEdit` is a change counter driven by a `ValueTree::Listener` on the SAMPLER node only, so arrangement edits would not mark the project changed.
- `ui/timeline` still holds the M0 spike `TimelineComponent` with its own plain model (`TimelineModel`) and a tested geometry header (`TimelineGeometry.h`: viewport, hit-testing, drop resolution, reorder index maths). `MainComponent` shows a placeholder main view.
- The M1 widget set (`ValueField`, `Knob`, `Fader`, `Meter`, `ToggleButton`, `SegmentedControl`, `Toast`) exists with tested value maths.
- Binding M0 findings: clips keep beat starts on tempo change; set the engine temp directory to `cache/` on every open; a play in the same message-loop turn as `ensureContextAllocated()` stops itself (play from a short timer); a reversed clip renders a proxy into `cache/` and is 0 bytes until done; save only through `saveEditAtomically`.

## Goals / Non-Goals

**Goals:**
- Keep the Tracktion boundary of M1: no Tracktion types outside `src/engine`, enforced by CMake.
- All arrangement logic that can be plain (overlap rule, split, duplicate and paste placement, snapping, hit-testing, selection mapping, position text, meter ballistics) lives in plain classes with Catch2 tests and no engine.
- Each demo (D1 to D8 in tasks.md) is built in a working state on one branch; later demos do not rework earlier ones.
- The time model of §5.3 stays intact so M4 (warp) and M10 (tempo map) need no model change.

**Non-Goals:**
- Warp, fades UI, clip loop, clip length past the sound's end (M4); follow-playhead, track height resize, adaptive snap (M7); session view (M6); export dialog (M4, but the offline render path built here is the one it will use); a full mixer panel (M6 strips).

## Decisions

### D-1 A Tracktion-free arrangement facade in `engine`
```
 ui ──const Arrangement&──> engine::Arrangement  (reads: tracks, clips, tempo, loop, ids)
 commands ──Arrangement&──> engine::Arrangement  (writes: primitive edits, through the edit's UndoManager)
 ui ──> engine::Transport, engine::LevelMeters   (not undoable: play/stop/locate, loop on, click on, meter levels)
                    │
                    └── te::Edit (private)
```
`engine::Arrangement` wraps the `te::Edit`. Its const methods return plain snapshots built from the edit on each call (`TrackInfo{id, name, colour, volumeDb, pan, mute, solo}`, `ClipInfo{id, trackId, sourceId, name, start, length, sourceOffsetSeconds, gainDb, reversed, missing}`, positions in beats) plus beat <-> seconds <-> bar.beat conversions through `te::TempoSequence`. Its non-const methods are primitive edits (add/remove/move track, set track property, create/remove clip, set clip position/offset/gain/reverse, set tempo, time signature, loop range), each a single change recorded into the edit's `UndoManager`. A change callback (a `ValueTree::Listener` on the edit state, coalesced with an `AsyncUpdater`) plus a generation counter tells the UI to repaint.
`sampler_commands` now links `sampler_engine` (the documented direction `engine <- commands`); `ModelContext` gains `engine::Arrangement* arrangement` (null in the M1 model-only tests). The UI only ever holds a `const Arrangement&`, so constness keeps the UI from editing (rule 2 of §4.2). The UI keeps no mirror: the only derived data it keeps is a per-track sorted clip index for culling, rebuilt when the generation changes.
Ids are Tracktion's `EditItemID`s rendered as strings; they survive save and reopen, so the selection and the clipboard hold ids, never pointers.
*Alternatives:* (a) link Tracktion into `commands`. Rejected: the UI needs a Tracktion-free read surface anyway, and the narrower boundary keeps CMake enforcement. (b) Commands edit Tracktion's XML nodes directly through the ValueTree. Rejected: it bypasses Tracktion's clip position logic and breaks on Tracktion upgrades.

### D-2 Edit planning as plain logic
`commands/EditPlanner` takes plain spans (`{clipId, trackIndex, start, length, sourceOffset}` per track) and an operation (place, move, trim, split, duplicate, paste) and returns a list of primitive operations (move clip, set bounds, split into two, remove, create). The commands read the spans from the `Arrangement`, run the planner, then execute its operations. The overlap rule of §8.3 (partly covered: shorten; fully covered: remove; landing inside: split around) lives only here, so it is tested exhaustively without an engine, and the debug invariant check (no overlap, regions within source bounds, unique ids) runs after every command against the real edit.
The planner works in beats; source offsets are in seconds of the source (clips play at native speed until M4) and conversions go through a `TimeMap` pair of functions handed in by the command, so a tempo map (M10) changes nothing here. Trimming the left edge by Δ moves the source offset by `seconds(Δ)`.
*Alternative:* resolve overlaps inside the engine facade. Rejected: harder to test, and the facade should stay a thin set of primitives.

### D-3 Commands and transactions
New commands: `CreateClipsFromSources`, `ImportAndPlace` (sources plus clips in one step), `MoveClips`, `TrimClip`, `SplitClips`, `DuplicateClips`, `DeleteClips`, `PasteClips`, `SetClipParams` (gain, start, reverse), `AddTrack`, `DeleteTracks`, `ReorderTrack`, `RenameTrack`, `SetTrackColour`, `SetTrackParam` (volume, pan, mute, solo, exclusive solo), `SetLoop`, `SetTempo`, `SetTimeSig`. Step names as in the undo-redo spec.
`CommandService` gains gestures: `begin(name)` opens a transaction, `update(cmd)` applies within it (fader, pan, tempo and gain drags are heard while dragging), `commit()` closes it, `cancel()` undoes the open transaction (Escape). Clip drags, trims and loop-bar drags do not use gestures: they paint a preview and apply one command on mouse-up (§4.2 rule 2). Coalescing: `SetTrackParam` carries a key `(track, control)`; when the previous command has the same key and was applied less than 500 ms earlier with nothing in between, no new transaction is opened.
Tempo change: `SetTempo` sets the tempo, then reads the clips and lets the planner shorten clips that now run into the next one, in the same transaction (transport spec). Whether Tracktion keeps a native-speed clip's length in seconds or in beats when the tempo changes is checked by a test in task 1.2; if it keeps beats, the command resets the lengths to their seconds in the same transaction.

### D-4 Dirty state from the undo history
Dirty tracking moves from the SAMPLER `ValueTree` listener to the undo history: `CommandService` keeps a list of transaction serial numbers matching the undo stack and exposes `historyToken()` (the serial of the current position). The project is dirty when the token differs from the one recorded at the last save. That gives exactly the project-bundle spec: every undoable edit, undo and redo dirties; play, locate, selection, zoom, loop on/off, click on/off and snap (all non-undoable) do not; undo after save dirties (M1 spec). `ProjectEdit::isDirty` is replaced by `AppController::isDirty`; M1's tests move with it.
*Alternative:* widen the listener to the whole edit with an ignore list. Rejected: Tracktion writes many properties that are not user edits (transport position, thumbnails, plugin state, view state), and the list would rot.

### D-5 Transport, loop and metronome
`engine::Transport` wraps `te::TransportControl` (play, stop, locate, position, isPlaying, looping on/off, click on/off) and is not undoable. The edit's playback context is allocated when the project opens, and Play is issued from a short timer if the context was just allocated (M0 finding). Stop-twice and Home/Enter logic is a plain `TransportLogic` class.
Loop range: `SetLoop` writes the range through `Arrangement::setLoopRange` into the edit's transport state with the `UndoManager`. Task 1.2 checks that Tracktion picks up an undo of that property; if it does not, the range is stored in a `SAMPLER/ARRANGE` node (undoable) and pushed to the transport on change.
Metronome: Tracktion's click (`clickTrackEnabled`), accent on beat 1 from the time signature. The render path leaves the click out.
Snap value and loop/click on-off are saved but not undoable and do not dirty: snap lives in a `SAMPLER/UI` node written without the undo manager; loop and click flags in Tracktion's transport state.

### D-6 Mixer and meters
Volume and pan are the parameters of each track's `VolumeAndPanPlugin`; mute and solo are Tracktion track properties (Tracktion does the solo logic; exclusive solo is one `SetTrackParam` that writes all tracks). Master volume is the master `VolumeAndPanPlugin`. `engine::LevelMeters` attaches a `te::LevelMeasurer` client per track (post-fader, Tracktion's default plugin order) and for the master, and returns peak and RMS in dB per channel; the UI polls it at 60 Hz in the `VBlankAttachment` that also draws the playhead. Decay (20 dB/s), 1.5 s hold, 300 ms RMS and the latched over light are a plain `MeterBallistics` class; the over flag comes from the master measurer's clip detection.
Header controls reuse M1 widgets: dB readout = `ValueField` with a dB mapping (-inf..+6, detent at 0, double-click reset), pan = `Knob` with centre detent, meter = `Meter`.

### D-7 UI structure and plain logic
```
MainComponent
 ├─ HeaderBar: ViewToggle, TransportBar(Stop, Play, Record disabled), TempoField, TimeSigField, PositionDisplay,
 │             ClickToggle, LoopToggle, SnapMenu, MasterStrip(volume, meter, over light), AudioStatus
 ├─ BrowserPanel (M1)
 ├─ ArrangementView
 │    ├─ TrackHeaderList (headers: colour, name editor, M, S, R disabled, dB, pan, meter; drag-reorder)
 │    ├─ TimelineRuler + LoopBar
 │    ├─ TimelineLanes (grid, clips, waveforms; DragAndDropTarget for "sample:<id>", file drops)
 │    ├─ SelectionOverlay (marquee, drag ghost, snap guide)   PlayheadOverlay (2 px dirty strip)
 │    └─ ScrollBars (h, v)
 └─ DetailPanel: ClipInspector (name, track, bars, waveform, GAIN, START, Reverse, disabled fades and editor button)
```
Plain classes (in `ui`, no painting): `ArrangeGeometry` (grows from `TimelineGeometry.h`: viewport with vertical scroll and content length rule, hit-testing body/left edge/right edge, marquee intersection, drop resolution including "below the last lane", reorder index), `SnapService` (grid step from snap value and time signature, triplets, Alt bypass), `SelectionModel` (clip ids, selected track, time selection, mapping after undo), `Clipboard` (clip records relative to the earliest clip and the top track), `PositionText` (format and parse bar.beat.sixteenth), `TransportLogic`, `MeterBallistics`, `KeyboardRouter`. The M0 `TimelineComponent` and its plain model are deleted in D1; its performance test is rewritten against `TimelineLanes` with 200 clips.
Waveforms use `SmartThumbnail` from `EngineHost::createThumbnail` (M1), one per source shared by all its clips, drawn for the clip's region (reversed when the clip is). Clips are culled by a binary search on the per-track sorted index.

### D-8 Drops and placement
Browser rows start a JUCE drag with `"sample:<sourceId>"`; `TimelineLanes` shows a ghost of the sound's length at the snapped beat and applies `CreateClipsFromSources` (with `newTrack` when below the last lane). Explorer files dropped on the lanes go to `ImportService` with a placement target `{trackId | newTrack, beat}`; the import job carries it and, when the batch is ready, the message thread applies one `ImportAndPlace`, so the import and the clips are one undo step. Placeholders while importing are UI state from `JobRegistry` (they carry the target), never model. Files dropped on the browser keep the M1 behaviour.

### D-9 Keyboard router
`KeyboardRouter` keeps a scope stack (dialog > text input > view > global; the sample-editor scope arrives in M3). `MainComponent` routes `keyPressed` through it: when a `TextEditor` has focus or a modal dialog is open, unmodified keys (Space, M, S, Z, L, Delete, Home, Enter) are left to them; otherwise view shortcuts go to the focused view and the rest to the `ApplicationCommandManager`, which also drives the menus (one `MenuBarModel`, "Mod" shortcuts). Auto-repeat is ignored for transport keys.

### D-10 Clip details
New clips get `sampler_sourceId` (the M0-verified custom property path), a name from the source, and Tracktion fade-in/fade-out of 3 ms as the declick; the inspector's fade fields show the user fade (0 ms), and M4 adds user fades on top. Gain is `setGainDB`; reverse is Tracktion's `setIsReversed`, which renders a proxy into `cache/`; while it renders, the clip is drawn hatched with "rendering" and is silent. A clip whose file is missing on open is kept and drawn as missing; the open reports the missing files in one toast.

### D-11 Offline render and golden tests
`engine::OfflineRender::renderToWav(ProjectEdit&, file, sampleRate, blockSize)` uses `te::Renderer` from bar 1 to the end of the last clip, without the click, after waiting for Tracktion's proxy jobs (reverse). M4 export reuses it. `EngineHost` gets a headless option (no audio device opened) used by the tool and the tests.
`tools/sampler-cli` (`sampler-cli render <bundle> <out.wav> [--rate 48000] [--block 512]`, exit code 0 or 1 with a message) wraps it.
Golden projects are not committed as bundles: `tests/golden/GoldenProjects.cpp` builds each one at test time into a temp folder from the committed audio fixtures, through the real `CommandService` and commands, so the cases stay readable as code and also exercise the commands. References are `tests/golden/<case>.wav` (48 kHz, block 512, Git LFS rule already present). Comparison: same length, peak difference at or below -90 dBFS (not bit-exact, so the debug and release CI jobs share one set of references). `SAMPLER_UPDATE_GOLDEN=1` rewrites the references; the README there says to listen before committing. One extra ctest runs the `sampler-cli` executable on one generated project and compares its file, so the tool itself is covered.
Cases: four tracks with clips; overlap trim; split; left trim; clip gain; reverse; volume and pan; mute and solo; master volume; tempo change.

### D-12 Projects from M1 and defaults
A project without tracks gets one audio track "Audio 1" on open, written without the undo manager and before the history token is recorded, so the project is not dirty and its file is not rewritten until the user saves. Defaults (120 BPM, 4/4, no loop range, 1-bar snap) come from Tracktion's new-edit defaults plus the `SAMPLER/UI` default. Schema version stays 1: everything added is optional and an M1 build would ignore it.

## Risks / Trade-offs

- [Tracktion's loop range is not undoable through the edit's `UndoManager`] → Checked in task 1.2; fallback in D-5 (our own undoable node pushed to the transport).
- [Tracktion changes a native-speed clip's length in beats, not seconds, on tempo change] → Checked in task 1.2; `SetTempo` corrects lengths in the same transaction (D-3).
- [The 8 ms frame budget with 200 clips and waveforms is missed] → Profile in D3 with the rewritten performance test; cull by binary search, draw thumbnails per visible region only; last resort an `OpenGLContext` on the lanes only (§10.4).
- [Reverse proxies are silent while they render, so a reversed clip may start silent] → Visible "rendering" state; the golden render waits for proxy jobs; listening check in the demo.
- [Golden renders differ between machines or builds] → Tolerance comparison and both CI presets on the same references; only plugin-free projects; fixed rate and block size.
- [Gesture transactions and coalescing interact with undo in surprising ways (undo during a drag)] → Undo and redo are disabled while a gesture is open; property tests mix gestures, coalesced commands and undo.
- [Alt on Windows can focus the menu bar] → The menu bar is a JUCE component in the window, not a native menu; the demo checks Alt-drag.
- [Moving dirty tracking breaks M1 behaviour] → M1's dirty tests are kept and re-pointed at the new token before the switch (task 1.6).

## Migration Plan

No user data needs converting: M1 projects open as they are (D-12). Inside the repo, the M0 `TimelineComponent` is deleted in D1 in the same commit that brings in its replacement, so the build and tests stay green. CI runs on every push to the branch. Rollback is reverting the branch.

## Open Questions

- Exact meter layout in the 112 px header (vertical bar at the right edge or a thin horizontal bar under the name): decided when the header is matched to the mock in D5.
- Whether the triplet values need 1/32T as well: add it if the PO asks during D2; the snap code handles any grid step.
