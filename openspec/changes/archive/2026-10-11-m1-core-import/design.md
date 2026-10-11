# Design

## Context

See proposal.md for motivation and specs/ for the required behaviour. Current state after M0:

- `src/engine` holds `EngineHost` (engine + device, plus spike-era helpers: `loadFile`, `play`, `renderToWav`, `probeDecode`), `EngineSetup` (`makeEngine`, `saveEditAtomically`) and the M0 test helpers `EditLab` / `PluginLab`. Tracktion is linked PRIVATE into `sampler_engine` only, so CMake already enforces "Tracktion only in `src/engine`".
- `src/io` has `AtomicFile`; `src/ui` has the M0 `TimelineComponent` (kept for M2); `src/platform/windows` has capture code (kept for M2b) and `MediaFoundationProbe` (M4A descoped).
- `apps/` holds the spike GUI, three probes, the stretch benchmark and the VST3 test plugin. Tests link `EditLab`/`PluginLab` and the test plugin.
- `src/model`, `src/commands` and `src/app` do not exist yet. `openspec/specs/project-bundle` covers relative paths, cache redirection and atomic saves (all from M0).
- Binding M0 findings: the edit file must exist before relative references are made; set the engine temp directory to `cache/` on every open; wait for the engine's start-up device scan before playing; the engine does not trim MP3 encoder delay; save only through `saveEditAtomically`.

## Goals / Non-Goals

**Goals:**
- A layering that M2 to M5 can extend without moving things again: model / commands / io / ui / app on top of a Tracktion-free engine interface.
- Each demo (D1 to D7 in tasks.md) is built in a working state on one branch; nothing in a later demo should force rework of an earlier one.
- Every behaviour in the specs is covered by an automated test, except look and audibility, which the demo scripts cover.

**Non-Goals:**
- Clips, tracks, transport UI (M2); analysis, BPM, real classification (M3); autosave, lock file and crash recovery (M4); global sample library; `KeyboardRouter` scopes (no shortcuts beyond the menu ones in M1); "clean up unused media".

## Decisions

### D-1 Layering and the Tracktion boundary
```
 app ──> ui ──> io ──> commands ──> engine ──> (Tracktion, private)
                  \        \           ^
                   \        └──> model ┘   model = ValueTree wrappers, no Tracktion
                    └──> model
```
`engine` exposes a `ProjectEdit` whose public interface is Tracktion-free: `juce::ValueTree samplerState()`, `juce::UndoManager& undoManager()`, `juce::File bundleDir()`, `save()`, `saveAs(dir)`, `isDirty()`. Commands edit the `SAMPLER` subtree through `model` wrappers with that `UndoManager`, so `commands` does not need to link Tracktion in M1. When M2 needs clip edits, the Tracktion-touching part of a command goes into an engine-side helper called from the command. This keeps the existing CMake enforcement instead of widening Tracktion to `commands`.
*Alternative:* link Tracktion into `commands` now (AGENTS.md allows it). Rejected for M1: nothing in M1 needs it, and the narrower boundary is easier to keep than to restore.

### D-2 Commands and undo
`CommandService::apply(Command&)` calls `undoManager.beginNewTransaction(cmd.name())`, runs `cmd.perform(ModelContext&)`, then in debug builds runs invariant checks (unique source ids, every `original`/`decoded` path relative and inside the bundle, schema version present). `undo()` / `redo()` / `canUndo()` / `undoName()` live on the service; the UI never touches the `UndoManager`. Commands in M1: `AddSources{list}` (one transaction for a whole drop) and `SetSourceCategory{id, category}`.
Dirty state is a change counter incremented by a `ValueTree::Listener` on the edit state, compared with the counter at the last save. So an undo after a save makes the project dirty, matching the spec.
*Alternative:* Tracktion's own changed flag. Rejected: it reacts to things we do not count as user edits (plugin state, view properties) and is harder to test without an engine.

### D-3 Import pipeline: workers prepare, one command commits
```
 drop/menu ──> ImportService.import(files)            (message thread)
                 │  JobRegistry: "Importing 3 files" (UI rows show "importing")
                 v
              worker (juce::ThreadPool)
                 per file: extension check -> open reader -> length <= 30 min
                           -> SHA-256 of bytes -> already in project? (dedupe)
                           -> copy to audio/originals/<name>-<hash8>.<ext>   (tmp + rename)
                           -> decode to audio/<name>-<hash8>.wav, 32f, native rate,
                              MP3 trim, >2 ch downmix                         (tmp + rename)
                 on failure: delete this file's partial outputs, record error
                 v
              callAsync ──> CommandService.apply(AddSources{ready files})   one undo step
                        ──> toasts for errors / downmix / duplicates (select existing row)
```
Nothing touches the model until the batch is ready, so an undo can never meet a half-imported sound and "importing" rows are UI state only (`JobRegistry`), not model. Files stay on disk after undo (redo is instant; the same hash is reused on re-import); "clean up unused media" is a later feature.
Source ids are `src_<hash8>`, extended with more hash characters on a prefix collision. Paths in the `SOURCE` node are relative to the bundle. SHA-256 comes from `juce::SHA256` (no new dependency; BLAKE3 can replace it later behind the same `hash="sha256:..."` prefix).
Decoding uses our own `AudioFormatManager` that registers exactly WAV, AIFF, FLAC, Ogg Vorbis and JUCE's MP3 format, not `registerBasicFormats()` (on Windows that also registers the Windows Media reader, which would make results depend on the OS codecs).

### D-4 MP3 gapless trim
A small parser reads the first MP3 frame: if it is a Xing/Info frame with a LAME extension, it takes the 12-bit encoder delay and padding; otherwise there is no tag. The decoded frame count is then cut as `start = delay + decoderDelay`, `end = padding - decoderDelay` (tagless: start 1105). The design doc's `529 + 1` is the decoder delay of an mpg123-style decoder; JUCE's MP3 decoder may differ, and it may or may not emit the Info frame as 1152 frames of silence. **The constant is calibrated in a test**: the fixture MP3 is encoded by LAME from a known WAV, and the test finds the offset by cross-correlation and checks the length within ±1 frame. The calibrated value is written down in `docs/m0-findings.md` (row on MP3 delay) and in a code comment.

### D-5 Bundle lifecycle, untitled projects and Save As
- **New:** create `userApplicationDataDirectory/Sampler/untitled/<uuid>.sdaw/`, create the edit and save it at once (M0: relative references need the edit file on disk), set the engine temp directory to its `cache/`.
- **Open:** read `project.tracktionedit` as XML first and check `SAMPLER@schemaVersion` *before* handing it to Tracktion, so a newer file is refused without Tracktion upgrading or touching it. A missing `SAMPLER` node is created as an empty v1 (the M0 test edits have none). `migrate_vN_to_vN1` chain is set up with no steps yet (v1 is the first version).
- **Save:** `saveEditAtomically` only.
- **Save As:** copy `project.tracktionedit` and `audio/` (not `cache/`, not `autosave/`) into the new folder, point the edit's file retriever at the new edit file, save atomically, then switch the cache directory. For an untitled project the folder is moved instead (rename when on the same volume, else copy then delete). The edit object and its undo history stay alive, so Save As keeps undo, matching the spec.
- **Discard untitled:** delete its folder. Untitled folders left behind by a crash are kept (M4 recovery may use them).
*Alternative:* ask for a location on New (no untitled state). Rejected: "start the app and drop a file" is the D1 demo, and DAWs commonly allow it.

### D-6 Audition through a preview edit
`engine/Audition` uses Tracktion's `Edit::createEditForPreviewingFile` on the decoded WAV, with its own playback context, so it plays at native speed and pitch regardless of project tempo, and never touches the project edit or its undo history. End of playback is detected by the UI's 30 Hz poll of the audition state (no audio-thread callbacks). In M3 audition moves onto the pad track's `SlicePadPlugin` (§7.4); the `Audition` interface (`play(sourceId)`, `stop()`, `playingId()`) stays.
**Start-up queue:** `EngineHost` reports `isAudioReady()` (an output device is open and the engine's start-up device scan has finished) and fires `onAudioReady` once. A plain class `AuditionRequestQueue` holds at most one pending request: a new click replaces it, a second click on the same row clears it. On ready it plays the pending request. If the engine is not ready after 10 s, the request is dropped and a toast says no audio output is available. A developer flag `--audio-start-delay <ms>` holds back the ready signal so the demo can show the queue.
*Alternative:* play through a hidden track of the project edit. Rejected for M1: it would put audition changes into the project's undo history and dirty flag unless carefully excluded.

### D-7 Browser and thumbnails
`BrowserModel` (plain class, in `ui`, no Components) holds the list of `SourceInfo` rows built from the `SAMPLER` tree, plus the search text and the category place; it returns the filtered, name-sorted rows. It is rebuilt on `ValueTree` changes; filtering 1,000 rows by substring is microseconds, so no index is needed (S1 calls it `LibraryIndex`; the global index is later).
`BrowserPanel` is a virtualised `juce::ListBox`. Thumbnails are Tracktion `SmartThumbnail`s from `EngineHost::createThumbnail` (exists from M0), created lazily for visible rows and kept in an LRU of 64; their disk cache lands in `cache/` because the engine temp directory points there (M0 finding).

### D-8 Look and feel, fonts, widgets
`SamplerLookAndFeel : LookAndFeel_V4` with the colour tokens from design §10.2; IBM Plex Sans and Mono (OFL) as `juce_add_binary_data` in a `sampler_resources` target. The §10.2 widget set (`Knob`, `Fader`, `Meter`, `Pad`, `ToggleButton`, `SegmentedControl`, `ValueField`, `Toast`) is built in D7 and shown in a developer "Widget gallery" window, so widgets that M1 does not use yet can still be demoed. `Toast` and the progress indicator exist in a plain form from D4 and are restyled in D7.

### D-9 Fixture generator and LAME
`tools/make-fixtures` (console app) synthesises with a seeded RNG: a 2-bar 120 BPM drum loop with sharp onsets, a 0.4 s kick, a 48 kHz mono bass phrase, a pad chord, a 60 s evolving texture, a 5-channel tone, plus a corrupt `.wav` and a `.txt`. It writes WAV/AIFF/FLAC/Ogg with JUCE writers and MP3 with libmp3lame (one file with a LAME tag, one with the tag disabled). Output goes to `tests/audio-fixtures/` (Git LFS rule added), with a `README.md` listing each file's purpose. Files over 30 minutes and the 10-minute "slow import" file are produced at test or demo time into a temp folder (`make-fixtures --long <dir>`), never committed.
LAME 3.100 comes in as a pinned submodule `external/lame` (from a Git mirror of the official release; the task checks its sources against the official `lame-3.100.tar.gz` checksum) with our own `cmake/SamplerLame.cmake` building `libmp3lame` static, no `/WX`, recorded in `external/VERSIONS.md`. M4's `Mp3Writer` reuses it.
*Alternatives:* a Python script with `lameenc` (adds a Python toolchain only for this); a manually downloaded `lame.exe` (not reproducible). Both rejected (discussed with the user).

### D-10 Clean-up placement
| Item | Where it goes | When |
|---|---|---|
| `apps/testplugin` | `tests/testplugin` | D1 start |
| `apps/probes`, `apps/benchmarks` | `tools/probes`, `tools/benchmarks` | D1 start |
| `MediaFoundationProbe` | into `tools/probes` (only `DecodeProbe` uses it) | D1 start |
| `EditLab`, `PluginLab` | `src/engine/testkit`, target `sampler_engine_testkit`, linked only by tests | D1 start |
| Spike-only `EngineHost` methods | removed, or moved to the testkit if a test uses them | when `ProjectEdit`/`Audition` replace them (D1) |
| `apps/spike` and `apps/` | deleted | end of D1, once the app plays a sound |
| README, `docs/building.md`, AGENTS.md layout, stale ROADMAP M0 path | updated | with the move that makes them stale |
Moving first means every later task lands in the final layout.

## Risks / Trade-offs

- [Two Tracktion edits playing at once (preview edit + project edit) may not mix on one device] → Check it in D1 with an automated test that plays audition while the project edit has a playback context. Fallback: attach only the preview edit's context while auditioning in M1 (no project playback exists until M2), and revisit when M3 moves audition to the pad track.
- [JUCE's MP3 decoder delay is unknown] → Calibrated by test against a LAME-encoded fixture (D-4); a wrong constant fails the test rather than the user's loops.
- [LAME has no official Git repository] → Mirror pinned to the 3.100 release commit and checked against the official tarball checksum; documented in `VERSIONS.md`.
- [SHA-256 of a large file is slow in a debug build] → It runs on a worker with progress; 30 minutes of stereo 32f is about 600 MB, worst case a few seconds in release.
- [Save As by copying `audio/` duplicates disk use] → Expected for "independent copy"; untitled projects move instead.
- [Untitled folders pile up after crashes] → Kept on purpose for M4 recovery; M4 adds the clean-up rule.
- [The browser's undo shortcut conflicts with text editing] → While the search box has focus, Mod+Z edits the search text (standard text field behaviour); the Edit menu still undoes the project.
- [Fixture files change when a dependency changes its encoder] → The regeneration test compares decoded samples, not bytes, and the committed files are the reference.

## Migration Plan

No user data exists yet. Within the repo: moves happen at the start of D1 in one commit so the build and tests stay green across it; CI runs on every push to the branch. Rollback is reverting the branch.

## Open Questions

- Exact IBM Plex weights to embed (Regular/Medium/SemiBold are likely enough): decided when the mock is matched in D7.
- Whether the LRU size of 64 thumbnails is right: tuned in D5 by watching memory with 1,000 sounds.
