# Tasks

## 1. Build skeleton and CI

- [x] 1.1 Add JUCE 8, Tracktion Engine 3 and Catch2 as pinned submodules in `external/`, record versions in `external/VERSIONS.md`; verify `git submodule status` shows exact commits and the file lists them
- [x] 1.2 Create top-level `CMakeLists.txt`, `CMakePresets.json` (`windows-msvc-debug`, `windows-msvc-release`), `src/` layout per AGENTS.md, static MSVC runtime, `/O2` on Tracktion and engine in debug; verify the debug preset configures and builds an empty JUCE+Tracktion app with no warnings
- [x] 1.3 Add a Catch2 test target and a trivial test wired to `ctest`; verify `ctest --preset windows-msvc-debug` runs and passes
- [x] 1.4 Add GitHub Actions workflow on `windows-latest` (submodules, build, ctest); verify the workflow is green on the branch
- [x] 1.5 Create `docs/m0-findings.md` with the results table skeleton listing every M0 exit item; verify each item from the roadmap M0 row has a row

## 2. Playback, M4A and offline render

- [x] 2.1 Spike app Player tab: open and play a WAV through Tracktion on WASAPI shared; verify audible, correct speed, no dropouts for 60 s in the debug preset
- [x] 2.2 Enable MP3 decoding and play an MP3; verify correct speed, and measure whether the decoder trims encoder delay (it does not: recorded in the findings, trimming is planned for the M1 import pipeline)
- [x] 2.3 M4A: descoped by the user (not a priority). AAC result recorded for information only; ALAC not tested
- [x] 2.4 Offline-render 16 bars at 120 BPM to WAV; verify the 32 s file is produced in under 32 s and its length is correct

## 3. Engine assumption tests (headless, Catch2)

- [x] 3.1 Test: tempo change 120 to 90 keeps a clip at its beat position; verify the test passes, or record the failure and write the `SetTempo` re-position decision in the findings
- [x] 3.2 Test: save a bundle with relative audio paths, move the folder, reload; verify no missing files, or record the decision
- [x] 3.3 Test: `sampler_sourceId`, `sampler_warpMode`, `sampler_sourceBpm` on a clip survive save/reload; verify the test passes, or switch to the `SAMPLER/CLIPMETA` fallback and record it
- [x] 3.4 Redirect proxies and thumbnails into `cache/`; verify by listing the filesystem after rendering waveforms that nothing is written beside the audio
- [x] 3.5 Verify atomic save (tmp, flush, rename) by killing a writer mid-save in a test; verify the old file stays loadable
- [x] 3.6 Verify audio files are byte-identical after open, edit, render, save (hash check in a test)

## 4. Computer-audio capture probe

- [x] 4.1 Define the `ICaptureSource` interface in `src/platform` and a Windows endpoint-loopback implementation (event-driven, MMCSS, ring buffer, writer thread to WAV); verify a 30 s recording of a browser video plays back correctly
- [x] 4.2 Add silence insertion for gaps; verify a recording with a 5 s pause is still about 30 s long
- [x] 4.3 Add process-loopback attempt with fallback to endpoint loopback; verify on build 19045 that the fallback happens without error and the result is logged in the findings
- [x] 4.4 Verify own-output muting: play a clip in the app during capture and confirm it is not in the file; record the mute mechanism

## 5. Timeline spike

- [x] 5.1 Implement viewport math, clip hit-testing and track-reorder index math as plain classes with Catch2 tests; verify tests pass
- [x] 5.2 Timeline Component with 4 tracks and 50 clips with `SmartThumbnail` waveforms, horizontal scroll and zoom; verify at least 30 fps during continuous zoom/scroll in release and report the debug figure
- [x] 5.3 Clip drag (same and other track) and track-header drag-reorder; verify manually against the spec scenarios and note results in the findings

## 6. Multi-out VST3 spike (manual; stand-in for Maschine 3)

- [ ] 6.0 Build a JUCE test plugin (VST3 instrument, 4 stereo outputs, deterministic looping output, state that changes); verify it loads in a plugin host or the spike app
- [ ] 6.1 Load the test plugin (and Surge XT / Kontakt 7 Player if installed) by fixed path on an instrument track and open its editor; verify the UI renders and responds, and record any activation or load issues
- [ ] 6.2 Play a pattern from host transport at 120 BPM for 16 bars; verify it starts with the transport and stays in time
- [ ] 6.3 Build the 4-output rack routing onto 4 tracks; verify each group is on its own track with its own meter, and confirm the plugin processes once per block (counter or CPU comparison)
- [ ] 6.4 Save, close and reopen the project; verify the plugin state is restored
- [ ] 6.5 Offline-render 16 bars including the plugin; verify timing against a real-time capture and record the result
- [ ] 6.6 If any step in 6.1 to 6.5 fails with a third-party plugin, repeat it with the JUCE test plugin to separate host faults from plugin faults; record which
- [ ] 6.7 Record in the findings the Maschine-specific checks deferred to M5 (very large state, editor resize, pattern drag-out)

## 7. Benchmarks and exit gate

- [ ] 7.1 Console benchmark of Rubber Band and Signalsmith stretch on a 4-bar loop and a 3-minute track; verify the timings are in the findings
- [ ] 7.2 Prototype beat-preserving renderer (split at onsets, place, no stretch inside) timed on the same inputs; verify the timings and a listening note are in the findings
- [ ] 7.3 Complete `docs/m0-findings.md`: every item has pass/fail/partial plus a decision for each failure (including any custom-engine fallback); verify no row is empty
- [ ] 7.4 Update `AGENTS.md` repo-state section and the design doc's "verify at M0" markers with the outcomes; verify the text matches the findings
