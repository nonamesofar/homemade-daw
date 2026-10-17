# Projects

A Sampler project is a folder named `Name.sdaw` (a "bundle"). Everything the project needs is inside it, so you can copy, move or back it up as one folder.

```
Name.sdaw/
  project.tracktionedit     the project file (XML); Sampler's own data is the <SAMPLER> section inside it
  audio/
    originals/              exact copies of the files you imported
    recordings/             the takes you recorded onto tracks (rec-<date>-<time>-<n>.wav), used as they are
    <name>-<hash>.wav       the decoded working copies the app plays (32-bit float WAV)
  cache/                    disposable: waveform pictures, reversed-clip renders, the analysis of each sound, the stretched copies of Beats clips (cache/beats/) and other generated files
```

The names of copied files are a cleaned-up form of the original name plus the first 8 characters of a content hash, for example `drums-loop-120-1a2b3c4d.wav`. Your own files are never changed: the app only reads them and keeps its own copy.

## Untitled projects

The app starts with an untitled project, so you can drop a file in at once. Until the first save its folder lives in the app's data folder: `%APPDATA%\Sampler\untitled\<random-id>.sdaw`.

- **Save** (Ctrl+S) on an untitled project asks where to save it and what to call it (`.sdaw` is added). The folder is moved there, with everything in it.
- **Quit / New / Open** with unsaved changes asks Save, Discard or Cancel. Discard deletes the untitled folder. An untitled project with no changes is simply removed when you leave it.
- If the app crashes, the untitled folder stays where it is. (Recovery comes in a later milestone.)

## Save, Save As, Open

- **Save** writes the project file in place. The file is written to a temporary file and swapped in, so it is always the old version or the new one.
- **Save As** (Ctrl+Shift+S) makes an independent copy under a new name: the project file and `audio/` are copied, `cache/` is not. From then on you work in the new copy; the first one stays as it was. Undo history is kept.
- **Open** (Ctrl+O) takes a `.sdaw` folder. A folder without a valid project file is refused with a message and the current project stays open. A project made by a newer version of Sampler is refused too, and its file is not touched.
- The undo history is not saved in the project.

## What the project stores (since the arrangement)

Saving keeps the whole arrangement inside `project.tracktionedit`:

- **Tracks** in order: name, colour, volume, pan, mute, solo. A project always has at least one audio track; a project saved before the arrangement existed opens with one empty track "Audio 1" and its file is not rewritten until you save.
- **Clips**: the track, start (bars and beats), length, the part of the sound they play, gain, reverse, and which sound they use. A clip refers to the sound's working copy in `audio/` by a path relative to the project folder, so the project can be moved. If that file is missing when the project opens, the clip is kept, drawn as missing and silent, and a message names the file.
- **Tempo, time signature, loop range and loop on/off, metronome on/off** and the **snap** value (the snap value lives in `<SAMPLER><UI snap="1 bar"/>`).
- The master volume.

Not saved: the playhead, selection, zoom and scroll, and the undo history. Play, stop, moving the playhead, looping on/off, the metronome and the snap value are not edits: they do not mark the project as changed (the window title shows `*` only after an undoable edit, an undo or a redo that moved the history away from the last save).

## Recording (since M2b)

You arm a track (R in its header), choose what it records (its **input**) and press Record. Each take is written to `audio/recordings/rec-<date>-<time>-<n>.wav` (32-bit float WAV) while you record, then added to the project as a sound in the category Recording and as a clip on the armed track. Takes are not copied again into `audio/originals/` and are never changed after they are finished.

- A track's input is saved with the project: System audio on the Windows default playback device (the default), on a named device, Master (the whole mix), or another track.
- Which tracks are armed is not saved. A recording that is running when you save is not in the file: its take is added when you stop.
- A take recorded in an untitled project is inside the untitled folder and moves with the first Save. Save As is refused while recording.
- If a recorded device is not there when you reopen a project, the track records the default device instead and its header says so.

## Slices and tempo corrections (since M3)

The sample editor saves, for each sound you chopped or corrected, inside the project file:

- **Slices**: how the sound was chopped (Beat, Transient or Manual), the grid (1/4, 1/8, 1/16, 1/32) and the Sensitivity last used, the slice points (positions in the sound, in samples) and, for each slice, its pitch, gain, Reverse and play mode (One-shot, Gate, Loop). A sound that was never chopped has none of this and is one slice. The slices cover the whole sound from its first sample to its last, at most 256 of them, each at least 5 ms long.
- **Tempo and first beat**: only what you typed or dragged (the tempo field, half and double, the "1.1.1" marker). What the app worked out by itself is not saved in the project (see the cache folder below), so a correction survives deleting `cache/`.

The audio files are never changed by any of this: slices are only marks and settings.

## Clips made from slices (since M3)

"Slices to new track" puts a new track at the end of the track list, named after the sound, with one clip per slice laid back to back from the playhead. The clips are copies: they are ordinary clips of the sound (each plays its slice's part with the slice's gain and direction) and do not change when you edit the slices afterwards. Each remembers which sound and slice it came from.

## The cache folder

`cache/` holds only things the app can make again (waveform pictures, the renders of reversed clips and Tones clips, the analysis of each sound and the stretched copies of Beats clips). You can delete it while the app is closed; the next time you open the project it fills up again and nothing is lost.

- `cache/beats/<key>.wav` is the stretched copy of a whole sound that a Beats clip plays (32-bit float WAV at the sound's rate). The key is a hash of the sound, the clip's own tempo, the project tempo, reverse and transpose, so a file is never changed after it is written, and each tempo you tried leaves a file (kept so that Undo is instant). A Beats clip whose file is missing shows "rendering" and is silent until the app has made it again in the background; delete the folder whenever you like.
- `cache/analysis/<key>.json` is the analysis of one sound: the hits it found with their strength, the best three tempo guesses and the first beat. The key is the first 16 characters of the sound's content hash (a recording uses a hash of its file's name, size and date). The file also says which version of the analysis made it; after an update of the app the sound is analysed again. Opening a project analyses nothing: a sound is analysed when it is imported or recorded, or when it is opened in the sample editor without a usable file here (the editor says "Analysing..." meanwhile). Analysis never changes the project, marks it as unsaved or adds an undo step.

## For developers

The `<SAMPLER schemaVersion="1">` section has one `SOURCE` node per imported sound (id, name, relative paths of the original and the decoded copy, content hash, sample rate, channels, frames, category). All paths are relative to the bundle. A newer `schemaVersion` than the app knows is refused; older ones are upgraded in memory by the steps in `src/model/Migrations.cpp` (none yet).

M3 adds, all optional so the schema version stays 1: on a `SOURCE` node the user's corrections `bpm` (20 to 999) and `beatOffset` (a frame of the sound), and a `SLICESET` child:

```xml
<SOURCE id="src_1" name="break" ... bpm="92.0" beatOffset="4410">
  <SLICESET mode="beat" division="1/4" sensitivity="50.0" markers="4410 33810">
    <SLICE pitch="0.0" gainDb="0.0" reverse="0" playMode="oneshot"/>   <!-- one SLICE per slice: markers + 1 -->
    <SLICE pitch="7.0" gainDb="-3.5" reverse="1" playMode="gate"/>
    <SLICE pitch="0.0" gainDb="0.0" reverse="0" playMode="oneshot"/>
  </SLICESET>
</SOURCE>
```

`markers` are frames of the sound at its own sample rate, strictly increasing and never 0 (the first slice always starts at 0); `mode` is `beat`, `transient` or `manual`; `division` is `1/4`, `1/8`, `1/16` or `1/32`; `playMode` is `oneshot`, `gate` or `loop`. Adding a point splits a slice and both halves keep its settings; removing a point merges the two and keeps the left slice's settings; choosing Beat, Transient or Auto-slice, or another division, resets every slice to the defaults. `findInvariantViolation` checks the slice set after every command in debug builds. The analysis result (`cache/analysis/<key>.json`: `algorithmVersion`, `sampleRate`, `frames`, `firstBeatFrame`, `tempo` candidates, `onsets` as `[frame, strength]`) is derived data written by `io::AnalysisService`; the effective tempo and first beat are the correction if there is one, else the analysis.

A clip made from a slice carries three more `sampler_*` properties on Tracktion's clip node: `sampler_originSource` (the sound's id), `sampler_originSlice` (the slice's index at the time) and `sampler_pitch` (the slice's pitch in semitones; since M4a it is the clip's transpose until the user sets one).

Warping (since M4a) adds optional `sampler_*` properties on a clip node, all absent on projects from before, which then open with WARP off and are not changed by opening: `sampler_warp` (1 = on), `sampler_warpMode` (`beats`, `tones` or `repitch`), `sampler_sourceBpm` (the clip's own tempo, 20 to 999), `sampler_transpose` (semitones, -24 to +24), `sampler_reversed` (1 on a warped Beats clip that is reversed: its render is made from the reversed sound, so Tracktion's own reverse flag stays off), and, for loop and fades, `sampler_loop` (1 = on; set to 0 when switched off, not removed), `sampler_loopStart` and `sampler_loopLength` (the part of the sound the clip repeats, in seconds of the sound; the clip's offset says where inside it the clip starts, and Tracktion's loop range is written from them in timeline seconds, or beats for Tones), and `sampler_fadeIn` and `sampler_fadeOut` (the user's linear fades in seconds, cut to the clip's length when played; Tracktion's own fade is the larger of the user's fade and 3 ms). Tracktion's own clip properties (auto-tempo, stretch mode, speed, pitch, the source file, start, length, offset) are always the result of these properties and the tempo: a Beats clip's `source` is `cache\beats\<key>.wav`, and a warped clip's offset is stored in seconds of the timeline, so it is read back through the clip's tempo ratio.

Arrangement data is stored by Tracktion (tracks, clips, tempo, transport) with our extras as `sampler_*` properties on its nodes: a clip's `sampler_sourceId` names its `SOURCE`. Everything added in M2 and M2b is optional, so the schema version stays 1. M2b adds: the track property `sampler_input` on Tracktion's track node (`system:default` is the default and is not stored; `system:<endpoint id>`, `master`, `track:<track item id>`), and on a SOURCE node of a take the optional `recordedFrom` (for example `System audio (Speakers)`, `Master` or a track name) and `recordedAt` (ISO 8601 local time). A take's `original` and `decoded` are the same `audio/recordings/...` path. While a recording runs, the edit holds taps (plugins of type `samplerTap`, marked `sampler_tap`) on the tapped tracks or the master; they are added and removed without the undo manager and `saveEditAtomically` leaves them out of the file. Dirty state comes from the undo history (`CommandService::historyToken`), not from watching the file.
