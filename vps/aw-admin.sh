#!/usr/bin/env bash
# Azeroth Warfare admin, on the VPS (installed as /usr/local/bin/aw-admin by ship-server.sh).
#   aw-admin invite new [--by WHO] [count] [note...]
#                                 make single-use invite codes (count 1-100, default 1), one per
#                                 line on stdout; a player signs up with one in the launcher
#   aw-admin invite list [--unused]   every code: unused / used (by which account) / revoked
#   aw-admin invite revoke <code>     cancel an unused code
#   aw-admin create <name> [gm]   make an account directly; the password is read from stdin:
#                                 echo PASS | aw-admin create NAME [gm level 0-6, default 0]
#   aw-admin gm <name> [level]    make an account a GM: 3 game master, 4+ money and more, 6 full admin
#                                 (default 6; 0 takes it away)
#   aw-admin password <name>      set a new password (read from stdin: echo PASS | aw-admin password NAME)
#   aw-admin accounts             every account: GM level, characters, online, joined, last login
#   aw-admin chars [account]      characters (all, or one account's): race, class, level, gold, ...
#   aw-admin crashes list [--limit N] [--version V] [--account A] [--days D]
#                                 players' crash reports (newest first): id, account, version, what
#   aw-admin crashes show <id>    one report: who, which version, and the game's crash text
#   aw-admin crashes export [--limit N] [--version V] [--days D]
#                                 Markdown of recent crashes grouped by what crashed, for triage
#   aw-admin crashes fetch <id>   where the report and minidump are, and the scp to fetch them
#   aw-admin feedback list [--status S] [--type T] [--limit N]
#                                 players' bug reports and suggestions, newest first (S: new,
#                                 reviewing, accepted, declined, done, open = the first three, all,
#                                 or a comma list; default all. T: bug, suggestion, other. N: 50)
#   aw-admin feedback show <id>   one report in full, as Markdown
#   aw-admin feedback set <id> <status> [note...]
#                                 set its status and, if given, a note (a note of - is read from stdin)
#   aw-admin feedback export [--status S] [--type T] [--limit N] [--json]
#                                 the reports in one go as Markdown (or a JSON array), for a person
#                                 or an AI to read; default --status open --limit 200
#   aw-admin ban <account> <duration> <reason...>   keep an account out: duration 30m, 12h, 7d,
#                                 2w or perm; written where the login server checks it
#   aw-admin ban ip <address> <duration> <reason...>   the same for an address (no logins and no
#                                 new accounts from it)
#   aw-admin unban <account>  /  aw-admin unban ip <address>
#   aw-admin bans                 the bans in force, accounts and addresses
#   aw-admin attestations [--account A] [--missing]
#                                 players' legal-access attestations (account, when, address,
#                                 statement version, where), newest first; --missing lists the
#                                 accounts with none for the current statement
# The game files download only with an account (the launcher exe itself is public).
# From the PC: the .bat files in admin/ (they read vps/deploy.env.local), or
# ssh <your server> aw-admin accounts
set -euo pipefail

# Invite codes: 12 characters that can't be misread (no 0/O, 1/I/L), shown as XXXX-XXXX-XXXX.
# The gate (vps/gate.py, CODE_ALPHABET) takes the same alphabet, in any case, with or without dashes.
ALPHABET=ABCDEFGHJKMNPQRSTUVWXYZ23456789

new_code() {  # 12 random characters of the alphabet (tr keeps only those bytes: no bias)
    local c=""
    while [ ${#c} -lt 12 ]; do
        c=$(head -c 600 /dev/urandom | LC_ALL=C tr -dc "$ALPHABET" | cut -c1-12)
    done
    printf '%s' "$c"
}

show_code() { printf '%s-%s-%s\n' "${1:0:4}" "${1:4:4}" "${1:8:4}"; }

code_arg() {  # an invite code as typed, as stored (or exit)
    local c
    c=$(printf '%s' "$1" | tr -d ' -' | tr 'a-z' 'A-Z')
    [[ "$c" =~ ^[$ALPHABET]{12}$ ]] || { echo "not an invite code: $1" >&2; exit 1; }
    printf '%s' "$c"
}

# Text that goes into the table as it is: no quotes, nothing a shell or SQL would read.
text_ok() { [[ "$1" =~ ^[A-Za-z0-9\ _.,:@+-]{0,100}$ ]] || { echo "$2: letters, digits, spaces and _.,:@+- only (at most 100)" >&2; exit 1; }; }

invite() {
    case "${1:-}" in
        new)
            # The one way codes are made: this command (a Discord bot would call it too, e.g.
            # aw-admin invite new --by discord:ModName 1 for NewFriend).
            shift
            by=admin
            if [ "${1:-}" = --by ]; then by="${2:?--by who}"; shift 2; fi
            count="${1:-1}"
            [[ "$count" =~ ^[0-9]+$ ]] && [ "$count" -ge 1 ] && [ "$count" -le 100 ] \
                || { echo "count is 1-100" >&2; exit 1; }
            shift || true
            note="$*"
            text_ok "$by" "--by"
            text_ok "$note" "the note"
            for _ in $(seq "$count"); do
                c=$(new_code)
                mariadb realmd -e "INSERT INTO aw_invite (code, created_by, note) VALUES ('$c', '$by', '$note')"
                show_code "$c"
            done
            ;;
        list)
            where=""
            [ "${2:-}" = --unused ] && where="WHERE used_by_account IS NULL AND revoked = 0"
            mariadb -t realmd -e "
                SELECT CONCAT_WS('-', SUBSTR(code, 1, 4), SUBSTR(code, 5, 4), SUBSTR(code, 9, 4)) AS code,
                       CASE WHEN used_by_account IS NOT NULL THEN 'used'
                            WHEN revoked THEN 'revoked' ELSE 'unused' END AS state,
                       COALESCE(used_by_account, '') AS used_by, COALESCE(used_at, '') AS used_at,
                       created_at, created_by, note
                FROM aw_invite $where ORDER BY created_at DESC, code"
            ;;
        revoke)
            [ -n "${2:-}" ] || { echo "which invite code?" >&2; exit 1; }
            c=$(code_arg "${*:2}")  # typed with spaces, it arrives as several words
            n=$(mariadb -N realmd -e "UPDATE aw_invite SET revoked = 1 WHERE code = '$c' AND used_by_account IS NULL AND revoked = 0; SELECT ROW_COUNT()")
            if [ "$n" = 1 ]; then echo "$(show_code "$c") revoked"; exit 0; fi
            used=$(mariadb -N realmd -e "SELECT COALESCE(used_by_account, IF(revoked, '-', '')) FROM aw_invite WHERE code = '$c'")
            case "$used" in
                "") echo "no invite code $(show_code "$c")" >&2 ;;
                -) echo "$(show_code "$c") was already revoked" >&2 ;;
                *) echo "$(show_code "$c") was already used by $used (that account stays)" >&2 ;;
            esac
            exit 1
            ;;
        *)
            sed -n '3,7p' "$0"; exit 1
            ;;
    esac
}

# -- Bug reports and suggestions (characters.aw_feedback, made by mangosd at startup) --
# Every filter is checked against a fixed list or read as digits before it reaches SQL; a note is
# passed as hex, so nothing typed is ever read as SQL.
FB_STATUSES="new reviewing accepted declined done"
FB_TYPES="bug suggestion other"

fb_usage() { sed -n '/^#   aw-admin feedback list/,/^#.*--status open --limit 200/p' "$0" >&2; exit 1; }

fb_table() {  # the table is there (mangosd makes it the first time it starts with this build)
    [ "$(mariadb -N characters -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'characters' AND table_name = 'aw_feedback'")" = 1 ] && return 0
    echo "no reports yet: the aw_feedback table appears when mangosd first starts with the feedback build" >&2
    exit 0
}

fb_in_list() {  # fb_in_list WORD "LIST": WORD is one of the words of LIST
    local w
    for w in $2; do [ "$w" = "$1" ] && return 0; done
    return 1
}

fb_id() { [[ "$1" =~ ^[0-9]{1,9}$ ]] || { echo "a report id is a number: $1" >&2; exit 1; }; printf '%s' "$1"; }

# Parse --status/--type/--limit/--json (after the default status and limit) into FB_WHERE,
# FB_LIMIT and FB_JSON.
fb_filters() {
    local status="$1" type="" s list parts
    FB_LIMIT="$2"; FB_JSON=0; shift 2
    while [ $# -gt 0 ]; do
        case "$1" in
            --status) status="${2:-}"; [ -n "$status" ] || fb_usage; shift 2 ;;
            --type) type="${2:-}"; fb_in_list "$type" "$FB_TYPES" || { echo "--type is one of: $FB_TYPES" >&2; exit 1; }; shift 2 ;;
            --limit) FB_LIMIT="${2:-}"; [[ "$FB_LIMIT" =~ ^[0-9]{1,5}$ ]] && [ "$FB_LIMIT" -ge 1 ] || { echo "--limit is 1-99999" >&2; exit 1; }; shift 2 ;;
            --json) FB_JSON=1; shift ;;
            *) fb_usage ;;
        esac
    done
    FB_WHERE="WHERE 1"
    if [ "$status" != all ]; then
        [ "$status" = open ] && status="new,reviewing,accepted"
        list=""
        IFS=, read -r -a parts <<< "$status"
        for s in "${parts[@]}"; do
            fb_in_list "$s" "$FB_STATUSES" || { echo "--status is one of: $FB_STATUSES, open, all (or a comma list)" >&2; exit 1; }
            list="$list${list:+, }'$s'"
        done
        FB_WHERE="$FB_WHERE AND f.status IN ($list)"
    fi
    if [ -n "$type" ]; then FB_WHERE="$FB_WHERE AND f.type = '$type'"; fi
    return 0
}

FB_RACE="ELT(f.race, 'Human','Orc','Dwarf','Night Elf','Undead','Tauren','Gnome','Troll')"
FB_CLASS="ELT(f.class, 'Warrior','Paladin','Hunter','Rogue','Priest','','Shaman','Mage','Warlock','','Druid')"
FB_MAP="CASE f.map WHEN 0 THEN 'Eastern Kingdoms' WHEN 1 THEN 'Kalimdor' ELSE CONCAT('map ', f.map) END"

fb_markdown() {  # fb_markdown "WHERE ..." LIMIT: each report as a Markdown section
    mariadb -N -B -r characters -e "
        SELECT CONCAT(
            '## #', f.id, ' [', f.type, '] ', f.title, '\n',
            '- Status: ', f.status, IF(f.updated_at IS NULL, '', CONCAT(' (changed ', f.updated_at, ')')), '\n',
            '- Sent: ', f.created_at, ' (server time)\n',
            '- From: ', f.character_name, ' (account ', f.account_name, ', id ', f.account_id,
                IF(f.gm_level > 0, CONCAT(', GM level ', f.gm_level), ''), '), level ', f.level, ' ',
                COALESCE($FB_RACE, '?'), ' ', COALESCE($FB_CLASS, '?'), '\n',
            '- Where: ', $FB_MAP, IF(f.zone_name <> '', CONCAT(', ', f.zone_name), ''),
                IF(f.area_name <> '' AND f.area_name <> f.zone_name, CONCAT(' / ', f.area_name), ''),
                ' at (', ROUND(f.x, 1), ', ', ROUND(f.y, 1), ', ', ROUND(f.z, 1), ')',
                ', .go xyz ', ROUND(f.x, 1), ' ', ROUND(f.y, 1), ' ', ROUND(f.z, 1), ' ', f.map, '\n',
            IF(f.target <> '', CONCAT('- Target: ', f.target, '\n'), ''),
            '- Client: ', IF(f.client_version <> '', f.client_version, '?'),
                '; server build ', IF(f.server_build <> '', f.server_build, '?'), '\n',
            IF(f.admin_note <> '', CONCAT('- Admin note: ', f.admin_note, '\n'), ''),
            '\n',
            IF(f.body <> '', CONCAT('> ', REPLACE(f.body, '\n', '\n> ')), '> (no description)'),
            '\n')
        FROM aw_feedback f $1 ORDER BY f.id DESC LIMIT $2"
}

fb_json() {  # fb_json "WHERE ..." LIMIT: a JSON array, newest first
    mariadb -N -B -r characters -e "
        SELECT JSON_OBJECT(
            'id', f.id, 'created_at', DATE_FORMAT(f.created_at, '%Y-%m-%d %H:%i:%s'),
            'type', f.type, 'status', f.status, 'title', f.title, 'body', f.body,
            'account', JSON_OBJECT('id', f.account_id, 'name', f.account_name, 'gm_level', f.gm_level),
            'character', JSON_OBJECT('guid', f.character_guid, 'name', f.character_name, 'level', f.level,
                'race', $FB_RACE, 'class', $FB_CLASS),
            'location', JSON_OBJECT('map', f.map, 'map_name', $FB_MAP, 'zone', f.zone, 'zone_name', f.zone_name,
                'area', f.area, 'area_name', f.area_name,
                'x', ROUND(f.x, 2), 'y', ROUND(f.y, 2), 'z', ROUND(f.z, 2), 'orientation', ROUND(f.o, 2)),
            'target', f.target, 'client_version', f.client_version, 'server_build', f.server_build,
            'admin_note', f.admin_note,
            'updated_at', IF(f.updated_at IS NULL, NULL, DATE_FORMAT(f.updated_at, '%Y-%m-%d %H:%i:%s')))
        FROM aw_feedback f $1 ORDER BY f.id DESC LIMIT $2" \
        | awk 'BEGIN { print "[" } NR > 1 { print "," } { print } END { print "]" }'
}

feedback() {
    local id status note hex set_note out n
    case "${1:-}" in
        list)
            shift
            fb_filters all 50 "$@"
            fb_table
            mariadb -t characters -e "
                SELECT f.id, f.created_at AS sent, f.type, f.status, f.character_name AS who, f.level AS lvl,
                       LEFT(IF(f.area_name <> '' AND f.area_name <> f.zone_name, f.area_name, f.zone_name), 24) AS place,
                       LEFT(f.title, 60) AS title
                FROM aw_feedback f $FB_WHERE ORDER BY f.id DESC LIMIT $FB_LIMIT"
            echo "(aw-admin feedback show <id> for one in full; export for all of them at once)"
            ;;
        show)
            id=$(fb_id "${2:-}")
            fb_table
            out=$(fb_markdown "WHERE f.id = $id" 1)
            [ -n "$out" ] || { echo "no report #$id" >&2; exit 1; }
            printf '%s\n' "$out"
            ;;
        set)
            id=$(fb_id "${2:-}")
            status="${3:-}"
            fb_in_list "$status" "$FB_STATUSES" || { echo "the status is one of: $FB_STATUSES" >&2; exit 1; }
            note="${*:4}"
            if [ "$note" = - ]; then IFS= read -r note || true; fi
            # Control characters out; the column keeps at most 1000 characters.
            note=$(printf '%s' "$note" | tr -d '\000-\037\177')
            fb_table
            [ "$(mariadb -N characters -e "SELECT COUNT(*) FROM aw_feedback WHERE id = $id")" = 1 ] \
                || { echo "no report #$id" >&2; exit 1; }
            set_note=""
            if [ -n "$note" ]; then
                hex=$(printf '%s' "$note" | od -An -v -tx1 | tr -d ' \n')
                set_note=", admin_note = LEFT(CONVERT(UNHEX('$hex') USING utf8), 1000)"
            fi
            mariadb characters -e "UPDATE aw_feedback SET status = '$status', updated_at = NOW()$set_note WHERE id = $id"
            echo "report #$id is now $status${note:+ (note saved)}"
            ;;
        export)
            shift
            fb_filters open 200 "$@"
            fb_table
            if [ "$FB_JSON" = 1 ]; then
                fb_json "$FB_WHERE" "$FB_LIMIT"
            else
                n=$(mariadb -N characters -e "SELECT COUNT(*) FROM (SELECT f.id FROM aw_feedback f $FB_WHERE LIMIT $FB_LIMIT) t")
                echo "# World of Modern Warfare: player reports"
                echo
                echo "$n report(s), newest first, exported $(date '+%Y-%m-%d %H:%M %Z'). Each holds the player's own"
                echo "words (quoted) and what the server added: where they stood, their target, their client."
                echo "Statuses: new, reviewing, accepted, declined, done (aw-admin feedback set <id> <status> [note])."
                echo
                fb_markdown "$FB_WHERE" "$FB_LIMIT"
            fi
            ;;
        *)
            fb_usage
            ;;
    esac
}

name_ok() { [[ "$1" =~ ^[A-Za-z0-9]{3,16}$ ]] || { echo "account names are 3-16 letters or digits" >&2; exit 1; }; }
exists() { [ "$(mariadb -N realmd -e "SELECT COUNT(*) FROM account WHERE username = UPPER('$1')")" = 1 ]; }

read_password() {  # the password from stdin (one line, as a .bat's echo sends it), checked
    local pass ok='^[A-Za-z0-9!@#$%^*()_+=.,:;?~-]{6,16}$'
    IFS= read -r pass || true
    pass="${pass%$'\r'}"
    pass="${pass% }"
    [[ "$pass" =~ $ok ]] || { echo "passwords are 6-16 letters, digits or !@#\$%^*()_+=.,:;?~-" >&2; exit 1; }
    printf '%s' "$pass"
}

set_gm() {  # set_gm NAME LEVEL (0 takes it away)
    if [ "$2" = 0 ]; then
        mariadb realmd -e "DELETE a FROM account_access a JOIN account c ON c.id = a.id WHERE c.username = UPPER('$1')"
    else
        mariadb realmd -e "INSERT INTO account_access (id, gmlevel, RealmID) SELECT id, $2, -1 FROM account WHERE username = UPPER('$1') ON DUPLICATE KEY UPDATE gmlevel = $2"
    fi
}

# Crash reports from players' launchers, kept by the gate (vps/gate.py, Crashes): one folder per
# report under $CRASHES with meta.json, report.txt and maybe dump.dmp.gz (a gzipped minidump).
CRASHES="${AW_CRASH_DIR:-/var/lib/aw/crashes}"

crashes() {
    [ -n "${1:-}" ] || { sed -n '/^#   aw-admin crashes list/,/^#   aw-admin crashes fetch/p' "$0"; exit 1; }
    CRASHES="$CRASHES" VPS_ADDR="${AW_VPS_ADDR:-root@$(public_host)}" python3 - "$@" <<'PY'
import collections, json, os, re, sys, time

ROOT = os.environ["CRASHES"]
ID = re.compile(r"^[0-9]{8}-[0-9]{6}-[0-9a-f]{6}$")


def die(text):
    print(text, file=sys.stderr)
    sys.exit(1)


def when(t):
    return time.strftime("%Y-%m-%d %H:%M", time.gmtime(t)) if t else "-"


def size(n):
    if not n:
        return "-"
    return f"{n / 1e6:.1f} MB" if n >= 1e5 else (f"{n / 1e3:.0f} KB" if n >= 1000 else f"{n} B")


def exit_code(code):
    """A Windows exit code as it is usually written: 0xC0000005, or 101."""
    if not isinstance(code, int):
        return "none"
    code &= 0xFFFFFFFF
    return f"0x{code:08X}" if code > 0xFFFF else str(code)


def reports():
    """Every kept report's meta, newest first."""
    out = []
    try:
        names = os.listdir(ROOT)
    except OSError:
        return out
    for name in names:
        if not ID.match(name):
            continue
        try:
            with open(os.path.join(ROOT, name, "meta.json")) as f:
                out.append(json.load(f))
        except (OSError, ValueError):
            pass
    return sorted(out, key=lambda m: (m.get("received", 0), m["id"]), reverse=True)


def options(args, known):
    """--name value options (as known lists them) and the rest."""
    opts, rest = {}, []
    i = 0
    while i < len(args):
        if args[i] in known:
            if i + 1 >= len(args):
                die(f"{args[i]} needs a value")
            opts[args[i]] = args[i + 1]
            i += 2
        else:
            rest.append(args[i])
            i += 1
    return opts, rest


def number(text, what):
    if not text.isdigit() or int(text) < 1:
        die(f"{what}: a whole number, 1 or more")
    return int(text)


def pick(args, default_limit):
    opts, rest = options(args, ("--limit", "--version", "--account", "--days"))
    if rest:
        die(f"unexpected: {' '.join(rest)}")
    out = reports()
    if "--version" in opts:
        out = [m for m in out if m.get("game_version", "").startswith(opts["--version"])]
    if "--account" in opts:
        out = [m for m in out if m.get("account", "").upper() == opts["--account"].upper()]
    if "--days" in opts:
        since = time.time() - 86400 * number(opts["--days"], "--days")
        out = [m for m in out if m.get("received", 0) >= since]
    return out[: number(opts.get("--limit", str(default_limit)), "--limit")]


def one(args):
    cid = args[0].strip().rstrip("/") if len(args) == 1 else ""
    if not ID.match(cid):
        die("which report? an id as crashes list shows it, e.g. 20261018-120000-a1b2c3")
    folder = os.path.join(ROOT, cid)
    try:
        with open(os.path.join(folder, "meta.json")) as f:
            return folder, json.load(f)
    except (OSError, ValueError):
        die(f"no crash report {cid}")


def report_text(folder):
    try:
        with open(os.path.join(folder, "report.txt"), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def section(text, heading, lines):
    """The first `lines` lines after the line that starts with `heading`."""
    rows = text.splitlines()
    for i, row in enumerate(rows):
        if row.startswith(heading):
            out = []
            for r in rows[i + 1: i + 1 + lines]:
                if not r.strip():
                    break
                out.append(r)
            return out
    return []


def last_lines(text, heading, lines):
    rows = text.splitlines()
    for i, row in enumerate(rows):
        if row.startswith(heading):
            body = [r for r in rows[i + 1:] if r.strip()]
            return body[-lines:]
    return []


command, args = sys.argv[1], sys.argv[2:]
if command == "list":
    rows = pick(args, 30)
    if not rows:
        print("no crash reports")
        sys.exit(0)
    print(f"{'id':<22} {'received (UTC)':<16} {'account':<16} {'version':<17} {'kind':<6} {'dump':>7}  what")
    for m in rows:
        what = m.get("message", "")
        if m.get("location"):
            what += f" @ {m['location']}"
        print(f"{m['id']:<22} {when(m.get('received')):<16} {m.get('account', ''):<16} "
              f"{m.get('game_version', '')[:17]:<17} {m.get('kind', ''):<6} {size(m.get('dump_bytes', 0)):>7}  {what[:110]}")
elif command == "show":
    folder, m = one(args)
    print(f"crash report {m['id']}: {m.get('account')} on version {m.get('game_version') or '?'}, "
          f"received {when(m.get('received'))} UTC, crashed {when(m.get('crashed_at'))} UTC, "
          f"exit code {exit_code(m.get('exit_code'))}, launcher {m.get('launcher_version') or '?'}")
    print(f"client exe sha256: {m.get('client_sha256') or '?'}")
    print(f"minidump: {size(m.get('dump_bytes', 0)) if m.get('dump_bytes') else 'none'}"
          + (f" (aw-admin crashes fetch {m['id']})" if m.get("dump_bytes") else ""))
    print()
    sys.stdout.write(report_text(folder))
elif command == "fetch":
    folder, m = one(args)
    print(f"folder:   {folder}")
    for name in ("report.txt", "dump.dmp.gz", "meta.json"):
        path = os.path.join(folder, name)
        if os.path.exists(path):
            print(f"{name + ':':<10}{path} ({size(os.path.getsize(path))})")
    vps = os.environ["VPS_ADDR"]
    print("\nOn the PC (Git Bash or PowerShell):")
    print(f"  scp -r {vps}:{folder} .   (add -i <your key>, as in vps/deploy.env.local)")
    if m.get("dump_bytes"):
        print(f"  then unzip the dump: gzip -d {m['id']}/dump.dmp.gz (Git Bash), or 7-Zip")
    print(f"Symbols: build/symbols/ on the PC, the folder for version {m.get('game_version') or '?'}"
          f" (exe sha256 {(m.get('client_sha256') or '?')[:12]}); see vps/ADMIN.md.")
elif command == "export":
    rows = pick(args, 200)
    groups = collections.OrderedDict()
    for m in rows:
        groups.setdefault(m.get("signature") or m.get("message", "?"), []).append(m)
    order = sorted(groups.items(), key=lambda kv: (-len(kv[1]), -kv[1][0].get("received", 0)))
    print("# World of Modern Warfare crash reports\n")
    print(f"{len(rows)} report(s), {len(order)} distinct crash(es), newest {when(rows[0]['received']) if rows else '-'} UTC. "
          "Grouped by signature (numbers in the message don't split a group). Frames like "
          "`benilla-mw2.exe+0x1a2b3c` need that version's PDB (vps/ADMIN.md, Symbolising).\n")
    for n, (sig, ms) in enumerate(order, 1):
        newest = ms[0]
        accounts = collections.Counter(m.get("account", "?") for m in ms)
        versions = collections.Counter(m.get("game_version", "?") for m in ms)
        print(f"## {n}. {newest.get('message', '?')[:150]}\n")
        print(f"- **signature**: `{sig}`")
        print(f"- **count**: {len(ms)}; first {when(ms[-1].get('received'))}, last {when(newest.get('received'))} UTC")
        print(f"- **players**: {', '.join(f'{a} ({c})' for a, c in accounts.most_common())}")
        print(f"- **versions**: {', '.join(f'{v} ({c})' for v, c in versions.most_common())}")
        if newest.get("location"):
            print(f"- **where**: `{newest['location']}`")
        print(f"- **kind**: {newest.get('kind')}, exit code {exit_code(newest.get('exit_code'))}")
        print(f"- **reports**: {', '.join(m['id'] for m in ms[:10])}" + (" ..." if len(ms) > 10 else ""))
        text = report_text(os.path.join(ROOT, newest["id"]))
        header = []
        for row in text.splitlines()[1:20]:
            if not row.strip():
                break
            header.append(row)
        trace = section(text, "backtrace:", 25) or section(text, "frames", 20)
        log = last_lines(text, "last ", 12)
        print(f"\nNewest report ({newest['id']}):\n")
        print("```")
        print("\n".join(header))
        if trace:
            print("\n-- backtrace (top) --")
            print("\n".join(trace))
        if log:
            print("\n-- last log lines --")
            print("\n".join(log))
        print("```\n")
else:
    die("aw-admin crashes list | show <id> | export | fetch <id>")
PY
}

public_host() {  # the server's public name (AW_HOST in the gate's settings), else this machine's
    local h=""
    [ -r /etc/aw/gate.env ] && h=$(sed -n 's/^AW_HOST=//p' /etc/aw/gate.env | tr -d '"' | head -1)
    printf '%s' "${h:-$(hostname -f)}"
}

# Players' legal-access attestations (realmd.aw_attestation, vps/sql/aw_attestation.sql): the
# gate records one when an account signs up (the box ticked, or Y typed in the launcher) and once
# for an older account from the launcher. The statement's version is the gate's
# ATTESTATION_VERSION; a new version asks everyone again.
ATTEST_VERSION="${AW_ATTESTATION_VERSION:-1}"

attestations() {
    local account="" missing=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --account) account="${2:-}"; name_ok "$account"; shift 2 ;;
            --missing) missing=1; shift ;;
            *) echo "aw-admin attestations [--account NAME] [--missing]" >&2; exit 1 ;;
        esac
    done
    [ "$(mariadb -N realmd -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'realmd' AND table_name = 'aw_attestation'")" = 1 ]         || { echo "no aw_attestation table yet: ship-server.sh (or vps/sql/aw_attestation.sql) makes it" >&2; exit 1; }
    if [ "$missing" = 1 ]; then
        local where=""
        [ -n "$account" ] && where="AND a.username = UPPER('$account')"
        mariadb -t realmd -e "
            SELECT a.id AS id, a.username AS account, DATE(a.joindate) AS joined, a.last_login AS last_login
            FROM account a
            WHERE NOT EXISTS (SELECT 1 FROM aw_attestation t WHERE t.account_id = a.id AND t.version = $ATTEST_VERSION) $where
            ORDER BY a.id"
        return
    fi
    local where=""
    [ -n "$account" ] && where="WHERE a.username = UPPER('$account')"
    mariadb -t realmd -e "
        SELECT t.account_id AS id, a.username AS account, t.attested_at AS attested_at_utc,
               t.ip AS address, t.version AS version, t.source AS via
        FROM aw_attestation t JOIN account a ON a.id = t.account_id
        $where ORDER BY t.attested_at DESC, t.id DESC"
}

duration_secs() {  # 30m, 12h, 7d, 2w as seconds; perm (or 0) as 0, the server's "for good"
    case "$1" in
        perm|permanent|forever|0) echo 0; return ;;
    esac
    [[ "$1" =~ ^([0-9]{1,5})([mhdw])$ ]] || { echo "duration is like 30m, 12h, 7d, 2w or perm" >&2; exit 1; }
    local n=${BASH_REMATCH[1]} unit=${BASH_REMATCH[2]}
    case "$unit" in m) echo $((n * 60)) ;; h) echo $((n * 3600)) ;; d) echo $((n * 86400)) ;; w) echo $((n * 604800)) ;; esac
}

ip_ok() { [[ "$1" =~ ^[0-9A-Fa-f.:]{2,32}$ ]] || { echo "not an address: $1" >&2; exit 1; }; }

# Bans go into the login server's own tables (realmd.account_banned, realmd.ip_banned), as the
# in-game .ban account / .ban ip write them; the login server checks them at every login. A
# player online now stays on until they log out: kick them in game with .kick NAME (or ban them
# in game with .ban account, which kicks).
ban() {
    if [ "${1:-}" = ip ]; then
        local ip="${2:?which address}" secs
        ip_ok "$ip"
        secs=$(duration_secs "${3:?how long: 30m, 12h, 7d, 2w or perm}")
        local reason="${*:4}"
        [ -n "$reason" ] || reason="no reason given"
        text_ok "$reason" "the reason"
        reason="${reason:0:50}"
        mariadb realmd -e "REPLACE INTO ip_banned (ip, bandate, unbandate, bannedby, banreason) VALUES ('$ip', UNIX_TIMESTAMP(), UNIX_TIMESTAMP() + $secs, 'aw-admin', '$reason')"
        if [ "$secs" = 0 ]; then echo "$ip banned for good"; else echo "$ip banned for $3"; fi
        return
    fi
    local name="${1:?which account}" secs
    name_ok "$name"
    exists "$name" || { echo "no account $name" >&2; exit 1; }
    secs=$(duration_secs "${2:?how long: 30m, 12h, 7d, 2w or perm}")
    local reason="${*:3}"
    [ -n "$reason" ] || reason="no reason given"
    text_ok "$reason" "the reason"
    mariadb realmd -e "INSERT INTO account_banned (id, bandate, unbandate, bannedby, banreason, active, realm) SELECT id, UNIX_TIMESTAMP(), UNIX_TIMESTAMP() + $secs, 'aw-admin', '$reason', 1, 1 FROM account WHERE username = UPPER('$name')"
    if [ "$secs" = 0 ]; then echo "$name banned for good"; else echo "$name banned for $2"; fi
    online=$(mariadb -N realmd -e "SELECT online FROM account WHERE username = UPPER('$name')")
    [ "$online" = 0 ] || echo "$name is online now: kick them in game with .kick <character> (they can't log back in)"
}

unban() {
    if [ "${1:-}" = ip ]; then
        local ip="${2:?which address}"
        ip_ok "$ip"
        n=$(mariadb -N realmd -e "DELETE FROM ip_banned WHERE ip = '$ip'; SELECT ROW_COUNT()")
        if [ "$n" = 0 ]; then echo "$ip wasn't banned" >&2; exit 1; fi
        echo "$ip unbanned"
        return
    fi
    local name="${1:?which account}"
    name_ok "$name"
    exists "$name" || { echo "no account $name" >&2; exit 1; }
    n=$(mariadb -N realmd -e "UPDATE account_banned b JOIN account a ON a.id = b.id SET b.active = 0 WHERE a.username = UPPER('$name') AND b.active = 1; SELECT ROW_COUNT()")
    if [ "$n" = 0 ]; then echo "$name wasn't banned" >&2; exit 1; fi
    echo "$name unbanned"
}

case "${1:-}" in
    crashes)
        shift; crashes "$@"
        ;;
    invite)
        shift; invite "$@"
        ;;
    feedback)
        shift; feedback "$@"
        ;;
    ban)
        shift; ban "$@"
        ;;
    unban)
        shift; unban "$@"
        ;;
    attestations)
        shift; attestations "$@"
        ;;
    bans)
        mariadb -t realmd -e "
            SELECT a.username AS account, FROM_UNIXTIME(b.bandate) AS since,
                   IF(b.unbandate = b.bandate, 'never', FROM_UNIXTIME(b.unbandate)) AS until,
                   b.bannedby AS by_who, b.banreason AS reason
            FROM account_banned b JOIN account a ON a.id = b.id
            WHERE b.active = 1 AND (b.unbandate = b.bandate OR b.unbandate > UNIX_TIMESTAMP())
            ORDER BY b.bandate DESC"
        mariadb -t realmd -e "
            SELECT ip AS address, FROM_UNIXTIME(bandate) AS since,
                   IF(unbandate = bandate, 'never', FROM_UNIXTIME(unbandate)) AS until,
                   bannedby AS by_who, banreason AS reason
            FROM ip_banned WHERE unbandate = bandate OR unbandate > UNIX_TIMESTAMP()
            ORDER BY bandate DESC"
        ;;
    create)
        name="${2:?account name}"; level="${3:-0}"
        name_ok "$name"
        [[ "$level" =~ ^[0-6]$ ]] || { echo "GM level is 0-6" >&2; exit 1; }
        if exists "$name"; then echo "there is already an account $name" >&2; exit 1; fi
        pass=$(read_password)
        # --new: a plain INSERT, which fails rather than overwrite an account of that name.
        /opt/aw/bin/aw-account --new "$name" "$pass" | mariadb realmd
        [ "$level" = 0 ] || set_gm "$name" "$level"
        if [ "$level" = 0 ]; then echo "account $name made"; else echo "account $name made, GM level $level"; fi
        echo "(passwords are not case-sensitive)"
        ;;
    gm)
        name="${2:?account name}"; level="${3:-6}"
        name_ok "$name"
        [[ "$level" =~ ^[0-6]$ ]] || { echo "level is 0-6" >&2; exit 1; }
        exists "$name" || { echo "no account $name" >&2; exit 1; }
        set_gm "$name" "$level"
        echo "$name is now GM level $level (takes effect at their next login)"
        ;;
    password)
        name="${2:?account name}"
        name_ok "$name"
        exists "$name" || { echo "no account $name" >&2; exit 1; }
        pass=$(read_password)
        gm=$(mariadb -N realmd -e "SELECT gmlevel FROM account WHERE username = UPPER('$name')")
        # Without --new, aw-account updates the account's verifier (the password is never stored).
        # A download login cached by the gate keeps working up to 10 minutes on the old password.
        /opt/aw/bin/aw-account "$name" "$pass" "$gm" | mariadb realmd
        echo "$name has a new password (passwords are not case-sensitive)"
        ;;
    accounts)
        mariadb -t realmd -e "
            SELECT a.id AS id, a.username AS account,
                   COALESCE(MAX(x.gmlevel), 0) AS gm,
                   (SELECT COUNT(*) FROM characters.characters c WHERE c.account = a.id) AS chars,
                   IF(a.online, 'yes', '') AS online,
                   DATE(a.joindate) AS joined, a.last_login AS last_login, a.last_ip AS last_ip
            FROM account a LEFT JOIN account_access x ON x.id = a.id
            GROUP BY a.id ORDER BY a.id"
        ;;
    chars)
        where=""
        if [ -n "${2:-}" ]; then
            name_ok "$2"
            where="WHERE a.username = UPPER('$2')"
        fi
        mariadb -t characters -e "
            SELECT a.username AS account, c.name AS name, c.level AS lvl,
                   ELT(c.race, 'Human','Orc','Dwarf','Night Elf','Undead','Tauren','Gnome','Troll') AS race,
                   ELT(c.class, 'Warrior','Paladin','Hunter','Rogue','Priest','','Shaman','Mage','Warlock','','Druid') AS class,
                   IF(c.gender = 0, 'M', 'F') AS sex,
                   ROUND(c.money / 10000, 2) AS gold,
                   CASE c.map WHEN 0 THEN 'Eastern Kingdoms' WHEN 1 THEN 'Kalimdor' ELSE CONCAT('map ', c.map) END AS world,
                   c.zone AS zone,
                   CONCAT(FLOOR(c.played_time_total / 3600), 'h ', FLOOR(c.played_time_total % 3600 / 60), 'm') AS played,
                   IF(c.online, 'yes', '') AS online,
                   IF(c.logout_time > 0, FROM_UNIXTIME(c.logout_time), '') AS last_seen
            FROM characters c JOIN realmd.account a ON a.id = c.account
            $where ORDER BY a.username, c.level DESC, c.name"
        ;;
    *)
        sed -n '2,/^set -euo/p' "$0" | sed '$d'; exit 1
        ;;
esac
