# Projects

A Sampler project is a folder named `Name.sdaw` (a "bundle"). Everything the project needs is inside it, so you can copy, move or back it up as one folder.

```
Name.sdaw/
  project.tracktionedit     the project file (XML); Sampler's own data is the <SAMPLER> section inside it
  audio/
    originals/              exact copies of the files you imported
    recordings/             the takes you recorded onto tracks (rec-<date>-<time>-<n>.wav), used as they are
    <name>-<hash>.wav       the decoded working copies the app plays (32-bit float WAV)
  cache/                    disposable: waveform pictures, reversed-clip renders and other generated files
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

## The cache folder

`cache/` holds only things the app can make again (waveform pictures and the renders of reversed clips now; stretched renders and similar later). You can delete it while the app is closed; the next time you open the project it fills up again and nothing is lost.

## For developers

The `<SAMPLER schemaVersion="1">` section has one `SOURCE` node per imported sound (id, name, relative paths of the original and the decoded copy, content hash, sample rate, channels, frames, category). All paths are relative to the bundle. A newer `schemaVersion` than the app knows is refused; older ones are upgraded in memory by the steps in `src/model/Migrations.cpp` (none yet).

Arrangement data is stored by Tracktion (tracks, clips, tempo, transport) with our extras as `sampler_*` properties on its nodes: a clip's `sampler_sourceId` names its `SOURCE`. Everything added in M2 and M2b is optional, so the schema version stays 1. M2b adds: the track property `sampler_input` on Tracktion's track node (`system:default` is the default and is not stored; `system:<endpoint id>`, `master`, `track:<track item id>`), and on a SOURCE node of a take the optional `recordedFrom` (for example `System audio (Speakers)`, `Master` or a track name) and `recordedAt` (ISO 8601 local time). A take's `original` and `decoded` are the same `audio/recordings/...` path. While a recording runs, the edit holds taps (plugins of type `samplerTap`, marked `sampler_tap`) on the tapped tracks or the master; they are added and removed without the undo manager and `saveEditAtomically` leaves them out of the file. Dirty state comes from the undo history (`CommandService::historyToken`), not from watching the file.
