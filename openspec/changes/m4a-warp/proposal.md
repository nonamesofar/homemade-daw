# Proposal: M4a Warp (clips follow the project tempo, loop, length and fades)

## What the user will be able to do

Today every clip plays at the speed it was recorded at: a 90 BPM drum break dropped into a 120 BPM song drifts out of time, and changing the tempo only moves where clips start. After this change:

- **Clips follow the song's tempo.** A loop dropped into the arrangement plays in time with the project, whatever tempo it was recorded at. Change the project tempo and the loops speed up or slow down with it and keep their length in bars.
- **Three ways to stretch, chosen per clip** in the bottom panel (WARP on/off and MODE):
  - **Beats** (for drums and breaks, the default for loops): the hits stay crisp because each hit is moved to its new place instead of being stretched.
  - **Tones** (for melodic and chordal sounds): high-quality stretching that keeps the pitch.
  - **Re-pitch** (vinyl/sampler style): the sound is sped up or slowed down like a turntable, so its pitch changes with the tempo.
  - Texture is shown but stays disabled until a later milestone. With WARP off a clip plays at its own speed, as now.
- **The clip's own tempo** (BPM) is shown and can be typed, halved or doubled when the tempo detection was wrong.
- **Transpose** a clip by -24 to +24 semitones in Beats and Tones mode, without changing its length. Slices sent to a track with a pitch now sound at that pitch.
- **Loop a clip and set its length in bars.** With LOOP on, a 1-bar loop can be stretched over 8 bars by dragging its right edge or typing LENGTH, and it repeats.
- **Fades.** FADE IN and FADE OUT in the bottom panel are no longer greyed out, and each clip has small fade handles at its top corners that can be dragged in the timeline. Fades are heard at once and are drawn on the clip.
- **Slices line up with the grid.** "Slices to new track" makes clips that follow the project tempo, so a break chopped at 90 BPM sits on the bars of a 120 BPM song.

Projects saved by earlier versions open unchanged and sound the same: their clips keep WARP off until the user turns it on.

Not in this change (planned for M4b, the second half of the MVP): export to WAV/MP3/stems, autosave and crash recovery, the audio settings dialog and the Windows installer.

## Why

Warping is the second half of the core workflow (import or capture, chop, **fit to the song**, arrange). Without it, material at another tempo cannot be arranged, and slices from a 90 BPM break do not fit a 120 BPM project. M3 already finds each sound's tempo and hits, which is what warping needs. The M4 milestone was split in two at the PO's request: this change (M4a) covers warping and the clip controls; M4b covers export, autosave, the audio settings and the installer.

## What Changes

- New per-clip warp settings (on/off, mode, the clip's own tempo, transpose), loop on/off and length, and user fades, all saved with the project and each change one undo step.
- New clips of sounds with a known tempo that are not one-shots get WARP on in Beats mode; one-shots and sounds whose tempo is not known yet get WARP off.
- **Changed behaviour:** after a tempo change, warped clips keep their length in bars. Unwarped clips behave as before (they keep their start and their length in seconds).
- Beats mode renders a stretched copy of the sound in the project's cache folder in the background. While it is being made, the clip is drawn as "rendering" and is silent, the same as reversed clips today. Deleting the cache folder makes the copies again.
- The bottom panel gets WARP, MODE, BPM (with halve and double), TRANSPOSE, LENGTH and LOOP, and its FADE IN and FADE OUT fields are enabled. The timeline draws warped clips at their stretched length, loop repeats and fade curves, and offers fade handles.
- Slice clips made by "Slices to new track" are warped (Beats) when the sound's tempo is known, and their saved pitch is heard.
- Offline renders (the command-line render tool and the golden tests) wait for pending Beats renders, and new golden cases cover each warp mode, loops and fades.

## Capabilities

### New Capabilities
- `clip-warp`: how a clip follows the project tempo: warp on/off, the Beats, Tones and Re-pitch modes, the clip's own tempo, transpose, the default for new clips, behaviour while a Beats render is pending, and what a tempo change does to warped clips.

### Modified Capabilities
- `clip-inspector`: the panel gains WARP, MODE, BPM, TRANSPOSE, LENGTH and LOOP; FADE IN and FADE OUT become editable (the "disabled until later" requirement now only covers Texture).
- `arrangement-timeline`: clip loop and length (the right edge can extend a looped clip past the end of its sound), fade handles, and how warped and looped clips are drawn; trimming and splitting work in warped time.
- `transport`: "Clips keep their bars when the tempo changes" now also keeps the length in bars of warped clips.
- `project-bundle`: warp, loop, transpose and fade settings are saved; Beats renders live in the cache folder and are remade when missing; older projects open with WARP off and are not marked as changed.
- `sample-editor`: "Slices to new track" makes warped clips that keep the slice's pitch audible.
- `build-and-ci`: golden render tests gain warp, loop and fade cases; offline renders wait for Beats renders.

## Impact (technical overview)

- **render:** a new `BeatsRenderer` (pure DSP, no Tracktion): segments the sound at its hits with our onset detector (1/16 grid if none), places each segment at its stretched position, fades the gaps, and pitch-shifts segments with Signalsmith when transposed. It runs on `RenderScheduler`-style workers.
- **engine:** the `Arrangement` facade gains warp, loop and fade primitives and reports warped lengths; a `WarpService` keeps Beats render files up to date (renders missing or stale files, tells Tracktion to reload them) and tells `renderToWav` when it is idle. Re-pitch is a varispeed clip, Tones is Tracktion's auto-tempo with Rubber Band through a proxy (both to be confirmed by a short spike at the start).
- **commands:** `SetClipWarp`, `SetClipLoop`, `SetClipLength`, `SetClipFades` and `SetClipTranspose` (or one `SetClipParams` family); `SetTempo`, trim, split, `CreateClip` and `SlicesToTrack` take the warp into account. A Beats clip points at a cache file whose name comes from its settings, so the edit stays deterministic and undo needs no extra handling.
- **ui:** inspector fields, timeline drawing of warped, looped and faded clips, fade-handle hit testing.
- **model:** no schema bump: the new data is optional `sampler_*` clip properties, and missing ones mean "WARP off".
- Design doc §7.6 is updated (pending Beats renders are silent instead of falling back to Re-pitch); ROADMAP M4 is split into M4a and M4b.
- No new dependencies.
