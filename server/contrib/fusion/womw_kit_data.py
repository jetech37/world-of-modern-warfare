#!/usr/bin/env python3
"""World of Modern Warfare's own kit data on top of MW2's catalog (WOMW design, section 3): each
perk's stacking ("stack", "max_stack": how copies in several of a kit's perk slots add up), its Pro
challenge ("pro_kills": kills with the perk in the kit) and the kit window's texts ("effect",
"pro_effect", "stack_effect"), the deathstreaks', attachments' and equipment's "effect" texts.
The server reads stack, max_stack and pro_kills (fusion/Catalog.h); the client shows the texts.

make_seed_catalog.py applies it to every catalog it writes; run it alone to update one in place:

    womw_kit_data.py catalog.json [more.json ...]
"""
import json
import sys

PERKS = {
    1: dict(stack="additive", max_stack=3, pro_kills=150,
            effect="Sprint without tiring. Out of combat you run 5% faster.",
            pro_effect="Out of combat you run another 5% faster.",
            stack_effect="Additive: each copy makes you 5% faster out of combat (up to 15%)."),
    2: dict(stack="diminishing", max_stack=3, pro_kills=150,
            effect="Reload in half the time; spells in your hands cast 15% quicker.",
            pro_effect="Aim down the sights 20% faster.",
            stack_effect="Diminishing: spells cast 15%, 22% then 26% quicker. Reloads stay at half."),
    3: dict(stack="additive", max_stack=3, pro_kills=150,
            effect="Every kill worth experience brings a magazine back for every gun; 25% more gold from bodies you loot.",
            pro_effect="Another 25% more gold from bodies you loot.",
            stack_effect="Additive: each copy adds 25% more gold. Magazines come back once a kill."),
    4: dict(stack="special", max_stack=3, pro_kills=200,
            effect="Rails for one more attachment: two on your primary (MW2's Bling).",
            pro_effect="Your secondary takes a second attachment too.",
            stack_effect=("Special: each copy adds an attachment to the primary, up to four with three Rails; "
                          "never two of one kind (two sights, two underbarrels).")),
    5: dict(stack="none", max_stack=1, pro_kills=120,
            effect="Switch kits even in combat; others wait until combat ends.",
            pro_effect="The field editor is ready again in 30 minutes instead of an hour.",
            stack_effect="Does not stack."),
    6: dict(stack="none", max_stack=1, pro_kills=300,
            effect="Bullets hit 40% harder (not explosives); the weapon in your hands hits 20% harder.",
            pro_effect="The weapon in your hands hits 35% harder.",
            stack_effect="Does not stack: guns are everyone's baseline."),
    7: dict(stack="none", max_stack=1, pro_kills=150,
            effect="Move 12% faster; falls hurt half as much.",
            pro_effect="Fire sooner after a sprint (30% quicker).",
            stack_effect="Does not stack."),
    8: dict(stack="none", max_stack=1, pro_kills=150,
            effect="Killstreaks need one kill fewer; 10% more experience from kills.",
            pro_effect="Deathstreaks need one death fewer.",
            stack_effect="Does not stack."),
    9: dict(stack="none", max_stack=1, pro_kills=150,
            effect="Creatures never call their friends to help against you, and hear your gunfire from less far.",
            pro_effect="Creatures notice you 15% later.",
            stack_effect="Does not stack."),
    10: dict(stack="none", max_stack=1, pro_kills=150,
             effect="Your explosives hit 25% harder.",
             pro_effect="Your killstreaks' air support hits 25% harder too.",
             stack_effect="Does not stack."),
    11: dict(stack="none", max_stack=1, pro_kills=100,
             effect="Your knife lunge reaches 40 yards instead of 20.",
             pro_effect="No falling damage at all.",
             stack_effect="Does not stack."),
    12: dict(stack="none", max_stack=1, pro_kills=150,
             effect="Hip fire spreads 35% less.",
             pro_effect="Jumping no longer widens your hip fire.",
             stack_effect="Does not stack."),
    13: dict(stack="diminishing", max_stack=3, pro_kills=120,
             effect="Creatures' spells at you cast 20% slower.",
             pro_effect="Another 10% slower.",
             stack_effect="Diminishing: 20%, 30% then 35% slower."),
    14: dict(stack="diminishing", max_stack=3, pro_kills=120,
             effect="Creatures notice you at 70% of the distance; your stealth is 3 yards harder to see through.",
             pro_effect="Your stealth is 2 more yards harder to see through.",
             stack_effect="Diminishing: stealth 3, 4.5 then 5.25 yards harder to see. Notice distance does not stack."),
    15: dict(stack="additive", max_stack=3, pro_kills=120,
             effect="See stealthed enemies from 5 yards farther.",
             pro_effect="3 yards farther again.",
             stack_effect="Additive: each copy adds 5 yards."),
    16: dict(stack="none", max_stack=1, pro_kills=100,
             effect="A fatal hit leaves you down with your pistol drawn: fight on while you wait to be revived.",
             pro_effect="You bleed out 10 seconds later.",
             stack_effect="Does not stack."),
}
DEATHSTREAKS = {
    21: "After 4 deaths without a kill, your next life starts with your killer's kit.",
    22: "After 3 deaths without a kill, you take a third of the damage for 10 seconds after you return.",
    23: "After 4 deaths without a kill, you drop a live grenade where you fall.",
    24: "After 4 deaths without a kill, a fatal hit leaves you down with a pistol; hold on 10 seconds and you get back up at half health.",
}

ATT = {
 "reflex": "A red-dot sight: a cleaner sight picture, no zoom.",
 "silencer": "Firing keeps you off enemy maps and carries less far; shorter damage range.",
 "acog": "A 4x scope for longer shots.",
 "gl": "An underbarrel grenade launcher: raise it with the alternate weapon key.",
 "fmj": "Full metal jacket rounds punch through cover.",
 "shotgun": "An underbarrel shotgun: raise it with the alternate weapon key.",
 "grip": "A foregrip that steadies the recoil.",
 "rof": "Rapid fire: the gun cycles faster.",
 "akimbo": "A gun in each hand: twice the magazine and the fire rate, but no aiming down the sights.",
 "tactical": "A tactical knife in the off hand for quicker knife strikes.",
 "eotech": "A holographic sight: a clear sight picture, no zoom.",
 "thermal": "A thermal scope: warm bodies glow, even through smoke.",
 "heartbeat": "A heartbeat sensor that shows nearby enemies while you hold the gun.",
 "xmags": "Extended magazines: more rounds before a reload.",
}
EQ = {
 "frag_grenade_mp": "Cook it, throw it; it bounces and goes off on a timer.",
 "semtex_mp": "Sticks to whatever it hits, then goes off.",
 "throwingknife_mp": "Kills in one hit; walk over it to pick it back up.",
 "claymore_mp": "A tripwire mine that blasts forward when an enemy walks by.",
 "c4_mp": "A charge you place, then set off yourself.",
 "flash_grenade_mp": "Blinds and deafens anyone caught near it.",
 "concussion_grenade_mp": "Slows and staggers anyone caught near it.",
 "smoke_grenade_mp": "A thick smoke screen that blocks sight.",
}
PERK_KEYS = ["stack", "max_stack", "pro_kills", "effect", "pro_effect", "stack_effect"]


def apply(catalog):
    """Add the WOMW kit data to a catalog dict (version 2)."""
    for p in catalog.get("perks", []):
        extra = PERKS.get(p["id"])
        if extra:
            for k in PERK_KEYS:
                p[k] = extra[k]
    for d in catalog.get("deathstreaks", []):
        if d["id"] in DEATHSTREAKS:
            d["effect"] = DEATHSTREAKS[d["id"]]
    for a in catalog.get("attachments", []):
        if a["id"] in ATT:
            a["effect"] = ATT[a["id"]]
    for w in catalog.get("weapons", []):
        if w["name"] in EQ:
            w["effect"] = EQ[w["name"]]
    return catalog


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(apply(data), f, indent=1, ensure_ascii=False)
            f.write("\n")
