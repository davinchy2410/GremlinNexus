<#
Packages the downloadable "Scripts module" (Fase 19, Script Bridge, step
6/6) - an embedded Windows Python interpreter plus the nexus_bridge SDK,
zipped up for a GitHub Releases upload. This is NOT run as part of the
normal GremlinNexus build (see MasterPlan.md's "distribucion de Python"
decision: this module is a separate, optional download, mirroring how
vJoy/HidHide/ViGEmBus aren't bundled in the main installer either) - run it
by hand whenever nexus_bridge itself changes and a new ScriptsModule.zip
needs publishing.

Usage:
    .\tools\package_scripts_module.ps1
    .\tools\package_scripts_module.ps1 -PythonVersion 3.12.7
#>

param(
    [string]$PythonVersion = "3.12.7",
    [string]$OutputDir = "dist/ScriptsModule"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$moduleSource = Join-Path $repoRoot "scripts_module"
$outDir = Join-Path $repoRoot $OutputDir
$pythonEmbedDir = Join-Path $outDir "python-embed"

if (Test-Path $outDir) {
    Remove-Item -Recurse -Force $outDir -Confirm:$false
}
New-Item -ItemType Directory -Force -Path $pythonEmbedDir | Out-Null

# 1) Download the official Windows embeddable Python distribution.
$zipUrl = "https://www.python.org/ftp/python/$PythonVersion/python-$PythonVersion-embed-amd64.zip"
$zipPath = Join-Path $env:TEMP "python-embed-$PythonVersion.zip"
Write-Host "Downloading $zipUrl ..."
Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath
Expand-Archive -Path $zipPath -DestinationPath $pythonEmbedDir -Force
Remove-Item $zipPath -Force

# 2) Patch the embeddable distribution's own ._pth file so `import
#    nexus_bridge` resolves reliably. Gotcha: whenever a pythonXY._pth file
#    is present next to python.exe, CPython's startup enters an isolated
#    mode that IGNORES the PYTHONPATH environment variable entirely (only
#    the directories literally listed in ._pth end up on sys.path) - so the
#    PYTHONPATH env var ScriptsViewModel::startScript() sets is not enough
#    on its own with the embeddable distribution. Adding a ".." line here
#    (relative to python.exe, i.e. this ScriptsModule folder, which is
#    where nexus_bridge/ lives) makes it resolve independent of that.
$pthFile = Get-ChildItem -Path $pythonEmbedDir -Filter "python*._pth" | Select-Object -First 1
if (-not $pthFile) {
    throw "Could not find a python*._pth file inside the extracted embeddable distribution."
}
Add-Content -Path $pthFile.FullName -Value "`n.."

# 3) Copy the nexus_bridge SDK, docs and examples alongside python-embed.
Copy-Item -Path (Join-Path $moduleSource "nexus_bridge") -Destination $outDir -Recurse -Force
Copy-Item -Path (Join-Path $moduleSource "README.md") -Destination $outDir -Force
Copy-Item -Path (Join-Path $moduleSource "SCRIPTING_GUIDE.md") -Destination $outDir -Force
Copy-Item -Path (Join-Path $moduleSource "examples") -Destination $outDir -Recurse -Force

# 4) Zip it up for a GitHub Releases upload.
$zipOutput = Join-Path $repoRoot "dist/ScriptsModule.zip"
if (Test-Path $zipOutput) {
    Remove-Item $zipOutput -Force
}
Compress-Archive -Path "$outDir\*" -DestinationPath $zipOutput

Write-Host "Scripts module assembled at: $outDir"
Write-Host "Distributable zip at: $zipOutput"
