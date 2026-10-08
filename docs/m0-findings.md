# M0 spike findings

Result column: `pass`, `fail`, `partial` or `todo`. Every `fail` and `partial` needs a decision (a workaround, or "use the custom-engine fallback for this area").

Environment: Windows 10 22H2 build 19045. Pinned versions: see `external/VERSIONS.md`.

## Exit items from the roadmap (M0 row)

| # | Item | Task | Result | Numbers / notes | Decision |
|---|---|---|---|---|---|
| 1 | CMake + JUCE 8 + Tracktion 3 build on Windows | 1.2 | pass | Debug and CI build clean, no warnings | |
| 2 | CI green on Windows | 1.4 | pass | windows-latest, configure+build+ctest in 6m32s | |
| 3 | Play a WAV | 2.1 | todo | | |
| 4 | Play an MP3 (no encoder-delay gap) | 2.2 | todo | | |
| 5 | Capture probe: 30 s of browser audio via WASAPI loopback | 4.1, 4.2 | todo | | |
| 6 | Process loopback on build 19045 | 4.3 | todo | | |
| 7 | Own output not in the capture (mute policy) | 4.4 | todo | | |
| 8 | Timeline: 4 tracks, 50 clips with waveforms, scroll/zoom | 5.2 | todo | | |
| 9 | Timeline: clip drag | 5.3 | todo | | |
| 10 | Timeline: track drag-reorder | 5.3 | todo | | |
| 11 | Maschine 3 VST3 loads, editor opens | 6.1 | todo | | |
| 12 | Maschine plays from host transport at 120 BPM | 6.2 | todo | | |
| 13 | 4 stereo outputs on separate tracks via racks | 6.3 | todo | | |
| 14 | Rack runs the plugin once, not once per instance | 6.3 | todo | | |
| 15 | Save and reopen restores Maschine state | 6.4 | todo | | |
| 16 | Offline render of 16 bars (with Maschine) | 6.5 | todo | | |
| 17 | Offline render of 16 bars (plugin-free) | 2.4 | todo | | |

## "Verify at M0" assumptions

| # | Assumption | Task | Result | Numbers / notes | Decision |
|---|---|---|---|---|---|
| A1 | Clips keep beat positions on tempo change | 3.1 | todo | | |
| A2 | Relative paths survive moving a bundle | 3.2 | todo | | |
| A3 | Unknown `sampler_*` clip properties survive a round trip | 3.3 | todo | | |
| A4 | Proxies and thumbnails can be redirected into `cache/` | 3.4 | todo | | |
| A5 | Multi-out rack runs the plugin once | 6.3 | todo | | |
| A6 | `WindowsMediaAudioFormat` coverage of M4A | 2.3 | todo | | |
| A7 | Process loopback works on build 19045 | 4.3 | todo | | |
| A8 | Atomic save leaves the old file intact when interrupted | 3.5 | todo | | |
| A9 | Audio files are never modified | 3.6 | todo | | |

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

- Plugin hosting was in process; instability seen with Maschine: (todo)
- JUCE pinned to the commit Tracktion v3.2.0 pins (8.0.6 + 19 commits), not 8.0.15.
- Debug presets: `juce_recommended_config_flags` adds `/Od`, which cancels `/O2`; the engine target does not link it.
