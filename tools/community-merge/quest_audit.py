"""Community-content audit (read-only against the local world DB).

For every imported community quest, decide whether a player can complete it with the data we
have (no C++ scripts of theirs), list the creatures/objects/items whose C++ script we lack, and
write design/COMMUNITY_QUEST_AUDIT.md. With --sql, also write the data fixes
(server/sql/custom/azeroth_warfare/81_community_fixes.sql): hide quests that cannot be finished,
and EventAI for the dungeon and zone bosses that would otherwise only melee.

    python -I tools/community-merge/quest_audit.py [--md PATH] [--sql PATH] [--json PATH]

Never name the community client's developers or project: "the community client".
"""
import argparse
import json
import math
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cdb import query, ROOT  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
IDMAP = os.path.join(REPO, "design", "COMMUNITY_ID_MAP.tsv")
PRIV = os.path.join(ROOT, "private-assets", "womw-community")
SCRIPTS = os.path.join(PRIV, "scripts.tsv")
AREAS = os.path.join(PRIV, "areas.tsv")

# Maps with server rows but no client map files: nobody can stand there.
UNREACHABLE_MAPS = {31, 45, 49, 50, 150, 803, 804, 805, 806, 809, 810, 811, 812}
INSTANCE_MAPS = set(range(800, 823)) | {26, 27, 28, 532}
HIDE_CONDITION = 4190001  # CONDITION_LEVEL >= 100: never true; hides a quest from its giver
CHUNK = 533.3333333 / 16.0
ORIGIN = 32.0 * 533.3333333


def ids_sql(s):
    return ",".join(str(int(x)) for x in sorted(s)) or "0"


def load_idmap():
    m = defaultdict(dict)
    for line in open(IDMAP, encoding="utf-8"):
        if line.startswith("#"):
            continue
        k, a, b = line.rstrip("\n").split("\t")
        m[k][int(a)] = int(b)
    return m


def rows(sql):
    return query(sql)


def I(v):
    try:
        return int(v or 0)
    except ValueError:
        return int(float(v))


class Audit:
    def __init__(self):
        self.ids = load_idmap()
        self.Q = set(self.ids["quest"].values())
        self.C = set(self.ids["creature"].values())
        self.G = set(self.ids["gameobject"].values())
        self.IT = set(self.ids["item"].values())
        self.SP = set(self.ids["spell"].values())
        self.noscript = {"creature": {}, "gameobject": {}, "item": {}, "map": {}}
        for line in open(SCRIPTS, encoding="utf-8"):
            kind, tid, name, state = line.rstrip("\n").split("\t")
            if kind == "kind" or state != "not ported":
                continue
            tid = int(tid)
            our = tid if kind == "map" else self.ids[kind].get(tid, tid)
            self.noscript[kind][our] = name
        self.load()

    # ----------------------------------------------------------------------------------------
    def load(self):
        print("loading...", file=sys.stderr)
        qcols = ["entry", "Title", "ZoneOrSort", "MinLevel", "QuestLevel", "Type", "QuestFlags", "SpecialFlags",
                 "PrevQuestId", "NextQuestId", "ExclusiveGroup", "NextQuestInChain", "SrcItemId", "SrcSpell",
                 "RequiredCondition", "RewSpell", "RewSpellCast", "RequiredRaces", "LimitTime", "BreadcrumbForQuestId"]
        for i in range(1, 5):
            qcols += [f"ReqItemId{i}", f"ReqItemCount{i}", f"ReqSourceId{i}", f"ReqCreatureOrGOId{i}",
                      f"ReqCreatureOrGOCount{i}", f"ReqSpellCast{i}"]
        for i in range(1, 7):
            qcols.append(f"RewChoiceItemId{i}")
        for i in range(1, 5):
            qcols.append(f"RewItemId{i}")
        allq = rows("select q.%s from quest_template q join (select entry, max(patch) p from quest_template group by entry) m "
                    "on m.entry=q.entry and m.p=q.patch" % ",q.".join(qcols))
        self.quests_all = {I(r["entry"]): r for r in allq}
        self.quests = {k: v for k, v in self.quests_all.items() if k in self.Q}

        def rel(t):
            d = defaultdict(set)
            for r in rows(f"select id, quest from {t}"):
                d[I(r["quest"])].add(I(r["id"]))
            return d
        self.c_start, self.c_end = rel("creature_questrelation"), rel("creature_involvedrelation")
        self.g_start, self.g_end = rel("gameobject_questrelation"), rel("gameobject_involvedrelation")
        self.at_end = rel("areatrigger_involvedrelation")

        self.ct = {I(r["entry"]): r for r in rows(
            "select entry,name,level_max,rank,loot_id,pickpocket_loot_id,skinning_loot_id,npc_flags,ai_name,spell_list_id,"
            "vendor_id,unit_class,script_name,faction,gossip_menu_id from creature_template")}
        self.gt = {I(r["entry"]): r for r in rows("select entry,name,type,data0,data1,data2,data3,script_name from gameobject_template")}
        self.it = {I(r["entry"]): r for r in rows(
            "select entry,name,class,start_quest,spellid_1,spellid_2,spellid_3,flags from item_template")}
        self.spells = {I(r["entry"]): r for r in rows(
            "select entry,name,script_name,effect1,effect2,effect3,effectItemType1,effectItemType2,effectItemType3,"
            "effectMiscValue1,effectMiscValue2,effectMiscValue3 from spell_template")}

        # spawns: entry -> list of (map, x, y)
        self.c_spawn = defaultdict(list)
        for r in rows("select id,id2,id3,id4,id5,map,position_x x,position_y y from creature"):
            for k in ("id", "id2", "id3", "id4", "id5"):
                e = I(r[k])
                if e:
                    self.c_spawn[e].append((I(r["map"]), float(r["x"]), float(r["y"])))
        self.g_spawn = defaultdict(list)
        for r in rows("select id,map,position_x x,position_y y from gameobject"):
            self.g_spawn[I(r["id"])].append((I(r["map"]), float(r["x"]), float(r["y"])))

        # loot: loot table entry -> items (references resolved)
        ref = defaultdict(set)
        for r in rows("select entry,item,mincountOrRef from reference_loot_template"):
            ref[I(r["entry"])].add((I(r["item"]), I(r["mincountOrRef"])))

        def resolve(pairs, depth=0):
            out = set()
            for item, mc in pairs:
                if mc < 0 and depth < 5:
                    out |= resolve(ref.get(-mc, set()) or ref.get(item, set()), depth + 1)
                else:
                    out.add(item)
            return out

        def loot(t):
            d = defaultdict(set)
            for r in rows(f"select entry,item,mincountOrRef from {t}"):
                d[I(r["entry"])].add((I(r["item"]), I(r["mincountOrRef"])))
            return {k: resolve(v) for k, v in d.items()}
        self.loot = {t: loot(t) for t in ("creature_loot_template", "gameobject_loot_template", "item_loot_template",
                                          "pickpocketing_loot_template", "skinning_loot_template")}
        self.fishing_items = set().union(*loot("fishing_loot_template").values()) if True else set()
        vend = defaultdict(set)
        for r in rows("select entry,item from npc_vendor"):
            vend[I(r["entry"])].add(I(r["item"]))
        vt = defaultdict(set)
        for r in rows("select entry,item from npc_vendor_template"):
            vt[I(r["entry"])].add(I(r["item"]))
        self.vendor_items = defaultdict(set)  # item -> vendor creatures
        for c, items in vend.items():
            for it in items:
                self.vendor_items[it].add(c)
        for c, t in self.ct.items():
            for it in vt.get(I(t["vendor_id"]), ()):
                self.vendor_items[it].add(c)

        # script commands: created items, summoned creatures/objects, quest credits
        self.script_items, self.script_summons, self.script_gos = set(), set(), set()
        self.script_explored = set()
        self.ai_summons = set()
        for t in ("quest_start_scripts", "quest_end_scripts", "gossip_scripts", "event_scripts", "spell_scripts",
                  "gameobject_scripts", "creature_movement_scripts", "generic_scripts", "areatrigger_scripts",
                  "creature_ai_scripts", "creature_spells_scripts"):
            for r in rows(f"select command,datalong,datalong2 from {t} where command in (7,10,17,76,8)"):
                c, d = I(r["command"]), I(r["datalong"])
                if c == 17:
                    self.script_items.add(d)
                elif c == 10:
                    self.script_summons.add(d)
                elif c == 76:
                    self.script_gos.add(d)
                elif c == 7:
                    self.script_explored.add(d)
                elif c == 8:  # kill credit
                    self.script_explored.add(-d)
        # spells: created items / summoned creatures / quest complete
        self.spell_items = defaultdict(set)
        self.spell_summons = set()
        self.spell_questcomplete = set()
        for e, s in self.spells.items():
            for i in (1, 2, 3):
                ef = I(s[f"effect{i}"])
                if ef in (24, 59):
                    self.spell_items[I(s[f"effectItemType{i}"])].add(e)
                elif ef in (28, 41, 42, 56, 73, 87, 88, 89, 90, 97, 112):
                    self.spell_summons.add(I(s[f"effectMiscValue{i}"]))
                elif ef == 16:
                    self.spell_questcomplete.add(I(s[f"effectMiscValue{i}"]))
        # spells somebody can cast: items' spells, creature spell lists, EventAI casts
        self.item_spells = defaultdict(set)
        for e, it in self.it.items():
            for k in ("spellid_1", "spellid_2", "spellid_3"):
                if I(it[k]):
                    self.item_spells[I(it[k])].add(e)
        self.ai_creatures = defaultdict(int)
        for r in rows("select creature_id, count(*) n from creature_ai_events group by creature_id"):
            self.ai_creatures[I(r["creature_id"])] = I(r["n"])
        self.escort = {I(r["creature_id"]) for r in rows("select distinct creature_id from script_escort_data")} if True else set()
        self.areas = {I(r["entry"]): r["name"] for r in rows("select entry,name from area_template")}
        self.zone_of_chunk = {}
        with open(AREAS, encoding="utf-8") as f:
            next(f)
            for line in f:
                m, tx, ty, cx, cy, a, z = line.split("\t")
                self.zone_of_chunk[(int(m), int(tx) * 16 + int(cx), int(ty) * 16 + int(cy))] = int(z.strip()) or int(a)
        self.maps = {I(r["entry"]): r["map_name"] for r in rows("select entry,map_name from map_template")}

    # ----------------------------------------------------------------------------------------
    def zone_at(self, m, x, y):
        if m not in (0, 1):
            return "Instance: " + self.maps.get(m, f"map {m}")
        col = int(math.floor((ORIGIN - y) / CHUNK))
        row = int(math.floor((ORIGIN - x) / CHUNK))
        z = self.zone_of_chunk.get((m, col, row))
        return self.areas.get(z, f"map {m} (unknown area)") if z else f"map {m} (unknown area)"

    def reachable(self, spawns):
        return [s for s in spawns if s[0] not in UNREACHABLE_MAPS and s[0] in self.maps]

    def creature_ok(self, e):
        """(ok, how) - a player can meet this creature."""
        if e not in self.ct:
            return False, "no template"
        if self.reachable(self.c_spawn.get(e, [])):
            return True, "spawned"
        if e in self.script_summons or e in self.spell_summons:
            return True, "summoned by script/spell"
        if self.c_spawn.get(e):
            return False, "spawned only on an unreachable map"
        return False, "never spawned"

    def go_ok(self, e):
        if e not in self.gt:
            return False, "no template"
        if self.reachable(self.g_spawn.get(e, [])):
            return True, "spawned"
        if e in self.script_gos:
            return True, "summoned by script"
        return False, "never spawned" if not self.g_spawn.get(e) else "spawned only on an unreachable map"

    def item_sources(self, item, depth=0):
        """Ways to get `item`: list of (kind, id, ok)."""
        src = []
        for table, lid in self._inv.get(item, ()):
            if table == "gameobject_loot_template":
                for g in self.go_loot_owners(lid):
                    src.append(("object", g, self.go_ok(g)[0]))
            elif table == "item_loot_template":
                if depth < 2 and lid != item:
                    src.append(("container", lid, any(s[2] for s in self.item_sources(lid, depth + 1))))
            else:
                col, kind = {"creature_loot_template": ("loot_id", "drop"),
                             "pickpocketing_loot_template": ("pickpocket_loot_id", "pickpocket"),
                             "skinning_loot_template": ("skinning_loot_id", "skin")}[table]
                for c in self.loot_owners(col, lid):
                    src.append((kind, c, self.creature_ok(c)[0]))
        if item in self.fishing_items:
            src.append(("fishing", 0, True))
        for c in self.vendor_items.get(item, ()):
            src.append(("vendor", c, self.creature_ok(c)[0]))
        if item in self.script_items:
            src.append(("script", 0, True))
        for sp in self.spell_items.get(item, ()):
            castable = bool(self.item_spells.get(sp)) or sp in self.trained_or_known
            src.append(("spell", sp, True if castable else None))
        for kind, qe in self._qitems.get(item, ()):
            src.append((kind, qe, True))
        return src

    def build_owner_index(self):
        self._owners = defaultdict(lambda: defaultdict(set))
        for c, t in self.ct.items():
            for col in ("loot_id", "pickpocket_loot_id", "skinning_loot_id"):
                if I(t[col]):
                    self._owners[col][I(t[col])].add(c)
        self._go_owners = defaultdict(set)
        for g, t in self.gt.items():
            ty = I(t["type"])
            lid = I(t["data1"]) if ty in (3, 25) else (I(t["data0"]) if False else 0)
            if lid:
                self._go_owners[lid].add(g)
        self._inv = defaultdict(list)
        for table, d in self.loot.items():
            for lid, items in d.items():
                for it in items:
                    self._inv[it].append((table, lid))
        self._qitems = defaultdict(list)
        for qe, q in self.quests_all.items():
            if I(q["SrcItemId"]):
                self._qitems[I(q["SrcItemId"])].append(("quest-provided", qe))
            for c in [f"RewItemId{i}" for i in range(1, 5)] + [f"RewChoiceItemId{i}" for i in range(1, 7)]:
                if I(q[c]):
                    self._qitems[I(q[c])].append(("quest-reward", qe))
        self.trained_or_known = set()  # spells players learn are not tracked here: treat as unknown

    def loot_owners(self, col, lid):
        return self._owners[col].get(lid, ())

    def go_loot_owners(self, lid):
        return self._go_owners.get(lid, ())

    # ----------------------------------------------------------------------------------------
    def audit_quest(self, qe, q):
        block, risk, notes = [], [], []
        scripts_touched = set()

        def note_script(kind, e):
            if e in self.noscript[kind]:
                scripts_touched.add((kind, e))
                return True
            return False

        # givers
        givers = [("creature", c, self.creature_ok(c)) for c in self.c_start.get(qe, ())]
        givers += [("object", g, self.go_ok(g)) for g in self.g_start.get(qe, ())]
        givers += [("item", e, (any(s[2] for s in self.item_sources(e)), "item")) for e, t in self.it.items()
                   if I(t["start_quest"]) == qe] if qe in self._item_starters else []
        if not givers:
            block.append("no quest giver")
        elif not any(g[2][0] for g in givers):
            block.append("quest giver not reachable (%s)" % "; ".join(f"{k} {e}: {ok[1]}" for k, e, ok in givers))
        for k, e, _ in givers:
            if note_script({"object": "gameobject"}.get(k, k), e):
                risk.append(f"giver {k} {e} had a C++ script ({self.noscript[{'object': 'gameobject'}.get(k, k)][e]})")
        enders = [("creature", c, self.creature_ok(c)) for c in self.c_end.get(qe, ())]
        enders += [("object", g, self.go_ok(g)) for g in self.g_end.get(qe, ())]
        if not enders:
            block.append("no quest ender")
        elif not any(x[2][0] for x in enders):
            block.append("quest ender not reachable (%s)" % "; ".join(f"{k} {e}: {ok[1]}" for k, e, ok in enders))
        for k, e, _ in enders:
            if note_script({"object": "gameobject"}.get(k, k), e):
                risk.append(f"ender {k} {e} had a C++ script ({self.noscript[{'object': 'gameobject'}.get(k, k)][e]})")

        # objectives
        has_kill = False
        for i in range(1, 5):
            t, n, sp = I(q[f"ReqCreatureOrGOId{i}"]), I(q[f"ReqCreatureOrGOCount{i}"]), I(q[f"ReqSpellCast{i}"])
            if sp:
                if sp not in self.spells:
                    block.append(f"objective spell {sp} missing")
                else:
                    s = self.spells[sp]
                    if s["script_name"]:
                        risk.append(f"objective spell {sp} uses spell script {s['script_name']}")
                    if not self.item_spells.get(sp):
                        risk.append(f"objective spell {sp} ({s['name']}) is cast by no item")
            if t > 0 and n:
                has_kill = True
                ok, how = self.creature_ok(t)
                if not ok and -t not in self.script_explored:
                    block.append(f"target creature {t} ({self.ct.get(t, {}).get('name', '?')}): {how}")
                if note_script("creature", t):
                    risk.append(f"target creature {t} had a C++ script ({self.noscript['creature'][t]})")
            elif t < 0 and n:
                ok, how = self.go_ok(-t)
                if not ok:
                    block.append(f"target object {-t} ({self.gt.get(-t, {}).get('name', '?')}): {how}")
                if note_script("gameobject", -t):
                    risk.append(f"target object {-t} had a C++ script ({self.noscript['gameobject'][-t]})")
        for i in range(1, 5):
            it, n = I(q[f"ReqItemId{i}"]), I(q[f"ReqItemCount{i}"])
            if not it or not n:
                continue
            if it not in self.it:
                block.append(f"required item {it} has no template")
                continue
            src = self.item_sources(it)
            if not any(s[2] for s in src):
                if any(s[2] is None for s in src):
                    risk.append(f"required item {it} ({self.it[it]['name']}) only from spell {[s[1] for s in src if s[0]=='spell']}")
                else:
                    block.append(f"required item {it} ({self.it[it]['name']}) has no reachable source"
                                 + (f" [{', '.join(f'{k} {e}' for k, e, _ in src)}]" if src else ""))
        for i in range(1, 5):
            it = I(q[f"ReqSourceId{i}"])
            if it and it not in self.it:
                block.append(f"source item {it} has no template")
        if I(q["SrcItemId"]) and I(q["SrcItemId"]) not in self.it:
            block.append(f"provided item {q['SrcItemId']} has no template")
            if note_script("item", I(q["SrcItemId"])):
                pass
        for i in range(1, 5):
            it = I(q[f"ReqItemId{i}"])
            if it and it in self.noscript["item"]:
                scripts_touched.add(("item", it))
                risk.append(f"required item {it} had an item script ({self.noscript['item'][it]})")
        si = I(q["SrcItemId"])
        if si in self.noscript["item"]:
            scripts_touched.add(("item", si))
            if not any(I(q[f"ReqCreatureOrGOId{i}"]) for i in range(1, 5)) or any(I(q[f"ReqSpellCast{i}"]) for i in range(1, 5)):
                block.append(f"provided item {si} needs its C++ item script ({self.noscript['item'][si]})")
            else:
                risk.append(f"provided item {si} had an item script ({self.noscript['item'][si]})")
        for col in ("SrcSpell", "RewSpell", "RewSpellCast"):
            sp = I(q[col])
            if sp and sp not in self.spells:
                (block if col == "SrcSpell" else risk).append(f"{col} {sp} missing")
        for i in range(1, 5):
            r = I(q[f"RewItemId{i}"])
            if r and r not in self.it:
                risk.append(f"reward item {r} has no template")
        for i in range(1, 7):
            r = I(q[f"RewChoiceItemId{i}"])
            if r and r not in self.it:
                risk.append(f"reward choice item {r} has no template")

        # exploration / event
        if I(q["SpecialFlags"]) & 2:
            done_by = []
            if self.at_end.get(qe):
                done_by.append("area trigger")
            if qe in self.script_explored:
                done_by.append("script command")
            if qe in self.spell_questcomplete:
                done_by.append("spell")
            if any(qe == I(x) for x in self._ai_quest_credit):
                done_by.append("EventAI")
            if not done_by:
                block.append("event/exploration objective with nothing in data to complete it (needed their C++ script)")
            else:
                notes.append("event via " + ", ".join(done_by))
        if any(c in self.escort for k, c, _ in givers if k == "creature") or (
                I(q["SpecialFlags"]) & 2 and any(self.noscript["creature"].get(c, "").find("escort") >= 0 for k, c, _ in givers)):
            notes.append("escort")
        if any("escort" in self.noscript["creature"].get(c, "") for k, c, _ in givers if k == "creature"):
            if I(q["SpecialFlags"]) & 2 and not (self.at_end.get(qe) or qe in self.script_explored):
                pass
            risk.append("escort NPC script missing")

        # condition
        rc = I(q["RequiredCondition"])
        if rc and rc not in self._conds and rc != HIDE_CONDITION:
            block.append(f"RequiredCondition {rc} missing")

        # prerequisites (existence; status propagated later)
        prev = abs(I(q["PrevQuestId"]))
        if prev and prev not in self.quests_all:
            block.append(f"prerequisite quest {prev} missing")
        nxt = I(q["NextQuestInChain"])
        if nxt and nxt not in self.quests_all:
            risk.append(f"next quest in chain {nxt} missing")
        return {"block": block, "risk": risk, "notes": notes, "scripts": scripts_touched, "prev": prev,
                "givers": givers, "enders": enders}

    def zone_of_quest(self, qe, q, res):
        z = I(q["ZoneOrSort"])
        if z > 0 and z in self.areas:
            return self.areas[z]
        for k, e, _ in res["givers"]:
            sp = self.c_spawn.get(e) if k == "creature" else self.g_spawn.get(e) if k == "object" else None
            if sp:
                return self.zone_at(*sp[0])
        for k, e, _ in res["enders"]:
            sp = self.c_spawn.get(e) if k == "creature" else self.g_spawn.get(e)
            if sp:
                return self.zone_at(*sp[0])
        return "(no zone)" if z >= 0 else f"Sort {z}"

    def run(self):
        self.prepare()
        return self.evaluate()

    def prepare(self):
        self.build_owner_index()
        self._item_starters = {I(t["start_quest"]) for t in self.it.values() if I(t["start_quest"])}
        self._conds = {I(r["condition_entry"]) for r in rows("select condition_entry from conditions")}
        self._ai_quest_credit = {I(r["datalong"]) for r in rows("select datalong from creature_ai_scripts where command=7")}

    def evaluate(self, hidden=()):
        res = {}
        for qe, q in self.quests.items():
            res[qe] = self.audit_quest(qe, q)
        # propagate broken prerequisites (a quest whose prerequisite can never be done)
        changed = True
        while changed:
            changed = False
            for qe, r in res.items():
                p = r["prev"]
                if p in res and (res[p]["block"] or p in hidden) and not any(b.startswith("prerequisite") for b in r["block"]):
                    if int(self.quests[qe]["PrevQuestId"]) > 0:  # must have finished it
                        r["block"].append(f"prerequisite quest {p} cannot be completed")
                        changed = True
        for qe, r in res.items():
            r["zone"] = self.zone_of_quest(qe, self.quests[qe], r)
            r["status"] = "broken" if r["block"] else ("risk" if r["risk"] else "ok")
        self.res = res
        return res

    # ----------------------------------------------------------------------------------------
    def rank_scripts(self):
        """Creatures/objects whose C++ script we lack, with an importance tier."""
        quest_rel = defaultdict(set)  # (kind, id) -> quests
        for qe, r in self.res.items():
            for k, e in r["scripts"]:
                quest_rel[(k, e)].add(qe)
            for k, e, _ in r["givers"] + r["enders"]:
                kk = {"object": "gameobject"}.get(k, k)
                if e in self.noscript.get(kk, {}):
                    quest_rel[(kk, e)].add(qe)
        out = []
        for kind in ("creature", "gameobject", "item"):
            for e, name in self.noscript[kind].items():
                if kind == "creature":
                    t = self.ct.get(e, {})
                    sp = self.c_spawn.get(e, [])
                    maps = sorted({s[0] for s in sp})
                    rank = I(t.get("rank"))
                    inst = [m for m in maps if m in INSTANCE_MAPS]
                    if quest_rel.get((kind, e)):
                        tier = "1 quest"
                    elif inst and rank in (1, 3) or rank == 3:
                        tier = "2 boss"
                    elif rank in (2, 4):
                        tier = "3 rare"
                    else:
                        tier = "4 flavour"
                    where = self.zone_at(*sp[0]) if sp else "not spawned"
                    out.append(dict(kind=kind, id=e, name=t.get("name", "?"), script=name, tier=tier, level=t.get("level_max"),
                                    rank=rank, spawns=len(sp), where=where, quests=sorted(quest_rel.get((kind, e), ())),
                                    has_ai=self.ai_creatures.get(e, 0), spell_list=I(t.get("spell_list_id"))))
                elif kind == "gameobject":
                    t = self.gt.get(e, {})
                    sp = self.g_spawn.get(e, [])
                    tier = "1 quest" if quest_rel.get((kind, e)) else "4 flavour"
                    out.append(dict(kind=kind, id=e, name=t.get("name", "?"), script=name, tier=tier, level="", rank="",
                                    spawns=len(sp), where=self.zone_at(*sp[0]) if sp else "not spawned",
                                    quests=sorted(quest_rel.get((kind, e), ())), has_ai=0, spell_list=0))
                else:
                    t = self.it.get(e, {})
                    tier = "1 quest" if quest_rel.get((kind, e)) else "4 flavour"
                    out.append(dict(kind=kind, id=e, name=t.get("name", "?"), script=name, tier=tier, level="", rank="",
                                    spawns="", where="item", quests=sorted(quest_rel.get((kind, e), ())), has_ai=0, spell_list=0))
        out.sort(key=lambda r: (r["tier"], r["where"], str(r["name"])))
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", default=os.path.join(REPO, "design", "COMMUNITY_QUEST_AUDIT.md"))
    ap.add_argument("--json", default=None)
    ap.add_argument("--sql", default=None)
    ap.add_argument("--theirs-port", type=int, default=3399, help="scratch copy of their world DB (for --sql)")
    a = ap.parse_args()
    au = Audit()
    res = au.run()
    sc = au.rank_scripts()
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"quests": {str(k): dict(status=v["status"], zone=v["zone"], block=v["block"], risk=v["risk"],
                                               notes=v["notes"], title=au.quests[k]["Title"], level=au.quests[k]["QuestLevel"])
                                  for k, v in res.items()},
                       "scripts": sc}, f, indent=1)
    if a.sql:
        import community_fixes
        community_fixes.write(au, a.sql, a.theirs_port)
    import audit_report
    audit_report.write(au, sc, a.md)
    n = defaultdict(int)
    for v in res.values():
        n[v["status"]] += 1
    print(dict(n), file=sys.stderr)


if __name__ == "__main__":
    main()
