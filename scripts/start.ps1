param(
    [ValidateSet("debug", "release")]
    [string]$Config = "debug",
    [switch]$NoBuild,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)
# Builds the Sampler app (unless -NoBuild) and starts it. Extra arguments go to Sampler.exe.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$preset = "windows-msvc-$Config"
$cfgDir = (Get-Culture).TextInfo.ToTitleCase($Config)

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Preset $preset -Target Sampler
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$exe = Join-Path $root "build\$preset\src\app\Sampler_artefacts\$cfgDir\Sampler.exe"
if (-not (Test-Path $exe)) { throw "Sampler.exe not found at $exe (build it first)" }
& $exe @AppArgs
