# Copies the server's nightly database backups (/var/backups/aw) to this PC, so a copy survives
# if the server is lost. Run it from a scheduled task (daily, and at logon so a day the PC was
# off is caught up); also safe to run by hand. Keeps 30 days here.
# The server, the key and the folder come from vps\deploy.env.local (see vps\deploy.env.example):
# AW_VPS, AW_VPS_KEY and AW_BACKUP_DEST; -Dest overrides the folder.
param([string]$Dest = '')
$ErrorActionPreference = 'Stop'

function Read-DeployConfig {
    $file = Join-Path $PSScriptRoot '..\vps\deploy.env.local'
    if (-not (Test-Path $file)) {
        throw "No server settings: copy vps\deploy.env.example to vps\deploy.env.local and fill it in."
    }
    $cfg = @{}
    foreach ($line in Get-Content $file) {
        if ($line -match '^\s*#' -or $line -notmatch '=') { continue }
        $name, $value = $line -split '=', 2
        $name = $name.Trim()
        # The environment wins over the file.
        $envValue = [Environment]::GetEnvironmentVariable($name)
        $cfg[$name] = if ($envValue) { $envValue } else { $value.Trim() }
    }
    foreach ($k in @($cfg.Keys)) {
        if ($cfg[$k] -like '~*') { $cfg[$k] = $env:USERPROFILE + $cfg[$k].Substring(1) }
    }
    $cfg
}

$cfg = Read-DeployConfig
if (-not $cfg.AW_VPS) { throw 'AW_VPS (the server''s ssh login) is not set in vps\deploy.env.local.' }
if (-not $Dest) { $Dest = $cfg.AW_BACKUP_DEST }
if (-not $Dest) { throw 'AW_BACKUP_DEST (where the backups go) is not set in vps\deploy.env.local.' }
$vps = $cfg.AW_VPS
$keyArgs = if ($cfg.AW_VPS_KEY) { @('-i', $cfg.AW_VPS_KEY) } else { @() }
New-Item -ItemType Directory -Force $Dest | Out-Null
$log = Join-Path $Dest 'pull.log'
try {
    $remote = & ssh @keyArgs -o BatchMode=yes -o ConnectTimeout=20 $vps 'ls /var/backups/aw' |
        Where-Object { $_ -like '*.sql.gz' }
    $got = 0
    foreach ($f in $remote) {
        if (Test-Path (Join-Path $Dest $f)) { continue }
        & scp -q @keyArgs -o BatchMode=yes "${vps}:/var/backups/aw/$f" (Join-Path $Dest "$f.part")
        if ($LASTEXITCODE) { throw "scp $f failed" }
        Move-Item -Force (Join-Path $Dest "$f.part") (Join-Path $Dest $f)
        $got++
    }
    Get-ChildItem $Dest -Filter *.sql.gz | Where-Object LastWriteTime -lt (Get-Date).AddDays(-30) |
        Remove-Item -Force
    "$(Get-Date -Format s) ok, $got new file(s)" | Add-Content $log
} catch {
    "$(Get-Date -Format s) FAILED: $_" | Add-Content $log
    exit 1
}
