# World of Modern Warfare: game design

The direction agreed so far, in one place. Each section says what is decided and what is still open; the
open questions are gathered at the end. Nothing here is built yet unless it says so.

---

## 1. Vision and principles

**The setting.** Guns became as lethal as real-world firearms, and they outclass magic and steel. Every
class, race and faction has had to adapt. That is the world, and it is also why the classes are designed
the way they are: each class's abilities are its answer to guns.

**Principles**

1. **Guns are the baseline everyone shares.** Every character fights with a kit of guns, equipment and
   killstreaks.
2. **Classes do what guns can't.** Class power goes into moving, protecting, healing, controlling or
   denying ground, seeing enemies, and burst in the class's own situation, never into making guns hit
   harder.
3. **Gear never raises raw gun damage.** A gun's damage comes from its own numbers scaled by level. Gear
   changes handling, survival, recovery and abilities (section 7).
4. **Small kits.** First person punishes big spellbooks: a character has at most 9 class abilities and
   passives.
5. **Every ability needs the hands up (V).** Lowering the gun is the price of a class ability, so each
   one must be strong enough to be worth it.
6. **Every archetype has one signature mechanic** (a resource, a transformation, a combo), which keeps a
   small kit interesting.
7. **Systems over WoW-specific content.** Effort goes into systems (rules, kits, classes, companions,
   construction) rather than rewriting WoW content, because the long-term goal is a game of our own
   (section 16).

---

## 2. Classes and archetypes

### The talent system

- **3 core abilities at level 1**, automatic, shared by every build of the class.
- **6 tiers**, at levels 10, 20, 30, 40, 50 and 60. Each tier offers **3 options, one per archetype**; a
  character picks one per tier.
  - A pure build takes the same archetype's option every tier (a pure Wizard).
  - A blend mixes them (an Assassin-Duelist rogue).
- An option is either an **ability** or a **highly specialized passive**.
- Per class: 3 core + 18 options = 21 designs; 189 in all.

### Learning and choosing

- **Learning:** each option is bought **once, with gold, at the class trainer**, vanilla style. The price
  rises by tier (for example 1 gold at tier 10 to 50 gold at tier 60).
- **No ranks: one version of each ability, as there is one of each gun.** An ability bought once
  grows with the character's level (see "One entry per ability" below).
- **Choosing:** learned options are slotted into a **kit** at any **trainer or innkeeper** (section 3).
- **The old class spells are removed** from the spellbook. Racials, professions, mounts and First Aid
  stay.
- Race and class combinations stay as in vanilla.

### The archetypes

| Class | Archetypes |
|---|---|
| Warrior | Berserker · Vanguard · Warlord |
| Paladin | Crusader · Protector · Lightwarden |
| Hunter | Scout · Trapper · Slayer |
| Rogue | Assassin · Trickster · Duelist |
| Priest | Shadow · Cleric · Oracle |
| Shaman | Elementalist · Earthbreaker · Totemist |
| Mage | Wizard · Spellsword · Artificer |
| Warlock | Afflicter · Felsworn · Necromancer |
| Druid | Shapeshifter · Grovekeeper · Starcaller |

**Class pets are removed.** Hunters and warlocks are built to stand on their own; companions are a
separate system for every class (section 4).

**Warrior** (rage)
- **Berserker:** closes the gap and wins every close fight; kills and crowds send it into a Frenzy.
- **Vanguard:** a shield tank that blocks bullets and taunts enemy fire; a chain hook that yanks an enemy
  out of cover onto the shield.
- **Warlord:** a commander. Banners as deployables, rallying heals, a demoralizing fear.

**Paladin**
- **Crusader:** holy melee; swings chain into holy finishers.
- **Protector:** shields allies against bullets; Consecration denies ground.
- **Lightwarden:** the healer, with a battle resurrection; its heals may bring the sun to Zenith.

**Hunter.** No beasts at all. The core is **the hunt**:
- **Mark Prey:** one target at a time. You see it through walls, follow its trail of tracks, and move
  faster toward it. Killing your prey resets your cooldowns, so a good hunter chains kills.
- Feign Death, Disengage, Aspect of the Cheetah.
- **Scout:** the squad's eyes. **Spotting** through a field glass outlines enemies for the party (from
  what the scout actually looks at, unlike the UAV's sweep). Flare (reveals stealth), Camouflage
  (stand still to fade), Grappling Hook, Smoke Pellet.
- **Trapper:** prepares the ground. **Prepared Ground:** up to 4 hidden traps, and traps next to each
  other chain. Freeze, snare-net, tar and bear traps; tripwire alarms; a noise-maker decoy; a net
  launcher. Control, not damage (Claymores and C4 belong to the kits).
- **Slayer:** the monster and caster hunter (the witch hunter idea, renamed). **Bane:** mark a creature
  type or a caster and your melee and abilities hit it harder. Purge, Scatter (silence), a burning blade
  glaze, Hex Ward, throwing axes.

**Rogue**
- **Assassin:** stealthy openers. Ambush, Cheap Shot and Garrote only from stealth or the
  **Opening** (4 s after leaving stealth, blades and abilities doubled); Distract turns enemies
  away, Preparation readies Vanish for the next opening. The kill before they turn; weak once the
  fight drags on.
- **Trickster:** poisons (Deadly, and a satchel of Crippling, Mind-numbing and Wound), back into
  stealth mid-fight (Smoke Bomb), decoys and mirrors, pockets and locks (Light Fingers lifts a
  purse in combat, Pick Lock instant and +50); every ability **Slips** (untargetable 0.5 s).
- **Duelist** (replaced the Bard, 2026-10-21; music became the Minstrelsy profession): unstealthed,
  rapid, two-handed melee. **Momentum** builds with every blow (a buff, +5% a point, ebbs when the
  blows stop); Sinister Strike's double blow, Lunge (the one dash), Riposte's parry, Blade Flurry's
  cleave, Relentless, Adrenaline Rush.

**Priest**
- **Shadow:** DoTs, drains, mind control.
- **Cleric:** the pure healer.
- **Oracle:** a barrier that stops bullets, damage reduction for allies, Psychic Scream.

**Shaman**
- **Elementalist:** lightning and earthquakes; Static that builds into big procs.
- **Earthbreaker:** weapon-imbued melee; stone fists and earthquake slams.
- **Totemist:** totems as deployables (healing, grounding, earthbind), Chain Heal.

**Mage**
- **Wizard:** the pure caster; fire, frost and arcane burst.
- **Spellsword:** blade and spells in the hands.
- **Artificer:** support that places things rather than casting at enemies: warding runes that block
  bullets, a healing glyph, a pair of linked blink pads, conjured items for the squad, an arcane
  familiar that soaks damage. No turrets, no gun buffs.

**Warlock** (Life Tap, Healthstone, Soulstone, Fear and soul shards in the core)
- **Afflicter:** curses, DoTs, drains; wins long fights.
- **Felsworn:** becomes the demon; full demon form as the top tier.
- **Necromancer:** shoot things, then raise what you killed. Corpses become skeletons, ghouls, or an
  abomination with a meat hook. Minions last about 60 s, at most 3-5 at once (fodder, not pets). **Life
  Force** fills from kills and minions dying and pays for raising, Corpse Explosion and Bone Armor.

**Druid**
- **Shapeshifter:** many specialized forms: cat (burst, stealth), bear (tank), spider (webs, climbing,
  poison), worgen (frenzy, lifesteal), stag (speed), crow (glides, scouting). Each form has 2-3
  abilities. Forms never fade on their own; shifting grants **Wild Momentum**, a short buff that
  empowers the new form and fades after a time, so shifting is rewarded without being forced.
- **Grovekeeper:** healing over time, Rebirth.
- **Starcaller:** half melee, half lunar magic.

### One entry per ability

A class spell exists once (server `FusionClasses::BuildSingleEntries`, rules `fusion/Classes.h`
`SingleEntry`, `CollapseBar`):

- **The entry scales with the level.** The character knows one entry of each chain: the vanilla rank
  its level allows, which becomes the next rank by itself when the level reaches it (no training).
  So its damage, healing, absorb, duration and cost at any level are exactly what the old trainer's
  rank was at that level, plus the spell's own per-level growth in between (vmangos applies it from
  the caster's level), and stats scale it as before. This keeps every number vanilla's, tooltips
  included, with no formula of our own to drift from them.
- **How.** vmangos's spellbook supersede (`SpellMgr::GetSpellBookSuccessorSpellId`, used for
  professions) is given every class chain: a rank learned replaces the last in the book and on the
  bars (`SMSG_SUPERCEDED_SPELL`); the lower ranks stay learned beneath it, inactive, and are sent to
  no client. Characters saved with every rank active are fixed as they load (no wipe: the rows are
  only marked inactive).
- **Old references keep working.** At login and every level-up the server moves any bar button
  holding an older rank onto the entry; a cast of an older rank (an old bar, a hand readied before a
  level-up, a macro) is cast as the entry; the client's `/cast Name(Rank N)` finds the entry when
  the rank is not in the book; the "Rank N" under a spell's name is dropped (`TWEAK_NO_RANK`).
- **Trainers** sell the class options (once each), never ranks.
- **The Abilities page** (kit window) lists each ability once with its icon and no rank; its
  tooltips resolve the spell's numbers at the character's level.

### Class resources

WoW's own resources stay (mana, rage, energy, combo points where a class still uses them). Of the
custom archetype resources only three are left (and the Duelist's new Momentum), the ones the archetype's fantasy is built on; the
client shows each as a **stacking buff** over the action bar (its icon, the count, a tooltip), not
a bar. The rest are gone (2026-10-19): what they paid for is a cooldown, a chance or a flat effect.

| Resource | Archetype | Now | Why |
|---|---|---|---|
| Holy Power | Crusader | kept, a buff (3, 5 with Zealotry) | the builder-finisher rhythm is the Crusader |
| Static | Elementalist | kept, a buff (5, 8 with Overcharge) | building to an Overload is the Elementalist |
| Life Force | Necromancer | kept, a buff (100, 150 with Death's Harvest) | raising the dead from kills is the Necromancer |
| Momentum | Duelist | new (2026-10-21), a buff (5, 8 with Relentless): +1 a blow, +5% melee a point, ebbs 2 s after the last blow | the pressure of never stopping is the Duelist; nothing spends it |
| Fury | Berserker | removed: Frenzy on a kill, a Whirlwind among 3, or a Bloodthirst by 20%; Blood Frenzy's heal and Intercept reset on every kill | rage is the warrior's resource already |
| Solar Power | Lightwarden | removed: each heal may bring Zenith (Flash of Light 10%, Holy Light 20%, Holy Shock 15%) | a healer watching health bars needs no second meter |
| Axes | Slayer | removed: Throwing Axes on a 6 s cooldown; a Witchfire kill readies it | charges were a cooldown in disguise |
| Crescendo | Bard | removed (and the Bard with it, 2026-10-21: the Duelist took its place) | the songs are the Minstrelsy profession's now |
| Shadow Orbs | Shadow | removed: Vampiric Embrace a flat 20 s; Mind Control needs nothing, cooldown 60 to 90 s | DoTs and Mind Blast carry the fantasy |
| Devotion | Cleric | removed: Prayer of Healing heals 20% more | a pure healer needs no counter |
| Foresight | Oracle | removed: a Barrier holds half the priest's health (was 40%, doubled when full) | the screens are the Oracle |
| Earth Charges | Earthbreaker | removed: Ground Smash a flat 15 %EH, Stoneskin a flat 15% fewer bullets | blows and slams carry it |
| Spellweave | Wizard | removed as a counter: a spell of another school than the last deals 30% more | the switch is the weave, no count to track |
| Bladecharge | Spellsword | removed: Blade Blink a flat 20 %EH, Frost Nova 2 s longer, Arcane Edge from a blade blow (20%) or a melee kill | the blade carries it |
| Fel Fury | Felsworn | removed: Demon Form on its 180 s cooldown alone, Demonic Pact a flat 15% less damage | the demon is the Felsworn, not a meter |
| Lunar, Solar | Starcaller | removed: Moonfire, Faerie Fire and Lunar Strike bring Eclipse by 20%, a blow by 5% | two meters for one buff was too much |

### The Bard's instruments (moved)

The Bard's rhythm songs became the Minstrelsy profession (2026-10-21): section 12, "Minstrelsy
(built)". The archetype itself is being replaced.

### Ability use

- **Every ability needs the hands up (V).** No ability fires with the gun up.
- So each one is strong per use, limited by cooldowns rather than cost.

### Spellcasting

Spells are cast the two-hand way (client crate::spellcast, server fusion/Spellcasting.h).

- **Readying.** A spell clicked on the bar with a mouse button goes into that button's hand: the
  left button's hand shows in the right fist, the right button's in the left (the number key
  readies the left button's hand, Shift+number the right's).
- **Charge, hold, release.** Holding a hand's button charges its spell: 0.3 s for an instant, 0.45 s
  up to a 1.5 s cast, 0.6 s up to 2.5 s, 0.8 s beyond, 0.5 s for a channel, 0.9 s for anything that
  lands on the ground. Charged, it holds as long as the button stays down; letting go casts.
  Letting go early fizzles: nothing is cast and nothing is spent.
- **Instant, on the move, no target.** The server casts the release at once with no cast time and no
  global cooldown, and spells don't break on movement. Each hand casts at most once per charge
  (the server's own check), and the spell's cooldown still applies.
- **Free aim.** A hostile spell with reach (Fireball, Frostbolt, Shadow Bolt, Wrath,
  Polymorph, a curse) leaves the hand along the crosshair's ray and strikes whatever it meets
  first: an enemy (by its hitbox, as rounds do), a wall, the ground, a built piece, a closed door.
  Nothing needs to be under the crosshair. A spell with a missile speed flies for real (distance
  / speed), so a moving target can step out of it or into it; an instant one strikes along the
  ray at once (forgiving the crosshair's unit by a yard). Arcane Missiles fires its missiles a
  quarter second apart. The server flies every bolt from the caster's eye and decides what it
  hit; the spell is paid for (mana, cooldown) at the release.
- **No range, only a fizzle.** A bolt has no range: it flies until it strikes something. A purely
  technical safety cap ends it at 200 yards (or 12 s aloft), where it just fades. Some spells
  fizzle out sooner, with a puff where they fade: it is per spell, by its chain's first rank
  (fusion/Spellcasting.h `BoltRows`, `TraitsOf`). The defaults: a flying bolt never fizzles; an
  instant ray (a hit-scan such as Mind Blast) fizzles at twice its spell's reach, so it is no
  200-yard rifle. Rows: Arcane Missiles 45 yd, Earth Shock and Frost Shock 30 yd.
- **Solid things stop it, nothing else.** A bolt meets what a walking body meets: the terrain,
  buildings (WMO faces that collide), doodads with a collision hull (a tree's trunk, a rock),
  closed doors and gates (game objects with a collision model), built pieces and units. A bush, a
  tree's canopy, grass or a WMO's decorative DETAIL faces stop nothing. On the server that is the
  vmaps (which hold only models' collision hulls) plus the dynamic game-object tree; the caster's
  client foresees its bolt's end with the same set (its walking colliders, crate::aim
  `solid_distance`), not with the drawn pick the crosshair uses. One known gap: vmangos's vmap
  extractor keeps a WMO face flagged both DETAIL and COLLISION, which the client lets a body walk
  through; changing that needs the vmaps extracted again.
- **Splash: a bolt need not hit.** Where a bolt strikes (an enemy or the world), the enemies
  around it that the impact can see (walls, terrain and built pieces are cover) take a share of
  the spell: the one hit takes it whole; the others 75% at the impact, falling to 50% at the edge
  of the splash. Aim pays, a near miss still counts, and a crowd hit at its feet does not each
  take a full Fireball. A damaging spell splashes 2.5 yards (1.5 for Arcane Missiles; a dual cast
  half again); a spell that does no harm (Polymorph, Fear) or Curse of Doom strikes only what it
  hits; a spell with its own area about its destination keeps that area, at full strength, as its
  splash. No friendly fire: only enemies the caster may attack (PvP's rules too) are splashed.
- **What a splash carries.** Hard control (Polymorph, a stun, a fear, a root, a silence, a charm,
  an interrupt, a knock-back) lands only on a direct hit: a near miss never turns a crowd into
  sheep. Damage over time splashes at the splash's share of its damage; a slow or another debuff
  at the share of its duration. The class option of the spell runs once, for the hit.
- **Homing is a trait, not the rule.** Bolts fly straight. A homing bolt picks, at its launch, the
  caster's selected (Tab) target if it is within 45 degrees of the aim (and 80 yards), else the
  enemy nearest the crosshair within the bolt's own cone, and curves toward that target's chest
  at a limited turn rate, so a target close in or moving fast across it still makes it miss.
  Arcane Missiles homes (270 degrees a second, a 20-degree cone). Talents, perks and kit options
  grant it: `FusionClasses::GrantHoming(player, chain, homing, ms)` (chain: a spell's first rank,
  0 every bolt; ms 0: until `RevokeHoming(player, chain)`), usually with
  `Fusion::Spellcasting::kGrantedHoming` (120 degrees a second, a 12-degree cone); the stronger of
  the spell's own and the grant's turn wins. Everyone near, the caster too, is told the homing
  bolt's course as it turns (op 42 `BOLT_HOMING`), and draws it curving.
- **Sprays, like a flamethrower.** Held, not charged: while the button is down the hand pours a
  short cone in front, and four times a second every enemy in it that the hand can see takes a spell
  hit of the spray's own (its school, resistances, absorbs, crits, the combat log) and the caster
  pays mana (a share of its pool). Let go, out of mana, silenced or stunned, it stops. A spray's
  damage is measured against its school's bolt, so holding it trades with casting the bolt over the
  same time: each tick deals a share of what the bolt deals in a quarter second when cast as fast
  as its charge allows (one cast a charge from one hand), the bolt being the rank the caster's level
  allows with the caster's spell power and talents on it as a cast would have them. The mage has
  two, in its Utility spells from level 1 (on the bar after the kit's picks): Scorch is the fire
  stream (10 yd, a 20-degree half angle, 100% of a Fireball stream's damage a second to each enemy
  in it, 1.5% mana a tick: about 16 s from full) and Cone of Cold the frost spray (9 yd, 30
  degrees, 70% of a Frostbolt stream's and a 30% slow). At 60 with no spell power that is about 212
  and 117 a tick on the WoW scale (Fireball 678 every 0.8 s, Frostbolt 535), before the victim's
  pool scale, as any spell. Both are WoW's own short, frontal fire and frost spells, so they map
  naturally, and neither displaced a kit option. Existing channels (Arcane Missiles, Drain Life,
  Mind Flay, Blizzard, Hellfire) stay as they were. More sprays are rows in `SprayRows`. The client
  sends op 43 at the press, every 100 ms with its aim while held and at the release; the server
  tells those near (op 44), and each client draws the stream with the spell's own WoW cone model
  (crate::spell_fx).
- **What it does on impact.** A unit takes the spell as if cast at it (damage, auras, slows, procs,
  threat, the combat log, the kill); a spell with its own area about its destination centres it
  on the impact. The world shows the spell's impact where it struck, and a built piece takes the
  spell's damage (fusion/Build.h `DamageKind::Spell`), its splash's pieces too. Everyone near sees
  the bolt fly (the spell's own missile model) and land.
- **Which hand.** The action bar readies a spell into the left button's hand (a click, its key)
  or the right button's (a right click, Shift with its key). Any other binding names the hand
  itself: client `hands::ReadyInHand { side, action, spell_id }` (side 0 the left button's hand,
  the right fist; 1 the right button's, the left fist) or `hands::put_in_hand`; a release names
  it too (`spellcast::hand_aim(aim, spell, hand, self_key)`).
- **Each hand its own.** A spell is held, charged and cast by the hand it is in (right or left),
  each with its own ready pose, charge glow and release; the other hand stays its own (a weapon,
  a shield, a fist, another spell). A spell readied while holding a two-hander puts the
  two-hander away (the two-hand rule): the other hand comes free rather than gripping a hilt beside
  the spell; Attack readies the two-hander in both hands again. Only a dual cast (the same spell
  in both) uses both.
- **Melee does not lunge.** A melee swing is a swing where you stand; MW2's knife lunge (a dash
  at the enemy in front) is gone from it. Only the warrior's Charge carries the body forward. The
  lunge stays as a grant (fusion/Movement.h `SwingLunges`, `FusionClasses::GrantsSwingLunge`) a
  Charge perk could turn on; nothing grants it today, and the server drops a lunge request
  without one.
- **Everything else.** A touch (melee reach) still hits the crosshair's enemy, or the enemy at the
  crosshair's point; a beam channel (Drain Life, Mind Flay) holds on the one it was cast at. A heal
  or buff lands on the crosshair's friend, else on the caster (Alt: always the caster). Area spells
  and ground spells (Blizzard, Flamestrike, Rain of Fire) land at the crosshair's point, a ring
  showing where. Range and sight to the point are the server's to check.
- **Dual cast.** The same spell in both hands, both let go together (within 0.15 s): one cast at
  +50% damage and healing and a 50% wider area.
- **Ground spells.** Moonfire bursts on every enemy around the point and leaves a 6 s moonlit
  zone that moonfires whoever walks in; Frost Nova's shockwave and Consecration land at the point;
  Curse of Exhaustion curses every enemy around it.
- **Seen, with WoW's own effects.** A readied hand holds the spell's precast model (its channel,
  cast or state kit's when it has none), growing as it charges and pulsing when full; the release
  plays the cast kit's hand model (on others too, when their bolt leaves); the bolt is the spell's
  missile model, its impact the impact kit's models where it strikes, a fizzle the impact at a
  third of its size; a spray pours the spell's own cone (Cone of Cold's, the unused Cone of Fire
  for Scorch, a breath model for other schools). In first person they sit on the viewmodel's
  hands, in third person on the character's spell-hand attachments. A bar under the crosshair
  fills per hand; WoW's own ground reticle shows where a ground spell will land (wider for a dual
  cast) and where a zone stands, the zone with its spell's ground model.

---

## 3. Kits

- **A kit is a whole loadout:** the MW2 class (guns, attachments, camos, perks, deathstreak, equipment,
  killstreaks) and the 6 talent picks together.
- **Grenades are equipped, then thrown.** The lethal's and the tactical's slots ready the grenade
  in the main hand only (no dual-wielded grenades; Alt or a right click readies it there too),
  MW2's own first-person grenade in place of the gun. The main hand's mouse button throws it
  where the crosshair aims (a frag cooks while held, as before); the gun does not fire with it
  meanwhile. The slot again, a gun picked, leaving Modern or none left puts it back and
  the gun comes up again; C4 stays readied while it is out, the same button setting it off. The
  throw is the same shot as before: the server checks the count, the cook and the arc.
- **Up to 5 kits per character.** MW2's premade classes are removed.
- **Kits are edited and switched at any trainer or innkeeper.** Every trainer (class, profession, weapon
  master) and every innkeeper has an "Edit your kits" line in its gossip, which opens the kit window to
  edit; saves and switches count within reach of one. No extra NPCs stand in the world for it.
- **Anywhere else kits are viewed, not changed:** the HUD's Kits button opens the same window greyed,
  with a line saying to visit a trainer or innkeeper. Dying and respawning keeps the current kit.
- **The field editor:** from the kit window, once an hour, kits can be changed anywhere for two
  minutes; the hour starts with the first change. (The old Field Quartermaster's Ledger item still
  opens it, on the same cooldown; it is no longer sold.)
- **The kit window** has four pages: the loadout, the talents (each option with a stock-style
  tooltip: tier, cost, description), the abilities and the killstreaks (the kit's three, their kill
  costs and the streak this life). What the server says shows in the window, never the chat.
- **The kit window keeps one size** whatever is picked or shown (pages and lines are fixed in
  height; long text wraps or is clipped, never grows the frame). It is draggable like every window.
- **The kits are a dropdown** where the kit's name sits (stock `UIDropDownMenu` look): the five kits,
  then Rename, New Kit, Copy Kit (into the first free slot) and Delete Kit (empties the slot; the
  kit you carry can't be deleted). Save & Equip carries a kit; Save keeps it without carrying it.
- **Loadout page: parts on the left, a sub-menu on the right.** Every part of the kit is a row
  (primary, its three attachment slots, camo, secondary, its two attachment slots, lethal, tactical,
  perks 1-3, deathstreak, killstreaks 1-3) with its pick's icon and name and `<` `>` arrows. Clicking
  a row (or its arrows) shows that part's sub-menu: every option as an icon and a name (the kit's
  pick in green, locked ones red, ones that can't go in now grey), and the highlighted option's
  details: what it does, its numbers against the current pick (better green, worse red; a gun's
  four bars over the current pick's), how a perk stacks and what its Pro does, where it comes from,
  and what unlocks it with the progress. **Assign to Kit** puts it in. The arrows move the highlight;
  on the keyboard up/down walk the parts, left/right the options, Enter assigns (on the current pick
  Enter saves and equips). A perk shows Regular / Pro.
- **Rearm at kit stations:** every trainer's and innkeeper's gossip has "Rearm" beside "Edit your
  kits": magazines, reserves and equipment refilled (free, as talking to one already does), and the
  unlocked special rounds topped up for the quartermaster's fee when the character can pay.
- **Built today:** all of the above; the achievement unlocks for guns and gear; Pro perks and perk
  stacking (below).

### Pro perks

Every perk has a Pro version, as MW2's. A Pro is **unlocked by its challenge**: kills with the perk
in your kit (catalog.json's `pro_kills`, an achievement "<perk> Pro" under the perk's own in the
Achievements window, unlock kind PerkPro). Once earned, **each perk slot chooses Regular or Pro**
(the kit's Pro bits; a stacked perk's copies agree). The server refuses a Pro bit the account has
not earned. A GM's `.fusion pro on` still plays every Pro without the bits, for testing.

### Perk stacking

The same perk may fill more than one of a kit's three perk slots **only where stacking is
defined**. The rules are data (catalog.json `stack` and `max_stack`, written by
`server/contrib/fusion/womw_kit_data.py`); the server is authoritative (a perk in more slots than
allowed is refused, `PerkDoesNotStack`) and the kit window offers a perk again only where it may go.
Perks that touch gun damage, combat speed, killstreak costs or are on/off effects do not stack.

| Perk (lore name; MW2) | Stacking | Slots | Each copy | Pro (and its challenge) |
|---|---|---|---|---|
| Windrunner's Stride (Marathon) | additive | 3 | out of combat 5% faster (endless sprint once) | another 5% (150 kills) |
| Goblin Quick-Loader (Sleight of Hand) | diminishing | 3 | spells 15%, 22.5%, 26.25% quicker; reload stays halved | aim down sights 20% faster (150) |
| Gravedigger's Bandolier (Scavenger) | additive | 3 | 25% more looted gold; a magazine for every gun each kill, twice a body's rounds | another 25% gold (150) |
| Tinker's Rails (Bling) | special | 3 | one more attachment on the primary: 2, 3, then 4; one of each kind | the secondary's second attachment (200) |
| Field Quartermaster (One Man Army) | none | 1 | switch kits in combat; Rearm works in the field every 10 min (a free full load) | field editor ready in 30 min, field rearm every 5 min (120) |
| Thorium-Core Loads (Stopping Power) | none | 1 | bullets +40%, melee +20% | melee +35% (300) |
| Runeweave Kit (Lightweight) | none | 1 | 12% faster, half fall damage | fire 30% sooner after a sprint (150) |
| Mercenary Contract (Hardline) | none | 1 | killstreaks one kill sooner, +10% kill XP | deathstreaks one death sooner (150) |
| Shadowcloak Weave (Cold-Blooded) | none | 1 | no calls for help, gunfire heard less far | creatures notice you 15% later (150) |
| Sapper's Training (Danger Close) | none | 1 | explosives +25% | air support +25% too (150) |
| Warrior's Charge (Commando) | none | 1 | knife lunge 40 yd | no fall damage (100) |
| Hunter's Steady Hand (Steady Aim) | none | 1 | hip fire 35% tighter | no hip-fire penalty in the air (150) |
| Arcane Disruptor (Scrambler) | diminishing | 3 | creatures' casts at you 20%, 30%, 35% slower | 10% slower again (120) |
| Rogue's Soft Step (Ninja) | diminishing | 3 | stealth 3, 4.5, 5.25 yd harder to see (notice distance x0.7 once) | 2 yd more (120) |
| Sapper's Eye (SitRep) | additive | 3 | see stealth 5 yd farther | 3 yd more (120) |
| Last Rites (Last Stand) | none | 1 | fight on with a pistol while down | bleed out 10 s later (100) |

Tinker's Rails follows "one base attachment and one more per Rails", on the primary: none, one
attachment; one Rails is MW2's Bling (two); two Rails, three; three Rails, four (the cap). The
secondary carries one, and a second with the Rails' Pro (MW2's Bling Pro); three Rails no longer
spill over to it, as the third goes to the primary's fourth. As MW2's Bling, attachments never
overlap: a gun takes one of each kind (`Fusion::AttachmentKind`, the kit window's "Already has a
sight"), and every two of them must be a combination the gun has (MW2's own definitions):

| Kind | Attachments |
|---|---|
| Sight | Red Dot, Holographic, ACOG, Thermal |
| Underbarrel | Grenade Launcher, Shotgun, Grip |
| Muzzle | Silencer |
| Ammo | FMJ |
| Magazine | Extended Mags (its own kind: MW2 pairs it with FMJ) |
| Fire rate | Rapid Fire |
| Akimbo | Akimbo |
| Knife | Tactical Knife |
| Sensor | Heartbeat Sensor |

MW2's own exclusions (akimbo with a sight, rapid fire with an underbarrel) still hold. The gun is
held as two of them (its model shows at most two): a sight first, then an underbarrel, then the
other kinds that show, FMJ, extended mags and rapid fire last; every attachment's numbers count. A
gun picked up off the ground carries two. Deathstreaks have one slot and no Pro.

---

## 4. Companions

**Built today:** companion items (Bind on Equip, level and stage on the item, shown on its tooltip),
leveling 1-60 capped by the owner, evolutions, companion duels, mounts as companions' last forms, a
companion bar.

**Decided**
- Companions are **for every class**; with class pets gone, companions move into the **real pet slot**,
  which gives everyone the game's own pet bar (Attack, Follow, Stay, stances, autocast, keybinds).
- **Many new lines**, generated from existing creatures and curated: three distinct models per line,
  with sizes set from each model's real height:
  - stage 1 companion-sized (about 0.4x);
  - stage 2 normal mob size;
  - stage 3 mount-sized (about 1.6-2x).
- **Large non-humanoid final forms can be ridden** (spiders, bears, raptors, scorpids), not just the
  nine vanilla mount families.
- **Some humanoid companions carry guns**, a rare trait rather than the rule, using the NPC gunner
  system (a heavier gun at each stage). Humanoid last forms are large but not rideable.
- **Moveset:** a basic attack, a trait, and one ability gained per stage (stage 1, 2 and 3), plus a
  mounted move for rideable final forms. **Each evolution offers a choice of abilities.** Families fill
  roles: bruiser, skirmisher, controller, caster, support, humanoid.
- **Power:** strong in their role but low on raw damage. A strong companion can turn the tide of a
  battle; a weak one still makes a real impact, since every combat creature in this world has teeth.
- **Rare traits** roll when the item drops, stored on the item and shown on its tooltip. Rarity sets how
  many: Common 0, Uncommon 1, Rare 2, Epic 3, Legendary 3 with one legendary trait. Kinds: combat
  (Ironhide, Swift, Ferocious, Bloodthirsty, Berserk, Venomous, Thick-skulled), elemental (Fire-, Frost-,
  Shadow-, Storm-touched, Spellward), utility (Loyal, Tracker, Scavenger, Forager, Packmule), body and
  looks (Giant or Runt, Albino, Shadowcoat, Gilded), mount (Surefooted, Skybound, Tireless), humanoid
  (Gunner), legendary (Phoenix-blooded, Alpha, Twin-souled, Primal). **Some pairs combine into a named
  bonus** for players to discover (Venomous + Swift = Viper: the poison also slows).
- **Breeding** at **stable masters** (later also a Kennel among permanent structures, section 12): two
  adult (stage 3) companions of the same family; same species breeds true, two species of a family has a
  small chance of a hybrid colouring. The offspring is a new Bind on Equip companion item at level 1:
  each parent trait passes with about 35% (legendary about 3%), with a small chance of a new mutation,
  and it can pick from its parents' ability choices. Each companion breeds at most 3 times, with a
  cooldown and a reagent each time, so rare traits stay rare. Tooltips show lineage.
- **Fainting, monster-tamer style.** A companion beaten to 0 health faints, on its own (the owner's
  others still come out), and stays fainted through logouts and restarts until a **stable master**
  revives it (a small fee by level, wounds healed too), a **Reviving Draught** does (sold by general
  goods vendors and stable masters; half health), or about **an hour** passes (configurable; Loyal a
  third). A fainted companion can't be summoned or ridden. Losing a companion duel never faints it.
- **Control through the existing pet bar, aim-driven, usable with the gun up** (the one exception to
  "abilities need the hands up"): slot 1 is a context Command (crosshair on an enemy: attack it; on the
  ground: move there and hold; on nothing or a double tap: follow), then stances, the three abilities
  aimed at the crosshair with smarter autocast (a taunt when you take fire, a heal under half health, a
  root on a charging enemy), and the mounted move while riding. Built as the **Companion action
  system** (C): the companion's pet bar on the main bar's six large buttons (page 1 its moves: Command,
  the three picks, the ride or mounted move, the Twin-souled move; page 2 Assist/Aggressive, Hold,
  Follow, Stay), pressed by the slot keys or a click, right-click for autocast, and still Ctrl+1..0
  from any system. The stock pet bar is not shown. A ridden companion's buff and bar entry read
  "Ride <its form>" with its family's icon, never the borrowed mount spell's.

---

## 5. The arsenal

**Reach goal:** every gun and piece of equipment is a clone of its real-world counterpart in sound,
animation and handling, so it is obvious you hold a SPAS, a Barrett or an Intervention, but it looks
like advanced tech from inside the world, with its own name.

- **No faction split.** Each gun gets its **own creative origin**: a goblin pressure-blaster shotgun, a
  sniper with night elf moonglass optics, an LMG fed by a Dark Iron molten core, Forsaken plague-round
  pistols, an SMG on stolen Kirin Tor mana cells. A short origin story goes on its tooltip.
- **Kept from MW2:** animations, handling numbers, silhouette, the core of each sound.
- **Changed:** materials, decoration, names, some sound tails.

**Stages, cheapest first**
1. Names and flavour for every weapon, attachment, camo, perk, killstreak and piece of equipment.
2. Material reskins at runtime (we draw the guns ourselves): brass, thorium, engraving, glowing runes.
   Meshes and animations untouched; we ship only our own art.
3. Bolt-on parts attached to the gun's bones: gnomish scopes, mana cells, muzzle brakes, pressure tanks.
4. New models built to MW2's skeleton, reusing its animations, gun by gun.

**Equipment and killstreaks** (names to settle): frag → Thorium Grenade, C4 → Seaforium Charge,
flashbang → Arcane Flash Bomb, UAV → Gnomish Spy Gyro, Care Package → Goblin Cargo Drop, Predator
Missile → Goblin Rocket, Chopper Gunner → Gyrocopter Gunship, AC-130 → Zeppelin gunship, Sentry →
Dwarven Sentry Turret, Tactical Nuke → Gnomeregan Radiation Bomb.

**Sentry guns become a killstreak only**, removed from construction.

**Lore killstreaks (built):** the predator is the Goblin Rocket (steered from its own camera),
the attack helicopter the Sky Rider Gunship (gryphon or wind rider), the chopper gunner the
Gyrocopter Gunship, the harrier the Gnomish Sky Hornets, the stealth bomber the Siege Bomber and
the sentry the Dwarven Sentry Turret, all drawn with 1.12 creature models the server summons and
flies (`fusion/SkyStreaks.h`). There is no zeppelin creature model in 1.12 (the zeppelins are
transports), so the Horde's bomber is a big wind rider for now. GMs test them with `.streak`
(vps/ADMIN.md).

They stand up to fire, as MW2's do: every craft but the rocket can be shot down by the caller's
enemies (health in fresh players of the caller's level: gunship 8, gyrocopter 12, hornet 6,
bomber 10, turret 5; blasts count double against craft), bursts and falls, and credits the
shooter (`streaks_downed`). Gunships fly in from the horizon and away when done; the turret
deploys, sweeps its arc, turns to its target and spins up; every craft turns first on whoever
is on its owner, then on whoever shoots it, then players, then those already fighting. Bombing
runs show their landing marks to everyone near before the bombs fall, and the owner's HUD
shows each craft's time, health and kills (world op 39, `Sky::News`).

---

## 6. Weapon pickups

- Guns can be picked up and used on the fly, as in MW2: from dead NPC gunners, from dead players, and
  possibly from rare world crates.
- Look at it and hold F: it replaces the weapon in that slot **for this life**, with the ammo it had.
- **Never saved:** death, a kit swap or logging out clears it, and the kit's own weapons return.
- The server tracks it.
- A pickup ignores achievement unlocks: a new character can use a dead gunner's LMG until they die.

---

## 7. Stats

Every vanilla item keeps its stats; what the stats do changes. **Gear never raises raw gun damage.**

| Stat | Effect |
|---|---|
| Strength | Melee and shield power, recoil control, less movement penalty with heavy guns |
| Agility | Faster ADS, weapon swaps and sprint-out, a little hip-fire accuracy, dodge |
| Stamina | Health (diminishing returns) and how long you last when downed |
| Intellect | Class ability power and mana; equipment strength (flash and stun duration, smoke size, Claymore radius) |
| Spirit | How soon and how fast health regenerates out of combat; mana regeneration |
| Armor | Bullet damage reduction, capped (about 30% on full plate); less flinch. FMJ cuts through part of it |
| Hit | Tighter spread, less bloom |
| Crit | A chance that a body shot counts as a weak-spot hit at the headshot multiplier |
| Defense | Less flinch; fewer weak-spot hits taken |
| Spell power | Class abilities only |
| Attack power | Melee and melee abilities only |
| Ranged attack power | Handling: steadier sway, faster reloads; never damage |
| Weapon skill | Melee only, as in vanilla. "+Guns", "+Bows", "+Crossbows" on items do nothing for MW2 guns |
| Resistances | Against elemental class abilities and special rounds |

- **Item procs fire from gun hits** at a reduced rate, so "chance on hit" weapons and trinkets become build
  pieces.
- **Stat thresholds** unlock small bonuses (for example 150 Agility: the first ADS after a sprint is
  instant; 200 Spirit: regeneration starts a second earlier).
- **Archetypes favour different stats**, so gear follows the build.
- **Set bonuses keep working.**
- All conversions live in one tested table in the rules library.

---

## 8. Death

- **Durability is removed** (built: no loss on death or from hits, repairs free).
- **Downed instead of dead** (built: `fusion/Downed.h`, `FusionDowned.cpp`): a hit that would kill
  leaves you down at 1 health. You crawl (forced prone, slow), cannot attack, cast or use anything
  but a revival kit, and bleed out after 20 s plus 1 s per 25 Stamina (at most 60 s). Hits on a
  downed player drain a small finisher pool (a tenth of their health); the hit that empties it
  kills. Creatures turn to another target after 2 s and never pick a downed player up anew; with
  nobody else around they keep at it.
  - **Fighting back while downed needs the Last Stand perk** (crawl and fire your pistol, as in MW2:
    the secondary if it is a pistol, else the USP). Final Stand gets back up by itself after 10 s,
    once a life.
  - **No hold-F revive.** A downed player comes back up only from a resurrection spell
    (Resurrection, Redemption, Rebirth, Ancestral Spirit; also Goblin Jumper Cables) at 30% health,
    or a revival kit: First Aid's Field Revival Kit and Engineering's Gnomish Revival Coil, used on
    a downed ally (2.5-3 s) or on yourself while down (6-7 s), holding still.
  - **Later:** more self-revive paths (a shaman's Reincarnation, a warlock's Soulstone, an
    alchemist's stim).
- **Real death:** a corpse run, as in vanilla.
- **Respawn points:** Tactical Insertions are wanted in some form, but the world's scale needs care (a
  planted beacon can skip a long corpse run). Graveyard revival gets reworked in proportion to how
  generous Tactical Insertions turn out. A homestead bedroll may be an optional respawn point.

---

## 9. Ammo and health

- **Ammo is limited: the balancer between guns and classes** (design/BALANCE_GUNS_VS_CLASSES.md,
  `fusion/Ammo.h` AmmoRules, `Fusion.Ammo.*`). Guns win the first second of a fight; a load (an
  AK-47's 150 rounds) is about 35 normal creatures or 3-4 dungeon elites. **Rearming is free**: going
  back to a trainer or innkeeper (talking to one, or Rearm in its gossip or on the bar while resting
  in an inn or city) refills every magazine, reserve and piece of equipment; the trip is the cost.
  (A per-round price was tried and dropped after the 2026-10-26 playtest.) Out of combat a gun
  trickles back to one magazine at half a round a second; a respawn tops the reserve up to half.
  Gunners' bodies give half a magazine (elites three times that), enemy players a magazine, a
  dungeon boss restocks everyone who fought it. Gravedigger's Bandolier (Scavenger) doubles what a
  body gives and brings a magazine for every gun each kill; Field Quartermaster (One Man Army) lets
  Rearm work in the field every 10 minutes (Pro: 5); Packmule still brings magazines. Class
  abilities cost no ammunition, so long fights lean on them. `Fusion.Ammo.Limited = 0` restores the
  old model (half the reserve back in the field, a full load at respawn).
- **Health is a mix:** MW2-style regeneration after a delay (driven by Spirit), potions and bandages for
  instant healing, healers for sustain mid-fight.

---

## 10. A more dangerous world, without rewriting quests

Killing is faster with guns, so the world gets deadlier around the quests rather than the quests being
rewritten:

- **Humanoid camps become gunners automatically**, by faction and level: Defias, Scarlet Crusade,
  Syndicate, Dark Iron and the like (the NPC gunner system already does this per creature).
- **Gunfire draws the camp** (built), elites lead patrols and roaming squads.
- **Creature damage and health scale** so their time-to-kill against players rises with guns.
- Quest rewards keep their value through the stat rework.

---

### Bounty boards (built)

A repeatable reason to stay in a zone, made from the zone itself rather than written by hand
(`game/Fusion/FusionBounties.cpp`, rules in `fusion/Bounties.h`, placement in
`sql/custom/azeroth_warfare/27_bounty_boards.sql`):

- **A Bounty Board** (the Wanted Board model) stands beside the flight master of each major town
  or outpost of the open-world zones, 31 zones and about 50 boards; a contested zone has one per
  side, a neutral town (Booty Bay, Gadgetzan, Everlook...) one for both.
- **Five bounties a zone a day**, the same for everyone, re-posted at the daily reset (06:00
  server time): a kill from the low, middle and high third of the zone's levels, a collect, and a
  hunt (a rare or named elite that respawns within three hours) or a camp (two or three creatures
  of one faction standing together). Every target is checked against the live world at startup:
  normal open-world spawns only, game-event spawns, instances, townsfolk, critters and our own
  content left out; enough spawns in the zone that a bounty is never a wait.
- **Quests, not a tracker of our own:** each bounty is a quest the server makes in the reserved
  ids 95000-97999 (three blocks taking turns by the day), so the quest log, the quest tracker and
  kill and loot credit all work as for any quest. Taken and turned in at the board's own window
  (fusion channel 11), never shared.
- **Rewards scale with level:** about a vanilla kill quest's experience, coin with the square of
  the level, more for a camp (x1.25), a hunt (x1.5) and an elite hunt (x2); reputation with the
  board's town; the Bounty Hunter track (a title at 10, a camo for every gun at 50, the Manhunter
  title and a second camo at 100).
- **Five a day per character**, counting those held, so every bounty taken can be finished today.
  A bounty runs through its day and the next. GMs: `.bounty list`, `.bounty refresh` (a re-roll).

### Wanderers (Phases 1 and 2 built, off by default)

A third kind of character between players and NPCs (design/WANDERERS.md): named people of the
playable races who walk the roads between towns, shoot the local monsters with MW2 guns on the
same rules players use, loot, rest at inns, greet the players they pass (and remember them), call
for help when losing, and take kill bounties from the boards, hunt them and turn them in. They are
server-side creatures that exist only near players and walk their roads on paper otherwise, so a
zone's few residents cost almost nothing (`game/Fusion/FusionWanderers.cpp`, rules in
`fusion/Wanderers.h`, places in `sql/custom/azeroth_warfare/28_wanderers.sql`). Phase 1 covers
Elwynn and Westfall, Durotar and the Barrens; `Wanderers.Enable` turns them on, GMs use
`.wanderer list|info|goto|spawn|despawn`. Phase 2 gave them lives (design/WANDERERS.md section 13):
nine trades (fisherman, miner, herbalist, trapper, peddler, courier, sellsword, minstrel, pilgrim)
with homes, workplaces found in the world's own data, markets and daily schedules; natures that
decide fight or flight and detours; friends, rivals and news; a gossip menu, a stall of their real
goods and a short hire; and nine more zones (Redridge, Duskwood, Stormwind; Orgrimmar, Mulgore,
Thunder Bluff; Tirisfal, Silverpine, the Undercity). Next: their own nameplate colour, duels,
asking for help with a bounty, companions.

### Dungeons and raids

**Boss mechanics are not touched.** Bosses keep their scripts exactly as they are; only how dangerous
they are is scaled: their health (against gun damage and group size) and the damage they and their adds
deal, through the same systemic scaling as section 10. Downed players can be revived mid-fight.

---

## 11. Mounts and flight

- **Shoot while mounted; gunfire knocks you off.**
- **Brief flight:** gliding leaps, and launch points (towers, updrafts, flight masters) for short
  flights.
- **Skyriding (the flying companion lines):** Dragonflight-style momentum flight, never free: no
  climb on demand or hover; a dive trades height for speed and a pull-up speed for height, so
  range comes from height and jumping off high points is how to go far. Launch and Surge
  Forward spend vigor charges that refill, faster on the ground. The server holds every flight to
  the physics' energy law, so a fly or speed hack is ended (COMPANIONS.md "Flying mounts").
- **Player ships (first slice built):** a Sloop on the water and an Airship in the air, commissioned
  at a Shipwright for a deed, hundreds of gold and rare materials, piloted from the helm with
  others riding on deck, kept across restarts, with upkeep, wear, salvage and the Ship's Bell
  (design/PLAYER_SHIPS.md).
- **Map work:** vanilla maps have unfinished edges, invisible walls and holes in the sky box; making
  them safe from above needs altitude limits and server checks so flying cannot break the world.

---

## 12. Professions (draft)

**Principle:** professions never make guns stronger. They supply consumables, cosmetics, armor,
construction and the economy. Engineering collides most with the kits and is reworked first.

| Profession | Focus |
|---|---|
| Engineering | Field gear: extra stocks of lethal and tactical equipment beyond the kit's free charge, killstreak components, gun cosmetics (camos, engravings, glows), gadgets |
| Blacksmithing | Armor plates, melee weapons, construction hardware |
| Leatherworking | Light armor, bandoliers and ammo pouches (more spare ammo, not bigger magazines), holsters (faster swaps) |
| Tailoring | Cloth armor, ghillie and camouflage suits, bags |
| Alchemy | Potions, combat stims (short sprint, regeneration or focus boosts), flasks |
| Enchanting | **Consumable specialized rounds** (incendiary, frost, armor-piercing, mana-burning and the like: effects with trade-offs, not flat damage), melee and armor enchants, arcane cosmetics for guns (rune glows, tracer colours) |
| Construction | Two paths: **field structures** (barricades, sandbags, walls) and **permanent structures** (a Kennel, workshops and the like). Building pieces and homesteads are built today. See the rules below |
| Gathering | Mining, herbalism, skinning, woodcutting, fishing: in first person with the tool in hand (built, below) |
| Cooking, First Aid, Fishing | Mostly as they are; First Aid may become field medicine |
| Minstrelsy | A secondary profession: music as a rhythm game on Utility's bar, mostly out-of-combat party buffs (built, below) |
| Inscription | A primary profession: milling herbs into pigments and inks, vanilla's scrolls, and **glyphs that tune a kit** (built, below) |
| Jewelcrafting | A primary profession: prospecting ore into gems, jewelry, and **cut gems socketed into a kit's guns** (built, below) |

### The professions, as WoW's (built)

Construction and Minstrelsy work and look as the stock 1.12 trades do. Both are in the stock
Skills tab under Secondary Skills with their rank bars (the client can't learn a SkillLine.dbc
row, so the layer hands the engine its own lines). Each is trained at its trainer in vanilla's
four tiers, and each has a trade window that *is* the stock TradeSkillFrame: the recipes known in
their difficulty colours under headers, reagents with counts, Create and Create All, the stock
sounds. Minstrelsy's recipes are its instruments (learned from the trainer, made as items);
Construction's are its pieces, each made into a **kit** (materials taken and the skill-up rolled
at making, by the piece's colour; salvage builds but teaches nothing), and a kit is placed in the
world with Construction's **Build Structure** ability on Utility's bar (build mode; placing takes
the kit and teaches nothing). A game master or a free realm still builds without kits. Kits are
kept per character (`character_fusion_construction_kit`; the professions channel 12).

**Furniture** (built) is Construction's decor layer: 55 pieces of WoW's own doodads made as kits
(and a few sold by the Plot Steward or found when cutting lumber), placed on a quarter-yard grid
with 15-degree turns (free with Sprint), on floors, on tables (small things) or hung on walls,
with no cap on pieces. Many have a use with F: chairs and benches seat you, beds lie you down and
rest you at your homestead, chests, footlockers and wardrobes open the plot's shared stash,
bookshelves read lore and guides, the anvil, forge, cooking fire, loom and alchemy table work as
WoW's crafting stations, the mailbox is a mailbox, the training dummy reports your damage per
second, a trophy plinth shows a companion's likeness, and a bard's stage makes songs land
stronger. Banners, rugs, lights and the like are for the look. Details:
`server/src/fusion/docs/CONSTRUCTION.md`, "Furniture".

**B and the micro menu's Professions button** open the Professions hub: every profession the
character knows, stock and the layer's, with its bar, a button to open its trade window (Mining's
is Smelting; the stock trades cast their opener as the spellbook does) and the abilities it puts
on Utility's bar (a click puts one there). Construction's row also opens the homestead window.

**An instrument is a form.** Readying an instrument turns Utility's bar into its notes, as a
druid's form changes the bar: each lane's button reads its note or chord (the Hand Drum's low and
high drum, the Lute's G, C and D...), the other slots go quiet, and beside the bar are Song (the
next song) and Leave (bare hands, the bar back). Outside a song a note's key plays it for fun with
WoW's sounds; in a song the keys are the lanes the server judges, as before.

### Utility's tools (built)

The trades' tools are held in the hands in Utility (the weapons put away): a Mining Pick, a
Skinning Knife, bare hands for Herbalism, a Fishing Pole, a Woodcutting Axe, a Blacksmith Hammer and
the Repair Hammer, each on Utility's bar only (bare hands are Classic's and Utility's). Fire swings
the tool at what the crosshair is on; the work is physical (a vein takes 2 to 4 swings by skill, a
herb or a corpse 1 to 3) and the last swing is WoW's own gathering: its skill, loot, depletion and
skill-ups. The pole casts and reels, the smith's hammer at an anvil opens Blacksmithing. The tools
are vanilla's own requirements (the Mining Pick and Skinning Knife, any fishing pole).
**Hands** is Utility's first slot. Into Utility the hands hang relaxed, lowered and open, out of
a fight: the attack button does nothing. Equipping Hands (its key) raises the fists (the slot
glows gold, "R L"), and the attack button punches; pressed again they lower. A tool's key puts
the fists down and the tool in hand; Hands with a tool out puts it away into raised fists.
Utility's bar shows only the tools the character can use: the tool carried (bags or equipped;
Herbalism asks none) and its trade known (the Repair Hammer asks only owning it). The server tells
the client which (channel 10 op 9) at login and within a second of a change, and the client adds
or removes the buttons; bare hands are always there.

### Minstrelsy (built)

Music is a secondary profession every class may learn (like Fishing or Construction it takes no
primary slot). Rules in `fusion/Minstrelsy.h`, the game in `game/Fusion/FusionMinstrelsy.cpp`, the
client in `client/src/minstrel`, world data in `sql/custom/azeroth_warfare/30_minstrelsy.sql`.

**Training and skill.** A Minstrel Trainer (creature 94100) stands by the innkeeper of each capital
and of Goldshire, the Crossroads, Booty Bay and Gadgetzan; its window trains Apprentice (cap 75,
level 5, 1s), Journeyman (150; level 10, skill 50, 5s), Expert (225; level 20, skill 125, 25s) and
Artisan (300; level 35, skill 200, 1g), teaches the instrument recipes, and sells a starter Hand
Drum (so a new minstrel can begin at once; every innkeeper sells it too). The skill is kept in
the fusion's own skill table (`character_fusion_skill`, key 9002, beside Construction's 9001): the
client can't learn a new SkillLine.dbc row, and vanilla's unused secondary lines have no race/class
rows nor a client frame that would show them, so the fusion keeps the skill and hands the client's
Skills tab its own line (above, "The professions, as WoW's"). Skill rises only from songs that land: each song has vanilla's colour band from its
requirement (orange, yellow +25, green +50, gray +75); the chance is vanilla's for the colour times
how well it was played (nothing under a fifth, full from 0.8), and a song played at 0.9 or better
while orange or yellow gives two points. A robotic performance teaches as a good one at most.

**Instruments are Utility tools** (abilities 17-22, items 94101-94106), on Utility's bar only while
carried and while the skill reaches the instrument: Hand Drum 1, Lute 50, Flute 100, Harp 150,
Bagpipes 225 and Thunder Drums 275. **Minstrels make every instrument** (`Minstrelsy::Recipes`): each
recipe is learned from the trainer at the instrument's own requirement (50 copper to 2 gold) and
made anywhere from Minstrelsy's trade window (the Minstrelsy key or the hub), of other trades' goods: lumber (Construction's), leather
and hide, thread, and silver, mithril or thorium bars. Making one teaches Minstrelsy by vanilla's
colours from the requirement. The Thunder Drums' recipe is taught only to whoever hands over
their score (item 94110), a rare drop (0.2-0.5%) of the Smolderthorn and Vilebranch trolls'
drummers and casters; no other trade makes instruments. Utility's bar has a third page (buttons 210-215, the last of
bonus bar 12, taken from Modern's unused fourth page) to hold them. With one readied the bar is the instrument's form (above): its Song button
picks the next song (the line under the crosshair names it), fire plays it and fire again stops it.

**Playing** is the Bard's engine unchanged: seeded charts mirrored by the client, Perfect / Good /
Miss in the instrument's windows, holds and chords, the combo, potency from the instrument's floor
(50%) to its ceiling (110% to 150%), the server judging the presses and rejecting impossible streams
(lanes, order, the future, floods), robotic timing capped. The note highway sits over the bar's
buttons, which in Utility are Utility's. The ops are the action systems' channel 10: a song starts
as a tool strike (op 3, the target the song), then 50 notes, 51 start, 52 stop, 53 verdict, 55 the
songs on a listener, 56 the skill, 57 a song heard, 58 a tip, 59 its answer, 60-62 the window.

| Instrument | Songs (skill) |
|---|---|
| Hand Drum | Marching Cadence (travel, 1), Hearthside Rhythm (inn, 20) |
| Lute | Ballad of the Road (travel, 50), Tavern Ballad (inn, 70), Fireside Air (fire, 90) |
| Flute | Wanderer's Air (travel, 100), Lullaby of the Hearth (inn, 125), Ember Song (fire, 140) |
| Harp | Hymn of Rest (inn, 150), Hymn of the Hearth (fire, 175), Starlight Nocturne (travel, 200), Hymn of Warding (combat, 225) |
| Bagpipes | March of the Clans (travel, 225), Skirl of the Clans (combat, 250) |
| Thunder Drums | Grand Revel (inn, 275), Heartbeat of the Earth (combat, 285) |

**Effects** scale with the potency, each capped:
- *Travel:* the party within 30 yd runs 6% to 10% faster (times the potency, at most 15%) for 10 to
  20 minutes, out of combat only (suppressed in a fight, not mounted).
- *Inn:* played in a resting place (an inn or a city); everyone friendly within 20 yd who is resting
  gains rested experience, 1% to 3% of a level (times the potency, at most 5%), once per listener per
  half hour.
- *Fire:* by a campfire (spell focus 4, 8 yd); the party regenerates 0.25% to 0.35% of its health and
  mana a second out of combat (at most 0.6%) for 10 to 15 minutes, and their other songs last half
  again as long (once each).
- *Combat (225+):* Hymn of Warding 3% less damage taken, Skirl of the Clans 3% more movement, Heartbeat
  of the Earth 0.3% health a second; each times the potency, at most 5% (0.4% a second), for 20 s,
  with a 30 s cooldown on the song. No stuns, sleeps or strong debuffs.

**Combat.** Only the combat songs may be started in combat. Entering combat cuts any other song
(nothing lands, nothing is learned); a stun, silence, fear or confusion, death, leaving Utility or
readying another tool cuts any song; a root and damage do not.

**Tips.** Everyone within 20 yd of a landed song is told who played it and may tip within a minute
(1 gold a tip at most; 5 gold given and 10 gold taken a day per character; never between characters
of one account; once per song). At an inn or in a city the crowd (friendly humanoids and wanderers
within 15 yd) tips a song of quality 0.5 or better that is not gray to the minstrel: up to a
fifteenth of the day's cap (50 copper times the level) per song before five listeners, never past
that cap nor 60 silver a day for the whole account (`character_fusion_minstrel`).

### Inscription and Jewelcrafting (built)

Wrath's two trades on 1.12's herbs, ores and gems, built as the layer's other trades are (rules
in `fusion/Crafts.h`, the game in `game/Fusion/FusionCrafts.cpp`, the client in
`client/src/professions/{crafts,trainer,augments}.rs`, world data in
`sql/custom/azeroth_warfare/35_inscription_jewelcrafting.sql`). Unlike Construction and Minstrelsy
they are **primary professions**: learning one takes one of the two primary profession slots
(vanilla's free profession points; unlearning at the trainer gives it back), and they show in the
stock Skills tab under Professions. Their skills are kept in `character_fusion_skill` (keys 9003
Inscription, 9004 Jewelcrafting), their learned recipes in `character_fusion_craft_recipe`.

**Training.** An Inscription Trainer (creature 94300) and a Jewelcrafting Trainer (94450) stand
by the Minstrel Trainer at each capital's inn and at Goldshire, the Crossroads, Booty Bay and
Gadgetzan. Their window (professions channel 12, ops 20-24) trains vanilla's four tiers
(Apprentice 75 at level 5, 1s; Journeyman 150 at 10, skill 50, 5s; Expert 225 at 20, skill 125,
25s; Artisan 300 at 35, skill 200, 1g), teaches recipes (in their skill colours, the nearest
first), sells the trade's goods (Light and Resilient Parchment; the Jeweler's Kit every cut asks)
and unlearns. Everything is made at the trade window (the stock TradeSkillFrame, from the
Professions hub or the trainer's window), the skill-up rolled by the recipe's colour.

**Inscription.** *Milling*: five of one herb into two or three pigments (Alabaster from
Peacebloom, Silverleaf and Earthroot at 1; Dusky at 25; Golden at 75; Emerald at 125; Violet at
175; Silvery at 225); every mill comes with the profession. *Inks*: two pigments each (Moonglow,
Midnight, Lion's, Jadefire, Celestial, Shimmering). *Scrolls*: vanilla's own Stamina, Intellect,
Spirit, Strength, Agility and Protection scrolls I-IV, of ink and parchment. *Glyphs*: of ink and
parchment, below.

**Glyphs modify a kit.** Every kit has three Major and three Minor glyph slots, opened at
character levels 15/30/50 and 20/40/60. A glyph is inscribed into the kit carried where kits
change (a trainer or innkeeper, the field editor, a GM), the glyph item used up; a new glyph over
an old one destroys the old, and a kit holds a glyph once. Majors tune the kit's handling (Quick
Draw: swaps 12% quicker, ADS 4% slower; Steady Hand; Iron Skin; Second Wind; Heavy Gunner), its
equipment (Sapper), its field tools (Quartermaster: the field rearm a quarter sooner; Last Rites:
5 s more in Last Stand) or detection (Hunter's Eye, Shadow), most with a small cost elsewhere;
the **class glyphs** tweak class abilities and ask the WoW class (Quickened Words: casters' spells
6% quicker, reloads 5% slower; the Bladesworn: warriors', paladins' and rogues' melee 6% harder,
ADS 4% slower; the Tracker: hunters see stealth 5 yd farther). Minors are conveniences (Long Road:
3% faster out of combat; Feather; Gravedigger: 10% more gold; Field Editor: 10 minutes sooner;
Veteran: 3% kill XP; Sprinter). Glyphs feed the same hooks the perks use (cast time, melee,
detection, falls, loot, field tools) and the kit's handling; **nothing touches gun damage**.

**Jewelcrafting.** *Prospecting*: five of one ore (Copper at 20, Tin 50, Iron 125, Mithril 175,
Thorium 250) into one or two of vanilla's gems (Tigerseye and Malachite to Azerothian Diamonds
and Arcane Crystals). *Cuts*: each raw gem into a **cut gem** for the guns (17, red, yellow, blue,
two-colour, prismatic and two meta). *Materials and jewelry*: copper wire and bronze, mithril and
thorium settings; twelve rings and necklaces of vanilla's stats, level 5 to 60.

**Gems are augments for a kit's guns, fitted as attachments are.** The primary has two sockets
and a meta socket, the secondary one; a jeweler (Jewelcrafting 275 of their own) sets a third in
their primary. As a gun takes one attachment of each kind, it takes **one gem of each kind**: no
two gems on a gun tune the same handling. A sway gem asks a scope (a sniper, the ACOG or the
Thermal; MW2's own "sway only matters in a scope"). The **meta gem** wakes only while the gun's
other sockets hold gems of two colours or more (a prismatic gem is every colour). Gems tune
handling (`Stats::Out`: ADS, swap, sprint-out, recoil, hip spread, bloom, sway, reload, flinch,
and the meta's regeneration delay or weak-spot hardening), 2% to 8% each, inside the stats'
limits; they count only while their gun is in hand, and only on the kit's own guns (never a gun
picked up, nor a killstreak's). Socketing at a kit station uses the gem up; a new gem over an old
one shatters it.

**The Glyphs & Gems window** (the Professions hub's Inscription and Jewelcrafting rows; ops
30-32) shows the kit carried's glyph slots and sockets, what is in each, and the glyphs or gems in
the bags that may go there; the server checks every rule. Kept per character by kit slot in
`character_fusion_kit_glyph` and `character_fusion_kit_gem`. GMs use `.profession` with
"inscription" ("insc", "scribe") and "jewelcrafting" ("jc", "jewel").

### Construction in the open world

- **Both paths can be placed anywhere in the open world.**
- **Open-world structures are not protected:** guns and spells destroy them easily. This stops crowding
  and random clutter. (Purchased homestead plots stay fully protected, as built today.)
- **Building is materially expensive, with no cap on pieces** (built): cost and upkeep are the
  limit. A wood wall is 16 Rough Lumber and 4 Copper Nails, a stone wall 20 Solid Stone and 5 Iron
  Brackets.
- **Destroying a structure rewards the attacker** with part of its materials.
- **Decay and maintenance** (built): out in the world every piece loses 2% of its health a day and
  falls when nothing is left (about 50 days untended); on a bought plot nothing decays while its
  upkeep is paid. The **Repair Hammer**, a Utility tool sold by construction suppliers and made by
  smiths, mends a damaged piece a tenth a strike for a share of its own materials (a full repair
  about 60% of building it), never under fire (30 s after a hit), and never for Construction skill.
- **Reinforcement:** very expensive upgrades make a structure highly resistant to destruction.

---

## 13. PvP

- **Battlegrounds as MW2 modes:** Warsong Gulch as Capture the Flag, Arathi Basin as Domination,
  Alterac Valley as Ground War; new Team Deathmatch and Search & Destroy maps on small WoW areas
  (Deadmines streets, Booty Bay docks).
- **Killstreaks are PvP only** (built).
- World PvP and player raids on bases: left for later.

---

## 14. Progression

- **Leveling is faster** (guns kill faster) and the world is more dangerous (section 10).
- **Prestige: you choose what to prestige.** Three tracks, each with its own rewards:
  - **Player level** (reset from 60);
  - **Gun unlocks** (reset the achievement unlocks);
  - **Professions** (reset a maxed profession).
- **Massive rewards for all three at once:** at milestones where everything is maxed together (every gun
  and attachment unlocked, max level, max professions).

### Achievements (built, Wrath-style)

The account's achievements (`fusion/Achievements.h`, the game in `FusionAchievements.cpp`, the
window in `client/src/achievements.rs`) unlock the arsenal (the tabs Weapons ... Killstreaks and
Milestones, as before) and, Wrath's way, track everything else:

- **Categories** (the window's list, the arsenal's unlock tabs indented under their heading):
  General, Quests, Exploration, Player vs. Player, Dungeons & Raids, Professions, Reputation,
  World Events, Guns & Kits, Companions, Wanderers & Bounties and Feats of Strength.
- **Points** on every achievement (Wrath's 10, 25 and 50; the arsenal's unlocks 5 or 10, the old
  titled feats 25; Feats of Strength none), shown by each row and as the window's total.
- **Criteria**: the counters (`Stat`) gain Wrath's kinds: a profession's skill (stock lines and
  the layer's 9001-9004), things made, glyphs inscribed, gems socketed, herbs milled, ore
  prospected, songs played and played near perfectly, a faction's rank and the Exalted count,
  battlegrounds played and won (by map), each dungeon's and raid's last boss, world event quests
  and the events celebrated, quests by zone, companions caught, wanderers defeated, hired and
  traded with, and fully glyphed kits.
- **Meta-achievements** ask for other achievements (`Stat::Achievement`), and points tiers for
  the points earned (`Stat::AchievementPoints`); both are worked out from the earned set, never
  stored, and earn in the same pass as what they ask for.
- **Titles** as rewards: the Scribe, the Gemcutter, the Bard, Master Builder (each trade's
  Artisan), the Seeker (500 points), the Loremaster, Dungeon Master, Battlemaster, the Wayfarer,
  the Exalted, Patron of the Wanderers, the Walking Arsenal, Jack of All Trades and the Insane.
- The first set is about 200 achievements across our systems (guns, kits, companions and their
  battles, Construction, Minstrelsy, Wanderers, bounties, every profession including the two
  new). There is no dungeon finder.

---

## 15. Platform

### The HUD (built)

The stock interface, re-fitted around the four action systems (client crate::main_bar, crate::hud):

- **The action bar is six larger buttons** (1.42 times stock, on the stock art's first piece, which
  holds exactly six wells, scaled with them), centred at the bottom with the page piece, the micro
  menu and the bags after it as the stock bar has them. The XP and reputation bars run over the
  micro menu and the bags.
- **The current system** shows as an emblem (rifle, sword, pick) where the page arrows were, beside
  the page numeral (M1, C3, U2), and by name and page over the bar's top left. The emblem's tooltip
  names the keys bound now (X, Z, Alt with the wheel, H). A switch fades the buttons in.
- **Per system:** Modern's column over the bags holds the magazine and reserve, the gun, the special
  round and the grenades left, and its crosshair blooms; Classic's is the stock unit frames (health,
  mana) and the class resources as buff icons over the bar, with a still gold crosshair; Utility's
  column holds the tool in the hands and a gather bar, with a dot for a crosshair.
- **What the hands hold, on the bar** (client crate::hand_slots, in every system): a slot whose
  thing goes into a hand (a spell that readies, Attack, a gun, a grenade, Utility's Hands, a tool,
  an instrument) wears the stock button's border glow, the one an equipped item wears: faint
  grey when not in hand, **gold with a gold "R"** at the button's foot in the main (right) hand,
  **pale blue with a blue "L"** in the off (left) hand, gold with "R L" in both (a dual cast,
  raised fists). Instants (a shout, a strike, a killstreak, the rearm) keep the plain look. The
  tooltip adds "Equipped: Main Hand" (Off Hand, Both Hands) or "Equippable (not equipped)". A
  new system's abilities (Companion) add one arm to `hand_slots::ability_mark`.
- Killstreak timers sit top left under the player frame, the sprint gauge at the column's foot. All
  of it scales with the UI scale and keeps clear of itself from 1024×768 to ultrawide.
- **Interface options:** Large Action Buttons (on), Show Action System Label (on), Show Gryphons (off).

### Action systems and controls (built)

Four action systems share the main bar's six large buttons, each with its own saved pages
(client crate::action_systems): **Modern** (the gun and its gear), **Classic** (WoW's combat, the
two-hand action system), **Utility** (the weapons away, the trades' tools), and **Companion** (the battle
companion's pet bar, crate::companion_bar). Companion is a view over the hands' system: the hands
keep what they hold, and X, Z or C again leave it.

- **X** Modern / Classic, **Z** weapons away (Utility) and back, **C** the Companion bar and back,
  **Alt with the wheel** the page (never the camera's zoom, in either view).
- **Crouch is a tap of Left Ctrl** (or the unbound Crouch binding): it toggles on Ctrl's release, and
  only when nothing else went down while Ctrl was held, so Ctrl+1..0 (the companion's slots),
  Ctrl+Space (**Launch** while riding a flying companion: take off from the ground, climb in the
  air), Ctrl+Alt (the locked cursor) and a Ctrl-click never crouch. Space stands you up.
- **Prone is a held Left Ctrl** (MW2's held crouch): a bare Ctrl held 0.35 s goes prone while still
  held (its release is then nothing), and held again from prone stands you up; a tap from prone
  rises to crouch. The Crouch binding held 0.3 s goes prone too. A chord never goes prone. The
  server's prone rules (lower hit boxes, steadier aim, quieter detection, the crawl speed, the
  prone-kills stat) apply to every prone player, and the downed crawl is forced prone.

- **A tutorial** for first person, kits and guns, at some point.
- **The browser version** is to be considered, together with the move to our own IP.

---

## 16. Toward our own game

The long-term goal is a game of our own, free of external IP.

- **Ours already:** the server rules library, the client layer (benilla-mw2), classes and kits,
  companions, construction, the balance tables.
- **Borrowed:** the WoW client and its assets, MW2's assets.
- **So:** keep names, models and sounds behind data so they can be swapped; put effort into systems
  rather than WoW-specific content.

---

## 17. Open questions

1. Each class's 3 core abilities, and every option's exact design.
2. Option prices by tier.
3. Companion trait numbers and combinations; construction costs, salvage share and reinforcement tiers.
4. Dungeon and raid scaling numbers (boss and add health and damage).
5. Prestige: each track's rewards, and the all-three milestones.
6. World PvP and player raids on bases.
7. The exact downed timings, each class and profession self-revive path, and the Tactical Insertion and
   graveyard rework.
8. Stat conversion numbers and threshold bonuses.
9. Which professions change first, and how far.
10. The tutorial, the browser version and the IP transition.

---

## 18. Suggested build order

1. **Kits:** Create-a-Class becomes the kit window (5 kits, no premades), editing and switching
   at any trainer or innkeeper, viewing from the HUD, the hourly field editor.
2. **Stats rework:** the conversion table, and procs from gun hits.
3. **Downed state, corpse runs without durability, ammo and regeneration.**
4. **The class framework:** trainers selling options, the talent page in the kit editor, client spell
   data; then three archetypes end to end (one per role) to prove the feel; then the rest, a class at a
   time.
5. **Companions in the pet slot,** and the line generator.
6. **A more dangerous world:** automatic gunner camps, patrols, scaling.
7. **Arsenal stage 1 (names and flavour),** then reskins.
8. **Weapon pickups.**
9. **Battleground modes,** mounts and brief flight, professions, prestige.
