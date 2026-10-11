# Golden renders

`GoldenProjects.cpp` builds ten small, plugin-free projects through the real commands (from the audio fixtures in
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

Run them: `ctest --preset windows-msvc-debug -L golden`.

## Changing playback on purpose

Rewrite the references with `SAMPLER_UPDATE_GOLDEN=1` (PowerShell: `$env:SAMPLER_UPDATE_GOLDEN=1`, then run the
golden tests). **Listen to every changed file before committing it**: a reference that sounds wrong makes the test
pass for the wrong reason. The WAVs are stored with Git LFS.

To see a failure on purpose, run with `SAMPLER_GOLDEN_GAIN_DB=1`: every clip gets 1 dB more and each case fails
with its difference shown.
