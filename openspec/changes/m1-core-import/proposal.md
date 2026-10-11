# Proposal

## What this means for the user (plain language)

After this change, Sampler is a real app for the first time, not a test bench. With it the user can:

- Start Sampler and get a dark window laid out like the mock, with a sample browser on the left and an (empty) arrangement area.
- Create a new project, save it, close the app, open the project again and find everything as it was. The window title shows the project name and whether there are unsaved changes; closing with unsaved changes asks first.
- Drag audio files from Explorer onto the window (or use File > Import) to add them to the project. WAV, AIFF, FLAC, MP3 and OGG files work. The user's own files are never changed: the project keeps its own copy. Importing the same file twice does not create a second copy. Files that are too long (over 30 minutes) or cannot be read give a clear message.
- MP3 loops play without the short silence that MP3 files normally have at the start, so they loop cleanly.
- Undo and redo an import (Ctrl+Z / Ctrl+Y, or the Edit menu).
- See every imported sound in the browser with its name and a small waveform picture. Type in the search box to filter by name. Pick a category (All samples, Loops, One-shots, Recordings) to filter by type. Change a sound's category by hand.
- Click a sound to hear it; click again to stop. A click made in the first moment after start-up, while the audio system is still getting ready, plays as soon as it is ready instead of being lost.

The work is split into seven small demos that the product owner checks one by one on the same branch (the demo list is in `tasks.md`). Each demo ends with a short script: what to click and what to see or hear.

Not in this change: putting sounds on the timeline (M2), recording computer audio (M2b), the sample editor and slicing (M3), detecting the tempo of a loop (M3; until then the category is a guess from the length, which the user can correct), autosave and crash recovery (M4), a sample library shared between projects (later), and keyboard shortcuts for previewing sounds.

## Why

M0 proved the building blocks; M1 turns them into the app every later milestone builds on: the main window, the project file, the single path for all edits with undo, and the import pipeline that every sound goes through. M2 (arrangement) needs imported sounds to drag onto the timeline, and the M0 findings left two items for M1: trimming MP3 encoder delay at import and replacing the throwaway spike app.

## What Changes

- **The real app** (`src/app`): main window, menu bar (File, Edit), the dark look from the mock with embedded IBM Plex fonts, the first reusable widgets. Replaces the M0 spike app.
- **Projects**: new / open / save / save as for the `.sdaw` folder, with the `SAMPLER` data section (schema version 1) inside the project file, refusal of files from a newer version, an unsaved-changes prompt, and the cache folder set on every open.
- **Edits and undo**: every change to a project goes through one command service, one undo step per command, with debug checks after each command.
- **Import**: copy into the project, de-duplicate by content hash, decode once to a 32-bit float WAV, MP3 gapless trim from the LAME tag, 30-minute limit, down-mix of more than two channels, errors shown as a toast, decode on a worker thread with a progress indicator.
- **Browser**: this project's sounds only, with search, categories (length-based guess, user-editable), waveform thumbnails and click-to-audition (mouse only; a click before the engine is ready is queued).
- **Demo and test audio**: a small generator tool that synthesises a fixed set of sounds and writes them as WAV, AIFF, FLAC, OGG and LAME-encoded MP3. The files live in Git LFS and are used both for the demos and the tests. This brings the LAME encoder library in now (M4 needs it for MP3 export).
- **Clean-up as we go**: remove the spike app; move the test plugin into `tests/`, the probes and benchmarks into `tools/`, so `apps/` disappears; move the M0 test helpers out of the engine library into a test-only library; drop the M4A probe; reshape the spike-era engine interface into the real project and audition services; fix stale paths in the docs.
- No **BREAKING** changes for users (no released app). The spike executable goes away; the build-and-ci spec is updated accordingly.

## Capabilities

### New Capabilities
- `app-shell`: the main window, menus, look and feel, window title and the unsaved-changes prompt.
- `undo-redo`: every project edit is one undoable step through a single command path, with redo and debug consistency checks.
- `audio-import`: bringing audio files into a project: accepted formats, copying, de-duplication, decoding, MP3 gapless trim, limits, channel policy and error handling.
- `sample-browser`: listing, searching and categorising a project's sounds, with waveform thumbnails.
- `audition`: previewing a sound through the audio output, independent of the project transport, including the start-up queue.

### Modified Capabilities
- `project-bundle`: adds creating, opening and saving a project from the app, the app's data section with a schema version, refusing newer files, and the imported-audio folder layout.
- `build-and-ci`: the clean-checkout build now produces the app and tools instead of the spike; adds reproducible audio fixtures stored in Git LFS.

## Impact

- New code: `src/app`, `src/model`, `src/commands`, `src/io` (import, bundle), `src/ui` (look and feel, widgets, browser), `src/engine` (project edit, audition, thumbnails), `tools/make-fixtures`, `tests/audio-fixtures`.
- Moved or removed: `apps/spike` (deleted), `apps/testplugin` -> `tests/testplugin`, `apps/probes` and `apps/benchmarks` -> `tools/`, `EditLab`/`PluginLab` -> test-only library, `MediaFoundationProbe` -> the decode probe tool.
- New dependency: LAME 3.100 (`libmp3lame`, LGPL) as a pinned submodule with our CMake wrapper; IBM Plex fonts (OFL) as embedded data.
- Git LFS now also covers `tests/audio-fixtures/`.
- Docs: `README.md`, `docs/building.md`, `ROADMAP.md` (M1 change line; the exit criterion says 5 formats, now WAV, AIFF, FLAC, MP3, OGG since M4A was descoped at M0).
