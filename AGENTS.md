# Sampler DAW: agent guide

Working name "Sampler". A solo hobby desktop DAW: import audio (wav/mp3/flac/ogg/m4a) or **capture computer audio** (e.g. a YouTube video in the browser), chop it into slices in a sample editor, play slices from 4x4 pads, arrange clips on several tracks, host third-party plugins (**Native Instruments Maschine 3** is the acceptance test), and export WAV/MP3/stems.

## Repo state (as of 2026-10-08)
Milestone **M0 (spike)** is nearly done: the build skeleton, CI, the spike app, probes, the JUCE test plugin and 40 tests exist (results in `docs/m0-findings.md`; only a listening check of the beats prototype is open). Every Tracktion assumption passed, so the Tracktion design stands and the custom-engine fallback is not needed. Next is **M1**. The repo holds:
- `technical design/technical-design.md`: the authoritative design (v2). Section numbers below (e.g. "§6.4") refer to it. Read the relevant section before implementing a feature in depth; this file covers the decisions and rules that apply everywhere.
- `technical design/tech-comparison-rust-vs-cpp.md`: why C++/JUCE/Tracktion was chosen. `technical design/archive/` holds the superseded v1 (Tauri/Rust/React). Do not use v1, except its §5 custom-engine design, which is the fallback if Tracktion fails an M0 check.
- `concepts/`: HTML UX mocks (`index.html` opens Session, Arrangement and Sample editor boards from `concepts/project/*.dc.html`).
- `openspec/`: spec-driven change workflow (`/opsx:propose`, `/opsx:apply`, `/opsx:verify`, `/opsx:archive`; skills in `.claude/skills/`). `openspec/specs/` is empty so far.

The design doc's "Changes in v2" block and some milestone rows still say Windows + macOS parity is a hard requirement. **That is superseded** by the 2026-10-08 update (below).

## Standing decisions (do not reopen without the user)
- **Stack:** C++20 (MSVC v143), **JUCE 8** for the app and the UI, **Tracktion Engine 3** as the engine (edit model, transport, clips, warp, mixer, MIDI, plugin hosting, racks, PDC, recording, automation, rendering, undo). CMake 3.28+ with Ninja, dependencies as pinned git submodules in `external/` (record versions in `external/VERSIONS.md`). Catch2 for tests.
- **The mocks are drawings only.** They define layout and behaviour, never technology. Do not treat "reusing the HTML" as an argument for a web or WebView UI. The UI is JUCE Components with a custom `SamplerLookAndFeel`.
- **Hosting plugins is a core requirement.** VST3 (plus AU once on macOS). No VST2, AAX, CLAP or ARA (CLAP/ARA are Later). Plugins run in process; **scanning runs out of process** (`Sampler.exe --scan <path>`) with a timeout and a blacklist.
- **Platform: Windows only for now (TODO-MAC).** The user has no Mac. Build and verify on Windows 10/11 x64 only. Do not require Mac verification, macOS CI, AU, Core Audio taps or a `.dmg` until the user says they have a Mac. Then the parity rule comes back (every milestone verified on both platforms, §12.1). Keep the code portable anyway (rules below).
- **Dev machine:** Windows 10 22H2, build 19045. This is below build 20348, so WASAPI *process* loopback is not officially supported there: try it at runtime and fall back to *endpoint* loopback.
- **Audio drivers:** WASAPI shared by default (WASAPI exclusive is available in Audio Settings). The user's Focusrite is not connected, so **ASIO is deferred** (it needs the Steinberg ASIO SDK in `external/asiosdk` and `-DSAMPLER_ASIO=ON`).
- **License:** private hobby project. If it is ever published it must be AGPLv3 (JUCE AGPLv3 + Tracktion GPLv3). Rubber Band (GPL) and LAME (LGPL) are fine under that.

## Planned build (from §12; create these files at M0)
```
cmake --preset windows-msvc-debug   && cmake --build --preset windows-msvc-debug
cmake --build --preset windows-msvc-release   # then: iscc installer/sampler.iss
ctest --preset <preset>                       # unit + golden + migrations; `ctest -L slow` = analysis accuracy suite
sampler-cli render proj.sdaw out.wav          # headless render (golden tests); `sampler-cli analyze file.wav`
```
- Top-level CMake options: `SAMPLER_ASIO`, `SAMPLER_RUBBERBAND`. Static MSVC runtime. Warnings as errors, clang-tidy, ASan (`/fsanitize=address`) in CI.
- Build `tracktion_engine` and `src/engine` optimised (`/O2`) **even in debug presets**, because debug Tracktion is too slow for audio.
- Prerequisites: VS 2022 Desktop C++, CMake 3.28+, Ninja, Git LFS (golden WAVs live in LFS).
- CI: GitHub Actions on `windows-latest` (the macOS job is TODO-MAC).

## Progress tracking
`ROADMAP.md` is the progress tracker: one section per milestone with checkboxes for scope items and exit criteria. Keep it current as part of the work, in the same branch and PR as the change:
- When you propose a change for a milestone, fill in that milestone's `Change:` line.
- Tick `[x]` an item only once it works and was verified (build, test, or manual check as the design requires). Never tick on intent. Leave it unticked and say why if only partly done.
- Update "Current milestone" and "Last updated" when a milestone starts or its exit criteria are all met.
- Log decisions (failed M0 checks, fallbacks, scope changes) in its Decisions log, and keep `AGENTS.md` / the design doc consistent if a decision changes a standing rule.
- `/opsx:archive` and `/opsx:verify` should include a check that the matching ROADMAP items are ticked.
- The design doc §15 stays the authority for scope. If scope changes, update both.

## Pull request descriptions
Every PR description has exactly two sections, in this order:
1. **What changed for the user:** the functional change, in plain language, as the user of the app would experience it (what they can now do, or what now behaves differently). Nothing technical: no class names, file names, libraries or internals.
2. **Technical summary:** a high-level overview of the technical changes (new or changed modules, the approach taken, notable decisions). Keep it short; do not list every file or restate the diff.

If a change has no user-visible effect (refactor, build, tests), say so in section 1 in one line.

## Architecture
One process. Tracktion owns the document and the playback. A thin "Sampler layer" adds what Tracktion lacks: the sample library, analysis, slicing, pads, the Beats warp mode and the Command API.

**Source layout and dependency direction** (enforced through CMake target links):
`analysis` <- `render`; `model` <- `commands`; `analysis, render, model` <- `engine` <- `commands` <- `io` <- `ui` <- `app`.
| Dir | Contents |
|---|---|
| `src/model/` | Wrappers for the `SAMPLER` ValueTree subtree (Source, SliceSet, Slice, ClipMeta), ids, migrations. No GUI, no audio thread |
| `src/analysis/` | Onset detector, BPM estimator, classifier, slicer. Pure DSP on buffers: **no Tracktion, no JUCE GUI** |
| `src/render/` | BeatsRenderer, SliceRenderer, `Stretcher` interface (Signalsmith), RenderScheduler. **No Tracktion** |
| `src/engine/` | EngineHost (te::Engine and device setup), `SlicePadPlugin`, Audition, PluginService, WarpService |
| `src/commands/` | `CommandService` and every Command. **The only code that edits `te::Edit`** (`io` also may, for open/save only) |
| `src/io/` | ImportService, Mp3Writer (libmp3lame), ExportJob, bundle open/save, autosave, LibraryIndex |
| `src/ui/` | LookAndFeel, widgets, views, KeyboardRouter, UiState, SelectionModel |
| `src/platform/windows|mac/` | **The only place for OS-specific code** (WASAPI loopback capture, Media Foundation M4A reader, MMCSS, permissions), behind small interfaces such as `ICaptureSource` |
| `src/app/` | `Main.cpp`, MainWindow, command IDs, the `--scan` child-process entry |
| `tools/sampler-cli/` | Headless render and analysis |
| `tests/unit, golden, migrations, audio-fixtures` | Catch2; golden = plugin-free `.sdaw` fixtures rendered at a fixed block size |

All Tracktion calls stay in `engine/` and `commands/`, so that Tracktion can be replaced by the fallback engine.

### Threading and ownership rules (§4.2)
1. `te::Edit` (including our `SAMPLER` subtree) is the **single source of truth** and lives on the message thread.
2. **UI -> model only through `CommandService::apply(cmd)`.** UI code never writes the ValueTree and never calls Tracktion editing APIs. Each command is one `UndoManager` transaction. Continuous gestures (fader drag, clip drag) use `begin` / `update` / `commit` and form a single transaction; a clip drag paints a preview and applies on mouse-up. Repeated `SetTrackParam` calls on the same target within 500 ms coalesce into one transaction. In debug builds, `apply` checks post-conditions: no clip overlaps, regions within source bounds, unique ids.
3. **Model -> UI:** `ValueTree::Listener` and Tracktion change broadcasters trigger `repaint()`. No mirrored model, no patches.
4. **Audio -> UI:** read-only polling at 60 Hz through a `juce::VBlankAttachment` (transport position, `LevelMeasurer`, atomics for pad flashes). The audio thread never calls into the UI. 60 Hz items repaint only small dirty rectangles.
5. **Workers never touch the edit.** They post results with `MessageManager::callAsync`, and a Command or service applies them on the message thread.
6. Selection, zoom, scroll and view state live in `UiState` / `SelectionModel` and are not undoable.

### Real-time code (§5.1)
Our audio-thread code is only `SlicePadPlugin::applyToBuffer` and the level/position readers. In it: no allocation or free, no locks, no I/O, no logging (a lock-free log ring is allowed), work bounded by voices x block size, buffers preallocated in `initialise()`. UI -> audio messages go through `juce::AbstractFifo` SPSC queues; anything the audio thread reads otherwise is atomic. Shared buffers are reference-counted, and the last release must happen off the audio thread (retired buffers return through a garbage FIFO). Debug builds hook `operator new`: allocation in our audio code asserts, allocation inside Tracktion or plugins only logs.

### Platform-portability rules (apply now, even with Windows only)
- No `#ifdef _WIN32` / `JUCE_WINDOWS` / `JUCE_MAC` outside `src/platform/` and CMake files.
- Paths are `juce::File` only. Text is UTF-8 with `\n` line endings.
- Shortcut tables say "Mod", not "Ctrl" (`ModifierKeys::commandModifier`). Use one `MenuBarModel`.
- Prefs live at `juce::File::userApplicationDataDirectory`/Sampler (`settings.xml`, `library.xml`, `plugins.xml`, `logs/`).

## Key domain rules
- **Audio files are never modified.** Every edit is metadata in the edit. Derived audio (decoded WAVs, Beats renders, slice renders, Tracktion proxies, thumbnails) is disposable and lives in `cache/`, keyed by content hash.
- **Import (§6.1):** copy into `audio/originals/<sanitised-name>-<hash8>.<ext>` (deduplicate by hash), then decode once to `audio/<name>-<hash8>.wav` (32-bit float, native rate). **Clips always reference the decoded WAV.** Trim MP3 encoder delay at decode time. Reject files over 30 minutes. Downmix more than 2 channels to stereo. Sources keep their native rate, and Tracktion resamples at playback.
- **Time model (§5.3):** UI and commands use beats (`te::BeatPosition`), Tracktion uses seconds (`te::TimePosition`), and slice markers and source offsets use frames at the source rate (`int64`). Convert only through `te::TempoSequence`. Clips must keep their **beat** positions when the tempo changes (if Tracktion does not do this, `Cmd::SetTempo` re-positions the clips in the same transaction).
- **No clip overlaps within a track (§8.3):** moving or pasting B onto A trims A (or splits it when B lies inside), in the same command.
- New clips get a 3 ms declick fade in and out (separate from user fades).
- **Warp modes (§7.6):** Re-pitch = varispeed with stretch disabled. **Beats = our `BeatsRenderer`**: split at onsets, place each segment at its stretched position with no stretching inside segments, write `cache/beats_<key>.wav`; while a render is pending, play the previous render or fall back to Re-pitch. Tones = Tracktion `rubberbandMelodic` proxy. Texture = decide at M4. `WarpService` reacts to tempo, BPM and region changes.
- **Slices (§7):** one SliceSet per Source; slice i = [marker i, marker i+1). At most 256 slices, minimum length 5 ms. Modes Beat / Transient / Manual; the control that does not apply to the current mode is disabled. "Send slices to track" **copies** each slice into an independent clip that keeps `origin {sliceSetId, index}`.
- **Pads (§7.4):** `SlicePadPlugin` is a `te::Plugin` on a hidden pad track routed to the master, with 16 voices, oldest-voice stealing with a 3 ms fade, and a retrigger that chokes the previous voice. MPC layout with pad 1 bottom-left: keys `1234/QWER/ASDF/ZXCV`, `Z` = pad 1. Pad keys are active only while the Sample editor has focus and no text field is focused (`KeyboardRouter` scope stack: dialog > text input > sample editor > view > global). Ignore auto-repeat, and send all-pads-off on focus loss. From M5, MIDI notes 36 to 51 map to pads 1 to 16.
- **Computer-audio capture (§2.7, §6.4, M2b):** our own capture code, outside JUCE's device layer. WASAPI loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`, event-driven, shared mode) on a capture thread with MMCSS "Capture" -> `AbstractFifo` ring (10 s) -> `ThreadedWriter` -> `audio/recordings/rec-<timestamp>.wav`. Insert silence for gaps when nothing is playing (loopback sends no packets then). Exclude the DAW's own process when process loopback works; otherwise **mute DAW output while capturing** by default. When capture stops: trim silence (-60 dBFS), then `Cmd::AddCapturedSource` runs the normal import pipeline with category "Recordings". Starting and stopping capture is not undoable; adding the result is. Capture needs shared mode, because WASAPI exclusive mode silences other apps.
- **Multi-out instruments (§5.6):** wrap the plugin in a Tracktion Rack; each extra output pair N gets an output track holding another instance of that rack set to output N. Multi-out tracks move together with their instrument track when reordered.

## Project bundle (§9)
`Name.sdaw/` contains `project.tracktionedit` (Tracktion XML, written atomically: tmp + flush + rename), `audio/originals/`, `audio/recordings/`, decoded WAVs in `audio/`, a disposable `cache/`, and `autosave/` (2 s debounce, at least every 60 s when dirty; 10 rolling backups). Our data is a `<SAMPLER schemaVersion=N>` subtree inside the edit. Per-clip extras are `sampler_*` properties on Tracktion's clip nodes (fallback: `SAMPLER/CLIPMETA`). File references are relative to the bundle. Bump `schemaVersion` on breaking changes and add `migrate_vN_to_vN1(ValueTree&)` with a fixture test. Refuse files with a newer schema version. A `.sdaw.lock` file holding the PID detects crashes.

## Libraries (§3)
JUCE formats for decoding (MP3 via `JUCE_USE_MP3AUDIOFORMAT`; M4A through our own Media Foundation reader in `platform/windows`). Rubber Band through Tracktion (`TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1`). Signalsmith Stretch (MIT) for slice renders. r8brain-free for offline resampling. libmp3lame linked directly (not `lame.exe`). Analysis is our own spectral-flux onset detector and BPM estimator on `juce::dsp::FFT`. Thumbnails come from Tracktion `SmartThumbnail`. **No FFmpeg**, except an optional user-supplied `ffmpeg.exe` run as a subprocess.

## Roadmap (§15)
M0 spike -> M1 core + import -> M2 arrangement -> M2b capture -> M3 sample editor -> M4 warp + export = **MVP** (import or capture -> chop -> arrange -> export, on Windows) -> **M5 plugins + MIDI (Maschine acceptance test)** -> M6 Session view (Tracktion clip launcher) -> M7 polish -> M8 input recording -> M9 effects + automation -> M10 tempo map. The app opens in Arrange until M6. Do not pull plugin work ahead of M5 (scope rule), apart from the Maschine load test in the M0 spike.

**M0 results for the Tracktion assumptions** (details and numbers in `docs/m0-findings.md`): clips keep beat positions on tempo change (pass: `SetTempo` needs no re-positioning); relative paths survive moving a bundle (pass, but the edit file must exist on disk before a relative reference is created); unknown `sampler_*` clip properties survive a round trip (pass: no `CLIPMETA` fallback needed); proxies and thumbnails can be redirected into `cache/` (pass: set the engine temp directory on every project open; a stretch proxy is 0 bytes while rendering, so wait before tearing the engine down); a multi-out rack runs the plugin once (pass: 4 output tracks, 1 `processBlock` per block); JUCE's `WindowsMediaAudioFormat` does not open M4A (M4A import descoped; Media Foundation reads it if wanted later); process loopback works on build 19045 (use it first, endpoint loopback as fallback). Other M0 findings that bind later work: the engine does not trim MP3 encoder delay (trim at import); wait for the engine's startup device scan before playing; a plugin must be in the engine's known-plugin list before Tracktion can load it by path; Rubber Band is built into the Tracktion module (`external/rubberband`).

## Testing (§13)
Keep logic out of Components so Catch2 can test it on plain classes (snapping, hit-testing, viewport math, keyboard routing, track-reorder index math, drop-target resolution). Commands get property-style tests: random command sequences, invariants after each step, `undo(apply(x)) == x` on the serialised edit, save/load round trip. Golden renders run only on plugin-free projects. Test plugin hosting against a JUCE test plugin we build; Maschine is tested manually.
