# Design

## Context

- M0 left a working loopback capture: `src/platform/windows/WasapiLoopbackCapture.cpp` behind `platform::ICaptureSource`. It records the **default** render device, tries process loopback ("exclude this process tree") first and falls back to endpoint loopback, runs an MMCSS "Capture" thread into a 10 s `AbstractFifo` ring drained by a writer thread into a 32f WAV, fills loopback gaps with silence from QPC timestamps, and reports overruns, device loss and write failures. `tools/probes/CaptureProbe.cpp` and `tests/platform/capture.test.cpp` drive it. The app does not use it yet.
- `EngineHost::setOutputMuted` silences the device output below the edit (decision 2026-10-09), so the mute policy needs no new engine work.
- Tracktion gives every audio track a `WaveInputDevice` of type `trackWaveDevice` that carries the track's output (`AudioTrack::getWaveInputDevice()`), and `InputDevice::Consumer` / `addConsumer` delivers each block of an input device to a listener (`acceptInputBuffer`). That lets us listen to a track's output without changing the edit. There is no equivalent for the master output.
- M2 provides tracks, `engine::Transport`, `CreateClipsFromSources::place` (with the no-overlap rule), `SelectionModel`, and a disabled R button in `TrackHeader` (`src/ui/arrange/ArrangementView.cpp`). `model::Category::recording` and the browser's Recordings place exist.
- Constraints: OS code only in `src/platform/windows`; Tracktion only in `src/engine` and `src/commands`; workers never touch the edit; every project change through `CommandService`; our audio-thread code follows §5.1 (no allocation, locks or I/O).

## Goals / Non-Goals

**Goals:**
- One take pipeline (ring -> writer -> file -> finish) for all three sources, so trimming, limits, failure handling and tests are written once.
- Takes are created by our command, not by Tracktion's own recording, so the no-overlap rule, naming, Recordings and undo work like every other edit.
- Master and track takes are sample-exact against the transport; System takes are placed by clock and latency.
- The whole app-side flow is unit tested with fakes (no audio hardware).

**Non-Goals:**
- Tracktion's recording (`TransportControl::record`, `InputDeviceInstance` record targets): it writes its own files and creates clips itself, outside `CommandService`. Revisit at M8 for interface inputs if it fits better there.
- Input monitoring, count-in, punch, loop takes, one-app capture, pre-roll.
- Clock-drift correction between a capture device and the playback device (System takes play at their own rate, §6.4 Timing).

## Decisions

### D1. Three sources, one pipeline
```
 System audio (per device)          Track output (per source track)       Master
 WASAPI capture thread              Tracktion trackWaveDevice Consumer    master tap (D3)
        |  packets + QPC                 |  blocks + transport sample pos    |  blocks + pos
        v                                v                                   v
 +------------------------------------------------------------------------------------+
 |  TakeWriter (portable, src/platform/TakeWriter.*):                                 |
 |  SPSC ring (10 s) -> writer thread -> rec-<ts>-<n>.wav ; level + min/max taps;     |
 |  first-sample position ; optional leading/trailing trim ; frame limit ; errors     |
 +------------------------------------------------------------------------------------+
        |  finished take: file, frames, start (seconds on the timeline), stats
        v
 app::RecordingController -> commands::AddTakes (one undo step)
```
- `TakeWriter` takes interleaved or planar float blocks from one producer thread (capture thread or audio thread), never blocks the producer, and owns the WAV writer thread. The M0 ring/writer/gap-fill code moves into it; `WasapiLoopbackCapture` keeps only the device side and pushes packets.
- The producer reports where its first frame lies: for engine sources the transport position of the first block (sample-exact), for System the QPC time of the first packet (converted later, D6).
- Trim is per source: System uses leading live trim plus tail truncation (D5); engine sources are written untrimmed (a resample must start exactly on the record position). All sources detect "never above -60 dBFS" so silent takes are dropped.
- Alternative: Tracktion's recording for track/master takes and ours for System. Rejected: two file layouts, clips created outside our command, and a master input Tracktion does not have.

### D2. Track output listener (engine)
`engine::TakeRecorder` (Tracktion-free header) owns the engine side. For a track input it adds an `InputDevice::Consumer` to the source track's `trackWaveDevice`; `acceptInputBuffer` copies the block into the take's `TakeWriter` ring (no allocation: buffers sized in `prepare` from the device block size, oversize blocks split). The block's transport position comes from the playback context of the block being processed. What the device carries (post volume/pan, post mute) is checked in spike task 1.1 against the spec ("after its volume and pan"); if it is pre-fader, the listener applies nothing itself and we log the difference and choose (spec stays: the PO hears what is on the fader).

### D3. Master tap (spike)
Task 1.1 decides between, in this order:
1. **A non-saved tap on the master plugin chain**: our `MasterTapPlugin` (a `te::Plugin`, real-time code) inserted at the end of the edit's master plugin list on project open and removed before save, never through the undo manager. Gives the post-master-fader mix of the edit only (no metronome, no audition), with the transport position per block.
2. **A node in the playback graph** via Tracktion's graph builder hooks, if (1) leaks into the saved edit or the undo history.
3. **The device output processor** next to `OutputMuteProcessor`: simplest, but it includes the metronome and audition, and needs the transport position passed separately. Fallback only, with the click forced off while recording Master.
The spike measures: the take of a known render matches `sampler-cli render` of the same bars sample for sample (or within one block of offset); saving during a tap leaves no plugin in the file; undo history unchanged. The outcome is logged in the ROADMAP decisions and `docs/m0-findings.md`.

### D4. System audio: device choice, exclusion, mute
- `ICaptureSource` gains `CaptureOptions::deviceId` (empty = Windows default render device). New `platform::listPlaybackDevices()` returns active render endpoints `{id, friendlyName, isDefault}`.
- Default device: process loopback in exclude mode first, endpoint loopback as fallback (M0 behaviour). Other device: endpoint loopback of that device (process loopback follows the default device, to be confirmed in task 1.1).
- Mute policy (`RecordingPolicy`, plain logic): mute when any System take runs in endpoint mode on the device the engine plays to (`EngineHost::currentAudioDeviceName()` vs the endpoint friendly name; on doubt, mute). Applied after all sources started, cleared on stop, failure and shutdown.
- Several tracks on the same System device share one capture and one take file (two clips, one sound). Different devices run separate captures.
- A track input stored as `system:default` follows the Windows default device; `system:<endpoint id>` names one device and falls back to default if missing.

### D5. Trim without a second pass (System only)
- **Leading, live**: the capture thread pushes nothing until the first frame above -60 dBFS, keeping a 5 ms pre-margin; gap silence before it is not inserted; `leadingTrimFrames` is reported.
- **Trailing, at stop**: the writer tracks the last loud frame; after it closes the WAV, `trimWavTail(file, lastLoud + margin)` rewrites the `data`/`RIFF` sizes (and `ds64` for RF64) and truncates. It accepts only the layout JUCE's `WavAudioFormat` writes, else leaves the file untrimmed and logs. The file is not part of the project yet, so the "never modified" rule holds.
- Alternative: copy the loud part to a new file after stop. Rejected: a 30-minute stereo 48 kHz take is about 690 MB to read and write again.

### D6. Placing takes
Each finished take carries its start in timeline seconds:
- **Engine sources**: the transport position of its first block (exact). With looping on, later blocks keep going linearly (the writer does not care about wraps), so the take is one continuous clip from the start.
- **System**: `startSeconds = transportSecondsAt(firstPacketTime) - streamLatency`, where `transportSecondsAt` maps a `Time::getMillisecondCounterHiRes` moment to the timeline from (transport position, clock) pairs the controller samples each frame while recording; `firstPacketTime` is the QPC time of the first kept frame (after the leading trim); `streamLatency` comes from `IAudioClient::GetStreamLatency` (process loopback may report 0: then the measured value from task 1.1 is used as a constant).
- Beat = `Arrangement` seconds-to-beats at the current tempo (tempo edits are refused during recording, spec), not snapped, clamped at 0.
- `commands::AddTakes(std::vector<Take>)`, named "Record take" / "Record N takes": for each take adds a `recording`-category source (`original` = `decoded` = the take's relative path, `recordedFrom`, `recordedAt`) and places its clip on its track with `CreateClipsFromSources::place` (the overlap rule splits or shortens what was under it). One transaction.

### D7. Track inputs in the model
A track property `sampler_input` on Tracktion's track node (`system:default`, `system:<id>`, `master`, `track:<trackId>`), unknown or missing = `system:default` (M0 finding: unknown `sampler_*` properties survive). `commands::SetTrackInput` ("Set track input"). `DeleteTracks` also resets inputs that pointed at a deleted track, in the same step. Arm state lives in `SelectionModel`-like `ArmState` on the UI side (ids, not undoable, not saved; pruned when tracks go away).

### D8. RecordingController (app layer)
`src/app/RecordingController`, message thread only, created with factories for `ICaptureSource`, the device list and `TakeRecorder`, so tests pass fakes. States `idle -> starting -> recording -> finishing -> idle`.
- **start** (Record / Mod+R): refuse if nothing armed or an armed track records another armed track; resolve each armed track's input; open one take per source (`audio/recordings/rec-YYYYMMDD-HHMMSS-<n>.wav`); start System captures first (they can fail; any failure aborts everything, deleting files); then start the transport if stopped and attach engine listeners; apply the mute policy.
- **recording**: the VBlank tick reads each take's level and min/max ring for the growing clips and meters, samples (transport position, clock) for D6, checks for self-ended captures (device lost, limit) and finishes the recording if one ended.
- **finishing**: detaching listeners and joining capture/writer threads runs on a worker; results come back via `callAsync`; silent takes are deleted and reported; the rest go into one `AddTakes`.
- `CommandService` gets a "recording lock": while not idle, `apply` refuses everything except the mixer commands (`SetTrackParam`, master volume), and undo/redo are refused; the UI shows the message. `AppController::guardUnsaved` gains "finish the recording first" for New/Open/Quit; Save As and an untitled Save are refused like `refuseWhileImporting`.

### D9. UI
- `TrackHeader`: the R button becomes active (lit when armed; click = exclusive, Mod-click = toggle). An input label under the track name ("In: System (Speakers)", "In: Master", "In: Drums"), clicking it opens the input menu (devices from `listPlaybackDevices()` with the default marked, Master, other tracks). Exact layout settled in demo D1 against the mock's header height.
- `TimelineLanes`: a `TakeOverlay` draws each armed track's growing take from the record start to the playhead, filled red-tinted, with a waveform from the take's min/max ring drawn column by column into an image (the M2 per-clip image approach), and the no-signal warning text on it.
- Track meter shows the take's input level while recording (instead of the track output).
- Header Record button enabled, lit while recording.

## Risks / Trade-offs

- [The master tap leaks into the saved edit or undo history] → Spike 1.1 checks save and undo with the tap in place; options 2 and 3 in D3.
- [The track wave device carries pre-fader audio] → Spike 1.1 measures it; spec says post volume/pan, so a mismatch is fixed or raised with the PO before D3 work starts.
- [Real-time code in `acceptInputBuffer` / the tap plugin allocates or blocks] → Preallocated rings, debug `operator new` hook asserts in our audio code (§5.1), a test runs a 30 s render with the listener attached under that hook.
- [Process loopback does not follow the default device on 19045] → Task 1.1 checks it; if not, non-default and default both use endpoint mode for named devices, and exclusion only applies to `system:default`.
- [System take placement is off by the browser's own output latency] → Accepted (§6.4 Timing); the clip can be nudged. The spec allows "a few milliseconds".
- [Recording refuses most edits, which is stricter than Ableton] → Simple and safe for the first version; mixer moves (the common case while resampling) stay allowed. Can be relaxed later.
- [Stopping blocks while writers drain] → Done on a worker; the growing clips show "Finishing..." until the takes land.

## Migration Plan

None: no schema change. Tracks without `sampler_input` read as `system:default`, so M2 projects open unchanged and not dirty. Rollback is reverting the branch; projects with takes open in M2 builds (takes are Recording-category sounds and normal clips; the unknown track property is ignored).
