#!/usr/bin/env bash
# Build the Linux server in WSL and put it on the VPS: sync server/ into /root/aw-src, build and
# install into /root/aw-install, strip, upload the binaries, apply the world database's custom SQL
# (server/sql/custom/azeroth_warfare/*.sql, all idempotent; they write the world database, and touch
# characters only by additive INSERT IGNOREs such as 01's proficiency), and restart the servers.
# Every run also installs the gate (vps/gate.py: sign-up, open or by invite, the download check),
# aw-admin and Caddy's config (vps/Caddyfile: the game files only for game accounts), makes the
# invite-code table if missing, and gives the gate's database user its grants.
#
# Run from Windows in the repository root:
#   wsl -d Ubuntu-24.04 -u root -- bash vps/ship-server.sh [--no-build] [--no-sql]
#
# The server comes from vps/deploy.env.local (copy vps/deploy.env.example): AW_VPS (the ssh
# login), the key (AW_WSL_VPS_KEY, or AW_VPS_KEY under your Windows user folder), AW_PUBLIC_HOST
# and AW_EXTRA_SITES (the names Caddy answers to and the launcher is built for).
# Needs: the WSL build tree made once by hand (/root/aw-build, configured with
# -DSUPPORTED_CLIENT_BUILD=5875 -DBUILD_EXTRACTORS=OFF -DCMAKE_INSTALL_PREFIX=/root/aw-install, ninja).
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
CONFIG="$REPO/vps/deploy.env.local"
[ -f "$CONFIG" ] || { echo "No $CONFIG: copy vps/deploy.env.example there and fill it in." >&2; exit 1; }
# NAME=value lines (no quotes); a variable already in the environment wins.
while IFS='=' read -r name value; do
    name=${name%$'\r'}; value=${value%$'\r'}
    [[ "$name" =~ ^[A-Z_][A-Z0-9_]*$ ]] || continue
    [ -n "${!name:-}" ] || export "$name=$value"
done < "$CONFIG"
VPS=${AW_VPS:?AW_VPS is not set in vps/deploy.env.local}
PUBLIC_HOST=${AW_PUBLIC_HOST:?AW_PUBLIC_HOST is not set in vps/deploy.env.local}
# Every name Caddy answers to: the public one first, then the old ones (old launchers).
SITES=$(printf '%s' "$PUBLIC_HOST,${AW_EXTRA_SITES:-}" | sed 's/,*$//; s/,/, /g')
# The key as WSL sees it: given, or AW_VPS_KEY (~/...) under the Windows user's folder.
if [ -n "${AW_WSL_VPS_KEY:-}" ]; then
    WINKEY=$AW_WSL_VPS_KEY
else
    WINHOME=$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")
    WINKEY="$WINHOME/${AW_VPS_KEY#\~/}"
fi
KEY=/root/.ssh/womw_ship_key
BUILD=1
SQL=1
for a in "$@"; do
    case $a in
        --no-build) BUILD=0 ;;
        --no-sql) SQL=0 ;;
        *) echo "unknown option $a" >&2; exit 2 ;;
    esac
done

# The key, copied with the permissions ssh insists on.
mkdir -p /root/.ssh
install -m 600 "$WINKEY" "$KEY"
SSH="ssh -i $KEY -o StrictHostKeyChecking=accept-new -o BatchMode=yes"
# Never a prompt (a first run has no known_hosts: a prompt would wait forever).
SCP="scp -i $KEY -o StrictHostKeyChecking=accept-new -o BatchMode=yes -q"

if [ $BUILD = 1 ]; then
    echo "== sync server/ into /root/aw-src"
    command -v rsync >/dev/null || apt-get install -y -qq rsync >/dev/null
    rsync -a --delete --exclude .git "$REPO/server/" /root/aw-src/
    echo "== build"
    # 6 jobs: WSL has about half the RAM, and GCC at full width runs it out.
    # Sources are globbed at configure time: re-configure so new files are built.
    cmake /root/aw-build >/root/aw-configure.log 2>&1 || { tail -20 /root/aw-configure.log; exit 1; }
    ninja -C /root/aw-build -j 6 install >/root/aw-build.log 2>&1 || { tail -40 /root/aw-build.log; exit 1; }
    strip /root/aw-install/bin/mangosd /root/aw-install/bin/realmd
    echo "== upload binaries"
    for b in mangosd realmd; do
        $SCP "/root/aw-install/bin/$b" "$VPS:/opt/aw/bin/.new-$b"
    done
    # The fusion catalog the server reads at start (weapons, attachments, streaks). It is made
    # from an MW2 install, so a public checkout has none: the maintainer's copy lives in
    # private-assets/ (see private-assets.md).
    CATALOG="$REPO/server/contrib/fusion/catalog.json"
    [ -f "$CATALOG" ] || CATALOG="$REPO/private-assets/server/contrib/fusion/catalog.json"
    [ -f "$CATALOG" ] || { echo "No fusion catalog.json: see private-assets.md to make one." >&2; exit 1; }
    $SCP "$CATALOG" "$VPS:/opt/aw/data/fusion/.new-catalog.json"
fi

if [ $SQL = 1 ]; then
    echo "== upload world SQL"
    $SSH "$VPS" "rm -rf /tmp/aw-sql && mkdir -p /tmp/aw-sql"
    $SCP "$REPO"/server/sql/custom/azeroth_warfare/*.sql "$VPS:/tmp/aw-sql/"
fi

echo "== upload the gate, aw-admin and Caddy's config"
$SCP "$REPO/vps/gate.py" "$VPS:/tmp/aw-gate.py"
$SCP "$REPO/vps/aw-admin.sh" "$VPS:/tmp/aw-admin"
$SCP "$REPO/vps/Caddyfile" "$VPS:/tmp/aw-Caddyfile"
$SCP "$REPO/vps/sql/aw_attestation.sql" "$VPS:/tmp/aw-attestation.sql"

echo "== stop, swap, apply, start"
$SSH "$VPS" BUILD=$BUILD SQL=$SQL PUBLIC_HOST="'$PUBLIC_HOST'" SITES="'$SITES'" bash -s <<'REMOTE'
set -euo pipefail
systemctl stop aw-mangosd aw-realmd
if [ "$BUILD" = 1 ]; then
    for b in mangosd realmd; do
        if [ -f /opt/aw/bin/.new-$b ]; then
            cp /opt/aw/bin/$b /opt/aw/bin/$b.prev
            mv /opt/aw/bin/.new-$b /opt/aw/bin/$b
            chmod 755 /opt/aw/bin/$b
        fi
    done
    if [ -f /opt/aw/data/fusion/.new-catalog.json ]; then
        mv /opt/aw/data/fusion/.new-catalog.json /opt/aw/data/fusion/catalog.json
        chown aw:aw /opt/aw/data/fusion/catalog.json
    fi
fi
if [ "$SQL" = 1 ]; then
    for f in $(ls /tmp/aw-sql/*.sql | sort); do
        echo "   $(basename "$f")"
        # A failing file is reported, not fatal: the servers must come back up either way.
        mysql mangos < "$f" || echo "   FAILED: $(basename "$f")"
    done
fi
systemctl start aw-realmd aw-mangosd
# The gate: its script (LF endings) and the admin command.
tr -d '\r' < /tmp/aw-gate.py > /opt/aw/gate.py
tr -d '\r' < /tmp/aw-admin > /usr/local/bin/aw-admin
chmod 755 /usr/local/bin/aw-admin
# Crash reports from players' launchers (POST /api/crash; aw-admin crashes reads them).
install -d -o aw -g aw -m 750 /var/lib/aw /var/lib/aw/crashes
# The single-use invite codes (aw-admin invite new makes them; a sign-up uses one up).
mariadb realmd <<'SQL'
CREATE TABLE IF NOT EXISTS aw_invite (
    code CHAR(12) NOT NULL PRIMARY KEY,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    created_by VARCHAR(64) NOT NULL DEFAULT 'admin',
    note VARCHAR(255) NOT NULL DEFAULT '',
    used_by_account VARCHAR(32) NULL,
    used_at DATETIME NULL,
    revoked TINYINT(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Azeroth Warfare single-use invite codes';
SQL
# Players' legal-access attestations (the gate records one at sign-up and once per account from
# the launcher; aw-admin attestations lists them).
mariadb realmd < /tmp/aw-attestation.sql
# The gate's rights, no more: read an account's id, name, salt and verifier (the download check,
# a taken name); add an account and its realmcharacters row (sign-up); read the codes and mark
# one used; read the IP ban list (no sign-ups from a banned address); read the uptime (the
# site's status); read and add legal-access attestations.
mariadb <<'SQL'
REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'aw'@'localhost';
GRANT SELECT (id, username, s, v), INSERT (username, v, s, joindate) ON realmd.account TO 'aw'@'localhost';
GRANT INSERT ON realmd.realmcharacters TO 'aw'@'localhost';
GRANT SELECT ON realmd.realmlist TO 'aw'@'localhost';
GRANT SELECT, UPDATE (used_by_account, used_at) ON realmd.aw_invite TO 'aw'@'localhost';
GRANT SELECT ON realmd.ip_banned TO 'aw'@'localhost';
GRANT SELECT ON realmd.uptime TO 'aw'@'localhost';
GRANT SELECT, INSERT ON realmd.aw_attestation TO 'aw'@'localhost';
FLUSH PRIVILEGES;
SQL
sed -i 's/^Description=Azeroth Warfare sign-up$/Description=Azeroth Warfare gate (sign-up, download check, status)/' \
    /etc/systemd/system/aw-gate.service
systemctl daemon-reload
systemctl restart aw-gate
# The public names (AW_PUBLIC_HOST and AW_EXTRA_SITES in deploy.env.local): the old ones keep
# launchers built for them working. AW_HOST is the name new launchers are built for
# (package/publish.py reads it).
sed -i '/^AW_HOST=/d; /^AW_SITES=/d' /etc/aw/gate.env
printf '%s
' "AW_HOST=$PUBLIC_HOST" "AW_SITES=\"$SITES\"" >> /etc/aw/gate.env
# Caddy: /files/* only for game accounts (forward_auth to the gate), but for the launcher exe.
# The new config is validated (with the env Caddy reads, /etc/aw/gate.env) before it replaces the
# running one.
tr -d '\r' < /tmp/aw-Caddyfile > /etc/caddy/Caddyfile.new
if (set -a; . /etc/aw/gate.env; caddy validate --adapter caddyfile --config /etc/caddy/Caddyfile.new); then
    cp /etc/caddy/Caddyfile /etc/caddy/Caddyfile.prev
    mv /etc/caddy/Caddyfile.new /etc/caddy/Caddyfile
    systemctl reload caddy
    # The old shared invite code: nothing reads it any more (the old Caddyfile did).
    sed -i '/^AW_INVITE=/d; /^AW_GM_INVITE=/d; /^AW_ACCOUNT=/d' /etc/aw/gate.env
    CADDY=ok
else
    rm -f /etc/caddy/Caddyfile.new
    echo "   FAILED: the new Caddyfile doesn't validate; Caddy keeps the old one"
    CADDY=failed
fi
sleep 20
systemctl is-active aw-realmd aw-mangosd aw-gate caddy
[ "$CADDY" = ok ]
REMOTE
echo "== done"
