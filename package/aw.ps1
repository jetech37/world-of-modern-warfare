# Azeroth Warfare: run the whole game from this folder, with no Docker or installs.
#   aw.ps1 play                    start the server if needed, then the game
#   aw.ps1 start | stop | status   the server (database, login server, world server)
#   aw.ps1 account NAME PASS [GM]  make or reset a login account (GM 0-6)
#   aw.ps1 setup                   first-run steps only (game folders, map data, databases)
#   aw.ps1 join ADDRESS            play on a friend's server (join localhost: your own)
#   aw.ps1 host ADDRESS            host for friends: the address they reach you at
# Everything this writes lives under .\var (settings, database, configs, logs); the batch files
# beside it call this script.
param(
    [Parameter(Position = 0)][string]$Command = 'play',
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest
)
# Cmdlet failures stop the script; a program's stderr does not (Windows PowerShell turns it into
# errors), so each program's exit code is checked instead.
$ErrorActionPreference = 'Continue'
$PSDefaultParameterValues['*:ErrorAction'] = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Bin = Join-Path $Root 'server\bin'
$Maria = Join-Path $Root 'mariadb\bin'
$Var = Join-Path $Root 'var'
$DbDir = Join-Path $Var 'db'
$Etc = Join-Path $Var 'etc'
$Logs = Join-Path $Var 'logs'
$Data = Join-Path $Root 'data'
$SettingsFile = Join-Path $Var 'settings.json'
$ClientCnf = Join-Path $Etc 'client.cnf'
$DbPort = 3310

function Say([string]$Text) { Write-Host "[aw] $Text" }

# --- Settings -------------------------------------------------------------------------------

function New-Secret {
    $bytes = New-Object byte[] 18
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    ([Convert]::ToBase64String($bytes)) -replace '[+/=]', 'x'
}

function Get-Settings {
    New-Item -ItemType Directory -Force $Var, $Etc, $Logs | Out-Null
    $s = if (Test-Path $SettingsFile) { Get-Content $SettingsFile -Raw | ConvertFrom-Json } else { New-Object psobject }
    $defaults = @{
        wow = ''; mw2 = ''; host = 'localhost'; publicAddress = '127.0.0.1'
        realmName = 'Azeroth Warfare'; dbRoot = (New-Secret); dbMangos = (New-Secret)
    }
    foreach ($k in $defaults.Keys) {
        if (-not ($s.PSObject.Properties.Name -contains $k)) { $s | Add-Member -NotePropertyName $k -NotePropertyValue $defaults[$k] }
    }
    Save-Settings $s
    $s
}

function Save-Settings($s) { $s | ConvertTo-Json | Set-Content -Encoding UTF8 $SettingsFile }

# --- Game folders ---------------------------------------------------------------------------

function Test-WowDir([string]$Dir) { $Dir -and (Test-Path (Join-Path $Dir 'Data\dbc.MPQ')) }
function Test-Mw2Dir([string]$Dir) { $Dir -and (Test-Path (Join-Path $Dir 'zone')) -and (Test-Path (Join-Path $Dir 'main')) }

function Select-Folder([string]$Prompt) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = $Prompt
    if ($dialog.ShowDialog() -eq 'OK') { $dialog.SelectedPath } else { '' }
}

# The packaged copy (game\WoW, game\MW2) when present, else the folder this player picked once.
function Resolve-GameDirs($s) {
    $packedWow = Join-Path $Root 'game\WoW'
    $packedMw2 = Join-Path $Root 'game\MW2'
    if (Test-WowDir $packedWow) { $s.wow = $packedWow }
    if (Test-Mw2Dir $packedMw2) { $s.mw2 = $packedMw2 }
    while (-not (Test-WowDir $s.wow)) {
        Say 'Pick your World of Warcraft 1.12.1 folder (the one holding WoW.exe and Data).'
        $s.wow = Select-Folder 'Your World of Warcraft 1.12.1 folder (holds WoW.exe and Data)'
        if (-not $s.wow) { throw 'No WoW folder picked.' }
        if (-not (Test-WowDir $s.wow)) { Say "That folder has no Data\dbc.MPQ; try again." }
    }
    while (-not (Test-Mw2Dir $s.mw2)) {
        Say 'Pick your Call of Duty: Modern Warfare 2 folder (the one holding main and zone).'
        $s.mw2 = Select-Folder 'Your Modern Warfare 2 folder (holds main and zone)'
        if (-not $s.mw2) { throw 'No MW2 folder picked.' }
        if (-not (Test-Mw2Dir $s.mw2)) { Say "That folder has no main and zone folders; try again." }
    }
    Save-Settings $s
}

# --- Map data -------------------------------------------------------------------------------

# dbc, maps and vmaps from the WoW client, with the server's own extractors (once; a few minutes).
function Initialize-MapData($s) {
    if ((Test-Path (Join-Path $Data '5875\dbc')) -and (Test-Path (Join-Path $Data 'maps')) -and (Test-Path (Join-Path $Data 'vmaps'))) { return }
    Say 'Extracting map data from your WoW client (once; this takes several minutes)...'
    $work = Join-Path $Var 'extract'
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
    New-Item -ItemType Directory -Force $work | Out-Null
    # The extractors read .\Data: a junction to the client's, nothing copied.
    cmd /c mklink /J "$work\Data" "$($s.wow)\Data" | Out-Null
    $tools = Join-Path $Bin 'Extractors'
    Push-Location $work
    try {
        & (Join-Path $tools 'MapExtractor.exe'); if ($LASTEXITCODE) { throw 'MapExtractor failed.' }
        & (Join-Path $tools 'VMapExtractor.exe'); if ($LASTEXITCODE) { throw 'VMapExtractor failed.' }
        New-Item -ItemType Directory -Force vmaps | Out-Null
        & (Join-Path $tools 'VMapAssembler.exe') Buildings vmaps; if ($LASTEXITCODE) { throw 'VMapAssembler failed.' }
    } finally { Pop-Location }
    New-Item -ItemType Directory -Force (Join-Path $Data '5875') | Out-Null
    Move-Item (Join-Path $work 'dbc') (Join-Path $Data '5875\dbc')
    Move-Item (Join-Path $work 'maps') $Data
    Move-Item (Join-Path $work 'vmaps') $Data
    cmd /c rmdir "$work\Data" | Out-Null
    Remove-Item -Recurse -Force $work
    Say 'Map data ready.'
}

# The World of Modern Warfare patch layer (game\WoW\Data\patch-9.MPQ) changes the Alterac tiles
# under Wrath's Dalaran: when it is new or changed since the map data was made, the map data is
# extracted again (the extractors read patch-9.MPQ, opened last), and if the package has mmaps,
# the nine tiles around Dalaran are rebuilt (map 0, tiles 30-32 x 30-32).
function Update-WomwMapData($s) {
    $patch = Join-Path $s.wow 'Data\patch-9.MPQ'
    if (-not (Test-Path $patch)) { return }
    $item = Get-Item $patch
    $stamp = "$($item.Length)-$($item.LastWriteTimeUtc.Ticks)"
    $stampFile = Join-Path $Data 'womw-patch.stamp'
    if ((Test-Path $stampFile) -and ((Get-Content $stampFile -Raw).Trim() -eq $stamp)) { return }
    # Map data made after the archive (a first-run extraction) already read it.
    $maps = Join-Path $Data 'maps'
    if (-not (Test-Path $stampFile) -and (Test-Path $maps) -and (Get-Item $maps).CreationTimeUtc -gt $item.LastWriteTimeUtc) {
        Set-Content -Encoding ascii $stampFile $stamp
        return
    }
    if (Test-Path (Join-Path $Data 'maps')) {
        Say 'The WotLK patch layer changed: extracting the map data again...'
        foreach ($d in 'maps', 'vmaps', '5875\dbc') {
            $p = Join-Path $Data $d
            if (Test-Path $p) { Remove-Item -Recurse -Force $p }
        }
    }
    Initialize-MapData $s
    if (Test-Path (Join-Path $Data 'mmaps')) {
        Say 'Rebuilding the paths around Dalaran...'
        Push-Location $Data
        try {
            foreach ($x in 30..32) { foreach ($y in 30..32) {
                & (Join-Path $Bin 'Extractors\MoveMapGenerator.exe') 0 --tile "$x,$y" --silent
                if ($LASTEXITCODE) { throw "MoveMapGenerator failed on tile $x,$y." }
            } }
        } finally { Pop-Location }
    }
    Set-Content -Encoding ascii $stampFile $stamp
}

# The World of Modern Warfare patch layer's tables (womw-patch\server-dbc: CreatureDisplayInfo with
# the later-version displays, CreatureModelData, Map with Dalaran), over the extracted ones: the
# extractors read only the stock archives. Copied again whenever the package ships newer ones.
function Update-WomwDbc {
    $from = Join-Path $Root 'womw-patch\server-dbc'
    $to = Join-Path $Data '5875\dbc'
    if (-not (Test-Path $from) -or -not (Test-Path $to)) { return }
    Get-ChildItem $from -Filter *.dbc | ForEach-Object { Copy-Item -Force $_.FullName $to }
}

# The community zones (game\WoW\Data\patch-A.MPQ, tools/community-merge): new and changed tiles on
# Azeroth and Kalimdor. When the archive is new or changed since the map data was made, the map
# data is extracted again (the extractors open patch-A.MPQ last), and if the package has mmaps,
# the merged tiles (womw-community\tiles.txt: map x y per line) get their paths rebuilt.
function Update-CommunityMapData($s) {
    $patch = Join-Path $s.wow 'Data\patch-A.MPQ'
    if (-not (Test-Path $patch)) { return }
    $item = Get-Item $patch
    $stamp = "$($item.Length)-$($item.LastWriteTimeUtc.Ticks)"
    $stampFile = Join-Path $Data 'womw-community.stamp'
    if ((Test-Path $stampFile) -and ((Get-Content $stampFile -Raw).Trim() -eq $stamp)) { return }
    $maps = Join-Path $Data 'maps'
    $fresh = -not (Test-Path $stampFile) -and (Test-Path $maps) -and (Get-Item $maps).CreationTimeUtc -gt $item.LastWriteTimeUtc
    if (-not $fresh) {
        if (Test-Path $maps) {
            Say 'The community zones changed: extracting the map data again...'
            foreach ($d in 'maps', 'vmaps', '5875\dbc') {
                $p = Join-Path $Data $d
                if (Test-Path $p) { Remove-Item -Recurse -Force $p }
            }
        }
        Initialize-MapData $s
    }
    $tiles = Join-Path $Root 'womw-community\tiles.txt'
    if ((Test-Path (Join-Path $Data 'mmaps')) -and (Test-Path $tiles)) {
        Say 'Rebuilding the paths of the community zones...'
        Push-Location $Data
        try {
            foreach ($line in Get-Content $tiles) {
                $m, $x, $y = $line -split '\s+'
                if (-not $m) { continue }
                # A line with a map id alone is a whole instanced map.
                if ($y) { & (Join-Path $Bin 'Extractors\MoveMapGenerator.exe') $m --tile "$x,$y" --silent }
                else { & (Join-Path $Bin 'Extractors\MoveMapGenerator.exe') $m --silent }
                if ($LASTEXITCODE) { throw "MoveMapGenerator failed on map $m tile $x,$y." }
            }
        } finally { Pop-Location }
    }
    Set-Content -Encoding ascii $stampFile $stamp
}

# The community zones' tables (womw-community\server-dbc), last: they hold the WotLK layer's rows
# too, so they go over Update-WomwDbc's copies.
function Update-CommunityDbc {
    $from = Join-Path $Root 'womw-community\server-dbc'
    $to = Join-Path $Data '5875\dbc'
    if (-not (Test-Path $from) -or -not (Test-Path $to)) { return }
    Get-ChildItem $from -Filter *.dbc | ForEach-Object { Copy-Item -Force $_.FullName $to }
}

# The community zones' world rows (server\sql\custom\azeroth_warfare\8*_community_*.sql, private,
# idempotent): a fresh database takes them with the other custom files; an existing one applies a
# file again whenever it changed.
function Update-CommunitySql {
    $files = Get-ChildItem (Join-Path $Root 'server\sql\custom\azeroth_warfare') -Filter '8*_community_*.sql' -ErrorAction SilentlyContinue
    if (-not $files) { return }
    $stampFile = Join-Path $Var 'community-sql.stamp'
    $stamp = ($files | Sort-Object Name | ForEach-Object { "$($_.Name)=$((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }) -join ';'
    if ((Test-Path $stampFile) -and ((Get-Content $stampFile -Raw).Trim() -eq $stamp)) { return }
    foreach ($f in $files | Sort-Object Name) { Say "Applying $($f.Name)"; Import-SqlFile mangos $f.FullName }
    Set-Content -Encoding ascii $stampFile $stamp
}

# --- Database -------------------------------------------------------------------------------

function Invoke-Sql([string]$Database, [string]$Sql) {
    $cliArgs = @("--defaults-extra-file=$ClientCnf", '-N')
    if ($Database) { $cliArgs += $Database }
    $Sql | & (Join-Path $Maria 'mariadb.exe') @cliArgs
    if ($LASTEXITCODE) { throw "SQL failed in '$Database'." }
}

function Import-SqlFile([string]$Database, [string]$File) {
    $path = (Resolve-Path $File).Path -replace '\\', '/'
    & (Join-Path $Maria 'mariadb.exe') "--defaults-extra-file=$ClientCnf" $Database -e "source $path"
    if ($LASTEXITCODE) { throw "Loading $File into $Database failed." }
}

# Many files through one connection (a connection each, a thousand times over, runs the machine
# out of them): a script of `source` lines.
function Import-SqlFiles([string]$Database, $Files) {
    if (-not $Files) { return }
    $list = Join-Path $Var 'migrate.sql'
    $Files | ForEach-Object { "source $($_.FullName -replace '\\', '/');" } | Set-Content -Encoding ASCII $list
    Import-SqlFile $Database $list
    Remove-Item $list
}

function Test-DbUp {
    & (Join-Path $Maria 'mariadb-admin.exe') "--defaults-extra-file=$ClientCnf" ping 2>&1 | Out-Null
    $LASTEXITCODE -eq 0
}

function Write-ClientCnf($s) {
    @"
[client]
host=127.0.0.1
port=$DbPort
user=root
password=$($s.dbRoot)
"@ | Set-Content -Encoding ASCII $ClientCnf
}

function Start-Db($s) {
    Write-ClientCnf $s
    if (-not (Test-Path $DbDir)) {
        Say 'Creating the database (once)...'
        & (Join-Path $Maria 'mariadb-install-db.exe') "--datadir=$DbDir" "--password=$($s.dbRoot)" "--port=$DbPort" | Out-Null
        if ($LASTEXITCODE) { throw 'mariadb-install-db failed.' }
    }
    if (Test-DbUp) { return }
    # The server's settings with this folder's paths, written every start: the my.ini the install
    # left in the data folder names the folder it was made in, and the package can move.
    $ini = Join-Path $Etc 'my.ini'
    @"
[mysqld]
datadir=$($DbDir.Replace('\', '/'))
port=$DbPort
bind-address=127.0.0.1
plugin-dir=$((Join-Path $Root 'mariadb\lib\plugin').Replace('\', '/'))
log-error=$((Join-Path $Logs 'mariadb.log').Replace('\', '/'))
"@ | Set-Content -Encoding ASCII $ini
    Start-Process -WindowStyle Hidden -FilePath (Join-Path $Maria 'mariadbd.exe') -ArgumentList @(
        "--defaults-file=`"$ini`""
    ) | Out-Null
    for ($i = 0; $i -lt 60 -and -not (Test-DbUp); $i++) { Start-Sleep -Seconds 1 }
    if (-not (Test-DbUp)) { throw "The database did not start; see $Logs\mariadb.log." }
}

function Test-Loaded([string]$Database) {
    $n = Invoke-Sql '' "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$Database';"
    [int]($n | Select-Object -First 1) -gt 0
}

# The four databases, as the Docker setup made them (server\azeroth-warfare\scripts\setup-db.sh).
function Initialize-Db($s) {
    $sql = Join-Path $Root 'server\sql'
    Invoke-Sql '' @"
CREATE DATABASE IF NOT EXISTS realmd DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS mangos DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS characters DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS logs DEFAULT CHARSET utf8;
CREATE USER IF NOT EXISTS 'mangos'@'localhost' IDENTIFIED BY '$($s.dbMangos)';
CREATE USER IF NOT EXISTS 'mangos'@'127.0.0.1' IDENTIFIED BY '$($s.dbMangos)';
GRANT ALL PRIVILEGES ON realmd.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON mangos.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON characters.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON logs.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
FLUSH PRIVILEGES;
"@
    # Each database is loaded once, when it is empty, then migrated once: the migrations ran
    # against an existing one already (a fresh snapshot, or data moved over from another server).
    $sources = [ordered]@{ realmd = "$sql\logon.sql"; characters = "$sql\characters.sql"; logs = "$sql\logs.sql"; mangos = '' }
    $kinds = @{ realmd = 'logon'; characters = 'characters'; logs = 'logs'; mangos = 'world' }
    foreach ($name in @($sources.Keys)) {
        if (Test-Loaded $name) { continue }
        if ($name -eq 'mangos') {
            Say 'Loading the world database (once; a few minutes)...'
            $work = Join-Path $Var 'world'
            if (Test-Path $work) { Remove-Item -Recurse -Force $work }
            Expand-Archive -Path (Join-Path $Root 'server\world.zip') -DestinationPath $work
            $world = Get-ChildItem -Recurse $work -Filter *.sql | Where-Object { $_.Name -match 'world|mangos' } | Select-Object -First 1
            if (-not $world) { $world = Get-ChildItem -Recurse $work -Filter *.sql | Select-Object -First 1 }
            Import-SqlFile mangos $world.FullName
            Remove-Item -Recurse -Force $work
        } else {
            Import-SqlFile $name $sources[$name]
        }
        Say "Migrating $name..."
        Import-SqlFiles $name (Get-ChildItem "$sql\migrations" -Filter "*_$($kinds[$name]).sql" | Sort-Object Name)
        if ($name -eq 'mangos') {
            Get-ChildItem "$sql\custom\azeroth_warfare" -Filter *.sql | Sort-Object Name |
                ForEach-Object { Say "Applying $($_.Name)"; Import-SqlFile mangos $_.FullName }
        }
    }
    # The realm's name and address, updated in place: the world server keeps its own columns
    # (online, builds) in the same row. The login server lists a realm only to the builds in
    # `realmbuilds`, so 1.12.1's is there from the start.
    Invoke-Sql realmd @"
INSERT INTO realmlist (id, name, address, localAddress, port, icon, realmflags, timezone, allowedSecurityLevel, population, gamebuild_min, gamebuild_max, realmbuilds)
VALUES (1, '$($s.realmName)', '$($s.publicAddress)', '$($s.publicAddress)', 8085, 1, 2, 1, 0, 0, 5875, 5875, '5875 ')
ON DUPLICATE KEY UPDATE name = VALUES(name), address = VALUES(address), localAddress = VALUES(localAddress),
    realmbuilds = IF(realmbuilds = '', VALUES(realmbuilds), realmbuilds);
"@
}

# --- Server ---------------------------------------------------------------------------------

function Write-Configs($s) {
    $db = { param($name) "`"127.0.0.1;$DbPort;mangos;$($s.dbMangos);$name`"" }
    $dataDir = $Data -replace '\\', '/'
    $logsDir = $Logs -replace '\\', '/'
    (Get-Content (Join-Path $Bin 'realmd.conf.dist')) `
        -replace '^LoginDatabaseInfo = .*', "LoginDatabaseInfo = $(& $db realmd)" `
        -replace '^LogsDir = .*', "LogsDir = `"$logsDir`"" |
        Set-Content -Encoding ASCII (Join-Path $Etc 'realmd.conf')
    New-Item -ItemType Directory -Force (Join-Path $Data 'fusion') | Out-Null
    $weapons = Join-Path $Data 'fusion\weapons.json'
    if (-not (Test-Path $weapons)) { Copy-Item (Join-Path $Root 'server\seed\weapons.json') $weapons }
    # The catalog ships with the build: refreshed every start.
    Copy-Item -Force (Join-Path $Root 'server\seed\catalog.json') (Join-Path $Data 'fusion\catalog.json')
    (Get-Content (Join-Path $Bin 'mangosd.conf.dist')) `
        -replace '^LoginDatabase\.Info = .*', "LoginDatabase.Info = $(& $db realmd)" `
        -replace '^WorldDatabase\.Info = .*', "WorldDatabase.Info = $(& $db mangos)" `
        -replace '^CharacterDatabase\.Info = .*', "CharacterDatabase.Info = $(& $db characters)" `
        -replace '^LogsDatabase\.Info = .*', "LogsDatabase.Info = $(& $db logs)" `
        -replace '^DataDir = .*', "DataDir = `"$dataDir`"" `
        -replace '^LogsDir = .*', "LogsDir = `"$logsDir`"" `
        -replace '^mmap\.enabled = .*', "mmap.enabled = $([int](Test-Path (Join-Path $Data 'mmaps')))" |
        Set-Content -Encoding ASCII (Join-Path $Etc 'mangosd.conf')
}

function Get-ServerProcess([string]$Name) {
    Get-Process -Name $Name -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Bin, 'OrdinalIgnoreCase') }
}

function Wait-WorldUp {
    $log = Join-Path $Logs 'Server.log'
    for ($i = 0; $i -lt 300; $i++) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'World initialized' -Quiet)) { return $true }
        if (-not (Get-ServerProcess 'mangosd')) { return $false }
        Start-Sleep -Seconds 1
    }
    $false
}

function Start-Server {
    $s = Get-Settings
    if ((Get-ServerProcess 'mangosd') -and (Get-ServerProcess 'realmd')) { Say 'Server is up.'; return }
    Resolve-GameDirs $s
    Initialize-MapData $s
    Update-WomwMapData $s
    Update-CommunityMapData $s
    Update-WomwDbc
    Update-CommunityDbc
    Start-Db $s
    Initialize-Db $s
    Update-CommunitySql
    Write-Configs $s
    if (-not (Get-ServerProcess 'realmd')) {
        Start-Process -WindowStyle Minimized -WorkingDirectory $Bin -FilePath (Join-Path $Bin 'realmd.exe') -ArgumentList @('-c', "`"$Etc\realmd.conf`"") | Out-Null
    }
    if (-not (Get-ServerProcess 'mangosd')) {
        $log = Join-Path $Logs 'Server.log'
        if (Test-Path $log) { Move-Item -Force $log "$log.previous" }
        Say 'Starting the world server (its window has the GM console)...'
        Start-Process -WindowStyle Minimized -WorkingDirectory $Bin -FilePath (Join-Path $Bin 'mangosd.exe') -ArgumentList @('-c', "`"$Etc\mangosd.conf`"") | Out-Null
        if (-not (Wait-WorldUp)) { throw "The world server did not come up; see $Logs\Server.log." }
    }
    Say 'Server is up.'
}

# Ctrl+Break to a console process: the world server saves every player and exits cleanly. Sent
# from a helper PowerShell, since attaching to another console detaches this one.
function Send-CtrlBreak([int]$ProcessId) {
    $helper = @"
Add-Type -Namespace W -Name K -MemberDefinition '
[DllImport("kernel32.dll")] public static extern bool FreeConsole();
[DllImport("kernel32.dll")] public static extern bool AttachConsole(uint p);
[DllImport("kernel32.dll")] public static extern bool SetConsoleCtrlHandler(System.IntPtr h, bool add);
[DllImport("kernel32.dll")] public static extern bool GenerateConsoleCtrlEvent(uint e, uint g);'
[W.K]::FreeConsole() | Out-Null
if ([W.K]::AttachConsole($ProcessId)) { [W.K]::SetConsoleCtrlHandler([IntPtr]::Zero, `$true) | Out-Null; [W.K]::GenerateConsoleCtrlEvent(1, 0) | Out-Null }
"@
    # Encoded, so the command line keeps its quotes.
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($helper))
    Start-Process -WindowStyle Hidden -Wait -FilePath powershell.exe -ArgumentList @('-NoProfile', '-EncodedCommand', $encoded) | Out-Null
}

function Stop-Server {
    foreach ($name in 'mangosd', 'realmd') {
        foreach ($p in @(Get-ServerProcess $name)) {
            Say "Stopping $name..."
            Send-CtrlBreak $p.Id
            if (-not $p.WaitForExit(60000)) { Say "$name did not stop in time; ending it."; Stop-Process -Id $p.Id -Force }
        }
    }
    Write-ClientCnf (Get-Settings)
    if (Test-DbUp) {
        & (Join-Path $Maria 'mariadb-admin.exe') "--defaults-extra-file=$ClientCnf" shutdown 2>&1 | Out-Null
        for ($i = 0; $i -lt 60 -and (Get-Process mariadbd -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Maria*" }); $i++) { Start-Sleep -Seconds 1 }
    }
    Say 'Server stopped.'
}

function Show-Status {
    foreach ($name in 'mangosd', 'realmd') { Say ("{0}: {1}" -f $name, $(if (Get-ServerProcess $name) { 'running' } else { 'stopped' })) }
    Write-ClientCnf (Get-Settings)
    Say ("database: {0}" -f $(if (Test-DbUp) { 'running' } else { 'stopped' }))
}

function New-Account([string[]]$A) {
    if ($A.Count -lt 2) {
        # Double-clicked: ask.
        $name = Read-Host 'Account name'
        $pass = Read-Host 'Password'
        $gm = Read-Host 'GM level 0-6 (Enter for 0)'
        $A = @($name, $pass)
        if ($gm) { $A += $gm }
        if (-not $name -or -not $pass) { throw 'usage: aw.ps1 account NAME PASSWORD [GM level 0-6]' }
    }
    $s = Get-Settings
    Start-Db $s
    $sql = & (Join-Path $Bin 'aw-account.exe') @A
    if ($LASTEXITCODE) { throw 'aw-account failed.' }
    Invoke-Sql realmd ($sql -join "`n") | Out-Null
    Say "Account $($A[0]) ready."
}

function Start-Game {
    $s = Get-Settings
    if ($s.host -eq 'localhost') { Start-Server; $s = Get-Settings } else { Resolve-GameDirs $s }
    $env:WOW_DATA = Join-Path $s.wow 'Data'
    $env:MW2_PATH = $s.mw2
    $env:WOW_HOST = $s.host
    $client = Join-Path $Root 'client'
    Start-Process -WorkingDirectory $client -FilePath (Join-Path $client 'benilla-mw2.exe') | Out-Null
}

switch ($Command) {
    'play' { Start-Game }
    'start' { Start-Server }
    'stop' { Stop-Server }
    'status' { Show-Status }
    'account' { New-Account $Rest }
    # Play on someone else's server: `join 203.0.113.7`; `join localhost` plays your own again.
    'join' {
        if (-not $Rest) { throw 'usage: aw.ps1 join ADDRESS|localhost' }
        $s = Get-Settings; $s.host = $Rest[0]; Save-Settings $s; Say "Play now connects to $($s.host)."
    }
    # Host for friends: the address they reach you at goes in the realm list.
    'host' {
        if (-not $Rest) { throw 'usage: aw.ps1 host PUBLIC_ADDRESS' }
        $s = Get-Settings; $s.publicAddress = $Rest[0]; Save-Settings $s
        Say "Friends join with: aw.ps1 join $($s.publicAddress). Restart the server to apply; open TCP 3724 and 8085."
    }
    # The world database again from the snapshot: game content only, no players' data.
    'reload-world' {
        if (Get-ServerProcess 'mangosd') { throw 'Stop the server first (Stop Server.bat).' }
        $s = Get-Settings; Start-Db $s
        Invoke-Sql '' 'DROP DATABASE IF EXISTS mangos;'
        Initialize-Db $s; Say 'World database reloaded.'
    }
    'setup' { $s = Get-Settings; Resolve-GameDirs $s; Initialize-MapData $s; Update-WomwMapData $s; Update-CommunityMapData $s; Update-WomwDbc; Update-CommunityDbc; Start-Db $s; Initialize-Db $s; Update-CommunitySql; Write-Configs $s; Say 'Setup done.' }
    default { Write-Host 'usage: aw.ps1 play|start|stop|status|account NAME PASS [GM]|join ADDRESS|host ADDRESS|setup'; exit 2 }
}
