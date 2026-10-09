param(
    [string]$Preset = "windows-msvc-debug",
    [string]$Target = "",
    [switch]$Test,
    [switch]$ConfigureOnly
)
# Runs configure + build (+ ctest) inside the Visual Studio 2022 x64 environment (any edition).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio 2022 (Desktop C++)" }
# VS 2022 is major version 17; [17.0,18.0) excludes VS 2026 (v18), whose MSVC we do not build with yet.
$vsPath = & $vswhere -latest -products * -version "[17.0,18.0)" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio 2022 with the C++ x64 tools was not found" }
$vc = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vc)) { throw "vcvars64.bat not found under $vsPath" }

$cmake = "cmake --preset $Preset"
if (-not $ConfigureOnly) {
    $cmake += " && cmake --build --preset $Preset"
    if ($Target) { $cmake += " --target $Target" }
    if ($Test) { $cmake += " && ctest --preset $Preset" }
}
cmd /c "`"$vc`" >nul 2>&1 && cd /d `"$root`" && $cmake"
exit $LASTEXITCODE
