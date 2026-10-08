# Design

## Context

No source exists yet. The authoritative design is `technical design/technical-design.md` (v2); `AGENTS.md` holds the standing decisions (C++20, JUCE 8, Tracktion Engine 3, Windows only, WASAPI shared, ASIO deferred). Motivation: see proposal.md. M0 is both a risk spike and the creation of the build skeleton. Dev machine: Windows 10 22H2 build 19045, so WASAPI process loopback is probably unsupported.

## Goals / Non-Goals

**Goals:**
- Answer each "verify at M0" question with evidence and a recorded decision.
- Leave behind a build skeleton (CMake, presets, layout, tests, CI) that M1 keeps.
- Keep spike code in the intended module layout where cheap, so findings transfer.

**Non-Goals:**
- Production quality UI, `CommandService`, undo, import pipeline, analysis, slicing, pads, installer.
- macOS, ASIO, out-of-process plugin scanning (the spike loads test plugins by fixed path).
- Making spike code tidy. Throwaway is acceptable; findings are the deliverable.

## Decisions

1. **One spike app plus small probes, not one big app.** `apps/spike` (JUCE GUI app with tabs: Player, Timeline, Plugin) and a console `capture-probe` target. Probes isolate failures. Alternative: a single app with everything; rejected because a capture or plugin crash would block the other checks.

2. **Layout follows AGENTS.md from day one** (`src/platform/windows`, `src/engine`, `src/ui`, `tests/`), with targets linked in the documented direction. Spike-only code sits in `spike/`. This lets the `ICaptureSource` shape and the "Tracktion only in engine/commands" rule be tested early.

3. **Build setup.** CMake 3.28 + Ninja, `CMakePresets.json` with `windows-msvc-debug/release`; JUCE and Tracktion as git submodules pinned by commit; static MSVC runtime; `/O2` on `tracktion_engine` and `src/engine` in debug; warnings as errors on our targets only (not third-party). Catch2 via submodule. Alternative of FetchContent was rejected because the guide requires pinned submodules.

4. **Audio playback through Tracktion**: a `te::Edit` with `WaveAudioClip`s on WASAPI shared via the engine's device manager; MP3 through `JUCE_USE_MP3AUDIOFORMAT`; M4A tested through `WindowsMediaAudioFormat` with a handful of AAC/ALAC samples.

5. **Capture probe is raw WASAPI, outside JUCE's device layer** (as in AGENTS.md): event-driven loopback, MMCSS "Capture", `AbstractFifo` ring, writer thread to WAV, silence insertion from the QPC timestamps. Process loopback via `ActivateAudioInterfaceAsync` with `VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK`; on failure fall back to endpoint loopback and log which path ran. Alternative (JUCE loopback input) rejected: JUCE does not expose loopback.

6. **Timeline spike is a plain JUCE `Component`** drawing from a model with 4 tracks and 50 clips; waveforms from Tracktion `SmartThumbnail` (also exercises thumbnail redirection). Hit-testing, viewport math and drag-reorder index math are in plain classes with Catch2 tests, per the testing rules. Perf target: 30 fps or better under continuous zoom/scroll on the dev PC.

7. **Multi-out VST3 test with a fixed VST3 path** (stand-in for Maschine 3, which is not owned yet; the primary target is a JUCE test plugin built in this repo with 4 stereo outputs, optional extras are Surge XT and Kontakt 7 Player; Maschine-specific checks move to M5), scanning in process (spike only). Multi-out: wrap the plugin in a rack with 4 stereo output pairs; output tracks each hold a rack instance set to output N. Check "runs once" by counting `processBlock` calls (via a tiny wrapper plugin or the host's own counter) and by CPU comparison. If Tracktion racks instantiate the plugin per track, record failure and test the alternative of one shared instance feeding tracks through an `AudioProcessorGraph`-style tap. Offline render with Tracktion `Renderer`; compare against a real-time capture of the same range to check timing.

8. **Assumption checks are automated where possible.** Tempo-change, relative-path, custom-property round trip and cache-redirect checks are Catch2 tests (run headless, plugin-free), so they become regression tests for M1. Maschine, capture, UI perf are manual and logged in the findings doc.

9. **Benchmarks.** A console target times Rubber Band (via Tracktion) and Signalsmith stretch on a 4-bar loop and a 3-minute track, plus a prototype beat-preserving renderer (split at onsets, place segments, no stretch inside). Results go in the findings doc. This is a prototype, not the production `BeatsRenderer`.

10. **Findings doc is the exit gate.** `docs/m0-findings.md` has a table: item, method, result (pass/fail/partial), numbers, decision. Any fail has a decision, including "use custom-engine fallback for X".

## Risks / Trade-offs

- [Tracktion 3 / JUCE 8 API drift versus the design's assumptions] → pin versions, read the actual headers first, record differences in findings.
- [A third-party plugin refuses to load or needs activation] → log the exact failure; retest with the JUCE test plugin to separate host bugs from plugin quirks.
- [Stand-in does not exercise Maschine quirks (huge state, editor resize, drag-out)] → accepted; R14 stays open until the M5 acceptance test.
- [Process loopback unavailable on build 19045] → expected; endpoint loopback plus default-mute policy covers it; record it.
- [Debug-build performance of UI] → measure in the release preset as well; report both.
- [Scope creep into M1 work] → the non-goals list above; spike code isn't tidied.
- [Effort over 3 weeks] → timebox each probe to about 3 days; a probe that blows its box is reported as "inconclusive" with what was learnt.
