#!/usr/bin/env python3
"""Fails when two custom SQL files claim the same ids.

The files in this folder are applied in filename order, and each clears its own rows before writing
them (DELETE ... WHERE key BETWEEN / IN / =), so a later file whose ids overlap an earlier file's
silently wipes the earlier file's rows (33_furniture.sql and 34_dalaran.sql once deleted the
companion tamers that way). This reads every *.sql here and collects, per (table, key column):

  - the ids each DELETE clears (BETWEEN a AND b, IN (...), = n), and
  - the leading id of every row an INSERT writes (VALUES tuples, INSERT ... SELECT <n>, and the
    `UPDATE aw_tmp_ct SET entry = n` copy idiom, which writes a creature_template row).

A clash is an id an earlier file INSERTS that a later file deletes or writes again: the later file
destroys the earlier one's row. (An earlier file clearing a range that a later file then fills, or
two files clearing the same retired range, is harmless.) A later file that means to replace an
earlier file's rows says so in a comment, which this honours:

  -- id-override: 03_construction.sql fusion_recipe.id

Tables keyed by two columns (vendors, trainers, loot, gossip options) are compared on both. Stock
rows a file only UPDATEs are not claims. Every clash is reported and the script exits 1. Run: python server/sql/custom/azeroth_warfare/check_id_ranges.py
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Key column of each table, when the INSERT's column list is not given.
TABLES_SKIPPED = {"aw_tmp_ct", "map_template"}
# Tables whose rows are keyed by their first two columns: an INSERT claims (first, second), and a
# DELETE of the whole first key is not a claim (it is the file clearing its own sub-rows).
COMPOSITE = {"npc_vendor", "npc_trainer", "gossip_menu_option", "creature_loot_template",
             "gameobject_loot_template", "item_loot_template", "pickpocketing_loot_template",
             "skinning_loot_template", "reference_loot_template", "gossip_scripts", "npc_text_broadcast_text",
             "creature_addon_auras", "conditions"}
OVERRIDE_RE = re.compile(r"--\s*id-override:\s*(\S+)\s+(\w+)\.(\w+)", re.I)

DELETE_RE = re.compile(
    r"DELETE\s+FROM\s+`?(\w+)`?\s+WHERE\s+`?(\w+)`?\s*"
    r"(?:BETWEEN\s+(\d+)\s+AND\s+(\d+)|IN\s*\(([\d\s,]+)\)|=\s*(\d+))\s*;",
    re.I)
INSERT_RE = re.compile(
    r"INSERT\s+(?:IGNORE\s+)?INTO\s+`?(\w+)`?\s*\(([^)]*)\)\s*(VALUES|SELECT)", re.I)
TMP_COPY_RE = re.compile(r"UPDATE\s+`?aw_tmp_ct`?\s+SET\s+`?entry`?\s*=\s*(\d+)", re.I)


def strip_comments(sql: str) -> str:
    out, i, n, q = [], 0, len(sql), None
    while i < n:
        c = sql[i]
        if q:
            out.append(c)
            if c == "\\":
                out.append(sql[i + 1:i + 2]); i += 2; continue
            if c == q:
                if sql[i + 1:i + 2] == q:
                    out.append(q); i += 2; continue
                q = None
            i += 1; continue
        if c in "'\"":
            q = c; out.append(c); i += 1; continue
        if sql.startswith("--", i) or c == "#":
            j = sql.find("\n", i); i = n if j < 0 else j; continue
        if sql.startswith("/*", i):
            j = sql.find("*/", i + 2); i = n if j < 0 else j + 2; continue
        out.append(c); i += 1
    return "".join(out)


def tuples_first_values(sql: str, start: int, count: int = 1):
    """Leading `count` values of each (...) tuple of a VALUES list at `start`, until the ';'."""
    i, n, depth, q, firsts, cur, at_first = start, len(sql), 0, None, [], "", False
    vals = []
    while i < n:
        c = sql[i]
        if q:
            if c == "\\": i += 2; continue
            if c == q:
                if sql[i + 1:i + 2] == q: i += 2; continue
                q = None
            if at_first: cur += c
            i += 1; continue
        if c in "'\"": q = c; i += 1; continue
        if c == "(":
            depth += 1
            if depth == 1: at_first, cur, vals = True, "", []
        elif c == ")":
            depth -= 1
            if depth == 0 and at_first:
                vals.append(cur.strip()); firsts.append(tuple(vals)); at_first = False
        elif c == "," and depth == 1 and at_first:
            vals.append(cur.strip()); cur = ""
            if len(vals) == count: firsts.append(tuple(vals)); at_first = False
        elif c == ";" and depth == 0:
            break
        elif at_first:
            cur += c
        i += 1
    return [t[0] if count == 1 else t for t in firsts]


def claims(path: Path):
    """(inserted, deleted, overrides) of one file: {(table, col): ids}, and {(file, table, col)}."""
    raw = path.read_text(encoding="utf-8", errors="replace")
    overrides = {(f, t.lower(), c.lower()) for f, t, c in OVERRIDE_RE.findall(raw)}
    sql = strip_comments(raw)
    inserted, deleted = defaultdict(set), defaultdict(set)
    for m in DELETE_RE.finditer(sql):
        table, col = m.group(1).lower(), m.group(2).lower()
        if table in TABLES_SKIPPED or table in COMPOSITE: continue
        if m.group(3):
            deleted[(table, col)].update(range(int(m.group(3)), int(m.group(4)) + 1))
        elif m.group(5):
            deleted[(table, col)].update(int(x) for x in m.group(5).replace(" ", "").split(",") if x)
        else:
            deleted[(table, col)].add(int(m.group(6)))
    for m in INSERT_RE.finditer(sql):
        table = m.group(1).lower()
        if table in TABLES_SKIPPED: continue
        cols = [c.strip().strip("`").lower() for c in m.group(2).split(",")]
        n = 2 if table in COMPOSITE else 1
        key = (table, ".".join(cols[:n]))
        if m.group(3).upper() == "VALUES":
            vals = tuples_first_values(sql, m.end(), n)
        else:
            first = re.match(r"\s*(\d+)\s*,\s*(\d+)?", sql[m.end():])
            vals = [] if not first else [first.group(1) if n == 1 else (first.group(1), first.group(2))]
        for v in vals:
            if n == 1 and v.isdigit(): inserted[key].add(int(v))
            elif n == 2 and all(x and x.isdigit() for x in v): inserted[key].add(tuple(map(int, v)))
    for m in TMP_COPY_RE.finditer(sql):
        inserted[("creature_template", "entry")].add(int(m.group(1)))
    return inserted, deleted, overrides


def ranges(ids):
    ids, out = sorted(ids), []
    if ids and isinstance(ids[0], tuple): return ", ".join(map(str, ids))
    for x in ids:
        if out and x == out[-1][1] + 1: out[-1][1] = x
        else: out.append([x, x])
    return ", ".join(f"{a}" if a == b else f"{a}-{b}" for a, b in out)


def find_clashes(folder: Path):
    """{(table, col, earlier, later): ids} for every row a later file destroys."""
    written = defaultdict(dict)  # (table, col) -> id -> file that inserted it
    clashes = defaultdict(set)
    for path in sorted(folder.glob("*.sql")):
        inserted, deleted, overrides = claims(path)
        for key in set(inserted) | set(deleted):
            seen = written[key]
            for i in inserted.get(key, set()) | deleted.get(key, set()):
                prev = seen.get(i)
                if prev and prev != path.name and (prev, key[0], key[1]) not in overrides:
                    clashes[(key[0], key[1], prev, path.name)].add(i)
        for key, ids in inserted.items():
            for i in ids: written[key][i] = path.name
        for key, ids in deleted.items():
            for i in ids - inserted.get(key, set()): written[key].pop(i, None)
    return clashes


def main(folder: Path = HERE) -> int:
    clashes = find_clashes(folder)
    for (table, col, a, b), ids in sorted(clashes.items()):
        print(f"OVERLAP {table}.{col}: {b} destroys rows {a} wrote: {ranges(ids)}")
    if clashes:
        print(f"{len(clashes)} overlapping id range(s); give one side fresh ids "
              "(or mark a deliberate replacement with '-- id-override: <file> <table>.<col>').")
        return 1
    print("No overlapping ids across custom SQL files.")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else HERE))
