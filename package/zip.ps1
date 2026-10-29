# Azeroth Warfare: zip dist\AzerothWarfare for friends, leaving out var\ (your database,
# accounts, characters, settings and logs). Each friend's first run makes their own.
#   package\zip.ps1 [-Out <file.zip>]
param([string]$Out = '')
$ErrorActionPreference = 'Stop'

$Dist = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'
if (-not (Test-Path (Join-Path $Dist 'AzerothWarfare\Play.bat'))) { throw 'No dist\AzerothWarfare: run package\build.ps1 first.' }
if (-not $Out) { $Out = Join-Path $Dist ("AzerothWarfare-{0:yyyy-MM-dd}.zip" -f (Get-Date)) }
if (Test-Path $Out) { Remove-Item -Force $Out }

foreach ($part in 'WoW', 'MW2') {
    $has = Test-Path (Join-Path $Dist "AzerothWarfare\game\$part\*")
    Write-Host ("[zip] game\{0}: {1}" -f $part, $(if ($has) { 'included' } else { 'not included (players pick their own)' }))
}
Write-Host "[zip] Writing $Out (var\ left out)..."
# Windows' own tar writes zip64, so files past 4 GB and archives past 4 GB both work.
& "$env:SystemRoot\System32\tar.exe" -a -c -f $Out -C $Dist --exclude 'AzerothWarfare/var' AzerothWarfare
if ($LASTEXITCODE) { throw 'tar failed.' }
Write-Host ("[zip] Done: {0} ({1:N1} GB)." -f $Out, ((Get-Item $Out).Length / 1GB))
