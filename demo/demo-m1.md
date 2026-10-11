# Demo script: arrangement (change `m2-arrangement`)

Eight short demos, D1 to D8. Run them in order on the same build. Each one says what to do and what you should see or hear.

## Before you start

Build and start the app (release build):

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Preset windows-msvc-release
build\windows-msvc-release\src\app\Sampler_artefacts\Release\Sampler.exe
```

Sounds to use are in `tests\audio-fixtures\`: `drums-loop-120.wav`, `bass-phrase.flac`, `kick.wav`, `texture-60s.flac`.
Headphones are best for the pan and meter checks (D5).

---

## D1: Sounds on tracks, play them back

1. Start the app. You see one empty track "Audio 1", the bar ruler and the hint "Drag a sample from the browser onto the timeline to create a clip". Session cannot be chosen.
2. Drop `drums-loop-120.wav` and `bass-phrase.flac` on the browser (left).
3. Drag the drum loop onto track 1 at bar 1, and again at bar 3. Two clips with waveforms appear.
4. Drag the bass phrase below the last track. A new track appears holding the clip.
5. Press Space. The playhead runs, the position display counts, you hear the clips. Press Space again to stop.
6. Press Stop once more. The playhead goes to `1 . 1 . 1`.
7. Click the ruler at bar 3 and play. Playback starts at bar 3.
8. Click the position display, type `2.3`, press Enter. The playhead is at bar 2, beat 3.
9. Press Ctrl+Z. The bass clip and its track disappear in one step.
10. Save (Ctrl+S), quit, reopen the project. The clips are back and play.
11. Open a project saved by the previous version (M1). It has one empty track and the title shows no `*`.

## D2: Edit clips

1. Place a 4-bar drum loop at bar 1 and a kick at bar 9 on track 1.
2. Drag the kick onto bar 3. The loop now ends at bar 3 and the kick sits there. Press Ctrl+Z: both are as before.
3. Set the loop to cover bars 1 to 4 and drop the kick at bar 2, inside it. The loop is split around the kick and still plays the right parts.
4. Drag the loop's left edge to bar 2. It starts later but its hits stay in place.
5. Select the loop, put the playhead at bar 3, press Ctrl+E. Two clips.
6. Press Ctrl+D twice.
7. Box-select three clips, drag them to track 2, press Delete.
8. Set Snap to 1/16 and drag a clip: fine steps. Hold Alt while dragging: free placement.
9. The Edit menu names each step ("Undo Move 3 clips" and so on).

## D3: Copy and paste, Explorer drops, zoom and scroll, keys

1. Copy a clip on track 1, select track 2, put the playhead at bar 9, press Ctrl+V. The copy is at bar 9 on track 2.
2. Cut (Ctrl+X) a clip and paste it elsewhere.
3. Drop two audio files from Explorer onto track 1 at bar 5. Grey placeholders appear, then two clips back to back. One Ctrl+Z removes both clips and both sounds.
4. Zoom with Ctrl+wheel around the mouse. Scroll with the wheel, Shift+wheel and the scroll bars. Shift+Z shows the whole song, Z the selection.
5. Click in the browser search box, type `z`, press Delete. Only the search text changes.

## D4: Tracks

1. Press Ctrl+T three times: tracks "Audio 2" to "Audio 4" appear with different colours.
2. Double-click "Audio 1", type `Drums`, press Enter. Rename the others `Bass`, `Chops`, `Vocal`.
3. Right-click "Bass" and choose teal. Its clips turn teal.
4. Drag the "Vocal" header to the top. It moves with its clips. Ctrl+Z puts it back.
5. Delete "Chops" while it holds a clip. Ctrl+Z brings the track and the clip back.
6. Click R: nothing happens, and the tooltip says recording comes later.

## D5: Mixer and meters

1. With drums and bass playing, drag the drum track's dB readout down. It gets quieter while you drag (it follows within about a second). One Ctrl+Z restores it. Double-click the readout: 0.0 dB.
2. Pan the bass fully left: you hear it only in the left ear.
3. Mute drums (click M, then press M with the track selected).
4. Solo bass, then Ctrl+click solo on drums: only drums are heard.
5. Watch the meters: they move with the hits and fall back smoothly.
6. Raise the master to +6 dB and a clip gain until the over light comes on. It stays on until you click it.

## D6: Loop, metronome, tempo, time signature

1. Drag across the empty loop bar from bar 5 to bar 9. A loop of bars 5 to 8 appears.
2. Press L and play from bar 4. Playback runs to the end of bar 8 and jumps to bar 5 with no gap, again and again.
3. Drag the loop end to bar 10, then drag the whole loop.
4. Turn Click on: a click per beat with an accent on beat 1.
5. Type tempo `90`. The music and the click slow down, clips stay on their bars, the grid follows.
6. Drag the tempo up until clips meet: the earlier clip gets shorter. Ctrl+Z restores it.
7. Set the time signature to 3/4. The ruler and the click accent change.

## D7: Clip inspector

1. Click a drum clip. The bottom panel shows its name, "Drums · Bars 5–8" and its waveform in the track colour.
2. Set GAIN to `-6` while playing. It gets quieter.
3. Type START `9.1.1`. The clip jumps to bar 9.
4. Turn Reverse on. After a short "rendering" the loop plays backwards and the waveform is mirrored.
5. Select two clips. The panel says "2 clips selected". FADE IN, FADE OUT and "Open in sample editor" are greyed out.

## D8: Headless render and golden tests

1. Create a demo project (once):
   ```
   set SAMPLER_MAKE_DEMO=%CD%\build\demo\Demo.sdaw
   build\windows-msvc-debug\tests\sampler_golden_tests.exe [demo]
   ```
   Or use your own project from D7.
2. Render it without opening the app:
   ```
   build\windows-msvc-debug\tools\sampler-cli\sampler-cli.exe render build\demo\Demo.sdaw %TEMP%\demo.wav
   ```
   Play `%TEMP%\demo.wav`: it sounds like the app, without the click.
3. Run the golden tests: `ctest --preset windows-msvc-debug -L golden`. All pass.
4. Make one fail on purpose: set `SAMPLER_GOLDEN_GAIN_DB=1` and run the golden tests again. Each case fails and shows its difference.
5. Listen once to the files in `tests\golden\` (these are what the tests compare against).

---

## Exit run

After D8, do the full run in `docs/qa-checklist.md`: build the 16-bar, 4-track arrangement from `concepts/project/Arrangement.dc.html` (Drums, Bass, Chops, Vocal; include a reused sound, a reversed clip and a loop over bars 5 to 8), mix it, save, quit, reopen, and render it with `sampler-cli`.
