# M0 spike findings

Result column: `pass`, `fail`, `partial` or `todo`. Every `fail` and `partial` needs a decision (a workaround, or "use the custom-engine fallback for this area").

Environment: Windows 10 22H2 build 19045. Pinned versions: see `external/VERSIONS.md`.

## Exit items from the roadmap (M0 row)

| # | Item | Task | Result | Numbers / notes | Decision |
|---|---|---|---|---|---|
| 1 | CMake + JUCE 8 + Tracktion 3 build on Windows | 1.2 | pass | Debug and CI build clean, no warnings | |
| 2 | CI green on Windows | 1.4 | pass | windows-latest, configure+build+ctest in 6m32s | |
| 3 | Play a WAV | 2.1 | pass (audible check pending) | Debug build, 60 s, Windows Audio (WASAPI shared), 44.1 kHz / 441-frame blocks. Transport position tracked wall time (60.09 s after 60 s), engine CPU 1.2-1.5%, no input device open. JUCE WASAPI reports no xruns (-1), so "no dropouts" rests on timing and CPU, plus a listening check | Engine must not be loaded/played until its startup device scan has finished (see observations) |
| 4 | Play an MP3 | 2.2 | pass (delay not trimmed) | Plays in real time at the right speed (real 192 kbps LAME MP3). The engine decodes MP3 through Windows Media Foundation, which does NOT trim: decoded length = Xing frames x 1152 (9,199,833 vs 9,199,872), so ~1105 frames of encoder delay and the padding remain | **Trim at import in M1** (now in ROADMAP.md and design 6.1): start cut = delay + 529 + 1, end cut = padding; no tag: cut 1105 at the start. Test with a real LAME file |
| 5 | Capture probe: 30 s of browser audio via WASAPI loopback | 4.1, 4.2 | pass | Self-test: 14 s recording of a 440 Hz tone with the output device closed for 3 s in the middle: file 14.001 s, tone in 10 of 10 windows, gap silent in 3 of 3, no overruns. Real use: 30 s of a YouTube video in the browser recorded with `--endpoint`, file 30.04 s, no overruns, the user listened and it sounded right; the file has no dropouts (0.01 s of exact zeros, at the very start). Quiet because the system volume was low | **Open, minor:** in that run the recorder's counter said 18.9 s of silence was inserted, yet the file holds almost none. The file is what counts and it is right, so the counter (or how it is incremented) is probably wrong, not the audio. A clean silent 8 s run reports 8.0 s in one fill, as expected. Re-check the counter when the real `CaptureService` is built (M2b). Also: writing to the Desktop was slow (about 0.4 MB/s) and the writer finished 20 s after the recording; write to the project folder and keep writes in large blocks |
| 6 | Process loopback on build 19045 | 4.3 | pass (it works) | Windows 10 build 19045: `ActivateAudioInterfaceAsync` with process loopback succeeds although Microsoft documents it for build 20348+. Excluding this process: own 440 Hz tone absent (10 of 10 windows silent), exact length 14.000 s. Another process playing (the spike app): captured at full level (rms 0.106). Fallback checked by simulating a failure: falls back to endpoint loopback, logs `process loopback unavailable (0x80004001)`, no error | Use process loopback first and endpoint loopback as the fallback, as designed. Because it works on this build, the mute-own-output policy is only needed in the fallback |
| 7 | Own output not in the capture (mute policy) | 4.4 | pass | Real engine playing a tone, device-mode capture: master volume at zero gives a silent recording (12 of 12 windows), unmuting gives audible audio (11 of 11) | Mechanism: `EngineHost::setOutputMuted` takes the Edit's master volume slider to 0 (true silence) and restores the previous dB afterwards. Used only when capture runs in endpoint (fallback) mode |
| 8 | Timeline: 4 tracks, 50 clips with waveforms, scroll/zoom | 5.2 | pass | 4 tracks, 50 clips, waveforms from Tracktion `SmartThumbnail` (checked in a saved snapshot). Scripted zoom (10-160 px per beat) and scroll for 10 s, software renderer: **release 47.8 fps** (479 paints; paint avg 6.3 ms, p95 20 ms, max 22 ms; two runs agree), **debug 29.5 fps** (paint avg 22.5 ms, p95 88 ms). Bench driver ticks at 60 Hz, so 60 is the ceiling | Meets 30 fps in release. Debug is right at the line; keep an eye on it as the UI grows (only visible clips are drawn and only the visible part of each waveform) |
| 9 | Timeline: clip drag | 5.3 | pass | Tried by hand by the user in the spike app: clips drag to other tracks and sideways, with preview and snapping. Also covered by logic tests (15 cases) and the real component driven by synthetic mouse events (7 cases). The model changes only on mouse-up | Gaps seen by hand, all expected for a spike and owned by M2: dropping a clip onto another hides the one underneath (the no-overlap rule, design 8.3, trims it instead); no undo (comes with `CommandService`, M1) |
| 10 | Timeline: track drag-reorder | 5.3 | pass | Tried by hand by the user: header drag with insertion marker reorders the tracks and the clips travel with them. Same automated tests as row 9 | No scrollbar: only wheel scrolling exists (a scroll bar comes with the real timeline in M2) |
| 11 | Multi-out VST3 (stand-in for Maschine 3) loads, editor opens | 6.1 | todo | | |
| 12 | Plugin plays from host transport at 120 BPM | 6.2 | todo | | |
| 13 | 4 stereo outputs on separate tracks via racks | 6.3 | todo | | |
| 14 | Rack runs the plugin once, not once per instance | 6.3 | todo | | |
| 15 | Save and reopen restores plugin state | 6.4 | todo | | |
| 16 | Offline render of 16 bars (with plugin) | 6.5 | todo | | |
| 17 | Offline render of 16 bars (plug-free) | 2.4 | pass | 32.00 s WAV, whole test (engine start + generating a 40 s source + render) 3.4 s in the debug build | |

## "Verify at M0" assumptions

| # | Assumption | Task | Result | Numbers / notes | Decision |
|---|---|---|---|---|---|
| A1 | Clips keep beat positions on tempo change | 3.1 | pass | Clip at beat 8: 120 -> 90 BPM keeps beat 8.0 and moves from 4.000 s to 5.333 s. Test `A1` | None needed: `Cmd::SetTempo` does not have to re-position clips |
| A2 | Relative paths survive moving a bundle | 3.2 | pass (with a rule) | Stored as `../audio/loop.wav`-style paths relative to the edit file's folder; a renamed and moved `.sdaw` folder reopens with the audio found. Rule: the edit file must already exist on disk when a relative reference is created. Otherwise Tracktion treats the missing file as a folder and writes a wrong path (`..\audio\loop.wav`), which breaks after the move. Test `A2` | New project saves its edit file at once (before any clip is added) |
| A3 | Unknown `sampler_*` clip properties survive a round trip | 3.3 | pass | `sampler_sourceId`, `sampler_warpMode`, `sampler_sourceBpm` set on the clip node come back unchanged after save and reopen. Test `A3` | Keep per-clip extras as `sampler_*` properties on the clip node; the `SAMPLER/CLIPMETA` fallback is not needed |
| A4 | Proxies and thumbnails can be redirected into `cache/` | 3.4 | pass | `TemporaryFileManager::setTempDirectory(cache)`: the per-edit folder `cache/edit_<id>/` receives the render file (a reversed clip gave `render_*.wav`, 705,704 bytes); the thumbnails folder is `cache/thumbnails`. Nothing written beside the audio. Plain WAV thumbnails write no file at all. Test `A4` | Set the temp directory to the bundle `cache/` when a project opens. It is a single engine-wide setting that Tracktion also saves in its settings store, so set it on every open. Stretch proxies untested: no stretcher is built yet (see below) |
| A5 | Multi-out rack runs the plugin once | 6.3 | todo | | |
| A6 | M4A decoding (descoped, information only) | 2.3 | not required | JUCE wrapper lists only .mp3 .wmv .asf .wm .wma: 0 of 8 AAC .m4a files open in the engine. Media Foundation opens 8 of 8 directly (probe in `src/platform/windows`). ~2,100 frames of AAC priming untrimmed. ALAC untested | None for M0. M4A import is not a priority; if wanted later, use the Media Foundation reader and trim the priming |
| A7 | Process loopback works on build 19045 | 4.3 | pass | See row 6 | |
| A8 | Atomic save leaves the old file intact when interrupted | 3.5 | pass | `sampler::io::writeFileAtomically` (temp file next to the target, flush, swap). A separate process writing 50 MB was killed with half written: the project file kept its exact bytes (same SHA-256), still opened with its clip and properties, and a later save worked. A temp file `<name>_temp<hex>.<ext>` is left behind after a kill | Use this helper for every project write. Clean up `*_temp*` files on open |
| A9 | Audio files are never modified | 3.6 | pass | Open, add clips, change tempo, thumbnail, reverse render, offline render, save, reopen and save again: audio file has the same SHA-256, size and modification time. Test `A9` | |

## Benchmarks

| Item | Input | Time | Notes |
|---|---|---|---|
| Rubber Band | 4-bar loop | | |
| Rubber Band | 3-minute track | | |
| Signalsmith Stretch | 4-bar loop | | |
| Signalsmith Stretch | 3-minute track | | |
| Beats renderer prototype | 4-bar loop | | |
| Beats renderer prototype | 3-minute track | | |

## Other observations

- Engine startup: right after the engine is constructed there is no wave output device in the engine. Playing then makes the transport start and stop itself within 0.15 s (position stuck, playing=0). After the startup scan it works. M1 must wait for the device manager to finish before loading or playing (or reallocate the playback context when devices appear).
- Engine output device defaults: it also opens an audio input by default (this PC has an ASUS Xonar input). Switched off with an engine-behaviour override until M8 (input recording).
- Render tasks need a UI behaviour that runs them (the default does nothing): `runTaskWithProgressBar` runs the job on a thread while pumping messages. Without it `renderToFile` returns nothing.
- No time-stretch library is compiled in: `TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND`, `_SOUNDTOUCH` and `_ELASTIQUE` all default to 0, so Tracktion's default stretch mode is `disabled`. Rubber Band must be added and enabled (task 7.1) before warp or stretch proxies can be tested.
- Endpoint loopback records what the device plays after the Windows volume is applied: a tone at 0.2 FS arrived at 0.004 (device volume low), and a different level on the next run after the volume moved. Process loopback delivers the signal before the volume (full level). Process loopback is therefore also the better recording path.
- Loopback sends no packets while nothing plays; process loopback sends continuous (silent-flagged) packets, endpoint loopback does not. Gap filling from packet QPC timestamps handles both.
- Plugin hosting was in process; instability seen with the plugins tried: (todo). Deferred to M5 (Maschine 3 not owned): very large state, editor resize, pattern drag-out
- JUCE pinned to the commit Tracktion v3.2.0 pins (8.0.6 + 19 commits), not 8.0.15.
- Debug presets: `juce_recommended_config_flags` adds `/Od`, which cancels `/O2`; the engine target does not link it.
