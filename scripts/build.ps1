param(
    [string]$Preset = "windows-msvc-debug",
    [string]$Target = "",
    [switch]$Test
)
# Runs configure + build (+ ctest) inside the Visual Studio x64 environment.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$vc = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vc)) { $vc = "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" }
$cmake = "cmake --preset $Preset && cmake --build --preset $Preset"
if ($Target) { $cmake += " --target $Target" }
if ($Test) { $cmake += " && ctest --preset $Preset" }
cmd /c "`"$vc`" >nul 2>&1 && cd /d `"$root`" && $cmake"
exit $LASTEXITCODE
