# World of Modern Warfare: companion catalog

Design data for section 4 of `GAME_DESIGN.md` (Companions): the new companion lines, their sizes, movesets,
traits, breeding and the pet bar. It extends the starter set in
`server/sql/custom/azeroth_warfare/02_companions.sql` / `Companions::StarterSpecies()` (species 1-33) and the
integration guide `server/src/fusion/docs/COMPANIONS.md`. Nothing here is implemented yet; every creature,
display, spell and item id below was checked against the world database of the shipped package
(`mangos`, read-only) and, for sizes, against the 1.12.1 client's own model files.

Contents: 1. lines (64 new, plus a rescale of the 33 existing, plus 1d the 50 vanilla pet lines), 2. movesets, 3. traits, 4. breeding,
5. pet bar.

---

## 0. How the data was chosen

**Present on a 1.12 realm.** Every creature is a `creature_template` row with `patch` <= 10 (the dump's
highest), read at its highest patch. Entries marked `pN` in the tables first appear in patch N (all are
on a 1.12 realm). Drop sources were also checked to have spawns (`creature` rows), except the warlock Imp
(416), which is only a display.

**Real size.** vmangos' `creature_model_info` is not in this dump (the table is
`creature_display_info_addon`, whose bounding radius is unreliable: the riding wolf reads 0.42, the riding
horse 1.45). Vanilla's `CreatureModelData.dbc` carries no bounding box either (its collision height is
the 2.083 default for almost every model). So the sizes come from the client: each display's model was
opened in `game/WoW/Data/model.MPQ` (benilla's `benilla-mpq` + `benilla-m2` readers, run from a scratch
tool outside the repo) and its authored bounding-sphere radius multiplied by the display's
`CreatureDisplayInfo` scale and by `creature_template.display_scale1` when set. That number is **R** in the
tables, in yards. It includes animation reach (wings, tails, rearing bears), so it compares well within a
model family and only roughly across families. Reference points: a cat critter R 0.9, a whelpling 1.6, a
human 2.8, vanilla mounts 3.7 (wolf) to 6.1 (raptor).

**Scale rule** (the `stageN_scale` column; the companion's object scale on top of the display's own):

- Stage 2, "normal mob size": `N / R2`, where N is the median R of every spawned normal-rank creature on
  the same model (e.g. Tiger.mdx 3.52, Raptor.mdx 5.71, Wolf.mdx 3.40), clamped to 0.8-1.25. A stage-2
  companion is as big as a typical wild mob of its kind, whichever variant's display it uses.
- Stage 1, companion-sized: `0.42 x R2eff / R1`, capped so stage 1 never exceeds R 2.2 (4.4 for winged and
  long-bodied models, whose R is inflated), clamped 0.15-1.0. Shrinking a bigger display is preferred over
  a baby display that does not read as the same creature.
- Stage 3: `1.7 x R2eff / R3` (humanoids 1.5x), at least R 4.5 for rideable lines, at most R 10 (9
  humanoid), clamped 0.4-2.5.
- **Rideable final forms are locked at 1.0.** The mount aura shows the display at its own size (the
  object scale does not carry over to `UNIT_FIELD_MOUNTDISPLAYID` on the stock client), so a scaled
  walking form would shrink when mounted. For those lines stage 2 is instead capped at `R3 / 1.25` so
  stage 3 still reads bigger, and the table notes when the final form is smaller than the smallest vanilla
  mount (R 3.7) together with the ideal scale if benilla ever scales mounts.
- All scales rounded to 0.05. Tune by eye in game; the rule gets each line within about 20%.

**Riding seats.** A model can be ridden cleanly only if it authors attachment 0 (the client's seat;
benilla's `entities/mount.rs` falls back to the unit matrix and logs
`MOUNTDISPLAYIDNOMOUNTATTACHMENT` otherwise). Read from the M2 files: Raptor, DireWolf, TallStrider,
Drake, FrostSabre/DruidCat, Horse, Ram, RidingKodo, MechaStrider, Stag, Hippogryph, Wyvern, BogBeast and
Centaur have one; Bear, Tiger, Lion, Wolf, MineSpider, Tarantula, Scorpion, Crocodile, SeaTurtle, Boar,
KodoBeast, Basilisk, FelHound, CryptFiend, GoblinShredder and GnomeSpiderTank do not. Lines on a seatless
model are rideable only with a per-display seat offset (benilla table; the stock client draws the rider
at the model's origin). The tables give a first guess (0.35 x R).

**Riding a non-mount display.** The existing lines store vanilla mount spells whose aura brings the
display. The new rideable lines have no vanilla mount spell, so add a `ride_display` column
(0 = the spell's own): cast the family-neutral 60%/100% spells (e.g. 580 Timber Wolf / 23250 Swift Brown
Wolf, for speed and vanilla's mounted rules), then set `UNIT_FIELD_MOUNTDISPLAYID` to the stage-3
creature's display. Same `CanRide` rules as today (level 40 + riding 75; 60 + 150 for swift).

**Ids.** The item range 90001-90099 is full in practice (species 60-68's items are the mount items
90060-90068), so the new species use **ids 101-164** and **items 91101-91164** (`item_entry` is its own
column, so `FindByItem` needs no change; only the "entry = 90000 + id" convention in the comments). New
`Companions::Family` values continue from 26 (listed in each family heading).

**Drops.** As today (`DropChance`: critter 0.5%, normal 0.2%, rare 2%, elite 1%, rare elite 5%, boss 3%),
from `drop_entries` = the three stage creatures plus the relatives named in the "Drops from" column, so a
spider drop gives the line of the spider you killed. "Rare" in that column means a rare or rare-elite
spawn (the 2-5% rows). Where two lines share a level band and pet family, use entries, not
`drop_family`, so the roll is unambiguous.

**Evolution rules** for the new lines, unless a line says otherwise: stage 1 -> 2 at level 20 anywhere;
stage 2 -> 3 at level 40 with one family catalyst (90101 Draconic Essence, 90102 Primal Beast Essence,
90103 Elemental Core, 90104 Fel Ember; proposed new: 90105 Necrotic Residue for undead, 90106 Arcane
Gearwork for mechanicals, 90107 Tribal Fetish for humanoids, each dropping from its creature type at 25+
like the others), inside the zones the line drops in. Lines that drop above level 40 evolve at 45/55.

---

## 1. Lines

Each cell: `creature_template entry, name (display id, model, R), scale`. Stage 3 of a rideable line is
the mount. Item names follow the starter set's pattern (egg, carrier, crate, jar...).

### 1a. New lines (64)

Sixty are the core set; four are reserve, to ship later or drop: **103** (mixes two spider models), **134**
(only two hydra displays exist, so stages 1 and 2 share one, like the starter Mechanostrider), **139** and
**153**.

#### Spiders (Family 6)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 101 | Webwood Spiderling (91101) | 1986 Webwood Spider (d709, Tarantula, R2.4) **x0.70** | 442 Tarantula (d366, Tarantula, R3.3) **x1.25** | 769 Deathstrike Tarantula (d336, Tarantula, R5.9) **x1.00** | controller | yes (no seat att.: offset ~2.1 yd, tune) | spiders (pet_family 3), Teldrassil to Duskwood (L3-41) |
| 102 | Darkmist Hatchling (91102) | 4377 Darkmist Lurker (d2539, MineSpider, R2.6) **x0.55** | 4378 Darkmist Recluse (d2538, MineSpider, R4.3) **x0.80** | 4380 Darkmist Widow (d2537, MineSpider, R6.1) **x1.00** | controller | yes (no seat att.: offset ~2.1 yd, tune) | Darkmist/Darkfang spiders, Dustwallow (4376-4380, 4411-4415); Widow is rare-elite 5% (L36-40) |
| 103 | Cinderweb Spiderling (91103) | 5856 Glassweb Spider (d4456, MineSpider, R5.2) **x0.40** | 5857 Searing Lava Spider (d4457, Tarantula, R5.9) **x0.85** | 5858 Greater Lava Spider (d7510, Tarantula, R6.8) **x1.00** | controller | yes (no seat att.: offset ~2.4 yd, tune) | Glassweb/lava spiders, Searing Gorge (5856-5858) (L43-49) |

#### Bears (Family 5)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 104 | Black Bear Cub (91104) | 1128 Young Black Bear (d8843, Bear, R2.5) **x0.65** | 1186 Elder Black Bear (d707, Bear, R4.1) **x0.90** | 1189 Black Bear Patriarch (d762, Bear, R4.7) **x1.00** | bruiser | yes (no seat att.: offset ~1.6 yd, tune) | bears (pet_family 4), Dun Morogh to Loch Modan, Hillsbrad (L5-17) |
| 105 | Ironfur Cub (91105) | 1196 Ice Claw Bear (d8840, Bear, R3.5) **x0.60** | 5268 Ironfur Bear (d3201, Bear, R5.3) **x0.90** | 5272 Grizzled Ironfur Bear (d8838, Bear, R6.1) **x1.00** | bruiser | yes (no seat att.: offset ~2.1 yd, tune) | Ironfur/Ice Claw/Shardtooth bears, Feralas, Winterspring (L7-45) |

#### Cats (Family 4)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 106 | Stranglethorn Kitten (91106) | 681 Young Stranglethorn Tiger (d598, Tiger, R3.0) **x0.45** | 682 Stranglethorn Tiger (d320, Tiger, R3.5) **x0.90** | 1085 Elder Stranglethorn Tiger (d698, Tiger, R4.0) **x1.00** | skirmisher | yes (no seat att.: offset ~1.4 yd, tune) | tigers and panthers (pet_family 2), Stranglethorn (L30-35) |
| 107 | Mountain Lion Cub (91107) | 2384 Starving Mountain Lion (d1059, Tiger, R3.0) **x0.50** | 2406 Mountain Lion (d1058, Tiger, R4.0, p5) **x0.85** | 2407 Hulking Mountain Lion (d2300, Tiger, R4.6) **x1.00** | skirmisher | yes (no seat att.: offset ~1.6 yd, tune) | mountain lions and cougars, Hillsbrad, Alterac, Arathi, Thousand Needles (L23-34) |
| 108 | Savannah Cub (91108) | 3425 Savannah Prowler (d1973, Lion, R3.7) **x0.40** | 3241 Savannah Patriarch (d1977, Lion, R4.8) **x0.70** | 5828 Humar the Pridelord (d4424, Lion, R4.2) **x1.00** | skirmisher | yes (no seat att.: offset ~1.5 yd, tune) | Savannah lions, the Barrens (Humar is rare 5%) (L14-23) |

#### Wolves and worgs (Family 3)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 109 | Bloodsnout Pup (91109) | 1765 Worg (d11421, DireWolf, R2.8) **x0.55** | 1923 Bloodsnout Worg (d741, DireWolf, R3.7) **x1.00** | 9697 Giant Ember Worg (d9370, DireWolf, R4.9) **x1.00** | skirmisher | yes (seat: DireWolf att.0 z=1.40) | worgs (DireWolf model): Silverpine, Redridge, Burning Steppes (L10-56) |
| 110 | Silvermane Pup (91110) | 2923 Mangy Silvermane (d11413, DireWolf, R3.2) **x0.50** | 2924 Silvermane Wolf (d11419, DireWolf, R3.7, p5) **x1.00** | 2926 Silvermane Stalker (d11418, DireWolf, R4.9) **x1.00** | skirmisher | yes (seat: DireWolf att.0 z=1.40) | Silvermane wolves, the Hinterlands (L41-48) |
| 111 | Coyote Pup (91111) | 834 Coyote (d643, Wolf, R2.9) **x0.50** | 833 Coyote Packleader (d161, Wolf, R3.4) **x1.00** | 2729 Elder Crag Coyote (d1164, Wolf, R4.4) **x1.00** | skirmisher | yes (no seat att.: offset ~1.5 yd, tune) | coyotes, Westfall, Badlands (L10-40) |

#### Raptors (Family 7)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 112 | Sunscale Hatchling (91112) | 3254 Sunscale Lashtail (d1744, Raptor, R4.0) **x0.45** | 3255 Sunscale Screecher (d1747, Raptor, R4.9) **x0.90** | 3256 Sunscale Scytheclaw (d4442, Raptor, R5.7) **x1.00** | skirmisher | yes (seat: Raptor att.0 z=2.52) | Sunscale raptors, the Barrens (L11-18) |
| 113 | Deviate Hatchling (91113) | 3634 Deviate Stalker (d1746, Raptor, R4.0) **x0.45** | 3633 Deviate Slayer (d949, Raptor, R4.9) **x0.90** | 3637 Deviate Guardian (d755, Raptor, R5.7) **x1.00** | skirmisher | yes (seat: Raptor att.0 z=2.52) | Deviate raptors, Wailing Caverns (dungeon 1%) (L15-19) |
| 114 | Bloodfen Hatchling (91114) | 4351 Bloodfen Raptor (d2571, Raptor, R5.7) **x0.40** | 4355 Bloodfen Scytheclaw (d2574, Raptor, R7.4) **x0.80** | 4357 Bloodfen Lashtail (d2573, Raptor, R17.5) **x1.00** | skirmisher | yes (seat: Raptor att.0 z=2.52) | Bloodfen raptors, Dustwallow (L35-41) |

#### Scorpids (Family 13)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 115 | Venomtail Scorpling (91115) | 3125 Clattering Scorpid (d2486, Scorpion, R2.6) **x0.45** | 3127 Venomtail Scorpid (d2732, Scorpion, R3.0) **x0.90** | 4139 Scorpid Terror (d2491, Scorpion, R3.5, p5) **x1.00** | controller | yes (no seat att.: offset ~1.2 yd, tune) small: ideal x1.35 | scorpids (pet_family 20), Durotar, Barrens, Badlands (L5-34) |
| 116 | Stonelash Scorpling (91116) | 7078 Cleft Scorpid (d5985, Scorpion, R1.3, p5) **x1.00** | 11735 Stonelash Scorpid (d15383, Scorpion, R4.8, p6) **x0.80** | 11737 Stonelash Flayer (d15385, Scorpion, R5.3, p6) **x1.00** | controller | yes (no seat att.: offset ~1.8 yd, tune) | Cleft/Stonelash scorpids, Tanaris, Silithus (L35-58) |

#### Crocolisks (Family 8)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 117 | Drywallow Hatchling (91117) | 4341 Drywallow Crocolisk (d1080, Crocodile, R5.3) **x0.40** | 4343 Drywallow Snapper (d814, Crocodile, R6.5, p1) **x0.80** | 4345 Drywallow Daggermaw (d807, Crocodile, R7.6, p10) **x1.00** | bruiser | yes (no seat att.: offset ~1.7 yd, tune) | crocolisks (pet_family 6), Dustwallow, Stranglethorn (L35-41) |

#### Turtles (new Family 30)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 118 | Snapjaw Hatchling (91118) | 3461 Oasis Snapjaw (d6368, SeaTurtle, R1.1) **x1.00** | 2408 Snapjaw (d1244, SeaTurtle, R3.9) **x1.20** | 2505 Saltwater Snapjaw (d5027, SeaTurtle, R5.8) **x1.00** | bruiser | yes (no seat att.: offset ~2.0 yd, tune) | turtles (pet_family 21), Barrens oases, Hillsbrad, Tanaris coast (L15-50) |
| 119 | Surf Glider Hatchling (91119) | 4142 Sparkleshell Tortoise (d5052, SeaTurtle, R3.9) **x0.55** | 5431 Surf Glider (d7114, SeaTurtle, R5.0) **x1.00** | 5432 Giant Surf Glider (d5127, SeaTurtle, R6.8) **x1.00** | bruiser | yes (no seat att.: offset ~2.4 yd, tune) | Sparkleshell/Surf Glider turtles, Thousand Needles, Tanaris (L30-50) |

#### Gorillas (Family 9)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 120 | Groddoc Infant (91120) | 5260 Groddoc Ape (d3186, Gorilla, R4.0) **x0.50** | 5262 Groddoc Thunderer (d3188, Gorilla, R5.2, p7) **x0.90** | 6513 Un'Goro Stomper (d5294, Gorilla, R5.2, p7) **x1.55** | bruiser | no | gorillas (pet_family 9), Feralas, Un'Goro (L42-52) |

#### Boars (new Family 28)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 121 | Goretusk Piglet (91121) | 454 Young Goretusk (d8871, Boar, R2.5, p7) **x0.45** | 157 Goretusk (d3027, Boar, R3.4, p7) **x0.80** | 547 Great Goretusk (d3035, Boar, R3.9, p7) **x1.00** | bruiser | yes (no seat att.: offset ~1.4 yd, tune) | boars (pet_family 5), Westfall, Redridge, Razorfen (L12-17) |

#### Crabs (new Family 29)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 122 | Pygmy Crawler (91122) | 3106 Pygmy Surf Crawler (d1307, Crab, R1.8) **x0.60** | 2235 Reef Crawler (d981, Crab, R2.5) **x1.00** | 1088 Monstrous Crawler (d699, Crab, R3.8) **x1.15** | bruiser | no | crabs (pet_family 8), every coast (L5-44) |

#### Wind serpents (new Family 31)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 123 | Cloud Serpent Hatchling (91123) | 3247 Thunderhawk Hatchling (d1742, WindSerpent, R4.3, p6) **x0.65** | 4117 Cloud Serpent (d2705, WindSerpent, R6.6, p6) **x1.00** | 4119 Elder Cloud Serpent (d2703, WindSerpent, R10.3, p6) **x0.95** | caster | no | wind serpents (pet_family 27), Thousand Needles, Stonetalon, Feralas (L18-29) |

#### Carrion birds (new Family 32)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 124 | Swoop Fledgling (91124) | 2969 Wiry Swoop (d1228, CarrionBird, R5.3) **x0.65** | 2970 Swoop (d1229, CarrionBird, R6.4) **x1.25** | 2971 Taloned Swoop (d10824, CarrionBird, R7.5) **x1.35** | controller | no | carrion birds (pet_family 7), Mulgore, Barrens, Badlands (L5-10) |
| 125 | Roc Chick (91125) | 5428 Roc (d3248, CarrionBird, R7.5) **x0.50** | 5429 Fire Roc (d7348, CarrionBird, R8.6) **x1.00** | 5430 Searing Roc (d10827, CarrionBird, R11.3) **x0.90** | caster | no | rocs, Tanaris, Searing Gorge (L41-49) |

#### Hyenas (new Family 27)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 126 | Hecklefang Pup (91126) | 4127 Hecklefang Hyena (d2710, Hyena, R2.5) **x0.55** | 4129 Hecklefang Snarler (d2711, Hyena, R2.9) **x1.15** | 4128 Hecklefang Stalker (d2712, Hyena, R3.4) **x1.70** | skirmisher | no | hyenas (pet_family 25), Barrens, Thousand Needles, Desolace, Tanaris (L15-23) |

#### Owls (Family 10)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 127 | Strigid Owlet (91127) | 1995 Strigid Owl (d10832, Owl, R6.3) **x0.50** | 1996 Strigid Screecher (d4877, Owl, R7.5) **x1.00** | 1997 Strigid Hunter (d10830, Owl, R8.6) **x1.15** | controller | no | owls (pet_family 26), Teldrassil, Felwood, Winterspring (L5-9) |

#### Bats (Family 11)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 128 | Kraul Batling (91128) | 1554 Vampiric Duskbat (d8808, FelBat, R1.1) **x1.00** | 4538 Kraul Bat (d1955, FelBat, R3.9) **x1.00** | 4539 Greater Kraul Bat (d1954, FelBat, R5.1) **x1.30** | controller | no | bats (pet_family 24), Tirisfal, Razorfen Kraul, Uldaman (L8-32) |

#### Tallstriders (Family 12)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 129 | Foreststrider Fledgling (91129) | 2321 Foreststrider Fledgling (d1042, TallStrider, R3.6) **x0.50** | 2322 Foreststrider (d178, TallStrider, R4.2) **x1.00** | 2323 Giant Foreststrider (d1283, TallStrider, R5.4) **x1.00** | support | yes (seat: TallStrider att.0 z=2.41) | tallstriders (pet_family 12), Darkshore, Mulgore, Barrens (L11-19) |

#### Kodos (Family 22)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 130 | Aged Kodo Calf (91130) | 3234 Lost Barrens Kodo (d10918, KodoBeast, R9.6) **x0.45** | 4700 Aged Kodo (d1308, KodoBeast, R12.5) **x0.85** | 4702 Ancient Kodo (d2767/2768, KodoBeast, R14.6) **x1.00** | support | yes (no seat att.: offset ~3.2 yd, tune) | kodos (Barrens, Desolace Kodo Graveyard) (L14-37) |

#### Basilisks (new Family 33)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 131 | Gritjaw Hatchling (91131) | 4044 Blackened Basilisk (d2173, Basilisk, R4.1) **x0.60** | 4728 Gritjaw Basilisk (d1074, Basilisk, R6.3) **x0.90** | 4729 Hulking Gritjaw Basilisk (d7345, Basilisk, R7.4) **x1.00** | controller | yes (no seat att.: offset ~1.6 yd, tune) | basilisks (Badlands, STV, Stonetalon, Blasted Lands, Tanaris) (L23-36) |
| 132 | Glasshide Hatchling (91132) | 4147 Saltstone Basilisk (d12340, Basilisk, R5.2) **x0.60** | 5419 Glasshide Basilisk (d141, Basilisk, R7.4) **x1.00** | 5421 Glasshide Petrifier (d12339, Basilisk, R9.6) **x1.00** | controller | yes (no seat att.: offset ~2.1 yd, tune) | Saltstone/Glasshide basilisks, Thousand Needles, Tanaris (L30-49) |

#### Threshadons (new Family 34)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 133 | Thresher Fry (91133) | 1224 Young Threshadon (d867, Threshadon, R6.3) **x0.55** | 2187 Elder Darkshore Thresher (d994, Threshadon, R7.3) **x1.15** | 4390 Elder Murk Thresher (d6431, Threshadon, R9.5) **x1.05** | bruiser | no | threshadons (Darkshore, Wetlands, Dustwallow); water spawns (L16-45) |

#### Hydras (new Family 35)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 134 | Mystlash Hydraling (91134) | 3721 Mystlash Hydra (d10898, Hydra, R8.1) **x0.40** | 3721 Mystlash Hydra (d10898, Hydra, R8.1) **x1.00** | 4374 Strashaz Hydra (d2423, Hydra, R12.2) **x0.80** | caster | no | Mystlash Hydra (Ashenvale, 3721), Strashaz Hydra (Azshara, 4374, elite 1%) (L19-61) |

#### Wild elementals (Families 14-17, 36)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 135 | Timberling Sprout (91135) | 2022 Timberling (d3033, BogBeast, R3.4) **x0.65** | 1955 Lake Creeper (d2567, BogBeast, R5.6) **x1.15** | 764 Swampwalker (d2024, BogBeast, R6.3) **x1.60** | controller | no | bog beasts and timberlings (BogBeast model, type 4) (L5-39) |
| 136 | Toxic Mote (91136) | 5897 Corrupt Water Spirit (d5492/5493, PoisonElemental, R2.4) **x0.80** | 6220 Irradiated Horror (d4907, PoisonElemental, R4.0) **x1.15** | 8519 Blighted Surge (d5497, PoisonElemental, R5.2) **x1.50** | caster | no | poison elementals (Barrens, Gnomeregan, Plaguelands) (L19-55) |
| 137 | Cracked Golemling (91137) | 2156 Cracked Golem (d10799, GolemStone, R4.0) **x0.55** | 2723 Stone Golem (d10804, GolemStone, R6.1) **x1.00** | 5854 Heavy War Golem (d10801, GolemStone, R7.0) **x1.40** | bruiser | no | stone golems (type 4, GolemStone model), Alterac, Badlands, Burning Steppes (L18-49) |

#### Oozes (Family 20)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 138 | Ectoplasm Blob (91138) | 3638 Devouring Ectoplasm (d4266, Slime, R2.6) **x0.75** | 1030 Black Slime (d360, Slime, R3.8, p7) **x1.25** | 1033 Monstrous Ooze (d11139, Slime, R7.6, p7) **x1.05** | controller | no | oozes (type 10 Slime model), Wetlands, Wailing Caverns (L16-26) |
| 139 | Creeping Sludgeling (91139) | 12222 Creeping Sludge (d14350, SlimeLesser, R1.9, p4) **x1.00** | 2655 Green Sludge (d1145, Slime, R4.3, p7) **x1.25** | 2656 Jade Ooze (d11137, Slime, R7.6, p7) **x1.20** | controller | no | green oozes, Maraudon, Feralas, Un'Goro (L45-48) |

#### Dragonkin (Family 1)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 140 | Searing Hatchling (91140) | 4323 Searing Hatchling (d715, DragonWhelp, R4.0) **x0.45** | 7048 Scalding Broodling (d400, DragonWhelp, R5.4) **x0.80** | 7045 Scalding Drake (d9585, Drake, R17.8) **x1.00** | caster | yes (seat: Drake att.0 z=2.53) | Searing Gorge whelps, Burning Steppes broodlings, Blackrock Spire drakes (dragonkin) (L41-55) |

#### Demons (Families 19, 37-39)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 141 | Imp Familiar (91141) | 416 Imp (d4449, Imp, R1.9) **x1.00** | 9776 Flamekin Spitter (d10815, Imp, R4.9) **x1.00** | 9778 Flamekin Torcher (d10817, Imp, R5.7) **x1.45** | caster | no | imps and Flamekin, Burning Steppes, Felwood; also warlock-pet trainers sell none (L5-56) |
| 142 | Felstalker Pup (91142) | 3102 Felstalker (d850, FelHound, R2.0) **x1.00** | 6071 Legion Hound (d7949, FelHound, R4.8) **x1.20** | 4685 Ley Hunter (d6173, FelHound, R7.3) **x1.00** | controller | yes (no seat att.: offset ~2.5 yd, tune) | felhounds (pet_family 15 / FelHound model), Durotar, Blasted Lands, Dire Maul (L3-40) |
| 143 | Lesser Felguard (91143) | 3772 Lesser Felguard (d5049, FelGuard, R3.4) **x0.50** | 6011 Felguard Sentry (d9017, FelGuard, R4.5) **x0.90** | 12396 Doomguard Commander (d4426, DoomGuard, R8.2) **x0.85** | bruiser | no | felguards and doomguards (Ashenvale, Felwood, Blasted Lands, Azshara) (L23-61) |
| 144 | Darkhound Whelp (91144) | 1547 Decrepit Darkhound (d9021, DarkHound, R1.9) **x0.55** | 1548 Cursed Darkhound (d9020, DarkHound, R2.3) **x1.10** | 8598 Frenzied Plaguehound (d9022, DarkHound, R3.3) **x1.30** | skirmisher | no | darkhounds and plaguehounds (Tirisfal, Silverpine, Eastern Plaguelands) (L5-58) |

#### Undead (new Families 40-42)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 145 | Rattlecage Bones (91145) | 1890 Rattlecage Skeleton (d11400, SkeletonNaked, R2.8) **x0.45** | 1520 Rattlecage Soldier (d200, Skeleton, R2.4) **x1.25** | 1788 Skeletal Warlord (d775, Skeleton, R4.2) **x1.20** | bruiser | no | skeletons (type 6, Skeleton models), Tirisfal, Duskwood, Plaguelands (L2-57) |
| 146 | Ghoul Thrall (91146) | 1866 Ravenclaw Slave (d829, Ghoul, R2.5) **x0.45** | 1791 Slavering Ghoul (d570, Ghoul, R3.3) **x0.85** | 8543 Stitched Horror (d1693, FleshGolem, R7.8) **x0.60** | bruiser | no | ghouls and abominations (type 6), Silverpine to Eastern Plaguelands (L11-58) |
| 147 | Wailing Shade (91147) | 1534 Wailing Ancestor (d10752, Banshee, R2.0) **x0.55** | 6116 Highborne Apparition (d10702, Banshee, R2.6) **x1.00** | 8541 Hate Shrieker (d10751, Banshee, R3.4) **x1.30** | caster | no | banshees and highborne spirits (type 6), Tirisfal, Azshara, Plaguelands (L9-57) |
| 148 | Crypt Scuttler (91148) | 8556 Crypt Walker (d11178, CryptFiend, R4.9) **x0.40** | 8555 Crypt Fiend (d3004, CryptFiend, R5.8) **x0.80** | 8558 Crypt Slayer (d9758, CryptFiend, R5.8) **x1.00** | controller | yes (no seat att.: offset ~2.0 yd, tune) | crypt fiends (Eastern Plaguelands, Stratholme) (L53-59) |

#### Mechanicals (Family 21)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 149 | Servo (91149) | 2922 Servo (d1150, GolemHarvest, R1.8) **x0.90** | 36 Harvest Golem (d367, GolemHarvest, R3.8) **x1.00** | 115 Harvest Reaper (d379, GolemHarvest, R5.1) **x1.25** | bruiser | no | harvest golems (type 9), Westfall; Servo is Gnomeregan (L5-18) |
| 150 | Compact Reaper (91150) | 2676 Compact Harvest Reaper (d1159, GolemHarvestStage2, R2.2) **x1.00** | 11684 Warsong Shredder (d1303, GoblinShredder, R5.5) **x1.05** | 4260 Venture Co. Shredder (d1269, GoblinShredder, R7.3) **x1.00** | bruiser | yes (no seat att.: offset ~2.6 yd, tune) | shredders and Venture Co. machines (type 9), Stonetalon, Ashenvale, Stranglethorn (L27-41) |
| 151 | Mechano-Tankling (91151) | 6225 Mechano-Tank (d5926, GnomeSpiderTank, R3.7) **x0.35** | 6226 Mechano-Flamewalker (d6890, GnomeSpiderTank, R4.1) **x0.80** | 6227 Mechano-Frostwalker (d6891, GnomeSpiderTank, R4.1) **x1.00** | caster | yes (no seat att.: offset ~1.4 yd, tune) | Gnomeregan mechano-tanks (elite 1%) (L29-32) |

#### Humanoids (new Families 43-55)

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 152 | Riverpaw Runt (91152) | 97 Riverpaw Runt (d10791, gnoll, R2.0, p1) **x0.55** | 124 Riverpaw Brute (d384, GnollMelee, R2.7, p1) **x1.00** | 445 Redridge Alpha (d609, GnollMelee, R3.0, p1) **x1.35** | humanoid | no | gnolls (Westfall, Redridge, Duskwood, Hillsbrad, Feralas) (L8-22) |
| 153 | Kobold Vermin (91153) | 6 Kobold Vermin (d10913, Kobold, R0.7, p1) **x0.75** | 475 Kobold Tunneler (d139, Kobold, R1.0, p1) **x1.25** | 2574 Drywhisker Digger (d511, Kobold, R1.5, p1) **x1.25** | humanoid | no | kobolds (Elwynn, Westfall, Arathi, Thousand Needles) (L1-37) |
| 154 | Rockjaw Trogg (91154) | 707 Rockjaw Trogg (d606, Troglodyte, R1.7, p1) **x0.75** | 1163 Stonesplinter Skullthumper (d160, Troglodyte, R3.1, p1) **x1.00** | 4855 Stonevault Brawler (d11166, Troglodyte, R5.5) **x0.85** | humanoid | no | troggs (Dun Morogh, Loch Modan, Badlands, Uldaman) (L1-44) |
| 155 | Ogre Runt (91155) | 1178 Mo'grosh Ogre (d1122, Ogre, R4.3, p1) **x0.50** | 2566 Boulderfist Brute (d3193, Ogre, R5.6, p1) **x1.00** | 5241 Gordunni Warlord (d11561, OgreWarlord, R9.7, p1) **x0.85** | humanoid | no | ogres (Duskwood, Arathi, Alterac, Feralas, Burning Steppes) (L18-47) |
| 156 | Kolkar Drudge (91156) | 3119 Kolkar Drudge (d9442, Centaur, R3.1, p1) **x0.55** | 4093 Galak Wrangler (d9414, Centaur, R4.3, p1) **x1.00** | 5601 Khan Jehn (d9433, CentaurKhan, R5.6, p1) **x1.15** | humanoid | no | centaurs (Barrens, Thousand Needles, Desolace; the Khan is rare 5%) (L6-37) |
| 157 | Bristleback Piglet (91157) | 2952 Bristleback Quilboar (d1967, QuillBoar, R2.1, p1) **x0.55** | 3266 Razormane Defender (d1253, QuillBoar, R3.3, p7) **x0.80** | 4436 Razorfen Quilguard (d6108, QuillBoarWarrior, R3.9, p1) **x1.00** | humanoid | no | quilboar (Mulgore, Barrens, Razorfen Kraul and Downs) (L3-26) |
| 158 | Gnarlpine Cub (91158) | 2006 Gnarlpine Ursa (d6798, Furbolg, R2.9, p1) **x0.60** | 3921 Thistlefur Ursa (d1999, Furbolg, R4.2, p7) **x1.00** | 7440 Winterfall Den Watcher (d6828, Furbolg, R7.9, p1) **x0.80** | humanoid | no | furbolgs (Teldrassil, Darkshore, Ashenvale, Felwood, Winterspring) (L5-56) |
| 159 | Windfury Fledgling (91159) | 2962 Windfury Harpy (d10877, Harpy, R4.1, p1) **x0.55** | 2964 Windfury Sorceress (d1255, Harpy, R4.8, p1) **x1.15** | 2965 Windfury Matriarch (d10869, Harpy, R5.5, p1) **x1.50** | humanoid | no | harpies (Mulgore, Barrens, Stonetalon, Desolace, Feralas) (L7-11) |
| 160 | Troll Whelp (91160) | 706 Frostmane Troll Whelp (d5606, TrollMale, R3.1, p1) **x0.55** | 597 Bloodscalp Berserker (d4579, TrollMale, R4.2, p1) **x1.00** | 9268 Smolderthorn Berserker (d7803, TrollDire, R5.7) **x1.10** | humanoid | no | jungle and dire trolls (Dun Morogh, Stranglethorn, Hinterlands, Blackrock Spire) (L3-58) |
| 161 | Stormscale Wave Rider (91161) | 2179 Stormscale Wave Rider (d4036, NagaMale, R4.1) **x0.55** | 3711 Wrathtail Myrmidon (d6747, NagaMale, R5.3) **x1.00** | 4366 Strashaz Serpent Guard (d9134, NagaMale, R7.2) **x1.10** | humanoid | no | naga (Darkshore, Ashenvale coast, Azshara) (L15-61) |
| 162 | Makrura Clacker (91162) | 3103 Makrura Clacker (d1940, Lobstrok, R3.1) **x0.70** | 4401 Muckshell Clacker (d2426, Lobstrok, R5.6) **x1.00** | 6350 Makrinni Razorclaw (d1247, Lobstrok, R8.4) **x1.00** | humanoid | no | makrura (Durotar coast, Dustwallow, Azshara) (L6-55) |
| 163 | Young Moonkin (91163) | 10159 Young Moonkin (d9390, ForceofNature, R4.0, p1) **x0.50** | 10158 Moonkin (d5927, ForceofNature, R4.8, p1) **x1.00** | 7452 Crazed Owlbeast (d12237, ForceofNature, R4.8, p1) **x1.50** | humanoid | no | moonkin and owlbeasts (Darkshore, Hinterlands, Winterspring) (L11-57) |
| 164 | Moonrage Whelp (91164) | 1769 Moonrage Whitescalp (d11180, Worgen, R2.9, p1) **x0.60** | 1782 Moonrage Darksoul (d574, Worgen, R4.4, p1) **x0.90** | 920 Nightbane Tainted One (d1098, Worgen, R4.9, p1) **x1.20** | humanoid | no | worgen (Silverpine, Duskwood, Shadowfang Keep) (L10-31) |

Notes on individual lines:

- **101-103**: Tarantula/MineSpider have no seat. 102's Darkmist Widow (4380) is a rare spawn (1 spawn),
  the line also drops from the common Darkmist/Darkfang spiders.
- **108**: Humar the Pridelord (5828) is a rare; its display (4424) is the white lion the line grows into.
- **109/110**: worg lines ride on the DireWolf seat (the same model as the riding wolf), the cleanest new
  mounts. 109 replaces the Bloodaxe Worg Pup (10221), whose display 9563 the starter Wolf Pup already uses.
- **112-114**: Raptor.mdx has the riding raptor's seat (2.52). 114's Lashtail (4357) is a very large
  display (R 17.5): the "dinosaur" mount, keep at 1.0.
- **118**: Oasis Snapjaw (3461) is a naturally tiny turtle; stage 1 at 1.0.
- **130**: wild kodos are much larger than the riding kodo. Ride it as the Ancient Kodo (seatless model)
  or switch stage 3 to 11689 Riding Kodo (seat 3.54) like the starter Kodo Calf.
- **133** Threshadons are water creatures; check that the model has a ground walk before shipping
  (otherwise keep it in water zones, like the Water Mote's place rules).
- **140**: the only new flying-shaped mount. Vanilla has no flight outside taxis, so it is a ground mount
  on the Drake seat.
- **141**: stage 1 is the warlock Imp display (416), never spawned; drops come from Flamekin (9776-9778)
  and Burning Steppes / Felwood imps.
- **146**: ghoul to abomination (FleshGolem model), the clearest "grows up" story in the undead set.
- **150**: the Compact Harvest Reaper (2676) grows into goblin shredders; riding it puts the owner in the
  pilot seat (offset needed).
- **152-164 (humanoids)** are never rideable. Their stage 3 is a big unit (1.5x), several of them rare
  leaders (5601 Khan Jehn, 5241 Gordunni Warlord). The Gunner trait (section 3) only rolls on these lines.

### 1b. Rescale of the starter set (species 1-33)

The same rule applied to today's rows (rideable finals stay at 1.0, today's value). The biggest
changes from `02_companions.sql`: stage 1 of the four mount-only lines (30-33) goes from 0.5-0.6 to 0.4;
the adult forms of 9, 10, 14, 17, 18 and 24 grow to 1.5-2.05 (today 0.8-1); 1 and 2's drakes and 25's slime
shrink to 0.4; 21's and 24's stage 1 shrink to 0.35 / 0.3. Kodo Calf (29): the wild kodo models are far
bigger than the riding kodo, so keeping stage 2 below the mount needs 0.2 / 0.35; either accept that or
leave 29 as it is. Lines 26-28 (critters) were single-stage here; section 1e gives every line three forms, and 29's look table holds the final sizes.

| # | Species (item) | Stage 1 (scale) | Stage 2 (scale) | Stage 3 (scale) | Role | Ride | Drops from |
|---|---|---|---|---|---|---|---|
| 1 | Black Whelpling (90001) | 7543 Dark Whelpling (d6288, DragonWhelp, R1.6, p10) **x0.90** | 441 Black Dragon Whelp (d387, DragonWhelp, R2.6) **x1.25** | 7044 Black Drake (d6374, Drake, R15.5) **x0.40** | caster | no | as today (creatures L1-52) |
| 2 | Crimson Whelpling (90002) | 7544 Crimson Whelpling (d6290, DragonWhelp, R1.6) **x1.00** | 1069 Crimson Whelp (d505, DragonWhelp, R3.6) **x1.15** | 13976 Tortured Drake (d14012/6373, Drake, R20.1, p4) **x0.40** | caster | no | as today (creatures L1-40) |
| 3 | Emerald Whelpling (90003) | 7545 Emerald Whelpling (d6291, DragonWhelp, R1.6) **x1.00** | 741 Dreaming Whelp (d621, DragonWhelp, R4.7) **x0.85** | 742 Green Wyrmkin (d7863, DragonSpawnCaster, R4.9, p10) **x1.35** | caster | no | as today (creatures L1-42) |
| 4 | Azure Whelpling (90004) | 7547 Azure Whelpling (d6293, DragonWhelp, R1.6) **x1.00** | 10659 Cobalt Whelp (d10585, DragonWhelp, R4.0) **x1.00** | 10660 Cobalt Broodling (d10584, DragonWhelp, R5.4) **x1.25** | caster | no | as today (creatures L1-56) |
| 5 | Sprite Darter Hatchling (90005) | 9662 Sprite Darter Hatchling (d6294, FaerieDragon, R1.1) **x1.00** | 5278 Sprite Darter (d2158, FaerieDragon, R4.2) **x0.85** | - | support | no | as today (creatures L1-45) |
| 6 | Wolf Pup (90006) | 10259 Worg Pup (d9563/9572, DireWolf, R1.3) **x0.95** | 565 Rabid Dire Wolf (d802, Wolf, R3.4) **x0.85** | 358 Riding Wolf (Brown) (d247, RidingDireWolf, R3.7) **x1.00** | skirmisher | yes (seat: RidingDireWolf att.0 z=1.40) small: ideal x1.35 | as today (creatures L1-21) |
| 7 | Nightsaber Cub (90007) | 2031 Young Nightsaber (d11454, Tiger, R2.5) **x0.60** | 2042 Nightsaber (d6805, Tiger, R3.0) **x1.20** | 7690 Riding Tiger (BlackStriped) (d6448, RidingFrostSabre, R5.6) **x1.00** | skirmisher | yes (seat: RidingFrostSabre att.0 z=1.24) | as today (creatures L1-6) |
| 8 | Frostsaber Cub (90008) | 7430 Frostsaber Cub (d9958, Tiger, R1.8) **x0.85** | 7431 Frostsaber (d9953, Tiger, R4.6) **x0.80** | 6074 Riding Tiger (White Striped) (d6080, RidingFrostSabre, R5.6) **x1.00** | skirmisher | yes (seat: RidingFrostSabre att.0 z=1.24) | as today (creatures L1-57) |
| 9 | Panther Cub (90009) | 683 Young Panther (d2437, Tiger, R3.0) **x0.50** | 736 Panther (d599, Tiger, R3.5) **x1.00** | 684 Shadowmaw Panther (d633, Tiger, R4.0, p5) **x1.50** | skirmisher | no | as today (creatures L30-38) |
| 10 | Bear Cub (90010) | 6789 Thistle Cub (d5510, Bear, R2.5) **x0.80** | 3809 Ashenvale Bear (d820, Bear, R4.7) **x1.00** | 3810 Elder Ashenvale Bear (d982, Bear, R4.7, p1) **x1.70** | bruiser | no | as today (creatures L9-26) |
| 11 | Young Web Spider (90011) | 1504 Young Night Web Spider (d513, MineSpider, R2.5) **x0.60** | 217 Venom Web Spider (d955, MineSpider, R4.3) **x0.80** | 2001 Giant Webwood Spider (d6808, Tarantula, R6.8) **x0.85** | controller | no | as today (creatures L2-20) |
| 12 | Raptor Hatchling (90012) | 3122 Bloodtalon Taillasher (d1960, Raptor, R3.1) **x0.65** | 3123 Bloodtalon Scythemaw (d1959, Raptor, R4.0) **x1.20** | 7708 Riding Raptor (Violet) (d6473, RidingRaptor, R6.1) **x1.00** | skirmisher | yes (seat: RidingRaptor att.0 z=2.52) | as today (creatures L1-10) |
| 13 | Young Crocolisk (90013) | 1417 Young Wetlands Crocolisk (d1035, Crocodile, R1.4) **x1.00** | 1151 Saltwater Crocolisk (d2548, Crocodile, R6.5) **x0.80** | - | bruiser | no | as today (creatures L21-36) |
| 14 | Infant Gorilla (90014) | 1108 Mistvale Gorilla (d843, Gorilla, R2.6, p7) **x0.75** | 1557 Elder Mistvale Gorilla (d838, Gorilla, R5.4, p7) **x0.85** | 1558 Silverback Patriarch (d844, Gorilla, R4.6) **x1.65** | bruiser | no | as today (creatures L32-43) |
| 15 | Owlet (90015) | 7555 Hawk Owl (d6299, Owl, R1.9) **x1.00** | 7456 Winterspring Screecher (d10833, Owl, R11.2) **x0.80** | - | controller | no | as today (creatures L1-59) |
| 16 | Duskbat Pup (90016) | 1512 Duskbat (d4732, FelBat, R0.3) **x1.00** | 8600 Plaguebat (d7894, FelBat, R3.4) **x1.15** | - | controller | no | as today (creatures L1-55) |
| 17 | Plainstrider Chick (90017) | 2955 Plainstrider (d1219, TallStrider, R2.9) **x0.60** | 2956 Adult Plainstrider (d1220, TallStrider, R3.6) **x1.20** | 2957 Elder Plainstrider (d1221, TallStrider, R3.6) **x2.05** | support | no | as today (creatures L1-9) |
| 18 | Scorpid Hatchling (90018) | 3124 Scorpid Worker (d2485, Scorpion, R2.1) **x0.60** | 4140 Scorpid Reaver (d3247, Scorpion, R3.0) **x1.00** | 5422 Scorpid Hunter (d2414, Scorpion, R3.0) **x1.70** | controller | no | as today (creatures L3-41) |
| 19 | Ember Spark (90019) | 17003 Cinder Elemental (d8409, FireElemental, R0.9, p9) **x1.00** | 6296 Flame Elemental (d1070, FireElemental, R3.8) **x0.80** | 575 Fire Elemental (d1405, FireElemental, R2.3) **x2.25** | caster | no | as today (creatures L1-36) |
| 20 | Water Mote (90020) | 6748 Water Spirit (d525/5561, WaterElemental, R3.2) **x0.50** | 691 Lesser Water Elemental (d5561, WaterElemental, R3.8) **x1.00** | 510 Water Elemental (d525, WaterElemental, R3.2) **x2.00** | caster | no | as today (creatures L19-60) |
| 21 | Pebble (90021) | 2735 Lesser Rock Elemental (d1161, ElementalEarth, R6.0) **x0.35** | 92 Rock Elemental (d171, ElementalEarth, R7.0) **x1.00** | 2736 Greater Rock Elemental (d14328, ElementalEarth, R8.1, p1) **x1.25** | bruiser | no | as today (creatures L37-44) |
| 22 | Zephyr (90022) | 5898 Air Spirit (d8714, AirElemental, R2.8) **x0.50** | 11576 Whirlwind Ripper (d8714, AirElemental, R2.8, p7) **x1.25** | 11744 Dust Stormer (d8715, AirElemental, R4.3) **x1.40** | caster | no | as today (creatures L29-57) |
| 23 | Murloc Hatchling (90023) | 15186 Murky (d15369, BabyMurloc, R1.1, p10) **x0.85** | 735 Murloc Streamrunner (d527, Murloc, R1.9) **x1.25** | 127 Murloc Tidehunter (d1995, Murloc, R3.3) **x1.05** | humanoid | no | as today (creatures L1-19) |
| 24 | Fel Cinder (90024) | 4676 Lesser Infernal (d10906, Infernal, R7.8) **x0.30** | 6073 Searing Infernal (d10905, Infernal, R8.7) **x1.00** | 89 Infernal (d169, Infernal, R6.0, p7) **x1.65** | bruiser | no | as today (creatures L29-50) |
| 25 | Oozeling (90025) | 15429 Disgusting Oozeling (d15436, SlimeLesser, R0.4, p10) **x1.00** | 8257 Oozeling (d7763, SlimeLesser, R0.8) **x1.25** | 12387 Large Vile Slime (d4754, Slime, R5.7, p7) **x0.40** | controller | no | as today (creatures L5-57) |
| 29 | Kodo Calf (90029) | 2972 Kodo Calf (d1451/1454, KodoBeast, R7.1) **x0.20** | 3236 Barrens Kodo (d1453, KodoBeast, R10.8) **x0.35** | 11689 Riding Kodo (Brown) (d11641, RidingKodo, R5.1) **x1.00** | support | yes (seat: RidingKodo att.0 z=3.54) | as today (creatures L1-20) |
| 30 | Foal (90030) | 385 Horse (d229, Horse, R3.9) **x0.40** | 284 Riding Horse (Brown) (d2404, RidingHorse, R3.8) **x1.00** | - | skirmisher | yes (seat: RidingHorse att.0 z=1.87) | as today (creatures L1-5) |
| 31 | Mechanostrider Runt (90031) | 7749 Riding MechaStrider (Blue) (d6569, MechaStrider, R3.6) **x0.40** | 7749 Riding MechaStrider (Blue) (d6569, MechaStrider, R3.6) **x1.00** | - | skirmisher | yes (seat: MechaStrider att.0 z=1.62) small: ideal x1.25 | as today (creatures L1-1) |
| 32 | Skeletal Foal (90032) | 11153 Riding Skeletal Horse (Red) (d10670, RidingUndeadHorse, R4.2) **x0.40** | 11153 Riding Skeletal Horse (Red) (d10670, RidingUndeadHorse, R4.2) **x1.00** | - | skirmisher | yes (seat: RidingUndeadHorse att.0 z=1.94) | as today (creatures L1-1) |
| 33 | Ram Kid (90033) | 2098 Ram (d10000/10001, Ram, R4.3) **x0.40** | 4710 Riding Ram (Gray) (d2736, RidingRam, R4.3) **x1.00** | - | bruiser | yes (seat: RidingRam att.0 z=1.87) | as today (creatures L1-5) |

### 1c. Looks: a display per stage (`29_companion_looks.sql`)

The owner's rule: **every form of a line looks clearly different**, and a mountable line's last form
is a mount model. The stage creatures above often share one display (a Stranglethorn kitten, tiger
and elder tiger are all the red tiger), so each stage now names its own display
(`stageN_display`, `Companions::StageInfo::display`), picked from the same family's 1.12 displays:
another model where 1.12 has one (whelp -> drake -> dragon, a tiger cub model, a polar bear cub, the
dire bear, a red wisp spark, a lesser slime mote, the fel beast, the fire imp), else another skin.

- **Size.** `stageN_scale` is the companion's scale on top of the display's own (CreatureDisplayInfo
  x CreatureModelData), which is how vanilla scales any creature and what the section 0 rule meant;
  the server used to replace the display's size with the stage scale, so a 0.4-scale pup display
  stood as big as its adult. Each form on the same model as the one before is at least 1.2x bigger;
  a mount line's earlier forms are at most 1/1.25 of the next.
- **Mounts.** The wolf, cat, raptor and kodo lines end on a riding model (riding wolf / war wolf,
  riding sabercat, riding raptor, riding kodo) at 1.0, so the companion walks as the mount it is
  ridden as (`Index::MountDisplay` takes the display it walked with, `LookDisplay`). The other
  rideable lines keep their final creature (no 1.12 mount of their family) and their seat offsets.
- **One-skin models.** A few models are painted one way in 1.12 (faerie dragon, fire and water
  elemental, infernal, felhound, goblin shredder): there the forms differ by size (and the first
  form's model where one fits). The client's `every_stage_look_is_a_real_display_of_its_family`
  test checks every id against the install: the display and its model file exist, the model is of
  the line's family, consecutive forms differ in model or skin (or, on a one-skin model, grow), and
  the mount lines end on a riding model.

- **Three forms, a pet-sized first one** (section 1e). Every line has exactly three stages. The
  client test also measures each form from its model's own box (CreatureModelData x
  CreatureDisplayInfo x stage scale x the M2's bounding box, longest side): stage 1 is at most 2.5
  yards (vanity-pet size; the vanilla pets measure 0.7-2.5) and at most 0.6 of stage 3, and each form
  is at least 1.2x the one before.

Scales are the stage scale; **x1** on a riding model is the mount's own size. Tune by eye in game.

| # | Species | Stage 1 | Stage 2 | Stage 3 |
|---|---|---|---|---|
| 1 | Black Whelpling | d6288 DragonWhelp Black **x0.9** | d9586 Drake Black1 **x0.25** | d8574 Dragon 1Black **x0.2** |
| 2 | Crimson Whelpling | d6290 DragonWhelp Red **x1** | d6376 Drake Red1 **x0.3** | d2718 Dragon 1Red **x0.2** |
| 3 | Emerald Whelpling | d6291 DragonWhelp Green **x1** | d5145 Drake Green1 **x0.3** | d7863 DragonSpawnCaster Green **x1.35** |
| 4 | Azure Whelpling | d6293 DragonWhelp Blue **x1** | d13971 Drake Blue1 **x0.3** | d1687 Dragon 1Blue **x0.2** |
| 5 | Sprite Darter Hatchling | d6294 FaerieDragon - **x1** | d9345 FaerieDragon - **x0.75** | d2158 FaerieDragon - **x1** |
| 6 | Wolf Pup | d9572 direwolf DireWolfSkinDarkBrown **x0.95** | d802 wolf WolfSkinArctic **x0.85** | d247 ridingdirewolf RidingDireWolfSkinBrown **x1** |
| 7 | Nightsaber Cub | d11454 Tiger Dark **x0.6** | d9954 FrostSabre RidingTigerSkinLavender **x0.75** | d6448 RidingFrostSabre RidingTigerSkinDark **x1** |
| 8 | Frostsaber Cub | d16942 Tigercub TigerSkinWhite **x1.15** | d9953 Tiger White **x0.8** | d6080 RidingFrostSabre RidingTigerSkinWhite **x1** |
| 9 | Panther Cub | d11448 Tiger BlackSpotted **x0.6** | d599 Tiger Black **x1** | d11450 Tiger BlackStriped **x1.5** |
| 10 | Bear Cub | d5510 Bear Brown **x0.65** | d820 Bear DrkBrown **x1** | d2281 DruidBear - **x1.9** |
| 11 | Young Web Spider | d9934 tarantula TarantulaSkinGreen **x1.8** | d955 minespider MineSpiderSkinCave **x0.8** | d6808 tarantula TarantulaSkinGreen **x0.85** |
| 12 | Raptor Hatchling | d960 raptor RaptorSkinViolet **x0.5** | d1959 raptor RaptorSkinRed **x1.2** | d6473 ridingraptor RidingRaptorSkinViolet **x1** |
| 13 | Young Crocolisk | d1034 crocodile CrocodileSkinSwamp **x0.55** | d2548 crocodile CrocodileSkinMarsh **x0.8** | d807 crocodile CrocodileSkinSwamp **x1** |
| 14 | Infant Gorilla | d841 gorilla GorillaSkinSilver **x0.55** | d838 gorilla GorillaSkinGrey **x0.85** | d844 gorilla GorillaSkinSilver **x1.65** |
| 15 | Owlet | d15534 owl OwlWhite **x0.45** | d4566 owl OwlBrown **x1** | d10833 owl OwlWhite **x0.8** |
| 16 | Duskbat Pup | d4185 felbat BatSkinViolet01 **x1** | d1955 felbat BatSkin01 **x0.8** | d7894 felbat BatSkinViolet01 **x1.15** |
| 17 | Plainstrider Chick | d1284 tallstrider TallStriderSkinIvory **x0.35** | d1220 tallstrider TallStriderSkinGray **x1.2** | d1221 tallstrider TallStriderSkinIvory **x2.05** |
| 18 | Scorpid Hatchling | d10987 scorpion ScorpionskinPink **x0.4** | d3247 scorpion ScorpionskinRed **x1** | d2414 scorpion ScorpionskinPink **x1.7** |
| 19 | Ember Spark | d8409 fireelemental FireElementalSkin **x1.6** | d1070 fireelemental FireElementalSkin **x0.8** | d1405 fireelemental FireElementalSkin **x2.25** |
| 20 | Water Mote | d2069 waterelemental WaterElementalSkin **x1.4** | d5561 waterelemental WaterElementalSkin **x1** | d525 waterelemental WaterElementalSkin **x2** |
| 21 | Pebble | d4766 elementalearth RockElementalSkinGray **x0.18** | d171 elementalearth RockElementalSkinBrown **x1** | d2533 elementalearth RockElementalSkinGray **x1.25** |
| 22 | Zephyr | d5327 airelemental AirElementalSkinBrown **x0.65** | d8714 airelemental AirElementalSkinWhite **x1.25** | d8715 airelemental AirElementalSkinBrown **x1.4** |
| 23 | Murloc Hatchling | d15369 BabyMurloc MurlocBabyBlue **x0.85** | d527 Murloc Sahauginskin **x1.25** | d1079 Murloc SahauginskinBlue **x1.05** |
| 24 | Fel Cinder | d10906 Infernal - **x0.2** | d10905 Infernal - **x1** | d169 Infernal - **x1.9** |
| 25 | Oozeling | d8573 slimelesser SlimeSkinCrimson **x0.25** | d7763 slimelesser SlimeSkinAquaBlue **x1.25** | d4754 slime SlimeSkinCrimson **x0.4** |
| 26 | Snow Tiger Kitten | d748 tiger TigerSkinSnow **x0.55** | d16942 tigercub TigerSkinWhite **x1.45** | d749 tiger TigerSkinSnow **x0.95** |
| 27 | Snowshoe Rabbit | d328 Rabbit - **x1** | d4626 Rabbit Brown **x1.8** | d16191 Rabbit white **x1.05** |
| 28 | Spider Tank Bot | d14379 gnomebot GnomeSpiderTankSkinBlue **x0.27** | d8910 gnomebot GnomeSpiderTankSkinBlack **x1.15** | d6891 gnomespidertank GnomeSpiderTankSkinBlue **x0.75** |
| 29 | Kodo Calf | d1454 kodobeast KotoBeastSkinBrown **x0.2** | d1230 kodobeast KotoBeastSkinGray **x0.45** | d11641 ridingkodo RidingKotoBeastSkinYellow **x1** |
| 30 | Foal | d229 Horse Brown **x0.4** | d908 Horse Chessnut **x0.75** | d2404 RidingHorse Brown **x1** |
| 31 | Mechanostrider Runt | d10666 mechastrider MechaStriderSkin_WhiteBlue **x0.35** | d10661 mechastrider MechaStriderSkin_GrayGreen **x0.6** | d6569 mechastrider GnomeMechaStriderSkinBlue **x1** |
| 32 | Skeletal Foal | d5050 UndeadHorse - **x0.4** | d5228 RidingUndeadHorse UndeadHorseSkinBlack **x0.75** | d10670 RidingUndeadHorse UndeadHorseSkinRed **x1** |
| 33 | Ram Kid | d10000 Ram Grey **x0.4** | d10003 Ram White **x0.8** | d2736 RidingRam Grey **x1** |
| 101 | Webwood Spiderling | d709 tarantula TarantulaSkinGreen **x0.7** | d366 tarantula TarantulaSkinOrange **x1.25** | d336 tarantula TarantulaSkinGreen **x1** |
| 102 | Darkmist Hatchling | d2536 minespider MineSpiderSkinJungle **x2.2** | d14853 minespider MineSpiderSkinWetLands **x0.75** | d2537 minespider MineSpiderSkinJungle **x1** |
| 103 | Cinderweb Spiderling | d4457 tarantula TarantulaSkinMagma **x0.27** | d963 minespider MineSpiderSkinBlood **x0.7** | d7510 tarantula TarantulaSkinMagma **x1** |
| 104 | Black Bear Cub | d8843 Bear Black **x0.65** | d11908 Bear DrkBrown **x0.7** | d762 Bear Black **x1** |
| 105 | Ironfur Cub | d8840 bear BearSkinBlue **x0.45** | d865 bear BearSkinWhite **x0.9** | d8838 bear BearSkinBlue **x1** |
| 106 | Stranglethorn Kitten | d15151 tiger TigerSkinRed **x0.7** | d1043 tiger TigerSkinYellow **x0.9** | d6443 ridingfrostsabre RidingTigerSkinRed **x1** |
| 107 | Mountain Lion Cub | d1059 Tiger Brown **x0.5** | d1043 Tiger Yellow **x1** | d6442 RidingFrostSabre RidingTigerSkinBrown **x1** |
| 108 | Savannah Cub | d1978 Tiger LionessSkinGold **x0.4** | d1977 Lion Gold **x0.7** | d9714 RidingFrostSabre RidingTigerSkinGold **x1** |
| 109 | Bloodsnout Pup | d15204 direwolf DireWolfSkinReddishBrown **x1.15** | d741 direwolf DireWolfSkinDarkBlack **x0.75** | d2326 ridingdirewolf RidingDireWolfSkinReddishBrown **x1** |
| 110 | Silvermane Pup | d855 wolf WolfSkinArctic **x1** | d11419 direwolf DireWolfSkinLightGrey **x0.75** | d14336 pvpridingdirewolf PvPRidingDireWolfSkinWhite **x1** |
| 111 | Timber Wolf Pup | d9572 direwolf DireWolfSkinDarkBrown **x1.15** | d3202 wolf WolfSkinTimber **x0.65** | d2328 ridingdirewolf RidingDireWolfSkinDarkBrown **x1** |
| 112 | Sunscale Hatchling | d675 raptor RaptorSkinMottledBlueGreen **x0.45** | d1747 raptor RaptorSkinViolet **x0.9** | d6472 ridingraptor RidingRaptorSkinTurquoise **x1** |
| 113 | Deviate Hatchling | d674 raptor RaptorSkinMottledDarkGreen **x0.5** | d949 raptor RaptorSkinMottledBlueGreen **x0.9** | d4806 ridingraptor RidingRaptorSkinEmerald **x1** |
| 114 | Bloodfen Hatchling | d2571 Raptor Red **x0.3** | d788 Raptor Orange **x0.7** | d6469 RidingRaptor Tomato **x1** |
| 115 | Venomtail Scorpling | d5905 scorpion ScorpionskinBlack **x1.5** | d2732 scorpion ScorpionskinYellow **x0.9** | d2491 scorpion ScorpionskinBlack **x1** |
| 116 | Stonelash Scorpling | d13097 scorpion ScorpionskinSilver **x0.5** | d2765 scorpion ScorpionskinGolden **x1.1** | d15385 scorpion ScorpionskinSilver **x1** |
| 117 | Drywallow Hatchling | d831 crocodile CrocodileSkinSwamp **x0.38** | d2548 crocodile CrocodileSkinMarsh **x0.8** | d807 crocodile CrocodileSkinSwamp **x1** |
| 118 | Snapjaw Hatchling | d14660 seaturtle SeaTurtleSkinRed **x1.5** | d1244 seaturtle SeaTurtleSkin **x1.15** | d5027 seaturtle SeaTurtleSkinRed **x1** |
| 119 | Surf Glider Hatchling | d6368 seaturtle SeaTurtleSkinBlue **x0.75** | d7837 seaturtle SeaTurtleSkinGrey **x1** | d5127 seaturtle SeaTurtleSkinBlue **x1** |
| 120 | Groddoc Infant | d837 gorilla GorillaSkinSilver **x0.45** | d3188 gorilla GorillaSkinRed **x0.9** | d5294 gorilla GorillaSkinSilver **x1.55** |
| 121 | Goretusk Piglet | d6807 boar BoarSkinCrimson **x0.45** | d704 boar BoarSkinBrown **x0.8** | d3035 boar BoarSkinCrimson **x1** |
| 122 | Pygmy Crawler | d641 crab CrabSkinBronze **x0.5** | d981 crab CrabSkinIvory **x1** | d699 crab CrabSkinBronze **x1.15** |
| 123 | Cloud Serpent Hatchling | d3006 windserpent WindSerpentSkinBlack **x0.35** | d2705 windserpent WindSerpentSkinWhite **x1** | d7829 windserpent WindSerpentSkinBlack **x0.7** |
| 124 | Swoop Fledgling | d1192 carrionbird CarrionBirdSkinBlue **x0.21** | d2305 carrionbird CarrionBirdSkinBrown **x1.05** | d10826 carrionbird CarrionBirdSkinBlue **x0.9** |
| 125 | Roc Chick | d490 carrionbird CarrionBirdSkinRed **x0.26** | d2305 carrionbird CarrionBirdSkinBrown **x1.15** | d10827 carrionbird CarrionBirdSkinRed **x0.95** |
| 126 | Hecklefang Pup | d2726 hyena HyenaSkinBlack **x0.55** | d2711 hyena HyenaSkinYellow **x1.15** | d10902 hyena HyenaSkinBlack **x1.5** |
| 127 | Strigid Owlet | d15534 owl OwlWhite **x0.45** | d4877 owl OwlGrey **x1** | d10828 owl OwlWhite **x1.15** |
| 128 | Kraul Batling | d9535 felbat BatSkinBrown01 **x0.9** | d1955 felbat BatSkin01 **x1** | d11032 felbat BatSkinBrown01 **x0.8** |
| 129 | Foreststrider Fledgling | d1281 tallstrider TallStriderSkinPurple **x0.4** | d178 tallstrider TallStriderSkinBrown **x1** | d6477 tallstrider TallStriderSkinPurple **x1** |
| 130 | Aged Kodo Calf | d10916 kodobeast KotoBeastWoolySkin **x0.15** | d1308 kodobeast KotoBeastSkinGray **x0.3** | d12241 ridingkodo RidingKotoBeastSkinWhite **x1** |
| 131 | Gritjaw Hatchling | d1074 basilisk BasiliskSkinYellow **x0.22** | d4486 basilisk BasiliskSkinGreen **x0.9** | d7345 basilisk BasiliskSkinYellow **x1** |
| 132 | Glasshide Hatchling | d1073 basilisk BasiliskSkinRed **x0.85** | d8797 basilisk BasiliskSkinBlue **x1** | d12339 basilisk BasiliskSkinRed **x1** |
| 133 | Thresher Fry | d2425 threshadon ThreshadonSkinGray **x0.2** | d994 threshadon ThreshadonSkinAqua **x1.15** | d6431 threshadon ThreshadonSkinGray **x1.1** |
| 134 | Mystlash Hydraling | d2728 hydra HydraSkinBlack **x0.2** | d10898 hydra HydraSkinBlue **x1** | d2423 hydra HydraSkinBlack **x0.8** |
| 135 | Timberling Sprout | d5712 bogbeast BogBeastSkinGreen **x0.58** | d2567 bogbeast BogBeastSkinRed **x1.15** | d2024 bogbeast BogBeastSkinGreen **x1.6** |
| 136 | Toxic Mote | d5491 poisonelemental WaterElementalGreenSkin **x0.9** | d9449 fireelementalgreen FireElementalGreenSkin **x0.9** | d5497 poisonelemental WaterElementalGreenSkin **x1.5** |
| 137 | Cracked Golemling | d2690 golemstone StoneGolemSkinCopper **x0.34** | d10804 golemstone StoneGolemSkin **x1** | d10801 golemstone StoneGolemSkinCopper **x1.4** |
| 138 | Ectoplasm Blob | d7763 slimelesser SlimeSkinAquaBlue **x1.55** | d360 slime SlimeSkinBlackpudding **x1.25** | d11139 slime SlimeSkinAquaBlue **x1.05** |
| 139 | Creeping Sludgeling | d14350 SlimeLesser SlimeSkinGreen **x0.65** | d1145 Slime Green **x1.25** | d15686 GiantSlime Green **x0.25** |
| 140 | Black Drake Hatchling | d387 dragonwhelp DragonWhelpSkinBlack **x0.6** | d8713 dragonwhelp DragonWhelpSkinRed **x0.95** | d9585 drake DrakeSkinBlack1 **x1** |
| 141 | Imp Familiar | d10811 imp ImpSkinRed **x0.75** | d10812 imp ImpSkinBlack **x1.4** | d12190 fireimp ImpSkinRed **x1.35** |
| 142 | Felstalker Pup | d850 FelHound - **x0.5** | d12168 FelBeast RedBlack **x0.85** | d6173 FelHound - **x1** |
| 143 | Doomguard Spawn | d433 doomguard BalrogSkinRed **x0.5** | d9017 felguard FelGuardSkinBlue **x0.9** | d4426 doomguard BalrogSkinRed **x0.85** |
| 144 | Darkhound Whelp | d7892 darkhound HoundSkinPurple **x0.6** | d9020 darkhound HoundSkinBlue **x1.1** | d9022 darkhound HoundSkinPurple **x1.3** |
| 145 | Rattlecage Bones | d1019 skeleton SkeletonSkinGreen **x0.55** | d200 skeleton SkeletonSkinDarkTeal **x1.25** | d11489 skeleton SkeletonSkinGreen **x1.2** |
| 146 | Ghoul Thrall | d3670 Ghoul Grey **x0.8** | d570 Ghoul Red **x0.85** | d1693 FleshGolem 1 **x0.6** |
| 147 | Wailing Shade | d985 banshee BansheeSkin **x0.55** | d10702 banshee BansheeSkinPurple **x1** | d8783 banshee BansheeSkin **x1.15** |
| 148 | Crypt Scuttler | d9793 cryptfiend CryptFiendSkinPurple **x0.22** | d3004 cryptfiend CryptFiendSkin **x0.8** | d9758 cryptfiend CryptFiendSkinPurple **x1** |
| 149 | Servo | d1150 GolemHarvest HarvestGolemSkinGrey **x0.9** | d367 GolemHarvest HarvestGolemSkinBrown **x1** | d548 GolemHarvest HarvestGolemSkinBlue **x1.1** |
| 150 | Compact Reaper | d1159 GolemHarvestStage2 HarvestStage2Chest **x0.7** | d1303 GoblinShredder Goblin **x1.05** | d1269 GoblinShredder Goblin **x1** |
| 151 | Mechano-Tankling | d14379 gnomebot GnomeSpiderTankSkinBlue **x0.27** | d6890 gnomespidertank GnomeSpiderTankSkinRed **x0.8** | d6891 gnomespidertank GnomeSpiderTankSkinBlue **x1** |
| 152 | Riverpaw Runt | d10791 gnoll RiverPawGnollWhelpSkin **x0.55** | d384 GnollMelee RiverPawGnollFighterSkin **x1** | d609 GnollMelee RedRidgeGnollFighterSkin **x1.35** |
| 153 | Kobold Vermin | d10913 Kobold Normal **x0.75** | d2299 Kobold Albino **x1.1** | d511 Kobold Normal **x1.25** |
| 154 | Rockjaw Trogg | d990 troglodyte TroglodyteSkinGreen **x0.5** | d160 troglodyte TroglodyteSkinNormal **x1** | d6533 troglodyte TroglodyteSkinGreen **x1** |
| 155 | Ogre Runt | d11571 ogrewarlord OgreWarlordSkinGray **x0.28** | d3193 ogre OgreSkinGray **x1** | d11561 ogrewarlord OgreWarlordSkinGray **x0.85** |
| 156 | Kolkar Drudge | d9442 Centaur - **x0.5** | d9414 Centaur White **x1** | d9433 CentaurKhan Yellow **x1.15** |
| 157 | Bristleback Piglet | d1967 QuillBoar QuilBoarSkin **x0.55** | d1253 QuillBoar QuilBoarSkinGrey **x0.8** | d6108 QuillBoarWarrior QuilBoarSkinArmoredBlack **x1** |
| 158 | Gnarlpine Cub | d6827 furbolg FurbolgSkinWhite **x0.45** | d1999 furbolg FurbolgSkinBrown **x1** | d6828 furbolg FurbolgSkinWhite **x1.15** |
| 159 | Windfury Fledgling | d10873 harpy HarpyPurple **x0.34** | d1255 harpy HarpyAqua **x1.15** | d10874 harpy HarpyPurple **x1.35** |
| 160 | Troll Whelp | d5606 TrollMale - **x0.55** | d4579 TrollMale - **x1** | d7803 TrollDire DeepTrollSkinBlack **x1.1** |
| 161 | Stormscale Wave Rider | d4921 nagamale NagaMaleSkinGreen **x0.45** | d6747 nagamale NagaMaleSkinSilver **x1** | d9134 nagamale NagaMaleSkinGreen **x1.1** |
| 162 | Makrura Clacker | d2598 lobstrok LobstrokPurple **x0.27** | d2426 lobstrok LobstrokBlack **x1** | d1817 lobstrok LobstrokPurple **x1.15** |
| 163 | Young Moonkin | d12234 forceofnature OwlBearBlueSkin **x0.35** | d5927 forceofnature OwlBearSkin **x1** | d12237 forceofnature OwlBearBlueSkin **x1.5** |
| 164 | Moonrage Whelp | d657 worgen WargenSkinBlack **x0.6** | d1979 worgen WargenSkinBrown **x0.9** | d1098 worgen WargenSkinBlack **x1.2** |

### 1d. The vanilla pets (`31_vanilla_companions.sql`, species 165-214)

Every vanilla 1.12 non-combat pet is a battle companion: 61 pet items (every item whose spell is
SPELL_EFFECT_SUMMON_CRITTER, minus the Bloodsail Admiral's Hat, the Moro'gai gems and the orphan
whistles, which are equipment or quest items). Each pet is the **first form** of its line. The old
items are retired: every vendor, creature and object loot, container, quest reward and crafting or
"create item" spell that gave one now gives its companion item, and the server swaps every copy
left in bags, banks, mail and the auction house at startup (`Companions::VanillaPets`,
`FusionMgr::SwapRetiredPetItems`, before the auction house loads). Critters have no level band:
the lines are 1-60 like any other, stage 2 at level 20 anywhere, no catalyst.

Eleven pets already were a line's first form and give that species' item:

| Retired pet item | Species |
|---|---|
| Mechanical Squirrel Box 4401 | 28 |
| Cat Carrier (Silver Tabby) 8488 | 26 |
| Rabbit Crate (Snowshoe) 8497 | 27 |
| Tiny Emerald Whelpling 8498 | 3 |
| Tiny Crimson Whelpling 8499 | 2 |
| Hawk Owl 8501 | 15 |
| Dark Whelpling 10822 | 1 |
| Sprite Darter Egg 11474 | 5 |
| Worg Carrier 12264 | 6 |
| Blue Murloc Egg 20371 | 23 |
| Disgusting Oozeling 20769 | 25 |

Fifty new lines, items 91165-91214 (the pet item's own name, icon and price). Stage 1 wears the
pet's own creature and display (shrunk where the pet stood bigger than a pet); every line has three
forms (section 1e): kittens grow into a tiger cub and a tiger of their colouring, parrots into
bigger parrots, snakes into Wailing Caverns serpents, the cockroach into a scarab and a silithid,
chickens into a tallstrider (the mechanical one into mechanostriders), frogs into bigger frogs,
the clockwork pets into gnome bots, spider tanks and a shredder, the mechanical yeti into wendigos,
the panda into a polar cub and a bear, Mini Diablo into a felguard and a doomguard, snapjaws into
sea turtles, Speedy into the riding turtle, the shark into a hammerhead, Poley into ice bears, Mr.
Wiggles into a pig and a boar, the hippogryph hatchling into hippogryphs, the zergling into a
scarab and a silithid, the prairie dog into a squirrel, and Peddlefeet grows on the cupid model. Stage 2 at 20 and stage 3 at 40, anywhere,
no catalyst. The clockwork pets (family Mechanical) fight with the Critter moveset, as the
Mechanical Squirrel does (Critter: **Dash** / **Growl**, then **Thrash** / **Hamstring**, then
**Knockdown** / **Enrage**). The Stage 2/3 cells name the stage creature (reused from the stage
before when the form is only a new look) and the display it wears.

| # | Species (item) | Retired item | Stage 1 | Stage 2 | Stage 3 | Role |
|---|---|---|---|---|---|---|
| 165 | Shadowmaw Kitten (91165) | Cat Carrier (Bombay) 8485 | 7385 (d6943 tiger TigerSkinBlack **x0.75**) | 7385 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7385 (d2437 Tiger Black **x1.15**) | melee |
| 166 | Ridge Stalker Kitten (91166) | Cat Carrier (Cornish Rex) 8486 | 7384 (d2278 tiger TigerSkinBrown **x0.9**) | 7384 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7384 (d1056 Tiger Brown **x0.95**) | melee |
| 167 | Jungle Tiger Kitten (91167) | Cat Carrier (Orange Tabby) 8487 | 7382 (d918 tiger TigerSkinYellow **x0.45**) | 7382 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7382 (d1008 Tiger Yellow **x1.15**) | melee |
| 168 | Winterspring Kitten (91168) | Cat Carrier (White Kitten) 8489 | 7386 (d6248 tiger TigerSkinWhite **x0.75**) | 7386 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7386 (d11446 Tiger White **x0.85**) | melee |
| 169 | Ice Tiger Kitten (91169) | Cat Carrier (Siamese) 8490 | 7380 (d9951 tiger TigerSkinIce **x0.37**) | 7380 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7380 (d9949 Tiger Ice **x0.95**) | melee |
| 170 | Nightstalker Kitten (91170) | Cat Carrier (Black Tabby) 8491 | 7383 (d321 tiger TigerSkinBlackStriped **x0.4**) | 7383 (d16942 Tigercub TigerSkinWhite **x1.45**) | 7383 (d11449 Tiger BlackStriped **x1.15**) | melee |
| 171 | Corrupted Kitten (91171) | Cat Carrier (Corrupted Kitten) 11903 | 9936 (d9993 tiger TigerSkinGreen **x0.75**) | 9936 (d16942 Tigercub TigerSkinWhite **x1.45**) | 9936 (d9230 Tiger Green **x0.95**) | melee |
| 172 | Green Wing Macaw (91172) | Parrot Cage (Green Wing Macaw) 8492 | 7387 (d5866 parrot ParrotSkinGreen **x1**) | 7387 (d8818 Parrot White **x0.8**) | 7387 (d5865 Parrot Green **x0.6**) | melee |
| 173 | Hyacinth Macaw (91173) | Parrot Cage (Hyacinth Macaw) 8494 | 7391 Hyacinth Macaw (d6192) | 7391 (d8819 Parrot Orange **x0.8**) | 7391 (d8816 parrot ParrotSkinBlue **x1.4**) | melee |
| 174 | Senegal Parrot (91174) | Parrot Cage (Senegal) 8495 | 7389 Senegal (d6190) | 7389 (d8816 Parrot Blue **x0.8**) | 7389 (d8819 parrot ParrotSkinOrange **x1.4**) | melee |
| 175 | Cockatiel (91175) | Parrot Cage (Cockatiel) 8496 | 7390 Cockatiel (d6191) | 7390 (d8817 Parrot Crimson **x0.8**) | 7390 (d8818 parrot ParrotSkinWhite **x1.4**) | melee |
| 176 | Snowy Owlet (91176) | Great Horned Owl 8500 | 7553 (d15534 owl OwlWhite **x0.45**) | 7456 (d4877 Owl Grey **x0.6**) | 7456 (d10833 Owl White **x0.8**) | melee |
| 177 | Olive Serpent Hatchling (91177) | Black Kingsnake 10360 | 7565 (d2995 snake SnakeSkinGreen **x0.32**) | 7565 (d2955 snake White **x1.85**) | 7565 (d14557 Serpent SerpantSkinOlive **x1.05**) | melee |
| 178 | Brown Snake (91178) | Brown Snake 10361 | 7562 Brown Snake (d2957) | 7562 (d2958 snake Green **x1.85**) | 7562 (d15182 serpent SerpantSkinBrown **x1.25**) | melee |
| 179 | Crimson Snake (91179) | Crimson Snake 10392 | 7567 Crimson Snake (d6303) | 7567 (d2956 snake Blue **x1.85**) | 7567 (d4312 Serpent SerpantSkinPurple **x1.4**) | melee |
| 180 | Cockroach (91180) | Cockroach 10393 | 7395 Cockroach (d2177) | 7395 (d7470 SilithidScarab Black **x1.4**) | 7395 (d157 Silithid AurokkSkin **x0.95**) | melee |
| 181 | Prairie Dog (91181) | Prairie Dog Whistle 10394 | 14421 Brown Prairie Dog (d1072) | 14421 (d2838 PrarieDog - **x1.1**) | 14421 (d4468 Squirrel - **x0.9**) | melee |
| 182 | Mechanostrider Hatchling (91182) | Mechanical Chicken 10398 | 8376 (d6569 mechastrider GnomeMechaStriderSkinBlue **x0.35**) | 8376 (d6978 GnomeMechaStrider Red **x0.65**) | 8376 (d10666 MechaStrider _WhiteBlue **x1**) | melee |
| 183 | Ivory Tallstrider Chick (91183) | Ancona Chicken 11023 | 7394 (d1284 tallstrider TallStriderSkinIvory **x0.35**) | 7394 (d2741 tallstrider TallStriderSkinGray **x0.55**) | 7394 (d1221 TallStrider Ivory **x1.25**) | melee |
| 184 | Gray Tallstrider Chick (91184) | Chicken Egg 11110 | 7392 (d2741 tallstrider TallStriderSkinGray **x0.35**) | 7392 (d1219 tallstrider TallStriderSkinBrown **x0.85**) | 7392 (d1220 TallStrider Gray **x1.25**) | melee |
| 185 | Tree Frog (91185) | Tree Frog Box 11026 | 7549 (d14932 frog FrogOrange **x0.47**) | 7549 (d6297 Frog tropical **x1.4**) | 7549 (d1924 Frog Orange **x2.5**) | melee |
| 186 | Wood Frog (91186) | Wood Frog Box 11027 | 7550 Wood Frog (d901) | 7550 (d6296 Frog BlueSpots **x1.4**) | 7550 (d5379 frog FrogBrownSpots **x1.65**) | melee |
| 187 | Pet Bombling (91187) | Pet Bombling 11825 | 9656 Tiny Walking Bomb (d8909) | 9656 (d6977 GnomeBomBot GnomeSpiderTankSkinRed **x1**) | 9656 (d6890 GnomeSpiderTank Red **x0.75**) | melee |
| 188 | Lil' Smoky (91188) | Lil' Smoky 11826 | 9657 Lil' Smoky (d8910) | 9657 (d14379 GnomeBot GnomeSpiderTankSkinBlue **x0.6**) | 9657 (d1303 GoblinShredder Goblin **x1.2**) | melee |
| 189 | Lifelike Toad (91189) | Lifelike Mechanical Toad 15996 | 12419 Lifelike Toad (d901) | 12419 (d6297 Frog tropical **x1.4**) | 12419 (d5379 frog FrogBrownSpots **x1.65**) | melee |
| 190 | Tranquil Mechanical Yeti (91190) | Tranquil Mechanical Yeti 21277 | 15699 Tranquil Mechanical Yeti (d10269) | 15699 (d3207 Wendigo Brown **x0.5**) | 15699 (d927 wendigo WendigoSkinWhite **x1.2**) | melee |
| 191 | Smolderweb Hatchling (91191) | Smolderweb Carrier 12529 | 10598 (d4458 tarantula TarantulaSkinMagma **x0.21**) | 5857 (d963 MineSpider Blood **x0.5**) | 5857 (d7510 Tarantula Magma **x1**) | melee |
| 192 | Zergling (91192) | Zergling Leash 13582 | 11327 Zergling (d10993) | 11327 (d9469 SilithidScarab Yellow **x1.2**) | 11327 (d11091 Silithid AurokkSkinCopper **x0.65**) | melee |
| 193 | Panda Cub (91193) | Panda Collar 13583 | 11325 Panda Cub (d10990) | 11325 (d16189 PolarBearCub - **x1.6**) | 11325 (d913 Bear White **x1.1**) | melee |
| 194 | Mini Diablo (91194) | Diablo Stone 13584 | 11326 Mini Diablo (d10992) | 11326 (d5049 FelGuard Yellow **x1**) | 11326 (d4426 DoomGuard BalrogSkinRed **x0.8**) | melee |
| 195 | Albino Snapjaw (91195) | Turtle Egg (Albino) 18963 | 14633 Albino Snapjaw (d14661) | 2408 (d1244 SeaTurtle - **x0.8**) | 2408 (d10947 seaturtle SeaTurtleSkinWhite **x0.95**) | tank |
| 196 | Loggerhead Snapjaw (91196) | Turtle Egg (Loggerhead) 18964 | 14629 Loggerhead Snapjaw (d14657) | 2408 (d1244 SeaTurtle - **x0.8**) | 2408 (d7114 seaturtle SeaTurtleSkinBlue **x1.1**) | tank |
| 197 | Hawksbill Snapjaw (91197) | Turtle Egg (Hawksbill) 18965 | 14632 Hawksbill Snapjaw (d14660) | 2408 (d1244 SeaTurtle - **x0.8**) | 2408 (d5027 SeaTurtle Red **x0.95**) | tank |
| 198 | Leatherback Snapjaw (91198) | Turtle Egg (Leatherback) 18966 | 14630 Leatherback Snapjaw (d14658) | 2408 (d1244 SeaTurtle - **x0.8**) | 2408 (d7839 seaturtle SeaTurtleSkinGrey **x0.95**) | tank |
| 199 | Olive Snapjaw (91199) | Turtle Egg (Olive) 18967 | 14631 Olive Snapjaw (d14659) | 2408 (d7837 SeaTurtle Grey **x0.6**) | 2408 (d2902 seaturtle SeaTurtleSkin **x0.95**) | tank |
| 200 | Tiny Red Dragon (91200) | Red Dragon Orb 19054 | 14756 Tiny Red Dragon (d14779) | 1069 (d6376 Drake Red1 **x0.3**) | 13976 (d2718 Dragon 1Red **x0.2**) | ranged |
| 201 | Tiny Green Dragon (91201) | Green Dragon Orb 19055 | 14755 Tiny Green Dragon (d14778) | 741 (d5145 Drake Green1 **x0.3**) | 742 (d2717 Dragon 1Green **x0.2**) | ranged |
| 202 | Jubling (91202) | A Jubling's Tiny Home 19450 | 14878 Jubling (d14938) | 14878 (d6295 Frog Green **x1.65**) | 14878 (d14934 Frog Orange **x1.15**) | melee |
| 203 | Murki (91203) | Orange Murloc Egg 20651 | 15361 Murki (d15395) | 735 (d527 murloc Sahauginskin **x1.25**) | 735 (d5025 murloc SahauginskinOrange **x0.9**) | melee |
| 204 | Gurky (91204) | Pink Murloc Egg 22114 | 16069 (d15393 babymurloc MurlocBabyBlue **x1**) | 735 (d527 Murloc Sahauginskin **x1.25**) | 735 (d1079 Murloc SahauginskinBlue **x1.05**) | melee |
| 205 | Terky (91205) | White Murloc Egg 22780 | 16445 Terky (d15398) | 735 (d527 murloc Sahauginskin **x1.25**) | 735 (d11293 murloc SahauginskinWhite **x0.9**) | melee |
| 206 | Baby Shark (91206) | Baby Shark 21168 | 15661 Baby Shark (d15595) | 2187 (d12193 Shark Brown **x0.7**) | 2187 (d2851 HammerHead SharkSkinBlue **x1.05**) | tank |
| 207 | Peddlefeet (91207) | Truesilver Shafted Arrow 22235 | 16085 Peddlefeet (d15992) | 16085 (d15989 Cupid - **x1**) | 16085 (d15990 Cupid GoblinCupid **x1.65**) | melee |
| 208 | Poley (91208) | Polar Bear Collar 22781 | 16456 Poley (d16189) | 1196 (d8840 Bear Blue **x0.9**) | 1196 (d913 Bear White **x1.3**) | tank |
| 209 | Speedy (91209) | Turtle Box 23002 | 16547 Speedy (d16259) | 2408 (d16360 Turtle Brown **x1**) | 2408 (d17158 RidingTurtle TurtleMount **x1.55**) | tank |
| 210 | Mr. Wiggles (91210) | Piglet's Collar 23007 | 16548 Mr. Wiggles (d16257) | 157 (d16357 Pig Black **x1.2**) | 157 (d499 boar BoarSkinIvory **x0.9**) | tank |
| 211 | Whiskers the Rat (91211) | Rat Cage 23015 | 16549 (d4959 rat BrownSkin **x0.95**) | 16549 (d1141 Rat BlackSkin **x1.6**) | 16549 (d16143 rat BrownSkin **x1.05**) | melee |
| 212 | Spirit of Summer (91212) | Captured Flame 23083 | 16701 (d6537 fireelemental FireElementalSkin **x1**) | 6296 (d1070 FireElemental - **x0.8**) | 6296 (d1405 FireElemental - **x2.25**) | ranged |
| 213 | White Tiger Cub (91213) | White Tiger Cub 23712 | 17254 White Tiger Cub (d16942) | 7431 (d9953 Tiger White **x0.8**) | 7431 (d6080 RidingFrostSabre RidingTigerSkinWhite **x1**) | melee |
| 214 | Hippogryph Hatchling (91214) | Hippogryph Hatchling 23713 | 17255 Hippogryph Hatchling (d16943) | 17255 (d479 Hippogryph 01 **x0.55**) | 17255 (d10890 Hippogryph BlueSkin01 **x1.05**) | melee |

### 1e. Three forms for every line

The owner's rule: every line has exactly three forms, the first pet-sized. The lines that had fewer
now have three, all from the 1.12 install (later-version models are researched, not used:
`design/COMPANION_MODEL_RESEARCH.md`):

- **Starter lines.** Sprite Darter 6294 -> 9345 -> 2158 (one-skin model, grown); Young Crocolisk
  gains the swamp crocolisk d807; Owlet gains d4566 (brown owl) between owlet and white owl; Duskbat
  gains d1955; Tabby Cat (now Snow Tiger Kitten, 1f) -> tiger cub d16942 -> snow tiger d749; Snowshoe Rabbit -> white rabbit
  d16191 -> brown rabbit d4626; Mechanical Squirrel (now Spider Tank Bot, 1f) -> gnome bot d8910 -> spider tank d6891.
- **Mount lines 30-33** gain a middle form and still end on their riding model: foal -> horse
  d908 -> riding horse; runt -> mechanostrider d10661 -> blue mechanostrider; skeletal foal -> black
  riding skeletal horse d5228 -> red riding skeletal horse; ram kid -> white ram d10003 -> riding ram. Stage 2 at 20 anywhere,
  the old rule (40, catalyst, zone) now takes them to the riding form; their companion items
  (90065-90068) start at the riding stage (index 2).
- **Stage 1 sizes.** Every first form whose model measured over 2.5 yards was scaled down (Pebble,
  Fel Cinder, the whelpling-sized lines from 101 up, ...); later forms keep their scales except
  Cinderweb, Coyote and Felstalker stage 2, slimmed so stage 3 stays clearly bigger.

Display ids were checked against the install's `CreatureDisplayInfo.dbc` and model files by the
client's `every_stage_look_is_a_real_display_of_its_family` (the Critter, Dragon, Mechanical, Boar,
Turtle and Threshadon families now also allow the pets' models: parrot, snake, cockroach, prairie
dog, chicken, frog, zergling, panda, diablo, cupid, rat, hippogryph, chinese dragon, wendigo, pig,
turtle, shark).

### 1f. The first form is the young of the last

The owner's rule: a line's first form is visibly the young of its own last form, the same species
and the same colour, and is named for it ("Instead of Bombay Kitten, a Shadowmaw kitten"). Stage 1
now wears its last form's skin (a black-tiger line starts as a small black tiger, a red-raptor line
as a red hatchling); where 1.12 has no young model in that skin it is the last form's own model at pet
size (whelp-sized tiger, raptor, crocolisk, gorilla, basilisk...). Pet size and growth are unchanged
(stage 1 at most 2.5 yards and 0.6 of stage 3, each form at least 1.2x the one before). Items, item
entries, vendors and drop sources are unchanged: the Elwynn cat vendor still sells the cat lines'
starters, only their names changed. Live companions wear the new first form at their next summon.

A few vanilla pets kept their own look and name and the line's **last** form was recoloured to match
instead, because each is a distinct pet with no other skin of its young: the snapjaws (Albino ->
white, Loggerhead -> blue, Leatherback -> grey, Olive -> plain sea turtle), Murki (orange) and Terky
(white) murlocs, Senegal (orange), Hyacinth (blue) and Cockatiel (white) parrots, Wood Frog and
Lifelike Toad (brown-spotted frog), Brown Snake (brown serpent), the Mechanical Yeti (white
wendigo), Whiskers (white rat) and Mr. Wiggles (ivory boar).

Renamed starters (item entries unchanged):

| # | Old name | New name | Old item | New item |
|---|---|---|---|---|
| 26 | Tabby Cat | Snow Tiger Kitten | Cat Carrier (Alley Tabby) | Cat Carrier (Snow Tiger) |
| 28 | Mechanical Squirrel | Spider Tank Bot | Salvaged Squirrel Box | Salvaged Spider Tank Bot |
| 111 | Coyote Pup | Timber Wolf Pup | Coyote Pup Carrier | Timber Wolf Pup Carrier |
| 140 | Searing Hatchling | Black Drake Hatchling | Searing Whelp Egg | Black Drake Egg |
| 143 | Lesser Felguard | Doomguard Spawn | Felguard Binding Rune | Doomguard Binding Rune |
| 165 | Bombay Kitten | Shadowmaw Kitten | Cat Carrier (Bombay) | Cat Carrier (Shadowmaw) |
| 166 | Cornish Rex Kitten | Ridge Stalker Kitten | Cat Carrier (Cornish Rex) | Cat Carrier (Ridge Stalker) |
| 167 | Orange Tabby Kitten | Jungle Tiger Kitten | Cat Carrier (Orange Tabby) | Cat Carrier (Jungle Tiger) |
| 168 | White Kitten | Winterspring Kitten | Cat Carrier (White Kitten) | Cat Carrier (Winterspring) |
| 169 | Siamese Kitten | Ice Tiger Kitten | Cat Carrier (Siamese) | Cat Carrier (Ice Tiger) |
| 170 | Black Tabby Kitten | Nightstalker Kitten | Cat Carrier (Black Tabby) | Cat Carrier (Nightstalker) |
| 176 | Great Horned Owlet | Snowy Owlet | Great Horned Owl | Snowy Owl Egg |
| 177 | Black Kingsnake | Olive Serpent Hatchling | Black Kingsnake | Olive Serpent Egg |
| 182 | Mechanical Chicken | Mechanostrider Hatchling | Mechanical Chicken | Mechanostrider Hatchling |
| 183 | Ancona Chicken | Ivory Tallstrider Chick | Ancona Chicken | Ivory Tallstrider Egg |
| 184 | Prairie Chicken | Gray Tallstrider Chick | Chicken Egg | Gray Tallstrider Egg |

The other lines kept their names: the new first form already fits it (Stranglethorn Kitten, Black
Bear Cub, Raptor Hatchling...).

**Not matched yet** (no young of the right skin in 1.12; the client test lists them in
`FIRST_FORM_TONE_EXCEPTIONS`): 10 Bear Cub (dire bear), 27 Snowshoe Rabbit, 29 Kodo Calf (yellow
riding kodo), 32 Skeletal Foal, 112 Sunscale (turquoise), 146 Ghoul Thrall, 149 Servo, 150 Compact
Reaper, 152 Riverpaw Runt, 156 Kolkar Drudge, 157 Bristleback Piglet, 160 Troll Whelp, 175 Cockatiel,
179 Crimson Snake, 180 Cockroach, 181 Prairie Dog, 188 Lil' Smoky, 192 Zergling, 193 Panda Cub, 194
Mini Diablo, 209 Speedy, 214 Hippogryph Hatchling.

**Hook for WotLK young models.** A first form is only `stage1_display` / `stage1_scale` (the
`UPDATE` rows in `29_companion_looks.sql` and `31_vanilla_companions.sql`, mirrored by `kLooks` in
`Companions.cpp`). An imported young model gets a CreatureDisplayInfo id, goes in those three
places, and the line leaves the exception list once its skin matches; nothing else (items, creature,
moves) changes. The client test `every_stage_look_is_a_real_display_of_its_family` checks the new
id (display, model file, family, sizes) and the colour match (`skin_tone`).

### 1g. The flying mounts (`35_flying_companions.sql`, species 215-223)

Owner: "Flying: just start with how we can implement them into our companion system." Every Wrath
flying mount kind 3.3.5 has becomes a three-form line whose last form is that mount. The last
form is ridden (the wolf's spells 580 / 23250, level 40 and riding skill like every mount line)
and **skyrides**, Dragonflight-style (`Skyriding.h`, COMPANIONS.md "Flying mounts"): momentum
flight with no free climb, range from height, and its mounted move, **Launch**, a vigor
charge (three, refilling) for a throw straight up that also takes off from the ground.

The models come from the owner's 3.3.5a install through the WotLK patch layer (displays
40019-40037, `client/src/wotlk/import.rs` `LOOKS`). Sizes are the client's measure (the model's box,
longest side, at the display's scale times the stage scale); the riding form is at 1, the size it
is ridden at, and is the display it is ridden with (`ride_display`).

| # | Species (item) | Family | Stage 1 | Stage 2 | Stage 3 (ridden) | Drops / sold | Evolves |
|---|---|---|---|---|---|---|---|
| 215 | Gryphon Hatchling (91215) | 59 Gryphon, melee | d40019 gryphon hatchling (3.3.5 pet) x1, 1.4 yd | d1149 gryphon x0.6, 6.1 yd | d40020 armored riding gryphon, 10.2 yd | Wild and Razorbeak Gryphons (Hinterlands); Yarlyn Amberstill, Dun Morogh, 10g | 40, Primal Beast Essence, the Hinterlands |
| 216 | Wind Rider Cub (91216) | 60 Wind Rider, melee | d40021 wind rider cub (3.3.5 pet) x1, 1.9 yd | d295 wyvern x0.65, 7.9 yd | d40022 tawny riding wind rider, 12.2 yd | Pridewing and Highperch Wyverns; Halpa, Thunder Bluff, 10g | 40, Primal Beast Essence, Thousand Needles or Stonetalon |
| 217 | Frayfeather Hatchling (91217) | 61 Hippogryph, melee | d16943 hippogryph hatchling x1, 1.7 yd | d10890 blue hippogryph x0.55, 5.0 yd | d40023 blue riding hippogryph (Cenarion), 9.2 yd | Frayfeather (Feralas) and Thunderhead (Azshara) Hippogryphs; Shylenai, Darnassus, 10g | 40, Primal Beast Essence, Feralas or Azshara |
| 218 | Dragonhawk Hatchling (91218) | 62 Dragonhawk, ranged | d40024 red dragonhawk hatchling x0.9, 2.3 yd | d40025 golden dragonhawk x0.4, 8.9 yd | d40026 red riding dragonhawk, 14.7 yd | Azshara's blood elves (Surveyor, Reclaimer, Defender) | 40, Primal Beast Essence, Azshara |
| 219 | Netherwhelp (91219) | 1 Dragon, ranged | d40027 netherwhelp x1, 2.3 yd | d40028 purple nether drake x1, 11.5 yd | d40029 purple riding nether drake, 26.4 yd | the Dark Portal's Felguard Sentries, Dreadlords (Blasted Lands) | 40, Draconic Essence, the Blasted Lands |
| 220 | Proto-Drake Whelp (91220) | 1 Dragon, ranged | d40030 red proto-drake whelp x0.35, 2.5 yd | d40031 violet proto-drake x0.3, 14.0 yd | d40032 red riding proto-drake, 46.7 yd (box) | Pterrordax, Frenzied Pterrordax (Un'Goro) | 40, Draconic Essence, Un'Goro |
| 221 | Frostbrood Whelp (91221) | 1 Dragon, ranged | d40033 frost wyrm whelp (Frosty) x0.5, 2.4 yd | d40034 frost wyrm x0.5, 11.4 yd | d40035 white riding frost wyrm, 22.6 yd | the Plaguelands' Scourge Warders, Guards, Champions, Eyes of Naxxramas | 40 at night, Necrotic Residue, Eastern Plaguelands |
| 222 | Phoenix Hatchling (91222) | 14 Fire Elemental, ranged | d40018 phoenix hatchling x0.4, 2.2 yd | d40015 phoenix x0.5, 12.2 yd | d40036 riding phoenix (Ashes of Al'ar), 21.6 yd | Scorching Elementals, Living Blazes (Fire Plume Ridge), Blazing Elementals | 40, Elemental Core, Fire Plume Ridge or Un'Goro |
| 223 | Bronze Whelpling (91223) | 1 Dragon, ranged | d6292 bronze whelp x1, 2.1 yd | d13972 bronze drake x0.5, 9.6 yd | d40037 bronze riding drake, 22.6 yd | Corrupted Bronze Whelps, Bronze Drakonids (Blackwing Lair) | 40, Draconic Essence, Tanaris |

Stage 1 -> 2 at 20 anywhere for all. Movesets (section 2e): the four new families have their own,
the drakes share **Drake** (the dragonkin's with Launch) and the phoenix has **Phoenix**. The
proto-drake's 46.7 yards is its model box (the wings' reach in its animations), as for the whelp
lines' proto-drake forms. Not imported: nether rays (not asked for), the twilight and red riding
drakes (one drake line is enough for now), the netherwing drakes' other colours. Riding models
carry the mount seat (attachment 0) the client seats a rider on, so no `COMPANION_SEATS` entry is
added; if a converted one lacks it the client logs `MOUNTDISPLAYIDNOMOUNTATTACHMENT`. VERIFY in
game: every form's model, size and animations, the seat on each riding model, the leap and glide.
---

## 2. Movesets

### 2a. Rules

- Every companion has a **basic attack**, a **family trait** (passive, always on), and gains **one ability
  per stage**: at each evolution the owner picks one of two (stage 1 is picked at the first summon). A
  rideable final form also gets a **mounted move**. The pick is stored per item (three bytes in
  `character_fusion_companion`, see section 4) and can be changed at a stable master for a fee.
- **Numbers are WOMW's; the spell id is the vehicle.** The server casts the vanilla spell (triggered,
  from the companion) so the client shows its icon, cast animation, visual and aura, and overrides the
  magnitude through the spell's basepoints/duration/cooldown at cast time (`CastCustomSpell`-style
  overrides, or an aura-mod hook). The tooltip a stock client shows is vanilla's text; benilla should
  rewrite pet-spell tooltips from this table.
- **BH** = the companion's own average basic hit (its `damageMin/Max` from `Companions::Stats`), so
  abilities scale with the role and stage multipliers and stay under the hunter-pet yardstick. **AP** is
  never used. Cooldowns are in seconds. "Ranged" means 20 yards unless stated.
- Role budgets (keep within these when tuning): bruiser abilities lean on taunts, mitigation and AoE at
  60-150% BH; skirmishers on burst 150-250% BH and mobility; controllers on 2-5 s stuns or 4-8 s roots
  with 20-45 s cooldowns; casters on ranged 100-180% BH every 6-12 s; support on heals worth 10-15% of the
  owner's health per 30 s and party buffs. A single stun is never longer than 3 s on players (PvP
  diminishing returns as for vanilla stuns), and controllers' CC breaks on gun damage after 50% of its
  duration.
- Mounted moves are cast while riding (the server casts them triggered, bypassing vanilla's "can't do
  that while mounted"), never dismount, and are manual only.

### 2b. Beasts

| Family (role) | Basic attack | Trait | Stage 1 (pick one) | Stage 2 (pick one) | Stage 3 signature (pick one) | Mounted move |
|---|---|---|---|---|---|---|
| Spiders (controller) | Bite 17253, 100% BH | **Silk-spinner**: its first hit on a new target roots it 1.5 s (Web 12023 visual), once per 20 s | **Web** 4167: root 4 s, cd 20 / **Poison** 744: 60% BH nature over 12 s, cd 8 | **Venom Spit** 6917: ranged, 80% BH + 60% over 10 s, cd 12 / **Infected Bite** 7367: 50% BH over 15 s and target takes +5% physical, cd 15 | **Web Volley** 745: roots up to 3 enemies in an 8 yd cone 4 s, cd 30 / **Fatal Bite** 7938: 200% BH, heals itself twice that, cd 20 | **Skitter Web** 12023: drops a web behind, roots pursuers within 6 yd 3 s, cd 30 |
| Bears (bruiser) | Claw 16827, 100% BH | **Thick fur**: -10% physical damage taken | **Growl** 2649: taunt 3 s, cd 8 / **Maul** 12161: next hit +60% BH, cd 6 | **Swipe** 9754: 100% BH to up to 3 enemies in 5 yd, cd 8 / **Demoralizing Roar** 15971: enemies in 10 yd -15% melee damage 20 s, cd 30 | **Bash** 5211: stun 3 s, cd 30 / **Enrage** 8599: +30% attack speed, +15% damage 12 s, cd 60 | **Overrun** (Trample 5568): 80% BH and knockdown 1 s to enemies in 5 yd, cd 20 |
| Cats (skirmisher) | Claw 16827 | **Cat's grace**: +10% dodge, +10% run speed | **Rake** 1822: 50% BH + 90% over 9 s, cd 10 / **Prowl** 24450: stealth out of combat, opener +50%, cd 10 | **Pounce** 9005: 15 yd leap or from stealth, stun 2 s, cd 25 / **Dash** 23099: +40% speed 8 s, cd 30 | **Ravage** 3242: 250% BH, stun 2 s, cd 30 / **Frenzy** 19451: after a crit +50% attack speed 8 s, cd 30 | **Pounce leap** (Dash 23110): +80% speed 3 s and the next swing knocks down 1 s, cd 20 |
| Wolves, worgs, coyotes (skirmisher) | Bite 17253 | **Pack hunter**: +5% damage per other ally hitting its target (max +15%) | **Furious Howl** 24604: owner and party in 10 yd +8% damage on their next 3 attacks or shots, cd 20 / **Tendon Rip** 3604: -25% speed 8 s, cd 20 | **Dash** 23109: +60% speed 8 s, cd 30 / **Piercing Howl** 23600: daze enemies in 10 yd (-50% speed 6 s), cd 30 | **Charge** 22911: charge, 100% BH, stun 2 s, cd 20 / **Howling Rage** 7481: party in 20 yd +10% damage 15 s, cd 60 | **Run them down** (Dash 23110 + Knockdown 13360 on the first enemy touched, 2 s), cd 25 |
| Raptors (skirmisher) | Bite 17253 | **Ripping claws**: crits add Rend 13443 (30% BH over 15 s) | **Thrash** 3391: two extra attacks, cd 12 / **Rend** 13443: 75% BH over 15 s, cd 12 | **Infected Wound** 3427: target takes +8% physical 20 s, cd 20 / **Savage Rage** 3258: +50% attack speed 4 s, cd 20 | **Ravage** 3446: leap, 250% BH, stun 2 s, cd 40 / **Terrifying Screech** 6605: up to 3 enemies in 8 yd flee 4 s, cd 45 | **Leaping rake** (Ravage 3242): 150% BH, stun 1 s, cd 20 |
| Scorpids (controller) | Claw 16827 | **Chitin**: -10% physical damage, immune to poison | **Scorpid Poison** 24640: 20% BH over 10 s, stacks 5, cd 4 / **Poisonous Stab** 7357: target attacks 25% slower 15 s, cd 30 | **Knockdown** 5164: stun 2 s, cd 15 / **Venom Spit** 6917: ranged, as spiders | **Muscle Tear** 12166: 150% BH and -25% speed 5 s, cd 20 / **Venom Burst** (Scorpid Poison 24583 visual): consumes the target's stacks for 60% BH each, cd 20 | **Tail Sweep** 15847: 120% BH, knockback in a cone behind, cd 25 |
| Crocolisks (bruiser) | Bite 17253 | **Ambusher**: +20% damage on its first attack after standing still or swimming 3 s | **Growl** 2649 / **Gore** 4102: 120% BH bleed over 15 s, cd 30 | **Knockdown** 5164: stun 2 s, cd 15 / **Muscle Tear** 12166 | **Death roll** (Head Butt 11650): 150% BH, stun 3 s on a rooted or slowed target (2 s otherwise), cd 40 / **Harden Skin** 22693: +100% armor 10 s, cd 45 | **Tail Lash** 23364: stun 2 s in a cone behind, cd 25 |
| Turtles (bruiser) | Bite 17253 | **Shell**: -15% damage from the front | **Growl** 2649 / **Shell Shield** 26064: -50% damage taken, -43% attack speed 12 s, cd 120 | **Head Butt** 6730: stun 2 s, cd 20 / **Harden Skin** 22693 | **Shell Bash** (Knock Away 10101): 100% BH and knockback, cd 20 / **Snapping Challenge** (Suffering 17735): taunt all in 8 yd, cd 60 | **Shell ram** (Trample 5568, then Shell Shield 26064 on the rider 4 s), cd 45 |
| Gorillas (bruiser) | Claw 16827 | **Silverback**: +10% max health; owner within 10 yd +5% armor | **Thunderstomp** 26090: 60% BH nature to enemies in 8 yd, cd 20 / **Growl** 2649 | **Knock Away** 10101 / **Demoralizing Roar** 15971 | **War Stomp** 11876: 50% BH and stun 2 s in 8 yd, cd 30 / **Enrage** 8599 | - |
| Boars (bruiser) | Bite 17253 | **Bristleback**: +10% run speed out of combat, attackers take 5% of its BH (Thorns 467 visual) | **Boar Charge** 3385: +60% speed 4 s, next hit +50% BH, cd 15 / **Growl** 2649 | **Rushing Charge** 6268: charge, knockdown 1 s, cd 15 / **Gore** 4102 | **Trample** 5568: 100% BH to enemies in 5 yd, cd 15 / **Enrage** 8599 (fires itself below 35% health once per fight) | **Boar Charge** 3385 through a pack: knockdown 1 s, cd 15 |
| Crabs (bruiser) | Claw 16827 | **Carapace**: +15% armor | **Growl** 2649 / **Pierce Armor** 6016: target -10% armor 20 s, cd 45 | **Knockdown** 5164 / **Harden Skin** 22693 | **Shell Shield** 26064 / **Sunder Armor** 11971: -4% armor per stack, 5 stacks, cd 6 | - |
| Wind serpents (caster) | Lightning Breath 24844, ranged at its attack speed | **Storm-scaled**: +5% spell crit, +10% nature resistance | **Lightning Breath** 25008: 150% BH, cd 8 / **Dive** 23145: +40% speed 15 s, cd 30 | **Lightning Cloud** 6535: ground AoE 40% BH per 3 s for 15 s, cd 40 / **Gust of Wind** 6982: stun enemies in 8 yd 2 s, cd 45 | **Chain Lightning** 12058: 120% BH to 3 targets, cd 10 / **Wing Buffet** 18500: cone knockback, 60% BH, cd 25 | - |
| Carrion birds (controller; rocs caster) | Claw 16827 | **Carrion feeder**: heals 5% when an enemy within 20 yd dies | **Screech** 24423: 60% BH, enemies in melee -10% damage 4 s, cd 15 / **Dive** 23145 | **Swoop** 5708: cone stun 2 s, 50% BH, cd 30 / **Wing Flap** 11019: cone knockback, cd 20 | **Terrifying Screech** 6605 / rocs: **Flame Buffet** 9574: 120% BH fire, target +10% fire taken 20 s, cd 10 | - |
| Hyenas (skirmisher) | Bite 17253 | **Scavenger's laugh**: +10% damage to targets below 35% | **Tendon Rip** 3604 / **Furious Howl** 24604 | **Rabies** 3150: target -50% health regen and -5% stats 60 s, cd 30 / **Infected Bite** 7367 | **Fatal Bite** 7938 / **Thrash** 3391 | - |
| Owls (controller) | Claw 16827 | **Night eyes**: sees stealth within 15 yd; +10% damage at night | **Screech** 24423 / **Dive** 23145 | **Sonic Burst** 8281: silence enemies in 8 yd 3 s, cd 30 / **Rake** 1822 | **Terrifying Screech** 6605 / **Swoop** 5708 | - |
| Bats (controller) | Bite 17253 | **Echolocation**: immune to blind and darkness effects; sees stealth within 10 yd | **Screech** 24423 / **Dive** 23145 | **Sonic Burst** 8281 / **Fatal Bite** 7938 | **Terrifying Screech** 6605 / **Blood frenzy** (Frenzy 19451): +50% attack speed 8 s after Fatal Bite, cd 30 | - |
| Tallstriders (support) | Snap Kick 8646 visual, 100% BH | **Long stride**: owner +5% run speed while it is out | **Snap Kick** 8646: stun 1 s, cd 15 / **Dash** 23099 | **Wild Regeneration** 7948: owner or itself heals 12% over 20 s, cd 30 / **Battle Roar** 6507: party +5% damage 60 s, cd 60 | **Roar of Fortitude** 4238: party +8% max health 2 min, cd 180 / **Thrash** 3391 | **Stride** (Dash 23110): +80% speed 6 s, cd 45 |
| Kodos (support/bruiser) | Head butt (Bite visual) | **Herd bond**: party within 15 yd +5% armor | **Growl** 2649 / **Head Butt** 6730 | **Kodo Stomp** 6266: stun 2 s in 8 yd, cd 30 / **Roar of Fortitude** 4238 | **War Stomp** 19482: 100% BH and stun 2 s, cd 30 / **Trample** 5568 | **Kodo Stomp** 6266 |
| Basilisks (controller) | Bite 17253 | **Crystalline hide**: +20% armor, -10% run speed | **Crystal Gaze** 3635: stun 3 s, cd 45 / **Stone Skin** 5810: -15% damage taken and itself slowed 6 s, cd 30 | **Petrify** 11020: stun 4 s (target +30% armor), cd 40 / **Crystalline Slumber** 3636: sleep 8 s, breaks on damage, cd 45 | **Crystal Flash** 5106: cone stun 3 s, cd 45 / **Rock Skin** 8314: +armor 60 s, cd 120 | **Crystal Flash** 5106 |
| Threshadons (bruiser) | Bite 17253 | **Amphibious**: +40% swim speed; +20% damage while it or its target is in water | **Growl** 2649 / **Gore** 4102 | **Thrash** 3391 / **Knockdown** 5164 | **Tail Sweep** 15847 / **Fatal Bite** 7938 | - |
| Hydras (caster) | Acid Spit 9591, ranged | **Many heads**: the basic attack hits a second enemy for 50% | **Acid Spit** 15653: 150% BH, cd 8 / **Frost Breath** 3129: cone 80% BH, cd 12 | **Chilling Breath** 3132: 40% BH per 3 s and -20% speed 12 s, cd 30 / **Acid Breath** 12533: cone 60% BH + DoT, cd 20 | **Corrosive Acid Breath** 16359: cone 150% BH, -armor 30 s, cd 30 / **Fireball Volley** 15285: 100% BH to enemies in 10 yd, cd 20 | - |
| Dragonkin: whelps, drakes (caster) | Firebolt 3110, ranged | **Draconic**: +10% all resistances | **Firebolt** 3110: 150% BH, cd 8 / **Fire Shield** 2947: attackers take 10% BH fire 3 min | **Flame Buffet** 9574 / **Scorch Breath** 5010: cone 40% BH per 2 s for 10 s, cd 60 | **Flame Breath** 9573: cone 180% BH, cd 20 / **Bellowing Roar** 18431: enemies in 10 yd flee 3 s, cd 60 | drakes: **Flame Breath** 9573 |
| Faerie dragons (support, starter 5) | Arcane spit (Lightning Breath 24844 visual) | **Blink**: dodges the first ranged attack every 10 s | **Renew** 8362: owner heals 10% over 20 s, cd 20 / **Slow** 246 | **Sonic Burst** 8281 / **Arcane Explosion** 15253: 80% BH in 10 yd, cd 15 | **Frost Nova** 11831 / **Rejuvenation** 12160 | - |

### 2c. Elementals, oozes, demons, undead, mechanicals

| Family (role) | Basic attack | Trait | Stage 1 | Stage 2 | Stage 3 | Mounted move |
|---|---|---|---|---|---|---|
| Fire elementals (caster, starter 19) | Firebolt 3110 | **Burning core**: attackers take 10% BH fire (Fire Shield 2947 aura) | **Fire Shield** 2947 / **Firebolt** 3110 | **Fire Nova** 11969: 100% BH in 8 yd, cd 15 / **Flame Buffet** 9574 | **Fireball Volley** 15285 / **Immolate** 348: 60% BH + 120% over 15 s, cd 10 | - |
| Water elementals (support, starter 20) | Frostbolt 9672 (water bolt look) | **Tidal**: owner +1% health per 5 s while it is out of combat | **Frostbolt** 9672: 120% BH, slow 4 s / **Renew** 8362 | **Frost Nova** 11831: root 4 s in 8 yd, cd 25 / **Healing Wave** 11986: owner 15% health, cd 30 | **Ice Blast** 11264: 80% BH and freeze 4 s in 8 yd, cd 30 / **Healing Stream Totem** 5394: party 2% health per 2 s for 30 s, cd 60 | - |
| Earth elementals and golems (bruiser, starter 21 and 137) | Strike (melee) | **Stoneform**: immune to bleed, -10% physical damage | **Growl** 2649 / **Rock Skin** 8314 | **Knock Away** 10101 / **Boulder** 9483: ranged 150% BH, cd 12 | **Ground Smash** 12734: 120% BH, stun 2 s in 8 yd, cd 30 / **Harden Skin** 22693 | - |
| Air elementals (caster, starter 22) | Lightning Bolt 9532 | **Windborne**: immune to snares | **Lightning Bolt** 9532 / **Gust of Wind** 6982 | **Lightning Cloud** 6535 / **Slow** 246 | **Chain Lightning** 12058 / **Lightning Shield** 12550: attackers take 40% BH, 3 charges | - |
| Bog beasts, timberlings (controller) | Strike (Lash 6607 visual) | **Rooted**: cannot be knocked back | **Entangling Roots** 12747: root 5 s + 40% BH over it, cd 20 / **Growl** 2649 | **Grasping Vines** 8142: roots enemies in 8 yd 4 s, cd 40 / **Thorns** 782 on the owner (10 min) | **Wild Regeneration** 7948: heals 25% over 20 s, cd 60 / **Lash** 6607: stun 2 s and disarm chance, cd 20 | - |
| Poison elementals (caster) | Toxic Spit 7951 | **Toxic**: immune to poison; its DoTs last 20% longer | **Poison Bolt** 21067: 100% BH + DoT, cd 8 / **Acid Splash** 6306: DoT to enemies in 6 yd, cd 20 | **Venom Spit** 6917 / **Corrosive Acid** 8245: enemies in 8 yd -10% armor 30 s, cd 30 | **Plague Cloud** 3256: enemies in an area -5% stats 30 s, cd 60 / **Acid Breath** 12533 | - |
| Oozes (controller) | Slime (melee, nature) | **Amorphous**: immune to bleeds and roots, -20% run speed | **Corrosive Ooze** 9459: target -10% damage 20 s, cd 20 / **Acid Spit** 9591 | **Corrosive Acid** 8245 / **Disease Cloud** 12187: aura, enemies in 5 yd take 10% BH per 2 s | **Acid Splash** 6306 / **Black Sludge** 7279: enemies in 8 yd attack 30% slower and move 30% slower 15 s, cd 45 | - |
| Murlocs (humanoid, starter 23) | see 2d | | | | | |
| Infernals (bruiser, starter 24) | Strike | **Infernal**: immune to fire; attackers take 10% BH fire (Immolation 4436) | **Growl**-like **Torment** 3716 / **Fire Shield** 11350 aura: 20% BH fire to enemies in 5 yd per 3 s, 15 s, cd 30 | **War Stomp** 11876 / **Fire Nova** 11969 | **Rampage** 8285: 100% BH and stun 2 s in 8 yd, cd 25 / **Enrage** 8599 | - |
| Imps (caster) | Firebolt 3110 | **Phase-born**: Phase Shift 4511 out of combat, can't be targeted until it acts | **Fire Shield** 2947 / **Phase Shift** 4511 | **Fireball** 9053: 160% BH, cd 8 / **Fire Nova** 11969 | **Fireball Volley** 15285 / **Immolation** 4436: attackers take 15% BH fire, permanent | - |
| Felhounds (controller, anti-caster) | Bite 17253 | **Mana hunter**: +20% damage to targets with mana | **Devour Magic** 19505: dispel 1 magic effect from an enemy or ally, cd 8 / **Tendon Rip** 3604 | **Spell Lock** 19244: counter a cast, silence 3 s, cd 24 / **Mana Burn** 25779: burns mana, damage = 50% of it, cd 15 | **Devour Magic** 17012: purge 1 effect and heal itself 10%, cd 8 / **Howl of Terror** 5484: 3 enemies in 10 yd flee 3 s, cd 40 | **Spell Lock** 19244 |
| Felguards, doomguards (bruiser) | Strike (Cleave visual) | **Demonic armor**: +10% armor, +1% health per 5 s | **Torment** 3716: taunt, cd 8 / **Sunder Armor** 11971 | **Cleave** 15496: 110% BH to 2 targets, cd 6 / **Intimidating Shout** 19134: target cowers 4 s, others flee 3 s, cd 60 | **War Stomp** 11876 / **Mortal Strike** 13737: 150% BH, target -50% healing 5 s, cd 10 | - |
| Darkhounds, plaguehounds (skirmisher) | Bite 17253 | **Plaguefang**: 15% chance on hit to apply Infected Bite 7367 | **Tendon Rip** 3604 / **Fatal Bite** 7938 | **Infected Bite** 7367 / **Piercing Howl** 23600 | **Charge** 22911 / **Rabies** 3150 | - |
| Skeletons (bruiser) | Strike | **Bones**: immune to bleed and fear | **Shield Bash** 11972: interrupt 4 s, 50% BH, cd 12 / **Sunder Armor** 11971 | **Bone Shards** 17014: attackers take 15% BH 10 s, cd 30 / **Disarm** 6713: 5 s, cd 45 | **Bone Armor** 11445: absorbs 25% of its health 30 s, cd 60 / **Mortal Strike** 13737 | - |
| Ghouls and abominations (bruiser) | Claw 16827 | **Unliving**: immune to fear and poison | **Ghoul Rot** 12541: target -10% hit 20 s, cd 20 / **Torment** 3716 | **Ghoul Plague** 16458: target -20% speed and -10% damage 15 s, cd 30 / **Infected Bite** 7367 | **Disease Cloud** 12187: aura 10% BH per 2 s in 5 yd / **Cleave** 15496 (the abomination's hook swing) | - |
| Banshees, shades (caster) | Shadow Bolt 9613, ranged | **Incorporeal**: -15% physical damage taken | **Banshee Curse** 5884: target -10% hit 12 s, cd 15 / **Shadow Word: Pain** 11639: 120% BH over 18 s, cd 10 | **Drain Life** 17173: 100% BH over 5 s, heals itself, cd 15 / **Silence** 6726: 4 s, cd 30 | **Psychic Scream** 13704: 3 enemies in 8 yd flee 3 s, cd 45 / **Banshee Wail** 16565: 200% BH shadow, cd 15 | - |
| Crypt fiends (controller) | Claw 16827 | **Carapace**: -10% physical damage | **Web** 745: root 5 s, cd 20 / **Poison** 744 | **Infected Bite** 7367 / **Web** 4167 (ranged 20 yd root 4 s, cd 25) | **Fatal Bite** 7938 / **Venom Spit** 6917 | **Skitter Web** 12023 |
| Mechanicals: harvest golems, shredders (bruiser) | Strike (scythe) | **Machine**: immune to poison, bleed and fear; can't be healed by spells, repairs 5% per 2 s out of combat | **Growl** 2649 / **Knockdown** 5951 (golems) or **Sunder Armor** 11971 (shredders) | **Whirlwind** 15589: 100% BH to enemies in 5 yd, cd 12 / **Cleave** 15496 | **Sweeping Slam** 12887: cone 120% BH and knockback, cd 20 / **Harden Skin** 22693 (golems) or **Electrified Net** 11820 (shredders: root 5 s, cd 30) | shredders: **Saw** (Whirlwind 15589), cd 15 |
| Mechanicals: spider tanks (caster) | Shoot 6660, ranged | **Machine** | **Flame Spray** 10733: cone 50% BH per tick 3 s, cd 15 / **Electrified Net** 11820 | **Flame Cannon** 15575: 160% BH fire, cd 10 / **Frost Nova** 11831 | **Ice Blast** 11264 / **Bomb** 9143: ground AoE 150% BH, cd 20 | **Flame Spray** 10733 |
| Mechanostriders (skirmisher, starter 31) | Kick (Snap Kick 8646) | **Machine** | **Snap Kick** 8646 / **Dash** 23099 | **Electrified Net** 11820 / **Rushing Charge** 6268 | **Knockdown** 5951 / **Bomb** 9143 | **Overdrive** (Dash 23110), cd 45 |
| Horses, skeletal horses, rams (starters 30, 32, 33) | Kick / Head Butt 6730 | horse: **Steadfast** (+10% run speed); skeletal: **Unliving**; ram: **Ram-headed** (knockback immunity) | **Charge** 22911 / **Dash** 23099 | **Trample** 5568 / **Head Butt** 6730 | horse: **War Stomp** 11876 / **Battle Shout** 9128; skeletal: **Disease Cloud** 12187 / **Terrifying Screech** 6605; ram: **Knock Away** 10101 / **Demoralizing Roar** 15971 | **Trample** 5568 (horse), **Ghoul Plague** 16458 trail (skeletal), **Head Butt** 6730 (ram) |

### 2d. Humanoids

Humanoids (role "humanoid") fight with the weapon on their display and are the only lines that can roll
**Gunner**, which swaps the basic attack for a gun from the NPC gunner system (section 3). Without it they
use the basic attack below. Their trait is per race.

| Race (lines) | Flavor | Basic attack | Trait | Stage 1 | Stage 2 | Stage 3 |
|---|---|---|---|---|---|---|
| Gnolls (152) | skirmisher | Strike | **Pack cur**: +10% damage when another ally is within 10 yd of its target | **Hamstring** 9080: -40% speed 10 s, cd 10 / **Throw** 10277: ranged 120% BH, cd 8 | **Pierce Armor** 6016 / **Thrash** 3391 | **Battle Shout** 9128: party +8% damage 2 min / **Charge** 22911 |
| Kobolds (153) | utility | Strike (pick) | **Tunnel rat**: -20% threat; +10% loot money (stacks with Scavenger) | **Throw** 10277 / **Kick** 11978: interrupt 4 s, cd 12 | **Dynamite** 8800: ground AoE 150% BH, cd 20 / **Disarm** 6713 | **Earthbind Totem** 15786: enemies in 10 yd -50% speed 20 s, cd 45 / **Bomb** 9143 |
| Troggs (154) | bruiser | Strike (club) | **Thick-headed**: -25% stun duration | **Knockdown** 5951 / **Torment** 3716 | **Thunderclap** 8078: 60% BH, enemies in 8 yd slowed 10 s, cd 15 / **Disarm** 6713 | **Ground Smash** 12734 / **Enrage** 8599 |
| Ogres (155) | bruiser | Strike (two-hander) | **Ogre-size**: +10% health, knockback immune | **Knock Away** 10101 / **Demoralizing Shout** 13730 | **Cleave** 15496 / **Bloodlust** 6742: owner +20% attack and reload speed 15 s, cd 60 | **War Stomp** 11876 / **Mortal Strike** 13737 |
| Centaurs (156) | ranged skirmisher | Shoot (bow) 6660 | **Four legs**: +15% run speed | **Concussive Shot** 17174: -50% speed 4 s, cd 12 / **Throw** 10277 | **Multi-Shot** 14443: 100% BH to 3 targets, cd 10 / **Trample** 5568 | **War Stomp** 11876 / **Charge** 22911 |
| Quilboar (157) | support caster | Strike (spear) | **Bristles**: attackers take 5% BH (Thorns 467) | **Battle Shout** 9128 / **Thorns** 782 on the owner | **Healing Wave** 11986: owner 15% health, cd 30 / **Earth Shock** 13281: interrupt, 120% BH, cd 12 | **Earthbind Totem** 15786 / **Bloodlust** 6742 |
| Furbolgs (158) | support healer | Strike | **Ursine**: +10% health | **Rejuvenation** 12160: owner 10% over 12 s, cd 15 / **Battle Shout** 9128 | **Healing Wave** 11986 / **Entangling Roots** 12747 | **Healing Stream Totem** 5394 / **Cleanse** 4987: remove poison, disease and magic from the owner, cd 10 |
| Harpies (159) | caster | Claw 16827 | **Winged**: falls slowly (Slow Fall 130 when knocked off a ledge) | **Lightning Bolt** 9532 / **Slow** 246 | **Frost Shock** 12548: 120% BH, -50% speed 8 s, cd 12 / **Chain Lightning** 12058 | **Terrifying Screech** 6605 / **Lightning Cloud** 6535 |
| Trolls (160) | thrower | Throw 10277, ranged | **Regeneration**: +1% health per 3 s in combat | **Hamstring** 9080 / **Throw** 22887: 180% BH, cd 10 | **Hex** 16707: polymorph 5 s (breaks on damage), cd 40 / **Bloodlust** 6742 | **Whirlwind** 15589 / **Enrage** 8599 |
| Naga (161) | bruiser caster | Strike | **Amphibious** (as threshadons) | **Sunder Armor** 11971 / **Frostbolt** 9672 | **Frost Nova** 11831 / **Shield Bash** 11972 | **Cleave** 15496 / **Chain Lightning** 12058 |
| Makrura (162) | bruiser | Claw 16827 | **Shell**: -15% damage from the front | **Growl** 2649 / **Pierce Armor** 6016 | **Knockdown** 5164 / **Harden Skin** 22693 | **Shell Shield** 26064 / **Sweeping Slam** 12887 |
| Moonkin (163) | caster | Strike | **Moonlit**: +10% damage at night | **Moonfire** 15798: 80% BH + 60% over 12 s, cd 8 / **Wrath** 9739: 140% BH, cd 6 | **Rejuvenation** 12160 / **Entangling Roots** 12747 | **Thunderclap** 15588 / **Hibernate** 2637: sleeps a beast or dragonkin 10 s, cd 45 |
| Worgen (164) | skirmisher | Claw 16827 | **Moonrage**: +15% attack speed below 35% health | **Rend** 13443 / **Hamstring** 9080 | **Infected Wound** 3427 / **Fixate** 12021: the target must attack it 6 s and it attacks 50% faster, cd 45 | **Enrage** 8599 / **Terrify** 7399: fear 4 s, cd 40 |
| Murlocs (starter 23) | skirmisher | Strike | **Mrrgl**: +10% swim speed; +5% damage per other murloc within 20 yd | **Net** 12024: root 5 s, cd 25 / **Throw** 10277 | **Healing Wave** 11986 (oracle) / **Frost Nova** 11831 (tidehunter) | **Bloodlust** 6742 / **Battle Shout** 9128 |

### 2e. The flying mounts (section 1g)

Every one's mounted move is **Launch** (Slow Fall's feather icon, 130; nothing is cast): a
vigor charge for a throw straight up into or within a skyride, slot cd 1 (the charges limit it).

| Moveset (lines) | Basic attack | Trait | Stage 1 | Stage 2 | Stage 3 |
|---|---|---|---|---|---|
| Gryphon (215) | Claw 16827 | **Aerie-bred**: +10% damage to a target its owner is fighting | **Screech** 24423 / **Dive** 23145 | **Swoop** 5708 / **Rend** 13443 | **Thunderclap** 8078 / **Wing Buffet** 18500 |
| Wind Rider (216) | Bite 17253 | **Stinger**: 10% chance on hit to poison | **Dive** 23145 / **Poison** 744 | **Venom Spit** 6917 / **Swoop** 5708 | **Venom Burst** 24583 / **Terrifying Screech** 6605 |
| Hippogryph (217) | Claw 16827 | **Moon-blessed**: +10% healing taken | **Screech** 24423 / **Dive** 23145 | **Swoop** 5708 / **Rejuvenation** 12160 | **Wing Buffet** 18500 / **Entangling Roots** 12747 |
| Dragonhawk (218) | Firebolt 3110, ranged | **Sunfire**: +10% fire damage | **Firebolt** 3110 / **Dive** 23145 | **Flame Buffet** 9574 / **Fire Shield** 2947 | **Flame Breath** 9573 / **Swoop** 5708 |
| Drake (219-221, 223) | Firebolt 3110, ranged | **Draconic**: +10% all resistances | as Dragonkin | as Dragonkin | as Dragonkin |
| Phoenix (222) | Firebolt 3110, ranged | **Burning plumage**: attackers take 10% of its BH as fire | **Firebolt** / **Fire Shield** | **Fire Nova** 11969 / **Flame Buffet** | **Fireball Volley** 15285 / **Immolate** 348 |

The new families' traits are moveset text for now (nothing in the game reads them yet).

---

## 3. Traits

Rolled when the item drops (and at breeding), stored on the companion (a bitmask in
`character_fusion_companion`, see section 4) and shown on the tooltip ("Rare: Swift, Venomous"). Traits
are multiplicative with the role's stats and never lift a companion above
`HunterPetBaseline` x 1.0 (today's cap is 0.896; traits get the remaining ~10% as headroom; the tests
should check every trait combination stays under 1.0).

### 3a. The list

| Kind | Trait | Effect | Eligible |
|---|---|---|---|
| Combat | **Ironhide** | +10% armor, +5% max health | all |
| Combat | **Swift** | +15% run speed, +8% attack speed | all |
| Combat | **Ferocious** | +8% damage, +5% crit | all |
| Combat | **Bloodthirsty** | heals 15% max health on a killing blow; 3% of its damage heals it | not mechanical, not elemental |
| Combat | **Berserk** | below 35% health: +25% attack speed, +10% damage (Enrage 8599 visual) until healed above 50% | all |
| Combat | **Venomous** | 20% chance on hit: 30% BH nature over 12 s (Poison 744 visual), one stack | beasts, demons, undead, dragonkin, humanoids |
| Combat | **Thick-skulled** | stun and daze durations on it -40% | all |
| Elemental | **Fire-touched** | basic attacks +10% as fire; +20% fire resistance; ember glow | all; fire elementals and imps always roll this one if they roll an elemental |
| Elemental | **Frost-touched** | 10% chance on hit: -30% speed 4 s (Frostbolt 9672 slow); +20% frost resistance | all |
| Elemental | **Shadow-touched** | 4% of damage dealt heals it; +20% shadow resistance | not mechanical |
| Elemental | **Storm-touched** | 8% chance on hit: a lightning arc for 50% BH to up to 2 enemies (Chain Lightning 12058 visual) | all |
| Elemental | **Spellward** | -15% magic damage taken, -25% duration of spell CC on it | all |
| Utility | **Loyal** | a faint lasts a third as long (20 min, not 60); when the owner drops below 25% health it gains -30% damage taken 8 s (once per 60 s) | all |
| Utility | **Tracker** | owner gets the family's tracking while it is out: Track Beasts 1494, Humanoids 19883, Undead 19884, Elementals 19880, Demons 19878, Dragonkin 19879 (mechanicals: Track Hidden 19885) | all |
| Utility | **Scavenger** | +10% money from kills it took part in; 5% chance of an extra trade good (cloth, meat, scale) from the corpse | all |
| Utility | **Forager** | 3% chance per kill it took part in to find its family's breeding reagent (section 4) | all |
| Utility | **Packmule** | carries ammo: refills 1 magazine of the owner's equipped gun every 30 s in combat (every 10 s out of combat) | all |
| Body/looks | **Giant** | x1.2 scale at every stage (the mount too, on benilla), +8% health, -5% run speed | all; excludes Runt |
| Body/looks | **Runt** | x0.8 scale, +10% dodge, +5% run speed, -5% health | all; excludes Giant |
| Body/looks | **Albino** | pale variant: the line's lightest display at each stage, or a client tint where none exists; cosmetic | beasts, dragonkin; excludes the other two looks |
| Body/looks | **Shadowcoat** | dark variant (darkest display or tint); enemies detect it at 70% range at night | beasts, demons, undead; excludes the other looks |
| Body/looks | **Gilded** | golden sheen (tint and a faint glow); vendor price x5; cosmetic otherwise | all (the mechanicals' only look trait); excludes the other looks |
| Mount | **Surefooted** | while ridden: immune to daze (1604) and to being knocked off by damage; 50% less fall damage | rideable lines only |
| Mount | **Skybound** | while ridden: Slow Fall 130 when leaving a ledge; safe-fall distance +10 yd | rideable lines only |
| Mount | **Tireless** | while ridden +10% speed (60% -> 70%, 100% -> 110%); `CanRide` health floor 35% -> 15% | rideable lines only |
| Humanoid | **Gunner** | uses the NPC gunner system: basic attack becomes a gun at 25 yd, priced as the ranged role (40/55/35); stage 1 a pistol, stage 2 an SMG or shotgun, stage 3 a rifle or LMG (heavier gun each stage, same family as the line's NPC kits) | humanoid lines only (152-164 and the murloc 23) |
| Legendary | **Phoenix-blooded** | once per 10 min, on death: revives at 40% health in a burst of fire (Fire Nova 11969 visual, 100% BH to enemies in 8 yd) | all |
| Legendary | **Alpha** | party within 20 yd +3% damage; other companions and pets nearby +5% damage; it is never feared | all |
| Legendary | **Twin-souled** | both choices of one stage are active (the stage is rolled with the trait; stage 3 at a third of the rolls) | all |
| Legendary | **Primal** | stats as one stage higher (multiplier 1.12 -> 1.2 at stage 3; still capped at the baseline), ability cooldowns -20% | all |

### 3b. Roll weights per rarity

Item rarity is rolled when the item drops:

| Victim | Common | Uncommon | Rare | Epic | Legendary |
|---|---|---|---|---|---|
| critter, normal | 60% | 27% | 10% | 2.7% | 0.3% |
| elite, rare | 35% | 33% | 22% | 8.5% | 1.5% |
| rare elite, boss | 15% | 30% | 33% | 17% | 5% |

Traits per rarity: Common 0, Uncommon 1, Rare 2, Epic 3, Legendary 2 + 1 legendary. Each slot draws a kind,
then a trait in that kind (no duplicates, exclusions re-drawn):

| Kind weight | Uncommon | Rare | Epic | Legendary (non-legendary slots) |
|---|---|---|---|---|
| Combat | 40 | 36 | 32 | 30 |
| Elemental | 15 | 20 | 24 | 26 |
| Utility | 25 | 20 | 16 | 14 |
| Body/looks | 15 | 14 | 14 | 14 |
| Mount (rideable lines; else 0) | 5 | 6 | 8 | 8 |
| Gunner (humanoid lines; else 0) | 4 | 8 | 12 | 12 |

Inside a kind all traits weigh 1, except body/looks (Giant 3, Runt 3, Shadowcoat 3, Albino 2, Gilded 1).
The legendary slot draws Phoenix-blooded, Alpha, Twin-souled and Primal at 25 each.

### 3c. Exclusions and restrictions

- Giant and Runt exclude each other; Albino, Shadowcoat and Gilded exclude each other (one coloring).
- At most one of Fire-, Frost-, Shadow- and Storm-touched (one element); Spellward goes with any.
- Fire elementals, imps and infernals cannot roll Frost-touched; water elementals cannot roll
  Fire-touched; mechanicals cannot roll Bloodthirsty, Shadow-touched, Venomous, Albino or Shadowcoat;
  elementals and oozes cannot roll Bloodthirsty; undead cannot roll Albino.
- Mount traits only on lines with a rideable stage (they work from the first ride; on stages that are
  not yet rideable they show greyed on the tooltip).
- Gunner only on humanoid lines; it excludes nothing, but a Gunner companion keeps its stage abilities.

### 3d. Named combinations

Shown on the tooltip as a third line once both traits are present ("Viper: Venomous + Swift").

| Name | Traits | Bonus |
|---|---|---|
| **Viper** | Venomous + Swift | the poison also slows 30% for 4 s |
| **Juggernaut** | Ironhide + Thick-skulled | immune to knockback; -10% damage taken above 50% health |
| **Bloodfrenzy** | Bloodthirsty + Berserk | kills during Berserk extend it 4 s and heal 10% |
| **Wildfire** | Fire-touched + Ferocious | crits set the target burning, 40% BH over 6 s (Immolate 348 visual) |
| **Glacier** | Frost-touched + Ironhide | every third chill freezes the target 1.5 s (Frost Nova 11831 visual) |
| **Stormcaller** | Storm-touched + Swift | arcs hit 3 enemies and proc at 12% |
| **Nightstalker** | Shadowcoat + Shadow-touched | at night its first attack on a target stuns 2 s (once per target per 30 s) |
| **Voidward** | Spellward + Shadow-touched | absorbs the first harmful spell every 30 s |
| **Colossus** | Giant + Ironhide | +15% health instead of +8%; taunts what hits its owner automatically (cd 10 s) |
| **Warhorse** | Surefooted + Tireless | mounted +15% speed in total; dismount-proof while above 35% health |
| **Pathfinder** | Tracker + Forager | tracking also shows herbs and minerals; Forager chance 5% |
| **Golden Hoard** | Gilded + Scavenger | +25% money instead of +10% |
| **Quartermaster** | Gunner + Packmule | its own gun never runs dry; owner refills every 20 s in combat |
| **Ashborn** (legendary) | Phoenix-blooded + Fire-touched | revives at 60% health and the burst stuns 2 s |

---

## 4. Breeding

At **stable masters** (later the Kennel structure). Rules:

- **Who**: two stage-3 companions of the **same family** (the `Companions::Family` value), both in the
  player's bags (or one each from two players in a trade-like window), neither summoned elsewhere nor
  duelling. Same species breeds true; two species of one family make the first parent's species, with a
  chance of a hybrid coloring (below).
- **Inheritance**: each trait of each parent passes independently at **35%** (legendary **3%**).
  Duplicates count once; exclusions are resolved by keeping one at random; at most 3 traits (a legendary
  takes one of the three). Two Epic parents give 2.1 traits on average.
- **Mutation**: **5%** chance of one extra random eligible non-legendary trait (rolled with the Rare kind
  weights); **0.3%** chance of a legendary mutation if both parents are Epic or Legendary.
- **Offspring**: a new Bind on Equip item at level 1, stage 1, rarity from its trait count (0 Common,
  1 Uncommon, 2 Rare, 3 Epic, legendary trait = Legendary). At each evolution it may pick from the union of
  its own two choices and the choices its parents made at that stage (up to four options).
- **Limits**: each companion breeds at most **3** times; **72 h** real-time cooldown per parent after a
  breeding; **reagent**: 5 of the family reagent from each parent's owner (10 per breeding), plus a fee
  of 10 silver x the parents' average level.
- **Lineage**: the tooltip shows "Gen N, of <parent species> and <parent species>" and breedings left
  ("Bred 1 of 3").
- **Storage** (columns to add to `character_fusion_companion`): `traits` bigint unsigned (bitmask, 32 bits
  used), `rarity` tinyint, `picks` mediumint (three 1-byte ability picks), `breed_count` tinyint,
  `breed_ready` int (unix time), `generation` tinyint, `parent_a` / `parent_b` smallint (species),
  `hybrid` smallint (species whose coloring it wears, 0 none). The traits also have to reach the
  tooltip: they do not fit the suffix factor, so add them to the bar op or a new "item state" op.

### 4a. Breeding reagents

Vanilla items, chosen to fit the family and its level band (low / high band where two are given).

| Family | Reagent (item id) |
|---|---|
| Critters (the vanilla pets, now three-stage) | Small Egg 6889 |
| Spiders | Gooey Spider Leg 2251 / White Spider Meat 12205 |
| Bears | Bear Meat 3173 / Big Bear Meat 3730 |
| Cats | Lion Meat 3731 / Tiger Meat 12202 |
| Wolves, worgs | Stringy Wolf Meat 2672 / Tender Wolf Meat 12208 |
| Raptors | Raptor Egg 3685 / Raptor Flesh 12184 |
| Scorpids | Scorpid Stinger 5466 / Scorpid Scale 8154 |
| Crocolisks | Crocolisk Meat 2924 / Tender Crocolisk Meat 3667 |
| Turtles | Turtle Meat 3712 |
| Gorillas | Tel'Abim Banana 4537 / Soft Banana Bread 4601 |
| Boars | Chunk of Boar Meat 769 / Boar Ribs 2677 |
| Crabs | Crawler Meat 2674 / Crawler Claw 2675 |
| Wind serpents | Long Elegant Feather 4589 / Ironfeather 15420 |
| Carrion birds | Stringy Vulture Meat 729 / Buzzard Wing 3404 |
| Hyenas | Mystery Meat 12037 |
| Owls | Small Egg 6889 / Giant Egg 12207 |
| Bats | Large Fang 5637 |
| Tallstriders | Strider Meat 5469 |
| Kodos | Kodo Meat 5467 / Heavy Kodo Meat 12204 |
| Basilisks | Solid Stone 7912 (they eat stone) |
| Threshadons | Raw Spotted Yellowtail 4603 |
| Hydras | Raw Glossy Mightfish 13754 |
| Dragonkin (whelps, drakes, faerie dragons) | Black Whelp Scale 7286 / Red Whelp Scale 7287 / Green Whelp Scale 7392 (by line), faerie dragons Dreamfoil 13463 |
| Fire elementals, imps, infernals | Elemental Fire 7068 / Heart of Fire 7077 (imps and infernals: Felcloth 14256) |
| Water elementals, poison elementals | Elemental Water 7070 / Globe of Water 7079 |
| Earth elementals, golems | Elemental Earth 7067 / Core of Earth 7075 |
| Air elementals | Elemental Air 7069 / Breath of Wind 7081 |
| Bog beasts, timberlings | Wildvine 8153 / Living Essence 12803 |
| Oozes | Globe of Water 7079 |
| Demons (felhounds, felguards, darkhounds) | Felcloth 14256 |
| Undead | Ichor of Undeath 7972 / Essence of Undeath 12808 |
| Mechanicals | Handful of Copper Bolts 4359 / Gyrochronatom 4389 / Thorium Widget 15994 |
| Mounts' families (horse, ram, skeletal horse) | Handful of Oats (1528) is a quest item, so: Fine Aged Cheddar 3927 (ram), Moonberry Juice 1645 (horse), Ichor of Undeath 7972 (skeletal) |
| Gnolls, kobolds, troggs | Mystery Meat 12037, Coarse Stone 2836, Heavy Stone 2838 |
| Ogres, centaurs, quilboar | Mutton Chop 3770, Thick Leather 4304, Chunk of Boar Meat 769 |
| Furbolgs, moonkin, harpies | Moonberry Juice 1645, Wildvine 8153, Long Elegant Feather 4589 |
| Trolls, naga, makrura, worgen, murlocs | Flask of Mojo 8151, Black Pearl 7971, Zesty Clam Meat 7974, Tender Wolf Meat 12208, Murloc Fin 1468 |

### 4b. Hybrid colorings

Two species of one family: **8%** chance the offspring wears the other parent's coloring. It keeps its
own line (sizes, abilities); at each stage it uses the other species' display **when both stages use the
same model file** (a recolor, so animations and sizes match); stages without a matching model keep their
own display. Pairs with a full or partial match:

| Family | Pair (species ids) | Shared models |
|---|---|---|
| Spiders | Young Web Spider 11 x Darkmist 102 | MineSpider at stages 1-2 |
| Spiders | Young Web Spider 11 x Webwood 101 | Tarantula at stage 3 |
| Spiders | Webwood 101 x Cinderweb 103 | Tarantula at stages 2-3 |
| Bears | Bear Cub 10 x Black Bear 104 x Ironfur 105 | Bear at all stages (any pair) |
| Cats | Panther 9 x Stranglethorn 106 x Mountain Lion 107 | Tiger at all stages (any pair) |
| Cats | Nightsaber 7 x Frostsaber 8 | Tiger at stages 1-2, RidingFrostSabre at 3 |
| Wolves | Bloodsnout 109 x Silvermane 110 | DireWolf at all stages |
| Wolves | Wolf Pup 6 x Coyote 111 | Wolf at stage 2 |
| Raptors | Raptor 12 x Sunscale 112 x Deviate 113 x Bloodfen 114 | Raptor at stages 1-2 (and 3 among 112-114) |
| Scorpids | Scorpid 18 x Venomtail 115 x Stonelash 116 | Scorpion at all stages |
| Crocolisks | Young Crocolisk 13 x Drywallow 117 | Crocodile at stages 1-2 |
| Turtles | Snapjaw 118 x Surf Glider 119 | SeaTurtle at all stages |
| Gorillas | Infant Gorilla 14 x Groddoc 120 | Gorilla at all stages |
| Carrion birds | Swoop 124 x Roc 125 | CarrionBird at all stages |
| Owls | Owlet 15 x Strigid 127 | Owl at stages 1-2 |
| Bats | Duskbat 16 x Kraul 128 | FelBat at stages 1-2 |
| Tallstriders | Plainstrider 17 x Foreststrider 129 | TallStrider at all stages |
| Kodos | Kodo Calf 29 x Aged Kodo 130 | KodoBeast at stages 1-2 |
| Basilisks | Gritjaw 131 x Glasshide 132 | Basilisk at all stages |
| Oozes | Ectoplasm 138 x Sludgeling 139; Oozeling 25 x Sludgeling 139 | Slime at stages 2-3; SlimeLesser at stage 1 and Slime at 3 |
| Dragonkin | Black 1 x Crimson 2 x Emerald 3 x Azure 4 x Searing 140 | DragonWhelp at stages 1-2 (the classic whelp recolors) |
| Elementals | Ember Spark 19 x Water Mote 20 x Pebble 21 x Zephyr 22 | none (cross-element pairs only swap the stage-1 glow tint) |
| Demons | Felstalker 142 x Darkhound 144 | none: offspring stays its own line, 8% is a Shadowcoat roll instead |
| Humanoids | Riverpaw 152 x (any gnoll) | per race; humanoid families are one race each, so hybrids are rank variants of the same race (e.g. a gnoll with a Mosshide coat) |

---

## 5. Pet bar layout

Companions use the game's own pet bar (the real pet slot, `SMSG_PET_SPELLS`, ten buttons), so the
keybinds (Ctrl+1..0 by default; WOMW binds them to the side mouse and number row) work with the gun up,
the one exception to "abilities need the hands up". Abilities aim at the **crosshair**: the client sends
the unit under the crosshair (or the ground point under it) with the pet action; with nothing under it,
the current target.

| Slot | Content | Notes |
|---|---|---|
| 1 | **Command** (context) | crosshair on an enemy: attack it; on the ground: move there and hold; on nothing, or a double tap: follow. The icon shows what a press will do (Attack / Move / Follow). |
| 2 | **Stance: Assist / Aggressive** (toggle) | Assist = vanilla Defensive: attacks what the owner shoots and what attacks the owner. Aggressive engages anything hostile in 15 yd. |
| 3 | **Stance: Hold** | vanilla Passive + Stay: acts only on Command or a manual ability. |
| 4 | Stage 1 ability | autocast toggle (right click) |
| 5 | Stage 2 ability | greyed until stage 2 |
| 6 | Stage 3 ability | greyed until stage 3 |
| 7 | Mounted move | shown only while riding; otherwise empty (rideable lines) |
| 8-10 | spare | reserved: e.g. Ride/Dismount (today's companion-bar button), Evolve when ready, Dismiss. Duplicating the companion bar here would let the client drop that bar later. |

The small ring by the crosshair shows the companion's health and the three ability cooldowns (and the
mounted move while riding).

### 5a. Smarter autocast

Vanilla's pet AI casts an autocast spell whenever it is off cooldown and has a target. WOMW replaces that
with a condition per ability type (in the companion's AI, checked every 0.5 s like `UpdateCompanion`):

| Type | Abilities (examples) | Cast when |
|---|---|---|
| Taunt | Growl, Torment, Suffering, Snapping Challenge | an enemy hit the owner in the last 2 s and is not attacking the companion; or an ally under 50% health is being attacked. Never on a target already attacking the companion. |
| Self/ally heal | Wild Regeneration, Rejuvenation, Healing Wave, Renew, Healing Stream Totem | the owner or the companion is under 50% health and has no heal-over-time from it; the totem when 2+ allies are under 70% |
| Root / snare | Web, Net, Entangling Roots, Frost Nova, Tendon Rip, Hamstring, Concussive Shot | an enemy is moving toward the owner within 15 yd (a charging melee enemy), or a fleeing enemy is under 20% health |
| Interrupt | Spell Lock, Kick, Shield Bash, Earth Shock, Sonic Burst | an enemy within range is casting a spell of 1 s or longer, the owner or the companion is its target first |
| Stun | Bash, Knockdown, Head Butt, Crystal Gaze, Petrify | the target is casting, or is in melee with the owner; AoE stuns (War Stomp, Ground Smash, Kodo Stomp) need 2+ enemies in range |
| AoE damage | Swipe, Thunderstomp, Fire Nova, Trample, Whirlwind, breaths | 2+ enemies in range (cones: in front) and no crowd-controlled enemy would be hit (no breaking the owner's own CC) |
| Single-target damage | Bite, Rake, Ravage, Fireball, Lightning Breath | off cooldown with a target; burst abilities (Ravage, Fatal Bite, Mortal Strike) wait for a target under 50% or a boss |
| Debuff (stacking or long) | Scorpid Poison, Sunder Armor, Infected Bite, Curse, Rabies | the target lacks it, has fewer than max stacks, or it ends within 3 s |
| Party buff | Furious Howl, Battle Shout, Bloodlust, Roar of Fortitude, Howling Rage | the owner is in combat and lacks the buff; Furious Howl and Bloodlust prefer the moment the owner starts firing |
| Defensive | Shell Shield, Bone Armor, Harden Skin, Stone Skin, Phase Shift | the companion is under 40% health or attacked by 3+ enemies |
| Dispel | Devour Magic, Cleanse | an ally has a dispellable harmful effect (CC first), or an enemy has a beneficial magic effect (shields first) |
| Gap closer | Charge, Pounce, Dash, Boar Charge, Rushing Charge | its target is more than 8 yd away and not coming closer |
| Fear | Terrifying Screech, Psychic Scream, Howl of Terror, Intimidating Shout | 2+ enemies in melee with the owner or the companion; never in a dungeon pull with other packs within 30 yd (feared mobs pull more) |
| Mounted move | all | never autocast |
