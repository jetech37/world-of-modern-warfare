# Azeroth Warfare: build and start the client against your own game installs.
#
#   .\scripts\play.ps1 -Wow "C:\Games\WoW 1.12.1" -Mw2 "C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare 2" -Server 203.0.113.7
#
# The paths are remembered in scripts\play.local.json (git-ignored), so next time a bare
# .\scripts\play.ps1 is enough. Nothing from either game is copied: the client reads them in place.
param(
    [string]$Wow,
    [string]$Mw2,
    [string]$Server
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$saved = Join-Path $PSScriptRoot "play.local.json"
$cfg = @{}
if (Test-Path $saved) {
    (Get-Content $saved -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $cfg[$_.Name] = $_.Value }
}
if ($Wow) { $cfg.wow = $Wow }
if ($Mw2) { $cfg.mw2 = $Mw2 }
if ($Server) { $cfg.server = $Server }
if (-not $cfg.server) { $cfg.server = "localhost" }

# WoW 1.12.1 (build 5875, English): the folder holding WoW.exe and Data\.
if (-not $cfg.wow -or -not (Test-Path (Join-Path $cfg.wow "Data"))) {
    throw "Give -Wow: your WoW 1.12.1 folder (the one with WoW.exe and a Data folder)."
}
# Modern Warfare 2 (Steam, PC): the folder holding main\ and zone\.
if (-not $cfg.mw2 -or -not (Test-Path (Join-Path $cfg.mw2 "zone"))) {
    throw "Give -Mw2: your Call of Duty Modern Warfare 2 folder (the one with a zone folder)."
}
$cfg | ConvertTo-Json | Set-Content -Encoding utf8 $saved

$env:WOW_DATA = Join-Path $cfg.wow "Data"
$env:MW2_PATH = $cfg.mw2
$env:WOW_HOST = $cfg.server
Write-Host "WoW data:  $env:WOW_DATA"
Write-Host "MW2:       $env:MW2_PATH"
Write-Host "Server:    $env:WOW_HOST"
Push-Location $root
try {
    cargo run --release
} finally {
    Pop-Location
}
