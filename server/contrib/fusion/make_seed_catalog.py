#!/usr/bin/env python3
"""Azeroth Warfare: write catalog.json (version 2), the content catalog the server and the client
share (MW2 design spec 1.1).

    make_seed_catalog.py weapons.json catalog.json [weapons.tsv models.txt]

weapons.json is the version 1 gun table (ids, classes, unlock ranks, hit-location multipliers
and starters are kept from it). With the two exports from the benilla-mw2 client,

    cargo run --release --example mw2_export > weapons.tsv
    cargo run --release --example mw2_export -- --models > models.txt

each gun takes MW2's own numbers from its weapon definition (damage, ranges, fire time, pellets,
magazine, ammo, reload, move speeds), the attachments it allows and the definition for every
combination from the install's per-combination definitions (m4_acog_silencer_mp), and whether
it takes camos (camo_model: the viewmodel name the camo suffix goes on). Without them it writes the version 1 numbers
with no attachments, so the build never needs an install. Only numbers and asset names are
written: nothing from the install itself.
"""
import csv
import io
import json
import sys

import womw_kit_data

IN = 1.0 / 36.0

# MW2's attachments in its own order, with the install's icon materials and an unlock rank.
# MW2 unlocks attachments by kills with each gun; Azeroth Warfare has no per-gun kills, so each
# unlocks at an MW2 rank instead, spread over the levels like the guns (UnlockLevel scales it).
ATTACHMENTS = [
    ("reflex", "Red Dot Sight", "weapon_attachment_reflex", 4),
    ("silencer", "Silencer", "weapon_attachment_suppressor", 10),
    ("acog", "ACOG Scope", "weapon_attachment_acog", 16),
    ("gl", "Grenade Launcher", "weapon_attachment_m203", 20),
    ("fmj", "FMJ", "weapon_attachment_fmj", 24),
    ("shotgun", "Shotgun", "weapon_attachment_shotgun", 28),
    ("grip", "Grip", "weapon_attachment_grip", 30),
    ("rof", "Rapid Fire", "weapon_attachment_rof", 34),
    ("akimbo", "Akimbo", "weapon_attachment_akimbo", 38),
    ("tactical", "Tactical Knife", "weapon_attachment_tactical", 40),
    ("eotech", "Holographic Sight", "weapon_attachment_eotech", 44),
    ("thermal", "Thermal", "weapon_attachment_thermal", 50),
    ("heartbeat", "Heartbeat Sensor", "weapon_attachment_heartbeat", 56),
    ("xmags", "Extended Mags", "weapon_attachment_xmags", 62),
]
ORDER = {a[0]: i for i, a in enumerate(ATTACHMENTS)}

# MW2's perks (id, tier, name, icon, unlock rank) and deathstreaks (id, name, icon, deaths, rank);
# the ids are the server's Fusion::PerkId. MW2 unlocks perks by rank; these are close to its own.
PERKS = [
    (1, 1, "Marathon", "specialty_marathon", 4),
    (2, 1, "Sleight of Hand", "specialty_fastreload", 1),
    (3, 1, "Scavenger", "specialty_scavenger", 7),
    (4, 1, "Bling", "specialty_bling", 21),
    (5, 1, "One Man Army", "specialty_onemanarmy", 45),
    (6, 2, "Stopping Power", "specialty_bulletdamage", 1),
    (7, 2, "Lightweight", "specialty_lightweight", 1),
    (8, 2, "Hardline", "specialty_hardline", 9),
    (9, 2, "Cold-Blooded", "specialty_coldblooded", 13),
    (10, 2, "Danger Close", "specialty_dangerclose", 29),
    (11, 3, "Commando", "specialty_commando", 1),
    (12, 3, "Steady Aim", "specialty_steadyaim", 1),
    (13, 3, "Scrambler", "specialty_localjammer", 25),
    (14, 3, "Ninja", "specialty_quieter", 33),
    (15, 3, "SitRep", "specialty_bombsquad", 37),
    (16, 3, "Last Stand", "specialty_pistoldeath", 41),
]
DEATHSTREAKS = [
    (21, "Copycat", "specialty_copycat", 4, 1),
    (22, "Painkiller", "specialty_painkiller", 3, 1),
    (23, "Martyrdom", "specialty_grenadepulldeath", 4, 7),
    (24, "Final Stand", "specialty_finalstand", 4, 50),
]

CAMOS = [
    (1, "Woodland", "_woodland"),
    (2, "Desert", "_desert"),
    (3, "Arctic", "_arctic"),
    (4, "Digital", "_digital"),
    (5, "Urban", "_red_urban"),
    (6, "Red Tiger", "_red_tiger"),
    (7, "Blue Tiger", "_blue_tiger"),
    (8, "Fall", "_orange_fall"),
]


def read_tsv(path):
    with io.open(path, encoding="utf-8-sig") as f:
        return {r["name"]: r for r in csv.DictReader(f, delimiter="\t")}


def fire_time(row, table_fire_time):
    """Seconds between shots: the definition's fire time, plus the bolt or pump's rechamber. A gun
    that fires once per pull (fire type 1, or a double barrel) fires as fast as a trigger is
    pulled in MW2; the client repeats while the trigger is held, so those keep the table's
    trigger cadence as a floor. Automatic and burst guns take the definition's own."""
    secs = int(row["fire_ms"]) / 1000.0
    if row["bolt"] == "true":
        secs += int(row["rechamber_ms"]) / 1000.0
    if row["fire_type"] not in ("0", "3") or row["bolt"] == "true":
        secs = max(secs, table_fire_time)
    return round(secs, 4)


def stat_diff(variant, base, table_fire_time):
    """The gameplay numbers a combination's definition changes from its gun's (a silencer's
    shorter ranges, extended mags' magazine, rapid fire's fire time), in catalog units."""
    out = {}
    def put(key, value, base_value):
        if abs(value - base_value) > 1e-6:
            out[key] = value
    put("damage", int(variant["damage"]), int(base["damage"]))
    put("min_damage", int(variant["min_damage"]), int(base["min_damage"]))
    put("max_damage_range", round(float(variant["max_range"]) * IN, 2), round(float(base["max_range"]) * IN, 2))
    put("min_damage_range", round(float(variant["min_range"]) * IN, 2), round(float(base["min_range"]) * IN, 2))
    put("fire_time", fire_time(variant, table_fire_time), fire_time(base, table_fire_time))
    put("mag_size", int(variant["clip"]), int(base["clip"]))
    put("max_reserve", int(variant["max_ammo"]), int(base["max_ammo"]))
    # The reload (akimbo pairs reload slower or quicker than one gun): the server holds a reload
    # to a share of the quickest the client may take (fusion/Reload.h), so it needs each one's.
    put("reload_time", float(variant["reload"]), float(base["reload"]))
    put("reload_empty_time", float(variant["reload_empty"]), float(base["reload_empty"]))
    return out


def camo_stem(row, models):
    """The viewmodel name a camo's suffix goes on (viewmodel_fn2000 + _woodland), or None when the
    gun has no camos. The camo models do not always share the plain model's name (viewmodel_f2000,
    viewmodel_magpul_masada_dust), so the world model's name is tried too."""
    candidates = []
    for model in (row["gun"], "viewmodel_" + row["world"][len("weapon_"):] if row["world"].startswith("weapon_") else "-"):
        if model != "-":
            # The name itself, then without its last word (viewmodel_magpul_masada_dust,
            # viewmodel_steyr_lmg).
            candidates += [model, model.rsplit("_", 1)[0]]
    for c in candidates:
        if c + "_woodland" in models:
            return c
    return None


# MW2's equipment, as weapons the shot message throws or plants (FusionMgr::UseEquipment):
# (id, definition, slot, MW2 unlock rank). The numbers are the definitions'.
EQUIPMENT = [
    (4001, "frag_grenade_mp", "lethal", 1),
    (4002, "semtex_mp", "lethal", 1),
    (4003, "throwingknife_mp", "lethal", 7),
    (4004, "claymore_mp", "lethal", 31),
    (4005, "c4_mp", "lethal", 43),
    (4101, "flash_grenade_mp", "tactical", 1),
    (4102, "concussion_grenade_mp", "tactical", 1),
    (4103, "smoke_grenade_mp", "tactical", 1),
]

# The killstreaks, as weapons the shot message calls in (FusionMgr::CallIn): 3000 + the server's
# StreakId, and what each one's blast is (yards, CoD damage at the centre and the edge).
KILLSTREAKS = [
    (3001, "uav_mp", 0, 0, 0),
    (3004, "sentry_gun_mp", 0, 0, 0),
    (3005, "remotemissile_projectile_mp", 12.0, 600, 300),
    (3007, "harrier_ffar_mp", 8.0, 300, 80),
    (3008, "cobra_player_minigun_mp", 0, 0, 0),
    (3011, "stealth_bomb_mp", 12.0, 500, 150),
    (3012, "cobra_20mm_mp", 3.0, 120, 60),
    (3015, "nuke_mp", 0, 0, 0),
]


def equipment_entry(eid, name, slot, rank, row):
    entry = {
        "id": eid,
        "name": name,
        "class": slot,
        "slot": slot,
        "damage": int(row["damage"]) if row else 0,
        "min_damage": int(row["damage"]) if row else 0,
        "mag_size": max(1, int(row["clip"])) if row else 1,
        "max_reserve": 0,
        "fire_time": 1.0,
        "unlock_rank": rank,
    }
    if row:
        entry.update(explosion(row))
    return entry


# The underbarrel attachments that are weapons of their own (MW2's alternate weapons), and the
# id range each takes: the alternate of gun N is ALTERNATE_IDS[att] + N.
ALTERNATE_IDS = {"gl": 1000, "shotgun": 2000}
ALTERNATE_CLASS = {"gl": "launcher", "shotgun": "shotgun"}


def explosion(row):
    """A definition's splash in catalog units (yards), or {} without one."""
    radius = float(row.get("explosion_radius") or 0)
    if radius <= 0:
        return {}
    return {
        "explosion_radius": round(radius * IN, 2),
        "explosion_inner": int(row["explosion_inner"]),
        "explosion_outer": int(row["explosion_outer"]),
    }


def alternate_entry(gun, att, row, alt_name):
    """The weapon an underbarrel attachment raises (gl_m4_mp, m4_shotgun_attach_mp): its own
    numbers, tied to its gun and attachment. Only a class whose primary carries the attachment
    fires it (FusionMgr::EffectiveWeapon); it is never a Create-a-Class pick (slot alternate)."""
    entry = {
        "id": ALTERNATE_IDS[att] + gun["id"],
        "name": alt_name,
        "class": ALTERNATE_CLASS[att],
        "slot": "alternate",
        "alternate_of": gun["id"],
        "attachment": att,
        "damage": int(row["damage"]),
        "min_damage": int(row["min_damage"]),
        "max_damage_range": round(min(float(row["max_range"]), 10000.0) * IN, 2),
        "min_damage_range": round(min(float(row["min_range"]), 10000.0) * IN, 2),
        "fire_time": round(max(int(row["fire_ms"]) / 1000.0, 0.1), 4),
        "pellets": max(1, int(row["pellets"])),
        "fire_type": int(row["fire_type"]),
        "mag_size": max(1, int(row["clip"])),
        "max_reserve": int(row["max_ammo"]),
        "reload_time": float(row["reload"]),
        "reload_empty_time": float(row["reload_empty"]),
        "move_speed_scale": gun.get("move_speed_scale", 0.95),
        "ads_move_speed_scale": gun.get("ads_move_speed_scale", 0.5),
        "hip_spread_min": round(float(row["hip_min"]), 3),
        "hip_spread_max": round(float(row["hip_max"]), 3),
        "ads_spread": round(float(row["ads_spread"]), 3),
        "unlock_rank": gun.get("unlock_rank", 1),
    }
    entry.update(explosion(row))
    return entry


def attachments_of(base, name):
    """The attachment ids in a variant's name, or None when it is not one of the gun's."""
    stem = base[: -len("_mp")]
    if name == base or not (name.startswith(stem + "_") and name.endswith("_mp")):
        return None
    parts = name[len(stem) + 1: -len("_mp")].split("_")
    if not parts or any(p not in ORDER for p in parts):
        return None
    return sorted(parts, key=ORDER.get)


def main(v1_path, out_path, tsv_path=None, models_path=None):
    v1 = json.load(open(v1_path, encoding="utf-8"))
    rows = read_tsv(tsv_path) if tsv_path else {}
    models = set()
    if models_path:
        with io.open(models_path, encoding="utf-8-sig") as f:
            models = {line.strip() for line in f if line.strip()}

    together = set()
    weapons = []
    for w in v1["weapons"]:
        gun = dict(w)
        row = rows.get(w["name"])
        if row and w["class"] == "launcher":
            gun.update(explosion(row))     # the table's damage, MW2's splash
            # MW2's reload: the client times it from the definition, and the server holds a
            # reload to a share of the same (fusion/Reload.h), so it must not be the table's.
            gun.update({"reload_time": float(row["reload"]), "reload_empty_time": float(row["reload_empty"])})
        if row and w["class"] not in ("launcher", "riot_shield"):
            # Launchers and the shield keep the table's numbers: their definitions carry splash
            # and bash damage, not a bullet's. Move speeds too: the asset layer does not read
            # the definitions' move-speed fields yet (every gun reads 1.0).
            gun.update({
                "damage": int(row["damage"]),
                "min_damage": int(row["min_damage"]),
                "max_damage_range": round(float(row["max_range"]) * IN, 2),
                "min_damage_range": round(float(row["min_range"]) * IN, 2),
                "fire_time": fire_time(row, w["fire_time"]),
                "pellets": max(1, int(row["pellets"])),
                "fire_type": int(row["fire_type"]),
                # MW2's spread cones (degrees): the server holds each shot's to the gun's least.
                "hip_spread_min": round(float(row["hip_min"]), 3),
                "hip_spread_max": round(float(row["hip_max"]), 3),
                "ads_spread": round(float(row["ads_spread"]), 3),
                **explosion(row),
                "mag_size": int(row["clip"]),
                "max_reserve": int(row["max_ammo"]),
                "reload_time": float(row["reload"]),
                "reload_empty_time": float(row["reload_empty"]),
            })
            variants = {}
            variant_stats = {}
            for name in rows:
                atts = attachments_of(w["name"], name)
                if atts:
                    key = "+".join(atts)
                    variants[key] = name
                    # What the combination changes: MW2's own numbers for its definition.
                    diff = stat_diff(rows[name], row, w["fire_time"])
                    if diff:
                        variant_stats[key] = diff
                    for a in atts:
                        for b in atts:
                            if a != b:
                                together.add((a, b))
            allowed = sorted({a for k in variants for a in k.split("+")}, key=ORDER.get)
            gun["attachments"] = allowed
            gun["variants"] = dict(sorted(variants.items()))
            if variant_stats:
                gun["variant_stats"] = dict(sorted(variant_stats.items()))
            stem = camo_stem(row, models)
            gun["camos"] = stem is not None
            if stem:
                gun["camo_model"] = stem
        weapons.append(gun)

    # The underbarrel weapons, each after the guns.
    alternates = []
    for gun in weapons:
        for att in ALTERNATE_IDS:
            variant = gun.get("variants", {}).get(att)
            alt = rows.get(variant, {}).get("alternate", "-") if variant else "-"
            if alt != "-" and alt in rows:
                alternates.append(alternate_entry(gun, att, rows[alt], alt))
    weapons += alternates
    weapons += [equipment_entry(i, n, sl, r, rows.get(n)) for i, n, sl, r in EQUIPMENT]
    weapons += [{"id": i, "name": n, "class": "killstreak", "slot": "killstreak", "fire_time": 1.0,
                 "mag_size": 1, "max_reserve": 0, "unlock_rank": 1,
                 **({"explosion_radius": rad, "explosion_inner": inner, "explosion_outer": outer} if rad else {})}
                for i, n, rad, inner, outer in KILLSTREAKS]

    used = {a for w in weapons for a in w.get("attachments", [])}
    attachments = []
    for att_id, name, icon, rank in ATTACHMENTS:
        if rows and att_id not in used:
            continue
        # Incompatible: never on one gun together (two sights; the launcher and the shotgun).
        incompatible = [b for b, *_ in ATTACHMENTS if b != att_id and b in used
                        and (att_id, b) not in together] if rows else []
        attachments.append({"id": att_id, "name": name, "icon": icon, "unlock_rank": rank,
                            "incompatible": incompatible})

    catalog = {
        "version": 2,
        "source": "MW2 multiplayer weapon definitions (make_seed_catalog.py)" if rows
                  else v1.get("source", "seed"),
        "weapons": weapons,
        "attachments": attachments,
        "camos": [{"id": i, "name": n, "suffix": s} for i, n, s in CAMOS],
        "perks": [{"id": i, "tier": t, "name": n, "icon": ic, "unlock_rank": r}
                  for i, t, n, ic, r in PERKS],
        "deathstreaks": [{"id": i, "tier": 0, "name": n, "icon": ic, "deaths": d, "unlock_rank": r}
                         for i, n, ic, d, r in DEATHSTREAKS],
    }
    # WOMW's own: perk stacking, Pro challenges and the kit window's texts.
    womw_kit_data.apply(catalog)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(catalog, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(f"{out_path}: {len(weapons)} weapons, {len(attachments)} attachments, "
          f"{sum(len(w.get('variants', {})) for w in weapons)} variants")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
