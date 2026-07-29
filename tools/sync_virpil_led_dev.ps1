<#
Dev-only helper: copies virpil_led_control.py and virpil_led_rules.json
from the source tree (scripts_module/examples/) to the LOCAL deployed
ScriptsModule folder (dist/ScriptsModule/examples/) that this repo's own
dist/GremlinNexus/scripts_config.json points at.

Why this exists: Nexus's Scripts panel always runs the script from
wherever scripts_config.json says, which is the DEPLOYED copy, never the
source tree directly - editing scripts_module/examples/*.py has zero
effect on an already-set-up Nexus session until this copy step runs (see
Memory.md's "Editar un script de ejemplo... no alcanza" entry). This is
purely a local dev convenience - a real release doesn't need this at all,
since tools/package_scripts_module.ps1 already builds ScriptsModule.zip
straight from source each time.

Usage:
    .\tools\sync_virpil_led_dev.ps1

After running this, Stop then Start the script again in Nexus's Scripts
panel - it only rereads its files on (re)launch, not while "Running".
#>

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$src = Join-Path $repoRoot "scripts_module\examples"
$dst = Join-Path $repoRoot "dist\ScriptsModule\examples"

if (-not (Test-Path $dst)) {
    throw "Deployed folder not found: $dst - run tools\package_scripts_module.ps1 first."
}

$files = @("virpil_led_control.py", "virpil_led_rules.json")
foreach ($file in $files) {
    $srcPath = Join-Path $src $file
    if (Test-Path $srcPath) {
        Copy-Item -Path $srcPath -Destination $dst -Force
        Write-Host "Copied $file"
    }
}

Write-Host "Done. Now Stop then Start the script in Nexus's Scripts panel to pick up the change."
