# Proposal

## What this means for the user (plain language)

After this change there is a first, rough, runnable version of the app on the developer's Windows PC. It is a test bench, not the finished product. With it the user can:

- Open a small window and play a WAV file and an MP3 file.
- Record 30 seconds of whatever the computer is playing (for example a YouTube video in the browser) and get it back as an audio file.
- See a timeline with 4 tracks and 50 audio clips, each showing its waveform. Scroll and zoom it, drag clips around, and drag whole tracks to reorder them.
- Load a multi-output VST3 instrument (a test instrument built in this repo, plus Surge XT or Kontakt 7 Player if installed; Native Instruments Maschine 3 is not owned yet), open its window, and play it in time with the app's play button at 120 BPM. Four stereo outputs of the instrument appear on four separate tracks. Save, close, reopen and find the instrument as it was. Export 16 bars to an audio file.
- Read a short written report that says, for each risky assumption in the design, "works" or "does not work, and here is what we do instead".

Nothing here is meant to be polished. The point is to find out early whether the chosen building blocks can do the job, before months of work depend on them.

## Why

The whole design rests on one third-party engine (Tracktion) behaving as the design document assumes, and on a third-party multi-output VST3 instrument loading inside the app. Those are the biggest risks (R2, R14). Maschine 3 is not owned yet, so a stand-in instrument covers the generic hosting risk now; Maschine-specific behaviour (very large state, editor resizing, pattern drag-out) is verified at M5. The design explicitly marks several assumptions "verify at M0". Finding out now costs 2 to 3 weeks; finding out at M3 would cost a rewrite. The repo has no source code yet, so M0 also creates the build skeleton every later milestone uses.

## What Changes

- Create the CMake/Ninja build with JUCE 8 and Tracktion Engine 3 as pinned submodules in `external/` (versions recorded in `external/VERSIONS.md`), the `src/` layout from the architecture guide, Catch2, and a Windows CI job. Green build on Windows 10/11 x64.
- A spike app that plays WAV and MP3 through Tracktion on WASAPI shared.
- A capture probe: WASAPI loopback (endpoint and process mode) recording 30 s to a WAV; report whether process loopback works on build 19045.
- A timeline spike component: 4 tracks, 50 clips with waveforms, scroll/zoom, clip drag, track drag-reorder.
- A multi-out VST3 spike (stand-in for Maschine 3): load VST3, open editor, host transport at 120 BPM, 4 stereo outputs through racks on separate tracks, save/reopen with state, offline render of 16 bars.
- Test every "verify at M0" assumption (tempo change keeps beat positions; relative paths survive moving a bundle; unknown `sampler_*` clip properties survive a round trip; proxies and thumbnails redirect into `cache/`; a multi-out rack runs the plugin once; M4A through the Windows media format; process loopback on build 19045).
- Benchmark Rubber Band and a prototype of the Beats renderer.
- Write `docs/m0-findings.md`: one pass/fail row per item and, for each failure, a written decision (including whether to use the custom-engine fallback for that area).

No **BREAKING** changes (no prior code).

## Capabilities

### New Capabilities
- `build-and-ci`: reproducible Windows build, pinned dependencies, test runner and CI.
- `audio-playback`: decoding and playing WAV/MP3 (and M4A coverage check) through the engine on the Windows audio device.
- `computer-audio-capture`: recording what the computer is playing via WASAPI loopback, including the process-loopback probe.
- `arrangement-timeline`: multi-track timeline view with waveform clips, scroll/zoom, clip drag and track reorder.
- `plugin-hosting`: loading a multi-out VST3 instrument (stand-in for Maschine 3), its editor, host-synced playback, multi-output routing, state save/restore and offline render.
- `project-bundle`: engine-document behaviours the design relies on (beat-stable tempo changes, relative paths, custom clip properties, cache redirection).

### Modified Capabilities
None (`openspec/specs/` is empty).

## Impact

- New top-level: `CMakeLists.txt`, `CMakePresets.json`, `external/` (submodules), `src/`, `tests/`, `.github/workflows/`, `docs/m0-findings.md`.
- Dependencies added: JUCE 8, Tracktion Engine 3, Catch2, Rubber Band, Signalsmith Stretch (benchmark only).
- Needs a JUCE test plugin built from this repo (multi-out instrument, 4 stereo outputs); Surge XT or Kontakt 7 Player are optional extra manual targets. Maschine 3 is not needed at M0 and is verified at M5.
- Out of scope: macOS (TODO-MAC), ASIO, plugin scanning UI, real commands/undo, the final look and feel, real import pipeline, anything from M1 onwards. Spike code may be thrown away or reshaped in M1; only the build skeleton and findings are kept.
