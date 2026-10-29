# Azeroth Warfare: assemble the shareable folder (dist\AzerothWarfare) from this repository.
#   package\build.ps1 [-Data <dir>] [-Wow <dir>] [-Mw2 <dir>] [-SkipCompile]
#     -Data  copy already-extracted map data (holds 5875\dbc, maps, vmaps) so players skip the
#            first-run extraction
#     -Wow, -Mw2  copy those game folders into game\WoW and game\MW2 (otherwise each player
#            picks their own installs on first run)
# Needs tools\cmake and tools\mariadb (portable, unpacked from tools\downloads), Visual Studio
# 2022's C++ tools and Rust. The world database snapshot is tools\downloads\world.zip.
param([string]$Data = '', [string]$Wow = '', [string]$Mw2 = '', [switch]$SkipCompile)
$ErrorActionPreference = 'Stop'

$Repo = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Repo 'dist\AzerothWarfare'
$Cmake = Join-Path $Repo 'tools\cmake\bin\cmake.exe'
function Say([string]$Text) { Write-Host "[package] $Text" }
function Copy-Tree([string]$From, [string]$To) {
    New-Item -ItemType Directory -Force $To | Out-Null
    # Two quick retries: a locked file (a running server) fails fast instead of waiting.
    robocopy $From $To /E /R:2 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Copying $From failed." }
}

# The package's programs are replaced below: the server and game in it must be stopped.
$running = Get-Process mangosd, realmd, benilla-mw2 -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($Out, 'OrdinalIgnoreCase') }
if ($running) { throw "Stop the server (aw.ps1 stop) and close the game first: $($running.Name -join ', ') running from $Out." }

if (-not $SkipCompile) {
    Say 'Building the server...'
    if (-not (Test-Path (Join-Path $Repo 'build\server\CMakeCache.txt'))) {
        & $Cmake -S (Join-Path $Repo 'server') -B (Join-Path $Repo 'build\server') -G 'Visual Studio 17 2022' -A x64 `
            -DSUPPORTED_CLIENT_BUILD=5875 -DBUILD_EXTRACTORS=ON -DBUILD_FOR_HOST_CPU=OFF `
            -DCMAKE_INSTALL_PREFIX="$(Join-Path $Repo 'build\server-install')"
        if ($LASTEXITCODE) { throw 'Configuring the server failed.' }
    }
    # Two projects at a time: an unbounded build has exhausted memory before.
    & $Cmake --build (Join-Path $Repo 'build\server') --config Release -- /m:2 /v:minimal
    if ($LASTEXITCODE) { throw 'Building the server failed.' }
    Say 'Building the game client...'
    Push-Location (Join-Path $Repo 'client')
    try { cargo build --release; if ($LASTEXITCODE) { throw 'Building the client failed.' } } finally { Pop-Location }
}

Say "Assembling $Out..."
New-Item -ItemType Directory -Force $Out | Out-Null
$stage = Join-Path $Repo 'build\server-install'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $Cmake --install (Join-Path $Repo 'build\server') --config Release --prefix $stage
if ($LASTEXITCODE) { throw 'Installing the server failed.' }

# Server: programs, configs, SQL, seeds and the world snapshot.
$server = Join-Path $Out 'server'
# On Windows the install is flat: programs and config templates together, the extractors in
# Extractors\.
Copy-Tree $stage (Join-Path $server 'bin')
$sql = Join-Path $Repo 'server\sql'
New-Item -ItemType Directory -Force (Join-Path $server 'sql') | Out-Null
foreach ($f in 'logon.sql', 'characters.sql', 'logs.sql') { Copy-Item -Force (Join-Path $sql $f) (Join-Path $server 'sql') }
Copy-Tree (Join-Path $sql 'migrations') (Join-Path $server 'sql\migrations')
Copy-Tree (Join-Path $sql 'custom\azeroth_warfare') (Join-Path $server 'sql\custom\azeroth_warfare')
New-Item -ItemType Directory -Force (Join-Path $server 'seed') | Out-Null
Copy-Item -Force (Join-Path $Repo 'server\contrib\fusion\weapons.json') (Join-Path $server 'seed')
# The catalog is made from an MW2 install, so a public checkout has none: the maintainer keeps it
# in private-assets\ (see private-assets.md).
$catalog = Join-Path $Repo 'server\contrib\fusion\catalog.json'
if (-not (Test-Path $catalog)) { $catalog = Join-Path $Repo 'private-assets\server\contrib\fusion\catalog.json' }
if (-not (Test-Path $catalog)) { throw 'No fusion catalog.json (server\contrib\fusion or private-assets\server\contrib\fusion): see private-assets.md to make one.' }
Copy-Item -Force $catalog (Join-Path $server 'seed\catalog.json')
# Unix line endings: a checkout with core.autocrlf gives CRLF, and the migrations' `delimiter`
# lines then never match.
Get-ChildItem -Recurse (Join-Path $server 'sql') -Filter *.sql | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName)
    if ($text.Contains("`r`n")) { [IO.File]::WriteAllText($_.FullName, $text.Replace("`r`n", "`n")) }
}
$world = Join-Path $Repo 'tools\downloads\world.zip'
if (-not (Test-Path $world)) { throw "No world database snapshot at $world (vmangos release db_latest)." }
Copy-Item -Force $world (Join-Path $server 'world.zip')

# The database, portable.
Copy-Tree (Join-Path $Repo 'tools\mariadb\bin') (Join-Path $Out 'mariadb\bin')
Copy-Tree (Join-Path $Repo 'tools\mariadb\share') (Join-Path $Out 'mariadb\share')
Copy-Tree (Join-Path $Repo 'tools\mariadb\lib') (Join-Path $Out 'mariadb\lib')

# The game client.
New-Item -ItemType Directory -Force (Join-Path $Out 'client') | Out-Null
Copy-Item -Force (Join-Path $Repo 'client\target\release\benilla-mw2.exe') (Join-Path $Out 'client')
# Its symbols (benilla_mw2.pdb) never go to players: kept with a copy of the exe in
# build\symbols\staged, which publish.py files under the version it publishes, so crash reports
# from that version can be symbolised (vps/ADMIN.md, Crash reports).
$staged = Join-Path $Repo 'build\symbols\staged'
if (Test-Path $staged) { Remove-Item -Recurse -Force $staged }
New-Item -ItemType Directory -Force $staged | Out-Null
Copy-Item -Force (Join-Path $Repo 'client\target\release\benilla-mw2.exe') $staged
$pdb = Join-Path $Repo 'client\target\release\benilla_mw2.pdb'
if (Test-Path $pdb) { Copy-Item -Force $pdb $staged } else { Say 'No benilla_mw2.pdb: crash reports from this build cannot be symbolised.' }

# Visual C++ runtime beside the programs (app-local, as Microsoft's redistribution terms allow),
# so players need no installer.
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$crt = Get-ChildItem -Directory "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" | Sort-Object FullName | Select-Object -Last 1
foreach ($dir in (Join-Path $server 'bin'), (Join-Path $server 'bin\Extractors'), (Join-Path $Out 'client')) {
    if (Test-Path $dir) { Copy-Item -Force "$($crt.FullName)\*.dll" $dir }
}

# Launchers.
New-Item -ItemType Directory -Force (Join-Path $Out 'scripts') | Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot 'aw.ps1') (Join-Path $Out 'scripts')
Get-ChildItem $PSScriptRoot -Filter *.bat | ForEach-Object { Copy-Item -Force $_.FullName $Out }
Copy-Item -Force (Join-Path $PSScriptRoot 'README.txt') $Out

# Optional: map data and game folders.
if ($Data) {
    Say 'Copying map data...'
    Copy-Tree $Data (Join-Path $Out 'data')
}
if ($Wow) { Say 'Copying WoW...'; Copy-Tree $Wow (Join-Path $Out 'game\WoW') }
if ($Mw2) { Say 'Copying MW2...'; Copy-Tree $Mw2 (Join-Path $Out 'game\MW2') }

# The World of Modern Warfare patch layer (private-assets\womw-patch, built from a WotLK 3.3.5a
# install by `cargo run --release --example wotlk_import`; private-assets.md): the archive goes
# into the packaged game data, where it mounts above the stock 1.12 archives, and its tables go
# where aw.ps1 lays them over the server's extracted ones. Never in git.
$womw = Join-Path $Repo 'private-assets\womw-patch'
if (Test-Path (Join-Path $womw 'patch-9.MPQ')) {
    $gameData = Join-Path $Out 'game\WoW\Data'
    if (Test-Path $gameData) {
        Say 'Copying the WotLK patch layer...'
        Copy-Item -Force (Join-Path $womw 'patch-9.MPQ') $gameData
    } else { Say 'No game\WoW\Data in the package: the WotLK patch layer (patch-9.MPQ) is not copied.' }
    Copy-Tree (Join-Path $womw 'server-dbc') (Join-Path $Out 'womw-patch\server-dbc')
    if (Test-Path (Join-Path $Out 'data\5875\dbc')) { Copy-Item -Force (Join-Path $womw 'server-dbc\*.dbc') (Join-Path $Out 'data\5875\dbc') }
} else { Say 'No private-assets\womw-patch\patch-9.MPQ: the package ships without the WotLK models and Dalaran.' }

# The community zones (private-assets\womw-community, built by tools/community-merge; see
# private-assets.md): patch-A.MPQ into the game data (it mounts above patch-9), its tables and
# tile list where aw.ps1 lays them over the server's, and the generated world SQL beside our own
# custom SQL. Build it after the WotLK layer: its tables carry that layer's rows. Never in git.
$community = Join-Path $Repo 'private-assets\womw-community'
if (Test-Path (Join-Path $community 'patch-A.MPQ')) {
    $gameData = Join-Path $Out 'game\WoW\Data'
    if (Test-Path $gameData) {
        Say 'Copying the community zones...'
        Copy-Item -Force (Join-Path $community 'patch-A.MPQ') $gameData
    } else { Say 'No game\WoW\Data in the package: the community zones (patch-A.MPQ) are not copied.' }
    Copy-Tree (Join-Path $community 'server-dbc') (Join-Path $Out 'womw-community\server-dbc')
    Copy-Item -Force (Join-Path $community 'tiles.txt') (Join-Path $Out 'womw-community')
    if (Test-Path (Join-Path $Out 'data\5875\dbc')) { Copy-Item -Force (Join-Path $community 'server-dbc\*.dbc') (Join-Path $Out 'data\5875\dbc') }
    $privSql = Join-Path $Repo 'private-assets\server\sql\custom\azeroth_warfare'
    if (Test-Path $privSql) {
        Get-ChildItem $privSql -Filter '8*_community_*.sql' | ForEach-Object {
            $text = [IO.File]::ReadAllText($_.FullName).Replace("`r`n", "`n")
            [IO.File]::WriteAllText((Join-Path $server "sql\custom\azeroth_warfare\$($_.Name)"), $text)
        }
    }
} else { Say 'No private-assets\womw-community\patch-A.MPQ: the package ships without the community zones.' }

# The optional HD pack (private-assets\womw-hd, built by `community-merge hd`; design/HD_PACK.md):
# its archives and manifest into hd\ beside the game, never into game\WoW\Data, so the base game
# and the server's extractors never see it. The client mounts it when it is there and Options >
# Graphics > HD Textures and Models is on; publish.py --hd offers it as a separate download.
$hdSrc = Join-Path $Repo 'private-assets\womw-hd'
$hdManifest = Join-Path $hdSrc 'womw-hd.txt'
if (Test-Path $hdManifest) {
    Say 'Copying the HD pack...'
    $hdOut = Join-Path $Out 'hd'
    New-Item -ItemType Directory -Force $hdOut | Out-Null
    $parts = Get-Content $hdManifest | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { ($_ -split "`t")[0] }
    # The manifest goes last and comes down first, so a half-copied pack never mounts.
    Remove-Item -Force (Join-Path $hdOut 'womw-hd.txt') -ErrorAction SilentlyContinue
    Get-ChildItem $hdOut -Filter 'womw-hd-*.MPQ' | Where-Object { $parts -notcontains $_.Name } | Remove-Item -Force
    robocopy $hdSrc $hdOut $parts /R:2 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw 'Copying the HD pack failed.' }
    Copy-Item -Force $hdManifest $hdOut
} else { Say 'No private-assets\womw-hd\womw-hd.txt: the package ships without the optional HD pack.' }

$size = (Get-ChildItem -Recurse -File $Out | Measure-Object Length -Sum).Sum / 1GB
Say ("Done: {0} ({1:N1} GB)." -f $Out, $size)
