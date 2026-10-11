# Projects

A Sampler project is a folder named `Name.sdaw` (a "bundle"). Everything the project needs is inside it, so you can copy, move or back it up as one folder.

```
Name.sdaw/
  project.tracktionedit     the project file (XML); Sampler's own data is the <SAMPLER> section inside it
  audio/
    originals/              exact copies of the files you imported
    <name>-<hash>.wav       the decoded working copies the app plays (32-bit float WAV)
  cache/                    disposable: waveform pictures and other generated files
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

## The cache folder

`cache/` holds only things the app can make again (waveform pictures now; stretched renders and similar later). You can delete it while the app is closed; the next time you open the project it fills up again and nothing is lost.

## For developers

The `<SAMPLER schemaVersion="1">` section has one `SOURCE` node per imported sound (id, name, relative paths of the original and the decoded copy, content hash, sample rate, channels, frames, category). All paths are relative to the bundle. A newer `schemaVersion` than the app knows is refused; older ones are upgraded in memory by the steps in `src/model/Migrations.cpp` (none yet).
