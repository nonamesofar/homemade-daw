# Proposal

## What this means for the user (plain language)

After this change, Sampler can make music: sounds from the browser go onto tracks, and the user builds a song from them and plays it back. With it the user can:

- Drag a sound from the browser (or a file from Explorer) onto a track to place it as a clip at the bar where it was dropped. Dropping below the last track makes a new track. The clip shows its waveform in the track's colour.
- Press Play and Stop (or Space) and hear the arrangement. A playhead runs across the tracks, and the header shows the position as bar . beat . sixteenth. Clicking the ruler moves the playhead; clicking the position lets the user type where to go. Pressing Stop a second time goes back to the start.
- Edit clips: select them (click, Shift or Ctrl to add, or drag a box around them), move them (also to another track), shorten either end, split at the playhead, duplicate, delete, and copy, cut and paste. Two clips on one track never overlap: putting a clip on top of another shortens or splits the one underneath.
- Choose how edits line up with the bars (off, 1 bar down to 1/32, and triplets); holding Alt while dragging ignores it.
- Zoom (Ctrl+wheel, Z for the selection, Shift+Z for the whole song) and scroll with the wheel or the scroll bars.
- Add, delete, rename and recolour tracks, and drag a track header up or down to change the order.
- Set each track's volume (drag the dB readout), pan, mute and solo, and watch its level meter. The master volume and meter sit at the right of the header, with a light that shows when the output went over 0 dB.
- Set a loop over part of the song by dragging the loop bar over the ruler and turn looping on or off (Loop button or L); playback then repeats that part without a gap.
- Turn on the metronome (Click), with a stronger click on beat 1.
- Change the tempo and the time signature in the header. Clips stay on their bars when the tempo changes; the ruler, the grid and the metronome follow.
- Select a clip to see its details at the bottom: name, track, bars and waveform. Change its gain and start position there, or play it backwards.
- Undo and redo every edit; a whole drag or fader move is one step. Everything is saved with the project and comes back when it is reopened.

The work is split into eight small demos that the product owner checks one by one on the same branch (the list is in `tasks.md`). Each demo ends with a short script: what to click and what to see or hear. At the end the product owner builds the 16-bar, 4-track arrangement from the mock in the app.

Not in this change: fades, warp (clips play at their own speed and do not follow tempo changes in pitch or speed), clip looping and length beyond the sound's end (M4); recording computer audio (M2b); the sample editor ("Open in sample editor" stays greyed out, M3); the Session view (the toggle stays on Arrange, M6); exporting to a file (M4); follow-playhead scrolling, resizing track heights and adaptive snap (M7); recording from inputs and track arm (the R button is shown but disabled, M8).

## Why

M2 is the arrangement milestone of the MVP: it is where imported sounds become a song. M3 (slices sent to a track), M2b (captured audio placed at the playhead) and M4 (export) all build on the tracks, clips, transport and mixer added here. M1 left the main view empty, and the M0 timeline is a throwaway spike that holds its own data and lets clips hide each other. The golden render tests planned for M2 also give every later milestone a safety net for playback changes.

## What Changes

- **Arrangement view**: replaces the M0 spike timeline with the real one, drawn from the project itself: ruler with loop bar, track headers, lanes with grid, clips with waveforms, playhead, scroll bars, selection overlay.
- **Clip editing commands**: create from a sound, move, trim, split, duplicate, delete, copy/cut/paste, gain, start, reverse, each applying the no-overlap rule in the same undo step. Snap settings and a selection model on the UI side.
- **Tracks and mixer**: add, delete, reorder, rename, recolour; volume, pan, mute, solo per track with meters; master volume, meter and over light.
- **Transport**: play, stop, locate, position display, loop range and loop toggle, metronome, tempo and time signature.
- **Keyboard**: a focus-scoped shortcut router (dialog > text field > view > global), so Space, M, S, Z and Delete never fire while typing; Edit and Track menus for the new commands.
- **Clip inspector** in the bottom panel.
- **Projects**: the arrangement (tracks, clips, mixer, tempo, time signature, loop, snap) is saved and reopened; any arrangement edit marks the project as changed.
- **Headless render and golden tests**: a command-line tool renders a project to WAV; a set of small fixed projects is rendered in CI and compared with stored WAVs. A manual QA checklist is started.
- No **BREAKING** changes for users. Projects from M1 open unchanged (they simply have no clips); the project schema version stays 1 because everything added is optional.

## Capabilities

### New Capabilities
- `track-mixer`: tracks (add, delete, reorder, rename, colour) and their mixing: volume, pan, mute, solo, level meters, master volume, master meter and over light.
- `transport`: playing the arrangement: play, stop, locate, the position display, the loop range and loop toggle, the metronome, tempo and time signature.
- `clip-inspector`: the bottom panel that shows and edits the selected clip (gain, start, reverse) with its waveform.

### Modified Capabilities
- `arrangement-timeline`: the M0 spike requirements (fixed spike project, 50 clips) are replaced by the real arrangement: placing clips from the browser or Explorer, selection, editing, snap, the no-overlap rule, zoom and scroll with scroll bars, playhead.
- `project-bundle`: the arrangement is part of the saved project, and arrangement edits mark the project as changed.
- `undo-redo` (introduced by the unarchived `m1-core-import` change): gestures form one step, repeated mixer changes coalesce, selection survives undo, transport and view actions are not undoable.
- `app-shell` (introduced by `m1-core-import`): header transport and tempo controls, Edit and Track menu items, and focus-scoped keyboard shortcuts.
- `build-and-ci`: a headless render tool, golden render tests in CI, and a manual QA checklist.

## Impact

- New code: `src/engine` (a Tracktion-free arrangement facade over tracks, clips, tempo, loop, transport and meters), `src/commands` (arrangement commands and plain-logic edit planning for the overlap rule, split, duplicate and paste), `src/ui/arrange` (ruler, loop bar, track headers, lanes, overlays, inspector, transport header widgets), `src/ui` `KeyboardRouter`, `SelectionModel`, `SnapService`, `tools/sampler-cli`, `tests/golden`.
- Changed: `sampler_commands` now links `sampler_engine` (Tracktion stays private to the engine); `ProjectEdit` dirty tracking widens from the `SAMPLER` section to the whole edit (ignoring view-only properties); `MainComponent` gets the arrangement view, header controls, bottom panel and new menus; the M0 `TimelineComponent` is removed (its geometry helpers are reused).
- No new third-party dependency. Golden WAVs go to Git LFS (the rule already exists).
- Docs: `docs/qa-checklist.md` (new), `docs/projects.md` (what the project now stores), `ROADMAP.md` (M2 change line, ticks, decisions).
- Order: `m1-core-import` should be archived (its specs synced) before this change is archived, because this change adds to two capabilities that change introduced.
