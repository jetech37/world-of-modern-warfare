#!/usr/bin/env python3
"""Azeroth Warfare: write the seed weapons.json the server loads (<DataDir>/fusion/weapons.json).

These are approximate Modern Warfare 2 multiplayer stats from public community testing, good
enough to play with. Replace them with exact values exported from your own MW2 install once the
exporter is pointed at it (see README.md here). Ranges are MW2 inches, converted to yards (/36).
Unlock ranks are MW2's own; the server scales them to WoW levels (rank 70 -> level 60).
"""
import json
import sys

IN = 1.0 / 36.0

AR_LOC = {"head": 1.4, "neck": 1.0, "torso_upper": 1.0, "torso_lower": 1.0, "arm": 1.0, "leg": 1.0}
SNIPER_LOC = {"head": 1.5, "neck": 1.5, "torso_upper": 1.5, "torso_lower": 1.0, "arm": 1.0, "leg": 1.0}
FLAT_LOC = {"head": 1.0, "neck": 1.0, "torso_upper": 1.0, "torso_lower": 1.0, "arm": 1.0, "leg": 1.0}

# name, class, damage, min_damage, max_range_in, min_range_in, pellets, rpm, mag, reserve,
#   reload, reload_empty, move, ads_move, unlock_rank, starter, locations
W = [
    # Assault rifles
    ("famas_mp", "assault_rifle", 40, 30, 1500, 2600, 1, 900, 30, 120, 2.6, 3.2, 0.95, 0.5, 1, True, AR_LOC),
    ("m4_mp", "assault_rifle", 30, 20, 1500, 2600, 1, 800, 30, 120, 2.2, 2.8, 0.95, 0.5, 4, False, AR_LOC),
    ("scar_mp", "assault_rifle", 40, 30, 1500, 2600, 1, 750, 20, 80, 2.4, 3.0, 0.95, 0.5, 8, False, AR_LOC),
    ("tavor_mp", "assault_rifle", 30, 20, 1500, 2600, 1, 900, 30, 120, 2.4, 3.0, 0.95, 0.5, 20, False, AR_LOC),
    ("fal_mp", "assault_rifle", 55, 35, 1500, 2600, 1, 600, 20, 80, 2.6, 3.2, 0.95, 0.5, 28, False, AR_LOC),
    ("m16_mp", "assault_rifle", 40, 30, 1500, 2600, 1, 900, 30, 120, 2.4, 3.0, 0.95, 0.5, 40, False, AR_LOC),
    ("masada_mp", "assault_rifle", 30, 25, 1500, 2600, 1, 750, 30, 120, 2.4, 3.0, 0.95, 0.5, 48, False, AR_LOC),
    ("fn2000_mp", "assault_rifle", 30, 20, 1500, 2600, 1, 1000, 30, 120, 2.6, 3.2, 0.95, 0.5, 60, False, AR_LOC),
    ("ak47_mp", "assault_rifle", 40, 30, 1500, 2600, 1, 700, 30, 120, 2.6, 3.2, 0.95, 0.5, 70, False, AR_LOC),
    # Submachine guns
    ("ump45_mp", "smg", 40, 35, 700, 1000, 1, 670, 32, 128, 2.2, 2.7, 1.0, 0.6, 1, True, AR_LOC),
    ("mp5k_mp", "smg", 40, 20, 700, 1000, 1, 900, 30, 120, 2.0, 2.6, 1.0, 0.6, 4, False, AR_LOC),
    ("kriss_mp", "smg", 30, 20, 700, 1000, 1, 1200, 33, 132, 2.2, 2.7, 1.0, 0.6, 12, False, AR_LOC),
    ("p90_mp", "smg", 30, 20, 700, 1000, 1, 920, 50, 200, 3.0, 3.4, 1.0, 0.6, 24, False, AR_LOC),
    ("uzi_mp", "smg", 30, 20, 700, 1000, 1, 1100, 32, 128, 2.2, 2.7, 1.0, 0.6, 44, False, AR_LOC),
    # Light machine guns
    ("sa80_mp", "lmg", 40, 30, 1500, 2600, 1, 750, 100, 200, 6.5, 7.5, 0.85, 0.4, 1, True, AR_LOC),
    ("rpd_mp", "lmg", 50, 30, 1500, 2600, 1, 750, 100, 200, 6.5, 7.5, 0.85, 0.4, 4, False, AR_LOC),
    ("mg4_mp", "lmg", 40, 30, 1500, 2600, 1, 900, 100, 200, 6.5, 7.5, 0.85, 0.4, 16, False, AR_LOC),
    ("aug_mp", "lmg", 40, 30, 1500, 2600, 1, 750, 42, 168, 3.0, 3.6, 0.85, 0.4, 32, False, AR_LOC),
    ("m240_mp", "lmg", 50, 30, 1500, 2600, 1, 900, 100, 200, 7.0, 8.0, 0.85, 0.4, 52, False, AR_LOC),
    # Sniper rifles
    ("cheytac_mp", "sniper", 98, 70, 10000, 10000, 1, 50, 5, 20, 3.2, 3.8, 0.9, 0.3, 1, True, SNIPER_LOC),
    ("barrett_mp", "sniper", 98, 70, 10000, 10000, 1, 200, 10, 40, 3.5, 4.2, 0.9, 0.3, 4, False, SNIPER_LOC),
    ("wa2000_mp", "sniper", 70, 50, 10000, 10000, 1, 260, 6, 24, 3.0, 3.6, 0.9, 0.3, 36, False, SNIPER_LOC),
    ("m21_mp", "sniper", 70, 50, 10000, 10000, 1, 300, 10, 40, 3.0, 3.6, 0.9, 0.3, 56, False, SNIPER_LOC),
    # Shotguns (damage per pellet)
    ("spas12_mp", "shotgun", 40, 20, 250, 1000, 8, 70, 8, 32, 0.6, 0.6, 0.95, 0.6, 1, True, FLAT_LOC),
    ("aa12_mp", "shotgun", 25, 10, 250, 1000, 8, 400, 8, 32, 2.8, 3.4, 0.95, 0.6, 12, False, FLAT_LOC),
    ("striker_mp", "shotgun", 25, 10, 250, 1000, 6, 260, 12, 48, 0.5, 0.5, 0.95, 0.6, 34, False, FLAT_LOC),
    ("ranger_mp", "shotgun", 40, 20, 300, 1000, 8, 400, 2, 24, 2.8, 2.8, 0.95, 0.6, 42, False, FLAT_LOC),
    ("m1014_mp", "shotgun", 25, 10, 250, 1000, 6, 200, 4, 32, 0.5, 0.5, 0.95, 0.6, 54, False, FLAT_LOC),
    ("model1887_mp", "shotgun", 35, 15, 250, 1000, 8, 75, 7, 28, 0.6, 0.6, 0.95, 0.6, 67, False, FLAT_LOC),
    # Pistols
    ("usp_mp", "pistol", 40, 20, 300, 800, 1, 400, 12, 48, 1.6, 2.0, 1.0, 0.75, 1, True, AR_LOC),
    ("coltanaconda_mp", "pistol", 50, 35, 300, 800, 1, 180, 6, 24, 2.5, 2.5, 1.0, 0.75, 26, False, AR_LOC),
    ("beretta_mp", "pistol", 40, 20, 300, 800, 1, 450, 15, 60, 1.6, 2.0, 1.0, 0.75, 46, False, AR_LOC),
    ("deserteagle_mp", "pistol", 50, 40, 300, 800, 1, 250, 7, 28, 1.8, 2.2, 1.0, 0.75, 62, False, AR_LOC),
    # Machine pistols
    ("pp2000_mp", "machine_pistol", 30, 20, 400, 900, 1, 950, 20, 80, 2.0, 2.4, 1.0, 0.7, 1, True, AR_LOC),
    ("glock_mp", "machine_pistol", 30, 20, 400, 900, 1, 1100, 18, 72, 2.0, 2.4, 1.0, 0.7, 22, False, AR_LOC),
    ("beretta393_mp", "machine_pistol", 40, 20, 400, 900, 1, 1000, 15, 60, 2.0, 2.4, 1.0, 0.7, 38, False, AR_LOC),
    ("tmp_mp", "machine_pistol", 40, 20, 400, 900, 1, 1100, 15, 60, 2.0, 2.4, 1.0, 0.7, 58, False, AR_LOC),
    # Launchers (direct-hit damage; splash comes from the explosive rules)
    ("at4_mp", "launcher", 150, 150, 10000, 10000, 1, 60, 1, 1, 4.0, 4.0, 0.85, 0.5, 1, True, FLAT_LOC),
    ("m79_mp", "launcher", 150, 150, 10000, 10000, 1, 60, 1, 2, 2.6, 2.6, 0.9, 0.5, 4, False, FLAT_LOC),
    ("stinger_mp", "launcher", 150, 150, 10000, 10000, 1, 60, 1, 1, 4.0, 4.0, 0.85, 0.5, 30, False, FLAT_LOC),
    ("javelin_mp", "launcher", 150, 150, 10000, 10000, 1, 60, 1, 1, 4.0, 4.0, 0.85, 0.5, 50, False, FLAT_LOC),
    ("rpg_mp", "launcher", 150, 150, 10000, 10000, 1, 60, 1, 2, 3.0, 3.0, 0.85, 0.5, 65, False, FLAT_LOC),
    # Riot shield (melee bash)
    ("riotshield_mp", "riot_shield", 50, 50, 50, 50, 1, 60, 1, 0, 0.0, 0.0, 0.8, 0.8, 1, True, FLAT_LOC),
]


def main(path):
    weapons = []
    for i, (name, cls, dmg, mind, maxr, minr, pellets, rpm, mag, res, rl, rle, mv, ads, rank, starter, loc) in enumerate(W, 1):
        weapons.append({
            "id": i, "name": name, "class": cls,
            "damage": dmg, "min_damage": mind,
            "max_damage_range": round(maxr * IN, 2), "min_damage_range": round(minr * IN, 2),
            "pellets": pellets, "fire_time": round(60.0 / rpm, 4),
            "mag_size": mag, "max_reserve": res,
            "reload_time": rl, "reload_empty_time": rle,
            "move_speed_scale": mv, "ads_move_speed_scale": ads,
            "location_multipliers": loc, "unlock_rank": rank, "starter": starter,
        })
    doc = {"version": 1, "source": "seed: approximate MW2 multiplayer stats", "weapons": weapons}
    with open(path, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    print(f"wrote {len(weapons)} weapons to {path}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "weapons.json")
