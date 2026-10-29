"""World DB import for the community zones: their world database (a vmangos-family schema of an
older revision) to ours, one zone at a time.

    python -I tools/community-merge/db_import.py --port 3399 --theirs <their world db> \
        --ours mangos --areas private-assets/womw-community/areas.tsv --zones 5121 \
        --map design/COMMUNITY_ID_MAP.tsv \
        --sql private-assets/server/sql/custom/azeroth_warfare/80_community_zones.sql \
        --displays private-assets/womw-community/displays.txt

Reads both databases on one local MariaDB (their world schema copied there read-only, ours a
vmangos world with our migrations and custom SQL applied, to find our ids), places their spawns by
the chunk-to-area table `community-merge areas` writes (a spawn belongs to the zone of the terrain
chunk under it), and writes one idempotent SQL file: every row it inserts is first deleted by key,
so it can be applied again after a re-import. Ids of theirs that collide with ours, or that sit in
our reserved custom bands, are renumbered; the old -> new table (`--map`) is read first and
extended, so a re-import keeps every number it gave before.

The SQL holds their content, so it is private (`private-assets/`, never committed); the id map
holds only numbers and lives in the private design notes.
"""

import argparse
import math
import os
import subprocess
import sys
from collections import defaultdict

# Ranges for renumbered ids, per key space: (low, high). Wrath imports use 100000-109999
# (creatures) and 130000-139999 (items) (design/WRATH_IMPORT_PLAN.md).
RANGES = {
    "creature": (110000, 129999),
    "item": (140000, 149999),
    "gameobject": (220000, 229999),
    "quest": (160000, 169999),
    "creature_loot": (110000, 129999),
    "gameobject_loot": (110000, 129999),
    "item_loot": (140000, 149999),
    "skinning_loot": (110000, 129999),
    "pickpocket_loot": (110000, 129999),
    "reference_loot": (700000, 709999),
    "gossip_menu": (62000, 65535),
    "npc_text": (300000, 309999),
    "broadcast_text": (400000, 409999),
    "page_text": (30000, 39999),
    "equipment": (1100000, 1109999),
    "creature_spells": (1100000, 1109999),
    "ai_script": (21000000, 21999999),
    "creature_guid": (12000000, 12999999),
    "spell": (65000, 65535),  # spell ids are smallints in our tables
    "condition": (1500000, 1599999),
    "game_event": (1000, 1099),
    "pool": (60000, 65535),
    "quest_start_script": (22000000, 22999999),
    "quest_end_script": (23000000, 23999999),
    "gossip_script": (24000000, 24999999),
    "movement_script": (25000000, 25999999),
    "event_script": (26000000, 26999999),
    "gameobject_guid": (12000000, 12999999),
}
# Our own custom content and the planned import blocks sit here: an id of theirs inside keeps
# nothing, it is always renumbered.
RESERVED = {k: (90000, 199999) for k in ("creature", "item", "gameobject", "quest")}
# Spells: our own (61000-62999), the Wrath carriers (63000-64999) and the cherry-pick block.
RESERVED["spell"] = (60000, 69999)
# Our columns narrower than theirs: an id above the limit is renumbered (gossip_menu.entry is a
# smallint here, an int in their schema).
LIMIT = {"gossip_menu": 65535, "pool": 65535, "spell": 65535}

def our_script_names(src=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "server", "src")):
    """The script names our core registers (`newscript->Name = "..."` in server/src): one of
    theirs with the same name keeps it, the rest are listed as not ported."""
    import re
    names = set()
    pat = re.compile(r'Name\s*=\s*"([A-Za-z0-9_]+)"')
    for root, _, files in os.walk(src):
        for n in files:
            if n.endswith(".cpp"):
                with open(os.path.join(root, n), encoding="utf-8", errors="replace") as f:
                    names |= set(pat.findall(f.read()))
    return names


CHUNK = 533.3333333 / 16.0
ORIGIN = 32.0 * 533.3333333


def chunk_of(x, y):
    """The global (column, row) of the terrain chunk under world (x, y): column from world y,
    row from world x, as ADT tiles are named (`<map>_<column tile>_<row tile>`)."""
    return int(math.floor((ORIGIN - y) / CHUNK)), int(math.floor((ORIGIN - x) / CHUNK))


class IdMap:
    """old -> new ids per key space, persisted as `kind<TAB>old<TAB>new` lines."""

    def __init__(self, path=None):
        self.path = path
        self.map = defaultdict(dict)
        self.taken = defaultdict(set)  # new ids handed out, per kind
        self.ours = defaultdict(set)   # ids our database already uses, per kind
        if path and os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if line.startswith("#") or not line.strip():
                        continue
                    kind, old, new = line.split("\t")[:3]
                    self.map[kind][int(old)] = int(new)
                    self.taken[kind].add(int(new))

    def get(self, kind, old):
        """The new id for `old` (0 stays 0)."""
        if not old:
            return old
        m = self.map[kind]
        if old in m:
            return m[old]
        lo_r, hi_r = RESERVED.get(kind, (1, 0))
        free = (old not in self.ours[kind] and old not in self.taken[kind] and not lo_r <= old <= hi_r
                and old <= LIMIT.get(kind, 1 << 32))
        if free and not kind.endswith("_guid"):
            new = old
        else:
            lo, hi = RANGES[kind]
            new = lo
            used = self.taken[kind] | self.ours[kind]
            # Next free id above everything already handed out in the range.
            in_range = [v for v in used if lo <= v <= hi]
            if in_range:
                new = max(in_range) + 1
            while new in used:
                new += 1
            if new > hi:
                raise SystemExit(f"{kind}: range {lo}-{hi} is full")
        m[old] = new
        self.taken[kind].add(new)
        return new

    def save(self):
        if not self.path:
            return
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
        with open(self.path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Community-zone import: their id -> our id, per key space (db_import.py).\n")
            f.write("# An id kept as it was is listed too, so a re-import never renumbers it.\n")
            for kind in sorted(self.map):
                for old, new in sorted(self.map[kind].items()):
                    f.write(f"{kind}\t{old}\t{new}\n")


# --- the database -------------------------------------------------------------------------------


class Db:
    def __init__(self, exe, port):
        self.exe, self.port = exe, port
        self.types = {}
        self.ranges = {}

    def query(self, sql):
        out = subprocess.run(
            # The query on stdin: a long IN list is past the Windows command-line limit.
            [self.exe, "--host=127.0.0.1", f"--port={self.port}", "--user=root", "-B"],
            input=sql.encode("utf-8"), capture_output=True, check=True,
        ).stdout.decode("utf-8", "replace")
        lines = out.split("\n")
        if not lines or not lines[0]:
            return []
        cols = lines[0].split("\t")
        rows = []
        for line in lines[1:]:
            if not line:
                continue
            vals = [None if v == "NULL" else unescape(v) for v in line.split("\t")]
            rows.append(dict(zip(cols, vals)))
        return rows

    def columns(self, schema, table):
        key = (schema, table)
        if key not in self.types:
            rows = self.query(
                "select column_name c, data_type t, column_type ct from information_schema.columns "
                f"where table_schema='{schema}' and table_name='{table}' order by ordinal_position")
            self.types[key] = [(r["c"], r["t"]) for r in rows]
            self.ranges[key] = {r["c"]: int_range(r["t"], r["ct"]) for r in rows}
        return self.types[key]


def unescape(v):
    out, i = [], 0
    while i < len(v):
        c = v[i]
        if c == "\\" and i + 1 < len(v):
            n = v[i + 1]
            out.append({"n": "\n", "t": "\t", "0": "\0", "\\": "\\"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


INT_BITS = {"tinyint": 8, "smallint": 16, "mediumint": 24, "int": 32, "bigint": 64}


def int_range(data_type, column_type):
    """(min, max) of an integer column, `None` for any other."""
    bits = INT_BITS.get(data_type)
    if not bits:
        return None
    if "unsigned" in (column_type or ""):
        return (0, (1 << bits) - 1)
    return (-(1 << (bits - 1)), (1 << (bits - 1)) - 1)


def fit(column, value, rng):
    """A value of theirs past our column: a flags column keeps the bits that fit (their extra
    bits mean nothing to our core), anything else is clamped."""
    if "flag" in column.lower() and value >= 0:
        return value & rng[1]
    return min(max(value, rng[0]), rng[1])


NUMERIC = {"tinyint", "smallint", "mediumint", "int", "bigint", "float", "double", "decimal"}


def sql_value(v, t):
    if v is None:
        return "NULL"
    if t in NUMERIC:
        return str(v)
    s = str(v).replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n").replace("\r", "\\r")
    return f"'{s}'"


def ids_sql(ids):
    return ",".join(str(int(i)) for i in sorted(ids)) or "NULL"


# --- conversions --------------------------------------------------------------------------------

# Their unit_flags / type_flags / regeneration / inhabit_type, as vmangos's static flags.
UNIT_FLAG_IMMUNE_TO_PLAYER = 0x100
UNIT_FLAG_IMMUNE_TO_NPC = 0x200
UNIT_FLAG_USE_SWIM_ANIMATION = 0x8000
UNIT_FLAG_NOT_SELECTABLE = 0x2000000
SF_TAMEABLE, SF_IMMUNE_TO_PC, SF_IMMUNE_TO_NPC = 0x10, 0x20, 0x40
SF_UNINTERACTIBLE, SF_NO_AUTOMATIC_REGEN = 0x200, 0x400
SF_AQUATIC, SF_AMPHIBIOUS, SF_CAN_SWIM = 0x40000, 0x80000, 0x10000000


def static_flags(t):
    uf = int(t.get("unit_flags") or 0)
    f = 0
    if uf & UNIT_FLAG_IMMUNE_TO_PLAYER:
        f |= SF_IMMUNE_TO_PC
    if uf & UNIT_FLAG_IMMUNE_TO_NPC:
        f |= SF_IMMUNE_TO_NPC
    if uf & UNIT_FLAG_NOT_SELECTABLE:
        f |= SF_UNINTERACTIBLE
    if uf & UNIT_FLAG_USE_SWIM_ANIMATION:
        f |= SF_CAN_SWIM
    if int(t.get("type_flags") or 0) & 0x1:
        f |= SF_TAMEABLE
    if int(t.get("regeneration") if t.get("regeneration") is not None else 3) == 0:
        f |= SF_NO_AUTOMATIC_REGEN
    inhabit = int(t.get("inhabit_type") or 1)
    if inhabit == 2:
        f |= SF_AQUATIC
    elif inhabit == 3:
        f |= SF_AMPHIBIOUS
    return f


def creature_template(t, stats):
    """Their creature_template row (absolute health, mana, armor and damage) as ours (multipliers
    of the class/level base stats, `creature_classlevelstats`)."""
    # Our creature_classlevelstats holds classes 1, 2 and 8 only: any other class of theirs
    # (0, 4 rogue, ...) becomes a mage (8) when the creature has mana, else a warrior (1).
    cls = int(t.get("unit_class") or 0)
    if cls not in (1, 2, 8):
        cls = 8 if float(t.get("mana_max") or 0) > 0 else 1
    lmax = int(t["level_max"] or 1)
    lmin = int(t["level_min"] or lmax)
    base = stats.get((cls, lmax)) or stats.get((1, lmax)) or {}
    base_min = stats.get((cls, lmin)) or base

    def ratio(v, b):
        v, b = float(v or 0), float(b or 0)
        return round(v / b, 4) if v > 0 and b > 0 else 1.0

    dmg = (float(t["dmg_min"] or 0) + float(t["dmg_max"] or 0)) / 2.0
    melee = (float(base.get("melee_damage") or 0) + float(base_min.get("melee_damage") or 0)) / 2.0
    out = dict(t)
    out.update({
        "patch": 0,
        "unit_class": cls,
        "health_multiplier": ratio(t["health_max"], base.get("health")),
        "mana_multiplier": ratio(t["mana_max"], base.get("mana")),
        "armor_multiplier": ratio(t["armor"], base.get("armor")),
        "damage_multiplier": round(ratio(dmg, melee) * float(t.get("dmg_multiplier") or 1), 4),
        "damage_variance": 0.14,
        "damage_school": t.get("dmg_school") or 0,
        "pet_family": t.get("beast_family") or 0,
        "static_flags1": static_flags(t),
        "static_flags2": 0,
        "totem_spell_id": 0,
    })
    n = 0
    for i in range(1, 5):
        on = int(t.get(f"display_id{i}") or 0) != 0
        out[f"display_scale{i}"] = float(t.get("scale") or 0) if on else 0
        out[f"display_probability{i}"] = 1 if on else 0
        n += on
    out["display_total_probability"] = n
    return out


# --- the import ---------------------------------------------------------------------------------


class Import:
    def __init__(self, db, theirs, ours, ids):
        self.db, self.theirs, self.ours, self.ids = db, theirs, ours, ids
        self.out = []          # SQL statements
        self.report = []
        self.displays = defaultdict(set)
        self.area_rows = []  # area_template rows (community-merge's area_template.tsv)
        self.instance_maps = set()  # their instanced maps, every spawn on which comes in
        self.scripts = []  # (kind, their id, script name): their core's C++ scripts
        self.our_scripts = our_script_names()

    def q(self, sql):
        return self.db.query(sql.replace("{T}", self.theirs).replace("{O}", self.ours))

    def our_ids(self, table, col, kind=None):
        """The ids our database uses in `table.col`, without those an earlier run of this import
        put there (the id map's `kind`), so a re-import against an imported database is the same."""
        got = {int(r["v"]) for r in self.q(f"select distinct {col} v from {{O}}.{table}") if r["v"] is not None}
        return got - set(self.ids.map[kind or table].values())

    def keep_ids(self, kind, values):
        """Record ids this import adds under their own numbers, so a re-import knows them."""
        for v in values:
            self.ids.map[kind][int(v)] = int(v)

    def emit(self, table, rows, keys):
        """DELETE by key, then INSERT, in our table's columns; missing ones take their default."""
        if not rows:
            return
        cols = self.db.columns(self.ours, table)
        names = [c for c, _ in cols if any(c in r for r in rows)]
        types = dict(cols)
        ranges = self.db.ranges[(self.ours, table)]
        fixed = 0
        for r in rows:
            for c in names:
                v, rng = r.get(c), ranges.get(c)
                if rng is None or v is None or v == "":
                    continue
                n = int(float(v))
                if not rng[0] <= n <= rng[1]:
                    r[c] = fit(c, n, rng)
                    fixed += 1
        if fixed:
            self.report.append(f"{table}: {fixed} values past our column widths (flags masked, others clamped)")
        for k in keys:
            vals = sorted({r[k] for r in rows})
            self.out.append(f"DELETE FROM `{table}` WHERE `{k}` IN ({','.join(sql_value(v, types[k]) for v in vals)});")
        for i in range(0, len(rows), 200):
            chunk = rows[i:i + 200]
            vals = ",\n".join(
                "(" + ",".join(sql_value(r.get(c), types[c]) for c in names) + ")" for r in chunk)
            self.out.append(f"INSERT INTO `{table}` (`" + "`,`".join(names) + f"`) VALUES\n{vals};")
        self.report.append(f"{table}: {len(rows)} rows")

    def run(self, areas, zones, maps):
        ids = self.ids
        for kind, (table, col) in {
            "creature": ("creature_template", "entry"),
            "item": ("item_template", "entry"),
            "gameobject": ("gameobject_template", "entry"),
            "quest": ("quest_template", "entry"),
            "creature_loot": ("creature_loot_template", "entry"),
            "gameobject_loot": ("gameobject_loot_template", "entry"),
            "item_loot": ("item_loot_template", "entry"),
            "skinning_loot": ("skinning_loot_template", "entry"),
            "pickpocket_loot": ("pickpocketing_loot_template", "entry"),
            "reference_loot": ("reference_loot_template", "entry"),
            "gossip_menu": ("gossip_menu", "entry"),
            "npc_text": ("npc_text", "ID"),
            "broadcast_text": ("broadcast_text", "entry"),
            "page_text": ("page_text", "entry"),
            "equipment": ("creature_equip_template", "entry"),
            "creature_spells": ("creature_spells", "entry"),
            "ai_script": ("creature_ai_scripts", "id"),
            "creature_guid": ("creature", "guid"),
            "gameobject_guid": ("gameobject", "guid"),
            "spell": ("spell_template", "entry"),
            "condition": ("conditions", "condition_entry"),
            "game_event": ("game_event", "entry"),
            "pool": ("pool_template", "entry"),
            "quest_start_script": ("quest_start_scripts", "id"),
            "quest_end_script": ("quest_end_scripts", "id"),
            "gossip_script": ("gossip_scripts", "id"),
            "movement_script": ("creature_movement_scripts", "id"),
            "event_script": ("event_scripts", "id"),
        }.items():
            ids.ours[kind] = self.our_ids(table, col, kind)
        ours_ct = ids.ours["creature"]

        # 1. Spawns placed by the chunk under them.
        def in_zone(m, x, y):
            if int(m) in self.instance_maps:
                return True
            col, row = chunk_of(float(x), float(y))
            return areas.get((int(m), col, row), (None, None))[1] in zones
        maps = set(maps) | self.instance_maps
        maplist = ",".join(str(m) for m in sorted(maps))
        cspawns = [r for r in self.q(f"select * from {{T}}.creature where map in ({maplist})")
                   if in_zone(r["map"], r["position_x"], r["position_y"])]
        gspawns = [r for r in self.q(f"select * from {{T}}.gameobject where map in ({maplist})")
                   if in_zone(r["map"], r["position_x"], r["position_y"])]
        self.report.append(f"spawns in zone: {len(cspawns)} creatures, {len(gspawns)} objects")
        # In a zone we already have (a stock zone they edited), their copies of our spawns stay
        # out: only spawns of entries we lack come in, and none within 3 yd of one of ours of the
        # same entry. A zone new to us takes every spawn.
        our_zones = self.our_ids("area_template", "entry", "area")
        stock = {z for z in zones if z in our_zones}
        if stock:
            def zone_of(r):
                col, row = chunk_of(float(r["position_x"]), float(r["position_y"]))
                return areas.get((int(r["map"]), col, row), (None, None))[1]

            def dedupe(rows, table, have):
                near = defaultdict(list)
                for o in self.q(f"select id, map, position_x x, position_y y, position_z z from {{O}}.{table} where map in ({maplist}) "
                                f"and guid not between 12000000 and 12999999"):
                    near[(int(o["id"]), int(o["map"]), int(float(o["x"]) // 10), int(float(o["y"]) // 10))].append(o)

                def dup(r):
                    e, m = int(r["id"]), int(r["map"])
                    x, y, z = float(r["position_x"]), float(r["position_y"]), float(r["position_z"])
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            for o in near.get((e, m, int(x // 10) + dx, int(y // 10) + dy), ()):
                                if (float(o["x"]) - x) ** 2 + (float(o["y"]) - y) ** 2 + (float(o["z"]) - z) ** 2 < 9:
                                    return True
                    return False
                return [r for r in rows if zone_of(r) not in stock or (int(r["id"]) not in have and not dup(r))]
            before = (len(cspawns), len(gspawns))
            cspawns = dedupe(cspawns, "creature", ours_ct)
            gspawns = dedupe(gspawns, "gameobject", ids.ours["gameobject"])
            self.report.append(f"zones we have {sorted(stock)}: kept {len(cspawns)}/{before[0]} creature and "
                               f"{len(gspawns)}/{before[1]} object spawns (entries we lack only)")

        # 2. Templates: theirs only where we do not have the entry.
        centries = {int(r[k]) for r in cspawns for k in ("id", "id2", "id3", "id4") if r.get(k) and int(r[k])}
        gentries = {int(r["id"]) for r in gspawns}
        new_c = {e for e in centries if e not in ours_ct}
        new_g = {e for e in gentries if e not in ids.ours["gameobject"]}
        # Quests: given or ended by the zone's creatures and objects, or sorted to the zone.
        rel = []
        for t, ent in (("creature", centries), ("gameobject", gentries)):
            for kind in ("questrelation", "involvedrelation"):
                for r in self.q(f"select * from {{T}}.{t}_{kind} where id in ({ids_sql(ent)})"):
                    rel.append((t, kind, r))
        qids = {int(r["quest"]) for _, _, r in rel}
        zone_areas = {a for (_, _, _), (a, z) in areas.items() if z in zones}
        for r in self.q(f"select entry from {{T}}.quest_template where ZoneOrSort in ({ids_sql(zone_areas | zones)})"):
            qids.add(int(r["entry"]))
        new_q = {q for q in qids if q not in ids.ours["quest"]}
        # Close the chains (previous, next, next in chain, breadcrumbs) over new quests.
        frontier = set(new_q)
        quests = {}
        while frontier:
            rows = self.q(f"select * from {{T}}.quest_template where entry in ({ids_sql(frontier)})")
            frontier = set()
            for r in rows:
                quests[int(r["entry"])] = r
                for c in ("PrevQuestId", "NextQuestId", "NextQuestInChain"):
                    v = abs(int(r.get(c) or 0))
                    if v and v not in quests and v not in ids.ours["quest"]:
                        frontier.add(v)
        new_q = set(quests)

        # 3. Creature templates (+ what they name).
        ctemps = {int(r["entry"]): r for r in self.q(f"select * from {{T}}.creature_template where entry in ({ids_sql(new_c)})")}
        stats = {(int(r["class"]), int(r["level"])): r for r in self.q("select * from {O}.creature_classlevelstats")}
        # Quest objectives may name creatures and objects that are not spawned here.
        for r in quests.values():
            for i in range(1, 5):
                v = int(r.get(f"ReqCreatureOrGOId{i}") or 0)
                if v > 0 and v not in ours_ct and v not in ctemps:
                    for t in self.q(f"select * from {{T}}.creature_template where entry={v}"):
                        ctemps[v] = t
                elif v < 0 and -v not in ids.ours["gameobject"]:
                    new_g.add(-v)
        gtemps = {int(r["entry"]): r for r in self.q(f"select * from {{T}}.gameobject_template where entry in ({ids_sql(new_g)})")}

        # Items: loot, quest objectives and rewards, vendors, equipment; references closed.
        loot = defaultdict(list)  # table -> rows
        def take_loot(table, entries):
            if not entries:
                return
            for r in self.q(f"select * from {{T}}.{table} where entry in ({ids_sql(entries)})"):
                loot[table].append(r)
        take_loot("creature_loot_template", {int(t["loot_id"]) for t in ctemps.values() if int(t["loot_id"] or 0)})
        take_loot("skinning_loot_template", {int(t["skinning_loot_id"]) for t in ctemps.values() if int(t["skinning_loot_id"] or 0)})
        take_loot("pickpocketing_loot_template", {int(t["pickpocket_loot_id"]) for t in ctemps.values() if int(t["pickpocket_loot_id"] or 0)})
        # Chests (type 3) and fishing holes (type 25) name their loot in data1.
        take_loot("gameobject_loot_template", {int(t["data1"]) for t in gtemps.values() if int(t["type"]) in (3, 25) and int(t["data1"] or 0) > 0})
        refs, seen_refs = set(), set()
        def collect_refs():
            out = set()
            for rows in loot.values():
                for r in rows:
                    m = int(r["mincountOrRef"] or 0)
                    if m < 0 and -m not in seen_refs:
                        out.add(-m)
            return out
        refs = collect_refs()
        while refs:
            seen_refs |= refs
            take_loot("reference_loot_template", refs)
            refs = collect_refs()
        items = set()
        for rows in loot.values():
            for r in rows:
                if int(r["mincountOrRef"] or 0) >= 0:
                    items.add(int(r["item"]))
        for r in quests.values():
            for c in [f"ReqItemId{i}" for i in range(1, 5)] + [f"ReqSourceId{i}" for i in range(1, 5)] + \
                     [f"RewChoiceItemId{i}" for i in range(1, 7)] + [f"RewItemId{i}" for i in range(1, 5)] + ["SrcItemId"]:
                if int(r.get(c) or 0):
                    items.add(int(r[c]))
        vendors = self.q(f"select * from {{T}}.npc_vendor where entry in ({ids_sql(ctemps)})")
        items |= {int(r["item"]) for r in vendors}
        equip = self.q(f"select * from {{T}}.creature_equip_template where entry in ({ids_sql({int(t['equipment_id']) for t in ctemps.values() if int(t['equipment_id'] or 0)})})")
        for r in equip:
            items |= {int(r[c]) for c in ("equipentry1", "equipentry2", "equipentry3") if int(r.get(c) or 0)}
        new_i = {i for i in items if i not in ids.ours["item"]}
        itemrows = {int(r["entry"]): r for r in self.q(f"select * from {{T}}.item_template where entry in ({ids_sql(new_i)})")}
        take_loot("item_loot_template", {e for e in itemrows if int(itemrows[e]["flags"] or 0) & 0x4})
        # Loot rows for items we do not get (theirs missing or ours) are kept only if the item exists.
        known_items = ids.ours["item"] | set(itemrows)

        # --- renumber, convert, emit ---------------------------------------------------------
        C = lambda e: ids.get("creature", int(e)) if int(e) in ctemps else int(e)
        G = lambda e: ids.get("gameobject", int(e)) if int(e) in gtemps else int(e)
        I = lambda e: ids.get("item", int(e)) if int(e) in itemrows else int(e)
        Q = lambda e: (ids.get("quest", abs(int(e))) * (1 if int(e) >= 0 else -1)) if abs(int(e)) in quests else int(e)
        # Ours may lack an entry a row names: such rows are dropped.
        have_c = lambda e: int(e) in ctemps or int(e) in ours_ct
        have_g = lambda e: int(e) in gtemps or int(e) in ids.ours["gameobject"]

        # Gossip: menus, texts and broadcast texts of the imported creatures.
        menus = {int(t["gossip_menu_id"]) for t in ctemps.values() if int(t["gossip_menu_id"] or 0)}
        gm, gmo, texts, btexts = [], [], set(), set()
        while menus:
            got = self.q(f"select * from {{T}}.gossip_menu where entry in ({ids_sql(menus)})")
            opts = self.q(f"select * from {{T}}.gossip_menu_option where menu_id in ({ids_sql(menus)})")
            done = {int(r["entry"]) for r in gm} | menus
            gm += got
            gmo += opts
            texts |= {int(r["text_id"]) for r in got}
            btexts |= {int(r[c]) for r in opts for c in ("option_broadcast_text", "box_broadcast_text") if int(r[c] or 0)}
            menus = {int(r["action_menu_id"]) for r in opts if int(r["action_menu_id"] or 0) > 0} - done
        npc_texts = self.q(f"select * from {{T}}.npc_text where ID in ({ids_sql(texts)})")
        for r in npc_texts:
            btexts |= {int(r[f"BroadcastTextID{i}"]) for i in range(8) if int(r[f"BroadcastTextID{i}"] or 0)}
        # EventAI.
        ai_events = self.q(f"select * from {{T}}.creature_ai_events where creature_id in ({ids_sql(ctemps)})")
        script_ids = {int(r[f"action{i}_script"]) for r in ai_events for i in (1, 2, 3) if int(r[f"action{i}_script"] or 0)}
        ai_scripts = self.q(f"select * from {{T}}.creature_ai_scripts where id in ({ids_sql(script_ids)})")
        for r in ai_scripts:
            # Talk (command 0) names broadcast texts in dataint..dataint4.
            if int(r["command"]) == 0:
                btexts |= {int(r[c]) for c in ("dataint", "dataint2", "dataint3", "dataint4") if int(r[c] or 0) > 0}
        bt_rows = self.q(f"select * from {{T}}.broadcast_text where entry in ({ids_sql(btexts)})")
        spell_lists = self.q(f"select * from {{T}}.creature_spells where entry in ({ids_sql({int(t['spell_list_id']) for t in ctemps.values() if int(t['spell_list_id'] or 0)})})")
        pages = set()
        for r in itemrows.values():
            if int(r.get("page_text") or 0):
                pages.add(int(r["page_text"]))
        page_rows = []
        while pages:
            got = self.q(f"select * from {{T}}.page_text where entry in ({ids_sql(pages)})")
            page_rows += got
            pages = {int(r["next_page"]) for r in got if int(r["next_page"] or 0)} - {int(r["entry"]) for r in page_rows}

        new_bt = {int(r["entry"]) for r in bt_rows if int(r["entry"]) not in ids.ours["broadcast_text"]}
        BT = lambda e: ids.get("broadcast_text", int(e)) if int(e) in new_bt else int(e)
        new_nt = {int(r["ID"]) for r in npc_texts if int(r["ID"]) not in ids.ours["npc_text"]}
        NT = lambda e: ids.get("npc_text", int(e)) if int(e) in new_nt else int(e)
        new_gm = {int(r["entry"]) for r in gm if int(r["entry"]) not in ids.ours["gossip_menu"]}
        GM = lambda e: ids.get("gossip_menu", int(e)) if int(e) in new_gm else int(e)
        new_pages = {int(r["entry"]) for r in page_rows if int(r["entry"]) not in ids.ours["page_text"]}
        PT = lambda e: ids.get("page_text", int(e)) if int(e) in new_pages else int(e)
        loot_kind = {
            "creature_loot_template": "creature_loot", "gameobject_loot_template": "gameobject_loot",
            "item_loot_template": "item_loot", "skinning_loot_template": "skinning_loot",
            "pickpocketing_loot_template": "pickpocket_loot", "reference_loot_template": "reference_loot",
        }
        new_loot = {t: {int(r["entry"]) for r in rows if int(r["entry"]) not in ids.ours[loot_kind[t]]} for t, rows in loot.items()}
        def L(table, e):
            e = int(e)
            if e in new_loot.get(table, ()):
                # A creature's loot keyed by its own entry follows the entry's new number.
                if table == "creature_loot_template" and e in ctemps:
                    new = ids.get("creature", e)
                    if new not in ids.ours["creature_loot"]:
                        ids.map["creature_loot"].setdefault(e, new)
                        ids.taken["creature_loot"].add(new)
                return ids.get(loot_kind[table], e)
            return e
        RL = lambda e: L("reference_loot_template", e)
        new_sl = {int(r["entry"]) for r in spell_lists if int(r["entry"]) not in ids.ours["creature_spells"]}
        SL = lambda e: ids.get("creature_spells", int(e)) if int(e) in new_sl else int(e)
        new_eq = {int(r["entry"]) for r in equip if int(r["entry"]) not in ids.ours["equipment"]}
        EQ = lambda e: ids.get("equipment", int(e)) if int(e) in new_eq else int(e)
        AS = lambda e: ids.get("ai_script", int(e)) if int(e) else 0
        need = defaultdict(set)  # kind -> their ids the rows below name

        def lazy(kind):
            def f(e):
                e = int(e or 0)
                if not e or e in ids.ours[kind]:
                    return e
                need[kind].add(e)
                return ids.get(kind, e)
            return f
        CD = lazy("condition")
        EVT = lazy("game_event")
        # Spells: every spell of theirs we lack comes in (below); one we have keeps its number.
        their_spells = {int(r["entry"]) for r in self.q("select distinct entry from {T}.spell_template")}
        new_spells = their_spells - ids.ours["spell"]
        S = lambda e: ids.get("spell", int(e)) if int(e or 0) in new_spells else int(e or 0)
        SCR = {k: lazy(k) for k in ("quest_start_script", "quest_end_script", "gossip_script", "movement_script", "event_script")}

        # creature_template.
        rows = []
        for e, t in sorted(ctemps.items()):
            r = creature_template(t, stats)
            r["entry"] = C(e)
            r["loot_id"] = L("creature_loot_template", r["loot_id"]) if int(r["loot_id"] or 0) else 0
            r["skinning_loot_id"] = L("skinning_loot_template", r["skinning_loot_id"]) if int(r["skinning_loot_id"] or 0) else 0
            r["pickpocket_loot_id"] = L("pickpocketing_loot_template", r["pickpocket_loot_id"]) if int(r["pickpocket_loot_id"] or 0) else 0
            r["gossip_menu_id"] = GM(r["gossip_menu_id"])
            r["spell_list_id"] = SL(r["spell_list_id"]) if int(r["spell_list_id"] or 0) else 0
            r["equipment_id"] = EQ(r["equipment_id"]) if int(r["equipment_id"] or 0) else 0
            self.scripts.append(("creature", e, t.get("script_name") or ""))
            # Their core's scripts do not come with the database; a name ours registers stays.
            r["script_name"] = t.get("script_name") if t.get("script_name") in self.our_scripts else ""
            for i in range(1, 5):
                if int(r.get(f"display_id{i}") or 0):
                    self.displays["creature"].add(int(r[f"display_id{i}"]))
            rows.append(r)
        self.emit("creature_template", rows, ["entry"])
        # Display rows: bounding radius and combat reach for new displays.
        dids = self.displays["creature"]
        ours_d = self.our_ids("creature_display_info_addon", "display_id", "display_addon")
        rows = [dict(r, build=0) for r in self.q(f"select * from {{T}}.creature_display_info_addon where display_id in ({ids_sql(dids - ours_d)})")]
        self.keep_ids("display_addon", (r["display_id"] for r in rows))
        self.emit("creature_display_info_addon", rows, ["display_id"])

        rows = [dict(r, entry=EQ(r["entry"]), probability=100, item1=I(r.get("equipentry1") or 0),
                     item2=I(r.get("equipentry2") or 0), item3=I(r.get("equipentry3") or 0),
                     patch_min=0, patch_max=10) for r in equip if int(r["entry"]) in new_eq]
        for r in rows:
            for c in ("equipentry1", "equipentry2", "equipentry3"):
                r.pop(c, None)
        self.emit("creature_equip_template", rows, ["entry"])
        self.emit("creature_spells", [dict(r, entry=SL(r["entry"]), **{f"spellId_{i}": S(r.get(f"spellId_{i}")) for i in range(1, 9)})
                                      for r in spell_lists if int(r["entry"]) in new_sl], ["entry"])
        self.emit("creature_onkill_reputation",
                  [dict(r, creature_id=C(r["creature_id"]), patch=0) for r in self.q(f"select * from {{T}}.creature_onkill_reputation where creature_id in ({ids_sql(ctemps)})")],
                  ["creature_id"])
        self.emit("npc_vendor", [dict(r, entry=C(r["entry"]), item=I(r["item"]), condition_id=CD(r.get("condition_id")))
                                 for r in vendors if int(r["item"]) in known_items], ["entry"])
        trainers = self.q(f"select * from {{T}}.npc_trainer where entry in ({ids_sql(ctemps)})")
        self.emit("npc_trainer", [dict(r, entry=C(r["entry"]), spell=S(r["spell"])) for r in trainers], ["entry"])

        def remap_script(d, r):
            """Ids inside a script command: talk texts, summoned creatures, spells cast, objects
            respawned or opened (by guid), and the command's own condition."""
            cmd = int(r["command"])
            if cmd == 0:
                for c in ("dataint", "dataint2", "dataint3", "dataint4"):
                    if int(r[c] or 0) > 0:
                        d[c] = BT(r[c])
            elif cmd == 10 and int(r["datalong"] or 0) in ctemps:  # summon creature
                d["datalong"] = C(r["datalong"])
            elif cmd == 15:  # cast spell
                d["datalong"] = S(r["datalong"])

        # creature_ai: events renumber by their creature (`entry·100 + n` where they followed it).
        ev = []
        for r in ai_events:
            cid = int(r["creature_id"])
            nid = int(r["id"])
            new_c_id = C(cid)
            new_id = new_c_id * 100 + nid % 100 if nid // 100 == cid else ids.get("ai_script", nid)
            d = dict(r, id=new_id, creature_id=new_c_id, condition_id=CD(r.get("condition_id")))
            for i in (1, 2, 3):
                d[f"action{i}_script"] = AS(r[f"action{i}_script"])
            ev.append(d)
        self.emit("creature_ai_events", ev, ["id"])
        sc = []
        for r in ai_scripts:
            d = dict(r, id=AS(r["id"]), condition_id=CD(r.get("condition_id")))
            remap_script(d, r)
            sc.append(d)
        self.emit("creature_ai_scripts", sc, ["id"])

        # Gossip and texts.
        self.emit("broadcast_text", [dict(r, entry=BT(r["entry"])) for r in bt_rows if int(r["entry"]) in new_bt], ["entry"])
        nts = []
        for r in npc_texts:
            if int(r["ID"]) not in new_nt:
                continue
            d = dict(r, ID=NT(r["ID"]))
            for i in range(8):
                d[f"BroadcastTextID{i}"] = BT(r[f"BroadcastTextID{i}"] or 0)
            nts.append(d)
        self.emit("npc_text", nts, ["ID"])
        self.emit("gossip_menu", [dict(r, entry=GM(r["entry"]), text_id=NT(r["text_id"]), script_id=SCR["gossip_script"](r["script_id"]), condition_id=CD(r["condition_id"]))
                                  for r in gm if int(r["entry"]) in new_gm], ["entry"])
        self.emit("gossip_menu_option", [dict(r, menu_id=GM(r["menu_id"]), action_menu_id=GM(r["action_menu_id"]) if int(r["action_menu_id"] or 0) > 0 else r["action_menu_id"],
                                              option_broadcast_text=BT(r["option_broadcast_text"] or 0), box_broadcast_text=BT(r["box_broadcast_text"] or 0),
                                              action_script_id=SCR["gossip_script"](r["action_script_id"]), condition_id=CD(r["condition_id"]))
                                         for r in gmo if int(r["menu_id"]) in new_gm], ["menu_id"])

        # Loot.
        for table, rows in loot.items():
            out = []
            for r in rows:
                if int(r["entry"]) not in new_loot[table]:
                    continue
                m = int(r["mincountOrRef"] or 0)
                if m >= 0 and int(r["item"]) not in known_items:
                    continue
                d = dict(r, entry=L(table, r["entry"]), condition_id=CD(r.get("condition_id")), patch_min=0, patch_max=10)
                if m < 0:
                    d["mincountOrRef"] = -RL(-m)
                else:
                    d["item"] = I(r["item"])
                out.append(d)
            self.emit(table, out, ["entry"])

        # Items.
        rows = []
        for e, r in sorted(itemrows.items()):
            d = dict(r, entry=I(e), patch=0, start_quest=Q(r["start_quest"]) if int(r["start_quest"] or 0) else 0,
                     page_text=PT(r["page_text"]) if int(r["page_text"] or 0) else 0)
            self.scripts.append(("item", e, r.get("script_name") or ""))
            d.pop("script_name", None)
            for i in range(1, 6):
                d[f"spellid_{i}"] = S(r.get(f"spellid_{i}"))
            if int(r.get("display_id") or 0):
                self.displays["item"].add(int(r["display_id"]))
            rows.append(d)
        self.emit("item_template", rows, ["entry"])
        self.emit("page_text", [dict(r, entry=PT(r["entry"]), next_page=PT(r["next_page"]) if int(r["next_page"] or 0) else 0)
                                for r in page_rows if int(r["entry"]) in new_pages], ["entry"])

        # Game object templates.
        rows = []
        for e, t in sorted(gtemps.items()):
            self.scripts.append(("gameobject", e, t.get("script_name") or ""))
            d = dict(t, entry=G(e), patch=0, script_name=t.get("script_name") if t.get("script_name") in self.our_scripts else "")
            d.pop("phase_quest_id", None)
            if int(t["type"]) in (3, 25) and int(t["data1"] or 0) > 0:
                d["data1"] = L("gameobject_loot_template", t["data1"])
            self.displays["gameobject"].add(int(t["displayId"]))
            rows.append(d)
        self.emit("gameobject_template", rows, ["entry"])

        # Quests.
        rows = []
        for e, r in sorted(quests.items()):
            d = dict(r, entry=Q(e), patch=0, StartScript=SCR["quest_start_script"](r["StartScript"]),
                     CompleteScript=SCR["quest_end_script"](r["CompleteScript"]), RequiredCondition=CD(r["RequiredCondition"]),
                     RewSpell=S(r["RewSpell"]), RewSpellCast=S(r["RewSpellCast"]), SrcSpell=S(r["SrcSpell"]))
            for i in range(1, 5):
                d[f"ReqSpellCast{i}"] = S(r[f"ReqSpellCast{i}"])
            for c in ("PrevQuestId", "NextQuestId", "NextQuestInChain"):
                d[c] = Q(r[c] or 0)
            for c in [f"ReqItemId{i}" for i in range(1, 5)] + [f"ReqSourceId{i}" for i in range(1, 5)] + \
                     [f"RewChoiceItemId{i}" for i in range(1, 7)] + [f"RewItemId{i}" for i in range(1, 5)] + ["SrcItemId"]:
                d[c] = I(r[c] or 0)
            for i in range(1, 5):
                v = int(r.get(f"ReqCreatureOrGOId{i}") or 0)
                d[f"ReqCreatureOrGOId{i}"] = C(v) if v > 0 else (-G(-v) if v < 0 else 0)
            rows.append(d)
        self.emit("quest_template", rows, ["entry"])
        for t in ("creature", "gameobject"):
            for kind in ("questrelation", "involvedrelation"):
                f = C if t == "creature" else G
                rs = [dict(r, id=f(r["id"]), quest=Q(r["quest"]), patch_min=0, patch_max=10)
                      for (tt, kk, r) in rel if tt == t and kk == kind and int(r["quest"]) in quests]
                # Keyed by quest: a re-import replaces exactly its own quests' givers and enders.
                self.emit(f"{t}_{kind}", rs, ["quest"])
        # Areas, flight masters' nodes and area triggers in the zones, where we lack them.
        if self.area_rows:
            ours_a = self.our_ids("area_template", "entry", "area")
            rows = [r for r in self.area_rows if int(r["entry"]) not in ours_a]
            self.keep_ids("area", (r["entry"] for r in rows))
            self.emit("area_template", rows, ["entry"])
        ours_tn = self.our_ids("taxi_nodes", "id", "taxi_node")
        tn = [dict(r, build=5875) for r in self.q(f"select * from {{T}}.taxi_nodes where map_id in ({maplist})")
              if int(r["id"]) not in ours_tn and in_zone(r["map_id"], r["x"], r["y"])]
        self.keep_ids("taxi_node", (r["id"] for r in tn))
        self.emit("taxi_nodes", tn, ["id"])
        ours_at = self.our_ids("areatrigger_template", "id", "areatrigger")
        # Instance entrances: every teleport into an instanced map we bring, wherever it stands.
        entrance = {int(r["id"]) for r in self.q(f"select id from {{T}}.areatrigger_teleport where target_map in ({ids_sql(self.instance_maps)})")}
        at = []
        for r in self.q("select * from {T}.areatrigger_template"):
            if int(r["id"]) in ours_at or not (int(r["id"]) in entrance or (int(r["map_id"]) in maps and in_zone(r["map_id"], r["x"], r["y"]))):
                continue
            at.append(dict(r, build=5875, condition_id=CD(r.get("condition_id")), script_id=SCR["event_script"](r.get("script_id")), script_name=""))
            if r.get("script_name"):
                self.scripts.append(("areatrigger", int(r["id"]), r["script_name"]))
        self.keep_ids("areatrigger", (r["id"] for r in at))
        self.emit("areatrigger_template", at, ["id"])
        at_ids = {int(r["id"]) for r in at}
        tele = self.q(f"select * from {{T}}.areatrigger_teleport where id in ({ids_sql(at_ids)})")
        self.emit("areatrigger_teleport", [dict(r, patch=0) for r in tele], ["id"])
        ats = self.q(f"select * from {{T}}.areatrigger_involvedrelation where quest in ({ids_sql(quests)})")
        self.emit("areatrigger_involvedrelation", [dict(r, quest=Q(r["quest"])) for r in ats], ["id"])

        # Spawns, renumbered into our guid block; only those whose template we have.
        rows, guids = [], []
        for r in sorted(cspawns, key=lambda r: int(r["guid"])):
            if not have_c(r["id"]):
                continue
            g = ids.get("creature_guid", int(r["guid"]))
            guids.append((int(r["guid"]), g))
            d = dict(r, guid=g, patch_min=0, patch_max=10, id5=0)
            for k in ("id", "id2", "id3", "id4"):
                d[k] = C(r[k]) if int(r[k] or 0) and have_c(r[k]) else 0
            rows.append(d)
        self.emit("creature", rows, ["guid"])
        gmap = dict(guids)
        adds = self.q(f"select * from {{T}}.creature_addon where guid in ({ids_sql(gmap)})")
        self.emit("creature_addon", [dict(r, guid=gmap[int(r["guid"])], patch=0) for r in adds], ["guid"])
        mv = self.q(f"select * from {{T}}.creature_movement where id in ({ids_sql(gmap)})")
        self.emit("creature_movement", [dict(r, id=gmap[int(r["id"])], script_id=SCR["movement_script"](r["script_id"]), path_id=0) for r in mv], ["id"])
        rows = []
        for r in sorted(gspawns, key=lambda r: int(r["guid"])):
            if not have_g(r["id"]):
                continue
            rows.append(dict(r, guid=ids.get("gameobject_guid", int(r["guid"])), id=G(r["id"]), patch_min=0, patch_max=10))
        self.emit("gameobject", rows, ["guid"])
        go_map = {int(r["guid"]): ids.get("gameobject_guid", int(r["guid"])) for r in gspawns if have_g(r["id"])}

        # Game events of the imported spawns, and their pools (mother pools closed).
        gec = self.q(f"select * from {{T}}.game_event_creature where guid in ({ids_sql(gmap)})")
        geg = self.q(f"select * from {{T}}.game_event_gameobject where guid in ({ids_sql(go_map)})")
        def ev_sign(v):
            v = int(v)
            return EVT(abs(v)) * (1 if v >= 0 else -1)
        self.emit("game_event_creature", [dict(r, guid=gmap[int(r["guid"])], event=ev_sign(r["event"])) for r in gec], ["guid"])
        self.emit("game_event_gameobject", [dict(r, guid=go_map[int(r["guid"])], event=ev_sign(r["event"])) for r in geg], ["guid"])
        pc = self.q(f"select * from {{T}}.pool_creature where guid in ({ids_sql(gmap)})")
        pg = self.q(f"select * from {{T}}.pool_gameobject where guid in ({ids_sql(go_map)})")
        pools = {int(r["pool_entry"]) for r in pc + pg}
        pp, seen = [], set()
        frontier = set(pools)
        while frontier:
            seen |= frontier
            got = self.q(f"select * from {{T}}.pool_pool where pool_id in ({ids_sql(frontier)}) or mother_pool in ({ids_sql(frontier)})")
            pp += [r for r in got if r not in pp]
            frontier = {int(r[c]) for r in got for c in ("pool_id", "mother_pool")} - seen
        pools |= seen
        new_pools = {p for p in pools if p not in ids.ours["pool"]}
        P = lambda e: ids.get("pool", int(e)) if int(e) in new_pools else int(e)
        self.emit("pool_template", [dict(r, entry=P(r["entry"]), patch_min=0, patch_max=10)
                                    for r in self.q(f"select * from {{T}}.pool_template where entry in ({ids_sql(new_pools)})")], ["entry"])
        self.emit("pool_pool", [dict(r, pool_id=P(r["pool_id"]), mother_pool=P(r["mother_pool"]))
                                for r in pp if int(r["pool_id"]) in new_pools], ["pool_id"])
        self.emit("pool_creature", [dict(r, guid=gmap[int(r["guid"])], pool_entry=P(r["pool_entry"]), patch_min=0, patch_max=10) for r in pc], ["guid"])
        self.emit("pool_gameobject", [dict(r, guid=go_map[int(r["guid"])], pool_entry=P(r["pool_entry"]), patch_min=0, patch_max=10) for r in pg], ["guid"])

        # Instanced maps.
        if self.instance_maps:
            mt = self.q(f"select * from {{T}}.map_template where entry in ({ids_sql(self.instance_maps)})")
            for r in mt:
                if r.get("script_name"):
                    self.scripts.append(("map", int(r["entry"]), r["script_name"]))
            self.emit("map_template", [dict(r, patch=0, script_name="") for r in mt], ["entry"])

        # Scripts (quest start/end, gossip, waypoint, area trigger), closed over what they name.
        script_tables = {"quest_start_script": "quest_start_scripts", "quest_end_script": "quest_end_scripts",
                         "gossip_script": "gossip_scripts", "movement_script": "creature_movement_scripts",
                         "event_script": "event_scripts"}
        extra_bt = set()
        def note_bt(d, r):
            if int(r["command"]) == 0:
                for c in ("dataint", "dataint2", "dataint3", "dataint4"):
                    if int(r[c] or 0) > 0 and int(r[c]) not in new_bt:
                        extra_bt.add(int(r[c]))
        done = defaultdict(set)
        while any(need[k] - done[k] for k in script_tables):
            for kind, table in script_tables.items():
                todo = need[kind] - done[kind]
                if not todo:
                    continue
                done[kind] |= todo
                rows = []
                for r in self.q(f"select * from {{T}}.{table} where id in ({ids_sql(todo)})"):
                    d = dict(r, id=ids.get(kind, int(r["id"])), condition_id=CD(r.get("condition_id")))
                    remap_script(d, r)
                    note_bt(d, r)
                    rows.append(d)
                self.emit(table, rows, ["id"])
        extra_bt -= ids.ours["broadcast_text"]
        if extra_bt:
            self.emit("broadcast_text", [dict(r, entry=ids.get("broadcast_text", int(r["entry"])))
                                         for r in self.q(f"select * from {{T}}.broadcast_text where entry in ({ids_sql(extra_bt)})")], ["entry"])

        # Game events named anywhere above.
        evs = self.q(f"select * from {{T}}.game_event where entry in ({ids_sql(need['game_event'])})")
        self.emit("game_event", [dict(r, entry=EVT(r["entry"]), patch_min=0, patch_max=10) for r in evs], ["entry"])

        # Every spell of theirs we lack, in our columns.
        sp = []
        for r in self.q(f"select * from {{T}}.spell_template where entry in ({ids_sql(new_spells)})"):
            sp.append(dict(r, entry=S(r["entry"]), build=5875))
        self.emit("spell_template", sp, ["entry"])

        # Conditions, closed over the logic types (and, or, not name other conditions).
        done_c = set()
        crow = []
        while need["condition"] - done_c:
            todo = need["condition"] - done_c
            done_c |= todo
            for r in self.q(f"select * from {{T}}.conditions where condition_entry in ({ids_sql(todo)})"):
                d = dict(r, condition_entry=ids.get("condition", int(r["condition_entry"])))
                t = int(r["type"])
                if t in (-1, -2, -3):
                    for c in ("value1", "value2", "value3", "value4"):
                        if int(r[c] or 0) > 0:
                            d[c] = CD(r[c])
                elif t in (1, 17):
                    d["value1"] = S(r["value1"])
                elif t in (2, 3, 23):
                    d["value1"] = I(r["value1"])
                elif t in (8, 9, 19, 22):
                    d["value1"] = Q(r["value1"])
                elif t == 12:
                    d["value1"] = EVT(r["value1"])
                elif t == 20:
                    d["value1"] = C(r["value1"])
                elif t == 21:
                    d["value1"] = G(r["value1"])
                crow.append(d)
        self.emit("conditions", crow, ["condition_entry"])

        self.report.append(
            f"new templates: {len(ctemps)} creatures, {len(gtemps)} objects, {len(itemrows)} items, {len(quests)} quests; "
            f"EventAI {len(ev)} events / {len(sc)} script lines; {len(sp)} spells; {len(crow)} conditions; "
            f"{len(evs)} game events; {len(new_pools)} pools; their core's C++ scripts named: "
            f"{len({n for _, _, n in self.scripts if n})} (not ported by this import)")


def load_areas(path):
    areas = {}
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            m, tx, ty, cx, cy, a, z = (int(v) for v in line.split("\t"))
            areas[(m, tx * 16 + cx, ty * 16 + cy)] = (a, z)
    return areas


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--mariadb", default=os.path.join(os.path.dirname(__file__), "..", "mariadb", "bin", "mariadb.exe"))
    p.add_argument("--port", type=int, default=3399)
    p.add_argument("--theirs", required=True)
    p.add_argument("--ours", default="mangos")
    p.add_argument("--areas", required=True)
    p.add_argument("--zones", required=True)
    p.add_argument("--map", required=True)
    p.add_argument("--sql", required=True)
    p.add_argument("--displays", required=True)
    p.add_argument("--instance-maps", default="", help="their instanced maps, e.g. 800-822")
    p.add_argument("--scripts", help="where to list the C++ scripts their content names")
    p.add_argument("--brand-rules", help="the private branding rules (default private-assets/brand-rules.tsv)")
    a = p.parse_args(argv)
    # The branding rules first (brand.py): the import never runs without them.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import brand
    rules = brand.Rules.find(a.brand_rules)
    areas = load_areas(a.areas)
    # `all`: every zone the chosen tiles hold.
    zones = {z for (_, z) in areas.values() if z} if a.zones == "all" else {int(z) for z in a.zones.split(",")}
    maps = {m for (m, _, _) in areas}
    ids = IdMap(a.map)
    imp = Import(Db(a.mariadb, a.port), a.theirs, a.ours, ids)
    at = os.path.join(os.path.dirname(os.path.abspath(a.areas)), "area_template.tsv")
    if os.path.exists(at):
        with open(at, encoding="utf-8") as f:
            cols = next(f).rstrip("\n").split("\t")
            imp.area_rows = [dict(zip(cols, line.rstrip("\n").split("\t"))) for line in f if line.strip()]
    for part in filter(None, a.instance_maps.split(",")):
        lo, _, hi = part.partition("-")
        imp.instance_maps |= set(range(int(lo), int(hi or lo) + 1))
    imp.run(areas, zones, maps)
    if a.scripts:
        with open(a.scripts, "w", encoding="utf-8", newline="\n") as f:
            f.write("kind\ttheir_id\tscript_name\tstate\n")
            for kind, i, n in sorted(set(imp.scripts)):
                if n:
                    f.write(f"{kind}\t{i}\t{n}\t{'kept (ours has it)' if n in imp.our_scripts else 'not ported'}\n")
    os.makedirs(os.path.dirname(os.path.abspath(a.sql)), exist_ok=True)
    with open(a.sql, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"-- World of Modern Warfare: community zones ({len(zones)}) and maps {sorted(imp.instance_maps)} (generated by\n"
                "-- tools/community-merge/db_import.py; private, never committed). Idempotent: every\n"
                "-- row is deleted by key before it is inserted.\n")
        for line in imp.report:
            f.write(f"-- {line}\n")
        f.write("\n".join(imp.out) + "\n")
    with open(a.displays, "w", encoding="utf-8", newline="\n") as f:
        for kind in sorted(imp.displays):
            for d in sorted(imp.displays[kind]):
                f.write(f"{kind} {d}\n")
    ids.save()
    # The branding scrub and its gate (brand.py, the rules of src/brand.rs): every text output.
    left = [(p, rules.scrub_file(p)) for p in (a.sql, a.scripts, a.displays) if p and os.path.isfile(p)]
    left = [(p, h) for p, h in left if h]
    print("\n".join(rules.scrub_text(r) for r in imp.report))
    if left:
        for p, h in left:
            print(f"BRANDING LEFT: {p}: {h[1]!r}", file=sys.stderr)
        return f"{len(left)} outputs still carry branding"
    print("brand gate: clean")


if __name__ == "__main__":
    sys.exit(main())
