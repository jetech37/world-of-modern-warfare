"""Tiny read helper for the community audit: runs queries through the packaged mariadb client
(no Python driver needed) and returns rows as dicts. Read-only by convention."""
import os
import subprocess

ROOT = os.environ.get("AW_ROOT", r"C:\Projects\azeroth-warfare")
CLIENT = os.path.join(ROOT, r"dist\AzerothWarfare\mariadb\bin\mariadb.exe")
CNF = os.path.join(ROOT, r"dist\AzerothWarfare\var\etc\client.cnf")


def _unescape(v):
    if v == "NULL":
        return None
    if "\\" not in v:
        return v
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 1 < len(v):
            n = v[i + 1]
            out.append({"t": "\t", "n": "\n", "\\": "\\", "0": "\0"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _args(db, port):
    if port:  # a scratch copy of their world DB (skip-grant-tables, no password)
        return [CLIENT, "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--skip-ssl", "-B", db]
    return [CLIENT, f"--defaults-extra-file={CNF}", "-B", db]


def query(sql, db="mangos", port=None):
    """Rows of the last result set in `sql` as a list of dicts (strings / None)."""
    p = subprocess.run(_args(db, port),
                       input=sql.encode("utf-8"), capture_output=True)
    if p.returncode:
        raise RuntimeError(p.stderr.decode("utf-8", "replace"))
    lines = p.stdout.decode("utf-8", "replace").split("\n")
    lines = [l for l in lines if l != ""]
    if not lines:
        return []
    head = lines[0].split("\t")
    return [dict(zip(head, (_unescape(x) for x in l.split("\t")))) for l in lines[1:]]


def run(sql, db="mangos"):
    """Runs a script; returns (returncode, stdout, stderr)."""
    p = subprocess.run([CLIENT, f"--defaults-extra-file={CNF}", "-B", db],
                       input=sql.encode("utf-8"), capture_output=True)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
