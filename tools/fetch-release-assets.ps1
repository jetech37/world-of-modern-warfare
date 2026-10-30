# Copies the small data that ships in git (game-data/) into private-assets/, then downloads the
# large files that are not in git (tools/release-assets.json) from the project's site
# (default https://womw.net/downloads/; -BaseUrl or WOMW_DOWNLOADS overrides it), checks their
# SHA-256 and unpacks the small-data zip. Files already present with the right hash are skipped.
#
#   powershell -ExecutionPolicy Bypass -File tools\fetch-release-assets.ps1 [-BaseUrl https://host/path/] [-Only patch-9.MPQ,...] [-Out <folder>]
#
# Every file is optional for building; see private-assets.md for what each one adds. The stock
# WoW 1.12 client is never downloaded: you supply your own.
#
# The usual way: install the game with the launcher (make an account at play.womw.net), then
#   powershell -ExecutionPolicy Bypass -File tools\fetch-release-assets.ps1 -FromInstall <launcher install folder>
# copies the large files out of that install instead of downloading them again.
param(
    [string]$BaseUrl,
    [string[]]$Only,
    [string]$Out,
    [string]$FromInstall
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Out) { $Out = Join-Path $root 'private-assets' }
$m = Get-Content -Raw (Join-Path $PSScriptRoot 'release-assets.json') | ConvertFrom-Json
if ($FromInstall) {
    $inGit = Join-Path $root 'game-data'
    if (Test-Path $inGit) { New-Item -ItemType Directory -Force $Out | Out-Null; Copy-Item -Recurse -Force (Join-Path $inGit '*') $Out }
    foreach ($f in $m.files) {
        if ($Only -and ($Only -notcontains $f.asset)) { continue }
        # The launcher keeps the layers in game\WoW\Data and the optional HD pack in hd\.
        $src = @((Join-Path $FromInstall "game\WoW\Data\$($f.asset)"), (Join-Path $FromInstall "hd\$($f.asset)")) |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $src) { Write-Host "missing   $($f.asset) (not in that install; the HD pack is optional)"; continue }
        $dest = Join-Path $Out $f.path
        New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
        Copy-Item -Force $src $dest
        Write-Host "copied    $($f.asset)"
    }
    return
}
if (-not $BaseUrl) { $BaseUrl = if ($env:WOMW_DOWNLOADS) { $env:WOMW_DOWNLOADS } else { $m.baseUrl } }
$base = $BaseUrl.TrimEnd('/')
$inGit = Join-Path $root 'game-data'
if (Test-Path $inGit) {
    New-Item -ItemType Directory -Force $Out | Out-Null
    Copy-Item -Recurse -Force (Join-Path $inGit '*') $Out
    Write-Host "copied    game-data\ into $Out"
}
foreach ($f in $m.files) {
    if ($Only -and ($Only -notcontains $f.asset)) { continue }
    $isZip = $f.asset -like '*.zip'
    $dest = if ($isZip) { Join-Path $env:TEMP $f.asset } else { Join-Path $Out $f.path }
    if (-not $isZip -and (Test-Path $dest) -and $f.sha256 -and
        (Get-FileHash $dest -Algorithm SHA256).Hash -ieq $f.sha256) {
        Write-Host "ok        $($f.path)"; continue
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Write-Host "download  $($f.asset) ($([math]::Round($f.size / 1MB)) MB)"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri "$base/$($f.asset)" -OutFile "$dest.part" -UseBasicParsing
    if ($f.sha256 -and (Get-FileHash "$dest.part" -Algorithm SHA256).Hash -ine $f.sha256) {
        Remove-Item "$dest.part"; throw "$($f.asset): SHA-256 does not match tools/release-assets.json (from $base)"
    }
    Move-Item -Force "$dest.part" $dest
    if ($isZip) { Expand-Archive -Force $dest $Out; Remove-Item $dest; Write-Host "unpacked  $($f.asset) into $Out" }
}
Write-Host "Done. package\build.ps1 ships what is in $Out."
