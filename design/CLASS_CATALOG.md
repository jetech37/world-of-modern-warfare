# World of Modern Warfare: class catalog

The full design data for the class system of `GAME_DESIGN.md` section 2: for each of the 9 classes, the 3
core abilities (level 1, every build) and the 18 tier options (6 tiers x 3 archetypes), with numbers and an
implementation route. This is a design document; nothing here is built. Spell ids were read from the world
DB (`mangos.spell_template`, latest build row per entry, cross-checked with `skill_line_ability`).

---

## 0. Conventions

**Units**
- **%EH** = percent of the max health of an *equal-level enemy player*. Damage written this way scales with
  level by itself (no ranks), and the same table works against NPCs (their health is scaled per section 10).
  A gun magazine at equal level is roughly 100-150 %EH, so a class hit of 20-30 %EH is "a strong burst".
- **% HP** = percent of the target's own max health (heals), **% own HP** = of the caster (absorbs).
- Intellect / spell power add about +1% magnitude per 10 points (melee abilities: attack power); one table
  in the rules library, section 7. Nothing here touches gun damage.
- Durations against players are shown as `NPC / player` where they differ (vanilla PvP diminishing
  returns still apply).
- Every ability needs the hands up (V), is instant unless a cast time is given, and is limited by its
  cooldown (cd); mana and rage costs are token (vanilla costs zeroed or kept tiny) unless the archetype's
  signature resource says otherwise.

**Implementation routes** (the most important column)
- **A** = an existing vanilla spell used as-is or with server-side tweaks (`spell_template` overrides,
  `spell_mod`, a script). The client already has its name, icon, tooltip and visual in its own DBC.
- **B** = a vanilla spell id borrowed for its name slot, icon and SpellVisual; the behaviour is custom
  server logic (a spell script / aura script keyed on that id).
- **C** = fully custom logic; a donor spell is still given for the visual so the client needs no new DBC
  rows (the custom tooltip text comes from the benilla-mw2 client layer).
- **(ranked)** = the vanilla spell has ranks. The id given is the **highest rank**; the trainer teaches the
  chain once and the server swaps in the highest rank whose `spellLevel` is at or below the character's
  level (on login and on level-up), so the player never buys ranks. Numbers in this document are the
  level-60 values; the server's rank swap plus the %EH scaling handle lower levels.
- Visual ids (`v...`) are `spellVisual1` values, given so the implementer can find look-alikes.

**The class identity pass (2026-10-28).** The %EH numbers below are the catalog's. The game scales
some of them, adds a few identity rules on top, and keeps all of it in one table:
`server/src/fusion/include/fusion/ClassTuning.h`. The rules are:
- Charging through fire on the gap-closers.
- Ambush's sundering.
- The trap's snap.
- Ghost Wolf's bullet cut.
- The hunter's bond with its companion.
- Shatter.
- Atonement for healer-led kits.
- Drain pressure.
- The basic spell's scale by the kit's lead archetype.
- Creature spell power.

`design/BALANCE_GUNS_VS_CLASSES.md` part 5 gives the audit, the duel numbers and the reasons. Each
option's talent tooltip shows its line from the pass (`identity` in `client/assets/class_options.json`).

**Basic spells (outside the 9-slot kit).** Every caster has its class's basic damaging spell from level 1 in
every kit: the priest Smite, the druid Wrath, the warlock Shadow Bolt, the mage Fireball, the shaman Lightning
Bolt and the paladin Exorcism. It is no option and no core ability (the kit editor does not list it): it sits on
the bar right after the three core abilities (slot 4), ranks up with the level like the stock spell, costs no
mana and has a 2 s cooldown (Exorcism keeps its 15 s). Where a tier option is the same spell (the Wizard's
Fireball, the Elementalist's Lightning Bolt), picking the option adds its archetype's part to it.

**Utility spells (outside the 9-slot kit).** A few non-combat class spells stay in the spellbook without
taking a kit slot: they cannot be used in combat (resurrections, conjures, travel). They are listed per
class under "Utility". This is a proposed exception to "at most 9" (open question 1 at the end).

---

## 1. Warrior

Rage stays the warrior's resource (from damage dealt and taken, gun hits included). Signatures:
**Berserker: Frenzy** (a kill, a Whirlwind among 3 enemies, or a Bloodthirst by a 20% chance sends the
warrior into Frenzy for 6 s: +30% move speed and 25% less bullet damage; no counter to keep up, the Fury
resource is gone). **Vanguard: Bulwark** (a raised shield that blocks frontal
bullets; blocked damage becomes rage). **Warlord: Banners** (deployable standards; allies inside their
radius get the banner's effect).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Charge | ability | Rush a target 8-30 yd away, root it 1 s, +15 rage; cd 15 s. Out of combat only in vanilla: tweaked to work in combat. | A | Charge 11578 (ranked; R1 100). Tweak range 30 yd, drop the out-of-combat flag. |
| Core | all | Bloodrage | ability | +20 rage and regain 15% HP over 6 s; cd 45 s. The universal "get back up" button. | A | Bloodrage 2687. Replace the health cost with a heal (script). |
| Core | all | Disarm | ability | Melee range: the target's weapon is knocked aside: it cannot fire or swing for 4 s / 3 s; cd 40 s. Guns are weapons, so this is the warrior's answer to a gunner at arm's length. | A | Disarm 676. Server: the disarm aura also blocks the MW2 fire input for player targets. |
| 10 | Berserker | Bloodthirst | ability | Strike for 18 %EH, heal 10% HP; a 20% chance of Frenzy; cd 6 s. | A | Bloodthirst 23894 (ranked). Talent spell made trainable; heal added by script. |
| 10 | Vanguard | Bulwark | ability | Raise the shield for up to 6 s: 100% of bullet damage in a 100° frontal cone is blocked, you move at 60%; blocked damage gives rage; cd 15 s. Needs a shield. | B | Shield Block 2565 (v3442). Custom aura: frontal-arc bullet absorb in the rules library's hit pipeline. |
| 10 | Warlord | Battle Standard | ability | Plant a banner for 30 s (one at a time): allies within 15 yd regenerate 1.5% HP/s and recover from flinch 50% faster; cd 45 s. | B | Battle Standard 23034 (v6801, summons the BG standard). Custom: aura holder object, radius 15 yd. |
| 20 | Berserker | Intercept | ability | Dash to an enemy up to 30 yd away and stun it 3 s / 2 s; usable in any stance; a Frenzy refunds the cooldown; cd 20 s. | A | Intercept 20617 (ranked). Remove stance requirement. |
| 20 | Vanguard | Chain Hook | ability | Throw a chain at an enemy up to 25 yd away and yank it to your shield (stops 2 yd in front), stunned 1 s; cd 20 s. Pulls snipers out of cover. | B | Magnetic Pull 28337 (v173) for the look (Hooked Net 13608, v3519, is the hunter's Grappling Hook, cast on oneself); custom pull-to-caster (knockback toward caster / jump-to-position on the target). |
| 20 | Warlord | Rallying Cry | ability | Allies within 30 yd heal 15% HP instantly and gain an absorb of 10% HP for 8 s; downed allies in range gain 10 s of bleed-out time; cd 60 s. | B | Battle Shout 25289 (ranked, v246). Custom effect replaces the attack-power buff. |
| 30 | Berserker | Whirlwind | ability | Spin: 14 %EH to every enemy within 8 yd; among 3 or more, Frenzy; cd 10 s. | A | Whirlwind 1680. Remove stance requirement. |
| 30 | Vanguard | Draw Fire | ability | Taunt every enemy within 25 yd for 4 s. NPCs must attack you; players who damage anyone else during it deal 30% less damage to those targets (the "marked" debuff shows on their screen). cd 30 s. | B | Challenging Shout 1161 (v209). NPC part is the vanilla taunt; player part is a custom aura. |
| 30 | Warlord | Demoralizing Shout | ability | Enemies within 20 yd deal 20% less damage and have +50% weapon spread for 10 s; cd 30 s. (Debuffs enemies; never buffs your guns.) | A | Demoralizing Shout 11556 (ranked). Replace the attack-power reduction with damage-done and spread modifiers. |
| 40 | Berserker | Blood Frenzy | passive | Killing blows reset Intercept and heal 10% HP; Frenzy also makes you immune to slows. | B | Flurry 12970 (v2759) as the aura holder; custom proc logic. |
| 40 | Vanguard | Shield Wall | ability | All damage taken -60% for 8 s; Bulwark can be held during it; cd 120 s. | A | Shield Wall 871 (cd 30 min reduced to 2 min, duration 8 s). |
| 40 | Warlord | Intimidating Shout | ability | The target cowers 8 s / 4 s and up to 5 enemies within 10 yd flee 8 s / 4 s; cd 90 s. The "demoralizing fear". | A | Intimidating Shout 5246. cd 3 min to 90 s. |
| 50 | Berserker | Death Wish | ability | 15 s: melee and Berserker abilities +30% damage, immune to fear, and 20% less bullet damage (instead of vanilla's armor penalty); cd 120 s. | A | Death Wish 12328. Replace the armor penalty with damage reduction. |
| 50 | Vanguard | Shield Charge | ability | Charge 20 yd behind a raised Bulwark; enemies in the path are knocked down 2 s and take 8 %EH; cd 25 s. | B | Shield Charge 19131 (v3783). Custom path sweep. |
| 50 | Warlord | War Banners | passive | Up to 3 banners at once, each a different type chosen when planted: **Rally** (as Battle Standard), **Ward** (allies inside take 15% less bullet damage), **Pursuit** (allies inside move 15% faster and sprint 50% longer). | B | Battle Standard 23034/23035/23538/23539 (v6801) as the three summon variants; custom aura per banner. |
| 60 | Berserker | Rampage | ability (capstone, transformation) | 20 s: grow 30%, immune to stun, fear and knockback, Frenzy for all of it, Charge and Intercept have no cooldown, every melee kill heals 30% HP; cd 180 s. | B | Recklessness 1719 (v236) for the look and slot; custom aura (scale, immunities, cd resets). |
| 60 | Vanguard | Unbreakable | ability (capstone) | 12 s: +50% max HP, and Bulwark becomes a 360° bubble 6 yd wide that blocks all bullets fired from outside for every ally inside; cd 180 s. | B | Last Stand 12975 (v2758 on the 12976 effect) for the HP part; dome look from Divine Intervention 19752 (v5402). Custom bullet-block volume. |
| 60 | Warlord | Warlord's Command | ability (capstone) | 20 s: all allies within 40 yd are immune to fear and stun, regenerate 3% HP/s, and downed allies within 15 yd are revived at 30% HP when it starts; cd 300 s. | B | Rallying Cry of the Dragonslayer 22888 (v4230). Custom aura + revive. |

**Utility:** none.

---

## 2. Paladin

Signatures: **Crusader: Holy Power** (Crusader strikes add 1 Holy Power, max 3, max 5 with Zealotry;
finishers spend it all). **Protector: Aegis** (Protector shields record the bullet damage they absorb;
when one breaks or expires it releases a 4 s Consecration at its spot). **Lightwarden: Zenith** (each
Lightwarden heal may bring the sun to **Zenith** for 8 s, heals +50%: Flash of Light 10%, Holy Light 20%,
Holy Shock 15%; the Solar Power resource is gone). Holy Power is one of the three resources left; it
shows as a stacking buff.

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Divine Protection | ability | 8 s: all damage taken -50%; cd 120 s. | A | Divine Protection 5573 (ranked). Change from immunity to 50% reduction. |
| Core | all | Hammer of Justice | ability | Thrown hammer, 20 yd: stun 5 s / 3 s; cd 45 s. | A | Hammer of Justice 10308 (ranked). Range 10 to 20 yd. |
| Core | all | Blessing of Freedom | ability | Self or ally: remove roots and slows, immune to them for 6 s; cd 25 s. | A | Blessing of Freedom 1044. |
| 10 | Crusader | Crusader Strike | ability | Holy strike for 15 %EH, +1 Holy Power; cd 5 s. | A | Crusader Strike 10337 (ranked, a vanilla-era paladin spell left in the DBC at `spellLevel` 99; set the chain's levels 1/14/28/42/56). |
| 10 | Protector | Holy Shield | ability | Self or ally within 30 yd: absorbs bullets up to 20% of your HP for 10 s (Aegis); cd 15 s. | B | Holy Shield 20928 (ranked, v5620). Custom: absorb aura castable on allies. |
| 10 | Lightwarden | Flash of Light | ability | 1 s cast, 40 yd: heal 20% HP; a 10% chance of Zenith; cd 2 s. | A | Flash of Light 19943 (ranked). |
| 20 | Crusader | Judgement | ability (finisher) | 20 yd: holy blast for 8 %EH per Holy Power; at 3 it also stuns 1.5 s; cd 8 s. | A | Judgement 20271 (v5651). Script: needs no seal, spends Holy Power. |
| 20 | Protector | Consecration | ability | Consecrate 8 yd around you for 8 s: enemies inside take 2 %EH/s and move 30% slower; cd 20 s. | A | Consecration 20924 (ranked). Add the slow. |
| 20 | Lightwarden | Holy Light | ability | 1.5 s cast: heal 45% HP; a 20% chance of Zenith; cd 4 s. | A | Holy Light 25292 (ranked). |
| 30 | Crusader | Zealotry | passive | Holy Power cap 5; melee kills grant 2 Holy Power; each Holy Power held gives 4% move speed. | B | Seal of Command 20920 (ranked, v5622) as the permanent aura holder; custom. |
| 30 | Protector | Blessing of Protection | ability | An ally is immune to physical damage, bullets included, for 6 s but cannot fire or swing; cd 180 s. | A | Blessing of Protection 10278 (ranked). Server: the aura blocks the fire input. |
| 30 | Lightwarden | Holy Shock | ability | Instant, 30 yd: heal an ally 20% HP or smite an enemy for 12 %EH; a 15% chance of Zenith; cd 10 s. | A | Holy Shock 20930 (ranked; damage/heal triggers 25902/25903). |
| 40 | Crusader | Divine Storm | ability (finisher) | Spin: 10 %EH per Holy Power to every enemy within 8 yd; allies within 8 yd heal 5% HP per Holy Power; cd 12 s. | B | Holy Wrath 10318 (ranked, v126, a radius holy burst). Custom finisher. |
| 40 | Protector | Blessing of Sacrifice | ability | 12 s: 50% of the damage an ally takes is moved to you (Aegis absorbs apply); cd 30 s. | A | Blessing of Sacrifice 20729 (ranked). Raise the share to 50%. |
| 40 | Lightwarden | Rekindle | ability (battle resurrection) | 2 s cast, in combat: revive a dead or downed ally at 40% HP; cd 600 s. | A | Redemption 20773 (ranked). Allow in combat, cast 10 s to 2 s, add the cd. |
| 50 | Crusader | Repentance | ability | Incapacitate a target 6 s / 4 s (breaks on damage) and gain 2 Holy Power; cd 45 s. | A | Repentance 20066. |
| 50 | Protector | Sacred Ground | passive | Your Consecrations (cast or from Aegis) also give allies inside 20% less bullet damage and stop flinch. | B | Sanctity Aura 20218 (v321) as the aura holder; custom. |
| 50 | Lightwarden | Lay on Hands | ability | Heal an ally to full; a downed ally stands up at full health; cd 300 s. | A | Lay on Hands 10310 (ranked). Remove the mana drain; cd 60 min to 5 min. |
| 60 | Crusader | Avenging Crusade | ability (capstone, transformation) | 20 s: golden wings; Holy Power gain doubled, finishers cost nothing and keep it, immune to slows, 20% less bullet damage; cd 180 s. | B | Divine Favor 20216 (v7424) for the slot and glow; custom aura. |
| 60 | Protector | Bastion of Light | ability (capstone) | 8 s: an 8 yd dome around you stops every bullet fired from outside (allies may fire out); enemies inside are slowed 50%; cd 180 s. | C | Look: Divine Shield 1020 (ranked, v154) on the caster plus Divine Intervention 19752 (v5402). Custom bullet-blocking volume (shared with Oracle and Vanguard capstones). |
| 60 | Lightwarden | Solar Ascension | ability (capstone, transformation) | 20 s: Zenith, every heal also heals 2 more allies within 15 yd for 50%, casts move-castable; cd 180 s. | B | Divine Intervention 19752 (v5402) for the slot; light look from Holy Light 25292 (v2936). Custom aura. |

**Basic spell:** Exorcism 10314 (ranked, first rank at 20; see section 0). Any enemy, not only undead and demons; cd 15 s; below level 20 it hits as hard as the level allows.

**Utility:** Redemption 20773 (ranked; out-of-combat resurrection; Rekindle is the in-combat version).
Summon Warhorse 13819 and Summon Charger 23214 stay as mounts.

---

## 3. Hunter

No beasts at all: Tame Beast, Call/Dismiss/Revive/Feed/Mend Pet, Beast Lore, Bestial Wrath, Intimidation
and the pet skill lines are removed. The core is **the hunt**. Signatures: **Scout: Spotting** (what the
scout looks at through a field glass is outlined for the party). **Trapper: Prepared Ground** (up to 4
hidden traps; a trap that fires also fires every other trap within 6 yd; any trap crossed pings the
party's minimap like a tripwire). **Slayer: Bane** (a marked creature type, or "casters", takes more from
the slayer's melee and abilities).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Mark Prey | ability | Mark one enemy (one at a time, 100 yd): you see it through walls and its trail of tracks for 30 s, and move 15% faster toward it. Killing your prey resets all your cooldowns. cd 10 s. | B | Hunter's Mark 14325 (ranked, v3239) for the debuff and icon. Custom: outline/trail packets to the client layer, directional speed bonus, kill reset. |
| Core | all | Feign Death | ability | Drop as if dead: NPCs lose you, players see a fake death (kill-feed entry, your corpse). Lasts up to 6 s or until you move; cd 30 s. | A | Feign Death 5384. Script: fake kill-feed and corpse flag for players. |
| Core | all | Disengage | ability | Leap 15 yd backward; next 2 s you take 30% less damage; cd 15 s. | B | Disengage 14273 (ranked, v738). Custom self-knockback backward (replaces the threat drop). |
| 10 | Scout | Field Glass | ability (signature) | Hold V to look through a field glass (x3 zoom): every enemy kept under the reticle for 0.5 s is outlined for your party for 8 s; you cannot fire while looking. No cd. | B | Eagle Eye 6197 (v3404) for the slot. Custom zoom (client layer) and spotting packets. |
| 10 | Trapper | Freezing Trap | ability | Place a hidden trap (arms in 2 s, counts toward the 4): the first enemy freezes 10 s / 4 s (damage breaks it); chains to nearby traps; cd 15 s. | A | Freezing Trap 14311 (ranked; gameobject trap). Add the 4-trap limit and chaining in the trap script. |
| 10 | Slayer | Bane | ability (signature) | Mark the creature type of the target (or "caster" against a player with mana): for 60 s your melee and Slayer abilities deal +30% to that type. One Bane at a time; cd 20 s. | B | Track Undead 19884 / Track Humanoids 19883 (v316) for the icon family; custom type-wide aura. |
| 20 | Scout | Flare | ability | Fire a flare to 40 yd: everything within 15 yd for 20 s is revealed (stealth, Camouflage) and spotted for the party; cd 20 s. | A | Flare 1543. Radius 10 to 15 yd, add spotting. |
| 20 | Trapper | Net Launcher | ability | Fire a net up to 30 yd: the target is rooted 5 s / 3 s and cannot sprint for 8 s; cd 25 s. | B | Electrified Net 11820 or Net 6533 (v683, a root). Tweak to a projectile from the player. |
| 20 | Slayer | Throwing Axes | ability | Throw an axe up to 30 yd for 8 %EH and a 30% slow for 3 s; cd 6 s (a Witchfire kill readies it). | B | Throw Axe 16075 (v3962). Custom charges. |
| 30 | Scout | Camouflage | ability | Stand still 2 s to fade into stealth; walking keeps it, firing or sprinting breaks it; spotting shows nothing on enemy radar while camouflaged. Toggle, cd 10 s after it breaks. | B | Dryad Camouflage 5858 (v72, a stealth aura). Custom "still to fade" rule. |
| 30 | Trapper | Tar Trap | ability | Hidden trap: 10 yd of tar for 15 s, enemies inside move 50% slower and cannot sprint or jump; chains; cd 15 s. | A | Frost Trap 13809 (v3302, area slow). Add the no-sprint/no-jump flags; tar look via Immolation Trap Effect 14301 (v46) if the frost look is wrong. |
| 30 | Slayer | Purge | ability | 30 yd: remove 2 magic buffs (class shields and wards included) from the target and silence it 1 s; cd 15 s. | A | Tranquilizing Shot 19801 (v560). Fire as an ability (no bow needed), dispel 2 magic. |
| 40 | Scout | Grappling Hook | ability | Fire a hook at a surface up to 35 yd away and reel yourself to it (roofs, cliffs); cd 20 s. | C | Look: Hooked Net 13608 (v3519) missile. Custom jump-to-point (the server has `JumpTo`) with line-of-sight and map-height checks. |
| 40 | Trapper | Bear Trap | ability | Hidden trap: root 4 s, 3 %EH/s bleed for 6 s, no sprint for 10 s; chains; cd 15 s. | B | Placing Bear Trap 9437 (v353, places a bear trap gameobject). Custom trap trigger. |
| 40 | Slayer | Scatter | ability | 20 yd: the target is disoriented 4 s and silenced 4 s (no class abilities); cd 30 s. | A | Scatter Shot 19503. Add silence; fire as an ability. |
| 50 | Scout | Smoke Pellet | ability | Drop a smoke cloud, 8 yd, 10 s: blocks sight both ways, breaks target locks and spotting outlines; cd 40 s. | B | Smoke Bomb 7964 (v804) / Smoke Cloud 9458 (v1164). Custom vision-blocker volume (also used by Trickster). |
| 50 | Trapper | Noise-maker | ability | Throw a decoy up to 30 yd: for 12 s it plays gunfire and shows as a red dot on enemy radar; NPCs within 30 yd attack it; cd 40 s. Can be placed as a trap (counts toward the 4). | B | Target Dummy 4071 (v74, summons the engineering dummy that NPCs attack). Custom radar ping and sound. |
| 50 | Slayer | Hex Ward | ability | You and allies within 10 yd: absorb 30% HP of class-ability (non-bullet) damage and immunity to the next control effect, 8 s; cd 45 s. | B | Fear Ward 6346 (v343) for the slot; ward look Glyph of Deflection 28773 (v345). Custom school-filtered absorb. |
| 60 | Scout | Overwatch | ability (capstone) | 20 s: every enemy within 100 yd in your field of view is spotted for the party continuously, Mark Prey can hold 3 prey, and each prey kill refreshes Overwatch by 5 s; cd 180 s. | B | Track Hidden 19885 (v316) for the slot; custom. |
| 60 | Trapper | Killing Field | ability (capstone) | Instantly place 4 armed traps in a 10 yd ring (Freezing, Tar, Bear and Net, if known; otherwise Freezing); for 60 s the trap limit is 8 and chain radius 12 yd; cd 180 s. | B | Explosive Trap 14317 (ranked, v3302) for the slot; reuses the trap scripts. |
| 60 | Slayer | Witchfire | ability (capstone) | 30 s: your blade and axes burn (3 %EH/s for 4 s), Bane counts every target, attacks on targets below 25% HP kill them, and each kill readies the axes and heals 15% HP; cd 180 s. | B | Immolation Trap Effect 14301 (v46) for the burn; Flametongue Weapon 16342 (v290) for the blade glow. Custom aura. |

**Utility:** Aspect of the Cheetah 5118 (+30% run speed, breaks and dazes on damage; travel only, like a
mount), Track Humanoids 19883 and the other trackings (minimap only). Aspect of the Pack 13159 is not kept.

---

## 4. Rogue

Energy and combo points are removed; abilities are cooldown-based. The archetypes were reworked on
2026-10-21: the Bard is gone (music is the Minstrelsy profession now) and the **Duelist** took its
place, keeping its option ids (413..463), so a Bard's kit and bought options are the Duelist's,
tier by tier (see "Moving the Bard onto the Duelist" below).

Signatures:
- **Assassin: the Opening.** Leaving stealth (or casting an opener from it) starts a 4 s opening:
  the Assassin's blades and abilities hit twice as hard. The **openers** (Ambush, Cheap Shot,
  Garrote) can only be used from stealth or inside the opening, so the Assassin wins the first
  seconds and is weak once a fight drags on; Distract and Preparation set up the next opening.
- **Trickster: Slip.** Every Trickster ability makes the rogue untargetable for 0.5 s (targets on
  them drop). Poisons, getting back into stealth mid-fight, decoys, pockets and locks.
- **Duelist: Momentum.** A buff over the bar (a stack count, like Holy Power): every melee blow
  adds one (Sinister Strike and a riposte two), each adds 5% to the Duelist's melee damage, up to
  5 (8 with Relentless); two seconds without a blow and it ebbs, a point a second. Few stealth
  tricks: the Duelist fights in the open with both hands, the first-person swings (left and right
  hand) building Momentum. A swing never lunges; only Lunge dashes.

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Stealth | ability | Go invisible, move at 70%; firing, being hit or sprinting breaks it; off radar; cd 6 s after it breaks. | A | Stealth 1787 (ranked). Break-on-fire added in the gun pipeline. |
| Core | all | Sprint | ability | +70% move speed for 8 s, can fire while sprinting this way; cd 60 s. | A | Sprint 11305 (ranked). cd 5 min to 60 s. |
| Core | all | Vanish | ability | Instantly enter Stealth, drop all targeting and slows; cd 120 s. | A | Vanish 1857 (ranked). |
| 10 | Assassin | Ambush | ability (opener) | From stealth or the opening, from behind the target (a Distract lifts that): vanilla Ambush's heavy blow, doubled by the opening; any melee weapon; cd 8 s. | A | Ambush 11269 (ranked). Stealth stance and "behind" lifted (REFIT_ANY_SIDE); the server's opener gate. |
| 10 | Trickster | Deadly Poison | ability | Coat your blades for 60 s: melee blows stack a poison up to 3 times, 1.5 %EH/s each for 12 s; cd 15 s. | B | Deadly Poison V 25351 (the weapon enchant), cast on oneself; the poison is a server DoT. |
| 10 | Duelist | Sinister Strike | ability | A quick blow with the main hand and a second from the off hand (6 %EH): 2 Momentum; cd 5 s. | A | Sinister Strike 11294 (ranked); the second blow custom. |
| 20 | Assassin | Cheap Shot | ability (opener) | From stealth or the opening: stun 4 s / 2.5 s, a second longer from behind; cd 20 s. | A | Cheap Shot 1833. No combo points; durations fitted on the target. |
| 20 | Trickster | Smoke Bomb | ability | 8 yd smoke cloud for 8 s at your feet: blocks sight, breaks target locks; you vanish into it (back into Stealth even in combat, attacks on you stop) and are untargetable 1 s; cd 40 s. | B | Smoke Bomb 7964 (v804). Same vision-blocker volume as the Scout's; Vanish's combat drop. |
| 20 | Duelist | Lunge | ability | Dash at the enemy in the crosshair up to 30 yd away and strike it as you land (12 %EH, 1 Momentum); cd 12 s. The Duelist's only lunge. | B | Charge 22120 (the NPC's, for the look); the dash is a server leap. |
| 30 | Assassin | Garrote | ability (opener) | From stealth or the opening, any side: silence 3 s (4 s from behind) and bleed 2 %EH/s for 18 s; cd 20 s. | A | Garrote 11290 (ranked). "Behind" lifted; silence added (as the later Improved Garrote). |
| 30 | Trickster | Poison Satchel | ability | Coat your blades for 60 s with Crippling Poison (blows slow 50%); pressed again while coated it switches to Mind-numbing Poison (casts take longer) and then Wound Poison (healing taken less); cd 3 s. | B | Crippling Poison 11202 for the slot; the blows put on vanilla's debuffs (3409/11201, 5760/8692/11398, 13218-13224) at the level's rank. |
| 30 | Duelist | Riposte | ability | Raise your guard for 1.5 s: every melee blow from in front is parried (no damage) and struck back for 12 %EH and 2 Momentum. Bullets are no blows; cd 10 s. | B | Riposte 14251 (no parry needed); custom guard. |
| 40 | Assassin | Distract | ability | Throw a distraction 20 yd ahead: creatures within 10 yd of it that are not fighting turn to it for 10 s; for 8 s your openers count as from behind (Ambush from the front). Does not break stealth; cd 30 s. | B | Distract 1725 (v261) for the look; the point is ahead of the crosshair, no cursor. |
| 40 | Trickster | Light Fingers | ability | Lift a hostile humanoid's purse in melee range, in or out of combat, from any side and unstealthed: its pickpocket loot (vanilla's table and coin) goes straight to your bags, once per creature; never from players. While in the kit Pick Lock is instant and Lockpicking counts 50 higher; cd 20 s. | B | Pick Pocket's copy 5167 (921 is Utility); vanilla's pickpocket loot, server-side. Lockpicking +50 is the vanilla Lockpicking aura 9133. |
| 40 | Duelist | Blade Flurry | ability | 12 s: each of your melee blows also strikes the nearest other enemy within 8 yd for half; cd 30 s. | B | Blade Flurry 13877 (v211); the cleave custom (the hands' swings are no auto-attack). |
| 50 | Assassin | Preparation | ability | Stealth, Vanish, Sprint and every Assassin ability ready again: vanish, open again; cd 3 min. | B | Preparation 14185 (v3259). |
| 50 | Trickster | Decoy | ability | Leave a running copy of yourself that sprints forward for 6 s (enemies see your name and gun) while you go invisible for 3 s; cd 45 s. | B | Summon Illusions 17773 (v72, summons a copy). Custom: copy uses the player's display, moves forward. |
| 50 | Duelist | Relentless | passive | Momentum holds up to 8; at 5 or more, 15% fewer bullets hurt you (keep swinging, keep living). | B | Cold Blood 14177 as the passive slot. |
| 60 | Assassin | Deathmark | ability (capstone) | Mark a target for 10 s; when it ends, the target takes again 50% of all damage you dealt it during the mark; if it dies while marked you Vanish and all your cooldowns reset; cd 120 s. | B | Hemorrhage 17348 (ranked, v5119) for the debuff; custom. |
| 60 | Trickster | Hall of Mirrors | ability (capstone) | Create 3 illusions of you for 12 s that mimic your movement and fire blanks; you can swap places with any of them by pressing V again (3 swaps); cd 120 s. | B | Summon Illusions 17773 (v72). Custom mimicry and swap. |
| 60 | Duelist | Adrenaline Rush | ability (capstone) | 15 s: Momentum full and held there, and Lunge has no cooldown; cd 120 s. | B | Adrenaline Rush 13750 (v3260); custom. |

**Utility:** Pick Pocket 921 (vanilla: stealthed, behind, out of combat), Pick Lock 1804
(lockpicking stays: lockboxes, chests and doors by the Lockpicking skill), Disarm Trap 1842.

**Locks and homesteads.** A rogue's picks open vanilla locks only. A homestead's doors and portals
(design/GAME_DESIGN.md section 12, `fusion/Locks.h`) are player property under their owner's lock:
no pick opens them and nothing a rogue does shortens a code lock's wait after wrong guesses. A
4-digit code has 10,000 combinations and the limiter is all that stands between it and a patient
stranger; a class that halved the wait would be a burglary tool against other players' homes,
the one thing locks exist to stop. (A GM-run heist event could unlock a door the usual way, by its
owner or a GM setting it Unlocked.) The homestead pieces are not world objects either, so vanilla's
Pick Lock cannot even target them; `Fusion::Classes::PickOpens` writes the rule down and tests it.

**Moving the Bard onto the Duelist.** Option ids are `class*100 + tier*10 + archetype + 1`; the
Duelist is archetype 3 as the Bard was, so `character_fusion_class` kits and
`character_fusion_class_option` purchases need no change: a Bard's Hand Drum pick is the Duelist's
Sinister Strike, the Lute's Lunge and so on, and a blend keeps its other picks. At login the old
donor spells (the instruments, and the Assassin's Backstab and Kidney Shot and the Trickster's
Shadowstep and Blind) are retired from the spellbook (`RetiredSpells`), and every action-bar
button on one gets the option that took its tier's place when known, else is cleared
(`MigrateRetiredButtons`): Hand Drum to Sinister Strike, Lute to Lunge, Flute to Riposte, Harp to
Blade Flurry, Bagpipes cleared (Relentless is a passive), Thunder Drums to Adrenaline Rush,
Backstab to Ambush, Kidney Shot to Distract, Shadowstep to Deadly Poison, Blind to the Poison
Satchel. Once moved, nothing is left to move: it runs at every login. Assassins and Tricksters keep
their tier choices the same way (the ids are the tier's, the abilities in them changed).

---

## 5. Priest

Signatures: **Shadow: afflictions** (DoTs and Mind Blast; Vampiric Embrace and Mind Control on their
cooldowns). **Cleric: Prayer** (Prayer of Healing heals 20% more). **Oracle: screens** (a Barrier holds
half the priest's health in rounds). Shadow Orbs, Devotion and Foresight are gone.

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Power Word: Shield | ability | Self or ally, 40 yd: absorb 20% of the target's HP for 15 s; Weakened Soul 15 s; cd 6 s. | A | Power Word: Shield 10901 (ranked). |
| Core | all | Desperate Prayer | ability | Heal yourself 30% HP instantly; usable while downed (stands you up at 30%); cd 120 s. | A | Desperate Prayer 19243 (ranked; human/dwarf-only in vanilla, open to all). Downed hook by script. |
| Core | all | Fade | ability | 6 s: NPCs drop you, you vanish from enemy radar and become faintly translucent; cd 30 s. | A | Fade 10942 (ranked). Add radar hide and translucency. |
| 10 | Shadow | Shadow Word: Pain | ability | 40 yd: 1.5 %EH/s for 18 s; cd 3 s. | A | Shadow Word: Pain 10894 (ranked). |
| 10 | Cleric | Flash Heal | ability | 1 s cast: heal 25% HP; cd 2 s. | A | Flash Heal 10917 (ranked). |
| 10 | Oracle | Barrier | ability (signature) | Raise a wall of light up to 20 yd away, 6 yd wide, 3 yd high, for 8 s: stops bullets from the enemy side (allies fire through); it has 50% of your HP; cd 25 s. | C | Look: Power Word: Shield 10901 (v784) / Barrier of Light 16051 (v784). Custom bullet-blocking wall (gameobject + volume check), shared with the Artificer's rune. |
| 20 | Shadow | Mind Blast | ability | 1.5 s cast, 40 yd: 20 %EH; cd 8 s. | A | Mind Blast 10947 (ranked). |
| 20 | Cleric | Renew | ability | 15 s: heal 30% HP over time; cd 4 s. | A | Renew 25315 (ranked). |
| 20 | Oracle | Pain Suppression | ability | An ally takes 40% less damage and no flinch for 8 s; cd 60 s. | B | Inner Focus 14751 (v4372) for the slot and glow; custom ally aura. |
| 30 | Shadow | Vampiric Embrace | ability | 20 s: 25% of your shadow damage heals your party; cd 30 s. | A | Vampiric Embrace 15286. Duration and share tweaked. |
| 30 | Cleric | Prayer of Healing | ability | 2 s cast: party within 30 yd heal 25% HP, 20% more; cd 10 s. | A | Prayer of Healing 25316 (ranked). |
| 30 | Oracle | Psychic Scream | ability | Up to 5 enemies within 8 yd flee 8 s / 4 s; cd 30 s. | A | Psychic Scream 10890 (ranked). |
| 40 | Shadow | Devouring Plague | ability | 40 yd: 2 %EH/s for 24 s, healing you for all of it; jumps to an enemy within 10 yd if the target dies; cd 60 s. | A | Devouring Plague 19280 (ranked; undead-only in vanilla, open to all). Jump by script. |
| 40 | Cleric | Lightwell | ability | Place a Lightwell for 60 s: 5 charges, an ally clicking it (or below 35% HP within 5 yd) heals 30% HP over 6 s; cd 60 s. | A | Lightwell 27871 (ranked). Auto-use rule added. |
| 40 | Oracle | Premonition | passive | An ally within 30 yd who would drop below 15% HP instead gets an absorb of 20% of their HP (once per ally per 60 s). | B | Elune's Grace 19293 (ranked, v7376) look on proc; custom. |
| 50 | Shadow | Mind Control | ability | 2 s cast, 30 yd: control an NPC for 30 s, or a player for 5 s (they walk where you steer and cannot fire); cd 90 s. | A | Mind Control 10912 (ranked). Player variant via a short charm (script caps duration, blocks fire). |
| 50 | Cleric | Spirit of Redemption | passive | When you would go down, become a spirit of light for 12 s: you keep healing at full power and cannot be harmed, then you go down. | A | Spirit of Redemption 20711. Hooked to the downed state instead of death. |
| 50 | Oracle | Silence | ability | 30 yd: silence 5 s / 4 s; cd 40 s. | A | Silence 15487. |
| 60 | Shadow | Shadowform | ability (capstone, transformation) | 25 s: become shadow; 30% less damage taken, every DoT also slows 30%, each kill on an afflicted target spreads its DoTs to 2 nearby enemies; cd 120 s. | A | Shadowform 15473 (v3619). Toggle made a timed form; spread logic by script. |
| 60 | Cleric | Divine Hymn | ability (capstone) | Channel 8 s: allies within 40 yd heal 8% HP per second; downed allies inside at the end stand up at 50%; cd 180 s. | B | Holy Nova 27805 (ranked, v6882) pulses for the look; Prayer of Healing 25316 (v194) as the channel visual. Custom channel. |
| 60 | Oracle | Sanctum | ability (capstone) | 10 s: a 10 yd dome around you; bullets from outside are stopped, allies inside take 50% less of every other damage and do not flinch; cd 180 s. | C | Look: Sanctuary 22850 (v6680) and Divine Intervention 19752 (v5402). Same bullet-blocking volume as Bastion of Light. |

**Basic spell:** Smite 10934 (ranked; see section 0). cd 2 s.

**Utility:** Resurrection 20770 (ranked; out-of-combat rez, also stands a downed ally up from 30 yd).
Levitate 1706 (travel). Power Word: Fortitude, Shadow Protection and Fear Ward buffs are not kept.

---

## 6. Shaman

Signatures: **Elementalist: Static** (each Elementalist spell hit adds Static, max 5; at 5 the next
Lightning Bolt Overloads: it chains to 3 targets and stuns each 1 s; one of the three resources left,
shown as a stacking buff). **Earthbreaker: stone** (an imbued weapon's blows, a slam of a flat 15 %EH;
Earth charges are gone). **Totemist: Totemic
Web** (up to 3 totems; totems within 20 yd of another of yours are linked and their effects +50%).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Ghost Wolf | ability | Become a spirit wolf: +40% speed, usable in combat and indoors, firing breaks it; cd 5 s. | A | Ghost Wolf 2645. Cast time removed. |
| Core | all | Purge | ability | 30 yd: remove 2 magic buffs from an enemy; cd 8 s. | A | Purge 8012 (ranked). |
| Core | all | Reincarnation | passive | When downed, you may stand back up at 40% HP (cd 20 min). No ankh. | A | Reincarnation 20608 (passive) / 21169 (the revive). Hooked to the downed state; no reagent, cd 60 to 20 min. |
| 10 | Elementalist | Lightning Bolt | ability | 1.5 s cast, 40 yd: 15 %EH, +1 Static; cd 2 s. | A | Lightning Bolt 15208 (ranked; player top rank, DB `spellLevel` 56). |
| 10 | Earthbreaker | Stone Fists | ability | Imbue for 60 s: melee hits deal +8 %EH, knock the target back 3 yd; cd 10 s. | B | Rockbiter Weapon 16316 (ranked, v58). Custom weapon-enchant proc. |
| 10 | Totemist | Healing Stream Totem | ability (totem) | Totem for 30 s: allies within 20 yd heal 2% HP/s; cd 20 s. | A | Healing Stream Totem 10463 (ranked). Percent heal instead of flat. |
| 20 | Elementalist | Chain Lightning | ability | 2 s cast: 12 %EH to the target and 2 more within 10 yd, +1 Static per target; cd 8 s. | A | Chain Lightning 10605 (ranked). |
| 20 | Earthbreaker | Stormstrike | ability | Two strikes for 2 x 10 %EH; the target takes 20% more from your abilities for 8 s; cd 10 s. | A | Stormstrike 17364. |
| 20 | Totemist | Earthbind Totem | ability (totem) | 30 s: enemies within 10 yd move 50% slower and cannot sprint; cd 20 s. | A | Earthbind Totem 2484. Add no-sprint. |
| 30 | Elementalist | Frost Shock | ability | 30 yd: 10 %EH and a 50% slow for 8 s, +1 Static; cd 8 s. | A | Frost Shock 10473 (ranked). |
| 30 | Earthbreaker | Earth Shock | ability | 20 yd: 10 %EH, interrupts and locks class abilities 3 s; cd 12 s. | A | Earth Shock 10414 (ranked). |
| 30 | Totemist | Grounding Totem | ability (totem) | 15 s: redirects the next hostile class spell aimed at an ally within 20 yd, then the next thrown grenade or flash within 10 yd lands on the totem; cd 25 s. | A | Grounding Totem 8177. Equipment catch by script. |
| 40 | Elementalist | Elemental Mastery | ability | Your next spell is instant and crits; at 5 Static the Overload also hits 5 targets; cd 60 s. | A | Elemental Mastery 16166. |
| 40 | Earthbreaker | Ground Smash | ability | Slam: enemies within 10 yd knocked down 2 s and take 15 %EH; cd 20 s. | B | Ground Smash 12734 (v2643); War Stomp 20549 (v5719) as alternate look. Custom charge spend. |
| 40 | Totemist | Chain Heal | ability | 2 s cast: heals 30% / 20% / 10% HP over 3 allies, and refreshes the duration of any totem it bounces past; cd 4 s. | A | Chain Heal 10623 (ranked). |
| 50 | Elementalist | Overcharge | passive | Static cap 8; Overload chains to 4 and leaves a 4 s static field (5 yd) that slows 40%. | B | Lightning Shield 10432 (ranked player chain, v0) as the passive slot; look Static Barrier 11841 (v2337). Custom. |
| 50 | Earthbreaker | Stoneskin | passive | 15% less bullet damage. | B | Stoneskin Totem 10408 (ranked, v319) as the aura holder; custom. |
| 50 | Totemist | Windwall Totem | ability (totem) | 20 s: allies within 8 yd take 30% less damage from bullets and projectiles; cd 30 s. | A | Windwall Totem 15112 (ranked). Ranged reduction from flat to percent. |
| 60 | Elementalist | Earthquake | ability (capstone) | 2 s cast, 40 yd: a 15 yd quake for 10 s, enemies inside take 2 %EH/s and are knocked down every 3 s; cd 120 s. | B | Earthquake 19798 (v5424) or Myzrael Earthquake 4938 (v1587, ground shake). Custom area. |
| 60 | Earthbreaker | Avatar of Stone | ability (capstone, transformation) | 20 s: become a stone giant (+30% size, +50% max HP), immune to knockback and slows, every melee hit is a Ground Smash with 1 charge; cd 180 s. | B | Prismatic Form: Earth 4934 (transform into Rumbling Exile, an earth elemental model). Custom aura. |
| 60 | Totemist | Ancestral Circle | ability (capstone) | Place a great totem for 15 s: every totem you own has double effect, totem limit 5, and downed allies within 20 yd stand up at 30% HP when it lands; cd 180 s. | B | Mana Tide Totem 17359 (ranked, v364). Custom totem. |

**Basic spell:** Lightning Bolt 15208 (ranked; see section 0): the Elementalist's tier 1 option is the same spell, and picking it adds Static and Overload.

**Utility:** Ancestral Spirit 20777 (ranked; out-of-combat rez), Astral Recall 556, Water Walking 546,
Water Breathing 131, Far Sight 6196.

---

## 7. Mage

Signatures: **Wizard: Spellweave** (a spell of a different school from the last one deals 30% more; no
counter). **Spellsword: the blade** (blows with a conjured blade; Blade Blink and Frost Nova at flat
strength; Bladecharge is gone). **Artificer: Constructs** (placed
works, up to 3 at once; constructs within 15 yd of each other share their effects).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Blink | ability | Teleport 20 yd forward, break roots and stuns; cd 15 s. | A | Blink 1953. |
| Core | all | Ice Block | ability | Frozen solid for up to 6 s: immune to everything, cannot act; cd 180 s. | A | Ice Block 11958. |
| Core | all | Polymorph | ability | 1.5 s cast, 30 yd: a sheep for 20 s / 6 s, damage breaks it; cd 30 s. | A | Polymorph 12826 (ranked). |
| 10 | Wizard | Fireball | ability (fire) | 2 s cast, 40 yd: 18 %EH and 1 %EH/s for 6 s; cd 2 s. | A | Fireball 25306 (ranked). |
| 10 | Spellsword | Flame Blade | ability | Conjure a blade of fire for 60 s: melee hits deal 12 %EH fire; cd 10 s. | B | Fire Blast 10199 (ranked, v143) for the hit; weapon glow from Flametongue Weapon 16342 (v290). Custom conjured-weapon aura. |
| 10 | Artificer | Warding Rune | ability (construct) | Place a rune up to 20 yd away that raises a 4 yd wide arcane screen for 10 s; the screen stops bullets from the enemy side and has 40% of your HP; cd 20 s. | C | Look: Glyph of Deflection 28773 (v345) / Fire Ward 10225 (v290). Same bullet-blocking wall as the Oracle's Barrier, smaller. |
| 20 | Wizard | Frostbolt | ability (frost) | 1.5 s cast: 14 %EH and a 40% slow for 6 s; cd 2 s. | A | Frostbolt 25304 (ranked). |
| 20 | Spellsword | Blade Blink | ability | Blink to an enemy up to 20 yd away and strike for 20 %EH; cd 15 s. | B | Blink 1953 (v263). Custom blink-to-target strike. |
| 20 | Artificer | Healing Glyph | ability (construct) | Draw a 6 yd glyph for 15 s: allies inside heal 2% HP/s and recover from flinch faster; cd 30 s. | B | Draw Ancient Glyphs 24815 (v7322) for the ground glyph. Custom area heal. |
| 30 | Wizard | Arcane Missiles | ability (arcane) | Channel 3 s: 5 missiles of 6 %EH; cd 6 s. | A | Arcane Missiles 25345 (ranked). |
| 30 | Spellsword | Frost Nova | ability | Enemies within 10 yd frozen 6 s / 3 s, 2 s longer for the Spellsword; cd 20 s. | A | Frost Nova 10230 (ranked). |
| 30 | Artificer | Blink Pads | ability (construct) | Place 2 linked pads up to 40 yd apart for 45 s: any ally stepping on one appears on the other (2 s cooldown per ally); cd 60 s. Counts as one construct. | B | Blink 1953 (v263) on use; Teleport: Moonglade 18960 (v263) / portal look for the pads. Custom area trigger + teleport. |
| 40 | Wizard | Blast Wave | ability (fire) | 10 yd: 15 %EH, knockback 8 yd and a 50% slow 4 s; cd 30 s. | A | Blast Wave 13021 (ranked). Add the knockback. |
| 40 | Spellsword | Mana Shield | ability | Absorb 25% of your HP for 15 s; each melee hit restores 5% of it; cd 25 s. | A | Mana Shield 10193 (ranked). Absorb drawn from the shield pool, not mana. |
| 40 | Artificer | Conjure Supplies | ability | Conjure 4 Arcane Stims for the squad (trade them): using one heals 25% HP and gives +20% speed for 4 s; max 4 in existence; cd 60 s. | A | Conjure Mana Ruby 10054 (v161, item creation). Point it at a new item (custom item_template, client reads item names from the server). |
| 50 | Wizard | Combustion | ability | Your next 3 spells crit and count as a switch of school; cd 90 s. | A | Combustion 11129. |
| 50 | Spellsword | Arcane Edge | passive | A blade blow (20%) or a melee kill readies Arcane Edge for 10 s: your next Wizard spell is instant and deals double. | B | Clearcasting 12536 (v2736) look on proc; Arcane Power 12042 as an alternative slot. Custom. |
| 50 | Artificer | Arcane Familiar | ability (construct) | Summon a floating familiar bound to an ally for 30 s: it takes 30% of the damage that ally receives, and has 50% of your HP; cd 45 s. | B | Summon Elven Wisp 10721 (v353, a small floating companion). Custom damage split. |
| 60 | Wizard | Meteor | ability (capstone) | 2 s cast, 40 yd: a meteor lands after 1.5 s: 60 %EH split among enemies within 8 yd, the ground burns 3 %EH/s for 6 s; cd 90 s. | B | Meteor 24340 (v7479). Custom split damage. |
| 60 | Spellsword | Arcane Avatar | ability (capstone, transformation) | 20 s: blade swings send an arcane wave 15 yd forward (10 %EH), Blink and Blade Blink have no cooldown, 25% less damage taken; cd 180 s. | B | Arcane Power 12042 (v4370). Custom aura. |
| 60 | Artificer | Grand Array | ability (capstone) | 30 s: construct limit 6, constructs placed during it have no cooldown, and every construct pulses an absorb of 8% HP to allies within 8 yd every 3 s; cd 180 s. | B | Evocation 12051 (v2756) for the channel-like glow; Arcane Bubble 9438 (v968) for the pulses. Custom. |

**Basic spell:** Fireball 25306 (ranked; see section 0): the Wizard's tier 1 option is the same spell, and picking it counts it for Spellweave.

**Utility:** Conjure Food and Conjure Water 10140 (ranked), Teleport and Portal spells, Slow Fall 130,
Remove Lesser Curse 475 (out of combat use only).

---

## 8. Warlock

No permanent demons: Summon Imp, Voidwalker, Succubus, Felhunter, Enslave Demon, Health Funnel, Soul Link,
Demonic Sacrifice and the demon skill lines go. Soul shards stay as the class reagent: a killing blow
gives one (max 5). Signatures: **Afflicter: Malediction** (each different affliction on a target is a
stack; at 3 the target is Doomed: your drains heal double and its death spreads them). **Felsworn: the
demon** (Demon Form on its cooldown alone; Demonic Pact a flat 15% less damage; Fel Fury is gone).
**Necromancer: Life Force** (0-100, from kills (+20) and minions dying (+10); pays for raising, Corpse
Explosion and Bone Armor; one of the three resources left, shown as a stacking buff).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Fear | ability | 1.5 s cast, 30 yd: the target flees 15 s / 6 s (damage may break it); cd 20 s. | A | Fear 6215 (ranked). |
| Core | all | Life Tap | ability | Trade 15% HP for 1 soul shard or a full mana bar; cd 10 s. | A | Life Tap 11689 (ranked). Shard grant by script. |
| Core | all | Soulstone | ability | Spend 1 shard to place a soulstone on yourself or an ally for 15 min: when they go down (or die) they may stand up at 40% HP. One active per warlock; cd 15 min. | A | Create Soulstone (Major) 20757 + Soulstone Resurrection 20765. Made a direct buff (no item step) hooked to the downed state. |
| 10 | Afflicter | Corruption | ability | 40 yd: 1.5 %EH/s for 18 s; a Malediction stack; cd 3 s. | A | Corruption 25311 (ranked). Instant. |
| 10 | Felsworn | Fel Hide | ability | 20 s: 20% less bullet damage; cd 30 s. | B | Demon Armor 11735 (ranked, v130). Custom damage reduction. |
| 10 | Necromancer | Raise Skeleton | ability | Spend 20 Life Force: a corpse within 30 yd rises as a skeleton warrior for 60 s (max 3 minions; low damage, draws fire); cd 3 s. | B | Raise Dead 17473 (v4582) for the cast and Raise Dead 17475 (summons Mindless Skeleton). Custom corpse consume and limit. |
| 20 | Afflicter | Curse of Exhaustion | ability | 30 yd: 40% slower, no sprint, slower ADS for 12 s; a Malediction stack; cd 8 s. | A | Curse of Exhaustion 18223 (talent spell). Add no-sprint and ADS-speed modifiers. |
| 20 | Felsworn | Fel Leap | ability | Leap up to 25 yd; landing deals 10 %EH within 6 yd and stuns 1 s; cd 20 s. | B | Felfire 24530 (v143) for the impact; Inferno 1122 landing (v4859) as the larger look. Custom jump. |
| 20 | Necromancer | Corpse Explosion | ability | Spend 15 Life Force: a corpse or your own minion within 30 yd explodes, 20 %EH within 8 yd; cd 6 s. | B | Corpse Explosion 17616 (v4639). Custom corpse target. |
| 30 | Afflicter | Drain Life | ability | Channel 5 s, 30 yd: 3 %EH/s and you heal 100% of it (200% if Doomed); cd 10 s. | A | Drain Life 11700 (ranked). |
| 30 | Felsworn | Hellfire | ability | 10 s: burn enemies within 10 yd for 3 %EH/s while you lose 1% HP/s; you can move; cd 20 s. | A | Hellfire 11684 (ranked). Allow movement (not channelled). |
| 30 | Necromancer | Bone Armor | ability | Spend 20 Life Force: absorb 25% of your HP for 20 s; each living minion adds 5%; cd 20 s. | B | Bone Armor 11445 (v2737, an absorb aura). Custom scaling. |
| 40 | Afflicter | Howl of Terror | ability | Up to 5 enemies within 10 yd flee 8 s / 4 s; cd 40 s. | A | Howl of Terror 17928 (ranked). |
| 40 | Felsworn | Death Coil | ability | 30 yd: horror 3 s, 15 %EH, heal for the damage; cd 45 s. | A | Death Coil 17926 (ranked). |
| 40 | Necromancer | Raise Ghoul | ability | Spend 30 Life Force: a corpse rises as a ghoul for 60 s that leaps 20 yd onto your target and claws (5 %EH/s); minion cap 5; cd 10 s. | B | Raise Dead 17473 look; summon the Ghoul Berserker model (as in 28179). Custom AI (leap). |
| 50 | Afflicter | Soul Siphon | passive | When a target with your afflictions dies, all of them jump to the 2 nearest enemies within 15 yd and you gain a soul shard and 10% HP. | B | Siphon Life 18881 (ranked, v381) as the passive slot. Custom spread. |
| 50 | Felsworn | Demonic Pact | passive | You take 15% less damage of every kind. | B | Soul Link 19028 (v969). Custom damage-taken modifier. |
| 50 | Necromancer | Death's Harvest | passive | Life Force cap 150; minions explode for 10 %EH within 6 yd when they die; kills within 20 yd of a minion give +10 Life Force. | C | Look: Explode 28433 (v7183; Corpse Explosion 17616 is the tier 2 option's, and the trainer sells by spell). Custom. |
| 60 | Afflicter | Curse of Doom | ability (capstone) | 30 yd: after 10 s the target takes 60 %EH unless the curse is purged; if it dies, Doom spreads every affliction to all enemies within 15 yd; a Malediction stack; cd 60 s. | A | Curse of Doom 603 (+ 18662 effect). Delay 60 s to 10 s; spread by script; usable on players. |
| 60 | Felsworn | Demon Form | ability (capstone, transformation) | Become a demon for 30 s: +50% max HP, claws cleave 15 %EH, Fel Leap has no cooldown, immune to fear and slows; cd 180 s. | B | Perm. Illusion FelGuard 8872 (transform) or Perm. Illusion Dreadlord 8321; Inferno 1122 (v4859) for the burst. Custom aura. |
| 60 | Necromancer | Abomination | ability (capstone) | Spend 80 Life Force: stitch an abomination from up to 3 corpses for 45 s: big, slow, tanky, with a Meat Hook (pull an enemy 30 yd, cd 12 s); does not count toward the minion cap; cd 120 s. | B | Summon Type B 28422 (summons Unstoppable Abomination); hook look Hooked Net 13608 (v3519) or Magnetic Pull 28337. Custom summon + creature spell. |

**Basic spell:** Shadow Bolt 25307 (ranked; see section 0). cd 2 s.

**Utility:** Create Healthstone (Major) 11730 (and lower ranks, chosen by level; spends a shard; anyone
can use a healthstone), Ritual of Summoning 698, Unending Breath 5697, Eye of Kilrogg 126 (out of combat
scouting), the Felsteed and Dreadsteed mounts.

---

## 9. Druid

Signatures: **Shapeshifter: Wild Momentum** (each shift into a form grants a 6 s buff specific to that
form; forms never fade on their own; shifting has a 1.5 s global cooldown and no other cost). Each form is
one tier option; its 2-3 form abilities live on the form's own bar (stance bar) and do not take kit
slots. **Grovekeeper: Bloom** (each of your HoTs on an ally adds a Bloom; Swiftmend and Tranquility
consume Blooms for +25% each). **Starcaller: Eclipse** (Moonfire, Faerie Fire and Lunar Strike bring
Eclipse by a 20% chance, a melee blow by 5%: 10 s, melee hits trigger a Starfire, spells cost no
cooldown; the Lunar and Solar counters are gone).

| Slot | Archetype | Name | Type | What it does | Route | Implementation |
|---|---|---|---|---|---|---|
| Core | all | Barkskin | ability | 10 s: 30% less damage taken, usable while stunned, not affected by forms; cd 60 s. | A | Barkskin 22812. |
| Core | all | Entangling Roots | ability | Instant, 30 yd: root 8 s / 4 s, damage after 2 s may break it; cd 20 s. | A | Entangling Roots 9853 (ranked). Instant. |
| Core | all | Travel Form | ability | Become a cheetah: +40% speed, usable in combat and indoors; firing is impossible in form; cd 5 s. | A | Travel Form 783. |
| 10 | Shapeshifter | Cat Form | form | Prowl (stealth), Pounce (stun 3 s from stealth), Rake (10 %EH + bleed). Momentum: +30% speed and next attack crits. | A | Cat Form 768; Prowl 9913, Pounce 9827, Rake 9904 (all ranked). Energy and combo points removed. |
| 10 | Grovekeeper | Rejuvenation | ability | 40 yd: heal 30% HP over 12 s, +1 Bloom; cd 3 s. | A | Rejuvenation 25299 (ranked). |
| 10 | Starcaller | Moonfire | ability | Instant, 40 yd: 8 %EH and 1 %EH/s for 9 s; +1 Solar; cd 4 s. | A | Moonfire 9835 (ranked). |
| 20 | Shapeshifter | Bear Form | form | Maul (12 %EH), Bash (stun 4 s / 3 s, cd 45 s), Growl (taunt). +60% armor in form. Momentum: absorb 15% HP for 6 s. | A | Dire Bear Form 9634 (Bear Form 5487 below 40); Maul 9881, Bash 8983, Growl 6795 (ranked). |
| 20 | Grovekeeper | Regrowth | ability | 1.5 s cast: heal 20% HP and 20% more over 15 s, +1 Bloom; cd 4 s. | A | Regrowth 9858 (ranked). |
| 20 | Starcaller | Lunar Strike | ability | Melee strike with moonlight: 12 %EH arcane and a 5 yd knockback; a 20% chance of Eclipse; cd 6 s. | B | Starfire 25298 (ranked, v1264) impact look on a melee strike. Custom. |
| 30 | Shapeshifter | Stag Form | form | +50% speed; Stag Charge (20 yd, knock the target back 6 yd); Carry (an ally may mount you, as a two-seat mount, and keep firing). Momentum: 4 s immune to slows. | B | Reindeer 21936 (transform to the Mosshoof Runner stag model) on top of Travel Form 783's shapeshift. Charge: Feral Charge 16979 (v5162). Custom passenger seat. |
| 30 | Grovekeeper | Swiftmend | ability | Consume a HoT on an ally: instantly heal 25% HP, +25% per Bloom; cd 12 s. | A | Swiftmend 18562. |
| 30 | Starcaller | Faerie Fire | ability | 40 yd: the target is outlined for your party and cannot stealth or camouflage for 40 s; +1 Solar; cd 10 s. | A | Faerie Fire 9907 (ranked). Outline packet by script. |
| 40 | Shapeshifter | Crow Form | form | Glide from heights, flap up a little (3 short lifts), Spotting from above (enemies below within 40 yd are outlined for the party). Cannot capture or carry objectives. Momentum: 6 s invisible to radar. | B | Owl Form 8153 (transform to an owl/bird model; Jeklik Transform 23966 is a bat alternative). Custom glide via slow fall (Slow Fall 130) and lift impulses with map-height checks. |
| 40 | Grovekeeper | Rebirth | ability (battle resurrection) | 2 s cast, in combat: revive a dead or downed ally at 40% HP; cd 600 s. | A | Rebirth 20748 (ranked). Cast 2 s; no reagent. |
| 40 | Starcaller | Hurricane | ability | Channel 6 s, 30 yd: a 10 yd storm, 2 %EH/s, 40% slow, spread +50% for enemies inside; cd 60 s. | A | Hurricane 17402 (ranked). |
| 50 | Shapeshifter | Spider Form | form | Web Spray (10 yd cone, root 4 s / 2 s), Wall Crawl (climb steep surfaces for 6 s), Venom Bite (12 %EH poison over 6 s, 30% slow). Momentum: next Web Spray is instant and longer. | B | Mar'li Transform 24084 (Mar'li's spider form); Web Spray 12252 (v684). Wall crawl is custom movement with map checks. |
| 50 | Grovekeeper | Tranquility | ability | Channel 8 s: allies within 40 yd heal 6% HP per second, +1% per Bloom on them; cd 180 s. | A | Tranquility 9863 (ranked). |
| 50 | Starcaller | Lunar Grace | passive | Eclipse lasts 15 s; during it 30% of bullets miss you, and killing blows extend it by 3 s. | B | Moonkin Aura 24907 (passive slot); Elune's Grace 19293 (v7376) look. Custom. |
| 60 | Shapeshifter | Worgen Form | form (capstone, transformation) | Frenzy (+40% melee speed 10 s), Rending Leap (25 yd leap, 10 %EH), every melee hit heals 30% of its damage; +30% max HP in form. Momentum: 6 s immune to stun and fear. | B | Arugal's Curse 7621 (turns the target into a worgen) as the model; Thekal Transform 24169 as alternative. Leap: Feral Charge 16979. Custom form bar. |
| 60 | Grovekeeper | Tree of Life | ability (capstone, transformation) | 30 s: become a treant: HoTs +50%, cast while moving, 20% less damage taken, every HoT you cast also lands on the lowest-health ally within 20 yd; cd 180 s. | A | Tree Form 775 (an unused vanilla druid shapeshift still in the data) as the button; its shapeshift carries a stun, so its effects are replaced and the druid takes the Treant's model (display 864) for 30 s. |
| 60 | Starcaller | Starfall | ability (capstone) | 10 s: stars strike up to 2 enemies within 30 yd every second (6 %EH each); Eclipse starts at once; cd 120 s. | B | Starfall 20687 (a druid-family Starfall in the data, v5780). Custom pulses. Moonkin Form 24858 can be the visual skin during it. |

**Form note.** Every form is a real shapeshift (`server/src/fusion/include/fusion/DruidForms.h`). Stag,
Crow, Spider and Worgen take the empty 1.12 SpellShapeshiftForm rows 9-12; the server rewrites their donor
spells into one MOD_SHAPESHIFT aura each and gives them the donor creature's model. The client learns the
forms on fusion channel 7, op 8. Each form has its own action bar page: Cat 1, Bear and Dire Bear 3,
Moonkin 4, Travel 5, Aquatic 6, Stag 7, Crow 8, Spider 9 and Worgen 10. The action button array is
216 slots, not 1.12's 120. Tree of Life is no form: the druid keeps casting in a treant's look. In first person a form has no gun and no human hands; it shows its own
front limbs, in action only, except the worgen's clawed hands (`client/src/forms.rs`,
`client/src/form_limbs.rs`). Wild Momentum is one aura per form, applied by the shapeshift script.

**Basic spell:** Wrath 9912 (ranked; see section 0). cd 2 s.

**Utility:** Aquatic Form 1066, Teleport: Moonglade 18960, Remove Curse 2782 and Abolish Poison 2893 (out
of combat). Mark of the Wild, Thorns and Innervate are not kept.

---

## 10. Resurrection and self-revive, kept

| Spell | Id | Where it lives | Notes |
|---|---|---|---|
| Resurrection | 20770 (ranked) | Priest utility | Out of combat; also stands a downed ally up from 30 yd. |
| Redemption | 20773 (ranked) | Paladin utility; reused as Lightwarden **Rekindle** (tier 40) | Rekindle is the in-combat battle resurrection. |
| Rebirth | 20748 (ranked) | Grovekeeper tier 40 | Battle resurrection. Druids without it have no rez (as in vanilla). |
| Ancestral Spirit | 20777 (ranked) | Shaman utility | Out of combat. |
| Reincarnation | 20608 / 21169 | Shaman core (passive) | Self-revive from downed. |
| Soulstone Resurrection | 20765 (with 20757) | Warlock core (Soulstone) | Self or ally revive from downed or dead. |
| Lay on Hands | 10310 (ranked) | Lightwarden tier 50 | Stands a downed ally up at full. |
| Desperate Prayer | 19243 (ranked) | Priest core | Self stand-up from downed at 30%. |
| Spirit of Redemption | 20711 | Cleric tier 50 | Delays going down. |
| Warlord's Command, Divine Hymn, Ancestral Circle | see tables | Capstones | Area revive of downed allies. |

All of them revive **downed** players (section 8 of the game design) as well as dead ones where noted.
Profession revives (Goblin Jumper Cables and XL: Defibrillate 8342 / 22999, first-aid kits, alchemist stims) are outside
this catalog.

## 11. Untouched

**Racials** stay exactly as in vanilla, with their effects passing through the stat rework (section 7):
- Human: Perception 20600, The Human Spirit 20598, Diplomacy 20599, Sword Specialization 20597, Mace
  Specialization 20864.
- Dwarf: Stoneform 20594, Find Treasure 2481, Frost Resistance 20596, Gun Specialization 20595 (does
  nothing for MW2 guns, per the stat table; flavour only unless a later rule gives it handling).
- Night Elf: Shadowmeld 20580, Quickness 20582, Wisp Spirit 20585, Nature Resistance 20583.
- Gnome: Escape Artist 20589, Expansive Mind 20591, Arcane Resistance 20592, Engineering Specialization
  20593.
- Orc: Blood Fury 20572, Hardiness 20573, Command 20575, Axe Specialization 20574.
- Undead: Will of the Forsaken 7744, Cannibalize 20577, Underwater Breathing 5227, Shadow Resistance
  20579.
- Tauren: War Stomp 20549, Endurance 20550, Cultivation 20552, Nature Resistance 20551.
- Troll: Berserking 20554 (and its class variants 26296/26297), Regeneration 20555, Beast Slaying 20557,
  Throwing Specialization 20558, Bow Specialization 26290.

**Professions and secondary skills** (all spells in their skill lines): Alchemy, Blacksmithing,
Enchanting, Engineering, Leatherworking, Tailoring, Herbalism, Mining, Skinning, Cooking, First Aid,
Fishing, Lockpicking, plus riding and mounts. Their redesign is section 12 of the game design, not here.

**Removed from all classes:** every class spell not named in this catalog (weapon/armor proficiency and
Defense passives stay as hidden system spells).

---

## 12. Trainer prices

Core abilities and utility spells are free and automatic. Each option is bought once at the class trainer;
prices rise by tier so a pure build costs about 135 gold in total and every option of a class about 400.

| Tier | Level | Price per option | 3 options |
|---|---|---|---|
| 1 | 10 | 1 g | 3 g |
| 2 | 20 | 5 g | 15 g |
| 3 | 30 | 12 g | 36 g |
| 4 | 40 | 22 g | 66 g |
| 5 | 50 | 35 g | 105 g |
| 6 | 60 | 60 g | 180 g |
| | | **135 g per pure build** | **405 g per class** |

Implementation: the trainer lists the option's learn spell (`npc_trainer`, `reqlevel` = tier level); the
kit editor checks one option per tier per kit.

## 13. Route summary

Counted over the 27 core abilities and 162 tier options (189 designs; utility spells not counted).

| Class | A | B | C | Total |
|---|---|---|---|---|
| Warrior | 10 | 11 | 0 | 21 |
| Paladin | 14 | 6 | 1 | 21 |
| Hunter | 6 | 14 | 1 | 21 |
| Rogue | 8 | 12 | 1 | 21 |
| Priest | 16 | 3 | 2 | 21 |
| Shaman | 14 | 7 | 0 | 21 |
| Mage | 11 | 9 | 1 | 21 |
| Warlock | 10 | 10 | 1 | 21 |
| Druid | 14 | 7 | 0 | 21 |
| **All** | **103** | **79** | **7** | **189** |

**Shared custom systems** (build once, used by many B/C rows), in suggested order:
1. **Bullet-blocking volumes**: frontal arc (Bulwark), wall (Oracle Barrier, Artificer Warding Rune) and
   dome (Bastion of Light, Sanctum, Unbreakable). One hit-pipeline check in the rules library.
2. **Downed-state hooks** for revives and stand-ups (about a dozen rows).
3. **Outline / spotting packets** to the client layer (Mark Prey, Field Glass, Flare, Overwatch, Faerie
   Fire, Crow Form, Starcaller).
4. **Archetype resources**: only Holy Power, Static and Life Force are left (2026-10-19), each shown as
   a stacking buff (icon, count, tooltip), not a bar; the rest are chances, cooldowns or flat effects.
5. **Vision blockers** (Smoke Pellet, Smoke Bomb).
6. **Temporary minions** (Necromancer, Decoy, Hall of Mirrors, Arcane Familiar, Noise-maker).
7. **New forms** (Stag, Crow, Spider, Worgen) on top of existing shapeshifts.
8. **Player-side effects of vanilla CC**: disarm and protection blocking the fire input, charm and taunt
   variants for players.

## 14. Open questions

1. Utility spells outside the 9-slot kit (rez, travel, conjures): accept the exception, or make them kit
   options?
2. Rogue energy/combo points and warrior stances are removed in this draft; keep any of them?
3. Whether Shapeshifter form abilities (2-3 per form) count against the 9-ability limit (this draft says
   no, like a stance bar).
4. (Measured 2026-10-26, design/BALANCE_GUNS_VS_CLASSES.md: a rifle magazine is about 600 %EH against an
   equal player, not 100-150; ranged class spells now deal 2x through `Fusion.Classes.RangedSpellPower`
   as a stopgap until the table is retuned.) The %EH numbers assume a magazine of roughly 100-150 %EH at equal level; retune once gun TTK numbers are
   final.
5. Druid Rebirth is only in Grovekeeper; give every druid an out-of-combat rez?

## 15. Audit and status (stabilisation pass, 2026-10-11)

Every core ability and option was checked against the world DB's spell rows (`spell_template`,
`spell_chain`, `skill_line_ability`), the server's refit (`Fusion::Classes::Refits`), its custom part
(`game/Fusion/FusionClassAbilities.cpp`) and the hands (`client/src/hands.rs`): the spell exists and
is learned when the option is in the kit; it does what the catalog says; it cannot leave its user in
a state with no way out; its tooltip says what it does; it readies sensibly in a hand.

The audit lives in code: `Fusion::Classes::Specs()` holds each option's status, how its state ends,
its cooldown, what it spends and a note, and `server/src/fusion/tests/ClassAuditTests.cpp` checks
that every option resolves to a spell the server refits, has a status and (unless working) a note,
spends only its archetype's resource within the meter's cap, has a way out (a timer, its button on
the bar again, a channel, a form) and that an unavailable option is never sold or picked. The kit
editor's tooltip shows the note (`client/assets/class_options.json`, made from the catalog and the
audit by `client/scripts/gen_class_options.py`).

**Status:** *working* = does what the catalog says (the note tells a small difference); *partial* =
safe to use and does its core, the note says what is missing; *unavailable* = shown at the trainer
and on the talent page with "(unavailable)", never sold or picked (a GM may still pick it to test).
No option is unavailable after this pass: everything that misbehaved was fixed or cut back to a
safe partial. **Ends** is the way out of what it starts. Durations are the effect's.

**Polish pass (2026-10-12).** 71 of the 81 partial options were finished (**Polished:** in their
rows says what each does now); the ten left partial need client work (Field Glass's zoom, Mark
Prey's trail, the Noise-maker's radar ping, Fade's radar hide, Decoy's name and gun) or are druid
form mechanics left to the form rework (Stag's passenger seat, Crow's lifts, Hurricane's ground
cursor, Spider's wall crawl, Worgen's Frenzy); Crow and Worgen Form got what needs no form work.
The rules behind them are `fusion/ClassEffects.h`, checked by `ClassEffectTests.cpp`; the Arcane
Stim is `server/sql/custom/azeroth_warfare/21_class_options.sql`.

| Class | Working | Partial | Unavailable | Fixed (10-11) | Polished (10-12) |
|---|---|---|---|---|---|
| Warrior | 21 | 0 | 0 | 1 | 10 |
| Paladin | 21 | 0 | 0 | 7 | 6 |
| Hunter | 18 | 3 | 0 | 4 | 10 |
| Rogue | 20 | 1 | 0 | 5 | 4 |
| Priest | 20 | 1 | 0 | 5 | 9 |
| Shaman | 21 | 0 | 0 | 5 | 11 |
| Mage | 21 | 0 | 0 | 6 | 9 |
| Warlock | 21 | 0 | 0 | 4 | 8 |
| Druid | 16 | 5 | 0 | 3 | 4 |
| **All** | **179** | **10** | **0** | **40** | **71** |

#### Warrior

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 101 | Core core | Charge | A | 11578 | working | instant | 15 s | - | Usable in combat. Roots the target 1 s as vanilla Charge stuns it. |
| 102 | Core core | Bloodrage | A | 2687 | working | timed 6 s | 45 s | - | Costs no health: heals 15% over 6 s instead. |
| 103 | Core core | Disarm | A | 676 | working | timed 4 s | 40 s | 20 rage | 4 s on NPCs, 3 s on players; no shot or swing while disarmed (an NPC gunner's too). **Polished:** 4 s on NPCs, 3 s on players; a disarmed player neither fires nor swings, a disarmed NPC gunner stops shooting. |
| 111 | 10 Berserker | Bloodthirst | A | 23894 | working | instant | 6 s | 30 rage | A hit has a 20% chance to send the Berserker into a Frenzy. |
| 112 | 10 Vanguard | Bulwark | B | 2565 | working | timed 6 s | 15 s | 10 rage | Needs a shield. |
| 113 | 10 Warlord | Battle Standard | B | 23034 | working | timed 30 s | 45 s | - |  |
| 121 | 20 Berserker | Intercept | A | 20617 | working | instant | 20 s | 10 rage | A Frenzy refunds it. **Polished:** A Frenzy refunds its cooldown. |
| 122 | 20 Vanguard | Chain Hook | B | 28337 | working | instant | 20 s | - | 30 yd, Magnetic Pull's look. **Fixed:** Was Hooked Net, which an enemy target and the hunter's self-cast hook fought over: now Magnetic Pull, 30 yd. |
| 123 | 20 Warlord | Rallying Cry | B | 25289 | working | timed 8 s | 1 min | 10 rage | A downed ally in range gets 10 s more to be revived instead of the heal. **Polished:** Downed allies in range get 10 s more bleed-out time. |
| 131 | 30 Berserker | Whirlwind | A | 1680 | working | instant | 10 s | 25 rage | Needs a melee weapon. |
| 132 | 30 Vanguard | Draw Fire | B | 1161 | working | timed 6 s | 30 s | 5 rage | NPCs are taunted 6 s (vanilla); players are marked 4 s. |
| 133 | 30 Warlord | Demoralizing Shout | A | 11556 | working | timed 10 s | 30 s | 10 rage | Its cut replaces vanilla's attack power cut, on NPCs and players. **Polished:** Enemy players deal 20% less and their rounds scatter 50% wider; NPCs deal 20% less. |
| 141 | 40 Berserker | Blood Frenzy | B | 12970 | working | passive | - | - | **Polished:** Immune to slows during a Frenzy (the slows on you drop). |
| 142 | 40 Vanguard | Shield Wall | A | 871 | working | timed 10 s | 2 min | - | Needs a shield; lasts 10 s. |
| 143 | 40 Warlord | Intimidating Shout | A | 5246 | working | timed 8 s | 90 s | 25 rage |  |
| 151 | 50 Berserker | Death Wish | A | 12328 | working | timed 15 s | 2 min | 10 rage | No armor cut: +30% in melee, no fear, 20% fewer bullets hurt. **Polished:** No armor cut: +30% melee damage, no fear, 20% fewer bullets hurt. |
| 152 | 50 Vanguard | Shield Charge | B | 19131 | working | instant | 25 s | - | The vanilla charge and knockback; enemies within 2.5 yd of its path are swept. **Polished:** Enemies within 2.5 yd of the path are knocked down 2 s and take 8 %EH; Bulwark is up for the charge. |
| 153 | 50 Warlord | War Banners | B | 23538 | working | passive | - | - | Banners take turns: Rally, Ward, Pursuit. |
| 161 | 60 Berserker | Rampage | B | 1719 | working | timed 20 s | 3 min | - | **Polished:** Immune to stun, fear and knockback while it lasts. |
| 162 | 60 Vanguard | Unbreakable | B | 12975 | working | timed 12 s | 3 min | - | +50% health on Last Stand's buff. **Polished:** +50% max health for 12 s (Last Stand's +30% replaced). |
| 163 | 60 Warlord | Warlord's Command | B | 22888 | working | timed 20 s | 5 min | - | **Polished:** Allies immune to stun and fear for 20 s; downed allies within 15 yd stand up at 30% as it starts. |

#### Paladin

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 201 | Core core | Divine Protection | A | 5573 | working | timed 8 s | 2 min | - | Half damage taken, not immunity. |
| 202 | Core core | Hammer of Justice | A | 10308 | working | timed 5 s | 45 s | - | 5 s on NPCs, 3 s on players. **Polished:** 20 yd; stuns 5 s (NPCs) / 3 s (players). |
| 203 | Core core | Blessing of Freedom | A | 1044 | working | timed 10 s | 25 s | - | Lasts 10 s. |
| 211 | 10 Crusader | Crusader Strike | A | 10337 | working | instant | 5 s | - | Needs a melee weapon; ranks at 10, 22, 34, 46 and 58. **Fixed:** Every paladin had rank 5 (spellLevel 99) from level 10: ranks chained by level (18_class_fixes.sql). |
| 212 | 10 Protector | Holy Shield | B | 20928 | working | timed 10 s | 15 s | - | Needs no shield. **Fixed:** Needed a shield to cast. |
| 213 | 10 Lightwarden | Flash of Light | A | 19943 | working | instant | 2 s | - | **Fixed:** Cast time 1.5 s to the catalog's 1 s. |
| 221 | 20 Crusader | Judgement | A | 20271 | working | instant | 8 s | - | Needs no seal. **Fixed:** Could never be cast: it still wanted a seal (caster aura state). |
| 222 | 20 Protector | Consecration | A | 20924 | working | timed 8 s | 20 s | - | **Polished:** Enemies inside are slowed 30% (Aegis's consecrations too). |
| 223 | 20 Lightwarden | Holy Light | A | 25292 | working | instant | 4 s | - | **Fixed:** Cast time 2.5 s to the catalog's 1.5 s. |
| 231 | 30 Crusader | Zealotry | B | 20920 | working | passive | - | - |  |
| 232 | 30 Protector | Blessing of Protection | A | 10278 | working | timed 6 s | 3 min | - | Lasts 6 s; the ally cannot fire or swing. **Polished:** 6 s; the ally can neither fire nor swing. |
| 233 | 30 Lightwarden | Holy Shock | A | 20930 | working | instant | 10 s | - |  |
| 241 | 40 Crusader | Divine Storm | B | 10318 | working | instant | 12 s | - |  |
| 242 | 40 Protector | Blessing of Sacrifice | A | 20729 | working | timed 12 s | 30 s | - | No buff icon on the ally (a message says it). **Polished:** Half of what the ally takes goes to the paladin (his own shields apply), 12 s. |
| 243 | 40 Lightwarden | Rekindle | A | 20773 | working | instant | 10 min | - | Out of combat it is Redemption, with no cooldown. **Fixed:** Had no cooldown in combat: 10 min. |
| 251 | 50 Crusader | Repentance | A | 20066 | working | timed 6 s | 45 s | - | Humanoids only (vanilla). |
| 252 | 50 Protector | Sacred Ground | B | 20218 | working | passive | - | - |  |
| 253 | 50 Lightwarden | Lay on Hands | A | 10310 | working | instant | 5 min | - | Stands a downed ally up at full health; drains no mana. **Fixed:** Did not stand a downed ally up; drained all mana. |
| 261 | 60 Crusader | Avenging Crusade | B | 20216 | working | timed 20 s | 3 min | - | Divine Favor's glow, not wings. **Polished:** Immune to slows while it lasts. |
| 262 | 60 Protector | Bastion of Light | C | 1020 | working | timed 8 s | 3 min | - |  |
| 263 | 60 Lightwarden | Solar Ascension | B | 19752 | working | timed 20 s | 3 min | - | Its heals still stop to cast. **Polished:** Every Lightwarden heal also heals the two weakest allies within 15 yd of its target for half. |

#### Hunter

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 301 | Core core | Mark Prey | B | 14325 | partial | timed 30 s | 10 s | - | Spotting, speed and the kill reset; no trail yet. |
| 302 | Core core | Feign Death | A | 5384 | working | timed 6 s | 30 s | - | Moving ends it; the enemy players aiming at you see a kill. **Polished:** 6 s at most; the enemy players aiming at the hunter see a kill. |
| 303 | Core core | Disengage | B | 14273 | working | instant | 15 s | - |  |
| 311 | 10 Scout | Field Glass | B | 6197 | partial | instant | 1 s | - | Spots the enemies in a narrow cone ahead on each press; no zoom. **Fixed:** Eagle Eye's 60 s channel held the hunter: no channel now. |
| 312 | 10 Trapper | Freezing Trap | A | 14311 | working | timed 60 s | 15 s | - | Up to 4 traps (8 in a Killing Field); one going off sets off those within 6 yd. **Polished:** Up to 4 traps at once; one going off sets off every other within 6 yd. |
| 313 | 10 Slayer | Bane | B | 19884 | working | timed 60 s | 20 s | - | **Fixed:** Aims at the crosshair's unit (hands). |
| 321 | 20 Scout | Flare | A | 1543 | working | timed 30 s | 20 s | - | Lands on the crosshair's enemy or 30 yd ahead; its vanilla light shows at your feet. **Fixed:** Waited for a ground click the crosshair cannot give: lands at the hunter's feet. **Polished:** Fired at the crosshair's enemy or 30 yd ahead: 20 s, stealth within 15 yd broken, enemies spotted. |
| 322 | 20 Trapper | Net Launcher | B | 6533 | working | timed 10 s | 25 s | - |  |
| 323 | 20 Slayer | Throwing Axes | B | 16075 | working | instant | 6 s | - |  |
| 331 | 30 Scout | Camouflage | B | 5858 | working | toggle | 1 s | - | Off again at a press; after it breaks it waits 10 s. **Polished:** Still 2 s: stealth; walking keeps it; a shot or a sprint breaks it, 10 s before it can be worn again. |
| 332 | 30 Trapper | Tar Trap | A | 13809 | working | timed 60 s | 15 s | - | Jumping is not locked. **Polished:** Its ground forbids sprinting; it chains. |
| 333 | 30 Slayer | Purge | A | 19801 | working | instant | 15 s | - |  |
| 341 | 40 Scout | Grappling Hook | C | 13608 | working | instant | 20 s | - | Reels you to the target, or to the ground 30 yd ahead. **Fixed:** Needed an enemy target (the hook is cast on oneself now). |
| 342 | 40 Trapper | Bear Trap | B | 9437 | working | timed 60 s | 15 s | - | The vanilla bear trap, with the bleed and the sprint lock. **Polished:** Also bleeds 3 %EH/s for 6 s and forbids sprinting 10 s; it chains. |
| 343 | 40 Slayer | Scatter | A | 19503 | working | timed 4 s | 30 s | - |  |
| 351 | 50 Scout | Smoke Pellet | B | 7964 | working | timed 10 s | 40 s | - |  |
| 352 | 50 Trapper | Noise-maker | B | 4071 | partial | timed 15 s | 40 s | - | The vanilla target dummy NPCs attack; no radar ping. |
| 353 | 50 Slayer | Hex Ward | B | 6346 | working | timed 8 s | 45 s | - | **Polished:** The next hostile control effect on each warded ally breaks at once. |
| 361 | 60 Scout | Overwatch | B | 19885 | working | timed 20 s | 3 min | - | **Polished:** Mark Prey holds three prey while it lasts. |
| 362 | 60 Trapper | Killing Field | B | 14317 | working | timed 60 s | 3 min | - | The ring is Freezing, Tar and Bear Traps as bought; Freezing for the rest. **Polished:** Four armed traps in a ring; for 60 s the limit is 8 and the chain reaches 12 yd. |
| 363 | 60 Slayer | Witchfire | B | 14301 | working | timed 30 s | 3 min | - | **Polished:** Blade hits and axes burn 3 %EH/s for 4 s. |

#### Rogue

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 401 | Core core | Stealth | A | 1787 | working | toggle | 6 s | - | Not usable in combat (Vanish is). |
| 402 | Core core | Sprint | A | 11305 | working | timed 15 s | 1 min | - | Lasts 15 s. |
| 403 | Core core | Vanish | A | 1857 | working | instant | 2 min | - |  |
| 411 | 10 Assassin | Ambush | A | 11269 | working | instant | 8 s | - | An opener: from stealth or the opening, from behind (Distract lifts that); any weapon. |
| 412 | 10 Trickster | Deadly Poison | B | 25351 | working | timed 60 s | 15 s | - | Blows stack a poison (up to 3), 1.5 %EH/s each, 12 s. |
| 413 | 10 Duelist | Sinister Strike | A | 11294 | working | instant | 5 s | - | A second blow from the off hand; 2 Momentum. |
| 421 | 20 Assassin | Cheap Shot | A | 1833 | working | timed 4 s | 20 s | - | An opener; 4 s / 2.5 s, a second more from behind. |
| 422 | 20 Trickster | Smoke Bomb | B | 7964 | working | timed 8 s | 40 s | - | You restealth in the smoke, even in combat; untargetable 1 s. |
| 423 | 20 Duelist | Lunge | B | 22120 | working | instant | 12 s | - | A dash at the crosshair's enemy, then a blow (the Duelist's only lunge). |
| 431 | 30 Assassin | Garrote | A | 11290 | working | timed 18 s | 20 s | - | An opener, from any side; the silence a second longer from behind. |
| 432 | 30 Trickster | Poison Satchel | B | 11202 | working | timed 60 s | 3 s | - | Pressed again it switches: Crippling, Mind-numbing, Wound. |
| 433 | 30 Duelist | Riposte | B | 14251 | working | timed 1.5 s | 10 s | - | Melee blows on you while it holds are parried and struck back. |
| 441 | 40 Assassin | Distract | B | 1725 | working | timed 8 s | 30 s | - | Creatures near the point turn to it; your openers count as from behind for 8 s. |
| 442 | 40 Trickster | Light Fingers | B | 5167 | working | instant | 20 s | - | Pickpocket loot straight to your bags, in combat too; Pick Lock instant and +50 skill. |
| 443 | 40 Duelist | Blade Flurry | B | 13877 | working | timed 12 s | 30 s | - | Each blow also strikes another enemy within 8 yd for half. |
| 451 | 50 Assassin | Preparation | B | 14185 | working | instant | 3 min | - | Readies Stealth, Vanish, Sprint and every Assassin ability. |
| 452 | 50 Trickster | Decoy | B | 17773 | partial | timed 6 s | 45 s | - | The copy runs; you are unseen 3 s. |
| 453 | 50 Duelist | Relentless | B | 14177 | working | passive | - | - | Momentum holds 8; at 5 or more, 15% fewer bullets hurt. |
| 461 | 60 Assassin | Deathmark | B | 17348 | working | timed 10 s | 2 min | - |  |
| 462 | 60 Trickster | Hall of Mirrors | B | 24681 | working | timed 12 s | 2 min | - | The copies follow you and fire nothing; the button swaps. **Polished:** Pressed again it swaps you with a copy (the crosshair's), three swaps; the cooldown starts at its end. |
| 463 | 60 Duelist | Adrenaline Rush | B | 13750 | working | timed 15 s | 2 min | - | Momentum full and held; Lunge has no cooldown. |

#### Priest

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 501 | Core core | Power Word: Shield | A | 10901 | working | timed 30 s | 6 s | - |  |
| 502 | Core core | Desperate Prayer | A | 19243 | working | instant | 2 min | - | Usable while downed: stands you up. **Fixed:** Could not be cast while downed. |
| 503 | Core core | Fade | A | 10942 | partial | timed 10 s | 30 s | - | Vanilla Fade; no radar hide yet. |
| 511 | 10 Shadow | Shadow Word: Pain | A | 10894 | working | timed 18 s | 3 s | - |  |
| 512 | 10 Cleric | Flash Heal | A | 10917 | working | instant | 2 s | - | **Fixed:** Cast time 1.5 s to the catalog's 1 s. |
| 513 | 10 Oracle | Barrier | C | 16051 | working | timed 8 s | 25 s | - | Breaks after taking half your health in rounds. **Polished:** The wall breaks after half your health in rounds. |
| 521 | 20 Shadow | Mind Blast | A | 10947 | working | instant | 8 s | - |  |
| 522 | 20 Cleric | Renew | A | 25315 | working | timed 15 s | 4 s | - |  |
| 523 | 20 Oracle | Pain Suppression | B | 14751 | working | timed 8 s | 1 min | - | **Fixed:** Aims at the crosshair's ally (hands). |
| 531 | 30 Shadow | Vampiric Embrace | A | 15286 | working | timed 20 s | 30 s | - | On the priest, not a debuff on the target. **Polished:** 20 s: a quarter of your shadow damage heals the party. |
| 532 | 30 Cleric | Prayer of Healing | A | 25316 | working | instant | 10 s | - | **Polished:** Heals 20% more. |
| 533 | 30 Oracle | Psychic Scream | A | 10890 | working | timed 8 s | 30 s | - |  |
| 541 | 40 Shadow | Devouring Plague | A | 19280 | working | timed 24 s | 1 min | - | Jumps when you land the killing blow. **Polished:** Jumps to an enemy within 10 yd when its target dies. |
| 542 | 40 Cleric | Lightwell | A | 27871 | working | timed 60 s | 1 min | - | Heals by itself its first minute; clicking it works its vanilla 3 min. **Polished:** 60 s: an ally below 35% within 5 yd is healed 30% over 6 s by itself (5 charges). |
| 543 | 40 Oracle | Premonition | B | 19293 | working | passive | - | - |  |
| 551 | 50 Shadow | Mind Control | A | 10912 | working | channel | 90 s | - | A player is held 5 s and cannot fire; an NPC 30 s. **Fixed:** Held a player for the whole 60 s channel: 5 s. **Polished:** An NPC is held 30 s; a held player cannot fire. |
| 552 | 50 Cleric | Spirit of Redemption | A | 20711 | working | passive | - | - | Once every 2 min. **Polished:** Would go down: a spirit 12 s (unharmed, still healing), then down; once every 2 min. |
| 553 | 50 Oracle | Silence | A | 15487 | working | timed 5 s | 40 s | - |  |
| 561 | 60 Shadow | Shadowform | A | 15473 | working | timed 25 s | 2 min | - | Spreads when you land the killing blow. **Fixed:** Shadow Word: Pain, Mind Blast and the other shadow spells could not be cast in it (the refit dropped the form they allow). **Polished:** 25 s, 30% less damage, DoTs slow 30%, a kill on an afflicted target spreads its DoTs to 2 and gives an orb. |
| 562 | 60 Cleric | Divine Hymn | B | 27805 | working | timed 8 s | 3 min | - | Not a channel: moving does not stop it. **Polished:** Downed allies within 40 yd stand up at 50% when it ends. |
| 563 | 60 Oracle | Sanctum | C | 22850 | working | timed 10 s | 3 min | - |  |

#### Shaman

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 701 | Core core | Ghost Wolf | A | 2645 | working | toggle | 5 s | - |  |
| 702 | Core core | Purge | A | 8012 | working | instant | 8 s | - |  |
| 703 | Core core | Reincarnation | A | 20608 | working | passive | - | - | Stands you up 2 s after going down; the vanilla one (an Ankh) still works on death. **Polished:** Down: up again at 40% 2 s later; 20 min; no Ankh. |
| 711 | 10 Elementalist | Lightning Bolt | A | 15208 | working | instant | 2 s | - | Every shaman knows Lightning Bolt; this adds Static and Overload. **Fixed:** Cast time 3 s to the catalog's 1.5 s. |
| 712 | 10 Earthbreaker | Stone Fists | B | 16316 | working | timed 60 s | 10 s | - | **Polished:** Melee hits knock the target back 3 yd. |
| 713 | 10 Totemist | Healing Stream Totem | A | 10463 | working | timed 30 s | 20 s | - | **Polished:** 30 s: allies within 20 yd regenerate 2% a second (Totemic Web +50%, doubled in the circle). |
| 721 | 20 Elementalist | Chain Lightning | A | 10605 | working | instant | 8 s | - | **Fixed:** Cast time 2.5 s to the catalog's 2 s. |
| 722 | 20 Earthbreaker | Stormstrike | A | 17364 | working | timed 12 s | 10 s | - |  |
| 723 | 20 Totemist | Earthbind Totem | A | 2484 | working | timed 30 s | 20 s | - | **Polished:** 30 s: enemy players within 10 yd cannot sprint. |
| 731 | 30 Elementalist | Frost Shock | A | 10473 | working | timed 8 s | 8 s | - |  |
| 732 | 30 Earthbreaker | Earth Shock | A | 10414 | working | instant | 12 s | - | **Polished:** Locks class abilities 3 s. |
| 733 | 30 Totemist | Grounding Totem | A | 8177 | working | timed 15 s | 25 s | - | Catches one grenade or flash going off within 10 yd. **Polished:** 15 s; catches the next enemy grenade or flash going off within 10 yd. |
| 741 | 40 Elementalist | Elemental Mastery | A | 16166 | working | timed 15 s | 1 min | - |  |
| 742 | 40 Earthbreaker | Ground Smash | B | 12734 | working | instant | 20 s | - |  |
| 743 | 40 Totemist | Chain Heal | A | 10623 | working | instant | 4 s | - | **Fixed:** Cast time 2.5 s to the catalog's 2 s. **Polished:** Renews any of your totems within 20 yd of its target. |
| 751 | 50 Elementalist | Overcharge | B | 10432 | working | passive | - | - | **Polished:** An Overload leaves a 5 yd static field for 4 s, 40% slower. |
| 752 | 50 Earthbreaker | Stoneskin | B | 10408 | working | passive | - | - |  |
| 753 | 50 Totemist | Windwall Totem | A | 15112 | working | timed 20 s | 30 s | - | **Polished:** 20 s: allies within 8 yd take 30% less bullet damage. |
| 761 | 60 Elementalist | Earthquake | B | 19798 | working | timed 10 s | 2 min | - | **Fixed:** Aims at the crosshair's unit (hands). |
| 762 | 60 Earthbreaker | Avatar of Stone | B | 4934 | working | timed 20 s | 3 min | - | **Fixed:** A logout in it kept the stone look for good. **Polished:** +50% max health, immune to knockback and slows, each melee blow a one-charge Ground Smash. |
| 763 | 60 Totemist | Ancestral Circle | B | 17359 | working | timed 15 s | 3 min | - | One totem of each element, as in vanilla. **Polished:** Your totems doubled while it stands; downed allies within 20 yd stand up at 30% when it lands. |

#### Mage

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 801 | Core core | Blink | A | 1953 | working | instant | 15 s | - |  |
| 802 | Core core | Ice Block | A | 11958 | working | timed 10 s | 3 min | - | Click the aura off to leave it early. |
| 803 | Core core | Polymorph | A | 12826 | working | timed 20 s | 30 s | - |  |
| 811 | 10 Wizard | Fireball | A | 25306 | working | instant | 2 s | - | Every mage knows Fireball; this adds Spellweave's switch bonus. |
| 812 | 10 Spellsword | Flame Blade | B | 10199 | working | timed 60 s | 10 s | - |  |
| 813 | 10 Artificer | Warding Rune | C | 28773 | working | timed 10 s | 20 s | - | Breaks after taking 40% of your health in rounds. **Polished:** The screen breaks after 40% of your health in rounds; a construct. |
| 821 | 20 Wizard | Frostbolt | A | 25304 | working | instant | 2 s | - |  |
| 822 | 20 Spellsword | Blade Blink | B | 14514 | working | instant | 15 s | - | **Fixed:** Aimed only at an old selection: the crosshair's unit now (hands). |
| 823 | 20 Artificer | Healing Glyph | B | 24815 | working | timed 15 s | 30 s | - | **Fixed:** Could never be cast: Draw Ancient Glyphs wants a spell focus; its 10 s channel is gone too. |
| 831 | 30 Wizard | Arcane Missiles | A | 25345 | working | channel | 6 s | - |  |
| 832 | 30 Spellsword | Frost Nova | A | 10230 | working | timed 8 s | 20 s | - | **Polished:** 2 s longer frozen. |
| 833 | 30 Artificer | Blink Pads | B | 18960 | working | timed 45 s | 1 min | - | **Fixed:** Not usable in combat (Teleport: Moonglade's flag). |
| 841 | 40 Wizard | Blast Wave | A | 13021 | working | instant | 30 s | - | **Polished:** Knocks enemies within 10 yd back 8 yd. |
| 842 | 40 Spellsword | Mana Shield | A | 10193 | working | timed 15 s | 25 s | - | No shield glow. **Polished:** 25% of your health for 15 s from its own pool; each melee hit restores 5%. |
| 843 | 40 Artificer | Conjure Supplies | A | 10054 | working | instant | 1 min | - | Up to four carried; a stim heals 25% and speeds you 20% for 4 s. **Polished:** Arcane Stims (up to 4 carried, tradeable): 25% health back and +20% speed for 4 s. |
| 851 | 50 Wizard | Combustion | A | 11129 | working | timed 60 s | 90 s | - | Lasts up to 60 s. **Polished:** The next three Wizard spells crit and count as a switch of school. |
| 852 | 50 Spellsword | Arcane Edge | B | 12536 | working | passive | - | - | **Polished:** A blade blow (20%) or a melee kill: the next Wizard spell is instant and deals double. |
| 853 | 50 Artificer | Arcane Familiar | B | 10721 | working | timed 30 s | 45 s | - | **Fixed:** Aims at the crosshair's ally (hands). |
| 861 | 60 Wizard | Meteor | B | 24340 | working | timed 8 s | 90 s | - | **Fixed:** Aims at the crosshair's unit (hands). |
| 862 | 60 Spellsword | Arcane Avatar | B | 12042 | working | timed 20 s | 3 min | - | **Polished:** Melee swings send an arcane wave 15 yd ahead (10 %EH). |
| 863 | 60 Artificer | Grand Array | B | 12051 | working | timed 30 s | 3 min | - | **Fixed:** Evocation's 8 s channel held the mage: no channel now. **Polished:** Constructs placed during it have no cooldown; construct limit 6 (3 otherwise). |

#### Warlock

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 901 | Core core | Fear | A | 6215 | working | timed 20 s | 20 s | - |  |
| 902 | Core core | Life Tap | A | 11689 | working | instant | 10 s | - | Also a soul shard (up to 5). |
| 903 | Core core | Soulstone | A | 20757 | working | instant | 15 min | - | On the crosshair's ally or yourself; up 2 s after going down. **Polished:** On the crosshair's ally or yourself, 15 min, one a warlock; going down, up at 40% 2 s later. |
| 911 | 10 Afflicter | Corruption | A | 25311 | working | timed 18 s | 3 s | - |  |
| 912 | 10 Felsworn | Fel Hide | B | 11735 | working | timed 20 s | 30 s | - |  |
| 913 | 10 Necromancer | Raise Skeleton | B | 17473 | working | timed 60 s | 3 s | 20 Life Force | Needs a corpse within 30 yd. |
| 921 | 20 Afflicter | Curse of Exhaustion | A | 18223 | working | timed 12 s | 8 s | - | Aiming is not slowed. **Polished:** Enemy players cannot sprint for 12 s. |
| 922 | 20 Felsworn | Fel Leap | B | 24530 | working | instant | 20 s | - | **Fixed:** Needed an enemy target (Felfire): leaps without one. |
| 923 | 20 Necromancer | Corpse Explosion | B | 17616 | working | instant | 6 s | 15 Life Force | Needs a corpse or a minion within 30 yd. **Fixed:** Corpse Explosion's own targeting no longer gets in the custom part's way. |
| 931 | 30 Afflicter | Drain Life | A | 11700 | working | channel | 10 s | - |  |
| 932 | 30 Felsworn | Hellfire | A | 11684 | working | timed 10 s | 20 s | - | The burn has no fire of its own to see. **Polished:** 10 s on the move: 3 %EH/s within 10 yd, 1% of your own health a second. |
| 933 | 30 Necromancer | Bone Armor | B | 11445 | working | timed 20 s | 20 s | 20 Life Force |  |
| 941 | 40 Afflicter | Howl of Terror | A | 17928 | working | timed 8 s | 40 s | - |  |
| 942 | 40 Felsworn | Death Coil | A | 17926 | working | timed 3 s | 45 s | - |  |
| 943 | 40 Necromancer | Raise Ghoul | B | 28353 | working | timed 60 s | 10 s | 30 Life Force | **Polished:** The ghoul leaps up to 20 yd onto your target and claws 5 %EH/s. |
| 951 | 50 Afflicter | Soul Siphon | B | 18881 | working | passive | - | - | Spreads when you land the killing blow. **Polished:** Afflictions jump to the 2 nearest enemies within 15 yd. |
| 952 | 50 Felsworn | Demonic Pact | B | 19028 | working | passive | - | - |  |
| 953 | 50 Necromancer | Death's Harvest | C | 28433 | working | passive | - | - | **Fixed:** Shared Corpse Explosion's spell, so the trainer sold Corpse Explosion instead: own icon (Explode). |
| 961 | 60 Afflicter | Curse of Doom | A | 603 | working | timed 10 s | 1 min | - | Spreads when you land the killing blow. **Polished:** After 10 s, 60 %EH unless purged; a death spreads every affliction within 15 yd. |
| 962 | 60 Felsworn | Demon Form | B | 8872 | working | timed 30 s | 3 min | - | **Fixed:** A logout in it kept the fel guard look for good. **Polished:** +50% max health, claws cleave 15 %EH, immune to fear and slows. |
| 963 | 60 Necromancer | Abomination | B | 28422 | working | timed 45 s | 2 min | 80 Life Force | **Polished:** Meat Hook: an enemy within 30 yd pulled to it every 12 s. |

#### Druid

| Id | Tier | Option | Route | Spell | Status | Ends | Cd | Cost | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1101 | Core core | Barkskin | A | 22812 | working | timed 15 s | 1 min | - |  |
| 1102 | Core core | Entangling Roots | A | 9853 | working | timed 27 s | 20 s | - |  |
| 1103 | Core core | Travel Form | A | 783 | working | form | 5 s | - |  |
| 1111 | 10 Shapeshifter | Cat Form | A | 768 | working | form | - | - | No energy or combo points. **Polished:** Prowl, Pounce and Rake cost nothing; Pounce stuns 3 s; momentum's next blow crits. |
| 1112 | 10 Grovekeeper | Rejuvenation | A | 25299 | working | timed 12 s | 3 s | - |  |
| 1113 | 10 Starcaller | Moonfire | A | 9835 | working | timed 12 s | 4 s | - |  |
| 1121 | 20 Shapeshifter | Bear Form | A | 9634 | working | form | - | - | No rage. **Polished:** Maul strikes at once (12 %EH), Bash 4 s / 3 s on 45 s, Growl taunts; no rage. |
| 1122 | 20 Grovekeeper | Regrowth | A | 9858 | working | timed 21 s | 4 s | - |  |
| 1123 | 20 Starcaller | Lunar Strike | B | 25298 | working | instant | 6 s | - | **Polished:** A melee strike: 12 %EH and 5 yd back. |
| 1131 | 30 Shapeshifter | Stag Form | B | 21936 | partial | form | 1.5 s | - | Travel Form in the stag's look; no passenger seat. |
| 1132 | 30 Grovekeeper | Swiftmend | A | 18562 | working | instant | 12 s | - |  |
| 1133 | 30 Starcaller | Faerie Fire | A | 9907 | working | timed 40 s | 10 s | - |  |
| 1141 | 40 Shapeshifter | Crow Form | B | 8153 | partial | form | 1.5 s | - | Owl look, slow fall and spotting below; no lift. **Polished:** Enemies below within 40 yd are spotted. |
| 1142 | 40 Grovekeeper | Rebirth | A | 20748 | working | instant | 10 min | - |  |
| 1143 | 40 Starcaller | Hurricane | A | 17402 | partial | channel | 1 min | - | Centred on you (no ground cursor). **Fixed:** Waited for a ground click the crosshair cannot give: centred on the druid. |
| 1151 | 50 Shapeshifter | Spider Form | B | 24084 | partial | form | 1.5 s | - | Mar'li's look and Web Spray; no wall crawl or Venom Bite. |
| 1152 | 50 Grovekeeper | Tranquility | A | 9863 | working | channel | 3 min | - |  |
| 1153 | 50 Starcaller | Lunar Grace | B | 24907 | working | passive | - | - |  |
| 1161 | 60 Shapeshifter | Worgen Form | B | 7621 | partial | form | 1.5 s | - | Health, lifesteal, Rending Leap's hit, momentum; no Frenzy yet. **Polished:** +30% health in form, Rending Leap's 10 %EH, momentum 6 s immune to stun and fear. |
| 1162 | 60 Grovekeeper | Tree of Life | A | 775 | working | timed 30 s | 3 min | - | Regrowth still stops to cast. **Fixed:** Tree Form stunned the druid for its 8 s: a treant's look now, casting freely. **Polished:** Every HoT also lands on the weakest ally within 20 yd. |
| 1163 | 60 Starcaller | Starfall | B | 20687 | working | timed 10 s | 2 min | - | **Fixed:** Starfall's 10 s channel held the druid: no channel now. |


**Root causes behind the fixes:**
- *The refit cleared every stance*, including the forms a spell is also allowed in: Shadow Word: Pain,
  Mind Blast, Psychic Scream, Silence and the other shadow spells could not be cast in Shadowform. The
  refit now keeps the stance bits of spells marked usable while not shapeshifted.
- *Donors kept vanilla gates the custom part never needed*: Judgement's seal (caster aura state),
  Draw Ancient Glyphs' spell focus (Healing Glyph could never be cast), Holy Shield's shield, Backstab's
  dagger, Teleport: Moonglade's out-of-combat flag (Blink Pads), Felfire's and Hooked Net's enemy target
  (Fel Leap, Grappling Hook). The refit has flags for each (`REFIT_NO_AURA_STATE`, spell focus always
  cleared, `REFIT_NO_WEAPON`, `REFIT_IN_COMBAT`, `REFIT_SELF`), and the client's spell copy gets the same
  through three new op 7 flags (`TWEAK_ANY_CREATURE`, `TWEAK_NO_AURA_STATE`, `TWEAK_NO_FOCUS`).
- *Replaced donors that were channels held their caster*: Eagle Eye 60 s (Field Glass), Evocation 8 s
  (Grand Array), Starfall 10 s, Draw Ancient Glyphs: `REFIT_NOT_CHANNEL`.
- *Ground-targeted spells waited for a click the hands' crosshair cannot give* (Flare, Hurricane):
  `REFIT_AT_FEET` lands them at the caster's feet.
- *Tree Form is a shapeshift with a stun*: Tree of Life stunned its druid for 8 s. Now replaced, with the
  Treant's model.
- *Timed transformations outlived a logout* (Avatar of Stone, Demon Form): a fresh login takes them off.
- *Two options shared a spell in one class* (Death's Harvest on Corpse Explosion's), so the trainer, which
  sells by spell, sold the wrong one; and the warrior's Chain Hook and the hunter's Grappling Hook shared
  Hooked Net with opposite targeting. A test now forbids a shared trainer spell.
- *Self-cast options aim by the selection*, which the hands never set for a self cast: a self cast now
  selects the crosshair's unit first, as a move does.
- Cast times and cooldowns the refit missed (Flash of Light, Holy Light, Flash Heal, Lightning Bolt,
  Chain Lightning, Chain Heal; Rekindle's 10 min), Crusader Strike's unchained ranks (spellLevel 99,
  `server/sql/custom/azeroth_warfare/18_class_fixes.sql`), Desperate Prayer and Lay on Hands not standing
  a downed player up, Mind Control holding a player for its whole channel, a dead Bard's song playing on.

**Druid forms** (Cat, Bear, Stag, Crow, Spider, Worgen) are reworked separately; their rows say what
they do today.

**What the polish pass built (shared by many rows):**
- *Immunities from running effects* (Rampage, Warlord's Command, Blood Frenzy's Frenzy, Avenging
  Crusade, Death Wish, Avatar of Stone, Demon Form, Worgen's momentum): `ImmunitiesOf`, applied and
  lifted by the game as the effects come and go; what already holds the player drops.
- *Auras fitted once they land* (a cast's effects come after the class hook): Disarm and Hammer of
  Justice shorter on players, Blessing of Protection 6 s, Pounce 3 s, Bash 4 s / 3 s, Frost Nova
  longer by Bladecharge, Feign Death 6 s.
- *No shot, no swing, no sprint*: disarmed, pacified (Blessing of Protection) or charmed players
  cannot fire or swing, a disarmed NPC gunner stops shooting; Earthbind, nets, tar, bear traps and
  Curse of Exhaustion forbid sprinting. A demoralized shooter's rounds scatter 50% wider.
- *Damage over time* (Witchfire's burn, Envenom's poison stacks, the bear trap's bleed) and *melee
  procs run after the blow* (blade waves, stone smashes, demon claws, Stone Fists' knockback).
- *Rescues from going down*: Spirit of Redemption holds a priest up 12 s, Reincarnation and a
  soulstone stand a player back up at 40%; Rallying Cry gives a downed ally 10 s, Warlord's
  Command, Divine Hymn and Ancestral Circle stand downed allies up.
- *Traps side by side*: a hunter's traps no longer replace each other (up to 4, 8 in a Killing
  Field), and one going off sets off the others near it (`GameObject::Update`).
- *Totems as zones* (Totemic Web +50%, doubled in the Ancestral Circle; the totem leaves with its
  zone), *constructs with a limit* (3, 6 in a Grand Array), *walls that wear down*.
- Refits whose vanilla effect is now the custom part's (`REFIT_REPLACE*`), the cat's and bear's
  abilities free of energy and rage and kept in their form, Maul at once (`REFIT_NOT_NEXT_SWING`,
  read by the hands from op 7's attributes).

