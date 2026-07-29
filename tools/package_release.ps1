<#
Packages the two GitHub Releases download artifacts for a GremlinNexus
build - GremlinNexus_Portable_<version>.zip and
GremlinNexus_Installer_<version>.exe - with the actual version number baked
into both file names (previously always "GremlinNexus_Portable.zip"/
"GremlinNexus_Installer.exe", indistinguishable from one release to the
next once downloaded).

Version defaults to `git describe --tags --always --dirty` - the exact same
value CMakeLists.txt bakes into the app itself as NEXUS_APP_VERSION (see its
own comment there), so the file name and the app's own About panel always
agree. Pass -Version explicitly to override (e.g. for a release built from
a detached/shallow checkout with no tag history).

Assumes dist\GremlinNexus already exists and is up to date (a normal build +
windeployqt pass) - this script does not build or deploy the app itself.

Usage:
    .\tools\package_release.ps1
    .\tools\package_release.ps1 -Version 1.1.0-beta.2
    .\tools\package_release.ps1 -SkipInstaller   # portable zip only, e.g. if Inno Setup isn't installed
#>

param(
    [string]$Version = "",
    [string]$OutputDir = "release_output",
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$distDir = Join-Path $repoRoot "dist\GremlinNexus"

if (-not (Test-Path $distDir)) {
    throw "dist\GremlinNexus not found - build and windeployqt the app before running this script."
}

if ($Version -eq "") {
    $Version = (git -C $repoRoot describe --tags --always --dirty).Trim()
}
Write-Host "Packaging version: $Version"

$outDir = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# --- Portable ZIP ------------------------------------------------------
# Stages a clean copy rather than zipping dist\GremlinNexus in place - that
# folder is also the deployed build a developer actively runs Nexus from
# day to day, so excluding Logs\* (runtime log files, not part of the
# shipped app - same exclusion installer.iss's own [Files] section makes)
# has to happen on a throwaway copy, never by deleting anything out of the
# real deploy folder itself.
$stageDir = Join-Path $env:TEMP "GremlinNexus_release_stage"
if (Test-Path $stageDir) {
    Remove-Item -Recurse -Force $stageDir -Confirm:$false
}
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
Copy-Item -Path "$distDir\*" -Destination $stageDir -Recurse -Force
$stageLogs = Join-Path $stageDir "Logs"
if (Test-Path $stageLogs) {
    Remove-Item -Recurse -Force $stageLogs -Confirm:$false
}

$zipPath = Join-Path $outDir "GremlinNexus_Portable_$Version.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}
Compress-Archive -Path "$stageDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Recurse -Force $stageDir -Confirm:$false
Write-Host "Portable zip: $zipPath"

# --- Installer (Inno Setup) ---------------------------------------------
if ($SkipInstaller) {
    Write-Host "Skipping installer (-SkipInstaller)."
    exit 0
}

$isccCandidates = @(
    "$env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Warning "ISCC.exe (Inno Setup Compiler) not found - skipping installer. Portable zip was still built."
    exit 0
}

& $iscc "/DMyAppVersion=$Version" (Join-Path $repoRoot "installer.iss")
if ($LASTEXITCODE -ne 0) {
    throw "ISCC.exe failed with exit code $LASTEXITCODE"
}

$installerPath = Join-Path $repoRoot "installer_output\GremlinNexus_Installer_$Version.exe"
Copy-Item -Path $installerPath -Destination $outDir -Force
Write-Host "Installer: $(Join-Path $outDir "GremlinNexus_Installer_$Version.exe")"

Write-Host "`nRelease artifacts ready in: $outDir"
