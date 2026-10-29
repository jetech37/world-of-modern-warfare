# Companion model research: post-1.12 models for 3-stage lines

Research only. Nothing here is imported or wired up. Companion lines are those in
`COMPANION_CATALOG.md` sections 1a-1d (species 1-214). This file lists creature models added after
1.12 that could serve as better stage 1 (baby), stage 2 (adult) or stage 3 (elder / armored / mount)
forms. It then covers what loading them would take, the distribution problem, and a recommendation.

## How the rows were verified (revision 2)

- Every path marked **verified** was confirmed to exist in the current retail listfile through the
  wago.tools file API (`https://wago.tools/api/files?search=<term>`; the search term is given in the
  Source column as `wt:<term>`). The number is the file's FileDataID (FDID).
- **What the FDID says about expansion.** Every file that existed before Cataclysm got an FDID in
  roughly the 122,000-127,000 block, assigned alphabetically. So a low FDID means "vanilla, TBC or
  WotLK", not which one. For those rows, the expansion is from the pet or mount history and is marked
  "(exp. from history)". Later FDIDs fall in rough bands:
  - about 230k-330k: late WotLK patches
  - 380k-540k: Cataclysm
  - 570k-900k: MoP
  - 900k-1.1M: WoD
  - 1.1M-1.7M: Legion
  - 1.7M-3.0M: BfA
  - 3.0M-4.0M: Shadowlands
  - 4.0M-5.5M: Dragonflight
  - 5.5M and up: The War Within and later

  These bands are approximate.
- **unverified** means the path was not found or was not looked up. The row is kept only as a lead.
- The retail listfile also holds files *re-made* in later patches. A file under its old path in retail
  is not necessarily the same file as in the 3.3.5 client. For example, many vanilla creatures were
  re-modelled in Legion/BfA under names like `raptor2`, `boar2` and `gorilla2`, and some old paths were
  re-pointed. A later client of the target version (e.g. 3.3.5a) must be checked before use.
- Important check still open: some low-FDID files may already be in 1.12 (e.g. `bearcub`,
  `babycrocodile`). Before calling a file "new", check it against this project's own 1.12 MPQ
  listing (`dist\AzerothWarfare\game`).

Changes from revision 1 are listed at the end.

---

## 1. Model lists per family

### 1.1 Spiders (101-103, starter spider lines)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Nerubian spiderling | pre-Cata (WotLK likely, Azjol-Nerub) | creature/nerubianspiderling/nerubianspiderling.m2 | 125126 | verified wt:spider |
| S2 | Shale spider | Cata (Deepholm) | creature/shalespider/shalespider.m2 | 408915 | verified wt:spider |
| S2/S3 | Fire spider (Firelands) | Cata | creature/firespider/firespider.m2 | 516498 | verified wt:spider |
| S2 | Bone spider | late WotLK | creature/bonespider/bonespider.m2 | 234519 | verified wt:spider |
| S2/S3 | Giant spider remake | Legion/BfA | creature/giantspider2/giantspider2.m2 | 1513628 | verified wt:spider |
| S3 mount | Spider mount (Bloodfang Widow etc.) | Legion | creature/spidermount/spidermount.m2 | 1349631 | verified wt:spider |
| S3 mount | Vicious war spider (A/H) | Shadowlands | creature/viciousalliancespider/viciousalliancespider.m2 | 3536814 | verified wt:spider |
| S1 mech | Baby spider tank | Legion | creature/babyspidertank/babyspidertank.m2 | 1138491 | verified wt:spider |

The earliest real riding spider is Legion, which is the hardest format to load (chunked file plus .skel).

### 1.2 Bears (104-105, starter bear lines, Poley)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Bear cub | pre-Cata (check whether it is in 1.12) | creature/bearcub/bearcub.m2 | 123053 | verified wt:cub |
| S1 | Polar bear cub (Poley) | pre-Cata (in 1.12) | creature/bear/polarbearcub.m2 | 123051 | verified wt:cub |
| S3 mount | Riding bear (TBC-era bear mount, Amani war bear) | pre-Cata (exp. from history: TBC) | creature/bearmount/bearmount.m2 | 123058 | verified wt:bearmount |
| S3 mount | Alt riding bear | pre-Cata | creature/bearmountalt/bearmountalt.m2 | 123067 | verified wt:bearmount |
| S3 mount | Northrend bear mount (Armored Brown Bear) | WotLK | creature/northrendbearmount/northrendbearmountarmored.m2 | 234737 | verified wt:bearmount |
| S3 mount | Northrend bear, unarmored / BlizzCon (Big Blizzard Bear) | WotLK | creature/northrendbearmount/northrendbearmount.m2, ...blizzcon.m2 | 234736, 234740 | verified wt:bearmount |
| S3 mount | Vicious war bear (A/H) | Legion | creature/viciousalliancebearmount/viciousalliancebearmount.m2 | 1499645 | verified wt:bearmount |
| S1 | Primal bear cub | Dragonflight | creature/bearprimalcub/bearprimalcub.m2 | 4532352 | verified wt:cub |

### 1.3 Cats / sabers (Nightsaber line 7, tiger lines, white tiger cub, tabbies)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Tiger cub (in 1.12) | 1.12 | creature/tiger/tigercub.m2 | 126173 | verified wt:tiger |
| S1 | Saber cub (Winterspring Cub) | Cata | creature/frostsabre/sabrecub.m2 | 511423 | verified wt:cub |
| S1 | Spectral tiger cub | late WotLK (TCG pet) | creature/spectraltiger/spectraltigercub.m2 | 326934 | verified wt:cub |
| S3 mount | Spectral tiger / swift spectral tiger | pre-Cata (exp. from history: TBC TCG) | creature/spectraltiger/spectraltiger.m2, spectraltigerepic.m2 | 125988, 125989 | verified wt:tiger |
| S2 | Siberian (Pandaria) tiger | MoP | creature/siberiantiger/siberiantiger.m2 | 555561 | verified wt:tiger |
| S3 mount | Siberian tiger mount | MoP | creature/siberiantiger/siberiantigermount.m2 | 632248 | verified wt:tiger |
| S1 | Saber remake cub | Legion | creature/saber2cub/saber2cub.m2 | 1116253 | verified wt:cub |
| S1/S2/S3 | Baby Eversong lynx / Eversong lynx / Arathi lynx mount | TWW-era remakes | creature/babyeversonglynx/babyeversonglynx.m2, creature/eversonglynx/eversonglynx.m2, creature/arathilynxmount/arathilynxmount.m2 | 6830542, 6338575, 5689882 | verified wt:lynx |
| - | TBC Eversong lynx (original) | TBC | not found as `creature/lynx/lynx.m2`; likely an existing cat model with new skins | - | unverified (rev. 1 path was wrong) |

### 1.4 Wolves and worgs (line 6)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Frostwolf pup | WoD | creature/frostwolfpup/frostwolfpup.m2 | 970920 | verified wt:wolf |
| S2 | Draenor wolf | WoD | creature/wolfdraenor/wolfdraenor.m2 | 926251 | verified wt:wolf |
| S3 mount | Draenor wolf mount / armored | WoD | creature/wolfdraenor/wolfdraenormount.m2, wolfdraenormountarmored.m2 | 960116, 936994 | verified wt:wolf |
| S3 mount | Spectral riding dire wolf | Cata | creature/ridingdirewolfspectral/ridingdirewolfspectral.m2 | 464116 | verified wt:wolf |
| S2/S3 | Kor'kron elite wolf | MoP | creature/korkronelitewolf/korkronelitewolf.m2 | 893771 | verified wt:wolf |

### 1.5 Raptors

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Raptor pet (most likely the WotLK 3.1 hatchling pets: Darting, Ravasaur, Razzashi, etc.) | late WotLK | creature/raptor/raptorpet.m2 | 305835 | path verified wt:raptor; link to the hatchling pets is unverified |
| S1 | Baby raptor (newer hatchling model) | BfA | creature/babyraptor/babyraptor.m2 (+ 00.skin, .anim, 5 skins) | 2459259 | verified wt:babyraptor |
| S1 | Zandalari baby raptor | MoP | creature/zandalaribabyraptor/zandalaribabyraptor.m2 | 797334 | verified wt:babyraptor |
| S2 | Outland raptor | pre-Cata (exp. from history: TBC) | creature/raptoroutland/raptor_outland.m2 | 125568 | verified wt:raptor |
| S3 | Skeletal raptor + mount (Venomhide / Ravasaur-era undead raptor) | late WotLK | creature/skeletalraptor/skeletalraptormount.m2 | 405425 | verified wt:raptor (FDID suggests Cata) |
| S3 mount | Armored raptor | BfA | creature/armoredraptor/armoredraptor.m2 | 1719222 | verified wt:raptor (rev. 1 said Cata: wrong) |
| S3 mount | Vicious war raptor | WoD/Legion | creature/raptor2/viciouswarraptor.m2 | 1043670 | verified wt:raptor |

### 1.6 Scorpids

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S3 mount | Horde scorpion mount (Kor'kron-style) | Cata | creature/hordescorpionmount/hordescorpionmount.m2 | 463776 | verified wt:scorpion (rev. 1 said MoP `scorpionmount`: wrong) |
| S2 | Fire scorpion (Fandral, Firelands) | Cata | creature/fandralfirescorpion/fandralfirescorpion.m2 | 514172 | verified wt:scorpion |
| S2/S3 | Crystal scorpion | Legion | creature/crystalscorpion/crystalscorpion.m2 | 1279536 | verified wt:scorpion |
| S1 | Scorpling pet | Cata/MoP | no dedicated model found; probably a scaled scorpion | - | unverified |

### 1.7 Crocolisks

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Baby crocolisk (Chuck/Muckbreath/Snarly/Toothy fishing pets) | pre-Cata (exp. from history: WotLK 3.3) | creature/babycrocodile/babycrocodile.m2 | 122977 | verified wt:crocodile |
| S1 | Baby thunder crocolisk | MoP (Isle of Thunder) | creature/babythundercrocodile/babythundercrocodile.m2 | 801465 | verified wt:crocodile |
| S2/S3 | Cataclysm crocolisk | Cata | creature/cataclysmcrocodile/cataclysmcrocodile.m2 | 387274 | verified wt:crocodile |
| S3 mount | none found | - | - | - | - |

### 1.8 Turtles (Family 30, Speedy, Snapjaw pets)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Baby turtle | BfA | creature/babyturtle/babyturtle.m2 | 1738226 | verified wt:baby |
| S2 | Sea turtle | pre-Cata (exp. from history: WotLK 3.3 Sea Turtle) | creature/seaturtle/seaturtle.m2 | 125837 | verified wt:turtle |
| S3 mount | Riding Turtle | pre-Cata (exp. from history: TBC 2.x TCG) | creature/ridingturtle/ridingturtle.m2 | 125715 | verified wt:turtle |
| S2 | Dragon turtle | MoP | creature/dragonturtle/dragonturtle.m2 | 606106 | verified wt:turtle |
| S3 mount | Riding dragon turtle / great (epic) | MoP | creature/dragonturtle/ridingdragonturtle.m2, ridingdragonturtleepic.m2 | 606385, 606557 | verified wt:turtle |
| S3 | Ancient / deep sea turtle | Cata | creature/ancientseaturtle/ancientseaturtle.m2, creature/deepseaturtle/deepseaturtle.m2 | 418699, 365647 | verified wt:turtle |

### 1.9 Gorillas

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Gorilla pet (small) | pre-Cata (check whether it is in 1.12) | creature/gorilla/gorillapet.m2 | 124265 | verified wt:gorilla |
| S2/S3 | Gorilla remake | BfA | creature/gorilla2/gorilla2.m2 | 1716212 | verified wt:gorilla |
| S3 mount | none found | - | - | - | - |

### 1.10 Boars (Family 28, Mr. Wiggles)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Baby pig | BfA | creature/babypig/babypig.m2 | 1720966 | verified wt:baby |
| S2 | Fel boar | pre-Cata (exp. from history: TBC Hellfire) | creature/felboar/felboar.m2 | 123839 | verified wt:boar |
| S2 | Giant (Draenor) boar | WoD | creature/giantboar/giantboar.m2 | 926354 | verified wt:boar |
| S3 mount | Giant boar mount / armored (Trained Rocktusk, Armored Frostboar) | WoD | creature/giantboar/giantboarmount.m2, giantboararmoredmount.m2 | 930449, 937216 | verified wt:boar |

### 1.11 Crabs, hydras, threshadons, sea creatures

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 sea | Baby seahorse | Cata | creature/seahorse/seahorse_baby.m2 | 466080 | verified wt:baby |
| S3 sea | Sea serpent | Legion | creature/seaserpent/seaserpent.m2 | 1537045 | verified wt:serpent |
| S1 | Baby octopus | BfA | creature/babyoctopus/babyoctopus.m2 | 2100909 | verified wt:baby |
| crab/hydra | not searched in this pass | - | - | - | unverified |

### 1.12 Wind serpents (Family 31)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S2 | Outland wind serpent | pre-Cata (exp. from history: TBC) | creature/windserpentoutland/windserpentoutland.m2 | 126465 | verified wt:serpent |
| S1 | Pandaren serpent pet (cloud serpent hatchling) | MoP | creature/pandarenserpentpet/pandarenserpentpet.m2 | 630609 | verified wt:serpent |
| S2 | Pandaren cloud serpent | MoP | creature/pandarenserpent/pandarenserpent.m2 | 577443 | verified wt:serpent (rev. 1 `cloudserpent` path was wrong) |
| S3 mount | Cloud serpent mount | MoP | creature/pandarenserpent/pandarenserpentmount.m2 | 587703 | verified wt:serpent |
| S3 mount | Celestial serpent mount | MoP | creature/celestialserpent/celestialserpentmount.m2 | 643241 | verified wt:serpent |
| S2/S3 | Wind serpent remake | Legion | creature/windserpent2/windserpent2.m2 | 1661930 | verified wt:serpent |
| S3 mount | Serpent mount | Legion | creature/serpentmount/serpentmount.m2 | 1519569 | verified wt:serpent |

### 1.13 Carrion birds, owls, bats, hippogryph hatchling

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Phoenix pet (Phoenix Hatchling) | pre-Cata (exp. from history: TBC Magisters' Terrace) | creature/phoenixpet/phoenixpet.m2 | 125417 | verified wt:phoenix |
| S2 | Phoenix | pre-Cata (TBC) | creature/phoenix/phoenix.m2 | 125407 | verified wt:phoenix |
| S3 mount | Riding phoenix (Ashes of Al'ar) | pre-Cata (TBC) | creature/ridingphoenix/ridingphoenix.m2 | 125644 | verified wt:phoenix |
| S1/S3 | Dark phoenix pet / mount | Cata | creature/darkphoenixpet/darkphoenixpet.m2, creature/darkphoenix/darkphoenixmount.m2 | 463512, 402149 | verified wt:phoenix |
| S1 | Thunder Isle baby bird | MoP | creature/thunderislebird/thunderislebabybird.m2 | 838493 | verified wt:baby |
| S1 | Hippogryph pet (in 1.12 already? hatchling is a 1.12-era pet) | pre-Cata | creature/hippogryph/hippogryphpet.m2 | 124393 | verified wt:hippogryph |
| S3 mount | Hippogryph mount (Cenarion War Hippogryph, TBC) | pre-Cata | creature/hippogryphmount/hippogryphmount.m2 | 124410 | verified wt:hippogryph |
| S3 mount | Fel hippogryph mount | Cata | creature/felhippogryph/felhippogryphmount.m2 | 538457 | verified wt:hippogryph |
| S3 mount | Hippogryph remake mount | Legion | creature/hippogryph2/hippogryph2mount.m2 | 1121183 | verified wt:hippogryph |
| owls/bats/roc | not searched in this pass | - | - | - | unverified |

### 1.14 Hyenas (Family 27)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Baby hyena | TWW-era | creature/babyhyena/babyhyena.m2 | 5871774 | verified wt:hyena (fills a rev. 1 gap, but very late format) |
| S2 | Hyena remake | Legion | creature/hyena2/hyena2.m2 | 1519595 | verified wt:hyena |
| S3 mount | Hyena mount | Legion | creature/hyena2mount/hyena2mount.m2 | 1519594 | verified wt:hyena |

### 1.15 Tallstriders, kodos, basilisks, elekk, talbuk, ravager

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Baby hawkstrider (Golden/Red hawkstrider hatchling-style pet; pet link unverified) | pre-Cata (exp. from history: TBC) | creature/babyhawkstrider/babyhawkstrider.m2 | 122988 | verified wt:strider |
| S2/S3 | Tallstrider remake / primal tallstrider + mount | BfA / DF | creature/tallstrider2/tallstrider2.m2, creature/tallstriderprimal/tallstriderprimalmount.m2 | 2323979, 4037529 | verified wt:strider |
| S3 mount | Water strider mount | MoP | creature/waterstrider/waterstridermount.m2 | 593209 | verified wt:strider |
| S3 mount | Sunwalker kodo / elite | late WotLK-Cata | creature/kodobeast/ridingkotobeastsunwalker.m2, ...sunwalkerelite.m2 (note the "koto" spelling in the file name) | 326710, 329845 | verified wt:kodo |
| S2/S3 | Kodo remake + mount | Legion | creature/kodobeast2/kodobeast2.m2, creature/kodobeast2mount/kodobeast2mount.m2 | 1344501, 1345569 | verified wt:kodo |
| S1 | Kodo pet | DF | creature/kodopet/kodopet.m2 | 4204562 | verified wt:kodo |
| S2 | Outland basilisk | pre-Cata (TBC) | creature/basiliskoutland/basilisk_outland.m2 | 123035 | verified wt:basilisk |
| S3 mount | Basilisk mount | Legion | creature/basiliskmount/basiliskmount.m2 | 1305801 | verified wt:basilisk |
| new S1 | Baby elekk (Peanut / Elekk pet) | pre-Cata | creature/babyelekk/babyelekk.m2 | 122982 | verified wt:elekk |
| new S2 | Elekk | pre-Cata (TBC) | creature/elekk/elekk.m2 | 123729 | verified wt:elekk |
| new S3 | Riding elekk / great elite | pre-Cata (TBC) | creature/ridingelekk/ridingelekk.m2, ridingelekkelite.m2 | 125593, 125595 | verified wt:elekk |
| new S2/S3 | Talbuk / riding talbuk / epic | pre-Cata (TBC) | creature/talbuk/talbuk.m2, creature/ridingtalbuk/ridingtalbuk.m2, ridingtalbukepic.m2 | 126106, 125698, 125699 | verified wt:talbuk |
| new | Ravager (WoD remake + pet + mount) | WoD | creature/ravager2/ravager2pet.m2, ravager2.m2, ravager2mount.m2 | 986822, 986212, 986208 | verified wt:ravager. The TBC original was not found by this search: unverified |

### 1.16 Elementals, oozes

Not looked up in this pass. Leads: Pebble (Cata pet), tiny twister, "Disgusting Oozeling" (1.12
already). All unverified.

### 1.17 Dragonkin (whelpling lines 1-5, tiny dragons)

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 | Netherwhelp (cute Outland whelp) | pre-Cata (exp. from history: TBC CE pet) | creature/dragonwhelpoutlandcute/dragonwhelpoutlandcute.m2 | 123629 | path verified wt:whelp; the link to Netherwhelp is likely but unverified (rev. 1 `netherwhelp.m2` path was wrong) |
| S1/S2 | Outland whelp | pre-Cata (TBC) | creature/dragonwhelpoutland/dragonwhelpoutland.m2 | 123622 | verified wt:whelp |
| S1 | Onyxian whelp variant | late WotLK | creature/dragonwhelp/dragonwhelponyxia.m2 | 326545 | verified wt:whelp |
| S1 | Cataclysm whelp / Elementium whelp | Cata | creature/dragonwhelpcataclysm/dragonwhelpcataclysm.m2, creature/dragonwhelpelementium/dragonwhelpelementium.m2 | 462364, 466235 | verified wt:whelp |
| S1 | Proto-drake whelp | DF | creature/dragonwhelpproto/dragonwhelpproto.m2 | 4423740 | verified wt:whelp |
| S3 mount | Drake mount (Azure/Black/Bronze/Twilight Drake) | late WotLK | creature/drakemount/drakemount.m2 | 234564 | verified wt:drakemount |
| S3 mount | Proto-drake mount (Red/Blue/Plagued/Violet/Time-Lost) | late WotLK | creature/protodragon/mdprotodrakemount.m2 | 234782 | verified wt:protodrake (rev. 1 path `creature\protodrake\protodrakemount.m2` was wrong) |
| S3 mount | Armored twilight drake, fel drake | Cata | creature/drakemount/armoredtwilightdrake.m2, feldrakemount.m2 | 519559, 587735 | verified wt:drakemount |
| S3 mount | Red drake mount, scaled drake mount | Cata | creature/reddrakemount/reddrakemount.m2, creature/scaleddrakemount/scaleddrakemount.m2 | 528687, 520915 | verified wt:drakemount |
| S3 mount | Fel orc netherdrake (mounted) | pre-Cata (TBC) | creature/felorcnetherdrake/felorcnetherdrakemounted.m2 | 123908 | verified wt:drakemount. The player netherdrake mount path was not looked up: unverified |

### 1.18 Demons, undead, mechanicals, other babies

| Use | Model | Exp. | Path | FDID | Conf. / source |
|---|---|---|---|---|---|
| S1 undead | Baby lich (Lil' K.T.) | late WotLK | creature/babylich/babylich.m2 | 330503 | verified wt:baby |
| S1 mech | Mechanical chicken | Legion | creature/mechanicalchicken/mechanicalchicken.m2 | 1521038 | verified wt:chicken |
| S1 mech | Mechanical bear cub / tiger cub | MoP | creature/bearcub/mechanicalbearcub.m2, creature/tiger/tigercub_mechanical.m2 | 773082, 668542 | verified wt:cub |
| S1 misc | Baby moonkin (Moonkin Hatchling) | Cata | creature/babymoonkin/babymoonkin_ne.m2 | 467885 | verified wt:baby |
| S1 misc | Baby Ragnaros (Lil' Ragnaros) | Cata | creature/babyragnoros/babyragnoros.m2 | 467890 | verified wt:baby |
| demons / undead mounts | felguard, felstalker, geist, frost wyrm: not looked up | - | - | - | unverified |

### 1.19 Vanilla pets (165-214): later models per line

| Line | Candidates (see the family tables for FDIDs) | Conf. |
|---|---|---|
| Cats (tabbies) | saber cub (S1 alt), spectral tiger (S3) | verified |
| Parrots | no parrot model looked up | unverified |
| Snakes | no snake model looked up | unverified |
| Cockroach | none (Qiraji tank is already in 1.12) | - |
| Prairie dog | none | - |
| Chickens | Magic Rooster mount `creature/chickenmount/chickenmount.m2` (306852, late WotLK) | verified wt:chicken |
| Frogs | none found | - |
| Clockwork pets | mechanical chicken, mechanical bear/tiger cubs | verified |
| Zergling, Mini Diablo, Peddlefeet, Jubling | none | - |
| Panda cub | Jade panda cub `creature/panda/jadepandacub.m2` (309881). The Pandaren Monk pet was not looked up | verified wt:cub |
| Snapjaw turtles, Speedy | baby turtle, sea turtle, Riding Turtle, dragon turtle (1.8) | verified |
| Tiny dragons | 1.17 | verified |
| Baby murlocs | plated baby murloc `creature/murloc/babymurlocplated.m2` (252242); marine (Grunty) `creature/murloc/marinebabymurloc.m2` (306886) | verified wt:baby |
| Baby shark | baby octopus / seahorse only | verified (no shark model found) |
| Poley polar bear | Northrend bear mount (1.2) | verified |
| Mr. Wiggles | baby pig, giant boar mount (1.10) | verified |
| White tiger cub | spectral tiger cub and mounts, Siberian tiger mount (1.3) | verified |
| Hippogryph hatchling | hippogryph mount (1.13) | verified |

**Count:** about 105 model paths across 19 families, about 95 of them verified against the
listfile. About 10 leads remain unverified: TBC lynx, TBC ravager, Scorpling, netherdrake mount,
elementals, oozes, owls/bats, crabs/hydras, demons and the Pandaren Monk.

---

## 2. Technical requirements for a 1.12-based client (benilla)

### 2.1 M2 format versions (per wowdev.wiki/M2)

| Client | M2 version | Key differences vs 1.12 |
|---|---|---|
| Classic | 256-257 | Skins (views) inline in .m2; one global animation timeline with per-sequence ranges. |
| TBC | 260-263 | Same layout with minor header changes; close to 256. |
| WotLK | 264 | Skin profiles in separate `*00.skin`-`*03.skin`; per-sequence track arrays; some sequences in external `*####-##.anim`. |
| Cata | 265-272 | WotLK-like plus extra fields (texture combiners, particles). |
| MoP / WoD | 272 | Same base layout. Files move to CASC from WoD (6.0) and are named by FileDataID. |
| Legion+ | 272-274 | Chunked file: an `MD21` chunk wraps the old data (from 7.0.1.20740), plus `SFID`/`AFID`/`TXID`/`SKID`... side chunks. Skeleton may live in `.skel`. |

Correction from revision 1: the chunked MD21 format is **Legion**, not WoD.

The candidates above fall into tiers:
- pre-Cata (FDID about 122k-330k): needs a 260-264 loader or converter.
- Cata/MoP: 265-272, still non-chunked, but extracted from CASC.
- Legion and later (the BfA baby raptor, the spider mount and every "2" remake): needs MD21 and
  possibly .skel.

### 2.2 Animation timeline

- 256-263: each track has one key list for all sequences; a sequence is a time range in it.
- 264+: per-sequence key sub-arrays, timestamps local to the sequence; some are external `.anim`
  files (for example `babyraptor0061-00.anim`).
- Down-porting means concatenating per-sequence keys with offsets and rebuilding the sequence
  start/end times.

### 2.3 Textures

All are BLP2. 1.12 handles palettized and DXT1/3/5. Later files are mostly DXT1/DXT5, sometimes raw
BGRA. Later shader/combiner ids need mapping to vanilla blend modes. Retail-era files also add
`_lod01-03.skin` LOD profiles (seen for `babyraptor`); only `00.skin` is needed.

### 2.4 Loader vs converter

- **A:** extend the benilla loader to 260-264 (then 265-272, then MD21) and normalise into internal
  structures.
- **B:** offline down-port to 256. Community converters exist, e.g. "Multiconverter" (model-changing
  / modcraft communities). Their licence and maintenance status were not verified.

Either way, bounding and collision values and the mount attachment must come from the new model.

### 2.5 DBC and server rows

- Client: `CreatureModelData.dbc` (path, bounding, mount height) and `CreatureDisplayInfo.dbc`
  (model id, skin textures 1-3, scale, sound), with optional `CreatureSoundData`.
- Server: `creature_model_info` plus the companion `stageN_display`.
- Reserve a high id range.

### 2.6 Distribution

A patch MPQ with the models, skins, anims, BLPs and DBCs. The server needs only the DBC-derived values.

---

## 3. Legal / distribution reality (factual, not legal advice)

- None of these files are in a 1.12 install. They are Blizzard-copyrighted assets from later clients.
- Shipping them redistributes Blizzard assets the player may not own. Blizzard has a public record of
  takedowns against private-server client and asset repacks.
- The alternative keeps the project's current posture, where the player supplies the data. An opt-in
  installer step reads the player's own later client and extracts only the listed files locally:
  - a 3.3.5 MPQ install, using an MPQ library;
  - a modern install, using CascLib with these FDIDs.

  The step then converts the files and builds a local patch MPQ. The project ships only code,
  file lists and its own DBC row data.
- The long-term own-IP goal makes all of these a stopgap.

---

## 4. Recommendation

1. **Stay 1.12-only for now.** Every line already has three distinct 1.12 looks.
2. **Later, add an optional local-extraction pipeline** with a fallback to the 1.12 display. Target a
   WotLK 3.3.5 client first: MPQ-based, M2 264, and it covers every pre-Cata row.
3. **Loader order:** 260-263, then 264 (.skin/.anim), then Cata/MoP (272). Skip MD21/.skel.
4. **Highest-value short list** (all pre-Cata, all verified paths):
   1. Proto-drake mount `creature/protodragon/mdprotodrakemount.m2` and drake mount
      `creature/drakemount/drakemount.m2`: whelpling lines, stage 3.
   2. Raptor pet `creature/raptor/raptorpet.m2`: raptor stage 1 (confirm it is the hatchling model).
   3. Northrend bear mount (armored) `creature/northrendbearmount/northrendbearmountarmored.m2`:
      bears and Poley, stage 3. Rideable bears.
   4. Riding Turtle `creature/ridingturtle/ridingturtle.m2` plus sea turtle: the turtle line, Speedy
      and Snapjaws.
   5. Baby crocolisk `creature/babycrocodile/babycrocodile.m2`: crocolisk stage 1 (check whether it
      is already in 1.12).
   6. Outland whelp / cute whelp (`dragonwhelpoutlandcute`): an extra whelp stage 1.
   7. Spectral tiger cub and mounts: white tiger cub and cat lines.
   8. Magic Rooster `creature/chickenmount/chickenmount.m2`: chicken line, stage 3.
   9. Riding phoenix + phoenix pet + phoenix: a ready-made 3-stage set for a bird line.
   10. Elekk (baby / adult / riding) and talbuk (adult / riding): complete new lines.

   MoP cloud serpents (pet / adult / mount) are the best wind serpent upgrade if CASC extraction is
   accepted.

---

## Changes from revision 1

Path corrections (the revision 1 paths do not exist):
- Proto-drake mount: now `creature/protodragon/mdprotodrakemount.m2`.
- Netherwhelp: now most likely `dragonwhelpoutlandcute`.
- Cloud serpent: now `pandarenserpent*`.
- Scorpion mount: now `hordescorpionmount`, which is Cata, not MoP.
- Armored raptor: BfA, not Cata.
- Baby raptor `creature/babyraptor`: BfA, not WotLK. The WotLK hatchlings are most likely `raptorpet.m2`.
- TBC lynx and TBC ravager: not found under the old names.

Other changes:
- Corrected the M2 table: MD21 is Legion.
- Added about 40 verified rows: baby elekk, babyhawkstrider, babycrocodile, frostwolf pup, giant boar
  mounts, dragon turtles, phoenix set, hyena remake set, kodo remakes, basilisk mount, baby lich,
  and others.
- Gaps filled (all post-Cata): hyena (babyhyena), gorilla (gorillapet), wind serpent baby
  (pandarenserpentpet).

---

## Sources

- https://wago.tools/api/files?search=TERM, used with each term cited above: babyraptor, raptor,
  protodrake, drakemount, whelp, netherwhelp, turtle, bearmount, cub, lynx, tiger, sabre, wolf,
  spider, boar, kodo, crocodile, baby, talbuk, phoenix, elekk, serpent, cloudserpent, chicken,
  scorpion, hippogryph, ravager, basilisk, gorilla, hyena, strider
- https://wowdev.wiki/M2 (version table, .skin/.anim from Wrath, MD21 and .skel from Legion)
- https://warcraft.wiki.gg/wiki/Riding_Turtle (TCG origin)
- https://warcraft.wiki.gg/wiki/Time-Lost_Proto-Drake
- https://wowpedia.fandom.com/wiki/Reins_of_the_Blue_Proto-Drake
- https://github.com/wowdev/wow-listfile (community listfile, same data)
- https://github.com/ladislav-zezula/CascLib (CASC extraction)
