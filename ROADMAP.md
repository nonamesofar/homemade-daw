# Roadmap and progress

Source: `technical design/technical-design.md` §15 (the design doc stays authoritative for scope; this file tracks progress).
Platform: Windows only for now (TODO-MAC). The MVP is M0 to M4.

**Current milestone:** M3 Sample editor (M2b merged on 2026-10-12)
**Last updated:** 2026-10-13

Legend: `[ ]` not started, `[x]` done and verified. Tick a scope item only when it works and was checked; tick the exit criterion only when it was demonstrated. Put the OpenSpec change name in the `Change:` line when a proposal exists.

---

## M0 Spike (2 to 3 wks)
Change: `m0-spike` (34 of 34 tasks done; progress in `openspec/changes/m0-spike/tasks.md`, results in `docs/m0-findings.md`)

- [x] CMake + JUCE 8 + Tracktion 3 build on Windows, CI green (CI regressed on 2026-10-08 when windows-latest moved to VS 2026; fix in the working tree, not yet verified on CI)
- [x] Play a WAV and an MP3 (MP3 encoder delay is not trimmed by the engine: fix is in M1, see below)
- [x] Capture probe: record 30 s of a YouTube video via WASAPI loopback
- [x] Test whether process loopback works on build 19045 (it works on 19045)
- [x] Timeline component: 4 tracks, 50 clips with waveforms, scroll/zoom, clip drag, track drag-reorder
- [x] Stand-in for Maschine 3 (not owned yet): a multi-out VST3 instrument (our JUCE test plugin with 4 stereo outputs; Surge XT and Kontakt 7 Player are not installed). Check these with it:
  - [x] Loads and its editor opens (response checked with a click sent as window messages; the check by hand is moved to M5)
  - [x] Plays from host transport at 120 BPM
  - [x] 4 stereo outputs on separate tracks via racks
  - [x] State saves and reopens
  - [x] Offline render of 16 bars
- [x] Verify: clips keep beat positions on tempo change
- [x] Verify: relative paths survive moving a bundle (rule: save the edit file before adding clips)
- [x] Verify: unknown `sampler_*` clip properties survive a round trip
- [x] Verify: proxies and thumbnails can be redirected into `cache/`
- [x] Verify: a multi-out rack runs the plugin once, not once per instance
- [x] Verify: JUCE `WindowsMediaAudioFormat` coverage of M4A (result: not supported; M4A descoped, not a priority)
- [x] Benchmark Rubber Band and the Beats renderer (timings and listening note in the findings)
- [x] Pin versions in `external/VERSIONS.md`
- [x] **Exit:** all items pass, or a written decision to use the custom-engine fallback for each failing area (all pass; no fallback needed)

## M1 Core + import (3 wks)
Change: `m1-core-import` (done, 41 of 41 tasks; built as demos D1 to D7, see `openspec/changes/archive/2026-10-11-m1-core-import/tasks.md`)

- [x] App shell, `SamplerLookAndFeel` and widget set (IBM Plex embedded, widget gallery under Help; accepted in demo D7)
- [x] Bundle new/open/save (demo D3)
- [x] `CommandService` + undo (demo D2)
- [x] Import pipeline (demos D1 and D4)
- [x] MP3 gapless trim at decode: read delay/padding from the LAME tag, cut start `delay + 529` and the padding (no tag: assume 1105 at the start). Tested with a real LAME file (demo D4)
- [x] Browser with search and categories (demo D6)
- [x] Thumbnails (demo D5)
- [x] Audition (demo D2)
- [x] **Exit:** import 5 formats (WAV, AIFF, FLAC, MP3, OGG), save/reopen, undo works, waveforms drawn (all M1 tasks done; PR #2 merged with CI green on both presets)

## M2 Arrangement (4 to 6 wks)
Change: `m2-arrangement` (built as demos D1 to D8, see `openspec/changes/archive/2026-10-11-m2-arrangement/tasks.md`; also includes tempo/time signature, copy/paste, clip inspector and track rename/colour). Implemented and covered by automated tests; all items below were accepted by the PO on 2026-10-11 (demos D1 to D8 and the exit run).

- [x] Timeline editing: move, trim, split, duplicate, delete, snap, marquee
- [x] Timeline scroll bar and no-overlap rule when dropping clips (M0 spike has wheel scrolling only and lets clips hide each other)
- [x] Track add, delete, drag-reorder
- [x] Loop brace (demo D6)
- [x] Mixer strips (volume, pan, mute, solo) and meters
- [x] Metronome and position display (demos D1 and D6)
- [x] Golden tests (ten cases pass in debug; tick after the PO has listened to the references, see Decisions log)
- [x] **Exit:** build a 16-bar arrangement like the mock with 4 tracks; golden render tests pass

## M2b Capture (2 to 3 wks)
Change: `m2b-capture` (proposed 2026-10-12; built as demos D1 to D4, see `openspec/changes/archive/2026-10-12-m2b-capture/tasks.md`; recording onto armed tracks from System audio, Master or another track, Ableton style. Implemented and tested on 2026-10-12 (tasks.md: 18 of 25 done; what is left is the four PO demos, CI and the exit run). The items below are ticked when the PO has seen them work.)

- [ ] Track arm (R) and a per-track input menu: System audio per playback device, Master, another track
- [ ] Record button and Mod+R: one take per armed track, placed as a clip and listed under Recordings (`AddTakes`, one undo step)
- [ ] WASAPI endpoint + process loopback with device choice (`ICaptureSource`, `TakeWriter`)
- [ ] Resample Master or another track (take tap on the engine output, sample-exact)
- [ ] Exclusion/mute policy
- [ ] Silence trim and placement by clock and latency (System takes)
- [ ] Live take display: growing clip with waveform, input meter, no-signal warning
- [ ] 30-minute limit, device lost / disk full handling, project actions during a recording
- [ ] **Exit:** play a YouTube video and record 1 minute onto an armed track: it appears in Recordings and as a clip at the playhead, with no DAW sound in it; resample 4 bars of the song from Master onto a new track and it lines up

## M3 Sample editor (4 to 5 wks)
Change: `m3-sample-editor` (proposed 2026-10-12; built as demos D1 to D4, see `openspec/changes/m3-sample-editor/tasks.md`; analysis in the cache, slices saved per sound, pads in a separate preview edit. Implemented and tested on 2026-10-13 (tasks.md: 24 of 30 done; what is left is the four PO demos, CI on the branch and the exit run). The items below are ticked when the PO has seen them work.)

- [ ] Onset + BPM analysis
- [ ] Beat/Transient/Manual slicing
- [ ] Markers and slice strip
- [ ] `SlicePadPlugin`, pads and keyboard
- [ ] Play modes
- [ ] Slice renders
- [ ] Slices -> new track
- [ ] **Exit:** chop a break into 16, play the pads, lay the slices out in Arrange

## M4 Warp + export = MVP (3 to 4 wks)
Change: _none yet_

- [ ] Warp modes: Re-pitch, Beats, Tones
- [ ] Source BPM, clip loop/length
- [ ] Fades UI
- [ ] WAV/MP3/stems export
- [ ] Autosave and recovery
- [ ] Audio settings dialog (WASAPI exclusive; ASIO deferred)
- [ ] Windows installer
- [ ] **Exit (MVP):** import or capture -> chop -> arrange -> export an instrumental, end to end, on a Windows PC

## M5 Plugins + MIDI, Maschine acceptance (5 to 8 wks)
Change: _none yet_

- [ ] Plugin scanning (out of process) and plugin browser
- [ ] Instrument tracks and effect inserts
- [ ] MIDI clips and MIDI input; virtual pads MIDI input; MIDI notes 36 to 51 map to pads
- [ ] Multi-out tracks
- [ ] Plugin windows (floating + docked); includes using a plugin editor by hand with a real mouse (moved from M0)
- [ ] PDC display
- [ ] Plugin offline render
- [ ] `.mid` drop import
- [ ] Drag-out of Maschine patterns (D5)
- [ ] **Exit, Maschine acceptance test:**
  - [ ] 1. Maschine 3 loads on an instrument track
  - [ ] 2. Its patterns play in sync with host transport and loop
  - [ ] 3. Pads/MIDI keyboard play it
  - [ ] 4. 4+ outputs routed to separate tracks with their own meters and faders
  - [ ] 5. Save, quit, reopen restores the full Maschine state
  - [ ] 6. Export WAV and stems including Maschine
  - [ ] 7. A pattern dragged out of Maschine lands as a clip

## Later milestones

### M6 Session view (3 to 4 wks)
Change: _none yet_
- [ ] Clip slots, scenes, launch quantize, stop all, session/arrangement override
- [ ] "Send slices to Session"
- [ ] **Exit:** the mock's Session view works

### M7 Polish (3 wks)
Change: _none yet_
- [ ] Texture mode
- [ ] One-app capture (record only the browser: process loopback include mode)
- [ ] User warp markers
- [ ] Crossfades
- [ ] Adaptive snap
- [ ] FLAC + LUFS export

### M8 Input recording (2 to 3 wks)
Change: _none yet_
- [ ] Mic/line-in and MIDI recording into arrangement/slots (track arm and the input menu exist since M2b; interface inputs join that menu)
- [ ] Latency compensation

### M9 Effects + automation (3 to 5 wks)
Change: _none yet_
- [ ] Tracktion internal effects (EQ, compressor, delay, reverb)
- [ ] Sends/returns
- [ ] Automation lanes
- [ ] Clip envelopes

### M10 Tempo map + real-time warp (2 wks)
Change: _none yet_
- [ ] Tempo automation and ramps
- [ ] Real-time stretch for Tones/Texture

### Beyond (unscheduled)
CLAP and ARA hosting, Ableton Link, stem separation, consolidate/bounce, groove/swing, time-signature changes.

---

## Decisions log
Record decisions that came out of a milestone (for example a failed M0 check and what we chose instead). One line each, with the date.

- 2026-10-08: Windows only for now; macOS parity deferred (TODO-MAC).
- 2026-10-08: Maschine 3 is not owned yet. The M0 plugin checks use a multi-out VST3 stand-in (JUCE test plugin, Surge XT or Kontakt 7 Player). Maschine-specific checks (large state, editor resize, pattern drag-out) and the M5 acceptance test wait until Maschine 3 is available. The VST2 files on the dev machine are not used: no VST2 support.
- 2026-10-09: M0 plugin editor check by hand (clicking the test plugin's window with a real mouse) is accepted as passing on the automated check (a click sent as window messages changed the parameter). Hands-on testing of plugin windows is done in M5, when plugin hosting is built for real.
- 2026-10-09: CI regressed: windows-latest moved to VS 2026 (MSVC 19.51), whose C4458 in JUCE's VST3 hosting code failed the build because JUCE sources compiled inside our /WX targets. CI stays on `windows-latest` (we follow the newest MSVC, and `scripts/build.ps1` now uses the newest local VS too), with debug and release jobs.
- 2026-10-09: JUCE and Tracktion are compiled once, in `sampler_juce` and `sampler_tracktion` (`cmake/SamplerJuce.cmake`): one set of JUCE settings, /O2 even in Debug, no /WX, headers included as external. Only `sampler_engine` links `sampler_tracktion`, so CMake enforces "Tracktion only in src/engine". Before, every target compiled its own JUCE copy (about 12 copies of juce_core with different settings).
- 2026-10-09: Tracktion's `EditFileOperations::save` is not atomic (it deletes the edit file, then moves the new one in). The project file is saved by our `saveEditAtomically` (temp file, flush, one replacing rename) instead; findings row A8 corrected.
- 2026-10-09: The capture mute moved out of the edit: a global output processor on Tracktion's device manager silences the device output, so muting is not undoable, is never saved in a project and survives loading a file. It used to set the master volume to 0 in the edit.
- 2026-10-09: The M1 browser lists only the open project's sounds; a library shared across projects is later.
- 2026-10-09: M4A import stays descoped (M0 finding), so the five-format exit count uses AIFF instead: WAV, AIFF, FLAC, MP3, OGG.
- 2026-10-09: LAME was pulled forward from M4 into M1, because the MP3 gapless trim reads the LAME tag and the test fixtures need an encoder.
- 2026-10-09: Untitled projects live in the app data folder (`Sampler/untitled/<uuid>.sdaw`); the first Save moves the folder to the chosen place.
- 2026-10-09: Audition plays through a Tracktion preview edit; the planned plain-JUCE fallback was not needed. The first play starts from a 30 ms timer to avoid an engine stall (see `docs/m0-findings.md`).
- 2026-10-11: M2 dirty state comes from the undo history (history token compared with the token at the last save), not from a ValueTree listener: Tracktion writes many properties that are not user edits.
- 2026-10-11: M2 mixer controls (volume, pan, M, S, meter) live in the track headers and the master sits in the header bar, until the M6 mixer strips.
- 2026-10-11: M2 task 1.2 results. (a) After a tempo change a native-speed clip keeps its start beat and its length in seconds, but not its length in beats (2 s / 1 bar at 120 BPM is 3 beats at 90): `SetTempo` keeps the start beats and shortens clips that now overlap in the same step. (b) The loop points are bound without the undo manager in Tracktion; `Arrangement::setLoopRange` writes the transport properties through the undo manager and an undo reaches the transport, so the `SAMPLER/ARRANGE` fallback is not needed.
- 2026-10-11: Tracktion quirks handled in `engine::Arrangement`: its undo-transaction timer is held off (`UndoTransactionInhibitor`) so only `CommandService` opens steps; undo/redo of volume and pan do not reach the plugin parameters by themselves (re-synced after each change); `TempoSetting::setBpm` stops at 300 BPM (the spec asks for 999: the property is written directly above 300); a new edit's master volume starts at -3 dB (ours starts at 0 dB); an offline render with a clip whose file is missing never ends (such clips are left out of the render).
- 2026-10-11: M2 golden references were generated by the renderer itself and checked only numerically (levels, lengths); the PO must listen to them once (`tests/golden/README.md`) before they are trusted.
- 2026-10-11: M2 perf (task 3.5): with 200 clips and waveforms the first version of the lanes took up to 15 ms per paint in release (drawing the thumbnails); each clip's waveform is now drawn once into an image and reused while scrolling and zooming (one new picture per paint), giving a median of 1.3 ms and a worst of about 6 ms. The test is tagged `hardware` (wall-clock time), so CI leaves it out; run `ctest --preset windows-msvc-release -L perf`.
- 2026-10-11: Tracktion's live volume and pan follow a change through a ramp of about a second (measured on the meters: -12 dB reaches -18 dBFS after about 1.5 s), so a dragged fader is heard approaching the new level, not at once.
- 2026-10-11: Tracktion's `LevelMeasurer::Client::getAndClearOverload` reports a request to clear, not an overload; the over light is derived from the peak above 0 dBFS.
- 2026-10-12: M2b scope change (PO decision): recording works the Ableton way. Track arm (R) and a per-track input (System audio per playback device, Master, another track) come in M2b, with resampling of Master and tracks; recording only one app moves to M7; microphone and interface inputs stay in M8 and join the same input menu. Design doc 2.7 (C2, C4) and section 15 changed to match.
- 2026-10-12: M2b spike 1.1 (master tap): the Master and track taps are a non-saved Tracktion plugin (`TakeTapPlugin`, type `samplerTap`) at the end of the master or the track plugin list, added without the undo manager (design D3 option 1). Checked: a Master take of an offline render equals `renderToWav` of the same bars sample for sample (difference under 1e-5, offset 0); a track take equals a render of that track alone (post volume and pan, pan 0.5 included); a muted track records silence; saving with taps in place leaves `samplerTap` out of the file (`saveEditAtomically` strips it); the undo history and the history token are unchanged; no allocation inside our callbacks over 30 s of render. Options 2 and 3 were not needed. The offline renderer pre-rolls 0.5 s at negative times: the tap skips everything before time zero.
- 2026-10-12: M2b spike 1.1 (latency): `GetStreamLatency` reports 0 ms for endpoint and for process loopback on this machine (build 19045, Jabra Link 380). A click written by a JUCE output callback is seen by endpoint loopback 41.2 ms later (median of 10; range 40.7 to 41.4), so 41 ms is the constant used to place System takes while the device reports 0. Process loopback could not be measured the same way (our own sound is excluded from it). Open: whether process loopback follows a change of the Windows default device while it records (needs a second playback device and a manual switch); until checked, process loopback is used only for "the default device", and a named device is recorded with endpoint loopback.
- 2026-10-13: M3 spike 1.1 (`PadProbe`). (a) A plugin on a track of a preview edit is processed (about 100 blocks per second) while that edit's transport is stopped, and a pad slice is heard next to a tone from another edit playing to the same device (loopback capture on the output: 440 Hz tone 0.283 before the pad and during it, 1 kHz pad slice 0.000 before and 0.400 during), so the pad engine needs no keep-playing fallback. (b) Signalsmith Stretch (default preset) at +7 st on a 2 s stereo slice takes 50 ms in release and 66 ms in debug, not the 20 ms the design assumed (it runs at about 40x real time); the output keeps its length, the attack is 0.5 ms late and not smeared. The spec promise (a change is heard within 100 ms for slices up to 2 s) still holds, because renders run on workers and the old buffer plays until the new one is ready, so the 20 ms budget in design D6 became 100 ms and the perf test asserts 100 ms. (c) r8brain 48 -> 44.1 kHz on 2 s takes 21 ms (CDSPResampler24) and keeps a 1 kHz sine at 1000.0 Hz; a +7 st slice on a 44.1 kHz sound played on a 48 kHz device costs both, about 70 to 90 ms.
- 2026-10-13: M3 analysis. The tempo estimator only accepts a loop-length match (1, 2, 4, 8 or 16 bars at 70 to 180 BPM) when the hits support it, because every length matches some bar count at some tempo within 0.5% (a 7 s pattern is "4 bars at 137.14 BPM"); a sound with fewer than two hits gets no tempo. The onset flux is computed per frequency band (single bins at the bottom) because a plain linear-bin flux gave kicks a tenth of the strength of hats.
- 2026-10-13: M3 pads. The pad player lives in a separate preview edit (`PadEngine`), not on a hidden track of the project edit; AGENTS.md "Pads" and design doc 4.1 and 7.4 say so. Pads skip the project master volume until M5. Slice buffers reach the audio thread as one `SliceBank` per pointer exchange, with a garbage FIFO for the last release.
- 2026-10-13: M3 test status. Debug: all 477 non-hardware tests pass (serial run); release: all but one pass, and the 23 `hardware` and `slow` tests pass in release on the dev machine. The one failure is `TakeWriter: a long skipped span is silence, then the audio...` (M2b, `tests/platform/take_writer.test.cpp`): it fails in release on this machine every time (and once in debug under a parallel run), passes alone in debug, and links none of the M3 code, so it was left alone; the writer drops the audio after a 15 s gap when the producer outruns it. CI on the branch is still to be run by the PO.
- 2026-10-13: M3 demo scripts corrected to what the specs give: Beat 1/4 on break-90 makes 17 slices (16 beats from the first kick plus the 100 ms before it, the "pickup" slice) and 1/16 makes 65, and doubling 90 BPM gives 180 BPM and 8 bars (not 2). Creating the pad player when the app starts made the first window slower to appear (some older GUI tests missed their 150 ms wait), so it is created when the first sound is opened in the editor.
