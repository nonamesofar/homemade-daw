param(
    [string]$Preset = "windows-msvc-debug",
    [string]$Target = "",
    [switch]$Test,
    [switch]$ConfigureOnly
)
# Runs configure + build (+ ctest) inside the latest Visual Studio x64 environment (2022 or newer, any edition).
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: install Visual Studio 2022 or newer (Desktop C++)" }
# Newest VS 2022 or later (17.x, 18.x): the same compiler that windows-latest uses in CI.
$vsPath = & $vswhere -latest -products * -version "[17.0,)" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio (2022 or newer) with the C++ x64 tools was not found" }
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
