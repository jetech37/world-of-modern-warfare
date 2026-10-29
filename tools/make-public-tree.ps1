# Export this repository's committed tree into a fresh folder for the public repository: every
# tracked file at -Ref (HEAD by default), minus what tools/public-exclude.txt lists (data made from
# game installs, private documents, the list itself), then scan the result for secrets,
# deployment details and game-derived files. The folder is ready for `git init`.
#
#   powershell -ExecutionPolicy Bypass -File tools\make-public-tree.ps1 -Out <folder> [-Ref HEAD] [-Force] [-Strict]
#
#   -Out     the folder to write (made if missing; must be empty unless -Force, which empties it)
#   -Ref     the commit to export (uncommitted changes are never exported)
#   -Strict  exit 1 when the scan finds anything (for a final check before pushing)
#
# The exclusion list (tools/public-exclude.txt) and the extra scan markers
# (tools/private-markers.txt, one literal, case-sensitive string per line, or re:<regex>: your server's address,
# user names, ...)
# are private and are not exported. Without them the export still works: nothing is excluded
# (a warning says so) and only the generic scan runs.
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Ref = 'HEAD',
    [switch]$Force,
    [switch]$Strict
)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $PSScriptRoot

function Say([string]$Text) { Write-Host "[public-tree] $Text" }

# --- The tree -------------------------------------------------------------------------------
$commit = (& git -C $Repo rev-parse --verify "$Ref^{commit}").Trim()
if ($LASTEXITCODE) { throw "No commit $Ref." }
$dirty = & git -C $Repo status --porcelain --untracked-files=no
if ($dirty) { Say "Note: the working tree has uncommitted changes; only $Ref ($($commit.Substring(0, 12))) is exported." }

$Out = [IO.Path]::GetFullPath($Out)
if ($Out.TrimEnd('\') -ieq $Repo.TrimEnd('\')) { throw 'The export folder cannot be the repository itself.' }
if (Test-Path $Out) {
    $existing = Get-ChildItem -Force $Out
    if ($existing) {
        if (-not $Force) { throw "$Out is not empty (use -Force to empty it first)." }
        $existing | Remove-Item -Recurse -Force
    }
} else {
    New-Item -ItemType Directory -Force $Out | Out-Null
}

$tarFile = Join-Path ([IO.Path]::GetTempPath()) ("womw-public-" + [guid]::NewGuid().ToString('N') + '.tar')
try {
    & git -C $Repo archive --format=tar -o $tarFile $commit
    if ($LASTEXITCODE) { throw 'git archive failed.' }
    & tar -xf $tarFile -C $Out
    if ($LASTEXITCODE) { throw 'Unpacking the archive failed.' }
} finally {
    Remove-Item -Force -ErrorAction SilentlyContinue $tarFile
}
Say "Exported $commit into $Out."

# --- Exclusions -----------------------------------------------------------------------------
function Read-List([string]$Path) {
    if (-not (Test-Path $Path)) { return @() }
    Get-Content $Path | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') }
}

$listFile = Join-Path $PSScriptRoot 'public-exclude.txt'
$patterns = @(Read-List $listFile)
if (-not $patterns) { Say "WARNING: no $listFile; nothing is excluded." }
$removed = 0
foreach ($p in $patterns) {
    $rel = $p.Replace('/', '\')
    $matched = @()
    if ($rel.EndsWith('\')) {
        $dir = Join-Path $Out $rel.TrimEnd('\')
        if (Test-Path $dir) { $matched = @($dir) }
    } elseif ($rel -match '[*?]') {
        $parent = Split-Path -Parent $rel
        $leaf = Split-Path -Leaf $rel
        $base = if ($parent) { Join-Path $Out $parent } else { $Out }
        if (Test-Path $base) { $matched = @(Get-ChildItem -Force $base -Filter $leaf | ForEach-Object { $_.FullName }) }
    } else {
        $file = Join-Path $Out $rel
        if (Test-Path $file) { $matched = @($file) }
    }
    if (-not $matched) { Say "  (nothing at $p)"; continue }
    foreach ($m in $matched) {
        $count = if (Test-Path $m -PathType Container) { @(Get-ChildItem -Recurse -File -Force $m).Count } else { 1 }
        Remove-Item -Recurse -Force $m
        $removed += $count
        Say ("  excluded {0}{1}" -f $m.Substring($Out.Length + 1).Replace('\', '/'), $(if ($count -gt 1) { " ($count files)" } else { '' }))
    }
}
Say "$removed file(s) excluded."

# --- The scan -------------------------------------------------------------------------------
$files = @(Get-ChildItem -Recurse -File -Force $Out)
$findings = New-Object System.Collections.Generic.List[string]
function Note([string]$Kind, [string]$Where, [string]$What) {
    $findings.Add(("{0,-10} {1}: {2}" -f $Kind, $Where, $What))
}
function Rel([IO.FileInfo]$f) { $f.FullName.Substring($Out.Length + 1).Replace('\', '/') }

# Files that are game assets by their kind, or too big to be source.
$assetExt = '.blp', '.m2', '.wmo', '.adt', '.wdt', '.wdl', '.mpq', '.dbc', '.skin', '.anim', '.bls',
    '.ff', '.iwd', '.iwi', '.xmodel', '.xanim', '.hkx', '.bsa', '.ba2', '.esm', '.esp', '.nif',
    '.dds', '.tga', '.wav', '.ogg', '.mp3', '.flac', '.bik', '.vmap', '.vmtree', '.vmtile', '.mmap',
    '.mmtile', '.map', '.sql.gz', '.dmp', '.pdb'
foreach ($f in $files) {
    $name = $f.Name.ToLowerInvariant()
    foreach ($e in $assetExt) { if ($name.EndsWith($e)) { Note 'asset' (Rel $f) "a $e file" } }
    if ($f.Length -gt 1MB) { Note 'big' (Rel $f) ("{0:N1} MB" -f ($f.Length / 1MB)) }
}

# Text: keys, tokens, passwords, addresses, e-mail addresses, local paths, game-file paths.
$textExt = '.rs', '.py', '.ps1', '.sh', '.bat', '.cmd', '.md', '.txt', '.toml', '.json', '.yml', '.yaml',
    '.html', '.js', '.css', '.cpp', '.h', '.hpp', '.c', '.cmake', '.sql', '.conf', '.in', '.dist',
    '.service', '.timer', '.example', '.xml', '.tsv', '.cfg', '.ini', '.env'
$generic = [ordered]@{
    'key'      = '-----BEGIN [A-Z ]*PRIVATE KEY-----|ssh-(rsa|ed25519) AAAA'
    'token'    = 'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|AKIA[0-9A-Z]{16}|xox[abpr]-[A-Za-z0-9-]{10,}|sk-[A-Za-z0-9]{32,}|AIza[0-9A-Za-z_-]{30,}'
    'password' = '(?i)(password|passwd|pwd|secret|api_?key|token)\s*[:=]\s*["''][^"''\s]{6,}["'']'
    'ipv4'     = '(?<![\d.])(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)(?![\d.])'
    'email'    = '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
    'path'     = '(?i)[A-Z]:\\Users\\[^\\\s"'']+|/home/[a-z][a-z0-9_-]+/|/mnt/c/Users/[^/\s]+'
    'gamepath' = '(?i)meshes\\\\actors|\.hkx"|steamapps\\\\common\\\\[^"]*\\\\(main|zone)\\\\'
}
# Addresses that are fine anywhere: loopback, any, and the documentation ranges.
$okIp = '^(127\.|0\.0\.0\.0$|255\.|192\.0\.2\.|198\.51\.100\.|203\.0\.113\.|10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)'
$markers = @(Read-List (Join-Path $PSScriptRoot 'private-markers.txt'))
$ipCounts = @{}
foreach ($f in $files) {
    $ext = $f.Extension.ToLowerInvariant()
    if ($textExt -notcontains $ext -and $f.Name -notmatch '^(Caddyfile|LICENSE.*|NOTICE.*|README.*|CNAME|Dockerfile.*)$') { continue }
    if ($f.Length -gt 4MB) { continue }
    $rel = Rel $f
    $lines = [IO.File]::ReadAllLines($f.FullName)
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $line = $lines[$i]
        foreach ($kind in $generic.Keys) {
            foreach ($m in [regex]::Matches($line, $generic[$kind])) {
                $v = $m.Value
                if ($kind -eq 'ipv4') {
                    if ($v -match $okIp) { continue }
                    # Version numbers and the like (1.12.1.5875) are not addresses worth a line each:
                    # counted per file instead.
                    $ipCounts["$rel"] = 1 + [int]$ipCounts["$rel"]
                    if ($ipCounts["$rel"] -gt 3) { continue }
                }
                Note $kind "${rel}:$($i + 1)" $v
            }
        }
        foreach ($mk in $markers) {
            # A marker that starts with re: is a regular expression (for example re:alice(?!smith),
            # a user name that must not match inside a public handle); any other is a literal.
            $hit = if ($mk.StartsWith('re:')) { [regex]::IsMatch($line, $mk.Substring(3)) }
                   else { $line.IndexOf($mk, [StringComparison]::Ordinal) -ge 0 }
            if ($hit) { Note 'MARKER' "${rel}:$($i + 1)" $mk }
        }
    }
}
foreach ($k in $ipCounts.Keys) {
    if ($ipCounts[$k] -gt 3) { Note 'ipv4' $k "$($ipCounts[$k]) address-like strings in all" }
}

$report = Join-Path ([IO.Path]::GetTempPath()) 'womw-public-scan.txt'
$findings | Sort-Object | Set-Content -Encoding utf8 $report
Say ("{0} file(s) exported; the scan found {1} line(s) to review: {2}" -f $files.Count, $findings.Count, $report)
foreach ($kind in 'MARKER', 'key', 'token', 'password', 'asset', 'gamepath', 'path', 'big', 'email', 'ipv4') {
    $n = @($findings | Where-Object { $_.StartsWith($kind + ' ') -or $_.StartsWith($kind.PadRight(10)) }).Count
    if ($n) { Say ("  {0,-9} {1}" -f $kind, $n) }
}
Say 'Next: review the scan, then in the export folder: git init -b main; git add -A; git commit -m "World of Modern Warfare"'
if ($Strict -and $findings.Count) { exit 1 }
