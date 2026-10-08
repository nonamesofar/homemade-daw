# Building (Windows)

Prerequisites: Visual Studio 2022 (or Build Tools) with the C++ desktop workload, CMake 3.28+, Ninja, Git LFS.

```
git clone --recurse-submodules <repo>
```

Run the commands from an "x64 Native Tools Command Prompt for VS 2022" (the presets use the `cl` on the PATH):

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

The spike app is `build/windows-msvc-debug/apps/spike/SamplerSpike_artefacts/Debug/Sampler Spike.exe`.
