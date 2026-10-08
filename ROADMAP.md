# Roadmap and progress

Source: `technical design/technical-design.md` §15 (the design doc stays authoritative for scope; this file tracks progress).
Platform: Windows only for now (TODO-MAC). The MVP is M0 to M4.

**Current milestone:** M0 Spike
**Last updated:** 2026-10-08

Legend: `[ ]` not started, `[x]` done and verified. Tick a scope item only when it works and was checked; tick the exit criterion only when it was demonstrated. Put the OpenSpec change name in the `Change:` line when a proposal exists.

---

## M0 Spike (2 to 3 wks)
Change: `m0-spike` (22 of 32 tasks done; progress in `openspec/changes/m0-spike/tasks.md`, results in `docs/m0-findings.md`)

- [x] CMake + JUCE 8 + Tracktion 3 build on Windows, CI green
- [x] Play a WAV and an MP3 (MP3 encoder delay is not trimmed by the engine: fix is in M1, see below)
- [x] Capture probe: record 30 s of a YouTube video via WASAPI loopback
- [x] Test whether process loopback works on build 19045 (it works on 19045)
- [x] Timeline component: 4 tracks, 50 clips with waveforms, scroll/zoom, clip drag, track drag-reorder
- [ ] Stand-in for Maschine 3 (not owned yet): a multi-out VST3 instrument (our JUCE test plugin with 4 stereo outputs, plus Surge XT or Kontakt 7 Player if installed). Check these with it:
  - [ ] Loads and its editor opens
  - [ ] Plays from host transport at 120 BPM
  - [ ] 4 stereo outputs on separate tracks via racks
  - [ ] State saves and reopens
  - [ ] Offline render of 16 bars
- [x] Verify: clips keep beat positions on tempo change
- [x] Verify: relative paths survive moving a bundle (rule: save the edit file before adding clips)
- [x] Verify: unknown `sampler_*` clip properties survive a round trip
- [x] Verify: proxies and thumbnails can be redirected into `cache/`
- [ ] Verify: a multi-out rack runs the plugin once, not once per instance
- [x] Verify: JUCE `WindowsMediaAudioFormat` coverage of M4A (result: not supported; M4A descoped, not a priority)
- [ ] Benchmark Rubber Band and the Beats renderer
- [x] Pin versions in `external/VERSIONS.md`
- [ ] **Exit:** all items pass, or a written decision to use the custom-engine fallback for each failing area

Still open in M0: the multi-out VST3 stand-in checks, the multi-out rack check, the Rubber Band and Beats benchmarks, and the exit write-up (tasks 6.x and 7.x).

## M1 Core + import (3 wks)
Change: _none yet_

- [ ] App shell, `SamplerLookAndFeel` and widget set
- [ ] Bundle new/open/save
- [ ] `CommandService` + undo
- [ ] Import pipeline
- [ ] MP3 gapless trim at decode: read delay/padding from the LAME tag, cut start `delay + 529 + 1` and the padding (no tag: assume 1105 at the start). The engine does not trim (M0 finding); test with a real LAME file
- [ ] Browser with search and categories
- [ ] Thumbnails
- [ ] Audition
- [ ] **Exit:** import 5 formats, save/reopen, undo works, waveforms drawn

## M2 Arrangement (4 to 6 wks)
Change: _none yet_

- [ ] Timeline editing: move, trim, split, duplicate, delete, snap, marquee
- [ ] Timeline scroll bar and no-overlap rule when dropping clips (M0 spike has wheel scrolling only and lets clips hide each other)
- [ ] Track add, delete, drag-reorder
- [ ] Loop brace
- [ ] Mixer strips (volume, pan, mute, solo) and meters
- [ ] Metronome and position display
- [ ] Golden tests
- [ ] **Exit:** build a 16-bar arrangement like the mock with 4 tracks; golden render tests pass

## M2b Capture (2 to 3 wks)
Change: _none yet_

- [ ] `CaptureService`
- [ ] WASAPI endpoint + process loopback
- [ ] Capture source menu
- [ ] Meter and live waveform
- [ ] Exclusion/mute policy
- [ ] Silence trim
- [ ] Recordings category
- [ ] Place on timeline
- [ ] **Exit:** record 1 minute of a YouTube video; it appears in Recordings and as a clip at the playhead, with no DAW sound in it

## M3 Sample editor (4 to 5 wks)
Change: _none yet_

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
- [ ] Plugin windows (floating + docked)
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
- [ ] User warp markers
- [ ] Crossfades
- [ ] Adaptive snap
- [ ] FLAC + LUFS export

### M8 Input recording (2 to 3 wks)
Change: _none yet_
- [ ] Mic/line-in and MIDI recording into arrangement/slots
- [ ] Track arm
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
