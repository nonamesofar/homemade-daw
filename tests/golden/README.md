# Golden renders

`GoldenProjects.cpp` builds seventeen small, plugin-free projects through the real commands (from the audio fixtures in
`tests/audio-fixtures`), renders each offline at 48 kHz / block 512, and compares it with `<case>.wav` here. A render
passes when it has the same length as the reference and the peak of the difference is at or below -90 dBFS.

| Case | What it covers |
|---|---|
| four-tracks | clips on four tracks, a reused sound |
| overlap-trim | a clip dropped inside another splits it |
| split | one clip split at a bar |
| left-trim | trimming the start keeps the audio in time |
| clip-gain | clip gain -6 dB |
| reverse | a reversed clip |
| volume-pan | track volume and pan |
| mute-solo | mute and solo rules |
| master-volume | master volume -8 dB |
| tempo-change | clips keep their bars when the tempo drops to 90 BPM |
| warp-beats | the 90 BPM break in Beats mode fills bars 1 to 4 at 120 BPM; kicks on bars 1 and 2 to compare by ear (M4a) |
| warp-tones | a 440 Hz tone recorded at 100 BPM in Tones mode at 130 BPM: still 440 Hz, 4 bars (M4a) |
| warp-repitch | the same tone in Re-pitch mode at 150 BPM: faster and higher, 660 Hz (M4a) |
| warp-tempo-change | Beats, Tones and Re-pitch clips placed at 120 BPM, then the tempo drops to 100 BPM: they keep their bars (M4a) |
| beats-transpose | the break in Beats mode with +5 semitones: same four bars, hits on the same beats, higher (M4a) |
| clip-loop | the first bar of the drum loop with LOOP on and a length of 4 bars: the bar four times, no gap (M4a) |
| fades | a 0.5 s fade in and 1 s fade out on a tone; a bass clip with fades split in two: the fade in stays on the first part, the fade out on the second (M4a) |

Run them: `ctest --preset windows-msvc-debug -L golden`.

## Changing playback on purpose

Rewrite the references with `SAMPLER_UPDATE_GOLDEN=1` (PowerShell: `$env:SAMPLER_UPDATE_GOLDEN=1`, then run the
golden tests), or only some of them with a comma-separated list of case names (`SAMPLER_UPDATE_GOLDEN=warp-beats,warp-tones`). **Listen to every changed file before committing it**: a reference that sounds wrong makes the test
pass for the wrong reason. The WAVs are stored with Git LFS.

To see a failure on purpose, run with `SAMPLER_GOLDEN_GAIN_DB=1`: every clip gets 1 dB more and each case fails
with its difference shown.
