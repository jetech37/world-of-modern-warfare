# Community content import: inventory (PRIVATE)

Private working notes. Two downloaded folders under `E:\Local Games\` are used with the developers'
permission; they asked not to be credited publicly. Call them **the community client** and **the
repack** here, and never name their authors in a public file. This file must not go into the public
tree. Before the next export, add it to `tools/public-exclude.txt`.

Everything below came from read-only inspection (Oct 2026). Nothing from either folder was run.
MPQs were listed with our own `benilla-extract` / `mpqx` (release builds). DBC headers were read with
small `python -I` scripts kept in the session scratchpad. The repack's MariaDB data directory was
**copied** to scratch and opened with **our** `tools/mariadb` (11.8.9) on 127.0.0.1:3399 with
grant tables skipped. Only `their_world` and the system schemas were copied; the character, logon and
logs schemas were not, and their contents were not read. The server was shut down afterwards.

Paths used below:
- `CLIENT` = `E:\Local Games\<community client>\<same name>\Game`
- `REPACK` = `E:\Local Games\<repack folder dated 20_09_2026>`
- `STOCK` = `dist\AzerothWarfare\game\WoW\Data` (our 1.12.1 build 5875 data)

---

## 1. The community client

### Build and layout
- A pre-configured **custom-vanilla client** (its own version and build number), on a modified 1.12 `WoW.exe`
  (4.9 MB). It ships with many injected DLL mods (DXVK `d3d9.dll`, an extended-API hook, nampower,
  UnitXP, VanillaFixes, translation/CN fixes and others) plus about 20 addons. None of those matter
  to us: we only want data.
- `Data\` has the stock 1.12 archives, unchanged or close to it (`base`, `backup`, `dbc`, `fonts`,
  `interface`, `misc`, `model`, `patch`, `patch-2`, `sound`, `speech`, `terrain`, `texture`, `wmo`).
  Then come custom patches `patch-3` … `patch-9` (the upstream custom content) and `patch-A` …
  `patch-Y` (an HD texture/model pack plus small fixes). Loose files: `Data\Interface\`
  (GlueXML, Cinematics) and `GlueStrings.lua`.
- Load order: our `benilla_formats::Chain` already mounts `patch-?.MPQ` by case-folded name,
  exactly as the reference client does, so `patch-9` < `patch-A` < … < `patch-Y`. Opening `CLIENT\Data`
  as a chain worked (DBCs below were read that way).
- **patch-4, patch-8 and patch-9 have no `(listfile)`.** The others list normally. For those three,
  names were found by hashing candidate names against the hash table: every name from every other
  listfile, plus every `WDT/WDL` and all 64×64 `ADT` names for every `Map.dbc` directory. **ADT/WDT
  counts are therefore complete.** Counts of other file types there are lower bounds. Our loaders
  read by name, so a missing listfile only matters for listing.

### MPQ list (sizes on disk; counts are files by type)

| Archive | Size | Contents |
|---|---:|---|
| base / backup / fonts / misc / speech | small | stock |
| dbc.MPQ | 4 MB | 140 DBC (stock) |
| interface.MPQ | 70 MB | 4,907 interface (stock) |
| model.MPQ | 191 MB | 7,838 M2 (stock) |
| texture.MPQ | 665 MB | 42,045 BLP (stock) |
| wmo.MPQ | 364 MB | 5,858 WMO (stock) |
| terrain.MPQ | 1.10 GB | 2,352 ADT, 37 WDT (stock) |
| sound.MPQ | 939 MB | 3,241 sound (stock is 710 MB, so sounds were added) |
| patch.MPQ / patch-2.MPQ | 1.9 GB / 9 MB | stock 1.12.1 patches |
| **patch-3** | 2.06 GB | 16,470 files: 7,333 BLP, 2,279 M2, 1,953 WMO, 518 ADT, 13 WDT, 72 DBC, 2,280 interface, 2,001 sound. New maps: collin, gnomeshrink, grove, hateforge, karazhancrypt, moomoo, pvpzone05, scarletcitadel, sunnyglade, winterveil; tiles on azeroth/kalimdor |
| **patch-4** (no listfile) | 385 MB | 4,616 blocks; resolved 2,009 BLP, 136 WMO, 102 M2, 193 ADT, 3 WDT, 41 DBC, 434 interface. Maps: giln (17 ADT), grove, karazahn, sunnyglade, emeraldsanctum, azeroth (79), kalimdor (55) |
| **patch-5** | 261 MB | 2,183 files: 1,529 BLP, 181 WMO, 87 M2, 60 ADT, 32 DBC. azeroth/kalimdor tiles |
| **patch-6** | 451 MB | 3,903 files: 2,069 BLP, 600 WMO, 492 M2, 43 ADT, 52 DBC. Maps: highelfpvp, karazahn40, monasteryinstances, azeroth |
| **patch-7** | 175 MB | 1,623 files: 478 BLP, 213 M2, 210 WMO, 72 DBC, 537 interface. wailingcaverns, azeroth |
| **patch-8** (no listfile) | 485 MB | 4,978 blocks; resolved 946 BLP, 285 WMO, 54 M2, 222 ADT, 59 DBC. Maps: balor, dragonmaw, highelfpvp, razorfendowns, tanarisinstance, azeroth (132), kalimdor |
| **patch-9** (no listfile) | 507 MB | 6,282 blocks; resolved 2,883 interface, 478 BLP, 144 WMO, 48 M2, 196 ADT, 64 DBC. Maps: netherstormbg (36 ADT), frostmanecaverns, timbermaw, moocanyon, karazahn, blackwinglair, kalimdor (84). **Final copy of most DBCs** (Map, AreaTable, WorldMapArea, Spell, …) |
| patch-A (HD chars/NPCs) | 1.75 GB | 16,125 BLP, 23 M2 (character models, 4–26 MB each), 7 DBC |
| patch-B (HD buildings) | 1.61 GB | 4,486 BLP |
| patch-C (HD creatures) | 2.08 GB | 4,382 BLP, 326 M2, 218 sound, 53 `.anim`, 4 `.skin` |
| patch-D (HD doodads) | 1.65 GB | 6,169 BLP, 917 M2, 171 WMO |
| patch-E (HD environment) | 720 MB | 1,683 BLP, 4 M2, 1 DBC |
| patch-G (HD gear) | 775 MB | 3,746 BLP, 478 M2, ItemDisplayInfo.dbc |
| patch-I (HD interface) | 162 MB | 31,705 interface files |
| patch-L | 37 MB | 2 M2 (blood elf / troll female replacements) |
| patch-M (HD maps/loading screens) | 439 MB | 3,563 interface BLP (world map art and loading screens, custom zones included) |
| patch-T / patch-U (ultra HD chars/gear) | 542 MB / 1.66 GB | 28,600 / 21,859 BLP. The pack's own notes call them crash-prone |
| patch-Y | 13 KB | 3 GlueXML files (auto-login) |

### Custom maps (Map.dbc ids not in stock 1.12)

`Map.dbc`: 57 rows against 44 in stock. 19 new ids:

| Id | Directory | Server name (`map_template`) | Type / cap | ADTs |
|---:|---|---|---|---:|
| 26 | PVPZone05 | Blood Ring | arena | 36 (shared dir) |
| 27 | Sunnyglade | Sunnyglade Valley | BG / 10 | 55 |
| 28 | PVPZone05 | (Lordaeron Arena area) | arena | shared |
| 532 | Karazahn | (Karazhan, lower) | raid | 9 |
| 800 | KarazhanCrypt | Karazhan Crypt | dungeon / 5 | 6 |
| 801 | Moomoo | Secret Cow Level | – | 1 |
| 802 | Grove | Crescent Grove | dungeon / 5 | 17 |
| 807 | EmeraldSanctum | Emerald Sanctum | raid / 40 | 4 |
| 808 | Hateforge | Hateforge Quarry | dungeon / 5 | 9 |
| 813 | Winterveil | Winter Veil Vale | world | 16 |
| 814 | Karazahn40 | Tower of Karazhan | raid / 40 | 26 |
| 815 | Giln | Gilneas City | dungeon / 10 | 16 |
| 816 | Dragonmaw | Dragonmaw Retreat | dungeon / 5 | 9 |
| 817 | HighElfPVP | (test) | – | 10 |
| 818 | Balor | Stormwrought Ruins | dungeon / 5 | 9 |
| 819 | Timbermaw | Timbermaw Hold | raid / 20 | 9 |
| 820 | MooCanyon | Windhorn Canyon | dungeon / 5 | 9 |
| 821 | netherstormbg | Thorn Gorge | BG | 36 |
| 822 | FrostmaneCaverns | Frostmane Hollow | dungeon / 5 | 9 |

Other map directories that exist only here, with no Map.dbc row: `collin` (4 ADT), `gnomeshrink`
(WDT only), `scarletcitadel` (4 ADT), `grove`/`moomoo` duplicates. The server also has
`map_template` rows 804 Eldrethalas, 806 Frostmane Retreat and 809 Gnomeshrink with no client map.

**The big open-world additions are not new maps.** They sit on the **Azeroth (0) and Kalimdor (1)**
ADTs: Gilneas, Alah'Thalas / Thalassian Highlands, Lapidis, Gillijim, Tel'Abim, Hyjal, Icepoint,
Blackstone Island, Scarlet Enclave, Grim Reaches, Balor, Northwind, Moonwhisper and others
(`WorldMapArea` 500–705). The custom patches override **355 Azeroth and 320 Kalimdor tiles**. Of
these, **54 Azeroth and 91 Kalimdor tiles are new** (not in stock); the rest are edited stock tiles.

### DBCs that differ from stock (final chain copy against our STOCK chain)

Every DBC checked keeps the **stock 1.12 field count and record size**, so the layouts are standard
and only rows were added.

| DBC | Client rows | Stock rows | New ids | New id range / notes |
|---|---:|---:|---:|---|
| Map | 57 | 44 | 19 | 26–28, 532, 800–822 |
| AreaTable | 1,481 | 1,081 | 404 | 2038–5735 (zones above, Gilneas, Tel'Abim, …) |
| WorldMapArea | 175 | 51 | 124 | 500–705 (also adds per-dungeon map areas) |
| WorldMapContinent | 3 | 2 | 1 | id 3 |
| LoadingScreens | 69 | 40 | 29 | 200, 500–527 |
| AreaTrigger | 515 | 432 | 83 | up to 5665 |
| ChrRaces | 10 | 9 | 1 | 10 = High Elf |
| Faction | 204 | 190 | 14 | up to 1014 |
| CreatureDisplayInfo | 13,422 | 10,534 | 2,888 | up to 21697 |
| CreatureModelData | 876 | 430 | 446 | up to **90085** |
| GameObjectDisplayInfo | 11,769 | 1,638 | 10,131 | 6433–33281 |
| ItemDisplayInfo | 45,251 | 29,604 | 15,647 | 32878–69173 |
| Spell | 27,916 | 22,357 | 5,568 | up to 61001 |
| SpellVisual | 2,301 | 2,165 | 136 | 6923–8028 |

The repack's `server\data\dbc\` holds 158 DBCs extracted from this client (same content).

### Formats: standard 1.12, with a few later-era extensions
Samples from every patch archive (383 files) were read:
- **M2: every sample is `MD20` version 256** (vanilla). There are no `.skin`-split models. The few
  `.skin`/`.anim` files in patch-5/6/C/D are leftovers that the 1.12 path never loads. The HD models
  are just much **bigger** v256 files: character models run 4–26 MB (stock is about 0.3–1 MB), and
  some creatures run 10–24 MB.
- **WMO: v17** everywhere. **WDT/ADT: version 18** with the 1.12 chunk order (`MHDR, MCIN, MTEX,
  MMDX, MMID, MWMO, MWID, MDDF, MODF, MCNK…`).
- **Extensions to watch:**
  - Some new ADTs (patch-5, patch-7; 3 of 17 sampled) carry an **`MH2O`** liquid chunk (the
    WotLK liquid format) instead of per-chunk `MCLQ`.
  - Some WDTs set **MPHD flags 0x2 / 0x4 / 0x8** (seen 2 and 14), which in later clients mean
    vertex colours (`MCCV`), big alpha maps and sorted doodads.
  - Our `benilla-wdt` reads only MPHD bit 0, and `benilla-adt` has no `MH2O` path. Water would go
    missing and alpha could decode wrongly on those maps.
- **BLP: all BLP2**, compression 1 (palette) and 2 (DXT), alpha depth 0/1/8. Stock textures top out
  around 256–512 px. The HD packs ship **1024×1024** textures as standard (B, C, D, E, G, U).
  patch-T/U are character skins at 256–1024. Nothing larger than 1024 turned up in the samples.

### What "HD" means here
patch-A…U is a texture-replacement pack over stock *and* custom content, plus higher-poly
replacements for player/NPC models (A, L), creatures (C), doodads (D) and gear (G). File names and
formats stay the same, so it is a drop-in override. Costs: about 11 GB of data, 4× texture memory,
much heavier character models, and the pack's own notes report crashes with T/U in cities. **For
WOMW, treat HD as optional and separate from the zone content (patch-3…9).**

---

## 2. The repack

- **Server:** a MaNGOS-family core for the same custom content, with an AI playerbots
  module and an AH bot. It was compiled 2026-09-19 (core rev `1c7c6b4c`;
  boost 1.92, MSVC). The binaries are `server\mangosd.exe` and `server\realmd.exe`, plus the four
  extractors. Its config descends from the same Elysium/vmangos line as ours (`Progression.*` keys,
  the anticheat block), but **it is not vmangos**. The world DB schema differs (see below).
- **No SQL dumps.** The DB is a **live portable MariaDB 11.4.8 data directory**
  (`database\data`), with schemas `their_world` (≈670 MB), `their_char` (≈450 MB),
  `their_char_stage_oldnative` (≈300 MB, a staged copy of an older install's characters), `their_logon`
  (≈47 MB) and `their_logs`. The extracted SQL sources were not left behind. Only `setup.log` shows
  migrations being applied.
- **Server data, already extracted:** `server\data\dbc` (158), `maps` (1.4 GB, 2,805 files, map ids
  0/1/26–30/33/36/47/…/509–533/**800–822**), `vmaps` (4.2 GB), `mmaps` (3.0 GB, all listed maps
  including 800–822) and `mmapSettings.txt`/`offmesh.txt`. These belong to the repack's own core.
  The `.map`/vmap/mmap versions must be checked against our vmangos before reuse; regenerating with
  our extractors is safer.
- **Configs:** `mangosd.conf`, `realmd.conf`, `aiplayerbot.conf`, `ahbot.conf` (+ `.dist`).
- **Sensitive data (not copied, not read):**
  - The README states a default GM account login and the DB root/mangos logins. Both are defaults,
    but treat them as credentials.
  - `their_logon` holds `account`, IP logins, fingerprint/hwprint ban tables, shop/donation tables
    and two-factor tables.
  - `their_char` and `their_char_stage_oldnative` hold someone's characters, mail, tickets and chat
    whisper targets. `server\logs\` has chat, client and anticheat logs. `setup.log` has local paths.
  - **Never import or copy `their_logon`, `their_char*`, `their_logs` or the logs.** Only `their_world` is useful.

### World DB (`their_world`) compared with our vmangos world dump (`tools/downloads/world.zip`)

| Table | Repack | vmangos | Repack-only entries | Where the new ids sit |
|---|---:|---:|---:|---|
| creature_template | 14,341 | 10,381 | 5,184 | mostly 50000–69999 (3,027 in 60000s), 80000–99999 (1,009), 2M+ (46) |
| item_template | 26,543 | 17,707 | 8,836 | 30000–89999 (≈8,400), 1M+ (129) |
| quest_template | 6,708 | 4,433 | 2,463 | 40000–49999 (2,058), 50000–89999 (≈370) |
| gameobject_template | 21,196 | 9,368 | 12,436 | mostly 2,000,000+ (11,389) |
| spell_template | 27,923 | 22,381 | – | max id 90006 |
| creature spawns | 88,806 | – | – | max guid 2,902,655 |
| gameobject spawns | 66,117 | – | – | max guid 7,002,997 |

- Spawns on new maps: 800: 250, 802: 459, 807: 158, **808: 346**, 813: 180, 814: 225, 815: 340,
  816: 462, 818: 564, 819: 282, 820: 278, 822: 148 creatures. On maps 0/1 there are about 35k
  creatures each (vmangos has about 25k each), which is where the open-world zones live.
- **Schema:** 297 tables, of which 137 are not in vmangos. They are:
  - DBC mirrors kept in the DB: `map`, `areatable`, `spell*`, `itemdisplayinfo`,
    `creaturedisplayinfo`, `taxi*`, `talent*`, `lock`, `light*`, `soundentries` and more, plus
    later-era tables (`achievement*`, `vehicle*`, `currencytypes`, `itemextendedcost`, `holidays`,
    `movie`).
  - Collections, transmog and shop: `collection_mount/pet/toy`, `item_transmogrify_template`,
    `shop_*`.
  - Custom systems: `custom_graveyards`, `custom_merchant`, `dynamic_visibility_template`,
    `object_scaling`, `quest_cast_objective`, `scripted_areatrigger`, `miraclerace_*`,
    `daily_quest_timer`.
  - Playerbot tables (`ai_playerbot_*`).

  Fifteen vmangos tables are absent, among them `creature_classlevelstats`,
  `player_premade_*` and `areatrigger_scripts`. Template columns differ too (`map_template` has
  `map_type`, `player_limit`, `ghost_entrance_*` and so on). **Rows cannot be `INSERT … SELECT`ed
  as-is.** Each table needs a column-mapping script.

---

## 3. Compatibility with WOMW

### Can be imported more or less directly
- **New-map zone art**:
  - The 800–822 dungeons are self-contained `World\Maps\<dir>\` WDT+ADT sets, plus the WMOs, M2s
    and BLPs they reference.
  - Formats are 1.12 (M2 v256, WMO v17, ADT v18, BLP2), so our loaders read them, apart from the
    `MH2O`/MPHD-flag caveat.
  - Map ids 800–822 do **not** collide with anything we use.
- **DBC rows.** The layouts equal stock, so new rows can be appended to our DBCs: Map, AreaTable,
  WorldMapArea, LoadingScreens, AreaTrigger, CreatureDisplayInfo/ModelData,
  GameObjectDisplayInfo, ItemDisplayInfo.
- **Models/textures** for creature and object displays referenced by the zone.
- **HD pack.** It overrides files by name, so it works as optional client patches.

### Conflicts and risks
- **Item ids:**
  - Repack items exist in **91000–91999 (22)**, 92000–92999 (63) and **93000–94999 (37)**. These
    clash with our companion items 911xx–912xx, the quartermaster/ledger items 930xx–934xx,
    salvage 936xx and the instruments 941xx.
  - 90000–90999 is clear.
- **Creature ids:**
  - **407 repack creatures sit in 90000–91999** and 155 in 92000–92999. That covers our 90000+
    companions and the 92002–92699 quartermasters/W-packages.
  - 93500–93999 (wanderers 937xx) is clear.
- **Quest ids:** 95000–97999 is clear (0 repack quests). But 40000–89999 holds 2,463 repack quests,
  so check before we use those ranges.
- **Gameobject ids:** 97 repack GOs sit in 90000–99999. Check them against our construction
  objects. Their 2M+ range is free for us.
- **Spells:**
  - The client Spell.dbc adds 5,568 rows, up to 61001, and some sit in *low* ids that stock
    leaves empty.
  - The repack `spell_template` reaches 90006. Before importing any spell, diff it against our
    custom spell ids.
  - Importing their Spell.dbc wholesale would overwrite stock rows that our class and kit work
    depends on. **Only cherry-pick rows the zone needs.**
- **Display ids:**
  - CreatureModelData reaches 90085, and the display ranges reach 21697 (creatures), 33281
    (gameobjects) and 69173 (items).
  - If we have added our own display rows, renumber on import. Check our current DBC overrides
    first.
- **Map 0/1 zones** (Gilneas, Tel'Abim, Hyjal, …) mean replacing **675 Azeroth/Kalimdor tiles**.
  That clashes with any terrain work of our own on those continents. It also drags in all their
  ADT edits and their open-world spawns and quests, and it needs a new world map, taxi and area
  data. This is a big merge, not a first step.
- **Server tables.** The world DB uses a different schema (no vmangos patch columns, extra custom
  tables). Scripted content (`script_name` in `map_template`, `scripted_areatrigger`, boss C++
  scripts in their core) does **not** come with the DB. Bosses would be dumb melee until we
  rescript them in our own core.
- **Licensing.** Permission is private. Keep all imported assets out of the public tree and out of
  public credits, as agreed.

### What our client (benilla) needs
1. `benilla-wdt`: decode MPHD flags beyond bit 0 (0x2 MCCV, 0x4 big alpha 4096-byte layers,
   0x8 sorted doodads).
2. `benilla-adt`: an `MH2O` liquid reader, used where `MCLQ` is missing; `MCCV` vertex colours;
   big-alpha (uncompressed 64×64 8-bit) layers.
3. Map list, loading screens and world map art come from DBC. They work once the rows are in our
   DBCs (`WorldMapArea`/`WorldMapContinent` id 3 for any new continent).
4. Texture memory and streaming for 1024² BLPs and 10–25 MB M2s, if HD is ever enabled. Make it
   opt-in.
5. A packaging step that adds a `patch-?.MPQ` (one letter) holding only the imported files, so
   the `Chain` order stays explicit.

### Server-side steps
1. **Map rows:** add `map_template` (vmangos schema) rows for the new ids, and copy the DBC rows
   into the server's dbc folder too.
2. **Collision data:** run **our** vmangos extractors on a client data dir containing the imported
   patch, to make `maps/`, `vmaps/` and `mmaps/` for the new map id. Our map extractor must
   understand `MH2O` and the MPHD flags, or liquids and heights will be wrong. Do not reuse the
   repack's files without checking versions.
3. **DB merge strategy:**
   - Export only the rows the zone needs from a scratch copy of `their_world`: the `creature`/
     `gameobject` spawns for that map, their templates, loot, equipment, model info, `npc_text`,
     waypoints, quests and their relations, `areatrigger_teleport` rows, and the graveyards.
   - Push them through a per-table column-mapping script into vmangos columns, with
     `patch_min=0, patch_max=10`.
   - **Remap ids into a fresh WOMW import block:** creatures/items/GOs 200000–299999 for imported
     content, and quests 98000–99999. Keep a mapping table (old → new) checked into private SQL
     so later re-imports stay stable.
   - Spawn guids get a new range above our current maximums.
4. **Entrance:** add an `areatrigger_teleport` entry plus an entrance object or portal in our
   world, since the instance entrance lives on Azeroth tiles that we are not importing.

---

## 4. Recommended first zone: **Hateforge Quarry (map 808)**

Why this one:
- A self-contained 5-player dungeon on its **own map**: 9 ADTs, no MH2O seen, and it does not
  touch Azeroth tiles.
- Small server footprint: **346 creature spawns of 18 templates**, 44 GO spawns, 1 teleport
  trigger, and 7 quests sorted to zone 5098.
- Its theme is a Dark Iron quarry with dwarven industry. That fits our guns-versus-classes
  setting (a black-powder factory to fight through).
- Map id 808 is free in our server.

Runner-up: Crescent Grove (802; 17 ADTs, 459 creatures).

### Step-by-step plan
1. **Extract (read-only).** Use `benilla-extract` with the community client's `Data` dir as the
   chain:
   - `World\Maps\Hateforge\*` (WDT, WDL, 9 ADTs).
   - Walk each ADT's `MMDX`/`MWMO` and each WMO's `MOTX`/`MODN` to collect every referenced M2,
     WMO (+ `_NNN` groups) and BLP.
   - Grab the loading screen (LoadingScreens 515) and the world map art (WorldMapArea 671/672 in
     patch-M or the base patches).
   - Write it all to a private staging dir, never into the repo's public paths.
2. **Format check.** Run a census on the staged files: M2 version, WMO version, ADT chunks
   (MH2O/MCCV), WDT MPHD flags, BLP sizes. Fix or extend our loaders where needed (§3).
3. **DBC rows.** Append to our client DBCs, then rebuild them into a new private `patch-?.MPQ`:
   - Map 808 and AreaTable 5098/5101/5103.
   - WorldMapArea 671/672, LoadingScreens 515, the AreaTrigger rows for map 808.
   - CreatureDisplayInfo/ModelData and GameObjectDisplayInfo rows for the 18 creatures and the
     GOs. Renumber if they collide with our overrides.
4. **Client test.**
   - Teleport offline or locally with benilla (`WOW_MAP=808`) and check terrain, WMOs, doodads,
     liquids and lighting.
   - Run `scripts/visual.sh`-style captures and compare them with screenshots taken the same way.
5. **Server collision.** Run our extractors on the merged data to build 808's `maps/vmaps/mmaps`.
   Add the `map_template` 808 row (type dungeon, 5 players).
6. **DB import.** From the scratch `their_world` copy:
   - Export map 808's creature/GO spawns, templates, loot, waypoints and the 7 quests.
   - Map the columns into vmangos.
   - Remap the ids into the 200000+ import block; the quests go to 98000+.
   - Save the result as `server/sql/custom/azeroth_warfare/NN_import_hateforge.sql`, and keep that
     file private (add it to the exclude list).
7. **Make it WOMW.**
   - Retune the creatures to our gun baseline and swap some for gun NPCs (see
     `aw-npc-guns-design`).
   - Write simple boss scripts in our core.
   - Place an entrance portal in Burning Steppes/Searing Gorge on our Azeroth, via a GO plus
     `areatrigger_teleport`.
8. **Package.**
   - Put the private patch MPQ and the server data into `dist` only; the public export never
     gets them.
   - Do a live test once allowed, and only after the client has closed.
9. **Then scale up.** Do the other small dungeons next (802, 816, 818, 820, 822). Leave the
   Azeroth/Kalimdor open-world zones for a planned terrain merge.
