# Design: M4a Warp

## Context

See proposal.md for the why. What exists today:

- Every clip is made by `engine::Arrangement::createClip` with `setAutoTempo(false)` and `setUsesProxy(true)`: it plays at native speed, its offset and length are in seconds of the sound, and `ClipInfo` reports `start`/`length` in beats plus `startSeconds`/`lengthSeconds`/`offsetSeconds`. Reverse uses Tracktion's reversed proxy, and the clip is reported `rendering` (and is silent) until the proxy exists.
- New clips get a 3 ms fade in and out through Tracktion's own `fadeIn`/`fadeOut` properties (`Arrangement.cpp`, around line 473). The inspector's FADE IN / FADE OUT `ValueField`s exist but are disabled.
- `commands::SetTempo` keeps start beats and shortens clips that now overlap, using `EditPlanner::shortenOverlaps` and a baseline for tempo drags. `EditPlanner` works on `Span`s (beats) with a `TimeMap`.
- Tempo per sound: `io::AnalysisService` (cache only, `SoundAnalysis::bpm()`, onsets) plus the user correction `model::Source::tempoCorrection()` in the project. `engine` cannot see `io` (dependency direction `engine <- commands <- io`).
- `render::SliceRenderer` + `RenderScheduler` (Signalsmith, r8brain) render pad slices; `engine::renderToWav` waits for Tracktion proxies; `sampler-cli render` and the ten golden cases build projects through the real commands.
- Tracktion clip API that matters: `setAutoTempo`, `setSpeedRatio`, `setTimeStretchMode` (`TimeStretcher::rubberbandMelodic`), `setPitchChange`, `setLoopInfo` (`LoopInfo` BPM), `setLoopRange` / `setLoopRangeBeats`, `setFadeIn` / `setFadeOut`, `setUsesProxy`, plus the engine's `AudioFileManager` for reloading a changed file.

## Goals / Non-Goals

**Goals:**
- Warping that is deterministic for offline renders (golden tests, `sampler-cli`) and needs no special undo handling.
- Keep Tracktion inside `src/engine`; keep `BeatsRenderer` pure DSP in `src/render`.
- Old projects load and sound identical; existing golden references stay valid unchanged.

**Non-Goals:**
- User warp markers, Texture mode, tempo automation, real-time stretching (M7, M10).
- Placing a warped clip so that the sound's first beat ("1.1.1" marker) lands on the grid: clips still start at the start of their region, as in M2/M3. (A loop with a 100 ms pickup is 100 ms late on the grid; trim its left edge. Revisit with warp markers in M7.)
- Fade curve shapes other than linear; crossfades (M7).
- Export, autosave, audio settings, installer (M4b).

## Decisions

### D1. Clip data: `sampler_*` properties, warp chosen by the caller
New optional clip properties: `sampler_warp` (0/1), `sampler_warpMode` (`beats|tones|repitch`), `sampler_sourceBpm`, `sampler_transpose` (semitones; the M3 `sampler_pitch` of slice copies is read as the initial transpose and then migrated in place on the first edit, no schema bump), `sampler_loop` (0/1) plus the loop region in seconds of the sound (`sampler_loopStart`, `sampler_loopLength`), and `sampler_fadeIn` / `sampler_fadeOut` (user fades in seconds). A missing `sampler_warp` means WARP off, so M1 to M3 projects are unchanged (spec: projects without warp settings).

`ClipSpec` gains a `WarpSettings` struct. **The caller decides the default** (UI: `ArrangementView` drop handlers and `SlicesToTrack`'s caller ask `AnalysisService` + `Source::effectiveBpm` + category), not `CreateClip` itself. Reason: commands and `engine` cannot see the analysis cache (`io`), and the golden projects, built through commands without warp settings, must keep sounding as they do now.

Alternative considered: storing the sound's analysed BPM in the project so commands can decide. Rejected: M3 deliberately keeps analysis out of the project (spec "Analysis results are cache data").

### D2. Warp is applied by `engine::Arrangement` from the clip properties
`Arrangement` gets `setClipWarp(id, WarpSettings)`, `setClipLoop`, `setClipFades`, `setClipTranspose` and extends `ClipInfo` with the warp fields, `loop`, `fadeIn`/`fadeOut` and `rendering`. A single private `applyWarp(clip)` turns the `sampler_*` properties plus the project tempo into Tracktion settings:

| Mode | Tracktion setup |
|---|---|
| WARP off | as today: `autoTempo` off, speed ratio 1 |
| Re-pitch | `autoTempo` off, `TimeStretcher::disabled`, speed ratio = projectBpm / sourceBpm; length and offset scaled by the ratio |
| Tones | `autoTempo` on, `LoopInfo` BPM = sourceBpm, `rubberbandMelodic`, `setPitchChange(transpose)`, proxy on |
| Beats | `autoTempo` off, speed ratio 1, plays the Beats render file (D3); offset and length in seconds of the render |

Every place that writes clip bounds (create, move, trim, split, paste, tempo) goes through the same path, so a clip's Tracktion state is always a function of its properties and the tempo. `applyWarp` runs inside the command's transaction (it writes through the undo manager), so undo restores the Tracktion state with the properties.

Spike first (task 1.1): confirm Re-pitch by speed ratio keeps sample-accurate start positions and pitch (880 Hz test), Tones with auto-tempo keeps length in beats across a tempo change and renders offline deterministically, loop ranges work on non-auto-tempo clips (`setLoopRange` in seconds) and on Tones clips (`setLoopRangeBeats`). If Tones looping fails, LOOP is disabled for Tones clips (spec stays: the control greys out) and logged.

### D3. Beats: deterministic cache file named by its inputs
`render::BeatsRenderer` (pure DSP) takes the decoded WAV, sourceBpm, projectBpm, reverse and transpose and writes a whole-sound render:
1. Onsets from `analysis::OnsetDetector` with a fixed sensitivity (60) on the whole sound; none -> a 1/16 grid at sourceBpm. The renderer computes them itself, so it does not depend on the `io` cache and gives the same result headless.
2. If reversed, reverse the sound first (design doc 7.5: a reversed loop still lands on the grid).
3. Each segment [onset i, onset i+1) is copied unstretched to `ratio * t_i` (ratio = sourceBpm / projectBpm). Faster: cut at the next segment's start with a 3 ms fade-out. Slower: the gap fades out over the rest of the segment's time (decay fade, 10 ms default end fade).
4. Transpose ≠ 0: each segment is pitch-shifted with Signalsmith (length kept) before placing.
5. Written as a 32-bit float WAV at the sound's rate to `cache/beats/<key>.wav` through a temp file + rename.

`key` = hash of (sound content key, sourceBpm, projectBpm, reverse, transpose, renderer version). The **command** sets the clip's Tracktion source to that path (relative to the bundle) in the same transaction as the edit that changed any input, so the saved edit is deterministic and undo/redo flips between files that are both still in the cache: no rerender (spec: Undo is instant). Region, trim, split and loop do not change the key (whole-sound render, offset scaled by the ratio).

Alternatives considered: (a) keep the clip on the decoded WAV and swap the file at runtime without the undo manager (as the take taps do): rejected, because undo of an older step that touched the same property would revert the swap, and the saved file would depend on timing. (b) Fall back to Re-pitch while a render is pending (design doc 7.6): rejected for M4a, because it needs a second edit when the render finishes. The clip is silent and drawn "rendering" for the render time (about 10 to 50 ms per 4-bar loop), like reversed clips in M2. Design doc 7.6 is updated to say so.

### D4. `engine::WarpService`
Owned by `ProjectEdit` next to `Arrangement`. On project open, on every arrangement change and when the tempo changes, it walks the Beats clips, and for each whose file is missing and not queued, queues a `BeatsRenderer` job (two worker threads, visible/playing clips first, newest request per key wins). When a job finishes (`MessageManager::callAsync`), it tells Tracktion to reload the file (`AudioFileManager::forceFileUpdate` or equivalent, confirmed in the spike) and broadcasts so the lane drops "rendering". `isIdle()` is used by `renderToWav` (which already waits for proxies) and thus by `sampler-cli` and the golden tests. Missing-file reporting for Beats clips checks the sound's decoded WAV, not the render.

Cache growth: old renders are kept (undo needs them). A size cap with oldest-first deletion of files not referenced by the edit is added only if the M4a exit run shows growth is a problem; "Clean up cache" stays for later.

### D5. Commands
- `SetClipWarp{id, on, mode, sourceBpm}`: one command for the WARP toggle, MODE and BPM (½, ×2, typed). Keeps the start; recomputes the length (warped: bars from the sound region at sourceBpm; off: seconds), runs `EditPlanner` no-overlap on the new span.
- `SetClipTranspose{id, st}`, `SetClipFades{id, in, out}` (clamps so in + out ≤ length; a gesture for handle drags, same `begin/update/commit` as fader drags), `SetClipLoop{id, on}` and `SetClipLength{id, beats}`.
- `TrimClip` lets the right edge pass the sound end when looped; offsets of warped clips convert beats -> render or source seconds through the clip's ratio (helper in `EditPlanner`'s `TimeMap`, extended with a per-clip ratio).
- `SplitClips`: both parts copy all `sampler_*` properties; fade-in stays on the first, fade-out on the second; looped parts keep the loop region and get the right loop offset.
- `SetTempo`: unwarped clips as now; warped clips keep start and length in beats (no overlap work needed for them); Beats clips get their new key in the same transaction. The tempo-drag baseline is kept; Beats keys are only rewritten on the final `perform` of a drag (`commit`), to avoid queuing a render per drag step. During the drag Beats clips keep their old file and are heard at the old speed.
- `SlicesToTrack`: takes `WarpSettings` from its caller (D1), places clips back to back in beats when warped (slice length in beats = frames / rate * sourceBpm / 60), and copies the slice pitch into `sampler_transpose`.
- Debug post-conditions in `CommandService` gain: a warped clip has sourceBpm in range; fades in + out ≤ length.

### D6. Fades
User fades are stored in `sampler_fadeIn/Out`; Tracktion's fade is `max(user, 3 ms declick)`. The inspector shows the user value (0 ms means only the declick). Linear curve (`AudioFadeCurve::linear`).

### D7. UI
- `ClipInspector` grows a second row: WARP (toggle), MODE (`SegmentedControl` with Texture disabled), BPM (`ValueField` + ½ / ×2 buttons), TRANSPOSE, LENGTH (bars.beats `ValueField`), LOOP; FADE IN / FADE OUT enabled. Disabled states and the "Tempo not known" hint come from a plain `InspectorLogic` struct (testable). The inspector asks the app layer for the sound's tempo through the existing context (it can see `io`).
- Lanes: the waveform image of a clip is drawn from the sound (or the Beats render once ready) mapped through the clip's ratio, repeated for loops with a 1 px mark per repeat; fades drawn as a line + darkened area. Fade handles: 6 px squares, hit-testing in the existing `hitTestClip` in `ui/arrange/ArrangeGeometry.h` (new `ClipZone`s `fadeInHandle` / `fadeOutHandle`, tested), drawn only on hover.
- The waveform image cache key gains ratio, loop and fade values.

### D8. Golden and tests
Existing ten cases keep their references (no warp settings => identical). New cases: `warp-beats`, `warp-tones`, `warp-repitch`, `beats-transpose`, `clip-loop`, `fades`, `warp-tempo-change`. Tones is checked only against its own reference built on this machine (Rubber Band is deterministic for the same build and block size; if CI shows otherwise the Tones case gets a relaxed tolerance, logged). Unit tests: `BeatsRenderer` (hit positions within 1 ms, attack identical in the first 5 ms, gap fades, 1/16 fallback, reverse on grid, transpose keeps length), the key function, `InspectorLogic`, fade clamping, warped trim/split math, property tests extended with the new commands (`undo(apply(x)) == x`, no overlaps).

## Risks / Trade-offs

- [Tracktion auto-tempo or loop range behaves differently from the plan] -> spike 1.1 first; documented fallbacks (Tones without loop; Re-pitch via `autoTempo` + `disabled` stretch mode instead of speed ratio).
- [Silent gap while a Beats render is pending, audible when changing tempo during playback] -> renders are short and prioritised; documented in the spec; the stale-render fallback can come back later without a model change.
- [Cache grows with every tempo tried] -> keep for undo; measure in the exit run; cleanup later.
- [Rubber Band output not bit-identical across machines/CI] -> Tones golden tolerance fallback (D8).
- [Inspector gets crowded] -> two rows, following the mock's Session inspector (WARP/MODE/BPM) and Arrange inspector (GAIN/START/FADES); checked by the PO in demo D2.
- [Old `sampler_pitch` on slice clips becomes audible] -> intended (M3 spec said it would be heard once clips can be pitched); only affects clips once WARP is on, and old projects have WARP off.

## Migration Plan

No schema bump: every new property is optional and absent means today's behaviour. ROADMAP: M4 is split into "M4a Warp" (this change) and "M4b Export" with the remaining items; the Decisions log records the split and the "silent while rendering" choice; design doc §7.6 and §15 are updated to match.

## Open Questions

- Exact gap-fade length for Beats when slowing down (10 ms end fade vs. a decay curve): tuned by ear in demo D1; does not change the spec or tasks.
