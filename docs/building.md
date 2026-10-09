# Building (Windows)

Prerequisites: Visual Studio 2022 or newer (or Build Tools; CI uses the newest on `windows-latest`) with the C++ desktop workload, CMake 3.28+, Ninja, Git LFS.

```
git clone --recurse-submodules <repo>
```

Run the commands from an "x64 Native Tools Command Prompt for VS" (the presets use the `cl` on the PATH):

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Or let the script find the newest VS (any edition, via vswhere) and set up the environment: `powershell -File scripts/build.ps1 -Preset windows-msvc-debug -Test`.

Catch2 tags are ctest labels. Every test runs by default; two kinds are noisy and can be left out:

- `realtime`: plays sound on the default output device and records it through loopback.
- `gui`: opens a plugin window and posts mouse clicks to it.

```
ctest --preset windows-msvc-debug -LE "realtime|gui"
```

JUCE and Tracktion are compiled once, into `sampler_juce` and `sampler_tracktion` (`cmake/SamplerJuce.cmake`), optimised even in Debug and without warnings-as-errors. Our own targets link those, never `juce::juce_*` directly; only the test plugin builds its own JUCE copy.

The spike app is `build/windows-msvc-debug/apps/spike/SamplerSpike_artefacts/Debug/Sampler Spike.exe`.
