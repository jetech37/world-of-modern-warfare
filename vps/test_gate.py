"""Tests for the gate (vps/gate.py): the SRP6 password check, HTTP Basic, the download check's
cache and rate limit, crash reports, sign-up (open, or with single-use invite codes; the address and daily limits;
the legal-access attestation; against SQLite running the gate's own SQL), the attestation of older
accounts, and the HTTP endpoints. Run from the repository root:

    python -m unittest vps/test_gate.py
"""
import base64
import gzip
import http.client
import json
import os
import shutil
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate  # noqa: E402

# Made by the server's own aw-account (server/src/realmd/aw_account.cpp, the vmangos SRP6), e.g.
#   aw-account Tester secret12  ->  v, s as it writes them into realmd.account
VECTORS = [
    ("Tester", "secret12",
     "0AF2ABB9C516F43CF3D11CD9806DDC9ABDCCC827D1ADCF782AC429E341E99889",
     "F8EF38618F709520AAFDC23C58AF79F2B8814D65758DCA7A3E4A87E21C8E9B0B"),
    ("ab0", "p@ss:w0rd!",
     "04063083F2F3BDF4CFCBB0E5907588C5AD33595DFCB141EDA7818C421CFBE655",
     "BD70112B404CCC77BE3F2FC7104D46B1D0A953E00FE10C829BF5C7BF9B3FF105"),
    ("JUSTIN", "hunter22",
     "349B3503BDB9C7A03F25CB6FC8733F0C9494975859FD5268EFF1C200DAF8018D",
     "987D5B7C285B1B33DC3266C5E60BFE180924F54C11187B61D8DA9E37310B11FF"),
    # A verifier with a leading zero byte: the server writes it 62 hex digits long.
    ("Short", "pw1234",
     "C723DDCB4AD1A04AB5B6405B5B2BB93515E522440B59EF45EB00BB16872754",
     "E76CBE94AC62B635EF1725D36569B914511467104FD404FA5E231F34C0B67A3F"),
]


def basic(name: str, password: str) -> str:
    return "Basic " + base64.b64encode(f"{name}:{password}".encode()).decode()


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


class Accounts:
    """realmd.account as the gate sees it, counting lookups."""

    def __init__(self):
        self.rows = {name.upper(): (s, v) for name, _, v, s in VECTORS}
        self.calls = 0
        self.down = False

    def __call__(self, name):
        self.calls += 1
        if self.down:
            raise gate.DatabaseDown("down")
        return self.rows.get(name.upper())


ATTEST = {"attest": True, "attestation_version": gate.ATTESTATION_VERSION}


class Registrar(gate.Registrar):
    """The gate's, with the legal-access statement confirmed unless a test says otherwise."""

    def register(self, *args, attested=gate.ATTESTATION_VERSION, **kwargs):
        return super().register(*args, attested=attested, **kwargs)


class Addresses(unittest.TestCase):
    def test_network_of(self):
        self.assertEqual(gate.network_of("1.2.3.4"), "1.2.3.4")
        self.assertEqual(gate.network_of("2001:db8:1:2:3:4:5:6"), "2001:db8:1:2::/64")
        self.assertEqual(gate.network_of("2001:db8:1:2::9"), "2001:db8:1:2::/64")
        self.assertEqual(gate.network_of("::ffff:1.2.3.4"), "1.2.3.4")
        self.assertEqual(gate.network_of("not an address"), "not an address")
        self.assertEqual(gate.network_of("*"), "*")

    def test_body_length(self):
        self.assertEqual(gate.body_length({"Content-Length": "10"}, 1024), 10)
        self.assertEqual(gate.body_length({"Content-Length": "99999"}, 1024), 1024)
        self.assertEqual(gate.body_length({"Content-Length": "-1"}, 1024), 0)
        self.assertEqual(gate.body_length({"Content-Length": "x"}, 1024), 0)
        self.assertEqual(gate.body_length({}, 1024), 0)


class Srp(unittest.TestCase):
    def test_matches_aw_account(self):
        for name, password, v, s in VECTORS:
            self.assertEqual(gate.verifier(name, password, s), int(v, 16), name)
            self.assertTrue(gate.password_ok(name, password, s, v), name)

    def test_any_case_like_the_server(self):
        name, password, v, s = VECTORS[0]
        self.assertTrue(gate.password_ok(name.lower(), password.upper(), s, v))

    def test_wrong_password_or_name(self):
        name, password, v, s = VECTORS[0]
        self.assertFalse(gate.password_ok(name, password + "x", s, v))
        self.assertFalse(gate.password_ok("Other", password, s, v))
        self.assertFalse(gate.password_ok(name, password, s, "zz"))

    def test_salt_with_leading_zeros(self):
        # The server's BigNumber drops leading zero bytes before hashing the salt.
        s = "00" + "AB" * 31
        self.assertEqual(gate.verifier("A", "b", s), gate.verifier("A", "b", s[2:]))


class Basic(unittest.TestCase):
    def test_parses(self):
        self.assertEqual(gate.credentials(basic("Tester", "a:b c")), ("Tester", "a:b c"))
        self.assertEqual(gate.credentials("basic " + base64.b64encode(b"x:y").decode()), ("x", "y"))

    def test_rejects(self):
        self.assertIsNone(gate.credentials("Bearer abc"))
        self.assertIsNone(gate.credentials("Basic !!!"))
        self.assertIsNone(gate.credentials("Basic " + base64.b64encode(b"nocolon").decode()))


class AuthCheck(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.db = Accounts()
        self.auth = gate.Auth(lookup=self.db, clock=self.clock, ttl=600, max_failures=3, window=900)
        name, password, _, _ = VECTORS[0]
        self.good = basic(name, password)
        self.bad = basic(name, "wrong1")

    def test_good_and_bad(self):
        self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 200)
        self.assertEqual(self.auth.check(self.bad, "1.1.1.1"), 401)
        self.assertEqual(self.auth.check(basic("Nobody", "secret12"), "1.1.1.1"), 401)
        self.assertEqual(self.auth.check("Basic garbage", "1.1.1.1"), 401)

    def test_no_header_asks_without_counting(self):
        for _ in range(10):
            self.assertEqual(self.auth.check(None, "1.1.1.1"), 401)
        self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 200)

    def test_success_is_cached(self):
        for _ in range(1000):
            self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 200)
        self.assertEqual(self.db.calls, 1)
        # The cache is per name and password: another password is checked.
        self.assertEqual(self.auth.check(self.bad, "1.1.1.1"), 401)
        self.assertEqual(self.db.calls, 2)

    def test_cache_expires(self):
        self.auth.check(self.good, "1.1.1.1")
        self.clock.t += 599
        self.auth.check(self.good, "1.1.1.1")
        self.assertEqual(self.db.calls, 1)
        self.clock.t += 2
        self.auth.check(self.good, "1.1.1.1")
        self.assertEqual(self.db.calls, 2)

    def test_cache_holds_no_password(self):
        self.auth.check(self.good, "1.1.1.1")
        for key in self.auth.good:
            self.assertNotIn(b"SECRET12", key)
            self.assertNotIn(b"secret12", key)

    def test_rate_limit_per_address(self):
        for _ in range(3):
            self.assertEqual(self.auth.check(self.bad, "6.6.6.6"), 401)
        # Blocked, even with the right password (no more guesses get checked) ...
        self.assertEqual(self.auth.check(self.good, "6.6.6.6"), 429)
        calls = self.db.calls
        self.assertEqual(self.auth.check(self.bad, "6.6.6.6"), 429)
        self.assertEqual(self.db.calls, calls)
        # ... only that address ...
        self.assertEqual(self.auth.check(self.good, "2.2.2.2"), 200)
        # ... and only for a while.
        self.clock.t += 901
        self.assertEqual(self.auth.check(self.good, "6.6.6.6"), 200)

    def test_ipv6_wrong_tries_count_by_the_64(self):
        # One machine has a whole /64: a fresh address per guess is the same guesser.
        for i in range(3):
            self.assertEqual(self.auth.check(self.bad, f"2001:db8:1:2::{i + 1:x}"), 401)
        self.assertEqual(self.auth.check(self.bad, "2001:db8:1:2:ffff::9"), 429)
        # Another /64 isn't held up.
        self.assertEqual(self.auth.check(self.good, "2001:db8:1:3::1"), 200)

    def test_cached_success_passes_a_blocked_address(self):
        # A player behind the same address as a guesser keeps downloading.
        self.assertEqual(self.auth.check(self.good, "6.6.6.6"), 200)
        for _ in range(3):
            self.auth.check(self.bad, "6.6.6.6")
        self.assertEqual(self.auth.check(self.good, "6.6.6.6"), 200)

    def test_database_down_is_not_a_wrong_try(self):
        self.db.down = True
        for _ in range(5):
            self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 503)
        self.db.down = False
        self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 200)

    def test_unsafe_names_never_reach_the_database(self):
        self.assertEqual(self.auth.check(basic("a' OR '1'='1", "x"), "1.1.1.1"), 401)
        self.assertEqual(self.auth.check(basic("Tester", "x" * 17), "1.1.1.1"), 401)
        self.assertEqual(self.db.calls, 0)


class AccountGuardCheck(unittest.TestCase):
    """The soft per-account limit (security review 2026-10, #33): many addresses guessing one
    account get a few tries each, and the player is never locked out by it."""

    def setUp(self):
        self.clock = Clock()
        self.db = Accounts()
        guard = gate.AccountGuard(attack=5, per_address=2, window=900, known_ttl=3600)
        self.auth = gate.Auth(lookup=self.db, clock=self.clock, ttl=600, max_failures=10, window=900,
                              guard=guard)
        name, password, _, _ = VECTORS[0]
        self.name = name
        self.good = basic(name, password)
        self.bad = basic(name, "wrong1")

    def guess_from_many(self, n, start=0):
        for i in range(n):
            self.auth.check(self.bad, f"9.9.{(start + i) // 250}.{(start + i) % 250 + 1}")

    def test_quiet_account_keeps_the_per_address_limit(self):
        # Under the account's threshold, an address has its own ten tries.
        for _ in range(4):
            self.assertEqual(self.auth.check(self.bad, "1.1.1.1"), 401)
        self.assertFalse(self.auth.guard.under_attack(self.name, self.clock()))
        self.assertEqual(self.auth.check(self.good, "1.1.1.1"), 200)

    def test_distributed_guessing_gets_few_tries_per_address(self):
        self.guess_from_many(5)
        self.assertTrue(self.auth.guard.under_attack(self.name, self.clock()))
        # A fresh address: two wrong tries, then turned away without a check.
        self.assertEqual(self.auth.check(self.bad, "7.7.7.7"), 401)
        self.assertEqual(self.auth.check(self.bad, "7.7.7.7"), 401)
        calls = self.db.calls
        self.assertEqual(self.auth.check(self.bad, "7.7.7.7"), 429)
        self.assertEqual(self.db.calls, calls)
        # One /64 is one address.
        self.assertEqual(self.auth.check(self.bad, "2001:db8:5:6::1"), 401)
        self.assertEqual(self.auth.check(self.bad, "2001:db8:5:6::2"), 401)
        self.assertEqual(self.auth.check(self.bad, "2001:db8:5:6::3"), 429)
        # Other accounts are not touched.
        other = basic(VECTORS[1][0], VECTORS[1][1])
        self.assertEqual(self.auth.check(other, "7.7.7.7"), 200)
        # It passes with the window.
        self.clock.t += 901
        self.assertFalse(self.auth.guard.under_attack(self.name, self.clock()))
        self.assertEqual(self.auth.check(self.bad, "7.7.7.7"), 401)

    def test_the_player_is_never_locked_out(self):
        # The player signed in from home before the attack.
        self.assertEqual(self.auth.check(self.good, "3.3.3.3"), 200)
        self.guess_from_many(500)
        self.assertTrue(self.auth.guard.under_attack(self.name, self.clock()))
        # The cache ran out: the right password from home is still checked and let in, even
        # after a typo or two from home.
        self.clock.t += 601
        self.auth.check(self.bad, "3.3.3.3")
        self.auth.check(self.bad, "3.3.3.3")
        self.auth.check(self.bad, "3.3.3.3")
        self.assertEqual(self.auth.check(self.good, "3.3.3.3"), 200)
        # From a new address the player has its own tries, untouched by the guessers'.
        self.assertEqual(self.auth.check(self.good, "4.4.4.4"), 200)

    def test_known_addresses_lapse(self):
        self.assertEqual(self.auth.check(self.good, "3.3.3.3"), 200)
        self.clock.t += 3601
        self.guess_from_many(5)
        for _ in range(2):
            self.auth.check(self.bad, "3.3.3.3")
        self.assertEqual(self.auth.check(self.good, "3.3.3.3"), 429)

    def test_names_count_in_any_case(self):
        for i in range(5):
            self.auth.check(basic(self.name.lower() if i % 2 else self.name.upper(), "wrong1"), f"8.8.8.{i + 1}")
        self.assertTrue(self.auth.guard.under_attack(self.name, self.clock()))

    def test_unsafe_names_are_not_counted(self):
        for i in range(10):
            self.auth.check(basic("a' OR '1'='1", "x"), f"8.8.8.{i + 1}")
        self.assertEqual(self.auth.guard.wrong.events, {})


class Endpoint(unittest.TestCase):
    """The gate over HTTP, as Caddy's forward_auth calls it."""

    @classmethod
    def setUpClass(cls):
        cls.saved = gate.auth
        gate.auth = gate.Auth(lookup=Accounts(), max_failures=3)
        gate.Gate.log_message = lambda *a: None
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), gate.Gate)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        gate.auth = cls.saved

    def get(self, path, headers=None):
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        c.request("GET", path, headers=headers or {})
        r = c.getresponse()
        r.read()
        c.close()
        return r

    def test_ok(self):
        name, password, _, _ = VECTORS[1]
        r = self.get("/api/auth", {"Authorization": basic(name, password), "X-Forwarded-For": "9.9.9.1"})
        self.assertEqual(r.status, 200)

    def test_401_asks_the_browser(self):
        r = self.get("/api/auth", {"X-Forwarded-For": "9.9.9.2"})
        self.assertEqual(r.status, 401)
        self.assertIn("Basic", r.getheader("WWW-Authenticate"))

    def test_429_after_wrong_tries_by_forwarded_address(self):
        bad = {"Authorization": basic("Tester", "nope99"), "X-Forwarded-For": "spoofed, 9.9.9.3"}
        for _ in range(3):
            self.assertEqual(self.get("/api/auth", bad).status, 401)
        r = self.get("/api/auth", bad)
        self.assertEqual(r.status, 429)
        self.assertEqual(r.getheader("Retry-After"), "900")
        # Another address isn't held up.
        other = dict(bad, **{"X-Forwarded-For": "spoofed, 9.9.9.4"})
        self.assertEqual(self.get("/api/auth", other).status, 401)

    def post(self, path, data, addr):
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        c.request("POST", path, body=json.dumps(data).encode(),
                  headers={"Content-Type": "application/json", "X-Forwarded-For": addr})
        r = c.getresponse()
        body = json.loads(r.read() or b"{}")
        c.close()
        return r.status, body

    def test_sign_up_over_http(self):
        saved = gate.registrar
        database = Realmd()
        database.add_code("ABCDEFGHJKMN")
        gate.registrar = gate.Registrar(database=database)
        try:
            data = {"code": "abcd-efgh-jkmn", "name": "Newbie", "password": "Secret12", **ATTEST}
            self.assertEqual(self.post("/api/register", data, "9.9.9.5"),
                             (201, {"message": "Account NEWBIE made."}))
            status, body = self.post("/api/register", dict(data, name="Other"), "9.9.9.5")
            self.assertEqual(status, 403)
            self.assertIn("already been used", body["message"])
            self.assertEqual(self.post("/api/register", {"name": "x"}, "9.9.9.5")[0], 400)
        finally:
            gate.registrar = saved

    def test_open_sign_up_over_http(self):
        saved = gate.registrar
        gate.registrar = gate.Registrar(database=Realmd(), invite_required=False)
        try:
            data = {"name": "Opener", "password": "Secret12", **ATTEST}
            self.assertEqual(self.post("/api/signup", data, "9.9.9.6"),
                             (201, {"message": "Account OPENER made."}))
            status, body = self.post("/api/signup", data, "9.9.9.6")
            self.assertEqual((status, body["message"]), (409, "That account name is taken."))
            # The honeypot: a field no person sees.
            status, _ = self.post("/api/signup", dict(data, name="Botty", website="x"), "9.9.9.6")
            self.assertEqual(status, 400)
            self.assertEqual(self.post("/api/signup", ["not", "an", "object"], "9.9.9.6")[0], 400)
        finally:
            gate.registrar = saved

    def test_sign_up_needs_the_attestation(self):
        saved = gate.registrar
        database = Realmd()
        gate.registrar = gate.Registrar(database=database, invite_required=False)
        try:
            data = {"name": "Owner1", "password": "Secret12"}
            for body in (data, dict(data, attest=False, attestation_version=1),
                         dict(data, attest="yes", attestation_version=1), dict(data, attest=True),
                         dict(data, attest=True, attestation_version=gate.ATTESTATION_VERSION + 1),
                         dict(data, attest=True, attestation_version=True)):
                status, reply = self.post("/api/signup", body, "9.9.9.7")
                self.assertEqual(status, 400, body)
                self.assertIn(gate.ATTESTATION_TEXT, reply["message"])
            self.assertIsNone(database.row("SELECT id FROM account"))
            self.assertEqual(self.post("/api/signup", dict(data, **ATTEST), "9.9.9.7")[0], 201)
            row = database.row("SELECT t.account_id, t.version, t.ip, t.source, t.attested_at "
                               "FROM aw_attestation t JOIN account a ON a.id = t.account_id "
                               "WHERE a.username = 'OWNER1'")
            self.assertEqual(row[:4], (1, gate.ATTESTATION_VERSION, "9.9.9.7", "signup"))
            self.assertTrue(row[4])
        finally:
            gate.registrar = saved

    def test_a_negative_length_is_answered_not_waited_on(self):
        # A negative Content-Length once read until the client hung up (a thread held for good).
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        c.putrequest("POST", "/api/signup")
        c.putheader("Content-Length", "-1")
        c.putheader("X-Forwarded-For", "9.9.9.9")
        c.endheaders()
        r = c.getresponse()
        self.assertEqual(r.status, 400)
        r.read()
        c.close()

    def test_signup_info(self):
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        c.request("GET", "/api/signup-info")
        r = c.getresponse()
        body = json.loads(r.read())
        c.close()
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("Access-Control-Allow-Origin"), "*")
        self.assertEqual(body["invite_required"], gate.INVITE_REQUIRED)
        self.assertTrue(body["open"])
        self.assertIn("3-16", body["name_rule"])
        # The statement, word for word.
        self.assertEqual(body["attestation"], {
            "text": "I own and have legal access to World of Warcraft and Call of Duty: "
                    "Modern Warfare 2 (2009).",
            "version": 1})

    def test_releases_are_not_served(self):
        r = self.get("/api/releases")
        self.assertEqual(r.status, 404)


class Realmd:
    """realmd's account, realmlist, realmcharacters, aw_invite, ip_banned and aw_attestation in
    SQLite, running the gate's own SQL (MariaDB's few dialect words swapped): one statement batch
    at a time, like one mariadb client call, which stops at a failing statement and rolls back
    what its open transaction did."""

    def __init__(self):
        self.conn = sqlite3.connect(":memory:", check_same_thread=False)
        self.lock = threading.Lock()
        self.conn.executescript("""
            CREATE TABLE account (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE,
                                  gmlevel INTEGER NOT NULL DEFAULT 0, v TEXT, s TEXT, joindate TEXT);
            CREATE TABLE realmlist (id INTEGER PRIMARY KEY);
            CREATE TABLE realmcharacters (realmid INTEGER, acctid INTEGER, numchars INTEGER,
                                          PRIMARY KEY (realmid, acctid));
            CREATE TABLE aw_invite (code TEXT PRIMARY KEY, created_at TEXT DEFAULT CURRENT_TIMESTAMP,
                                    created_by TEXT NOT NULL DEFAULT 'admin', note TEXT NOT NULL DEFAULT '',
                                    used_by_account TEXT NULL, used_at TEXT NULL,
                                    revoked INTEGER NOT NULL DEFAULT 0);
            CREATE TABLE ip_banned (ip TEXT PRIMARY KEY, bandate INTEGER NOT NULL,
                                    unbandate INTEGER NOT NULL, bannedby TEXT, banreason TEXT);
            CREATE TABLE aw_attestation (id INTEGER PRIMARY KEY AUTOINCREMENT,
                                         account_id INTEGER NOT NULL, version INTEGER NOT NULL,
                                         attested_at TEXT NOT NULL, ip TEXT NOT NULL DEFAULT '',
                                         source TEXT NOT NULL DEFAULT '',
                                         UNIQUE (account_id, version));
            INSERT INTO realmlist (id) VALUES (1);
        """)
        self.statements = 0

    def add_code(self, code, revoked=0):
        with self.lock:
            self.conn.execute("INSERT INTO aw_invite (code, revoked) VALUES (?, ?)", (code, revoked))

    def row(self, sql):
        with self.lock:
            return self.conn.execute(sql).fetchone()

    def query(self, sql):
        sql = (sql.replace("NOW()", "CURRENT_TIMESTAMP").replace("ROW_COUNT()", "changes()")
               .replace("INSERT IGNORE", "INSERT OR IGNORE")
               .replace("UNIX_TIMESTAMP()", "CAST(strftime('%s', 'now') AS INTEGER)")
               .replace("UTC_TIMESTAMP()", "CURRENT_TIMESTAMP")
               .replace("START TRANSACTION", "BEGIN"))
        with self.lock:
            rows = []
            for statement in filter(str.strip, sql.split(";")):
                self.statements += 1
                time.sleep(0)  # let racing threads interleave between batches
                try:
                    cur = self.conn.execute(statement)
                except sqlite3.IntegrityError as e:
                    self.conn.rollback()
                    raise gate.DatabaseError(f"ERROR 1062 (23000): Duplicate entry ({e})")
                except sqlite3.OperationalError as e:
                    self.conn.rollback()
                    raise gate.DatabaseError(f"ERROR 1146 (42S02): {e}")
                rows = [["" if c is None else str(c) for c in r] for r in cur.fetchall()]
            if self.conn.in_transaction:
                self.conn.commit()
            return rows


class Register(unittest.TestCase):
    CODE = "K7QMX3PAW9TR"

    def setUp(self):
        self.db = Realmd()
        self.db.add_code(self.CODE)
        self.reg = Registrar(database=self.db, limit=1000, per_ip=1000, per_day=0)

    def used_by(self, code):
        return self.db.row(f"SELECT used_by_account FROM aw_invite WHERE code = '{code}'")[0] or ""

    def test_makes_a_working_srp_account(self):
        self.assertEqual(self.reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1")[0], 201)
        s, v = self.db.row("SELECT s, v FROM account WHERE username = 'NEWBIE'")
        # The same check the login server makes (and the download check), in any case.
        self.assertTrue(gate.password_ok("newbie", "SECRET12", s, v))
        self.assertFalse(gate.password_ok("newbie", "Secret13", s, v))
        self.assertEqual(len(s), 64)  # 32 bytes, the top bit set, as aw-account's salt
        self.assertGreaterEqual(int(s[0], 16), 8)
        self.assertEqual(self.db.row("SELECT COUNT(*) FROM realmcharacters")[0], 1)
        self.assertEqual(self.used_by(self.CODE), "NEWBIE")
        # The downloads take it.
        saved = gate.db
        gate.db = self.db
        try:
            self.assertEqual(gate.Auth().check(basic("Newbie", "secret12"), "1.1.1.1"), 200)
        finally:
            gate.db = saved

    def test_verifier_hex_like_aw_account(self):
        # v and s as the server writes them parse back the same, whatever their leading bytes.
        for _, _, v, s in VECTORS:
            self.assertEqual(gate.bn_hex(int(v, 16)), v)
            self.assertEqual(gate.bn_hex(int(s, 16)), s)

    def test_a_code_works_once(self):
        self.assertEqual(self.reg.register(self.CODE, "First1", "Secret12", "1.1.1.1")[0], 201)
        code, message = self.reg.register(self.CODE, "Second2", "Secret12", "1.1.1.1")
        self.assertEqual(code, 403)
        self.assertIn("already been used", message)
        self.assertIsNone(self.db.row("SELECT id FROM account WHERE username = 'SECOND2'"))

    def test_codes_as_typed(self):
        self.assertEqual(self.reg.register("k7qm-x3pa w9tr", "Newbie", "Secret12", "1.1.1.1")[0], 201)

    def test_unknown_code(self):
        code, message = self.reg.register("ABCDEFGHJKMN", "Newbie", "Secret12", "1.1.1.1")
        self.assertEqual((code, message), (403, "That invite code isn't right."))
        self.assertEqual(self.reg.register("not a code!", "Newbie", "Secret12", "1.1.1.1")[0], 403)
        self.assertIsNone(self.db.row("SELECT id FROM account"))

    def test_revoked_code(self):
        self.db.add_code("ABCDEFGHJKMN", revoked=1)
        code, message = self.reg.register("ABCDEFGHJKMN", "Newbie", "Secret12", "1.1.1.1")
        self.assertEqual(code, 403)
        self.assertIn("cancelled", message)
        self.assertIsNone(self.db.row("SELECT id FROM account"))

    def test_bad_input_or_taken_name_keeps_the_code(self):
        self.db.query("INSERT INTO account (username, v, s) VALUES ('TAKEN', '1', '1')")
        self.assertEqual(self.reg.register(self.CODE, "Taken", "Secret12", "1.1.1.1")[0], 409)
        self.assertEqual(self.reg.register(self.CODE, "a b", "Secret12", "1.1.1.1")[0], 400)
        self.assertEqual(self.reg.register(self.CODE, "Newbie", "short", "1.1.1.1")[0], 400)
        self.assertEqual(self.reg.register(self.CODE, "Newbie", "has space", "1.1.1.1")[0], 400)
        self.assertEqual(self.reg.register(self.CODE, "a'--", "Secret12", "1.1.1.1")[0], 400)
        self.assertEqual(self.used_by(self.CODE), "")
        self.assertEqual(self.reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1")[0], 201)

    def race(self, attempts):
        """Run register(code, name) attempts at once; their status codes, in order."""
        start = threading.Barrier(len(attempts))
        results = [None] * len(attempts)

        def go(i, code, name):
            start.wait()
            results[i] = self.reg.register(code, name, "Secret12", f"10.0.0.{i}")[0]

        threads = [threading.Thread(target=go, args=(i, c, n)) for i, (c, n) in enumerate(attempts)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        return results

    def test_concurrent_uses_of_one_code(self):
        for _ in range(5):
            self.setUp()
            results = self.race([(self.CODE, f"Racer{i}") for i in range(16)])
            self.assertEqual(results.count(201), 1, results)
            self.assertEqual(results.count(403), 15, results)
            self.assertEqual(self.db.row("SELECT COUNT(*) FROM account")[0], 1)
            winner = self.db.row("SELECT username FROM account")[0]
            self.assertEqual(self.used_by(self.CODE), winner)

    def test_concurrent_same_name_gives_the_losers_code_back(self):
        self.db.add_code("ABCDEFGHJKMN")
        for _ in range(5):
            results = self.race([(self.CODE, "Same"), ("ABCDEFGHJKMN", "Same")])
            self.assertEqual(sorted(results), [201, 409])
            self.assertEqual(self.db.row("SELECT COUNT(*) FROM aw_invite WHERE used_by_account IS NOT NULL")[0], 1)
            self.setUp()
            self.db.add_code("ABCDEFGHJKMN")

    def test_rate_limit_per_address(self):
        clock = Clock()
        reg = Registrar(database=self.db, clock=clock, limit=3, window=3600)
        for _ in range(3):
            self.assertEqual(reg.register("ABCDEFGHJKMN", "Newbie", "Secret12", "6.6.6.6")[0], 403)
        # Even a good code waits: guesses can't go faster than the limit.
        self.assertEqual(reg.register(self.CODE, "Newbie", "Secret12", "6.6.6.6")[0], 429)
        self.assertEqual(self.used_by(self.CODE), "")
        self.assertEqual(reg.register(self.CODE, "Newbie", "Secret12", "7.7.7.7")[0], 201)
        clock.t += 3601
        self.assertEqual(reg.register("ABCDEFGHJKMN", "Other1", "Secret12", "6.6.6.6")[0], 403)

    def test_open_sign_up_needs_no_code(self):
        self.assertEqual(self.reg.register("", "Newbie", "Secret12", "1.1.1.1"), (201, "Account NEWBIE made."))
        s, v = self.db.row("SELECT s, v FROM account WHERE username = 'NEWBIE'")
        self.assertTrue(gate.password_ok("Newbie", "secret12", s, v))
        self.assertEqual(self.db.row("SELECT COUNT(*) FROM realmcharacters")[0], 1)
        # No code was touched.
        self.assertEqual(self.used_by(self.CODE), "")

    def test_open_sign_up_still_takes_and_records_a_code(self):
        self.assertEqual(self.reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1")[0], 201)
        self.assertEqual(self.used_by(self.CODE), "NEWBIE")
        # A code that is given must be good, even when none is needed.
        code, message = self.reg.register(self.CODE, "Other1", "Secret12", "1.1.1.1")
        self.assertEqual(code, 403)
        self.assertIn("already been used", message)

    def test_invite_required(self):
        reg = Registrar(database=self.db, limit=1000, invite_required=True)
        code, message = reg.register("", "Newbie", "Secret12", "1.1.1.1")
        self.assertEqual(code, 403)
        self.assertIn("need an invite code", message)
        self.assertIsNone(self.db.row("SELECT id FROM account"))
        self.assertEqual(reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1")[0], 201)

    def test_name_and_password_rules(self):
        for name, password, says in [
            ("ab", "Secret12", "3-16 letters or digits"),
            ("seventeenletters1", "Secret12", "3-16 letters or digits"),
            ("Admin", "Secret12", "reserved"),
            ("superadmin7", "Secret12", "reserved"),
            ("Moderator", "Secret12", "reserved"),
            ("Newbie", "newbie", "be the account name"),
            ("Newbie", "sécret12", "6-16 letters"),
            ("Newbie", "x" * 17, "6-16 letters"),
        ]:
            code, message = self.reg.register("", name, password, "1.1.1.1")
            self.assertEqual(code, 400, name)
            self.assertIn(says, message, name)
        self.assertIsNone(self.db.row("SELECT id FROM account"))

    def test_honeypot(self):
        self.assertEqual(self.reg.register("", "Newbie", "Secret12", "1.1.1.1", honeypot="http://x"),
                         (400, "bad request"))
        self.assertIsNone(self.db.row("SELECT id FROM account"))

    def test_accounts_per_address(self):
        clock = Clock()
        reg = Registrar(database=self.db, clock=clock, limit=1000, per_ip=3,
                             per_ip_window=24 * 3600, per_day=0)
        for i in range(3):
            self.assertEqual(reg.register("", f"Alt{i}x", "Secret12", "6.6.6.6")[0], 201)
        code, message = reg.register("", "Alt3x", "Secret12", "6.6.6.6")
        self.assertEqual(code, 429)
        self.assertIn("tomorrow", message)
        # Another address isn't held up, and a day later this one may again.
        self.assertEqual(reg.register("", "Alt3x", "Secret12", "7.7.7.7")[0], 201)
        clock.t += 24 * 3600 + 1
        self.assertEqual(reg.register("", "Alt4x", "Secret12", "6.6.6.6")[0], 201)

    def test_accounts_per_ipv6_network(self):
        reg = Registrar(database=self.db, limit=1000, per_ip=2, per_day=0)
        self.assertEqual(reg.register("", "Six1x", "Secret12", "2001:db8:aa:bb::1")[0], 201)
        self.assertEqual(reg.register("", "Six2x", "Secret12", "2001:db8:aa:bb::2")[0], 201)
        self.assertEqual(reg.register("", "Six3x", "Secret12", "2001:db8:aa:bb:1234::3")[0], 429)
        self.assertEqual(reg.register("", "Six3x", "Secret12", "2001:db8:aa:bc::1")[0], 201)

    def test_failed_sign_ups_dont_use_up_the_address(self):
        reg = Registrar(database=self.db, limit=1000, per_ip=1, per_day=0)
        self.db.query("INSERT INTO account (username, v, s) VALUES ('TAKEN', '1', '1')")
        for _ in range(3):
            self.assertEqual(reg.register("", "Taken", "Secret12", "6.6.6.6")[0], 409)
            self.assertEqual(reg.register("ABCDEFGHJKMN", "Newbie", "Secret12", "6.6.6.6")[0], 403)
            self.assertEqual(reg.register("", "x", "Secret12", "6.6.6.6")[0], 400)
        self.assertEqual(reg.register("", "Newbie", "Secret12", "6.6.6.6")[0], 201)
        self.assertEqual(reg.register("", "Second", "Secret12", "6.6.6.6")[0], 429)

    def test_racing_sign_ups_from_one_address_keep_the_limit(self):
        reg = Registrar(database=self.db, limit=1000, per_ip=3, per_day=0)
        start = threading.Barrier(12)
        results = []

        def go(i):
            start.wait()
            results.append(reg.register("", f"Racer{i}", "Secret12", "6.6.6.6")[0])

        threads = [threading.Thread(target=go, args=(i,)) for i in range(12)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(sorted(results), [201] * 3 + [429] * 9)
        self.assertEqual(self.db.row("SELECT COUNT(*) FROM account")[0], 3)

    def test_accounts_per_day_for_everyone(self):
        reg = Registrar(database=self.db, limit=1000, per_ip=1000, per_day=2)
        self.assertEqual(reg.register("", "One111", "Secret12", "1.1.1.1")[0], 201)
        self.assertEqual(reg.register("", "Two222", "Secret12", "2.2.2.2")[0], 201)
        code, message = reg.register("", "Three3", "Secret12", "3.3.3.3")
        self.assertEqual(code, 429)
        self.assertIn("tomorrow", message)

    def test_settings(self):
        saved = dict(os.environ)
        try:
            os.environ["AW_X_TEST"] = " 5 "
            self.assertEqual(gate.setting("AW_X_TEST", 1), 5)
            os.environ["AW_X_TEST"] = "lots"
            self.assertEqual(gate.setting("AW_X_TEST", 1), 1)
            del os.environ["AW_X_TEST"]
            self.assertEqual(gate.setting("AW_X_TEST", 7), 7)
        finally:
            os.environ.clear()
            os.environ.update(saved)
        # Open sign-ups unless the server owner says otherwise.
        self.assertFalse(gate.INVITE_REQUIRED)
        self.assertEqual(gate.signup_info()["invite_required"], False)

    def test_banned_addresses_cant_sign_up(self):
        now = int(time.time())
        self.db.query(f"INSERT INTO ip_banned VALUES ('6.6.6.6', {now}, {now}, 'GM', 'perm')")
        self.db.query(f"INSERT INTO ip_banned VALUES ('7.7.7.7', {now - 100}, {now + 3600}, 'GM', 'an hour')")
        self.db.query(f"INSERT INTO ip_banned VALUES ('8.8.8.8', {now - 100}, {now - 10}, 'GM', 'over')")
        for addr in ("6.6.6.6", "7.7.7.7"):
            code, message = self.reg.register("", "Newbie", "Secret12", addr)
            self.assertEqual(code, 403, addr)
            self.assertIn("closed", message)
        self.assertIsNone(self.db.row("SELECT id FROM account"))
        self.assertEqual(self.reg.register("", "Newbie", "Secret12", "8.8.8.8")[0], 201)

    def test_an_unreadable_ban_list_bans_nobody(self):
        class NoGrant(Realmd):
            def query(self, sql):
                if "ip_banned" in sql:
                    raise gate.DatabaseError("ERROR 1142 (42000): SELECT command denied")
                return super().query(sql)

        reg = Registrar(database=NoGrant(), limit=1000)
        self.assertEqual(reg.register("", "Newbie", "Secret12", "6.6.6.6")[0], 201)

    def test_needs_the_attestation(self):
        reg = gate.Registrar(database=self.db, limit=1000, per_ip=1000, per_day=0)
        for attested in (None, 0, gate.ATTESTATION_VERSION + 1):
            code, message = reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1", attested=attested)
            self.assertEqual(code, 400)
            self.assertIn(gate.ATTESTATION_TEXT, message)
        # Nothing made, and the code still good.
        self.assertIsNone(self.db.row("SELECT id FROM account"))
        self.assertEqual(self.used_by(self.CODE), "")
        self.assertEqual(reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1",
                                      attested=gate.ATTESTATION_VERSION)[0], 201)

    def test_the_attestation_is_kept_with_the_account(self):
        self.assertEqual(self.reg.register("", "Newbie", "Secret12", "5.6.7.8")[0], 201)
        self.assertEqual(self.reg.register("", "Second", "Secret12", "not an address")[0], 201)
        rows = self.db.conn.execute(
            "SELECT a.username, t.version, t.ip, t.source FROM aw_attestation t "
            "JOIN account a ON a.id = t.account_id ORDER BY t.id").fetchall()
        self.assertEqual(rows, [("NEWBIE", gate.ATTESTATION_VERSION, "5.6.7.8", "signup"),
                                ("SECOND", gate.ATTESTATION_VERSION, "", "signup")])

    def test_no_account_without_its_attestation(self):
        # A server whose attestation table is missing makes no account (and keeps the code).
        self.db.conn.execute("DROP TABLE aw_attestation")
        code, _ = self.reg.register(self.CODE, "Newbie", "Secret12", "1.1.1.1")
        self.assertEqual(code, 503)
        self.assertIsNone(self.db.row("SELECT id FROM account"))
        self.assertEqual(self.used_by(self.CODE), "")

    def test_database_down(self):
        class Down:
            def query(self, sql):
                raise gate.DatabaseDown("down")

        self.assertEqual(Registrar(database=Down()).register(self.CODE, "Newbie", "Secret12", "1.1.1.1")[0], 503)


class OlderAccounts(unittest.TestCase):
    """An account made before the statement confirms it once (the launcher asks)."""

    def setUp(self):
        self.db = Realmd()
        self.db.query("INSERT INTO account (username, v, s) VALUES ('OLDTIMER', 'v', 's')")
        self.att = gate.Attestations(database=self.db)

    def test_asked_until_recorded_once(self):
        self.assertFalse(self.att.attested("OldTimer"))
        self.att.record("OldTimer", "4.4.4.4")
        self.assertTrue(self.att.attested("oldtimer"))
        self.att.record("OldTimer", "5.5.5.5")  # a repeat keeps the first
        rows = self.db.conn.execute("SELECT account_id, version, ip, source FROM aw_attestation").fetchall()
        self.assertEqual(rows, [(1, gate.ATTESTATION_VERSION, "4.4.4.4", "launcher")])

    def test_another_version_is_asked_again(self):
        self.db.query("INSERT INTO aw_attestation (account_id, version, attested_at) "
                      f"VALUES (1, {gate.ATTESTATION_VERSION - 1}, CURRENT_TIMESTAMP)")
        self.assertFalse(self.att.attested("OldTimer"))

    def test_bad_names_never_reach_the_database(self):
        self.assertFalse(self.att.attested("x'--"))
        with self.assertRaises(ValueError):
            self.att.record("x'--", "1.1.1.1")
        with self.assertRaises(ValueError):
            self.att.record("OldTimer", "1.1.1.1", source="x'--")


class AttestEndpoint(unittest.TestCase):
    """GET and POST /api/attest over HTTP, as the launcher calls them."""

    @classmethod
    def setUpClass(cls):
        cls.saved = gate.auth, gate.attestations
        cls.db = Realmd()
        for name, _, v, s in VECTORS:
            cls.db.query(f"INSERT INTO account (username, v, s) VALUES ('{name.upper()}', '{v}', '{s}')")
        gate.auth = gate.Auth(lookup=Accounts(), max_failures=50)
        gate.attestations = gate.Attestations(database=cls.db)
        gate.Gate.log_message = lambda *a: None
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), gate.Gate)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        gate.auth, gate.attestations = cls.saved

    def call(self, method, headers, body=None):
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        c.request(method, "/api/attest", body=None if body is None else json.dumps(body).encode(),
                  headers=headers)
        r = c.getresponse()
        data = json.loads(r.read() or b"{}")
        c.close()
        return r.status, data

    def test_ask_then_confirm(self):
        name, password, _, _ = VECTORS[2]
        auth = {"Authorization": basic(name, password), "X-Forwarded-For": "7.7.7.1"}
        status, data = self.call("GET", auth)
        self.assertEqual((status, data["attested"], data["version"]), (200, False, gate.ATTESTATION_VERSION))
        self.assertEqual(data["text"], gate.ATTESTATION_TEXT)
        # Not the current statement: nothing kept.
        self.assertEqual(self.call("POST", auth, {"attest": True, "version": 99})[0], 400)
        self.assertEqual(self.call("POST", auth, {"attest": False, "version": 1})[0], 400)
        self.assertEqual(self.call("POST", auth, ["x"])[0], 400)
        self.assertFalse(self.call("GET", auth)[1]["attested"])
        status, data = self.call("POST", auth, {"attest": True, "version": gate.ATTESTATION_VERSION})
        self.assertEqual((status, data["attested"]), (200, True))
        self.assertTrue(self.call("GET", auth)[1]["attested"])
        row = self.db.row("SELECT t.ip, t.source FROM aw_attestation t JOIN account a "
                          f"ON a.id = t.account_id WHERE a.username = '{name.upper()}'")
        self.assertEqual(row, ("7.7.7.1", "launcher"))

    def test_needs_the_account(self):
        body = {"attest": True, "version": gate.ATTESTATION_VERSION}
        self.assertEqual(self.call("GET", {"X-Forwarded-For": "7.7.7.2"})[0], 401)
        wrong = {"Authorization": basic("Tester", "wrong99"), "X-Forwarded-For": "7.7.7.2"}
        self.assertEqual(self.call("POST", wrong, body)[0], 401)
        self.assertIsNone(self.db.row("SELECT id FROM aw_attestation WHERE source = 'launcher' "
                                      "AND ip = '7.7.7.2'"))


PANIC_REPORT = """World of Modern Warfare crash report
kind:      panic
time:      1800000000 unix, 12.3 s after launch
game:      2026-10-18 12:00
thread:    main
at:        src/weapon.rs:10:5
panic:     index out of bounds: the len is 3 but the index is 7

frames (module+offset; symbolise with the build's PDB):
    0  benilla-mw2.exe+0x1a2b3c
"""

NATIVE_REPORT = """World of Modern Warfare crash report
kind:      native
exception: 0xC0000005 access violation
address:   benilla-mw2.exe+0x10
access:    read of 0x0
"""


def crash_body(**over):
    data = {"kind": "panic", "report": PANIC_REPORT, "game_version": "2026-10-18 12:00",
            "client_sha256": "ab" * 32, "launcher_version": "0.2.0", "crashed_at": int(time.time()) - 60,
            "exit_code": 101}
    data.update(over)
    return data


def gz(data: bytes) -> str:
    return base64.b64encode(gzip.compress(data)).decode()


class CrashSummary(unittest.TestCase):
    def test_panic(self):
        s = gate.summarize("panic", PANIC_REPORT, 101)
        self.assertEqual(s["message"], "index out of bounds: the len is 3 but the index is 7")
        self.assertEqual(s["location"], "src/weapon.rs:10:5")
        self.assertEqual(s["signature"],
                         "panic: index out of bounds: the len is N but the index is N @ src/weapon.rs:10:5")
        # The same crash with other numbers groups with it.
        other = PANIC_REPORT.replace("len is 3 but the index is 7", "len is 0 but the index is 12")
        self.assertEqual(gate.summarize("panic", other, 101)["signature"], s["signature"])

    def test_native(self):
        s = gate.summarize("native", NATIVE_REPORT, 0xC0000005)
        self.assertEqual(s["message"], "0xC0000005 access violation")
        self.assertEqual(s["location"], "benilla-mw2.exe+0x10")
        self.assertEqual(s["signature"], "native: 0xC0000005 access violation @ benilla-mw2.exe+0x10")

    def test_exit_without_report_lines(self):
        s = gate.summarize("exit", "the game exited with code 1 and wrote no crash report\n", 1)
        self.assertEqual(s["signature"], "exit: exited with 1")
        self.assertEqual(gate.exit_meaning(0xC00000FD), "0xC00000FD (stack overflow)")
        self.assertEqual(gate.exit_meaning(-1073741819), "0xC0000005 (access violation)")
        self.assertEqual(gate.exit_meaning(101), "101 (Rust panic)")

    def test_only_the_header_is_read(self):
        text = "x\nkind: panic\n\npanic: not this\n"
        self.assertEqual(gate.report_fields(text), {"kind": "panic"})


class CrashStore(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.clock = Clock()
        self.clock.t = 1_800_000_100.0
        self.crashes = gate.Crashes(root=self.dir, clock=self.clock, per_account=(3, 3600),
                                    per_address=(4, 3600))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def folder(self, cid):
        return os.path.join(self.dir, cid)

    def test_keeps_the_report_and_dump(self):
        dump = b"MDMP" + os.urandom(1000)
        status, _, cid = self.crashes.accept("TESTER", "1.1.1.1", crash_body(dump_gz=gz(dump)))
        self.assertEqual(status, 201)
        self.assertRegex(cid, gate.CRASH_ID)
        meta = json.load(open(os.path.join(self.folder(cid), "meta.json")))
        self.assertEqual(meta["account"], "TESTER")
        self.assertEqual(meta["kind"], "panic")
        self.assertEqual(meta["game_version"], "2026-10-18 12:00")
        self.assertEqual(meta["client_sha256"], "ab" * 32)
        self.assertEqual(meta["exit_code"], 101)
        self.assertEqual(meta["location"], "src/weapon.rs:10:5")
        self.assertNotIn("1.1.1.1", json.dumps(meta))  # no address is kept
        self.assertEqual(open(os.path.join(self.folder(cid), "report.txt"), encoding="utf-8").read(),
                         PANIC_REPORT)
        with gzip.open(os.path.join(self.folder(cid), "dump.dmp.gz")) as f:
            self.assertEqual(f.read(), dump)
        self.assertEqual(self.crashes.stored()[0], 1)

    def test_without_a_dump(self):
        status, _, cid = self.crashes.accept("TESTER", "1.1.1.1", crash_body(kind="native", report=NATIVE_REPORT))
        self.assertEqual(status, 201)
        self.assertEqual(sorted(os.listdir(self.folder(cid))), ["meta.json", "report.txt"])

    def test_bad_reports(self):
        bad = [
            crash_body(kind="other"),
            crash_body(report=5),
            crash_body(exit_code="1"),
            crash_body(exit_code=True),
            crash_body(crashed_at=-1),
            crash_body(client_sha256="../../etc"),
            crash_body(dump_gz="not base64!"),
            crash_body(dump_gz=base64.b64encode(b"MDMP raw, not gzipped").decode()),
            ["not", "a", "dict"],
        ]
        crashes = gate.Crashes(root=self.dir, clock=self.clock, per_account=(100, 3600),
                               per_address=(100, 3600))
        for data in bad:
            self.assertEqual(crashes.accept("TESTER", "1.1.1.1", data)[0], 400, data)
        self.assertEqual(os.listdir(self.dir), [])

    def test_size_limits(self):
        big = crash_body(report="x" * (gate.CRASH_MAX_REPORT + 1))
        self.assertEqual(self.crashes.accept("TESTER", "1.1.1.1", big)[0], 413)
        saved = gate.CRASH_MAX_DUMP
        gate.CRASH_MAX_DUMP = 100
        try:
            status = self.crashes.accept("OTHER", "2.2.2.2", crash_body(dump_gz=gz(os.urandom(500))))[0]
        finally:
            gate.CRASH_MAX_DUMP = saved
        self.assertEqual(status, 413)

    def test_rate_limit_per_account_and_address(self):
        for _ in range(3):
            self.assertEqual(self.crashes.accept("TESTER", "1.1.1.1", crash_body())[0], 201)
        self.assertEqual(self.crashes.accept("TESTER", "9.9.9.9", crash_body())[0], 429)
        # Another account from the same address, up to the address's limit.
        self.assertEqual(self.crashes.accept("OTHER", "1.1.1.1", crash_body())[0], 201)
        self.assertEqual(self.crashes.accept("THIRD", "1.1.1.1", crash_body())[0], 429)
        self.clock.t += 3601
        self.assertEqual(self.crashes.accept("TESTER", "1.1.1.1", crash_body())[0], 201)

    def test_a_full_store_says_so(self):
        crashes = gate.Crashes(root=self.dir, clock=self.clock, max_reports=2)
        self.assertEqual(crashes.accept("A1", "1.1.1.1", crash_body())[0], 201)
        self.clock.t += 1
        self.assertEqual(crashes.accept("A2", "1.1.1.2", crash_body())[0], 201)
        self.assertEqual(crashes.accept("A3", "1.1.1.3", crash_body())[0], 507)
        small = gate.Crashes(root=tempfile.mkdtemp(), clock=self.clock, max_bytes=100)
        try:
            self.assertEqual(small.accept("A1", "1.1.1.1", crash_body())[0], 507)
        finally:
            shutil.rmtree(small.root, ignore_errors=True)


class CrashEndpoint(unittest.TestCase):
    """POST /api/crash over HTTP, as the launcher sends it."""

    @classmethod
    def setUpClass(cls):
        cls.saved = gate.auth, gate.crashes
        cls.dir = tempfile.mkdtemp()
        gate.auth = gate.Auth(lookup=Accounts(), max_failures=50)
        gate.crashes = gate.Crashes(root=cls.dir)
        gate.Gate.log_message = lambda *a: None
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), gate.Gate)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        gate.auth, gate.crashes = cls.saved
        shutil.rmtree(cls.dir, ignore_errors=True)

    def post(self, body: bytes, headers: dict):
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        c.request("POST", "/api/crash", body=body, headers=headers)
        r = c.getresponse()
        data = json.loads(r.read() or b"{}")
        c.close()
        return r.status, data

    def test_a_logged_in_report_is_kept(self):
        name, password, _, _ = VECTORS[0]
        body = json.dumps(crash_body(dump_gz=gz(b"MDMP" * 100))).encode()
        status, data = self.post(body, {"Authorization": basic(name, password),
                                        "Content-Type": "application/json", "X-Forwarded-For": "8.8.8.1"})
        self.assertEqual(status, 201, data)
        meta = json.load(open(os.path.join(self.dir, data["id"], "meta.json")))
        self.assertEqual(meta["account"], "TESTER")  # the login's name, as stored

    def test_needs_the_account(self):
        body = json.dumps(crash_body()).encode()
        self.assertEqual(self.post(body, {"X-Forwarded-For": "8.8.8.2"})[0], 401)
        wrong = {"Authorization": basic("Tester", "wrong99"), "X-Forwarded-For": "8.8.8.2"}
        self.assertEqual(self.post(body, wrong)[0], 401)

    def test_a_refused_big_report_is_still_answered(self):
        body = b"x" * (2 * 1024 * 1024)
        wrong = {"Authorization": basic("Tester", "wrong99"), "X-Forwarded-For": "8.8.8.4"}
        self.assertEqual(self.post(body, wrong)[0], 401)

    def test_too_big_or_bad(self):
        name, password, _, _ = VECTORS[0]
        auth = {"Authorization": basic(name, password), "X-Forwarded-For": "8.8.8.3"}
        c = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        c.putrequest("POST", "/api/crash")
        for k, v in auth.items():
            c.putheader(k, v)
        c.putheader("Content-Length", str(gate.CRASH_MAX_BODY + 1))
        c.endheaders()
        r = c.getresponse()
        self.assertEqual(r.status, 413)
        r.read()
        r.close()
        c.close()
        self.assertEqual(self.post(b"{not json", auth)[0], 400)


if __name__ == "__main__":
    unittest.main()
