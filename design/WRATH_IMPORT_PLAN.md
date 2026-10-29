# Wrath (3.3.5a) import plan (PRIVATE)

Private planning doc. What WOMW could take from Wrath of the Lich King, how hard each piece is, and
in what order. It also merges this with the community-client import (see
`design/COMMUNITY_IMPORT_INVENTORY.md`, which is private and excluded from the public export; this
doc refers to it only as "the community client" and "its repack"). Keep this file out of the public
tree too: add it to `tools/public-exclude.txt` before the next export.

Sources (inspected read-only, Oct 2026; nothing in them was run):
- **WotLK client**: `E:\Local Games\world of warcraft 3.3.5a hd\Data`. Stock 3.3.5a archives
  (`common`, `common-2`, `expansion`, `lichking`, `patch`, `patch-2`, `patch-3`, locale chains) plus
  a stack of HD/mod patches (`patch-6/a/e/f/k/s/w/C/O`, several `.disabled`). Treat the HD patches as
  third-party mods: same caution as the community client's HD pack.
- **AzerothCore repack**: `E:\Local Games\Azerothcore WoTLK Repack (Playerbots)`. Has `data\` with
  `dbc` (199 MB), `maps` (2.9 GB), `mmaps` (3.1 GB), `vmaps`, `Cameras`; a `mysql\data` dir with
  `acore_world` (334 tables), plus auth/characters/playerbots schemas (not touched); `source\` holds
  only SQL folders and two module folders (`mod-playerbots` is empty, `mod-classless` has source).
- **Already done** (branch `wotlk-assets`, `C:\Projects\aw-wt-wotlk-assets`, `design/WOTLK_ASSETS.md`):
  an offline M2 264 -> 256 converter, 18 WotLK creature displays (ids **40001-40018**, models 4001+)
  in `private-assets/womw-patch/patch-9.MPQ`, companion looks (`33_wotlk_looks.sql`), and Dalaran
  (first built as map 800; **now being reworked to sit in Alterac Mountains on the Eastern Kingdoms
  map (0), replacing the classic Dalaran bubble/crater**). This plan treats those as taken and builds on them.

Note on the world DB: the repack runs **MySQL 8** (InnoDB with the MySQL 8 data dictionary,
`#innodb_redo`), not MariaDB. Our `tools/mariadb` cannot open those files, so the DB was **not**
copied or opened; table names were only listed. To read row data later, either (a) start a
separate MySQL 8 build of our own on a copy of `acore_world` only, or (b) use AzerothCore's public
`data/sql/base/db_world` SQL from their GitHub (same content, plain SQL; cleanest). Numbers below
marked "(known)" are from the public AzerothCore/3.3.5 data, not counted here.

Legal posture is unchanged from `COMPANION_MODEL_RESEARCH.md` section 3: these are Blizzard assets
from a later client. Keep them in `private-assets/`, or ship an installer step that extracts from
the player's own 3.3.5 client.

Effort: **S** = days, **M** = 1-3 weeks, **L** = a month+, **XL** = a project of its own.
Risk: what breaks or what we might regret.

---

## 1. Creature models

The converter exists, so each new creature is mostly a table row plus an in-game look.
Effort per creature after the pipeline: **S** (minutes to an hour, plus checking size/anim).

| Priority | Family / set (3.3.5 names) | Use in WOMW | Notes |
|---|---|---|---|
| **P1** | Baby/young forms: hatchlings (raptor, hawkstrider, proto-drake whelp, wind rider cub), worg pups, mammoth calf, rhino calf, gryphon/hippogryph hatchlings, nether-ray fry, sporebat, mana wyrmling, tiny snowman, baby murlocs | Stage-1 companion forms | Gives every line a real baby instead of a scaled adult. Several already done (40001+). |
| **P1** | New families: proto-drakes, mammoths, rhinos (Wooly), worgs, nerubians, vrykul, gorlocs, wolvars, frost wyrms, geists/ghouls/abominations (Scourge), jormungar, magnataur, iron dwarves/vrykul, taunka, tuskarr | Companion lines, enemies, Wanderer looks | Enemies are a cheap win: new silhouettes for zones without new terrain. |
| **P2** | Outland families (in the 3.3.5 client too): ravagers, talbuks, elekks, nether rays, warp stalkers, fel reavers, dragonhawks, clefthoof | Companions, enemies | All 264 format, same pipeline. |
| **P2** | Armored/mount versions: armored bears, mammoths, proto-drakes, frostsabers, talbuks | Mounts, top companion stage | Proto-drakes are "flying" models but play ground anims fine. |
| **P3** | Bosses and big set pieces (Lich King, Sindragosa, Malygos) | Raid/event bosses | Huge files, many anims; careful scale. |

Wanderers' looks: the 3.3.5 character models are the same generation as 1.12 (same skeletons and
skin layout), so **Wrath player races are not an upgrade** except Draenei/Blood Elf (TBC races) and
Death Knight gear. Wanderers gain most from Wrath **gear displays** (section 4) and Death Knight
style runeblades/eyes, not new bodies.

## 2. Visual upgrades

| Item | Value | Effort | Risk |
|---|---|---|---|
| **Spell visuals / particles** (SpellVisual, SpellVisualKit, effect M2s like frost/shadow novas, missiles, auras) | High: our abilities and killstreaks want punchier effects | M (models via converter; SpellVisual rows remapped to 1.12 layout) | 3.3.5 particle features (more ramp points, new blend modes) are down-sampled by the converter; may look flatter. The `spell-visuals` branch is the place for this. |
| **Icons** (`Interface\Icons`, about 4,000 more than 1.12 (known)) | High: icons for our abilities, kit options, companions, guns, items | S (BLP copy + `SpellIcon`/`ItemDisplayInfo` icon names) | None technically; easy first win. |
| **Weather** (snow/blizzard effects, Northrend weather) | Medium | S-M | 1.12 weather system is simpler; usable as textures. |
| **Sky / lighting** (`Light*.dbc` 3.3.5 has more params and skyboxes) | Medium, mainly for imported zones | M | DBC layouts differ; port per-zone, not wholesale. |
| **Water** (3.3.5 water textures, MH2O) | Medium | M (needs MH2O reader; see section 7) | Needed anyway for Northrend terrain. |
| **UI art** (frames, achievement UI, LFG, calendar textures) | Low-medium; our HUD is our own | S | Mostly reference art. |
| **HD textures** | See section 8: the community client's HD pack is the better source for 1.12 content; the WotLK install's HD patches are third-party mods for 3.3.5 content | M | Memory, crashes; opt-in only. |

## 3. Zones and cities

| Priority | Content | How | Effort | Risk |
|---|---|---|---|---|
| **In progress** | **Dalaran** (`ND_Dalaran.wmo`) placed in Alterac Mountains on map 0, replacing the classic crater | WMO placement (MODF) in the Alterac ADT tiles + terrain edit under it; converted doodads | (wotlk-assets) | Edits stock Azeroth tiles: must be merged with the community client's Azeroth tile edits if those ever land (check whether they touch the Alterac tiles). Needs map-0 vmaps re-extracted. Still needs NPCs, services, AreaTable/WMOAreaTable names, sounds. |
| **P1** | Other WMO-only places: Shattrath (TBC), dungeons whose maps are WMO-only (Utgarde Keep, Nexus, Halls of Stone/Lightning, Violet Hold, Culling of Stratholme, Ahn'kahet, Drak'Tharon, Gundrak are WMOs placed on maps with or without ADT) | Same route as Dalaran | S-M each | Some instance maps do have ADT tiles; check each WDT for MAIN flags. |
| **P2** | Single small Northrend areas as instanced "maps": e.g. Wintergrasp (killstreak/vehicle battleground showcase), Strand of the Ancients, Isle of Conquest | New map ids; needs ADT 3.3.5 support | M-L | Needs MH2O, MCCV, big alpha, plus vmaps. Battleground fit is excellent for MW2 modes. |
| **P3** | **Northrend as a new continent** (map 571) | New Map/WorldMapContinent rows; full ADT 3.3.5 support; server maps/vmaps/mmaps; creatures/quests from the AzerothCore DB | XL | Size (thousands of spawns, quests built for 70-80 and vehicles/phasing). Alternative: offer Northrend **zones** as level-scaled war zones without the quest chains. |
| **P3** | Outland (map 530) | Same as Northrend | XL | Lower value than Northrend for our tone. |

Server collision: the repack's `data\maps`, `vmaps`, `mmaps` are **AzerothCore 3.3.5 formats**, not
vmangos 1.12 ones (different headers/versions). Do **not** copy them. Regenerate with **our**
vmangos extractors over the converted client data (as done for Dalaran). Our map extractor needs the
MH2O/MCCV/big-alpha changes too for any ADT-based Northrend map.

## 4. Items, gear, mounts, vehicles, pets

| Priority | Content | Use | Effort | Risk |
|---|---|---|---|---|
| **P1** | **Guns, bows, crossbows** (Wrath has several dozen new gun models (known), incl. engineering rifles and dwarven/goblin designs) | Our arsenal, weapon pickups, lore names (W7) | S each (item M2s via converter + ItemDisplayInfo rows) | Item M2s are simple; low risk. Best value per hour in the whole plan. |
| **P1** | Item icons for the above | | S | |
| **P2** | Armor sets (Tier 7-10, PvP seasons, Death Knight sets) | Kits, prestige rewards, Wanderers | M (ItemDisplayInfo + character textures; 3.3.5 armor textures use the same component layout as 1.12, mostly) | Some shoulders/helms are 264 M2s; textures may need resizing for 1.12 regions. |
| **P2** | **Ground mounts** (mammoths, armored bears, Wooly rhino, mechano-hog/chopper, talbuks, elekks) | Mounts; the chopper fits our modern tone | S each | Multi-seat mounts need vehicle code (below). |
| **P3** | **Flying mounts** | 1.12 has no flying: no fly movement flags, no client flight control, maps were never built for it (open sky, missing tiles beyond walls, invisible barriers). Options: (a) ride them as ground mounts (works today); (b) brief scripted flight like W9's "brief flight" (taxi-like paths or a short glide); (c) real flying: client movement + server anticheat/movement support + map edges audit. Recommend (a)+(b); (c) is **XL**. | | |
| **P2** | **Vehicles** (siege engines, demolishers, choppers, turrets, gunships) | Killstreaks: controllable turrets, a demolisher, a gunship run | L: vmangos has no vehicle system (no `Vehicle.dbc`, `VehicleSeat.dbc`, seat transport, vehicle spells). We would build a slim version: one driver seat + a fixed ability bar, using our fusion channel | Big core work; start with "turret" (stationary, one seat) before moving vehicles. |
| **P1** | **Vanity pets** (about 150 in 3.3.5 (known): whelplings, mini Lich King-era pets, mechanical squirrels, pandaren monk, Tirisfal pumpkin, etc.) | New companion lines and baby forms | S each | Many are tiny-scale versions; fit the catalog's size rules. |

## 5. Gameplay data from the AzerothCore world DB

Tables seen in `acore_world` (names only).

| Area | Tables | Value for WOMW | Effort |
|---|---|---|---|
| **Playerbots** | `acore_playerbots` schema (not opened), `playerbots_rpg_races` in world; the module source folder is **empty** in this repack | Wanderers: ideas only (bots that roam, quest, group, talk). Code is GPL and written for AzerothCore's AI; we would not port it. Wanderers are already designed our way (`WANDERERS.md`). Read the public mod-playerbots repo for behaviour ideas (travel, "RPG" mode, chatter). | Reference only |
| **Achievements** | `achievement_dbc`, `achievement_criteria_dbc`, `achievement_category_dbc`, `achievement_criteria_data`, `achievement_reward(_locale)` + client `Achievement*.dbc` | We already have an achievements fusion channel (3). Wrath's catalogue (about 1,600 achievements (known)) is a good list of criteria *types* to copy (kill X, explore, reputation, collections). Take the design and criteria types, not the rows. | M |
| **Battle pets** | none (they are MoP) | Already our own design (`COMPANION_BATTLES.md`) | - |
| **Professions** | Inscription and Jewelcrafting spells/items (in `item_template`, skill lines, `spell_*`) | Inscription -> kit "glyph" modifiers; Jewelcrafting -> gem sockets as gun attachments. Both need the 3.3.5 socket/glyph systems that vmangos lacks. | L each |
| **Quests/creatures for imported zones** | `quest_template`, `creature_template`, `creature`, `gameobject`, `smart_scripts`, `waypoint_data`, `conditions`, loot tables | Only for zones we import. AzerothCore schema differs from vmangos (columns, SmartAI vs our scripts). Write a converter for the subset per zone. SmartAI scripts don't run on vmangos: bosses need our scripting. | M per zone |
| **Calendar / events** | `game_event*` (14 tables), holidays | Wrath holidays (Brewfest, Hallow's End, Pilgrim's Bounty, Children's Week, Noblegarden) and their props. vmangos already has `game_event`; port selected events. | S-M |
| **Dungeon finder** | `lfg_dungeon_template`, `lfg_dungeon_rewards`, `lfgdungeons_dbc` | Idea: a queue for our modes/dungeons. Build our own over fusion; use their reward tables as a pattern. | M |
| **Level rewards mail** | `mail_level_reward` | Trivial feature, nice for onboarding | S |
| **Classless** | `mod-classless` source | Reference for our class framework (W6) | Reference only |

## 6. Sounds and music

| Content | Value | Effort |
|---|---|---|
| Creature sounds for every imported creature (`CreatureSoundData`, `SoundEntries`) | Needed so new creatures aren't silent (today a 1.12 donor is used) | S (rows + WAV/OGG copy; 1.12 plays both? check: 1.12 plays WAV and MP3; 3.3.5 uses OGG/WAV - **OGG needs a decode in benilla**, our client can add it) |
| Weapon/impact sounds (guns, siege, explosions) | High for our tone | S |
| Zone music (Northrend, Dalaran, Icecrown) and ambience | Medium; Dalaran needs it now | S |
| Voice (NPC greetings, Lich King lines) | Low | S |

## 7. Technical work

**Client (benilla):**
- **M2 v264**: done as an offline converter to 256 (`client/src/wotlk/m2conv`). Keep that rule:
  benilla reads 1.12 data only. Extend the converter as new features show up (more particle ramps,
  ribbon details, extra blend modes, LOD skins).
- **ADT/WDT 3.3.5**: needed only for terrain maps (not Dalaran). Work: MH2O liquid (3.3.5 has no
  MCLQ in most tiles), MCCV vertex colours, big-alpha 64x64 8-bit layers, MPHD flags,
  `_tex`/`_obj` splits do **not** exist in 3.3.5 (those are Cata), so ADTs are still monolithic.
  Either teach `benilla-adt` these chunks (the community client needs MH2O/MCCV too) or convert
  ADTs offline to 1.12 (MH2O -> MCLQ is lossy but simple). Recommend: **reader support**, because
  both sources need it. **M**.
- **WMO**: 3.3.5 WMOs are version 17, same as 1.12; Dalaran proved it loads. Some later chunks
  (MOVV/MOVB visibility, MCVP convex volumes) appear; ignore them. **S**.
- **DBC layouts**: 3.3.5 rows are wider (e.g. `CreatureDisplayInfo` 16 fields vs 1.12's 14,
  `ItemDisplayInfo` and `Spell` very different, `Spell.dbc` 234 fields vs 173). Port **per row via
  explicit field maps** (as `wotlk::import::LOOKS` does), never wholesale.
- **BLP**: same BLP2; fine. **OGG** sound decode: add. **Texture budget**: some 3.3.5 textures are
  512-1024; fine at Wrath scale.

**Server (vmangos 1.12):**
- No vehicles, no flying, no achievements tables in Wrath form, no glyph/socket systems, no LFG,
  no phasing. Implement only what we choose, on our own fusion channels.
- Collision: regenerate `maps/vmaps/mmaps` with our extractors; never use the repack's.
- DB rows: AzerothCore schema -> vmangos schema converters, per imported zone.
- Map rows: `map_template`, area triggers, teleports.

## 8. Combined plan: Wrath vs the community client

### Which source is better, per area

| Area | Better source | Why |
|---|---|---|
| HD textures for 1.12 content (characters, zones we already have) | **Community client** HD pack | Drop-in by name over 1.12 files; WotLK HD mods target 3.3.5 files and races. Opt-in either way. |
| New creature models / families / baby forms | **Wrath** | Far larger catalogue; converter done. The community client's creatures are mostly 1.12-era reuses plus some custom ones: take those only when a zone needs them. |
| New zones on Azeroth/Kalimdor (Gilneas, Tel'Abim, Hyjal, ...) | **Community client** | They are already 1.12-format tiles. But they overwrite 675 tiles: big merge, do later. |
| Small instanced zones / dungeons | **Both**: community client's 800-822 maps (1.12 format, no conversion) first; Wrath WMO dungeons next | |
| Cities | **Wrath** (Dalaran, Shattrath) | Community client has Gilneas City as a dungeon. |
| Guns, ranged weapons, armor sets | **Wrath** for volume and modern look; community client has 15,647 extra ItemDisplayInfo rows, worth a browse for vanilla-style gear | |
| Spells / spell visuals | **Wrath** for visuals; **neither** for Spell.dbc rows wholesale (both overwrite rows our kits depend on) | Cherry-pick visuals only. |
| Mounts, vehicles, vanity pets | **Wrath** | |
| Quests/NPCs for imported zones | Whichever source the zone comes from | |
| Sounds/music | **Wrath** | |
| Gameplay systems (achievements, events, LFG) | **Wrath** (design reference) | |

### Duplicates to avoid
- A creature display that exists in both: prefer the 1.12-native one (community client) unless the
  Wrath model is clearly better; never ship both under two ids.
- Gilneas: community client open-world Gilneas + Gilneas City vs any later Gilneas: pick the
  community one.
- Karazhan: community client adds lower Karazhan/Tower (532, 814). Don't import Wrath-era versions.
- Icons: Wrath's icon set is a superset; copy once into one patch, not from both.
- HD: never stack the community HD pack and a WotLK HD mod on the same files.
- **MPQ name clash**: our WotLK layer is `patch-9.MPQ`, and the community client also has a
  `patch-9`. Our merged package must be one ordered set we own (see below), not their names.

### Map ids (the earlier map-800 collision is gone)
Dalaran was first built as map 800, which collided with the community client's **Karazhan Crypt
(800)**. The owner has since moved Dalaran into **Alterac Mountains on map 0**, so map 800 is no
longer ours and the community client's maps (26-28, 532, 800-822; plus 804, 806, 809 on its
server) can keep their own ids. Remaining points:

- Make sure the reworked Dalaran no longer writes a `Map.dbc` row 800 or a `map_template` 800
  (and that the old map-800 vmaps are dropped), so the community Karazhan Crypt can use it.
- **New overlap instead: Alterac tiles on map 0.** Dalaran now edits stock Azeroth ADTs. The
  community client overrides 355 Azeroth tiles; before a merge, list which of them cover Alterac
  (around the old Dalaran crater) and merge by hand. Our tile edits win.
- Reserve **900-949 for Wrath maps** (further cities/dungeons/battlegrounds, e.g. a Wintergrasp
  arena at 901), **571** (Northrend) and **530** (Outland) at their stock ids if whole continents
  ever come, and **950-999 for our own custom maps** (housing districts, W9 world building).

### Id-range plan (all three sources)
Rule: our custom content keeps its ranges; imports get blocks that neither collides with ours nor
with the other import. Existing ranges from `OVERNIGHT-2026-10-07.md`, `WANDERERS.md` and the
inventory.

| Kind | Ours (custom) | Wrath imports | Community-client imports |
|---|---|---|---|
| Map | 950-999 (Dalaran lives on map 0) | 900-949 (+ 530, 571 if whole continents) | 26-28, 532, 800-822 (as is) |
| CreatureDisplayInfo | (none yet beyond imports; reserve 39000-39999) | **40001-44999** (already 40001-40018) | as is (up to 21697); new ones renumber into 45000-49999 if they clash |
| CreatureModelData | reserve 3900-3999 | **4001-4999** | as is (up to 90085: check it doesn't hit 4001-4999; else renumber) |
| ItemDisplayInfo | reserve 70000-74999 | **75000-84999** | as is (32878-69173) |
| GameObjectDisplayInfo | reserve 34000-34999 | **35000-39999** | as is (6433-33281) |
| SpellVisual / Kit | reserve 9000-9499 | **9500-9999** (kits likewise) | as is (6923-8028) |
| SpellIcon | - | **5000-6999** | - |
| Spell ids | 61000-62999 (W2/W5-W8) | **63000-64999** (visual-only carrier spells) | do not import wholesale; cherry-picked rows renumber into 65000-65999 |
| creature_template | 90001-90999 companions, 92002-92699 W-packages, 93700-93798 Wanderers | **100000-109999** | 110000-129999 (their 90000-92999 clash with ours, so renumber all of them) |
| item_template | 90001-90999, 93001-93399, 930xx-941xx | **130000-139999** | 140000-149999 (their 91000-94999 clash) |
| quest_template | 95000-97999 free; keep ours there | **150000-159999** (stock Wrath quest ids 1-26000 would overlap 1.12 ids: always renumber) | as is in 40000-89999 only if no clash; else 160000-169999 |
| gameobject_template | our construction objects (check) | **2100000-2199999** | 2000000+ range they already use; keep |
| AreaTable | - | **6000-6999** (Dalaran's areas etc.) | as is (2038-5735) |
| WorldMapArea / Continent | - | 800-899 / continent 4+ | 500-705 / 3 |
| SQL files | `server/sql/custom/azeroth_warfare/` numbered as now | 33_, 34_ taken; continue 35_-49_ | 50_-69_ |
| MPQ patch letters | — | one ordered set: `patch-9` WotLK layer stays; zones from the community client go into **our own** repacked `patch-A` (content) and optional `patch-H` (HD, opt-in); never ship their archive names | |

Before each import, a script diffs the new rows against every range above (and against our current
DBC overrides) and fails on any overlap.

### Merged phased import order

**Phase 1 (now, low risk, high value):**
1. Finish Dalaran in Alterac (wotlk-assets): NPCs, AreaTable/WMOAreaTable, music; drop the old map-800 rows.
2. Wrath **icons** for our abilities, items, companions.
3. Wrath **guns/ranged weapons** + item icons into the arsenal.
4. More Wrath **baby forms and vanity pets** as companion lines.
5. Wrath **creature sounds** for the 18 imported displays (needs OGG decode or WAV).

**Phase 2 (client format work that serves both sources):**
6. `benilla-adt`/`benilla-wdt`: MH2O, MCCV, big alpha, MPHD flags.
7. First community-client dungeon (Hateforge Quarry, map 808, per the inventory) end to end.
8. Wrath **spell visuals** for our kits and killstreaks (on the `spell-visuals` work).
9. Wrath **ground mounts** and **enemy families** for existing zones.
10. Wrath **armor sets** for kits/prestige/Wanderers.

**Phase 3 (systems):**
11. Slim **vehicles**: turret first, then a demolisher; killstreak hooks.
12. Achievements expansion using Wrath criteria types; holiday events.
13. A Wrath battleground map (Wintergrasp or Strand) as a MW2 mode arena, map 901+.
14. More community-client dungeons (800-822 set).

**Phase 4 (big merges, decide first):**
15. Community-client open world on Azeroth/Kalimdor (675 tiles).
16. Northrend zones (571) as war zones or a full continent; Outland later or never.
17. Real flying (only if we decide to).
18. Optional HD pack (community client) as an opt-in download.

## 9. Decide (owner)

1. **Map ids**: keep the community client's 800-822 as they are and reserve 900-949 for Wrath maps? (Recommended.) And: if the community open world lands, whose Alterac tiles win (ours, with Dalaran, recommended)?
2. **Distribution**: ship Wrath assets in our private patch, or extract from the player's own 3.3.5
   client at install time?
3. **Flying**: ride flying mounts as ground mounts + brief scripted flight only (recommended), or
   fund real flying?
4. **Vehicles**: build a slim vehicle system for killstreaks (turret first)? Yes/no.
5. **Northrend**: none / a few zones as instanced war zones / the whole continent?
6. **Community open world** (Gilneas, Tel'Abim, Hyjal...): merge it into Azeroth/Kalimdor, given it
   replaces 675 tiles and clashes with any terrain work of ours?
7. **HD**: offer an opt-in HD pack at all? If so, the community client's (for 1.12 content).
8. **Professions**: add Inscription (as kit glyphs) and/or Jewelcrafting (as gun attachments)?
9. **Achievements**: expand ours with Wrath-style criteria? LFG-style queue for our modes?
10. **Reading the AzerothCore DB**: use AzerothCore's public SQL (recommended) rather than spinning
    up the repack's MySQL 8 data.
