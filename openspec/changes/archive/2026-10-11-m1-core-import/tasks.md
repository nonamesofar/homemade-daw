# Tasks

M1 is built as seven demos on one branch (`change/m1-core-import`), checked by the PO one at a time, with one PR at the end. Each group ends with a demo script and a PO acceptance task. Build: `scripts/build.ps1` or `cmake --build --preset windows-msvc-debug`; tests: `ctest --preset windows-msvc-debug -LE hardware`.

## 1. D1: Drop a WAV, see it in a list, click to hear it

Clean-up first, so everything after lands in the final layout (design D-10).

- [x] 1.1 Move `apps/testplugin` to `tests/testplugin`, `apps/probes` and `apps/benchmarks` to `tools/probes` and `tools/benchmarks`, and `src/platform/windows/MediaFoundationProbe.*` into `tools/probes`; fix the CMake paths. Verify: debug and release presets build with no warnings and all existing tests pass
- [x] 1.2 Move `EditLab` and `PluginLab` into `src/engine/testkit` as target `sampler_engine_testkit`, linked only by the test executables. Verify: `sampler_engine` no longer contains them (check the target sources) and the engine tests pass
- [x] 1.3 Add LAME 3.100 as submodule `external/lame` (Git mirror pinned to the 3.100 release; compare its `libmp3lame` sources with the official `lame-3.100.tar.gz`), add `cmake/SamplerLame.cmake` building `libmp3lame` static without `/WX`, and record it in `external/VERSIONS.md`. Verify: a smoke test encodes one second of sine to MP3 and JUCE decodes it
- [x] 1.4 Write `tools/make-fixtures` (seeded synthesis, design D-9) writing `tests/audio-fixtures/`: drum loop (WAV, AIFF, MP3 with LAME tag, MP3 without tag), kick one-shot (mono WAV), bass phrase (48 kHz mono FLAC), pad chord (OGG), 60 s texture (FLAC), 5-channel WAV, corrupt `.wav`, `.txt`; plus `--long <dir>` for the 10-minute and 31-minute files. Add the Git LFS rule for `tests/audio-fixtures/**` and a `README.md` there listing each file's purpose. Commit the files. Verify: a test regenerates into a temp folder and checks every file decodes to the same samples as the committed one
- [x] 1.5 Create `src/model` (target `sampler_model`): `SamplerState` (schema version 1), `Source` wrapper (id, name, original, decoded, originalPath, hash, sampleRate, channels, frames, category), id creation from the hash with collision extension. Verify: unit tests for wrappers, defaults and id collisions
- [x] 1.6 Add `engine/ProjectEdit` (design D-1, D-5, untitled part only): creates the untitled bundle in the app data folder, saves the edit at once, sets the engine temp directory to its `cache/`, exposes the `SAMPLER` tree, `UndoManager`, bundle folder and dirty state, all Tracktion-free in its header. Add `EngineHost::isAudioReady()` / `onAudioReady`. Verify: engine tests create a project, see the `SAMPLER` node with schema version 1 and the bundle folders, and see audio-ready fire (hardware label)
- [x] 1.7 Create `src/commands` (target `sampler_commands`): `CommandService` with `apply`, `undo`, `redo`, `canUndo`/`canRedo`, step names, debug invariant checks (design D-2), and `AddSources`. Verify: unit tests apply `AddSources`, check one transaction per apply, and that a broken invariant (duplicate id) trips the debug check
- [x] 1.8 Add `io/ImportService` for WAV only: worker job (probe, SHA-256, dedupe, copy to `audio/originals/` and decode to `audio/<name>-<hash8>.wav` 32f, both via temp file + rename), then one `AddSources` on the message thread; `JobRegistry` for pending rows. Verify: tests import the drum-loop WAV into a temp project and check files, the `SOURCE` node, relative paths and that the user's file hash is unchanged; importing it twice adds one source
- [x] 1.9 Add `engine/Audition` on a preview edit (design D-6) and the plain `AuditionRequestQueue` (latest click wins, second click on the same row cancels, 10 s timeout). Verify: unit tests for the queue rules; an engine test (hardware label) plays a source while the project edit is attached and checks it reaches the end. If two edits cannot play together, apply the fallback in design Risks and note it in the ROADMAP decisions log
- [x] 1.10 Create `src/app` (the `Sampler` GUI app): `Main.cpp`, `MainWindow`, `MainComponent` with a plain header (logo text, audio status "starting..." then device rate/latency), a plain browser list of source names with a playing indicator, the "Drop audio here" hint, OS file drop and File > Import Audio, and the developer flag `--audio-start-delay <ms>`. Verify: the app builds; a headless component test drops a fixture path on `MainComponent` and sees the row appear
- [x] 1.11 Delete `apps/spike` and the `apps/` folder; remove `EngineHost` methods only the spike used (move any still used by tests into the testkit); update `README.md`, `docs/building.md` (app path), AGENTS.md source layout (`tools/`, `tests/testplugin`) and the stale M0 path in `ROADMAP.md`. Verify: clean configure and build of both presets, all tests pass, CI green on the branch
- [x] 1.12 PO demo D1 accepted (script below)

**Demo D1 script:** start `Sampler.exe` (path in `docs/building.md`). A dark-grey window shows a list area with "Drop audio here". Drag `tests/audio-fixtures/drums-loop-120.wav` from Explorer onto the window: a row "drums-loop-120" appears. Click it: the loop plays once; click again while it plays: it stops. Drop the same file again: still one row. Then quit, start `Sampler.exe --audio-start-delay 3000`, drop the file and click the row at once while the header says "starting...": nothing plays, then about 3 s later the loop plays by itself.

## 2. D2: Undo and redo

- [x] 2.1 Register app commands with `ApplicationCommandManager` and one `MenuBarModel`: Edit > Undo / Redo (Mod+Z, Mod+Shift+Z, Mod+Y) showing the step name, disabled when there is nothing to undo or redo. Verify: component test triggers the commands and checks enabled state and item names
- [x] 2.2 Multi-file import as one step, step names ("Import kick.wav", "Import 3 files"). Verify: test drops three fixtures and one undo removes all three
- [x] 2.3 Property test for commands: random sequences of `AddSources` / undo / redo on a model with no engine; after each step the invariants hold and `undo(apply(x))` leaves the serialised `SAMPLER` tree equal to before. Verify: test runs 500 random sequences in CI
- [x] 2.4 PO demo D2 accepted (script below)

**Demo D2 script:** drop `drums-loop-120.wav`, then select three more fixtures and drop them together. Edit menu shows "Undo Import 3 files". Press Ctrl+Z: the three disappear, the loop stays. Ctrl+Y (or Ctrl+Shift+Z): they are back and play. Ctrl+Z twice: the list is empty; Redo is available; drop a new file: Redo is greyed out.

## 3. D3: Save, quit, reopen

- [x] 3.1 `ProjectEdit` open (design D-5): pre-parse the XML, check `SAMPLER@schemaVersion`, refuse a newer version without touching the file, create an empty v1 `SAMPLER` node when missing, set the cache directory; report missing or invalid project files. Add the empty `migrate_vN_to_vN1` chain. Verify: tests open a v1 fixture, an M0-style edit without `SAMPLER`, a schema-99 fixture (refused, file hash unchanged) and a folder without a project file
- [x] 3.2 Save (atomic, through `saveEditAtomically`) and Save As (copy `project.tracktionedit` + `audio/`, retarget the edit file, keep undo; move for untitled projects); discard deletes an untitled folder. Verify: tests for save and reopen with three sources, Save As independence (import after Save As changes only the new copy), untitled move, undo still available after Save As, original file hashes unchanged
- [x] 3.3 Dirty tracking (change counter vs. last save) and window title "Name - Sampler" with an unsaved marker. Verify: tests that import makes the project dirty, save clears it, undo after save sets it again, audition does not
- [x] 3.4 File menu New / Open / Save / Save As / Quit with Mod+N / Mod+O / Mod+S / Mod+Shift+S, and the Save / Discard / Cancel prompt before Quit, New and Open when dirty. Verify: component tests drive each path with a stubbed prompt (cancel keeps the project; save then quit saves)
- [x] 3.5 Write `docs/projects.md`: the bundle layout, where untitled projects live, what Save As copies, and that `cache/` can be deleted. Verify: following its steps in the app gives the folders it describes
- [x] 3.6 PO demo D3 accepted (script below)

**Demo D3 script:** start the app (title "Untitled"), drop two fixtures, title shows the unsaved marker. Ctrl+S: choose Desktop, name "Demo1". Title "Demo1", no marker. Look at `Demo1.sdaw` in Explorer: `project.tracktionedit`, `audio/originals/` with the two files, `audio/` with two WAVs, `cache/`. Quit, start again, Ctrl+O `Demo1.sdaw`: both rows are back and play. Drop a third file and try to quit: the app asks Save / Discard / Cancel; Cancel keeps it open. Save As "Demo2": `Demo1` still has two sounds when reopened.

## 4. D4: All formats, MP3 without the gap, problem files

- [x] 4.1 Our `AudioFormatManager` with exactly WAV, AIFF, FLAC, Ogg Vorbis and MP3; unsupported extensions rejected with a toast (plain `Toast` component, restyled in D7). Verify: tests import each format fixture and check sample rate, channels and length; `.txt` and `.m4a` are rejected with no change
- [x] 4.2 LAME/Xing tag parser and trim (design D-4); calibrate the decoder offset by cross-correlation against `drums-loop-120.wav`; tagless MP3 cut 1105. Record the calibrated value in `docs/m0-findings.md` (MP3 delay row) and the code. Verify: test checks length within ±1 frame and alignment of the tagged MP3, and the tagless rule
- [x] 4.3 30-minute limit (31-minute file generated in a temp folder at test time), downmix of more than 2 channels with a toast, corrupt file and partial-batch failure leave no files and no undo step. Verify: tests for each, including that the bundle folder listing is unchanged after a failure
- [x] 4.4 Progress indicator for running jobs and "importing" rows in the browser while a batch decodes. Verify: component test with a slow fake job shows the indicator and the pending row, both gone when it finishes
- [x] 4.5 PO demo D4 accepted (script below)

**Demo D4 script:** drop all five format fixtures (WAV, AIFF, FLAC, MP3, OGG): five rows, each plays. Click the MP3 loop and the WAV loop one after the other: both start on the beat with no gap at the start. Drop `5ch-tone.wav`: toast "mixed down to stereo". Drop `not-audio.txt` and `corrupt.wav`: toasts name each file, nothing is added. Run `make-fixtures --long %TEMP%\long` and drop the 10-minute file: a progress indicator and an "importing" row appear, and other rows still play meanwhile; drop the 31-minute file: toast "longer than 30 minutes".

## 5. D5: Waveforms in the browser

- [x] 5.1 Browser rows draw a `SmartThumbnail` from `EngineHost::createThumbnail`, created lazily for visible rows with an LRU of 64 (design D-7). Verify: component test scrolls 200 sources and the number of live thumbnails stays at or below 64
- [x] 5.2 Thumbnail files only under `cache/`, rebuilt after `cache/` is deleted. Verify: test imports fixtures, waits for thumbnails, checks no new files outside `cache/`, deletes `cache/`, reopens and gets thumbnails again
- [x] 5.3 PO demo D5 accepted (script below)

**Demo D5 script:** open `Demo1.sdaw` and drop the texture and kick fixtures. Each row shows a small waveform: the kick is a short spike, the texture a long evolving shape, the drum loop shows regular hits. Quit, delete `Demo1.sdaw\cache` in Explorer, reopen: the waveforms come back.

## 6. D6: Search and categories

- [x] 6.1 `BrowserModel` (plain class): rows from the `SAMPLER` tree, case-insensitive substring search, category place (All, Loops, One-shots, Recordings), name sort, empty-result state. Verify: unit tests for each filter combination and a 1,000-source test that filters in under 5 ms
- [x] 6.2 Category on import (under 2 s: One-shot, else Loop) and `SetSourceCategory` command from the row's context menu. Verify: tests for import categories, undo/redo of a category change and save/reopen keeping it
- [x] 6.3 Search box and Places list in `BrowserPanel`, wired to `BrowserModel`; "nothing matches" text. Verify: component test types into the search box and clicks a place, and checks the visible rows
- [x] 6.4 PO demo D6 accepted (script below)

**Demo D6 script:** with the fixtures imported, type "drum": only the drum loops stay; clear it. Click One-shots: the kick is there, the loops are not. Click Recordings: empty. Right-click "pad-chord" > One-shot: it moves to One-shots; Ctrl+Z moves it back. Save, reopen: categories are as left.

## 7. D7: The look from the mock

- [x] 7.1 Embed IBM Plex Sans and Mono (OFL, licence file alongside) in a `sampler_resources` binary-data target and add `SamplerLookAndFeel` with the colour tokens of design §10.2. Verify: a test checks the typeface loads from binary data; screenshot check in the demo
- [x] 7.2 Widget set: `Toast`, `ToggleButton`, `SegmentedControl`, `ValueField`, `Knob`, `Fader`, `Meter`, `Pad`, with logic (value mapping, drag-to-value, hit areas) in plain classes; a developer "Widget gallery" window from the Help menu. Verify: unit tests for the value and drag maths; the gallery opens
- [x] 7.3 Layout and styling pass on header, browser (search box, Places, rows, thumbnails, playing indicator), drop hint, toasts and progress so they match `concepts/project/Main.dc.html`. Verify: a `--snapshot <png>` developer flag saves the window, and the screenshot is compared with the mock in the demo
- [x] 7.4 PO demo D7 accepted (script below)

**Demo D7 script:** open the app next to `concepts/index.html` (Session board). The header, browser panel (search, PLACES, rows with waveforms) and the drop hint use the same colours, fonts and spacing. Trigger a toast (drop `not-audio.txt`) and an import progress: both are styled. Help > Widget gallery shows knob, fader, meter, pad, toggle, segmented control and value field, and they respond to the mouse.

## 8. M1 exit and wrap-up

- [x] 8.1 Full M1 exit run on a clean build: import the 5 formats, save, quit, reopen, undo and redo work, waveforms are drawn, search and categories work. Verify: performed by the PO from the demo scripts in one sitting
- [x] 8.2 Update `ROADMAP.md`: M1 `Change:` line, tick the M1 items that were demonstrated, exit criterion text "import 5 formats (WAV, AIFF, FLAC, MP3, OGG)", and decisions log entries (project-only browser, M4A replaced by AIFF in the format count, LAME pulled forward from M4, untitled projects in the app data folder, audition through a preview edit, plus any fallback taken in 1.9). Verify: every ticked item matches a passed task above
- [x] 8.3 CI green on the branch for both presets, then open the PR with the two required sections (What changed for the user / Technical summary). Verify: CI status and PR description

## Workflow follow-up

- Run `/opsx:verify` before archiving; it should also check that the ROADMAP M1 items are ticked.
- Archive with `/opsx:archive m1-core-import` after the PR is merged.
