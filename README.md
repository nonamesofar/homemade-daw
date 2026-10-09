# Sampler

Working name for a hobby desktop DAW (digital audio workstation) built around sampling. Windows only for now.

> **Status:** milestone M0 (technical spike) is done. M1 (core app and import) is next. There is no usable app yet; today the repo holds the build skeleton, a spike app, probes and tests. Progress is tracked in [ROADMAP.md](ROADMAP.md).

## What it is

Sampler is for making music out of existing audio. You bring in a recording, chop it into pieces, play the pieces from pads, lay them out on a timeline and export the result.

The intended workflow:

1. **Get audio in.** Import a file (WAV, MP3, FLAC, OGG), or **record whatever your computer is playing**, for example a YouTube video in the browser.
2. **Chop it.** In the sample editor, cut the audio into slices by beat, by transient (detected hits) or by hand.
3. **Play it.** Trigger the slices from a 4x4 pad grid, with the computer keyboard or later with MIDI.
4. **Arrange it.** Place clips on several tracks on a timeline, with a mixer for volume, pan, mute and solo.
5. **Add instruments.** Host third-party VST3 plugins. Native Instruments **Maschine 3** is the acceptance test.
6. **Export.** Render to WAV or MP3, or export each track as a stem.

## Main features

| Feature | Status |
|---|---|
| Import WAV, MP3, FLAC, OGG | planned (M1) |
| Non-destructive: original audio is never modified, every edit is project data | planned (M1) |
| Multi-track arrangement timeline | planned (M2) |
| Capture computer audio (WASAPI loopback), excluding the app's own sound | planned (M2b); proven in the M0 probe |
| Sample editor with beat, transient and manual slicing | planned (M3) |
| 4x4 slice pads | planned (M3) |
| Tempo-aware playback: re-pitch, beat-preserving and tonal warp modes | planned (M4) |
| WAV, MP3 and stem export | planned (M4) |
| VST3 plugin hosting, including multi-output instruments | planned (M5); proven with a test plugin in M0 |
| Session view (clip launcher) | planned (M6) |
| Input recording, effects, automation, tempo map | later (M8 to M10) |

M4A import was descoped. macOS support is on hold until a Mac is available.

## Tech overview

- **Language and UI:** C++20, with [JUCE 8](https://juce.com) for the app and a custom-drawn interface.
- **Audio engine:** [Tracktion Engine 3](https://github.com/Tracktion/tracktion_engine) provides the project model, transport, clips, mixer, MIDI, plugin hosting, rendering and undo.
- **Our layer on top:** the sample library, onset and BPM analysis, slicing, pads, a beat-preserving warp renderer and a command API. All edits to the project go through that command API, which gives undo and keeps the UI free of engine details.
- **Audio drivers:** WASAPI. ASIO is deferred.
- **Stretching and rendering:** Rubber Band (through Tracktion) and Signalsmith Stretch.
- **Projects:** a `.sdaw` folder holding the project file (Tracktion XML plus our own data), the audio and a disposable cache.
- **Tests:** Catch2, driven by CTest.
- **Plugins:** run inside the app process. Scanning runs in a separate process with a timeout and a blacklist.

Source layout, in dependency order: `analysis` and `render` (pure DSP), `model`, `engine`, `commands`, `io`, `ui`, `app`. Operating-system code lives only in `src/platform/`.

The full design is in [technical design/technical-design.md](technical%20design/technical-design.md). Rules for contributors and coding agents are in [AGENTS.md](AGENTS.md).

## Prerequisites

- Windows 10 or 11, x64
- Visual Studio 2022 or newer, or its Build Tools, with the **Desktop development with C++** workload
- CMake 3.28 or newer
- Ninja
- Git, with Git LFS (golden test audio is stored in LFS)
- An audio output device. Some tests play sound and record it back.

## Build

Clone with submodules (JUCE, Tracktion Engine, Catch2 and the stretch libraries are pinned in `external/`; versions are in [external/VERSIONS.md](external/VERSIONS.md)):

```
git clone --recurse-submodules <repo-url>
```

The easiest route is the script. It finds the newest Visual Studio, sets up the environment, then configures, builds and tests:

```
powershell -File scripts/build.ps1 -Preset windows-msvc-debug -Test
```

Or from an "x64 Native Tools Command Prompt for VS":

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Use `windows-msvc-release` for an optimised build. Two groups of tests make noise or open windows (`realtime` plays and records sound, `gui` clicks in a plugin window). To leave them out:

```
ctest --preset windows-msvc-debug -LE "realtime|gui"
```

The spike app is built to `build/windows-msvc-debug/apps/spike/SamplerSpike_artefacts/Debug/Sampler Spike.exe`. More build detail is in [docs/building.md](docs/building.md).

## Documentation

- [ROADMAP.md](ROADMAP.md): milestones and progress
- [docs/m0-findings.md](docs/m0-findings.md): results of the M0 technical spike
- [technical design/](technical%20design/): the design and the stack comparison
- [concepts/](concepts/): HTML mock-ups of the intended screens (drawings only, open `concepts/index.html`)
- [openspec/](openspec/): specs and change history

## License

A private hobby project. If it is ever published it must be released under the AGPLv3, because it builds on JUCE (AGPLv3) and Tracktion Engine (GPLv3).
