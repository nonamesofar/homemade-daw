# Sampler DAW: Technology Comparison (Proposed Rust/Tauri stack vs C++)

| | |
|---|---|
| Status | Draft v2 (2026-10-08). **Option B accepted** and applied in `technical-design.md` v2 |
| Compares | `technical-design.md` (Tauri 2 + Rust core + React/TS UI) against C++ alternatives |
| New hard requirements | (1) everything in `technical-design.md` incl. the post-MVP path, (2) drag-based editing (drag clips, drag tracks to reorder, drag from browser/OS, drag out), (3) access to many third-party plugins and libraries, (4) **host Native Instruments Maschine as a plugin**, plus other VSTs |
| Clarification (v2) | The files in `concepts/` are **visual drawings only**. They define layout and behaviour, not technology. Being written in HTML gives web UI no advantage, and the UI can be built with any toolkit |

---

## 1. Summary

**Requirement (4), Maschine as a plugin, decides the native side.** The current design puts MIDI at M10 and plugin hosting at M11, at the very end of the roadmap. It also treats hosting as greenfield work in Rust. To host Maschine, plugin hosting, MIDI routing, multi-output buses and host transport sync become core requirements. These are the areas where the C++ ecosystem (JUCE, Tracktion Engine, the Steinberg VST3 SDK itself) is mature and the Rust ecosystem is not.

**The clarification about the mocks decides the UI side.** The main reason for a web UI was "it matches the HTML mocks". With that gone, a webview only adds an IPC layer, a mirrored copy of the document, a telemetry channel and a second language. A native UI in the same language and process as the engine is simpler.

| Option | Verdict |
|---|---|
| **A. Proposed: Tauri 2 + Rust core + React UI** | Good for the original audio-only MVP. **Weakest for plugin hosting**, because Rust VST3 host crates are young and lightly tested on Windows. Its web UI is no longer an advantage |
| **A2. Rust core + native Rust UI (egui / iced / Slint)** | Removes the webview and IPC, but **does not fix plugin hosting**, which is the deciding problem. Rust GUI toolkits are also less proven for DAW-style UIs |
| **B. Pure C++: JUCE 8 (+ Tracktion Engine), JUCE-drawn UI** | **Recommended.** One language and one process. Mature in-process VST3/AU hosting, MIDI and multi-out. Plugin editors can be docked in the app, and drag in/out of the app is built in. Cost: C++ memory-safety discipline, more verbose UI code than HTML/CSS, and JUCE/Tracktion licences if you ever sell the app |
| **C. C++ engine (JUCE) + React UI in a JUCE 8 WebView** | Only worth it if you strongly prefer web tooling for UI work. It keeps the IPC layer and two languages, and gains nothing from the mocks |
| **D. Proposed Rust core + separate C++ (JUCE) plugin-host process** | Workable, but means **three languages** and shared-memory audio between processes. Too much for a solo hobby project |

---

## 2. Maschine: what the host actually has to do

Maschine 3 ships as standalone, **VST3 64-bit, AU 64-bit and AAX** (no VST2, no CLAP). It runs on Windows 10+ and macOS 13+. So "host Maschine" means "be a solid VST3 host on Windows (and AU or VST3 on macOS)". Here is what a host needs for Maschine to work well:

| # | Host capability | Why Maschine needs it | In the current design? |
|---|---|---|---|
| M-1 | Load and run VST3 **instrument** plugins in the audio callback | Maschine is an instrument | No (M11) |
| M-2 | **Transport, tempo and position sync** (VST3 `ProcessContext`: playing, PPQ position, bar start, tempo, time sig, loop) | Maschine plays its own patterns in sync with the host song | Partly (engine knows tempo/position but does not expose it to processors) |
| M-3 | **MIDI in** to the plugin (note events from MIDI clips, a MIDI controller or the app's pads) | Trigger pads, scenes or patterns from the host | No (M10) |
| M-4 | **Multi-output buses**: up to 16 stereo outputs, activated on demand and routed to mixer channels | Separate Maschine groups/sounds onto separate DAW tracks for mixing and stems | No (fixed tracks -> master topology) |
| M-5 | **Plugin state** save/restore (`IComponent::getState/setState`, large binary chunks) in the project | The whole Maschine project lives inside the host project | No |
| M-6 | **Editor window** (`IPlugView` attached to an HWND/NSView), resizable, HiDPI-aware | Maschine's UI is large and resizable | No |
| M-7 | **Plugin delay compensation** | Effects inside Maschine report latency | Seam only (5.2) |
| M-8 | **Offline render with plugins** (VST3 `kOffline` process mode) | Export and stems must include Maschine | No: export assumes prepared PCM only |
| M-9 | Accept **drag-out** from the plugin window (Maschine drags patterns out as audio or MIDI files) | A common Maschine workflow | Audio: yes (OS drop). MIDI: no (needs MIDI clips) |
| M-10 | Hardware controller | NI controllers talk to the plugin through NI's own background service, not through the host, so no host work is needed beyond keeping the plugin instance alive | n/a |

**Consequence for the architecture in either language:** the principle "the engine plays prepared PCM only" (section 1) still holds for audio clips. But tracks now also need **real-time processor chains** (instruments and effects), **MIDI tracks/clips**, **multi-output routing** and **PDC**. Export can no longer be bit-identical, because third-party plugins are not deterministic. Golden tests stay valid for projects without plugins.

---

## 3. Head-to-head by requirement

Rating: ●●● mature and ready, ●●○ workable with effort, ●○○ immature or a lot of custom work.

### 3.1 Plugin hosting (Maschine and other VSTs)

| Item | A / A2. Rust | B. C++ JUCE (+ Tracktion) |
|---|---|---|
| VST3 hosting | ●○○ `vst3-host` (0.5) and `rack` (0.4; its VST3 path is described as "working on macOS, untested on Windows/Linux"), or raw `vst3` bindings plus your own host. The VST3 SDK is MIT since 3.8, so bindings are legally fine. Expect to debug bus arrangements, `IPlugView` sizing, parameter threading and state chunks yourself | ●●● `AudioPluginFormatManager` / `VST3PluginFormat`, used in production by many commercial hosts. Plugin scanning, the out-of-process scanner, bus layouts, state and editors are all included |
| AU (macOS) | ●●○ `rack`'s AU path is described as production-ready | ●●● `AudioUnitPluginFormat` |
| CLAP | ●●○ `clack` (host side) is active | ●●○ via the `clap-juce-extensions` community work, or Tracktion support. Maschine does not ship CLAP anyway |
| VST2 (older third-party plugins) | ●○○ | ●○○ in practice. JUCE can host VST2 only if you hold the legacy VST2 SDK headers, which Steinberg stopped licensing to new developers in 2018. Treat VST2 as unsupported in both stacks |
| ARA 2 (Melodyne-style plugins) | ●○○ none | ●●● JUCE supports ARA hosting |
| Multi-out buses (M-4) | ●○○ build it | ●●● JUCE bus layouts; Tracktion has rack/output routing |
| Plugin editor windows | ●●○ Tauri: always a separate native window. egui/iced: separate window or child-HWND embedding, custom work | ●●● Separate windows, or **docked inside the app** as a child component (a JUCE UI can host native plugin views) |
| Crash isolation | Planned separate host process (complex) | In-process by default (a plugin crash takes the app down, like most DAWs). Out-of-process scanning is built in, so a bad plugin cannot crash the scan |
| Effort to Maschine-ready (M-1 to M-8) | **~14 to 20 weeks** on top of the MVP, with high uncertainty from the crates | **~4 to 8 weeks** with Tracktion Engine (most of M-1 to M-8 exists); **~8 to 12** with plain JUCE |

### 3.2 Everything else in the technical design

| Area (design section) | A. Rust / Tauri | B. C++ JUCE (+ Tracktion) | Edge |
|---|---|---|---|
| Audio I/O, WASAPI/ASIO/CoreAudio (T5) | ●●● cpal; ASIO needs LLVM + ASIO SDK | ●●● `AudioDeviceManager` with WASAPI shared/exclusive, ASIO, CoreAudio and a ready-made settings component (G12) | B, slightly |
| RT safety (5.1) | ●●● The compiler enforces thread safety; `assert_no_alloc` | ●●○ Discipline only. Data races and use-after-free are possible; use sanitizers (ASan/TSan) in CI | **A** |
| Decoding formats (T6) | ●●● Symphonia, pure Rust | ●●○ JUCE reads WAV/AIFF/FLAC/Ogg and MP3 (via Media Foundation on Windows / CoreAudio on macOS); M4A/AAC via the OS. Fine on Windows and macOS | A, slightly |
| Resampling / stretch (T7, T8) | ●●● rubato + Signalsmith via a C++ wrapper | ●●● Signalsmith is native C++. Tracktion has a time-stretch abstraction (SoundTouch, Rubber Band, élastique) | B, slightly (no FFI) |
| Onset/BPM analysis (T10) | ●●● realfft, own code | ●●● JUCE FFT or pffft, own code | Even |
| MP3/WAV export (T11) | ●●● hound, LAME | ●●● JUCE `WavAudioFormat`, `LAMEEncoderAudioFormat` (wraps the LAME exe) or LAME linked | Even |
| Arrangement model, undo (T15, section 8) | ●●● `im` persistent snapshots, very clean | ●●○ Tracktion `Edit` + `juce::ValueTree` + `UndoManager`, ready made but inverse-op style. Your own model is also possible | A for cleanliness, B for speed |
| Warp, clips, loop, fades, metronome, offline render | ●●○ All custom (~3k LOC engine) | ●●● Tracktion Engine has audio clips, warp, loop, fades, click track and offline rendering | **B** |
| MIDI (M10) | ●●○ `midir` for I/O; MIDI clips, sequencing and the plugin bridge are custom | ●●● `MidiInput`/`MidiOutput`; Tracktion MIDI clips | **B** |
| Effects and automation (M8) | ●●○ Custom DSP and automation lanes | ●●● JUCE `dsp` module (EQ, compressor, reverb, delay); Tracktion automation curves | **B** |
| Recording (M7) | ●●○ Custom | ●●● Tracktion recording with latency compensation | **B** |
| Session view, clip launching (M5) | ●●○ Custom | ●●○ Tracktion has clip-launcher support in recent versions (verify at spike); otherwise custom | Even to B |
| Third-party libraries in general | ●●○ Good crates for DSP and I/O; C/C++ libraries via FFI (`cc`, `cxx`, `bindgen`) | ●●● Every audio library is written for C/C++ first (Ableton Link, ARA, ONNX Runtime for stem separation, VST3/CLAP/AAX SDKs) | **B** |
| Installer size, cold start | ●●● ~10 MB, WebView2 shared | ●●● ~10 to 20 MB, no webview dependency | Even |
| Testing | ●●● `cargo test`, `proptest`, headless CLI | ●●○ Catch2/GoogleTest, headless via Tracktion offline render. Slightly more setup | A |
| Build friction on Windows | ●●○ Rust + MSVC + Node; ASIO feature needs LLVM | ●●● CMake + MSVC only (no Node toolchain) | B, slightly |
| Learning curve | Rust ownership/borrowing (R9) + React | C++ plus JUCE and Tracktion APIs. Tracktion has limited docs and its examples are the main reference | Depends on your background (Q3) |

### 3.3 UI toolkit and architecture (now an independent choice)

| Aspect | A. React in webview (Tauri) | A2. Rust native UI (egui/iced/Slint) | B. JUCE Components |
|---|---|---|---|
| Building panels, lists, dialogs, inspectors | ●●● fastest (HTML/CSS, hot reload) | ●●○ egui is quick but looks generic; Slint/iced are more styleable but younger | ●●○ More code per widget. `LookAndFeel` gives the mock's dark theme |
| Timeline and waveform drawing (custom painted in every option) | ●●● Canvas 2D (GPU) | ●●● wgpu-backed | ●●● `Graphics` with the Direct2D renderer on Windows (new in JUCE 8), CoreGraphics or OpenGL on macOS |
| Architecture overhead | **High**: IPC commands, JSON patches, doc mirror, binary telemetry channel, `peaks://` protocol (design 4.2, 10.2) | Low: same process, shared memory | **Low**: same process. Engine state readable from the message thread; Tracktion's `ValueTree` notifies UI of changes |
| Mock fidelity (colours, fonts, layout) | ●●● | ●●○ | ●●● All mock elements are rectangles, text, meters, waveforms and pads. Nothing needs a web engine |
| Ecosystem of DAW UIs built with it | Few | Very few | Many (Tracktion Waveform and many plugins) |

### 3.4 Drag-based editing

| Drag interaction | A. React in webview | B. JUCE Components |
|---|---|---|
| Move clips within/across tracks, trim edges, loop brace (A2, A5) | ●●● Pointer events + canvas overlay | ●●● `mouseDown/Drag/Up` on a custom timeline `Component` |
| **Reorder tracks by dragging the header** | ●●● DOM drag list + a `ReorderTrack` command | ●●● `DragAndDropContainer` / `DragAndDropTarget`, with an insertion marker you paint (~100 LOC) |
| Browser row -> timeline, slice -> timeline (A8, 10.6) | ●●● | ●●● Same `DragAndDropContainer` mechanism with a drag image |
| OS file drop into app (S4) | ●●● Tauri drag-drop event | ●●● `FileDragAndDropTarget` |
| **Drag out** of the app (clip/slice as WAV to Explorer or into another app) | ●●○ Needs a plugin (e.g. `tauri-plugin-drag`) | ●●● `performExternalDragDropOfFiles` |
| Drop from a **plugin window** (Maschine pattern drag-out, M-9) | ●●○ Audio works as an OS drop; MIDI needs MIDI clips | ●●● OS drop; MIDI clips exist with Tracktion |

---

## 4. Licensing (only matters if you distribute)

| Component | Licence | Notes |
|---|---|---|
| VST3 SDK | MIT (since 3.8, Oct 2025) | Fine for every option |
| JUCE 8 | AGPLv3 or a JUCE licence (Starter free up to $20k revenue, Indie $800, Pro $3,500) | A private hobby project is unaffected. Publishing open source means AGPLv3 |
| Tracktion Engine | GPLv3, or a commercial licence (free personal tier with a revenue cap; paid indie and enterprise tiers). Check current terms at engine.tracktion.com | **Requires a separate JUCE licence** for closed source |
| Rust crates in the design | MIT/Apache/MPL | Permissive |
| Maschine itself | NI EULA, installed by the user | The host never ships NI code. NKS browser integration needs NI partnership and is out of scope |

With the design's default (Q2: private hobby project, GPL if published), every option is compatible.

---

## 5. Risks per option

| Risk | A. Rust / Tauri | B. C++ JUCE |
|---|---|---|
| Maschine or other VSTs misbehave (bus layouts, editor sizing, state) | **High.** Young crates, few users, and you are the first to hit Windows bugs | Low. JUCE hosting is used by many shipping DAWs |
| Audio-thread memory bugs | Low (compiler-checked) | **Medium.** Mitigate with ASan/TSan builds, JUCE's lock-free FIFOs and `assert_no_alloc`-style checks |
| UI/engine IPC jank (R4) | Medium | None (same process) |
| UI build speed | Low risk (web tooling) | **Medium.** Widgets take more code. Mitigate with a small set of reusable components (knob, fader, meter, pad, toggle) built at M1 |
| Tracktion Engine learning curve and opinionated model | n/a | Medium. You adopt its `Edit` model instead of the design's `im` snapshot model. Mitigation: wrap it behind the same Command API (section 8.4) |
| Scope creep (R7) | Higher, because more is custom | Lower, because more is built in |

---

## 6. Recommendation

**Option B: pure C++ with JUCE 8 for engine and UI, with Tracktion Engine as the arrangement/playback engine** (pending the spike).

Why:
1. **Maschine hosting is the hardest requirement**, and it is the one area where the proposed stack is weakest. In C++ it is close to a solved problem. In Rust it is a research project on Windows.
2. **"Access to multiple third parties"** (plugins and SDKs such as VST3, AU, ARA, Ableton Link and ONNX) favours C++, because those SDKs are native C++ and need no FFI layer.
3. **One language, one process.** With the mocks no longer tied to HTML, the webview's only remaining benefit is faster widget building. That is outweighed by removing the IPC layer, the doc mirror and the telemetry channel (design 4.2 and 10.2 shrink to normal in-process observer code).
4. **Plugin windows and drag-out work natively.** Maschine's editor can be docked in the app, and clips can be dragged out to Explorer.
5. **Roadmap compresses.** With Tracktion Engine, M7 (recording), M8 (effects/automation), M10 (MIDI) and M11 (plugins) become mostly integration work, so Maschine support can move from the end of the roadmap to right after the MVP.

What you give up: Rust's compile-time thread and memory safety, the clean `im` snapshot undo (unless you keep your own model), pure-Rust decoding, and the speed of web tooling for panels and dialogs.

**When to choose otherwise:**
- If you know Rust well, don't know C++, and Maschine/VST support can wait 9+ months: option A2 (Rust + native UI), adding plugin hosting at M11 with the best crate available then.
- If you strongly prefer building UI with HTML/CSS: option C (JUCE engine + WebView UI). The plugin-hosting benefits are the same; you pay for the IPC layer.

### 6.1 Changes to `technical-design.md` if option B is accepted
- **T1/T2/T3:** Tauri + Rust + React -> JUCE 8 desktop app (CMake, MSVC/Xcode), UI as JUCE `Component`s with a custom `LookAndFeel` from the mock's design tokens. Bundle IBM Plex fonts as binary data.
- **T4/T5:** Engine = Tracktion Engine, *or* a custom JUCE engine following section 5. `AudioDeviceManager` for I/O.
- **T6 to T12:** JUCE audio formats + Signalsmith (native) + LAME; lock-free queues via `juce::AbstractFifo` or `moodycamel::ReaderWriterQueue`.
- **T15:** keep the Command API. Undo is either Tracktion's `UndoManager` or your own immutable model.
- **Section 4 and 10.2:** drop IPC, JSON patches, telemetry channel and `peaks://`. The UI reads document state on the message thread and engine telemetry from atomics on a 60 Hz `Timer`/`VBlankAttachment`.
- **Section 5.2:** add instrument and effect plugin slots per track, MIDI tracks, multi-output routing (aux returns for Maschine outputs 2 to 16) and PDC.
- **Section 5.10 / 13:** golden tests only for plugin-free projects; export uses `kOffline` process mode.
- **Section 15:** move MIDI + plugin hosting (Maschine as the acceptance test) to right after M4.

### 6.2 Spike before committing (about 2 weeks, replaces M0)
1. JUCE 8 app with a dark `LookAndFeel` and a timeline `Component`: 4 tracks, 50 clips with waveforms, scroll/zoom at 60 fps (Direct2D renderer), drag a clip, drag-reorder a track.
2. Load **Maschine 3 VST3**, show its editor (docked and floating), play it from host transport at 120 BPM, activate 4 stereo outputs and meter each one.
3. Save the project, reopen it and confirm Maschine's state is restored.
4. Offline-render 16 bars including Maschine to WAV.
5. Drag an audio pattern out of Maschine onto a track, and drag a clip out of the app into Explorer.
6. The same Maschine test (steps 2 and 3) with the best Rust crate (`vst3-host`), timeboxed to 3 days, to confirm or refute this comparison with real data.

**Exit criterion:** if steps 1 to 5 work in JUCE within the spike and the Rust attempt at step 6 does not, option B is confirmed. If step 1 feels too slow to build, fall back to option C for the UI and keep the JUCE engine.

---

### Sources (checked 2026-10-08)
- Maschine 3 formats and system requirements: [Thomann product page](https://www.thomann.co.uk/native_instruments_maschine_3.htm)
- Maschine multi-outputs in plugin mode: [NI Maschine FAQ](https://www.native-instruments.com/de/products/maschine/production-systems/maschine/faq/)
- Rust hosting crates: [rack](https://docs.rs/crate/rack/latest), [vst3-host](https://docs.rs/crate/vst3-host/0.5.0), [clack](https://gittrend.io/repo/prokopyl/clack)
- Tracktion Engine licensing: [GitHub](https://github.com/Tracktion/tracktion_engine/), [licensing update (JUCE forum)](https://forum.juce.com/t/announcement-tracktion-engine-licensing-update/56897), [CDM](https://cdm.link/the-guts-of-tracktion-are-now-open-source-for-devs-to-make-new-stuff/)
- JUCE 8 WebView UIs (option C): [JUCE 8 release](https://killerguitarrigs.com/juce-8-development-platform-released/), [tomduncalf_juce_web_ui](https://github.com/tomduncalf/tomduncalf_juce_web_ui/)
- Other licence facts are from the "Verified external facts" list in `technical-design.md`.
