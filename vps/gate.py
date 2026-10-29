#!/usr/bin/env python3
"""Azeroth Warfare gate, behind Caddy (which serves the files): sign-up, who may download, status.

POST /api/signup   {"name": ..., "password": ..., "code": ... (optional), "attest": true,
                   "attestation_version": N}; /api/register is the same (older launchers). The
                   player must have confirmed the legal-access statement (ATTESTATION_TEXT, version
                   ATTESTATION_VERSION): it is recorded with the account (realmd.aw_attestation:
                   account, time, address, version). Sign-ups are open unless AW_INVITE_REQUIRED=1; then a
                   single-use invite code is needed (realmd.aw_invite; aw-admin invite new makes
                   them). A code given is used up and recorded either way: it is consumed by one
                   conditional UPDATE, so two sign-ups with the same code can't both win.
                   201 made, 400 bad name or password, no attestation (or the honeypot field
                   filled), 403 unknown /
                   used / revoked / missing code, 409 name taken, 429 too many tries or accounts
                   from this address (or too many today in all), 503 the database didn't answer.
GET  /api/signup-info  whether sign-ups are open and an invite code is needed, the name and
                   password rules, and the legal-access statement and its version, for the launcher
                   and the game's Create Account dialog.
GET  /api/attest   with HTTP Basic (a game account, checked as /api/auth checks it): whether that
                   account has confirmed the current statement ({"attested": ..., "version": ...,
                   "text": ...}).
POST /api/attest   with HTTP Basic, {"attest": true, "version": N}: an account made before the
                   statement confirms it (the launcher asks once). 200 recorded, 400 not the
                   current statement, 401/429/503 as /api/auth.
GET  /api/auth     Caddy's forward_auth for /files/* (the manifest and the game files; the
                   launcher exe itself is public so a new player can sign up). HTTP Basic with a
                   game account's name and password, checked against the account's SRP6 verifier
                   in realmd.account (the password is never stored). 200 yes, 401 no (with
                   WWW-Authenticate, so a browser asks), 429 too many wrong tries from this
                   address (or, while an account is getting many wrong passwords from
                   everywhere, for that account from this address: AccountGuard), 503 the
                   database didn't answer.
POST /api/crash    a crash report from the launcher, after the player said yes: HTTP Basic with the
                   game account (checked as /api/auth checks it, before the body is read), a JSON
                   body (Crashes.accept) with the report's text and maybe a gzipped minidump. Kept
                   under AW_CRASH_DIR, one folder per report (meta.json, report.txt, dump.dmp.gz);
                   aw-admin crashes reads them. 201 kept, 400 bad report, 401 no account, 413 too
                   big, 429 too many from this account or address, 507 the store is full.
GET  /api/status   the server for the site: up or not, players online, up since, the version,
                   and a notice while a release is going up (/srv/aw/notice.txt)
Status is public (the site on GitHub Pages reads it) and holds nothing private. The release
notes (/srv/aw/releases.json) stay private: nothing serves them.

The gate uses the database as the system user aw (MariaDB's unix_socket), allowed only what this
needs: read an account's id, name, salt and verifier; add an account (name, verifier, salt, join
date) and its realmcharacters row; read the invite codes and mark one used; read the IP ban list
(no sign-ups from a banned address); read the uptime; read and add legal-access attestations
(realmd.aw_attestation, vps/sql/aw_attestation.sql).
Settings come from the environment (EnvironmentFile=/etc/aw/gate.env):
  AW_GATE_PORT             the port on 127.0.0.1 (8090)
  AW_CRASH_DIR             where crash reports are kept (/var/lib/aw/crashes, owned by aw)
  AW_INVITE_REQUIRED       1: sign-up needs an invite code; 0 (the default): open sign-ups
  AW_SIGNUPS_PER_IP        accounts one address may make per AW_SIGNUP_WINDOW_HOURS (3)
  AW_SIGNUP_WINDOW_HOURS   that window, in hours (24)
  AW_SIGNUPS_PER_DAY       accounts everyone together may make in 24 hours (200; 0 for no limit)
  AW_SIGNUP_TRIES_PER_HOUR sign-up tries (good or bad) one address may make in an hour (10)
The address limits live in memory: a restart of the gate forgets them. They count an IPv6 address
by its /64 (network_of): one machine has a whole /64.
"""
import base64
import binascii
import hashlib
import hmac
import ipaddress
import json
import os
import re
import secrets
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer



def setting(name: str, default: int) -> int:
    """A whole-number setting from the environment; a missing or unreadable one is the default."""
    try:
        return int(os.environ.get(name, "").strip() or default)
    except ValueError:
        return default


PORT = setting("AW_GATE_PORT", 8090)
INVITE_REQUIRED = setting("AW_INVITE_REQUIRED", 0) != 0
SIGNUPS_PER_IP = setting("AW_SIGNUPS_PER_IP", 3)
SIGNUP_WINDOW = setting("AW_SIGNUP_WINDOW_HOURS", 24) * 3600
SIGNUPS_PER_DAY = setting("AW_SIGNUPS_PER_DAY", 200)
SIGNUP_TRIES_PER_HOUR = setting("AW_SIGNUP_TRIES_PER_HOUR", 10)
FILES = "/srv/aw/files"
RELEASES = "/srv/aw/releases.json"
# While this file is there (package/publish.py writes it), the site says the game isn't ready.
NOTICE = "/srv/aw/notice.txt"
# The status is worked out at most this often (seconds), however many visitors ask.
STATUS_EVERY = 15
status_cache: tuple[float, dict] = (0.0, {})
REALM = "World of Modern Warfare"

# --- SRP6, as the server's (server/src/shared/Crypto/Authentication/SRP6.cpp, CalculateVerifier,
# fed by server/src/realmd/aw_account.cpp) -----------------------------------------------------
# N and g of the WoW 1.12 login.
N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
G = 7
# Account names as aw-account takes them (it uppercases them); passwords at most 16 characters.
NAME = re.compile(r"^[A-Za-z0-9]{1,16}$")
PASSWORD = re.compile(r"^[\x20-\x7e]{1,16}$")
# What sign-up takes: names as aw-admin's, passwords 6-16 printable characters without spaces.
NEW_NAME = re.compile(r"^[A-Za-z0-9]{3,16}$")
NEW_PASSWORD = re.compile(r"^[\x21-\x7e]{6,16}$")
# Invite codes: 12 characters that can't be misread (no 0/O, 1/I/L), shown as XXXX-XXXX-XXXX.
# aw-admin invite new makes them from the same alphabet.
CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"
CODE = re.compile(rf"^[{CODE_ALPHABET}]{{12}}$")
# An address as Caddy forwards it (IPv4 or IPv6), the shape realmd.ip_banned keeps.
IP = re.compile(r"^[0-9A-Fa-f.:]{2,45}$")

# The legal-access statement a player confirms before signing up (a box in the game's Create
# Account dialog, Y typed in the launcher), and its version. A new wording is a new version, and
# every account is asked again (the launcher asks once per account, POST /api/attest).
ATTESTATION_TEXT = ("I own and have legal access to World of Warcraft and Call of Duty: "
                    "Modern Warfare 2 (2009).")
ATTESTATION_VERSION = 1
ATTESTATION_NEEDED = f'To sign up, confirm the statement: "{ATTESTATION_TEXT}"'


def verifier(name: str, password: str, salt_hex: str) -> int:
    """v = g^x mod N, x = SHA1(s || SHA1(UPPER(name) ":" UPPER(password))) as the server makes it.

    The server's BigNumbers go into hashes little-endian and at their own length (no padding):
    the salt as stored (hex, most significant first) is hashed byte-reversed, and the digest x is
    read back little-endian. Upper-casing is ASCII only, like the server's toupper.
    """
    inner = hashlib.sha1(name.encode("ascii").upper() + b":" + password.encode("ascii").upper()).digest()
    s = int(salt_hex, 16)
    salt = s.to_bytes((s.bit_length() + 7) // 8, "little")
    x = int.from_bytes(hashlib.sha1(salt + inner).digest(), "little")
    return pow(G, x, N)


def bn_hex(n: int) -> str:
    """A number as the server's BigNumber::AsHexStr writes it: upper-case, whole bytes."""
    return n.to_bytes(max(1, (n.bit_length() + 7) // 8), "big").hex().upper()


def new_verifier(name: str, password: str) -> tuple[str, str]:
    """A fresh salt and the verifier for it, (s, v) as hex for realmd.account, as aw-account
    makes them: the salt is 32 random bytes with the top bit set (the server's SetRand(256))."""
    s = bn_hex(secrets.randbits(256) | 1 << 255)
    return s, bn_hex(verifier(name, password, s))


def password_ok(name: str, password: str, salt_hex: str, v_hex: str) -> bool:
    try:
        stored = int(v_hex, 16).to_bytes(32, "big")
        mine = verifier(name, password, salt_hex).to_bytes(32, "big")
    except (ValueError, OverflowError, UnicodeEncodeError):
        return False
    return hmac.compare_digest(mine, stored)


# --- The database ----------------------------------------------------------------------------
class DatabaseDown(Exception):
    pass


class DatabaseError(Exception):
    """A statement failed (the message is MariaDB's, e.g. "Duplicate entry ...")."""


class MariaDb:
    """realmd through the mariadb client, one connection per call. The SQL is built from values
    already checked against NAME, CODE or hex: nothing a player typed reaches it unchecked."""

    def query(self, sql: str) -> list[list[str]]:
        try:
            run = subprocess.run(["mariadb", "realmd", "-N", "-B", "-e", sql],
                                 capture_output=True, text=True, timeout=10)
        except (OSError, subprocess.SubprocessError) as e:
            raise DatabaseDown(str(e))
        if run.returncode != 0:
            if "ERROR 2002" in run.stderr or "Can't connect" in run.stderr:
                raise DatabaseDown(run.stderr.strip())
            raise DatabaseError(run.stderr.strip())
        return [line.split("\t") for line in run.stdout.splitlines() if line]


db = MariaDb()


def lookup(name: str) -> tuple[str, str] | None:
    """The account's salt and verifier (hex), or None when there is no such account."""
    name = name.upper()
    if not NAME.match(name):  # also what keeps the query safe
        return None
    try:
        rows = db.query(f"SELECT s, v FROM account WHERE username = '{name}'")
    except DatabaseError as e:
        raise DatabaseDown(str(e))
    return (rows[0][0], rows[0][1]) if rows and len(rows[0]) == 2 else None


def credentials(header: str) -> tuple[str, str] | None:
    """The name and password of an `Authorization: Basic ...` header, or None."""
    kind, _, token = header.strip().partition(" ")
    if kind.lower() != "basic":
        return None
    try:
        name, sep, password = base64.b64decode(token.strip(), validate=True).decode("utf-8").partition(":")
    except (ValueError, UnicodeDecodeError):
        return None
    return (name, password) if sep else None


def network_of(addr: str) -> str:
    """What an address is counted as by the limits: an IPv4 address itself, an IPv6 address by its
    /64. One home line or rented server is handed a whole /64 (2^64 addresses), so counting IPv6
    addresses one by one would let a single machine take a fresh address for every try and pass
    every per-address limit. Anything that isn't an address is itself."""
    try:
        ip = ipaddress.ip_address(addr.strip())
    except ValueError:
        return addr
    if ip.version == 6:
        if ip.ipv4_mapped:
            return str(ip.ipv4_mapped)
        return str(ipaddress.ip_network(f"{ip}/64", strict=False))
    return str(ip)


def body_length(headers, cap: int) -> int:
    """A request's Content-Length, at most `cap`; missing, unreadable or negative is 0 (a negative
    length would read until the client hangs up)."""
    try:
        length = int(headers.get("Content-Length", "0") or 0)
    except (TypeError, ValueError):
        return 0
    return max(0, min(length, cap))


class Limiter:
    """At most `limit` counted events per address in a sliding `window` (seconds). With
    `by_network`, IPv6 addresses count by their /64 (network_of)."""

    def __init__(self, limit: int, window: float, by_network: bool = False):
        self.limit = limit
        self.window = window
        self.by_network = by_network
        self.events: dict[str, list[float]] = {}

    def key(self, addr: str) -> str:
        return network_of(addr) if self.by_network else addr

    def blocked(self, addr: str, now: float) -> bool:
        addr = self.key(addr)
        recent = [t for t in self.events.get(addr, []) if now - t < self.window]
        if recent:
            self.events[addr] = recent
        else:
            self.events.pop(addr, None)
        return self.limit > 0 and len(recent) >= self.limit

    def count(self, addr: str, now: float) -> None:
        addr = self.key(addr)
        self.events.setdefault(addr, []).append(now)
        if len(self.events) > 10000:  # forget the addresses that have gone quiet
            for a in [a for a, ts in self.events.items() if now - ts[-1] >= self.window]:
                del self.events[a]

    def uncount(self, addr: str, at: float) -> None:
        """Take back the event counted at `at` (a sign-up that didn't happen after all)."""
        addr = self.key(addr)
        events = self.events.get(addr, [])
        if at in events:
            events.remove(at)


class AccountGuard:
    """A soft limit on wrong passwords for one account, from everywhere together (security review
    2026-10, #33). The per-address limit alone lets a guesser with many addresses try one account
    without end; a hard per-account lockout would let anyone keep a player from downloading by
    getting their password wrong on purpose. So the two are combined:

    - While an account has fewer than `attack` wrong passwords in `window` seconds, nothing
      changes: each address has its own limit (Auth.failures).
    - Once it has that many, the account is under attack: each address (an IPv6 /64 counted as
      one) may get that account's password wrong at most `per_address` times in the window. A
      guesser with a thousand addresses gets three guesses from each instead of ten, and
      every one of them is counted against its own address only.
    - Nobody is locked out by it. The right password from an address that still has its few tries
      gets in, and an address that has signed in to the account before (`known_ttl`, kept in
      memory) is never held up by the account's limit, however many guesses come from elsewhere.

    What it can't stop: a guesser on the very network the player uses up that network's few
    tries; the player's next download from an address that has signed in before still works."""

    def __init__(self, attack: int = 50, per_address: int = 3, window: float = 900,
                 known_ttl: float = 30 * 86400):
        self.wrong = Limiter(attack, window)
        self.tries = Limiter(per_address, window)
        self.known_ttl = known_ttl
        self.known: dict[tuple[str, str], float] = {}

    @staticmethod
    def pair(name: str, addr: str) -> tuple[str, str]:
        return name.upper(), network_of(addr)

    def under_attack(self, name: str, now: float) -> bool:
        return self.wrong.blocked(name.upper(), now)

    def refused(self, name: str, addr: str, now: float) -> bool:
        """Whether a try for `name` from `addr` is turned away before its password is checked."""
        pair = self.pair(name, addr)
        if self.known.get(pair, 0) > now or not self.under_attack(name, now):
            return False
        return self.tries.blocked("|".join(pair), now)

    def failed(self, name: str, addr: str, now: float) -> None:
        pair = self.pair(name, addr)
        self.wrong.count(pair[0], now)
        self.tries.count("|".join(pair), now)

    def succeeded(self, name: str, addr: str, now: float) -> None:
        self.known[self.pair(name, addr)] = now + self.known_ttl
        if len(self.known) > 100000:  # forget the pairs that have lapsed
            for k in [k for k, until in self.known.items() if until <= now]:
                del self.known[k]


class Auth:
    """Checks download credentials: a success is remembered a while (so the thousands of file
    requests of an install don't each do the SRP math), an address with too many wrong tries is
    turned away for a while, and an account getting many wrong passwords from everywhere is
    guarded softly (AccountGuard)."""

    def __init__(self, lookup=lookup, clock=time.monotonic, ttl: float = 600,
                 max_failures: int = 10, window: float = 900, guard: AccountGuard | None = None):
        self.lookup = lookup
        self.clock = clock
        self.ttl = ttl
        self.failures = Limiter(max_failures, window, by_network=True)
        self.guard = guard if guard is not None else AccountGuard(window=window)
        # Cache keys are keyed hashes of the name and password: no password sits in memory.
        self.key = secrets.token_bytes(32)
        self.good: dict[bytes, float] = {}
        self.lock = threading.Lock()

    def cache_key(self, name: str, password: str) -> bytes:
        # The server takes passwords in any case (it hashes them upper-cased): so does the cache.
        material = name.upper().encode("utf-8") + b"\0" + password.upper().encode("utf-8")
        return hmac.new(self.key, material, hashlib.sha256).digest()

    def check(self, header: str | None, addr: str) -> int:
        """200, 401, 429 or 503 for a request's Authorization header from an address."""
        if not header:
            return 401  # a first request, before the browser has asked: not a wrong try
        now = self.clock()
        given = credentials(header)
        key = self.cache_key(*given) if given else None
        with self.lock:
            if key and self.good.get(key, 0) > now:
                return 200
            if self.failures.blocked(addr, now):
                return 429
            if given and self.guard.refused(given[0], addr, now):
                return 429
        ok = False
        if given and NAME.match(given[0]) and PASSWORD.match(given[1]):
            try:
                found = self.lookup(given[0])
            except (DatabaseDown, OSError, subprocess.SubprocessError):
                return 503
            ok = found is not None and password_ok(given[0], given[1], *found)
        with self.lock:
            if ok:
                self.good[key] = now + self.ttl
                if len(self.good) > 10000:
                    for k in [k for k, until in self.good.items() if until <= now]:
                        del self.good[k]
                self.guard.succeeded(given[0], addr, now)
                return 200
            self.failures.count(addr, now)
            if given and NAME.match(given[0]):
                self.guard.failed(given[0], addr, now)
            return 401


def normalize_code(text: str) -> str:
    """An invite code as typed (any case, with dashes or spaces) as it is stored."""
    return re.sub(r"[\s-]", "", text).upper()


# Names nobody signs up as: they would pass for the staff.
RESERVED = {"ADMIN", "ADMINISTRATOR", "GM", "GAMEMASTER", "MODERATOR", "MOD", "ROOT", "SYSTEM",
            "SERVER", "SUPPORT", "STAFF", "OWNER", "WOMW", "BLIZZARD", "OFFICIAL"}
NAME_RULE = "Account names are 3-16 letters or digits."
PASSWORD_RULE = "Passwords are 6-16 letters, digits or symbols (no spaces)."


def check_new_account(name: str, password: str) -> str | None:
    """Why a new account's name and password won't do, or None when they will. The login server
    takes names and passwords of at most 16 characters (AccountMgr's MAX_ACCOUNT_STR), the 1.12
    login screen types only what is printable, and aw-account upper-cases both."""
    if not NEW_NAME.match(name):
        return NAME_RULE
    upper = name.upper()
    if upper in RESERVED or "ADMIN" in upper or "GAMEMASTER" in upper:
        return "That account name is reserved; pick another."
    if not NEW_PASSWORD.match(password):
        return PASSWORD_RULE
    if password.upper() == upper:
        return "The password can't be the account name."
    return None


def signup_info() -> dict:
    """What the launcher and the game's Create Account dialog ask before they show the form."""
    return {"open": True, "invite_required": INVITE_REQUIRED,
            "name_rule": NAME_RULE, "password_rule": PASSWORD_RULE,
            "attestation": {"text": ATTESTATION_TEXT, "version": ATTESTATION_VERSION}}


def attested_version(data: dict):
    """The statement version a request says the player confirmed ({"attest": true,
    "attestation_version": N}, or "version" for /api/attest), or None."""
    version = data.get("attestation_version", data.get("version"))
    if data.get("attest") is not True or not isinstance(version, int) or isinstance(version, bool):
        return None
    return version


def address_for_sql(addr: str) -> str:
    """The player's address as it may go into SQL: an IPv4 or IPv6 shape, else nothing."""
    return addr if IP.match(addr) else ""


class Registrar:
    """Sign-up: open, or with a single-use invite code (`invite_required`).

    A code, when one is given, is claimed first, by one UPDATE that only matches an unused,
    unrevoked code: of any number of sign-ups racing with one code, exactly one sees a changed row.
    Then the account is added; if that fails (the name was taken in the meantime), the claim is
    released. Every try counts against the address (`limit` a `window`), so codes can't be guessed
    at speed; each account made counts against the address (`per_ip` a `per_ip_window`) and
    against everyone (`per_day`). The slot is taken before the account is made and given back
    when it isn't, so racing sign-ups from one address can't pass the limit together. The account
    and its legal-access attestation (`attested`: the statement version the player confirmed) go
    in together, in one transaction."""

    def __init__(self, database=None, clock=time.monotonic, limit: int | None = None,
                 window: float = 3600, invite_required: bool | None = None,
                 per_ip: int | None = None, per_ip_window: float | None = None,
                 per_day: int | None = None):
        self.database = database
        self.clock = clock
        self.tries = Limiter(SIGNUP_TRIES_PER_HOUR if limit is None else limit, window, by_network=True)
        self.made = Limiter(SIGNUPS_PER_IP if per_ip is None else per_ip,
                            SIGNUP_WINDOW if per_ip_window is None else per_ip_window, by_network=True)
        self.everyone = Limiter(SIGNUPS_PER_DAY if per_day is None else per_day, 24 * 3600)
        self.invite_required = INVITE_REQUIRED if invite_required is None else invite_required
        self.lock = threading.Lock()

    @property
    def db(self):
        return self.database or db

    def register(self, code: str, name: str, password: str, addr: str,
                 honeypot: str = "", attested: int | None = None) -> tuple[int, str]:
        now = self.clock()
        with self.lock:
            if self.tries.blocked(addr, now):
                return 429, "Too many sign-up tries from here; try again in an hour."
            self.tries.count(addr, now)
        # A form field people never see: only a bot fills it in.
        if honeypot:
            return 400, "bad request"
        code = normalize_code(code)
        problem = check_new_account(name, password)
        if problem:
            return 400, problem
        if attested != ATTESTATION_VERSION:
            return 400, ATTESTATION_NEEDED
        if code and not CODE.match(code):
            return 403, "That invite code isn't right."
        if not code and self.invite_required:
            return 403, "Sign-ups here need an invite code; ask the server owner for one."
        with self.lock:
            if self.made.blocked(addr, now):
                return 429, "This address has made its accounts for today; try again tomorrow."
            if self.everyone.blocked("*", now):
                return 429, "Lots of people are signing up today; please try again tomorrow."
            self.made.count(addr, now)
            self.everyone.count("*", now)
        name = name.upper()
        try:
            if self.address_banned(addr):
                result = 403, "Sign-ups from your address are closed."
            else:
                result = self.make(code, name, password, addr)
        except (DatabaseDown, DatabaseError) as e:
            print(f"gate: sign-up for {name}: {e}", flush=True)
            result = 503, "The server's database isn't answering; try again soon."
        if result[0] == 201:
            how = f"invite {code}" if code else "open sign-up"
            print(f"gate: account {name} made from {addr} ({how})", flush=True)
        else:
            with self.lock:
                self.made.uncount(addr, now)
                self.everyone.uncount("*", now)
        return result

    def address_banned(self, addr: str) -> bool:
        """Whether the login server's IP ban list (realmd.ip_banned: `.ban ip`, aw-admin ban ip)
        has the address, as the login server reads it: permanent (unbandate = bandate) or not
        over yet. A list the gate may not read (an older grant) bans nobody."""
        if not IP.match(addr):  # also what keeps the query safe
            return False
        try:
            return bool(self.db.query(
                f"SELECT 1 FROM ip_banned WHERE ip = '{addr}' "
                "AND (unbandate = bandate OR unbandate > UNIX_TIMESTAMP())"))
        except DatabaseError as e:
            print(f"gate: can't read ip_banned: {e}", flush=True)
            return False

    def make(self, code: str, name: str, password: str, addr: str = "") -> tuple[int, str]:
        q = self.db.query
        if q(f"SELECT id FROM account WHERE username = '{name}'"):
            return 409, "That account name is taken."
        if code:
            claimed = q(f"UPDATE aw_invite SET used_by_account = '{name}', used_at = NOW() "
                        f"WHERE code = '{code}' AND used_by_account IS NULL AND revoked = 0; "
                        "SELECT ROW_COUNT()")
            if claimed[-1][0] != "1":
                state = q(f"SELECT used_by_account IS NOT NULL, revoked FROM aw_invite WHERE code = '{code}'")
                if not state:
                    return 403, "That invite code isn't right."
                if state[0][0] == "1":
                    return 403, "That invite code has already been used."
                return 403, "That invite code was cancelled."
        s, v = new_verifier(name, password)
        ip = address_for_sql(addr)
        try:
            # The account and its attestation together: if the second fails, the client stops, the
            # connection closes and the open transaction (the account too) is rolled back.
            q("START TRANSACTION; "
              f"INSERT INTO account (username, v, s, joindate) VALUES ('{name}', '{v}', '{s}', NOW()); "
              "INSERT INTO aw_attestation (account_id, version, attested_at, ip, source) "
              f"SELECT id, {ATTESTATION_VERSION}, UTC_TIMESTAMP(), '{ip}', 'signup' "
              f"FROM account WHERE username = '{name}'; "
              "COMMIT")
        except (DatabaseError, DatabaseDown) as e:
            # The code goes back: only this sign-up's claim (still holding this name) is undone.
            if code:
                q(f"UPDATE aw_invite SET used_by_account = NULL, used_at = NULL "
                  f"WHERE code = '{code}' AND used_by_account = '{name}'")
            if "Duplicate entry" in str(e):
                return 409, "That account name is taken."
            return 503, "The server couldn't make the account; try again soon."
        try:
            # As aw-account does: the account's row per realm (the login server copes without).
            q("INSERT IGNORE INTO realmcharacters (realmid, acctid, numchars) "
              f"SELECT r.id, a.id, 0 FROM realmlist r, account a WHERE a.username = '{name}'")
        except (DatabaseError, DatabaseDown) as e:
            print(f"gate: realmcharacters for {name}: {e}", flush=True)
        return 201, f"Account {name} made."


class Attestations:
    """Legal-access attestations of accounts that already exist (made before the statement): the
    launcher asks whether its account has confirmed the current statement and, once the player
    types Y, records it. `name` has passed the download check (`Auth`), so it is a NAME."""

    def __init__(self, database=None):
        self.database = database

    @property
    def db(self):
        return self.database or db

    def attested(self, name: str) -> bool:
        name = name.upper()
        if not NAME.match(name):
            return False
        return bool(self.db.query(
            "SELECT 1 FROM aw_attestation t JOIN account a ON a.id = t.account_id "
            f"WHERE a.username = '{name}' AND t.version = {ATTESTATION_VERSION}"))

    def record(self, name: str, addr: str, source: str = "launcher") -> None:
        """Keep the first confirmation of this version (a repeat changes nothing)."""
        name = name.upper()
        if not NAME.match(name) or source not in ("signup", "launcher"):
            raise ValueError("bad attestation")
        self.db.query(
            "INSERT IGNORE INTO aw_attestation (account_id, version, attested_at, ip, source) "
            f"SELECT id, {ATTESTATION_VERSION}, UTC_TIMESTAMP(), '{address_for_sql(addr)}', '{source}' "
            f"FROM account WHERE username = '{name}'")
        print(f"gate: attestation v{ATTESTATION_VERSION} from {name} ({source})", flush=True)


# --- Crash reports from players' games ---------------------------------------------------------
CRASH_DIR = os.environ.get("AW_CRASH_DIR", "/var/lib/aw/crashes")
# The whole request (Caddy refuses more first: vps/Caddyfile): the report, the dump in base64.
CRASH_MAX_BODY = 12 * 1024 * 1024
CRASH_MAX_REPORT = 512 * 1024  # bytes of report text
CRASH_MAX_DUMP = 8 * 1024 * 1024  # bytes of gzipped minidump
CRASH_KINDS = ("panic", "native", "exit")
CRASH_ID = re.compile(r"^[0-9]{8}-[0-9]{6}-[0-9a-f]{6}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
# A `label: value` line at the top of a report, as the game writes them.
HEADER_LINE = re.compile(r"^([a-z]+):\s+(.*)$")


def clean(text, limit: int) -> str:
    """A short value from the launcher as one printable line, at most `limit` characters."""
    text = "" if text is None else str(text)
    return re.sub(r"[^\x20-\x7e]", "?", text.strip())[:limit]


def exit_meaning(code: int | None) -> str:
    """A Windows exit code as people say it."""
    if code is None:
        return "no exit code"
    names = {
        0xC0000005: "access violation", 0xC00000FD: "stack overflow",
        0xC0000409: "fail fast (abort)", 0xC0000374: "heap corruption",
        0xC000001D: "illegal instruction", 0xC0000017: "out of memory",
        0xE06D7363: "C++ exception", 101: "Rust panic",
    }
    code &= 0xFFFFFFFF
    shown = f"0x{code:08X}" if code > 0xFFFF else str(code)
    return f"{shown} ({names[code]})" if code in names else shown


def report_fields(report: str) -> dict[str, str]:
    """The `label: value` lines at the top of a report (up to the first blank line)."""
    fields: dict[str, str] = {}
    for line in report.splitlines()[1:60]:
        if not line.strip():
            break
        m = HEADER_LINE.match(line)
        if m and m.group(1) not in fields:
            fields[m.group(1)] = m.group(2).strip()
    return fields


def summarize(kind: str, report: str, exit_code: int | None) -> dict[str, str]:
    """What crashed and where, and a signature that groups the same crash: numbers in a panic's
    message (indices, lengths, ids) don't make two crashes different."""
    f = report_fields(report)
    if kind == "panic" and f.get("panic"):
        message, location = f["panic"], f.get("at", "")
    elif kind == "native" and f.get("exception"):
        message, location = f["exception"], f.get("address", "")
    else:
        message, location = f"exited with {exit_meaning(exit_code)}", ""
    message, location = clean(message, 300), clean(location, 200)
    # A panic's numbers are its data; an exception code or exit code is what it is.
    shape = re.sub(r"0x[0-9a-fA-F]+|\d+", "N", message) if kind == "panic" else message
    signature = f"{kind}: {shape}" + (f" @ {location}" if location else "")
    return {"message": message, "location": location, "signature": signature[:400]}


class Crashes:
    """Crash reports, one folder each under `root`: meta.json (written last, so a folder without
    it is a report still being written), report.txt and maybe dump.dmp.gz. Only the account's
    name comes from the login; nothing about the player's address is kept."""

    def __init__(self, root: str | None = None, clock=time.time, per_account: tuple[int, float] = (5, 3600),
                 per_address: tuple[int, float] = (10, 3600), max_reports: int = 2000,
                 max_bytes: int = 4 * 1024 ** 3):
        self.root = root or CRASH_DIR
        self.clock = clock
        self.accounts = Limiter(*per_account)
        self.addresses = Limiter(*per_address, by_network=True)
        self.max_reports = max_reports
        self.max_bytes = max_bytes
        self.lock = threading.Lock()

    def stored(self) -> tuple[int, int]:
        """How many reports are kept and their size in bytes."""
        count = size = 0
        try:
            folders = list(os.scandir(self.root))
        except OSError:
            return 0, 0
        for d in folders:
            if not d.is_dir() or not CRASH_ID.match(d.name):
                continue
            count += 1
            for f in os.scandir(d.path):
                try:
                    size += f.stat().st_size
                except OSError:
                    pass
        return count, size

    def accept(self, account: str, addr: str, data) -> tuple[int, str, str | None]:
        """(status, message, report id) for a report from a logged-in account."""
        if not isinstance(data, dict):
            return 400, "bad report", None
        now = self.clock()
        with self.lock:
            if self.accounts.blocked(account, now) or self.addresses.blocked(addr, now):
                return 429, "Too many crash reports from you for now; thanks, we have them.", None
            self.accounts.count(account, now)
            self.addresses.count(addr, now)
        kind = data.get("kind")
        report = data.get("report")
        if kind not in CRASH_KINDS or not isinstance(report, str):
            return 400, "bad report", None
        report_bytes = report.encode("utf-8", "replace")
        if len(report_bytes) > CRASH_MAX_REPORT:
            return 413, "The report is too big.", None
        exit_code = data.get("exit_code")
        if exit_code is not None and (not isinstance(exit_code, int) or isinstance(exit_code, bool)
                                      or not -2 ** 63 <= exit_code < 2 ** 64):
            return 400, "bad exit code", None
        crashed_at = data.get("crashed_at")
        if crashed_at is not None and (not isinstance(crashed_at, int) or isinstance(crashed_at, bool)
                                       or not 0 < crashed_at < now + 86400):
            return 400, "bad time", None
        sha = clean(data.get("client_sha256"), 65).lower()
        if sha and not SHA256.match(sha):
            return 400, "bad client hash", None
        dump = None
        if data.get("dump_gz"):
            try:
                dump = base64.b64decode(str(data["dump_gz"]), validate=True)
            except (ValueError, binascii.Error):
                return 400, "bad dump", None
            if len(dump) > CRASH_MAX_DUMP:
                return 413, "The crash dump is too big.", None
            if not dump.startswith(b"\x1f\x8b"):
                return 400, "the dump isn't gzipped", None
        with self.lock:
            count, size = self.stored()
            if count >= self.max_reports or size + len(report_bytes) + len(dump or b"") > self.max_bytes:
                return 507, "The server's crash store is full; the report wasn't kept.", None
            cid = time.strftime("%Y%m%d-%H%M%S", time.gmtime(now)) + "-" + secrets.token_hex(3)
            folder = os.path.join(self.root, cid)
            meta = {
                "id": cid, "account": account, "received": int(now),
                "crashed_at": crashed_at, "kind": kind, "exit_code": exit_code,
                "game_version": clean(data.get("game_version"), 64),
                "client_sha256": sha,
                "launcher_version": clean(data.get("launcher_version"), 32),
                "report_bytes": len(report_bytes), "dump_bytes": len(dump) if dump else 0,
                **summarize(kind, report, exit_code),
            }
            try:
                os.makedirs(folder, mode=0o750)
                with open(os.path.join(folder, "report.txt"), "wb") as f:
                    f.write(report_bytes)
                if dump:
                    with open(os.path.join(folder, "dump.dmp.gz"), "wb") as f:
                        f.write(dump)
                with open(os.path.join(folder, "meta.json.tmp"), "w") as f:
                    json.dump(meta, f, indent=1)
                os.replace(os.path.join(folder, "meta.json.tmp"), os.path.join(folder, "meta.json"))
            except OSError as e:
                print(f"gate: crash report from {account}: {e}", flush=True)
                shutil.rmtree(folder, ignore_errors=True)
                return 503, "The server couldn't keep the report; try again later.", None
        print(f"gate: crash report {cid} from {account}: {meta['signature']}", flush=True)
        return 201, "Thanks: the crash report was sent.", cid


auth = Auth()
registrar = Registrar()
attestations = Attestations()
crashes = Crashes()


# --- Status ------------------------------------------------------------------------------------
def listening(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


def status() -> dict:
    global status_cache
    at, cached = status_cache
    if time.time() - at < STATUS_EVERY:
        return cached
    up = listening(3724) and listening(8085)
    players, since = 0, None
    # The world server writes its start and its players into realmd.uptime every few minutes.
    row = subprocess.run(
        ["mariadb", "realmd", "-N", "-e",
         "SELECT starttime, onlineplayers FROM uptime ORDER BY starttime DESC LIMIT 1"],
        capture_output=True, text=True,
    ).stdout.split()
    if len(row) == 2:
        since, players = int(row[0]), int(row[1])
    try:
        version = json.load(open(f"{FILES}/manifest.json"))["version"]
    except (OSError, ValueError, KeyError):
        version = None
    try:
        notice = open(NOTICE).read().strip() or None
    except OSError:
        notice = None
    result = {"online": up, "players": players if up else 0, "since": since if up else None,
              "version": version, "notice": notice, "checked": int(time.time())}
    status_cache = (time.time(), result)
    return result


MESSAGES = {
    200: "ok",
    401: "Wrong account name or password, or no account yet: sign up in the launcher",
    429: "too many wrong tries from here; wait 15 minutes",
    503: "the server's database isn't answering; try again soon",
}


class Gate(BaseHTTPRequestHandler):
    # A client that stops sending (a slow or stalled body) lets its thread go after this long,
    # rather than holding it for good.
    timeout = 60

    def send_json(self, code: int, data, public: bool = False, headers: dict | None = None) -> None:
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        if public:
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def address(self) -> str:
        # Caddy sets X-Forwarded-For to the player's address (it drops what an untrusted client
        # sent); the last entry is the one Caddy added, whatever else is there.
        headers = getattr(self, "headers", None)  # not there on a request too broken to parse
        forwarded = headers.get("X-Forwarded-For", "") if headers else ""
        return forwarded.split(",")[-1].strip() or self.client_address[0]

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/api/auth":
            code = auth.check(self.headers.get("Authorization"), self.address())
            headers = {"Cache-Control": "no-store"}
            if code == 401:
                headers["WWW-Authenticate"] = f'Basic realm="{REALM}", charset="UTF-8"'
            elif code == 429:
                headers["Retry-After"] = "900"
            return self.send_json(code, {"message": MESSAGES[code]}, headers=headers)
        if path == "/api/status":
            return self.send_json(200, status(), public=True)
        if path == "/api/signup-info":
            return self.send_json(200, signup_info(), public=True)
        if path == "/api/attest":
            return self.attest(None)
        self.send_json(404, {"message": "not found"})

    do_HEAD = do_GET

    def do_POST(self) -> None:
        path = self.path.split("?", 1)[0]
        if path == "/api/crash":
            return self.crash()
        if path == "/api/attest":
            return self.attest(self.small_json())
        if path not in ("/api/signup", "/api/register"):
            return self.send_json(404, {"message": "not found"})
        try:
            data = json.loads(self.rfile.read(body_length(self.headers, 1024)) or b"{}")
            name, password = str(data["name"]), str(data["password"])
            code = str(data.get("code") or "")
            honeypot = str(data.get("website") or "")
            attested = attested_version(data)
        except (ValueError, KeyError, TypeError, AttributeError):
            return self.send_json(400, {"message": "bad request"})
        status_code, message = registrar.register(code, name, password, self.address(), honeypot,
                                                  attested=attested)
        self.send_json(status_code, {"message": message})

    def small_json(self):
        """A request body of at most 1 KB as JSON, or None when it isn't."""
        try:
            return json.loads(self.rfile.read(body_length(self.headers, 1024)) or b"{}")
        except ValueError:
            return None

    def attest(self, data) -> None:
        """GET (data None): whether the logged-in account confirmed the current statement. POST:
        it confirms it now."""
        header = self.headers.get("Authorization")
        code = auth.check(header, self.address())
        if code != 200:
            headers = {"Cache-Control": "no-store"}
            if code == 401:
                headers["WWW-Authenticate"] = f'Basic realm="{REALM}", charset="UTF-8"'
            elif code == 429:
                headers["Retry-After"] = "900"
            return self.send_json(code, {"message": MESSAGES[code]}, headers=headers)
        account = credentials(header)[0].upper()
        try:
            if self.command == "POST":
                if not isinstance(data, dict) or attested_version(data) != ATTESTATION_VERSION:
                    return self.send_json(400, {"message": "That isn't the current statement.",
                                                "version": ATTESTATION_VERSION,
                                                "text": ATTESTATION_TEXT})
                attestations.record(account, self.address(), "launcher")
                return self.send_json(200, {"message": "Recorded.", "attested": True,
                                            "version": ATTESTATION_VERSION})
            return self.send_json(200, {"attested": attestations.attested(account),
                                        "version": ATTESTATION_VERSION, "text": ATTESTATION_TEXT},
                                  headers={"Cache-Control": "no-store"})
        except (DatabaseDown, DatabaseError) as e:
            print(f"gate: attestation for {account}: {e}", flush=True)
            return self.send_json(503, {"message": MESSAGES[503]})

    def crash(self) -> None:
        """POST /api/crash: the size and the account are checked before the body is taken."""
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self.close_connection = True
            return self.send_json(411, {"message": "length required"})
        if not 0 < length <= CRASH_MAX_BODY:
            self.close_connection = True
            return self.send_json(413, {"message": "The crash report is too big."})
        header = self.headers.get("Authorization")
        code = auth.check(header, self.address())
        if code != 200:
            # Read even when refused, so the launcher hears the answer rather than a reset; in
            # pieces and thrown away, so refused requests can't hold 12 MB each in memory.
            left = length
            while left > 0:
                piece = self.rfile.read(min(left, 64 * 1024))
                if not piece:
                    break
                left -= len(piece)
            return self.send_json(code, {"message": MESSAGES[code]})
        body = self.rfile.read(length)
        try:
            data = json.loads(body)
        except ValueError:
            return self.send_json(400, {"message": "bad request"})
        account = credentials(header)[0].upper()
        status_code, message, cid = crashes.accept(account, self.address(), data)
        self.send_json(status_code, {"message": message, "id": cid})

    def log_request(self, code="-", size="-") -> None:
        # An install is thousands of file requests, each one an auth check: only the refusals
        # are worth a line.
        if self.path.startswith("/api/auth") and str(code) == "200":
            return
        super().log_request(code, size)

    def log_message(self, fmt: str, *args) -> None:
        # No passwords or codes reach the log: only the request line (they travel in the body or
        # a header).
        print(f"gate: {self.address()} {fmt % args}", flush=True)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Gate).serve_forever()
