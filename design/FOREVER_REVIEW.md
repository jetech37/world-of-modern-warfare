# WoW Forever client review (PRIVATE)

A read-only look (Oct 27 2026) at the official "WoW Forever" client the owner installed at
`E:\Local Games\Battlenet\Wow\World of Warcraft` and at the matching server repack
`E:\Local Games\ForeverRepack_2026_10_13\ForeverRepack`. Nothing in either folder was run or
modified; accounts, characters and logs were not opened. Files pulled out for the review are in
the session scratchpad, not in the repo.

## What the install is

- `.build.info`: product `wowf`, version **1.60.1.70634**, build name
  `WOW-70634patch1.60.1_Retail`, `client-version = 3.12.0`. A modern (retail-engine) Classic-era
  client: 1.12 world content on the current engine and file formats.
- CASC: 48 `data.NNN` archives, 1.04 M local index entries, 2.2 M encoding entries, root
  `MFST v2` with **1,250,964 FileDataIDs, 1,245,874 stored locally** (the install is complete).
  Only 231 k root entries still carry a name hash; the rest are FileDataID-only.
- DB2s are `WDC5` (`WOWSTATIC_1_60_1_70399`). Some sections are encrypted (TACT key not shipped):
  Map.db2 has 3 encrypted sections (8 rows of unreleased maps); the tool zero-fills and reports them.
- The repack README says it targets **1.60.1.70338** ("WoW Classic beta", `_classic_beta_`,
  `WowB.exe`); the installed client is **70634** (`_forever_`). The build mismatch matters only if
  the owner ever wants to run it; we did not.

## The reader: `tools/casc-read`

Standalone crate (its own workspace, deps `anyhow` + `flate2`, no crates.io CASC crate needed).

- Reads `.build.info` -> build config -> root and encoding keys; newest `.idx` per bucket;
  `data.NNN` (30-byte header + BLTE); encoding CE pages; root (legacy, MFST v1 and v2) by
  FileDataID, plus Jenkins `hashlittle2` name hashes (tested against lookup3's vector).
- BLTE: `N` and `Z` chunks; `E` (encrypted) chunks are zero-filled and counted, never fatal.
- Picks, per FileDataID, a root entry that is stored locally (enUS/all-locale first).
- Commands: `info`, `extract <fdid> <out>`, `extract-ids <list>` (one open for a batch;
  opening takes ~4 s), `hash <path>`, `sniff <out.tsv> [from] [to]`, `find <listfile> <substr>`,
  `extract-list <listfile> <substr> <dir> [max]`.
- `db2dump.py`: WDC5 to TSV (plain, bitpacked, common, pallet, pallet-array, signed fields;
  string columns resolved). Fixed-size DB2s only (no offset map), enough for Map, AreaTable, UiMap.

```
cd tools/casc-read && CARGO_BUILD_JOBS=2 cargo build --release
target/release/casc-read "<games folder>/Battlenet/Wow/World of Warcraft" info
target/release/casc-read "<install>" extract 1349477 Map.db2      # Map.db2
python -I db2dump.py Map.db2 --strings 0,1
```

Known FileDataIDs used: Map.db2 1349477, AreaTable.db2 1353545, UiMap.db2 1957206, Azeroth.wdt
775971, Kalimdor.wdt 782779 (Map.db2 column 22 is the WDT FileDataID).

**Listfile (needs approval):** names come from the community listfile,
`https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv`
(release `202610271802`, **153,792,830 bytes**, `fdid;path` lines). Not downloaded. Everything
below was found without it, by following FileDataIDs from DB2 and WDT/ADT/WMO chunks.

## Dalaran

Yes, two Dalaran maps, both new (Forever-only) instance-style maps cut from the Alterac
Mountains, and both use **Wrath's city WMO**, not a Legion one and not the classic dome:

| Map | Name (Map.db2) | WDT fdid | Tiles | City WMO |
|---|---|---|---|---|
| 2959 | City of Dalaran (instance type 1; areas "City of Dalaran" 16544/16560, "Dalaran" 16558, "Fel Grove" 16559, "Hillsbrad Foothills" 16562) | 6982267 | 25 (29-33 x 29-33) | **7116370**, a Blizzard-reworked `ND_Dalaran`: 93 groups |
| 2980 | Dalaran City (map type 0, cosmetic parent map 0) | 7090844 | 16 (30-33 x 30-33) | **115404**, the stock Wrath `ND_Dalaran` (91 groups) |

Plus `3217 Dalaran Skycuter` (a transport map) and, on map 0, area 17707 "Old Dalaran Ruins".

- **Placement:** both cities stand at MODF `(16763.5, *, 16791.5)`, i.e. in the Alterac crater,
  within ~100 yd of the 1.12 dome centre we use. Heights: Wrath WMO at Z 62.3 (heading 81.5),
  reworked WMO at Z 90.7 (heading 75). Ours: Z 45 street (WMO origin ~26) over the 41.9 crater
  floor, so their terrain under the city was re-sculpted by hand.
- **The reworked city (7116370)** drops `floatingrock_01..03` (it is built to sit on the ground),
  plus `shop_interior10`, `shop_tailoring02`, `shop_skinning`, `shop_enchanting03`, `extpipe`;
  adds `prisondungeon`, `backrooms`, `lookout`, `Leftwindow`, `Rightwindow`, `connect5`,
  `archcon*`, `extpipe001`. This is exactly the problem our `dalaran.rs` solves by reshaping the
  ground around the floating rock.
- **Around the city:** 84 distinct WMOs (Alterac/Hillsbrad farmhouses, ruined walls, towers,
  Gilneas wall pieces, an inn) and ~430-460 distinct M2s, 10-16 k doodad placements per map.
  A dressed countryside, not just the city.

### Formats

| Part | Format | 1.12 needs | Work |
|---|---|---|---|
| WDT | MPHD flags 0x3ca (MCCV, big alpha, height texturing, FDID-based), **MAID** | MAIN + names | small |
| ADT | **split**: root (MHDR, MH2O, MCNK) + `_obj0` (MDDF/MODF by FileDataID, flag 0x40/0x8) + `_obj1` + `_tex0` (+ lod) | single ADT, MCLQ water, 4-bit alpha, MMDX/MWMO names | medium: merge split files, MH2O->MCLQ, big alpha->4-bit, MCCV bake or drop, FDIDs -> paths |
| WMO root | v17, no MOTX: **MOMT textures by FileDataID**, GFID (372 = 93 groups x 4 LODs) | v17 with MOTX names | small-medium |
| WMO groups | v17 with modern chunks: `MOGX`, `MPY2`, `MOVI`, `MOVT`, `MONR`, `MOTV`, `MOBA` (new layout), `MOBS`, `MOBN/MOBR`, `MOCV`, `MDAL`, `MNLR`, `MFVR`, `MLIQ` | MOPY, 1.12 MOBA, MOCV, MLIQ | medium: MPY2->MOPY, rebuild MOBA, drop MOGX/MDAL/MNLR/MOBS/MFVR, same MOCV lighting fix as ours |
| M2 | **MD21** chunked, inner `MD20` version **272**, `SFID` (skins by fdid), `TXID` (textures by fdid) | MDX v256 | medium: unwrap MD21, resolve SFID/TXID, then our existing Wrath (v264) down-port in `m2conv.rs` |
| BLP | BLP2, DXT (231 DXT1-style no alpha, 31 with alpha type 7 = DXT5), **42 of 262 non-power-of-two**, up to 2048 | BLP2 DXT1/DXT3 power-of-two (DXT5 support in the 1.12 reference client: verify) | small: decode, resize to POT, re-encode |

All referenced Dalaran textures (262) are stored locally.

### Recommendation

- **Keep our Wrath-based Dalaran on map 0** as the shipping city. Theirs is the same Wrath WMO;
  the textures and most geometry are the same art, so it would not look much better.
- **Borrow the reworked WMO (7116370) later** as an upgrade, because it is designed to sit on the
  ground (no floating rock) and adds a prison and back rooms. Converting it means: a modern-group
  -> 1.12 WMO converter (MPY2/MOBA/LOD drop, FDID -> path textures), the 1.12 lighting fix we
  already do, and ~260 BLPs. Estimated 2-4 days with the listfile, longer without (we would have
  to invent paths for FDID-only textures).
- **Their terrain around the crater** (hand-sculpted for the city, plus the dressed farmland)
  is the bigger visual win over our script-reshaped ring. It needs the split-ADT merger above;
  then only tiles 30-32 x 30-32 change, as now. Estimated 3-5 days on top, and it is shared work
  for any other Forever map.
- Do this after the listfile is approved; it makes FDID -> path naming trivial.

## Forever-specific content vs 1.12

### Maps (Map.db2; 81 rows, 73 readable)

New beyond 1.12's set (directories are numeric = map id):

- **Instances / scenarios:** 2720 The Searing Basin, 2784 Demon Fall Canyon, 2806 Shadow Hold,
  2807 Burning of Andorhal, 2817 Starfall Barrow Den, 2853 Deadwind Pass, 2875 Karazhan Crypts,
  2902 The Scarab Dais, 2921 Naxxramas (new copy), 2959 City of Dalaran, 2998 Excavation Site:
  Wetlands, 2999 Ruins of Lordaeron, 3065 The Hall of Thanes, 3109 Manor Mistmantle.
- **Raids:** 2789 The Tainted Scar, 2791 Storm Cliffs, 2804 The Crystal Vale, 2832 Nightmare Grove,
  2856 Scarlet Enclave.
- **Battlegrounds:** 2997 Darkspear Islands, 3005 Battle for Gilneas (Forsaken vs Worgen at the
  Greymane Wall), 2996 Warsong Gulch (Winter).
- **Open maps:** 2868 Eastern Plaguelands (Scarlet Raid Phase), 2980 Dalaran City, 2991 Zephras
  Isle, 2995 Hyjal Crater, 3021 Eastern Kingdoms Preserved, 3104 "nothing to see here".
- **Transports:** 3217 Dalaran Skycuter, 3218 Mulgore Skycuter.

### Continents

- Azeroth: 736 tiles vs 687 in 1.12: **49 new**, all sea/coast filler (23-26 x 54-59, 24 x 31-39,
  43-45 x 43-50; "The Great Sea", "The Forbidding Sea", Eastwind Shore). Kalimdor: 988 vs 1013
  (20 northern sea tiles 45-48 x 12-16 gone, 5 new at 48 x 21-25). Stock tiles may be re-edited
  too (not diffed in this pass).
- New areas on the continents (AreaTable, 294 rows above id 3500): Riverglades (map 0), Shen'dralas
  (map 1), Gilneas / Ruins of Gilneas / Shark-Infested Waters, Old Dalaran Ruins, Whelgar's
  Excavation Site, Half-Pint Tavern (map 3002), and a "Blue Whelpling Inn" prototype in Elwynn.
- UiMap adds world-map entries for Mount Hyjal, Zephras Isle, Darkspear Islands, Riverglades and
  Shen'dralas.

### Server repack DB

- `mysql\data\world`: 272 InnoDB `.ibd` + 4 MyISAM tables (`access_requirement`,
  `achievement_dbc`, `achievement_reward`, `creature_template_sparring`) with `.sdi` schemas.
  `hotfixes`: 1451 `.ibd`. `setup\` is empty: **no `.sql` dumps**. `data\` (maps) is empty.
- The `.ibd` pages are uncompressed: strings are readable straight from the file (e.g. 552
  Dalaran / Kirin Tor creature names in `creature_template.ibd`: Dalaran Apprentice/Watcher/
  Wizard/Protector/Warder, Archmage Ansirem Runeweaver...). Real row reading needs an InnoDB page
  parser using the SDI schema embedded in each `.ibd`. Options, no official MySQL needed:
  1. **Approve a download** of the offline parser `ibd2sql` (pure Python; GitHub
     `ddcw/ibd2sql`; licence and size to check before approval) and run it on copies with `-I`; or
  2. write our own small InnoDB reader (COMPACT/DYNAMIC rows + SDI) in `tools/` (~2 days).
  An official MySQL 8 download is **not** needed, and the repack's `mysqld` must not be run.
- Biggest tables: quest_template_locale 216 MB, creature_template_locale 144 MB,
  creature_template_difficulty 52 MB, quest_template 52 MB, creature_template 48 MB,
  creature_loot_template 32 MB, gameobject_template 31 MB, creature 22 MB, smart_scripts 18 MB.
  It is TrinityCore's modern schema (difficulty tables, quest_objectives, areatrigger_create_*),
  so like the AzerothCore import it needs a schema mapping to vmangos.

## Worth borrowing for WOMW

Cross-checked with `WRATH_IMPORT_PLAN.md`, `WOTLK_ASSETS.md`, `COMMUNITY_MERGE.md`:

1. **Battle for Gilneas** and **Darkspear Islands** battlegrounds: small, purpose-built PvP maps
   (capture objectives / flag) that fit MW2 modes; nothing comparable from Wrath or the community
   client. Warsong Gulch (Winter) as a cheap re-skin variant.
2. **The grounded Dalaran** (reworked WMO + sculpted crater terrain), as above.
3. **New dungeon/scenario maps in classic zones** (Karazhan Crypts, Ruins of Lordaeron, Hall of
   Thanes, Manor Mistmantle, Excavation Site: Wetlands, Demon Fall Canyon, Shadow Hold): classic-
   style art, small, good as strike/co-op maps. Overlaps: community client already gave us a
   Gilneas, a Scarlet Enclave and Hyjal; prefer theirs (Forever) only where the art is better.
4. **Hyjal Crater / Zephras Isle**: new open maps; check against the community Hyjal (616)
   before importing.
5. **NPC/quest data from the world DB** for the above (after the ibd reader).

Not worth it: modern UI (FrameXML for a retail engine, does not port to 1.12), the 49 new sea
tiles, the Skycuter transports.

Shared prerequisite for all of it: a **modern -> 1.12 converter set** (split ADT merge, modern WMO
groups, MD21 M2, BLP resize), which also future-proofs us for any retail-era asset.

## Needs the owner's approval

1. Download the community listfile (154 MB, URL above) into the scratchpad.
2. Download `ibd2sql` (or say "write our own InnoDB reader").
3. Decide whether Forever/Classic-era Blizzard art may go into the private patches like the
   Wrath art (same licensing stance as `private-assets.md`).

## Flying over cities: is Forever's world better? (follow-up, Oct 27)

Question: would Forever's maps hold up better under our skyriding than stock 1.12 or the
community client? Short answer: **no**. Blizzard's Forever world has not been fixed for flying,
and Forever itself has no flying.

### Forever does not fly

- `MountCapability.db2` (26 rows): every capability is ground-only (flags 17 / 145). None has the
  flying bit. Mount.db2 and SpellName.db2 are partly encrypted. The readable names include
  "Flight Form" and "Swift Flight" gear, but no skyriding or dragonriding spells.
- AreaTable: of 1375 readable areas, only **Zephras Isle** (its own map 2991) has
  `EnableFlightBoundsOnMap` (0x4000). No city or old-world zone has any flight flag.
- On Kalimdor 39_21 (Hyjal) Forever places `world/generic/pvp/collisionwall/collisionwallwmo02.wmo`,
  an **invisible wall**. Blizzard fences off the unfinished areas; it does not finish them.

### Which city WMOs Forever actually places

The listfile gave names to the FileDataIDs placed in Forever's `_obj0` tiles. The cities use
**rebuilt WMOs under new FileDataIDs**, not the legacy files:

| City | 1.12 file | Forever placed |
|---|---|---|
| Stormwind | `stormwind.wmo` | 7962574 (`autogen-names/stormwindcity`), plus 7556033 and the Cataclysm-era harbour pieces (`stormwindhangar`, `sw_harbor_*`) |
| Ironforge | `ironforge.wmo` | 7801267 |
| Undercity | `undercity.wmo` | 7675285 |
| Orgrimmar | `ogrimmar.wmo` (absent from Forever's root) | 7964229 |
| Blackrock Mountain | `blackrock.wmo` | 7952336 + `blackrock2.wmo` |
| Darnassus | `darnassis.wmo` | the same file (113359) |
| Thunder Bluff | mesa WMOs | the same mesa WMOs |

### Numbers

WMOs, as stock 1.12 / community client (`patch-3`, which our patch-A carries as
`World\wmo\Stormwind.wmo`) / Forever as placed:

| City | groups | triangles (collision-only) | bbox (yd) | top surface back-facing from above* |
|---|---|---|---|---|
| Stormwind | 306 / 306 / 289 | 910 k (80 k) / 768 k (63 k) / 968 k (92 k) | Forever and community reach x -850 (harbour side), stock -600 | 13.5% / 11.7% / 11.4% |
| Ironforge | 104 / 104 / 104 | 422 k / 422 k / 503 k | same box | 94% / 94% / 94% (underground: the mountain terrain covers it) |
| Orgrimmar | 144 / 144 / 138 | 538 k / 538 k / 550 k | same box | 6.1% / 6.1% / 6.1% |
| Undercity | 206 / 206 / 216 | 445 k / 446 k / 663 k | Forever top +68 yd (it now holds 19 above-ground groups) | 86% / 85% / 45% |
| Darnassus | 104 / 104 / 104 | 116 k / 116 k / 176 k | same box | 1.9% / 1.9% / 11%** |

\* For each 2-yd cell, take the highest rendered (non-collision) triangle. If its winding faces
down, the client culls it as a back face and a flyer looks straight through. This catches missing
roofs and open tops of walls. It ignores doodads and terrain.
\** Probably a group/LOD ordering artefact in Forever's GFID list. Either way, not better.

Terrain on the city tiles (chunks with holes, WMO / M2 placements), stock / community / Forever:

| Area (tiles) | holes | WMO placements | M2 placements |
|---|---|---|---|
| Stormwind (12) | 21 / 41 / 20 | 43 / 82 / 75 | 4114 / 5893 / 5388 |
| Ironforge + airport (16) | 17 / 32 / 18 | 54 / 83 / 57 | 5040 / 7687 / 5975 |
| Undercity / Lordaeron (4) | 74 / 74 / 50 | 8 / 8 / 6 | 1800 / 1800 / 1923 |
| Blackrock Mountain (6) | 18 / 19 / 9 | 21 / 21 / 21 | 669 / 736 / 746 |
| Orgrimmar (9) | 3 / 3 / 6 | 21 / 22 / 21 | 1699 / 1761 / 1702 |
| Thunder Bluff (4) | 2 / 2 / 2 | 47 / 48 / 51 | 632 / 639 / 825 |
| Darnassus (6) | 2 / 2 / 2 | 6 / 6 / 6 | 1350 / 1452 / 1353 |
| Hyjal (64) | 38 / 39 / 42 | 137 / 177 / 184 | 12620 / 21022 / 13411 |

- **No new city tiles.** Forever's 49 new Azeroth tiles are open sea (see above). The world's
  edges are the same.
- **Hyjal** gains three new Forever WMOs. One is a single 10 k-triangle group 900 x 975 yd and
  **1,207 yd tall** (Kalimdor 37-38 x 20-21), a backdrop or barrier, alongside the collision wall.
  This is closed off for players on foot, not finished for flyers.
- **Far terrain.** Forever's WDL adds `MAHO` (far-terrain holes), `MAOE` (ocean) and
  `MLDD/MLDX/MLMD/MLMX` (far doodads and WMOs). Its MAID tiles also carry `_lod` ADTs. That is
  better distance rendering, but on the retail engine's terms. benilla can draw distance its own
  way; there is no geometry fix in there to borrow.

### What breaks when flying, and what Forever changes

| Problem | Stock / community | Forever |
|---|---|---|
| Missing roofs and open wall tops (back-faces from above) | Stormwind ~12-14%, Orgrimmar 6% of the top view | about the same (11%, 6%) |
| Unfinished backs of mountains, holes outside the walkable area | present | same tiles. Undercity/Lordaeron has fewer holes (74 -> 50) and Blackrock Mountain fewer (18 -> 9); elsewhere unchanged |
| Invisible walls (collision-only triangles) | SW 80 k, IF 50 k | **more**: SW 92 k, IF 59 k, Org 61 k (from 48 k), plus a new Hyjal wall |
| Portal culling popping from above (exterior groups behind portals) | SW 1-2 exterior groups, 341 portals | SW 9 exterior groups, 319 portals: marginally better. Ironforge and Orgrimmar unchanged |
| Far terrain / WDL | MARE only | MAHO, MAOE and far-model chunks: better, engine-specific |
| Map edges, skybox, fog | 1.12 | same world extents |

### Conclusion and recommendation

- **Importing Forever's cities would not make flying work.** Their geometry from above is within a
  couple of points of stock 1.12 and the community client. They add invisible collision rather
  than remove it. Blizzard never had to make these cities flyable for Classic.
- **The cost would be the full converter set** (split ADT merge, modern WMO groups, MD21 M2,
  BLP resize). The cities come to about 3.5 M triangles and their textures. That is 2-3 weeks,
  and the result would still break in the same places.
- **Cheaper, and better:**
  1. **No-fly volumes over cities** in our skyriding server rules: a ceiling (for example
     terrain + 40 yd) or auto-dismount over Stormwind, Ironforge's mountain, Orgrimmar,
     Undercity/Lordaeron, Thunder Bluff and Darnassus. Key it on AreaTable ids and a per-area
     max height, rather than on AreaTable flags: 1.12 has no flying flags, and the client would
     ignore new ones. Allow landing on chosen pads (flight masters, roofs we have checked).
  2. **Invisible ceilings around the worst unfinished zones** (Hyjal, behind Blackrock Mountain,
     the Ironforge airport ridge). The same rule as Blizzard's collision wall, but enforced
     server-side, so no client art is needed.
  3. **Targeted fixes for what players will actually see.** Close specific terrain holes in our
     ADT patch layer (we already rewrite tiles for Dalaran). Add roof caps for the few large
     Stormwind back-faces as small WMO patches. Make benilla render WMO exterior groups two-sided
     when the camera is above the city's bounding box. That is a cheap client trick that hides
     most of the 6-14% back-face area.
  4. If one Forever piece is worth taking for flying, it is the **Undercity/Lordaeron exterior**
     (fewer holes, 19 above-ground groups). Import it only after the modern-WMO converter exists
     for the grounded Dalaran.
