# Manual QA checklist

Things automated tests cannot judge: how it sounds and feels. Each milestone adds a section with the steps of its exit
run. Follow it on a clean build (`scripts/build.ps1 -Preset windows-msvc-release`, then run `Sampler.exe`), tick the
steps, and write the date in "Last run".

## M2 Arrangement

**Last run:** _not yet_

Use `tests/audio-fixtures/` for sounds (drums-loop-120.wav, bass-phrase.flac, kick.wav, texture-60s.flac).

Build the 16-bar, 4-track arrangement of `concepts/project/Arrangement.dc.html`:

- [ ] Start the app: one empty track "Audio 1", the ruler, the hint "Drag a sample from the browser onto the timeline to create a clip", an empty bottom panel. Session cannot be chosen.
- [ ] Drop the four fixtures on the browser; the rows appear with waveforms.
- [ ] Ctrl+T three times; rename the tracks Drums, Bass, Chops, Vocal (double-click, type, Enter); recolour one (right-click > colour): its clips follow.
- [ ] Place clips as in the mock: Drums bars 1, 5, 9, 13 (reusing one sound), Bass from bar 5, Chops at bars 3, 5, 7, 11 and a **reversed** clip at bar 13 (Reverse in the bottom panel; wait for "rendering" to clear), Vocal at bars 9 and 15.
- [ ] Press Space: the playhead runs, the position display counts, the clips are heard in time. Space again stops where it was. Stop twice goes to 1 . 1 . 1.
- [ ] Click the ruler at bar 3 and play: it starts at bar 3. Click POSITION, type `2.3`, Enter: the playhead is at bar 2 beat 3.
- [ ] Drag a loop over bars 5 to 8 in the loop bar, press L, play from bar 4: it wraps at the end of bar 8 with no gap, again and again. Drag the loop end and body.
- [ ] Click on: a click per beat, accent on beat 1. Set 3/4: the ruler and the accent change; back to 4/4.
- [ ] Mix: drag a track's dB readout (quieter while dragging, Ctrl+Z restores in one step), double-click for 0 dB; pan Bass fully left (only left ear); mute Drums (M button and M key), solo Bass, Ctrl+click solo on Drums.
- [ ] Meters follow the hits and fall back smoothly; raise the master to +6 dB until the over light comes on; it stays on until clicked.
- [ ] Select a clip: the bottom panel shows name, "Track · Bars a–b" and the waveform in the track colour; set GAIN -6 dB while playing (quieter); type START `9.1.1` (clip jumps there); two clips selected: "2 clips selected"; FADE IN, FADE OUT and "Open in sample editor" are greyed.
- [ ] Editing: drag a clip onto another (the one underneath is shortened), drop a short clip inside a long one (it splits and plays the right parts), trim edges, Ctrl+E, Ctrl+D, copy/paste on another track, Delete, Ctrl+Z / Ctrl+Y through all of it. The Edit menu names each step ("Undo Move 3 clips").
- [ ] Snap: set 1/16 and drag (fine steps); hold Alt (free placement); 1/8T gives triplet steps.
- [ ] Zoom with Ctrl+wheel around the mouse, scroll with the wheel, Shift+wheel and the scroll bars; Shift+Z shows the whole song, Z the selection.
- [ ] Typing in the browser search box ("z", Space, Delete) only changes the text.
- [ ] Drop two files from Explorer onto track 1 at bar 5: grey placeholders, then two clips back to back; one Ctrl+Z removes the clips and the sounds.
- [ ] Save, quit, reopen: everything is back and plays the same. Delete a decoded WAV from `audio/`, reopen: that clip is drawn as missing and silent, one message names the file.
- [ ] Render with `sampler-cli render Demo.sdaw %TEMP%\demo.wav`; play it: it sounds like the app, without the click.
- [ ] `ctest --preset windows-msvc-debug -L golden` passes; listen once to `tests/golden/*.wav` (they are what the golden tests compare against).
