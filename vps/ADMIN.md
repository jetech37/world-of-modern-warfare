# Running the server for friends

Everything here is one command from the PC (PowerShell or Git Bash), or a double-click on a `.bat`
in `admin/`. `aw-admin` lives on the VPS.

Your server's details live in `vps/deploy.env.local` (gitignored): copy `vps/deploy.env.example`
there and fill in `AW_VPS` (the ssh login, e.g. `root@203.0.113.10`), `AW_VPS_KEY` (its key),
`AW_PUBLIC_HOST`, `AW_GAME_HOST` and the rest. The `.bat` files, `admin/pull-backups.ps1`,
`package/publish.py` and `vps/ship-server.sh` all read it. In the examples below, `ssh $AW_VPS`
stands for `ssh -i <AW_VPS_KEY> <AW_VPS>`.

## Sign-ups (how new players join)

Sign-ups are **open**: a new player downloads the launcher from the site (public), picks an
account name and password in the launcher (or with **Create Account** on the game's login
screen), and plays. The game files themselves only download with an account.

The gate's settings live in `/etc/aw/gate.env` on the VPS (then `systemctl restart aw-gate`):

| setting                    | default | what                                                         |
|----------------------------|---------|--------------------------------------------------------------|
| `AW_INVITE_REQUIRED`       | `0`     | `1`: sign-up needs an invite code again (closed playtest)    |
| `AW_SIGNUPS_PER_IP`        | `3`     | accounts one address may make per window                     |
| `AW_SIGNUP_WINDOW_HOURS`   | `24`    | that window                                                  |
| `AW_SIGNUPS_PER_DAY`       | `200`   | accounts everyone together may make in 24 hours (`0`: no cap) |
| `AW_SIGNUP_TRIES_PER_HOUR` | `10`    | sign-up tries (good or bad) per address per hour             |

The address limits are kept in the gate's memory, so a restart of the gate forgets them.
Other guards: names are 3-16 letters or digits (staff-like names such as `Admin`, `GM`,
`Moderator` are refused), passwords 6-16 characters without spaces and not the account name, a
hidden form field that only bots fill in, and no sign-ups from an address on the IP ban list
(`aw-admin ban ip`). Every account made is logged: `journalctl -u aw-gate | grep made`.

`GET https://<AW_PUBLIC_HOST>/api/signup-info` says whether a code is needed; the launcher and the
login screen ask it, so flipping `AW_INVITE_REQUIRED` needs no new client.

### Invite codes

Codes still work when sign-ups are open (a code given is used up and recorded, so you can see
who came through whom), and are required with `AW_INVITE_REQUIRED=1`.

| `.bat`              | `aw-admin`                              | what                                      |
|---------------------|-----------------------------------------|-------------------------------------------|
| `new-invite.bat`    | `aw-admin invite new [count] [note]`    | new codes (1-100), printed one per line   |
| `list-invites.bat`  | `aw-admin invite list [--unused]`       | every code: unused, used (by whom, when), revoked |
| `revoke-invite.bat` | `aw-admin invite revoke CODE`           | cancel a code nobody has used yet         |

```
ssh $AW_VPS aw-admin invite new 3 for Bob and Ann
ssh $AW_VPS aw-admin invite list --unused
```

Codes look like `K7QM-X3PA-W9TR`: 12 characters with no 0/O or 1/I/L to misread. Players can
type them in any case, with or without the dashes. Revoking a used code does nothing to the
account made with it.

### Later: codes from Discord

`aw-admin invite new` is the one way codes are made, so a Discord bot only has to run it, e.g.
`ssh ... aw-admin invite new --by discord:ModName 1 for NewFriend`, and post the printed line
back to the mod. `--by` goes into the code's `created_by`, so `invite list` shows which mod made
which code. Give the bot its own ssh key, not the root key, with a forced command in
`authorized_keys` that only lets `$SSH_ORIGINAL_COMMAND` through when it is `aw-admin invite new
...`.

## Legal-access attestations

Every player confirms, before they sign up, this statement (word for word, version 1):

> I own and have legal access to World of Warcraft and Call of Duty: Modern Warfare 2 (2009).

- **Sign-up** needs it: the game's Create Account dialog has a box to tick, and the launcher
  shows the statement and asks the player to type `Y`. The gate refuses a sign-up without it
  (`400`) and records it with the account in one transaction.
- **Accounts made before** are asked once, by the launcher at its next start, before it
  downloads anything (`GET /api/attest`, then `POST /api/attest` with the account's login).
- Each is a row of `realmd.aw_attestation` (`vps/sql/aw_attestation.sql`, made by
  `ship-server.sh` and `setup-server.sh`): account id, time (UTC), the address it came from,
  the statement's version, and whether it came from a sign-up or the launcher.
- A new wording is a new version (`ATTESTATION_VERSION` in `vps/gate.py`, and the
  `AW_ATTESTATION_VERSION` default in `aw-admin`): everyone is asked again.

| `.bat`               | `aw-admin`                                         | what                                   |
|----------------------|----------------------------------------------------|----------------------------------------|
| `attestations.bat`   | `aw-admin attestations [--account NAME]`           | every attestation, newest first        |
|                      | `aw-admin attestations --missing`                  | the accounts with none for the current statement |

`aw-admin create` (below) makes an account without one: its player is asked by the launcher.

## Making an account directly

**`admin/create-account.bat`** still works: name (3-16 letters or digits), password (6-16
letters, digits or `!@#$^*()_+=.,:;?~-`) and an optional GM level. By hand, the password goes on
stdin so it never shows on a command line:

```
echo Secret123| ssh $AW_VPS aw-admin create FriendName
echo Secret123| ssh $AW_VPS aw-admin create FriendName 3
```

Then the friend runs the launcher, presses Enter at the new-account prompt, and logs in.
Passwords aren't case-sensitive.

## Other account tools

| `.bat`                    | `aw-admin`                         | what                                        |
|---------------------------|------------------------------------|---------------------------------------------|
| `reset-password.bat`      | `echo PASS \| aw-admin password NAME` | a new password (the old one can't be read) |
| `make-gm.bat`             | `aw-admin gm NAME [level]`         | GM level 1-6 (default 6); they relog to get it |
| `remove-gm.bat`           | `aw-admin gm NAME 0`               | back to a player                            |
| `list-accounts.bat`       | `aw-admin accounts`                | every account, GM level, characters, last login |
| `list-characters.bat`     | `aw-admin chars [NAME]`            | characters, race, class, level, gold, ...   |

GMs are level 3: the `.fusion` admin commands, the full companion index with add/edit, free
homestead claims, `.go`, `.revive` and the rest of the stock GM commands. 4 and up add
`.modify money` and more; 6 is full admin.

## Bug reports and suggestions

Players send them from the game: **Esc > Report Bug / Suggest** opens a small dialog (Bug,
Suggestion or Other; a title; a description of up to 1000 characters). The server adds who and
where on its own: account, character, level, race, class, map, zone and area, position, what they
had targeted, their client's version (the launcher's published version) and the server build.
They land in the `characters` database's `aw_feedback` table, which mangosd makes itself the first
time it starts with this build.

| `.bat`                | `aw-admin`                                              | what                                    |
|-----------------------|---------------------------------------------------------|-----------------------------------------|
| `feedback-list.bat`   | `aw-admin feedback list [--status S] [--type T] [--limit N]` | a table, newest first (default: all, 50) |
| `feedback-show.bat`   | `aw-admin feedback show ID`                             | one report in full, as Markdown         |
| `feedback-set.bat`    | `aw-admin feedback set ID STATUS [note]`                | mark it, with an optional note          |
| `feedback-export.bat` | `aw-admin feedback export [--status S] [--json]`        | all of them at once, Markdown or JSON (default: open ones, at most 200) |

- Statuses: `new` (every report starts here), `reviewing`, `accepted`, `declined`, `done`.
  `--status open` is new + reviewing + accepted, `--status all` is every one, and a comma list
  works too (`--status new,reviewing`). Types: `bug`, `suggestion`, `other`.
- A note with quotes or other shell characters: send it on stdin with `-` as the note, as
  `feedback-set.bat` does: `echo It's fixed in 10-19| ssh ... aw-admin feedback set 12 done -`.
- `feedback-export.bat` saves to `%USERPROFILE%\womw-feedback.md` (or `.json`) and opens it in
  Notepad.

```
ssh $AW_VPS aw-admin feedback list --status new
ssh $AW_VPS aw-admin feedback show 12
ssh $AW_VPS aw-admin feedback set 12 accepted planned for the next release
ssh $AW_VPS aw-admin feedback export > feedback.md
```

**Handing them to Claude**: `aw-admin feedback export` prints every open report as one Markdown
document (a header, then one section per report: the player's words quoted, the context as a
list, a `.go xyz` line to get there), and `--json` prints one JSON array for a script. Paste or
attach it, or ask Claude to run the ssh command itself. After working through them, mark each one
with `feedback set` so the next export only has what is left.

**Spam limits** (the server's, per account, all its characters together; they survive a
restart): one report every 10 minutes and five in any 24 hours. Change them in `mangosd.conf`
with `Feedback.CooldownSeconds` and `Feedback.DailyLimit` (0 turns either off), or turn reports
off with `Feedback.Enable = 0`; restart mangosd after a change. Without those lines the defaults
hold. The Esc menu's button greys out with the countdown while a player has to wait.

In the game, GM level 3 and up (and the server console):

| command                                   | what                                                 |
|-------------------------------------------|------------------------------------------------------|
| `.feedback list [status] [type] [count]`  | the newest reports (default: new ones, 20)           |
| `.feedback show <id>`                     | one in full                                          |
| `.feedback set <id> <status> [note]`      | mark it                                              |

## Bans

| `.bat`          | `aw-admin`                                    | what                                     |
|-----------------|-----------------------------------------------|------------------------------------------|
| `ban.bat`       | `aw-admin ban NAME DURATION REASON`           | keep an account out                      |
| `ban.bat` (ip)  | `aw-admin ban ip ADDRESS DURATION REASON`     | keep an address out (logins and sign-ups) |
| `unban.bat`     | `aw-admin unban NAME` / `aw-admin unban ip ADDRESS` | lift it                            |
| `list-bans.bat` | `aw-admin bans`                               | every ban in force                       |

Durations are `30m`, `12h`, `7d`, `2w` or `perm`. Reasons are letters, digits and spaces.

```
ssh $AW_VPS aw-admin ban Griefer 7d spamming trade chat
ssh $AW_VPS aw-admin ban ip 203.0.113.9 perm account farm
```

These write the login server's own ban tables (`realmd.account_banned`, `realmd.ip_banned`), the
ones the in-game commands write, and the login server checks them at every login. Someone who is
online stays on until they log out: kick them with `.kick CHARACTER` in the game. The world
server also keeps its own copy of the IP list from its start: after `aw-admin unban ip`, type
`.reload ip_banned` in the game (GM level 6), or the address stays refused at the world server
until its next restart.

In the game, GMs have the stock vmangos commands too (they write the same tables):
`.ban account NAME 7d "reason"` and `.ban character NAME 7d "reason"` (GM level 3; they kick at
once), `.ban ip ADDRESS 7d "reason"` (level 6), `.unban account NAME` / `.unban ip ADDRESS`
(level 6), `.baninfo account NAME`, `.banlist account` and `.kick CHARACTER` (level 2). In-game
durations are `d`, `h`, `m`, `s` pieces like `7d` or `1d12h` (no weeks); `0` is for good, and so
is a mistyped duration, so check `.baninfo` after. A reason of more than one word needs quotes.
`aw-admin accounts` shows each account's last address (`last_ip`) for an IP ban.

## Wanderers

Off unless `Wanderers.Enable = 1` in mangosd.conf (design/WANDERERS.md; `28_wanderers.sql` must be
applied). GM level 3 and up, typed in the game's chat:

| command                     | what                                                              |
|-----------------------------|-------------------------------------------------------------------|
| `.wanderer list`            | the roster: id, name, level, race, kind, what each is doing, how far |
| `.wanderer info [id]`       | one of them (or the one selected): gun, purse, kills, trip, bounty, how well it knows you; its trade, nature, needs, haunts, what it is doing, today's schedule, pack and prices, friends and rivals, news, backstory |
| `.wanderer goto <id>`       | go to it (where it walks on paper, or its creature)              |
| `.wanderer spawn [id]`      | bring one (the nearest of your continent, or that id) in front of you |
| `.wanderer despawn <id\|all>` | send them out of the world; they stay out five minutes           |

The startup log says how many places and roads each region has, how many fishing waters, mines,
herb meadows, hunting grounds, markets and landmarks it found, who moved away (and who took their
place), and names any place in `aw_wanderer_node` whose anchor NPC is missing or stands in another
zone. Phase 2 knobs (mangosd.conf): `Wanderers.Lives`, `Trade`, `Hire`, `LifeDays`,
`LinesPerMinute`, `LineGapSeconds`, `WorkSpotsPerZone`. Their lives are kept in
`aw_wanderer.life` (the column is added at startup).

## Testing professions

GM level 2 and up, typed in the game's chat. It works on the selected player, or on yourself with
nobody selected:

| command                              | what                                                         |
|--------------------------------------|--------------------------------------------------------------|
| `.profession list`                   | every profession and its skill (or "not learned")            |
| `.profession learn <name> [skill]`   | learn it (the trainer ranks it needs) at that skill, 300 if none given |
| `.profession learn all [skill]`      | every profession at once, Construction and Minstrelsy too   |
| `.profession set <name> <skill>`     | set the skill (1-300); learns the ranks it needs             |
| `.profession recipes <name>`         | every recipe of it, and the skill to its max (learns it first if needed) |
| `.profession recipes all`            | every recipe of every profession                             |

Names: Alchemy, Blacksmithing, Enchanting, Engineering, Herbalism, Leatherworking, Mining,
Skinning, Tailoring, Cooking, First Aid, Fishing, Construction, Minstrelsy, Inscription,
Jewelcrafting; any case, spaces optional, or the first letters (`alch`, `tail`) and `bs`, `lw`,
`fa`, `ench`, `eng`, `herb`, `insc`, `scribe`, `jc`, `jewel`.
Construction has no recipes to learn (its pieces come with its tiers), so `recipes construction`
makes it Artisan 300. Minstrelsy's `recipes` learns every instrument recipe; Inscription's and
Jewelcrafting's every recipe of the trade (their glyphs and gems go into the kit carried from the
Professions hub's Glyphs & Gems window; a GM needs no item and no kit station). Learning every
profession ignores the two primary professions limit; that is only for testing.

## Testing killstreaks

GM level 3 and up, typed in the game's chat (it works on any server running this build):

| command               | what                                                                 |
|-----------------------|----------------------------------------------------------------------|
| `.streak list`        | the killstreak ladder: id, name, MW2 key and kills                   |
| `.streak <name or id>`| that killstreak, ready now without the kills; press 4 to call it in  |
| `.streak clear`       | send away your gunships, bombers, turrets and rocket                 |

Names: the id (`5`), MW2's key (`predator_missile`), the name in the world (`Goblin Rocket`) or a
short word: `uav`, `turret`/`sentry`, `rocket`/`predator`, `hornets`/`harrier`,
`gunship`/`helicopter`, `bomber`/`stealth`, `gyrocopter`/`chopper`, `nuke`. `.fusion world sentry`
still sets a turret down in front of you directly.

What each does (the craft are creatures the server flies, `23_lore_streaks.sql` must be applied):

- **Goblin Rocket** (predator, 5 kills): the view jumps to a goblin rocket car 90 yards over your
  mark; the mouse steers (down only), a click lights the second stage, Esc bails out (no blast),
  22 seconds of fuel. It blows up on what it hits (12 yards). Enemies get red brackets.
- **Dwarven Sentry Turret** (sentry, 5): a marker shows where it would stand; left click sets it
  down on flat ground within 6 yards, right click or Esc puts it back. It turns on hostiles within
  40 yards in a 120 degree arc for 60 seconds; two at most.
- **Gnomish Sky Hornets** (harrier, 7): a flying machine drops three bombs on your mark, then
  circles it firing rockets for 30 seconds.
- **Sky Rider Gunship** (attack helicopter, 7): a dwarf on a gryphon (Alliance) or an orc on a
  wind rider (Horde) circles you, 30 yards out and 40 up, for 60 seconds, shooting exposed enemies.
- **Siege Bomber** (stealth bomber, 9): a big flying machine (Alliance) or wind rider (Horde)
  flies over your mark along your aim, nine bombs eight yards apart.
- **Gyrocopter Gunship** (chopper gunner, 11): a gnomish gyrocopter circles you for 60 seconds
  with an explosive cannon.

A call-in the world refuses (no open sky, no ground for the turret) hands the reward back.

## How it works

- **Sign-up**: the launcher (or the login screen's Create Account) sends the name, password and
  any code to the gate (`POST /api/signup`; older launchers use `/api/register`, the same). A
  code is claimed with a single conditional `UPDATE` on `realmd.aw_invite` (only an unused,
  unrevoked code matches), so two people racing with one code can't both get an account. It then
  adds the account with an SRP6 verifier, as `aw-account` makes it. If the name was taken in the
  meantime, the code is given back. The game's login screen shows Create Account only when the
  launcher started it (`AW_SIGNUP_URL`, on the realm's own host); the local package has none.
- **Downloads**: Caddy serves `/files/*` (the manifest and the game files) only after asking the
  gate (`GET /api/auth`, Caddy's `forward_auth`), which checks the HTTP Basic name and password
  against the account's verifier, the same check the login server does. Only
  `/files/WorldOfModernWarfare.exe` (the launcher) is public.
  - A good login is remembered for 10 minutes, so an install's thousands of file requests don't
    each hit the database. After `aw-admin password`, the old password keeps downloading for up
    to 10 minutes.
  - 10 wrong passwords from one address in 15 minutes lock that address out for a while (429).
- **The gate's database user** (`aw`) can only: read an account's id, name, salt and verifier;
  add an account (name, verifier, salt, join date) and its realmcharacters row; read the codes
  and mark one used; read the IP ban list; read the uptime. It can't change or delete accounts
  or make codes.
- `/api/status` and `/api/signup-info` are public (the site and the launcher read them);
  `/api/attest` needs the account, as the downloads do.

The gate's user may also read and add rows of `realmd.aw_attestation` (nothing else there).

The launcher keeps the account name and the password, encrypted for the player's Windows user
(DPAPI), so later runs don't ask. A wrong or changed password makes it ask again.

## Crash reports (players' games)

When a player's game crashes, the launcher (which now stays open while the game runs, showing its
log) offers to send a crash report. It asks once ("Send crash reports when the game crashes?
[Y/n]", Enter is yes) and remembers the answer in `crash-reports.txt` in the install folder
(`%LOCALAPPDATA%\WorldOfModernWarfare`); deleting that file makes it ask again. A crash left while
the launcher window was closed is offered at the next start.

What a report holds, and nothing else:
- the game's crash text (`<install>\crashes\crash-<time>.txt`): what crashed (a Rust panic's
  message and source line, or a Windows exception code and address), the thread, raw frames
  (`benilla-mw2.exe+0x1a2b3c`) and the backtrace, the game version, build, Windows version,
  graphics card and driver, and the game's last 200 log lines. The player's Windows user folder
  is replaced by `%USERPROFILE%` / `%LOCALAPPDATA%` before it goes;
- after a hard crash (access violation, stack overflow, ...): a minidump (`crash-<time>.dmp`:
  the module list and every thread's registers and stack, no heap; about a MB), gzipped, at most
  8 MB (a bigger one stays on the player's PC). Stacks are raw memory: they can hold fragments of
  what the game was handling at that moment (a name, a chat line). The launcher says so when it
  asks;
- the game version, the client exe's sha256, the launcher version, the time, the exit code, and
  the account name (from the login the upload is made with, as for downloads).

An exit with an error code and no crash text (an abort) sends a short report of the exit code.
Closing the game's console window or a normal quit sends nothing. Sent or not, reports move to
`<install>\crashes\old` (the newest 10 are kept).

The gate (`POST /api/crash`) takes them only with a game account's name and password, at most 5
per account and 10 per address an hour, 12 MB a request (Caddy refuses more first), and keeps up
to 2000 reports / 4 GB in `/var/lib/aw/crashes/<id>/` (`meta.json`, `report.txt`,
`dump.dmp.gz`). Nothing about the player's address is kept. Delete a folder there to delete a
report.

| `.bat`               | `aw-admin`                                    | what                                     |
|----------------------|-----------------------------------------------|------------------------------------------|
| `crashes-list.bat [N]` | `aw-admin crashes list [--limit N] [--version V] [--account A] [--days D]` | newest first: id, account, version, kind, dump size, what and where |
| `crashes-show.bat ID` | `aw-admin crashes show ID`                   | who, which version and exe hash, then the game's whole crash text |
| `crashes-export.bat` | `aw-admin crashes export [--limit N] [--version V] [--days D]` | Markdown of recent crashes grouped by what crashed (count, players, versions, the newest one's top frames and last log lines), saved as `admin\crashes.md`: give it to an AI to triage in one read |
|                      | `aw-admin crashes fetch ID`                   | where the files are, and the `scp` to copy them to the PC |

Grouping: two panics are the same crash when their message (numbers aside) and source line
match; native crashes when exception code and address match.

### Symbolising a client crash

The exe players get has no symbols, so its frames are offsets: `benilla-mw2.exe+0x1a2b3c`. The
build's PDB turns them into functions (and file:line when built with line tables).
- `package\build.ps1` copies `client\target\release\benilla_mw2.pdb` and the exe to
  `build\symbols\staged` (never into `dist`, which is what players download), and
  `package\publish.py` files them, once per client exe, as
  `build\symbols\<version>_<first 12 of the exe's sha256>`. Keep `build\symbols` (back it up with
  the rest); a PDB can't be made again for an old build.
- Crash text: `aw-admin crashes show ID > report.txt` (or `crashes fetch`, then `scp`), then

  ```
  python client\scripts\symbolize_crash.py report.txt --sha <client exe sha256 from crashes show>
  ```

  which runs each `benilla-mw2.exe+0x...` through `server\contrib\crash\symbolize.py` (Windows'
  dbghelp) and prints function and line. `cargo install rustfilt` once makes the Rust names
  readable. A panic's `at:` line already names the source line, and its message is plain text.
- Minidump: `scp` it (`aw-admin crashes fetch ID` prints the command), `gzip -d dump.dmp.gz`, and
  open it in WinDbg (`winget install Microsoft.WinDbg`): `.sympath+ <checkout>\build\symbols\<folder>`,
  `.exepath+` the same folder, `.reload`, then `!analyze -v` and `~*k` (every thread's stack). Or
  Visual Studio: File > Open > the .dmp, "Set symbol paths" to that folder, "Debug with Native
  Only".
- Better line numbers: the `ship` profile (`client\Cargo.toml`) builds with
  `debug = "line-tables-only"` (the PDB grows, the exe doesn't). `build.ps1` builds `--release`,
  whose PDB has function names only; switch it to `--profile ship` (and copy from
  `target\ship\`) when a full rebuild is acceptable.

Testing it: start the game from a console with `AW_TEST_CRASH=panic` (a Rust panic 5 s in) or
`AW_TEST_CRASH=access` (an access violation, with a minidump), and `AW_CRASH_DIR` pointing at a
folder; the report and dump land there.

## Shipping a new version

```
wsl -d Ubuntu-24.04 -u root -- bash vps/ship-server.sh     # server + world SQL + gate + Caddy config
python package/publish.py --note "What changed"            # the launcher and game files
```

Deploy locally first (`dist` is what `publish.py` uploads). `ship-server.sh` makes the
crash report folder (`/var/lib/aw/crashes`, owned by `aw`) and the
`aw_invite` table if missing, validates the new Caddyfile before it swaps it in, and keeps the
previous one as `/etc/caddy/Caddyfile.prev`.
Status: `https://<AW_PUBLIC_HOST>/api/status` (any name in `AW_EXTRA_SITES` answers too)

## Database backups
`aw-backup.timer` dumps `realmd`, `characters` (kept 30 days) and `mangos` (kept 7 days) to
`/var/backups/aw` every night at 04:30 (server time). Install or update it with:
copy `vps/aw-backup.sh` to `/usr/local/bin/aw-backup`, `vps/aw-backup.service` and `.timer` to
`/etc/systemd/system/`, then `systemctl daemon-reload && systemctl enable --now aw-backup.timer`.
Run one now: `systemctl start aw-backup`. Restore one database (server stopped):
`zcat /var/backups/aw/characters-<stamp>.sql.gz | mariadb characters`.

## Off-server backup copy
A Windows scheduled task (e.g. daily and at logon) can run `admin\pull-backups.ps1`, which copies
new files from `/var/backups/aw` to `AW_BACKUP_DEST` from `vps/deploy.env.local` (kept 30 days;
log in `pull.log` there). `admin\backup-copy-now.bat` runs it by hand.

## SSH
Turn password logins off (e.g. `PasswordAuthentication no` in a file under
`/etc/ssh/sshd_config.d/`) so only your key (`AW_VPS_KEY`) gets in. Keep a copy of that key
somewhere safe. If it is lost, your hosting provider's web console is the way back in.
