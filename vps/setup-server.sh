#!/usr/bin/env bash
# Azeroth Warfare on a VPS (Ubuntu 24.04), run as root; safe to run again (each step is once).
#   setup-server.sh PUBLIC_IP [PUBLIC_NAME]
# PUBLIC_NAME is the HTTPS name players reach (a DNS name pointing at PUBLIC_IP); without one it
# is <ip>.sslip.io. Expects under /opt/aw: bin/ etc/ (the Linux build), data/{maps,vmaps,5875}
# (map data), server/{sql,seed,world.zip}, gate.py, Caddyfile, aw_attestation.sql (vps/sql/).
# Makes:
#   the four databases (loaded once), the configs, the gate's settings (/etc/aw/gate.env),
#   the invite-code and attestation tables (realmd.aw_invite, realmd.aw_attestation), services aw-realmd, aw-mangosd, aw-gate, and Caddy
#   with HTTPS at <ip>.sslip.io, whose /files/* (the game) only game accounts can fetch
#   (forward_auth to the gate); the launcher exe stays public.
# Players sign up in the launcher with single-use codes from aw-admin invite new (ship-server.sh
# installs aw-admin).
set -euo pipefail
PUBLIC=${1:?usage: setup-server.sh PUBLIC_IP [PUBLIC_NAME]}
AW=/opt/aw
SQL=$AW/server/sql
HOST="${2:-${PUBLIC//./-}.sslip.io}"
export DEBIAN_FRONTEND=noninteractive
command -v unzip >/dev/null || apt-get install -y -qq unzip >/dev/null

mkdir -p /etc/aw "$AW/logs" "$AW/data/fusion" /srv/aw/files
[ -f /etc/aw/db.pass ] || { head -c 24 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' > /etc/aw/db.pass; chmod 600 /etc/aw/db.pass; }
DBPASS=$(cat /etc/aw/db.pass)
if [ ! -f /etc/aw/gate.env ]; then
    printf 'AW_HOST=%s\nAW_SITES=%s\nAW_GATE_PORT=8090\n' "$HOST" "$HOST" > /etc/aw/gate.env
    chmod 640 /etc/aw/gate.env
fi
# The old shared invite code is gone (single-use codes live in realmd.aw_invite).
sed -i '/^AW_INVITE=/d; /^AW_GM_INVITE=/d; /^AW_ACCOUNT=/d' /etc/aw/gate.env
chgrp aw /etc/aw/gate.env

# --- Databases (as server/azeroth-warfare/scripts/setup-db.sh) -------------------------------
mariadb <<SQL
CREATE DATABASE IF NOT EXISTS realmd DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS mangos DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS characters DEFAULT CHARSET utf8;
CREATE DATABASE IF NOT EXISTS logs DEFAULT CHARSET utf8;
CREATE USER IF NOT EXISTS 'mangos'@'localhost' IDENTIFIED BY '$DBPASS';
CREATE USER IF NOT EXISTS 'mangos'@'127.0.0.1' IDENTIFIED BY '$DBPASS';
GRANT ALL PRIVILEGES ON realmd.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON mangos.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON characters.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
GRANT ALL PRIVILEGES ON logs.* TO 'mangos'@'localhost', 'mangos'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL
loaded() { [ "$(mariadb -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$1'")" != "0" ]; }
migrate() {
    for f in $(ls "$SQL"/migrations/*_"$1".sql 2>/dev/null | sort); do mariadb "$2" < "$f"; done
}
loaded realmd     || { mariadb realmd     < "$SQL/logon.sql";      migrate logon realmd; }
loaded characters || { mariadb characters < "$SQL/characters.sql"; migrate characters characters; }
loaded logs       || { mariadb logs       < "$SQL/logs.sql";       migrate logs logs; }
if ! loaded mangos; then
    echo "Loading the world database (a few minutes)..."
    work=$(mktemp -d)
    unzip -q -o "$AW/server/world.zip" -d "$work"
    world=$(find "$work" -name '*.sql' | grep -iE 'world|mangos' | head -1 || true)
    [ -n "$world" ] || world=$(find "$work" -name '*.sql' | head -1)
    mariadb mangos < "$world"
    rm -rf "$work"
    migrate world mangos
    for f in $(ls "$SQL"/custom/azeroth_warfare/*.sql | sort); do echo "Applying $(basename "$f")"; mariadb mangos < "$f"; done
fi
mariadb realmd <<SQL
INSERT INTO realmlist (id, name, address, localAddress, port, icon, realmflags, timezone, allowedSecurityLevel, population, gamebuild_min, gamebuild_max, realmbuilds)
VALUES (1, 'World of Modern Warfare', '$PUBLIC', '$PUBLIC', 8085, 1, 2, 1, 0, 0, 5875, 5875, '5875 ')
ON DUPLICATE KEY UPDATE name = VALUES(name), address = VALUES(address), localAddress = VALUES(localAddress),
    realmbuilds = IF(realmbuilds = '', VALUES(realmbuilds), realmbuilds);
SQL
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
# Players' legal-access attestations (the gate records them; aw-admin attestations lists them).
mariadb realmd < "$AW/aw_attestation.sql"
# The gate works as the system user aw (no password: the socket says who it is), allowed only:
# read an account's id, name, salt and verifier (the download check, a taken name); add an
# account and its realmcharacters row (sign-up); read the codes and mark one used; read the IP
# ban list; read the uptime; read and add legal-access attestations.
mariadb <<SQL
CREATE USER IF NOT EXISTS 'aw'@'localhost' IDENTIFIED VIA unix_socket;
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

# --- Configs ---------------------------------------------------------------------------------
db() { echo "\"127.0.0.1;3306;mangos;$DBPASS;$1\""; }
sed -e "s|^LoginDatabaseInfo = .*|LoginDatabaseInfo = $(db realmd)|" \
    -e "s|^LogsDir = .*|LogsDir = \"$AW/logs\"|" \
    "$AW/etc/realmd.conf.dist" > "$AW/etc/realmd.conf"
# No console under systemd; remote admin on this machine only (the firewall keeps 3443 closed).
sed -e "s|^LoginDatabase.Info = .*|LoginDatabase.Info = $(db realmd)|" \
    -e "s|^WorldDatabase.Info = .*|WorldDatabase.Info = $(db mangos)|" \
    -e "s|^CharacterDatabase.Info = .*|CharacterDatabase.Info = $(db characters)|" \
    -e "s|^LogsDatabase.Info = .*|LogsDatabase.Info = $(db logs)|" \
    -e "s|^DataDir = .*|DataDir = \"$AW/data\"|" \
    -e "s|^LogsDir = .*|LogsDir = \"$AW/logs\"|" \
    -e "s|^mmap.enabled = .*|mmap.enabled = $([ -d "$AW/data/mmaps" ] && echo 1 || echo 0)|" \
    -e "s|^Console.Enable = .*|Console.Enable = 0|" \
    -e "s|^Ra.Enable = .*|Ra.Enable = 1|" \
    -e "s|^Ra.IP = .*|Ra.IP = 127.0.0.1|" \
    "$AW/etc/mangosd.conf.dist" > "$AW/etc/mangosd.conf"
chown root:aw "$AW/etc/realmd.conf" "$AW/etc/mangosd.conf"
chmod 640 "$AW/etc/realmd.conf" "$AW/etc/mangosd.conf"
[ -f "$AW/data/fusion/weapons.json" ] || cp "$AW/server/seed/weapons.json" "$AW/data/fusion/weapons.json"
cp "$AW/server/seed/catalog.json" "$AW/data/fusion/catalog.json"
chown -R aw:aw "$AW/logs" "$AW/data/fusion" /srv/aw && chmod -R g+rX /srv/aw
# Crash reports from players' launchers (the gate keeps them, aw-admin crashes reads them).
install -d -o aw -g aw -m 750 /var/lib/aw /var/lib/aw/crashes

# --- Services --------------------------------------------------------------------------------
unit() {
    cat > "/etc/systemd/system/$1.service" <<UNIT
[Unit]
Description=$2
After=network-online.target mariadb.service
Requires=mariadb.service

[Service]
User=aw
WorkingDirectory=$AW
$3
Restart=on-failure
RestartSec=5
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
UNIT
}
unit aw-realmd "Azeroth Warfare login server" "ExecStart=$AW/bin/realmd -c $AW/etc/realmd.conf"
unit aw-mangosd "Azeroth Warfare world server" "ExecStart=$AW/bin/mangosd -c $AW/etc/mangosd.conf
TimeoutStopSec=60"
unit aw-gate "Azeroth Warfare gate (sign-up, download check, status)" "EnvironmentFile=/etc/aw/gate.env
ExecStart=/usr/bin/python3 $AW/gate.py"
mkdir -p /etc/systemd/system/caddy.service.d
printf '[Service]\nEnvironmentFile=/etc/aw/gate.env\n' > /etc/systemd/system/caddy.service.d/aw.conf
cp "$AW/Caddyfile" /etc/caddy/Caddyfile
usermod -aG aw caddy
systemctl daemon-reload
systemctl enable -q aw-realmd aw-mangosd aw-gate caddy
systemctl restart aw-realmd aw-mangosd aw-gate caddy
echo "Up: https://$HOST  (invite codes: aw-admin invite new)"
