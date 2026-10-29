# World of Modern Warfare: world building

Where the classic map's unfinished and unused land can be put to work: mainly **homestead districts**
(the plot system in `server/src/fusion/include/fusion/Plots.h`), and also arenas, open-world construction
frontiers, faction strongholds, companion grounds, quartermaster outposts and flight launch points.

This is design research. No game code or database rows were changed. Nothing here is built unless it
says so, and every coordinate still needs a look in game (`.go xyz`) before it goes into SQL.

---

## 0. How this was checked

- **Terrain:** the server's own extracted height maps (`dist/AzerothWarfare/data/maps/*.map`, format
  `MAPS z1.4`) were parsed directly. For each candidate the following were sampled: ground height,
  slope (the height range inside each plot-sized cell), terrain holes, water, and **area flags** (looked
  up against `area_template.explore_flag`). These are the same data the server uses for
  `GetHeight`/zone lookups, so the heights match what a server-side `.go` lands on. The check: the
  Hyjal gate `(5476, -3729)` reads z 1593.44, exactly the value in `game_tele` and
  `03_construction.sql`.
- **Spawns:** `creature` and `gameobject` counts inside each box (mangos DB, read only).
- **Graveyards:** `WorldSafeLocs.dbc` (from `data/5875/dbc`; vmangos has no `world_safe_locs` table,
  only `world_safe_locs_facing`), plus `game_graveyard_zone` for the zone links.
- **Map types:** both `map_template` (server) and the client's `Map.dbc`.
- **Not checked:** client rendering (textures, sky boxes, missing doodads), WMO and M2 collision
  (vmaps exist for maps 0, 1, 37, 169 and 451, but they weren't walked), and invisible walls placed
  by the client. Items marked **(verify)** depend on these.

**Axes:** in WoW, +x is north and +y is west. A plot district's origin is its lowest x and lowest y corner
(its south-east corner). Columns run north along x and rows run west along y, as in `Plots::District`.

**Flatness numbers:** "range N" means the highest point minus the lowest point inside one plot, in yards.
Below 10 is buildable as is. 10-25 needs foundations and stairs. Above 30 is a hillside. "p90" is the
90th percentile over a district's plots.

---

## 1. Findings that change existing decisions

1. **The current Hyjal districts sit partly on hillsides.** These are the per-plot height ranges at the
   origins in `Plots::Default()`:
   - Small (5490, -3850), 8 x 4 at 24 yd: ranges 2-85. The four plots at the north end, toward +x, are
     34-85.
   - Standard (5490, -3740), 6 x 4 at 40 yd: ranges 4-90. The two northern columns are 25-90.
   - Large (5490, -3570), 4 x 2 at 64 yd: ranges 13-**196**. Plot 201 spans z 1373-1687, which is
     off the bowl's edge.

   The districts run too far north, past x ≈ 5640. A measured re-layout is in §4.1. This is the
   most urgent item, because plots are already for sale.
2. **Azshara Crater is not a battleground map in this data.** `map_template` 37 has `map_type 0`, and
   the client's `Map.dbc` also has type 0 (the PvP flag is 1, the levels are 10-20 and the player cap is
   30). So `Map::Instanceable()` is false and `FusionMgr::CanBuildOn` already allows building there.
   It would be one shared continent-style map. `CONSTRUCTION.md` rejected it as "a battleground map,
   instanceable", and that reason doesn't hold here. It's the best second district site (§3.2).
3. **No graveyard serves Hyjal (zone 616).** `game_graveyard_zone` has no row for it.
   `ObjectMgr::GetClosestGraveYard` then returns nullptr, and a ghost who releases on the summit has no
   graveyard to go to (the nearest safe locs, Everlook and Moonglade, are 1.5-2 km away and not linked).
   The same is true of Azshara Crater (268), Designer Island (151) and Emerald Dream (map 169). See
   §5.3.
4. **The courier and the upkeep sweep assume one district.** `FusionConstruction.cpp` hardcodes the
   gate (`kHomesteadMap = 1`, `5476, -3729`), and runs the hourly lapse sweep only for players on map 1.
   A second district needs a gate per district and a sweep that doesn't depend on the map (§5.1).

---

## 2. Candidate summary

| # | Area | Map | Area id (zone) | Usable flat ground | Spawns in box | Verdict |
|---|---|---|---|---|---|---|
| 1 | Hyjal summit | 1 | 616 Hyjal (0) | several 100-250 yd pockets, z 1455-1600 | 0 near the bowl (homestead NPCs only) | **In use. Re-lay the districts** |
| 2 | Azshara Crater | **37** | 268 Azshara Crater (0) | Y-shaped valley floor z 230-340 | **0** | **Housing, top pick** |
| 3 | Quel'Thalas placeholder plateau | 0 | tagged 139 Eastern Plaguelands | **~600 x 1400 yd, perfectly flat**, z 129.0 | **0** | **Housing, the largest** |
| 4 | Stonetalon NW vale (placeholder) | 1 | 406 Stonetalon / 2325 Veiled Sea | ~250-400 x 1200 yd strip, flat at z 131.4 | 0 on the floor | Housing (Horde/goblin) |
| 5 | Grim Batol basin | 0 | 1037 Grim Batol (11 Wetlands) | ~200 x 250 yd, z ≈ 231 | 0 | Small district / stronghold |
| 6 | Dun Morogh airfield | 0 | 1 Dun Morogh | ~700 x 100 yd runway, z 503-505 | 25+ guards, mountaineers | Flight launch point, Alliance QM |
| 7 | Gilneas gate basin | 0 | 2397 "The Great Sea" (267 Hillsbrad) | ~200 x 250 yd, flat at z 54.2 | 0 | Boutique district |
| 8 | Redridge east plateau | 0 | 2403 Forbidding Sea (8 Swamp of Sorrows) | ~200 x 300 yd, flat at z 154 | 0 | Small district / outpost |
| 9 | Emerald Dream | 169 | 1397 Emerald Forest (0) + untagged | huge flat placeholder z 92, forest valley z 92-96 | 0 | Later, needs map-type change |
| 10 | Development Land: Programmer Isle / Designer Island | 451 | 22 / 151 | many flat test pads | 0-1 | Arenas, firing ranges, test grounds |
| 11 | GM Island | 1 | 876 | ~250 x 150 yd | GM furniture, spirit healer | Rejected (too small) |
| 12 | Uldum gates (Valley of the Watchers) | 1 | 989/990 (440 Tanaris) | 120 x 120, z ≈ 10 | 15 (Uldum quest NPCs) | Quartermaster outpost / frontier |
| 13 | Tanaris south island | 1 | 2317 South Seas (440) | hilly, range 24-74 | 13 (pirates) | Pirate PvP / frontier |
| 14 | Caverns of Time entrance (+ map 269) | 1 / 269 | 2300 (440) | 120 x 120, z ≈ 1 | 13 (AQ event NPCs) | Leave alone; map 269 is a later idea |
| 15 | Karazhan surroundings | 0 | 2562/41 Deadwind | 120 x 120, range 37 | 80 | Frontier / PvP, not housing |
| 16 | Silithus north | 1 | 1377/2742 | big flat sand, z 3-7 | 129 (Hive'Ashi) | Frontier only |
| 17 | Ashenvale/Felwood edges | 1 | 616 / 361 / 438 | ledges only | varies | Flight launch points |
| 18 | Unused islands | 0/1 | 408, 2079, 1120, 297, 2406 | mostly none | mostly in use | Not worth it (Gillijim's Isle has no terrain) |
| 19 | Northrend | n/a | n/a | n/a | n/a | Not in 1.12: no map 571 and no tiles |
| 20 | Interiors (maps 449/450, 13, 25, 29, 35, 42, Old Ironforge) | various | various | WMO floors only | 449/450 used | Quartermaster halls / arenas |

---

## 3. The candidates

### 3.1 Mount Hyjal summit (in use)

- **Map 1, area 616 "Hyjal" (zone 0).** Map tiles cover x 4150..6183, y -4217..-1150.
- **Ground:** a high bowl at z 1450-1700, with its rim at 1700-1830. The usable pockets were measured in
  40-yard cells:
  - The **gate pocket** around (5400..5640, -3790..-3630), z ≈ 1595.
  - The **east terrace** (5520..5712, -3450..-3354), z ≈ 1583, ranges 2-9. This is the flattest.
  - The **mid terrace** (5480..5720, -3110..-2950), z ≈ 1555.
  - The **crater floor** (5360..5490, -2870..-2610), z ≈ 1456-1475 (game_tele `HyjalCrater` 5430,
    -2805, 1463).
- **Access:** teleport only. The courier is in Stormwind and Orgrimmar (`03_construction.sql`). The
  rim drops 600-900 yards to Felwood and Winterspring. Climbers and fliers can get *out*, but they
  can't walk in.
- **Terrain state:** the terrain is finished and has no holes in the bowl. There is one deep shaft or cliff
  of about 490 yd around (5450..5490, -3430..-3510), next to the east terrace. Fence it. The sky box
  and doodads are known to be sparse (the "Blizzard Construction Co." sign) **(verify)**.
- **Hazards:** no graveyard (§1.3). The edges are lethal. Flying off the rim lands in Felwood or
  Winterspring, which is a way out of the district into the open world. There's a pit by the east
  terrace.
- **Use:** stays the neutral homestead capital. Re-lay it as in §4.1.

### 3.2 Azshara Crater (map 37)

- **Map 37 "Azshara Crater"** (client: `PVPZone02`, type 0, PvP flag 1), **area 268** (zone 0). The
  tiles cover x -1067..1600, y -1600..1600. Outside the crater the ground is a flat filler at z -29.6.
- **Ground:** a Y-shaped valley. The floor is at z 230-340 and the walls at 400-560. The best plot
  ground found:
  - North arm (75..315, 600..760), z ≈ 263: 40-yard plots, ranges 3-14.
  - Central floor (150..342, 0..96), z ≈ 238: 24-yard plots, p90 6.
  - The south canyon near the old Horde start (-675..-547, -325..-69), z ≈ 334: 64-yard plots, range
    ≤ 21.
  - game_tele `AzsharaCrater` (128, 135, 236) is on the floor.
- **Access:** only by teleport (a separate map). WorldSafeLocs **49 "Plains of Snow - Horde Start"
  (-662, -485, 386)** and **269 "Plains of Snow - Alliance Start" (-448, -1028, 431)** are on this map,
  so the graveyards already exist in the DBC. They're just not linked in `game_graveyard_zone`.
- **Terrain state:** the terrain is sculpted, with 3 small holes in the index and 1 water sample. 19
  vmap tiles. The 1.12 client ships the ADTs (the extractor produced them). Texture and doodad
  completeness and the sky are **(verify)**. Expect sparse trees and some untextured slopes.
- **Hazards:** the filler outside the rim (z -29.6) is a "floor" a flier could land on and walk around
  outside the playable area. The map's own edges are the world edge. Map PvP flag 1 **(verify the client
  doesn't force PvP flagging)**.
- **Spawns:** none. The map is empty.
- **Use:** **the best second housing district.** It's its own map, so it never collides with the
  classic world. It's big and empty. Its two "start" canyons give it natural Horde and Alliance wings,
  which allows faction-flavoured streets with a neutral market on the central floor. It can later also
  host a **battleground-style map** (it was cut as one) using a separate instance of the same terrain.
  Housing would keep the continent-style map 37, and a BG copy would need a new BG type.

### 3.3 Quel'Thalas placeholder plateau ("the Thalassian Highlands")

- **Map 0.** It is tagged **area 139 Eastern Plaguelands**, because 1.12 has no Quel'Thalas area id.
  Tiles 24,38-24,42 and 23,38-23,41 cover x 3733..4800, y -2666..-5866. North of x ≈ 4800 is open ocean,
  with no-height tiles and z -500.
- **Ground:** a **perfectly flat placeholder plateau at z 129.0** at about x 3725..4400 and
  y -3925..-5375, narrowing to the north. That's roughly 600 x 1400 yards. West of it, around
  (3750..4250, -3000..-3800), is a second flat shelf at z 45.5. Every 40-yard and 64-yard plot tested on
  the main block had a range of **0**.
  - game_tele `QuelThalasShore` (3611, -2820, 177) and `QuelThalasShoreTower` (4296, -2763, 16) lie
    at its west end.
- **Access:** a ridge at z 217-245 along x ≈ 3600-3700 separates it from EPL's north rim (Zul'Mashar,
  Quel'Lithien Lodge at 3339, -4240). At both ends of the ridge, around y -3900..-4150 and
  y -5250..-5400, flat cells reach x 3700, which may be ramps. **(verify** whether a walker can get in.
  Treat it as reachable and fence it.)
- **Terrain state:** flat stamped placeholder heights. Expect untextured or single-texture ground,
  no doodads, and possibly a placeholder sky **(verify)**. There are no holes on the plateau.
- **Hazards:** the north and east edges drop to z -500 no-height ocean tiles (falling through the world).
  The nearest graveyard is 909 Blackwood Lake (2648, -4014), about 1.4 km away, through zone 139's
  links. Mossflayer trolls (8 spawns) live on EPL's north rim, just outside.
- **Use:** **the largest district site on the map.** Flat ground means every plot builds the same. Its
  placeholder look suits a "new settlement on cleared land" theme: high-elf ruins being rebuilt by
  settlers. It is Alliance-leaning (high elves), or neutral if the shelf at z 45.5 becomes a Horde
  wing. Because the terrain is bare, it's also the place where player building does the decorating.

### 3.4 Stonetalon north-west vale (placeholder)

- **Map 1, area 406 Stonetalon Mountains**, and part of it is 2325 "The Veiled Sea" (zone 331).
  Tiles 28,28, 27,27 and 28,27.
- **Ground:** a winding flat valley at **z 131.4 (range 0)** from about (1700..1900, 1400..1600)
  north-east to (2400..2600, 2400..2700). It's 250-400 yards wide and about 1200 long, with walls of
  300-500 yards. A 6 x 4 block of 40-yard plots fits at (1850, 1825) with range 0, and 4 x 2 of 64 yards
  at (1700, 1825).
- **Access:** it's enclosed below Stonetalon Peak. The Peak's Cenarion camp is at z 340-380 around
  (2400..2500, 1800..1900), so a walker would have to drop off the mountain **(verify)**. Teleport
  in.
- **Terrain state:** the same stamped placeholder as Quel'Thalas.
- **Hazards:** the nearest linked graveyard is 409 Webwinder Path (898, 435), 2 km away. The vale's
  western part is in zone 331, Ashenvale, which sends ghosts to Astranaar. The north end opens to the
  Veiled Sea.
- **Use:** the **Horde/goblin district**: a Venture Co. claim turned into a builders' town, reached
  from Orgrimmar or Thunder Bluff. Also a good **open-world construction frontier** (unprotected
  building, raids allowed), because nothing else lives there.

### 3.5 Grim Batol basin (Wetlands)

- **Map 0, area 1037 Grim Batol** (zone 11), bounds about x -4517..-3983, y -3717..-3283. game_tele
  `GrimBatol` (-4074, -3459, 281) and `GrimBatolWatchPost` (-4073, -3380, 372).
- **Ground:** a basin at **z ≈ 231**, at (-4300..-4100, -3550..-3300). A 6 x 4 block of 24-yard plots
  fits at (-4310, -3410) with range 3. A 4 x 3 block of 40 yards fits at (-4320, -3440) with p90 6 and
  max 32. Above it, on the Loch Modan side, is a terrace at z ≈ 390 (-4470..-4310, -3760..-3640).
- **Access:** behind the Dragonmaw Gates, with mountains at z 380-440 on three sides. In 1.12 it's
  closed off **(verify invisible walls)**. Teleport in.
- **Hazards:** the nearest graveyards are 6 Thelsamar (1.3 km) and 7 Wetlands Crossroads. There are
  no spawns.
- **Use:** a **faction stronghold** and small district. Its Alliance flavour is a Bronzebeard
  reclamation of the old dwarven fortress. It's a natural home for a **Kennel / companion breeding
  ground**, because the drakes of Grim Batol fit the companion theme.

### 3.6 Dun Morogh airfield (behind Ironforge)

- **Map 0, area 1 Dun Morogh.** The runway runs about (-5175..-4475, -1730..-1605) at **z 503-505**.
  A lower apron at z 498 is at (-5250..-5000, -1755..-1655), and the north end is at z 509.
  game_tele `OldIronForge` (-4819, -974, 464) is a different place (§3.20).
- **Ground:** a 12 x 3 block of 24-yard plots fits at (-4790, -1700) with max range 8. An 8 x 2 block
  of 40 yards fits at (-4860, -1720) with max 6.
- **Access:** it's ringed by peaks at z 600-830. Players reach it by climbing. Graveyards 852 (Gates of
  Ironforge) and 102 are about 900 yd away.
- **Spawns:** about 25 already: Ironforge Guards, Ironforge Mountaineers, Stormpike Mountaineers
  and rabbits, standing on the runway at z 503-509. These anchor the heights.
- **Hazards:** it's next to a capital. Anything built there is visible from Ironforge's surroundings,
  and flying off the strip lands in Dun Morogh.
- **Use:** **not housing.** It's an **Alliance flight launch point** (a runway for brief flight,
  §11), an Alliance **quartermaster outpost**, and a gyrocopter or glider start. It fits the theme as is.

### 3.7 Gilneas (behind the Greymane Wall)

- **Map 0.** It's tagged **area 2397 "The Great Sea" (zone 267 Hillsbrad)**, because 1.12 has no
  Gilneas area. The Greymane Wall (area 230) is at x -850..-583.
- **Ground:** one enclosed **flat basin at z 54.2** at about (-1210..-1000, 1400..1700), ringed by
  ridges at 100-180. game_tele `Gilneas` (-987, 1580, 54) lands on it. South of x ≈ -1450 there is
  **nothing**: ocean, then no-height tiles at z -500. Gilneas in 1.12 is this one pocket.
- **Fits:** a 4 x 4 block of 40-yard plots at (-1210, 1400) with range 0, or 8 x 6 of 24 yards at
  (-1210, 1410).
- **Access:** sealed by the wall. Teleport in.
- **Hazards:** zone 267's graveyard links send Horde ghosts to Tarren Mill (98) and Alliance ghosts to
  Southshore (149). Southern void.
- **Use:** a **boutique district** of 16-48 plots, with a Gilneas/worgen theme that ties to the
  druid's worgen form. Neutral or Alliance.

### 3.8 Redridge east plateau ("Forbidding Sea islands")

- **Map 0.** It's tagged **2403 "The Forbidding Sea" (zone 8 Swamp of Sorrows)**, at the edge of
  Redridge (44). Flat **z 154** at about (-9250..-9050, -4800..-4500). game_tele
  `TheForbiddingSeaIslands` (-9392, -4855, 301) is on a peak beside it.
- **Fits:** a 4 x 6 block of 40-yard plots at (-9260, -4820), with p90 3 and max 12.
- **Access:** behind Redridge's eastern peaks at z 180-260. Teleport in **(verify)** whether climbers
  reach it.
- **Hazards:** **zone 8's graveyard is 108 Stonard, a Horde town**, so an Alliance ghost would be
  sent there. Link it to Redridge graveyards 2 and 104 instead. To the east, the land drops to
  the sea.
- **Use:** a small Alliance district or outpost. It's a low priority.

### 3.9 Emerald Dream (map 169)

- **Map 169** (server type 2 raid, client type 2). It has 256 tiles covering x -4800..3733,
  y -4267..4267. Only area **1397 "Emerald Forest"** is tagged, at x 1083..3717, y -4250..-1617. The rest
  is untagged.
- **Ground:** mostly flat stamped placeholder at **z 92**, with a few sculpted blocks at 120-130. The
  Emerald Forest is an enclosed valley with a rim of 180-300, a floor at z 92-96 around
  (2600..3400, -1900..-2900), and a lake.
- **Problems:** it's `Instanceable()` (a raid), so building is refused and every raid ID would get its
  own copy. Using it would mean changing `map_template.map_type` to 0 on the server, while the client
  still says raid **(verify the client tolerates it)**. There are no WorldSafeLocs, so no graveyard,
  and no spawns.
- **Use:** later. It's an "endless frontier" or a seasonal map for open-world construction or a
  large PvP mode, once the server is confident with non-classic maps. It isn't a first housing site.

### 3.10 Development Land: Programmer Isle and Designer Island (map 451)

- **Map 451** (type 0). Two 3 x 3 tile corners:
  - **Programmer Isle**, area 22, x/y 15483..17050. game_tele `ProgrammerIsle`/`pisle` (15708, 16574,
    49). Flat pads at z 12, 49 and 69. It has its own graveyards: WorldSafeLocs **529, 669, 670, 671**
    ("Programmer Isle", "Bucklers Cemetery"), linked to zone 22.
  - **Designer Island**, area 151, at x 15483..17050, y -17050..-15483. game_tele `designer`
    (16843, -16114, 220) and `disle`. Flat pads at z 2, 38-44 and 40. It has 10 terrain holes. One
    spawn (Field Repair Bot 74A). Area 2877 is "Chunk Test".
- **Terrain:** developer test terrain: abrupt flat pads, test textures, odd props **(verify)**.
- **Use:** **arenas and firing ranges**, not housing (it looks like a test map). Programmer Isle's
  terraces at three levels and its own graveyards suit a **killhouse or arena hub**, a **gun range**
  for trying weapons and attachments, and a **QA proving ground** for the balance tables.

### 3.11 GM Island (map 1)

- **Area 876**, (16226, 16257, 13). It's an island of about 250 x 150 yd, with sea around it. It has GM
  furniture (16 objects) and a spirit healer. Rejected, as `CONSTRUCTION.md` already says.

### 3.12 Uldum gates (southern Tanaris)

- **Map 1, area 989 Uldum / 990 Valley of the Watchers** (zone 440). game_tele `Uldum` (-9636,
  -2788, 8). It's flat at (-9564..-9444, -2820..-2700), z ≈ 10, range 12.
- **Access:** a walk from Tanaris. Graveyard 209 is linked to zone 440.
- **Spawns:** Prospector Gunstan and Elder Ragetotem (the Uldum quests), plus wildlife.
- **Use:** a **neutral quartermaster outpost** ("Watchers' Gate"), and a staging point for a future
  "beyond the gates" frontier. Don't build housing here.

### 3.13 Tanaris south island (South Seas)

- **Map 1, area 2317 South Seas** (zone 440). game_tele `TanarisSouthSeasIsland` (-11805, -4754, 6).
  Hilly: ranges of 24-74 per 120 yards. 13 spawns (Southsea pirates).
- **Use:** a pirate-themed **PvP skirmish island** or killstreak test arena. It isn't good for housing.

### 3.14 Caverns of Time entrance, and map 269

- **Map 1, area 2300** (zone 440). Flat at (-8288..-8168, -4682..-4562), z ≈ 1. The AQ-event NPCs
  (Anachronos, Chronalis) stand here. Leave it alone.
- **Map 269** ("CavernsOfTime", instance) has 39 tiles of **Old Hillsbrad and Black Morass** early
  terrain in 1.12 (areas 2366 and 2367 are tagged). It's an instance, so it's not for housing. It's a
  later idea for an instanced scenario or PvP map.

### 3.15 Karazhan surroundings (Deadwind Pass)

- **Map 0, area 2562 Karazhan / 41 Deadwind.** The best flat spot is (-11114..-10994, -2076..-1956),
  z ≈ 40, range 37. There are 80 spawns (ogres, ghosts), and Deadwind's graveyard 36.
- **Use:** an **open-world construction frontier or contested PvP ground** (unprotected building,
  raids on). It's too busy and too steep for housing.

### 3.16 Silithus north

- **Map 1, zone 1377.** There's big flat sand (range < 10 over 200 yards) at about
  (-6600..-6400, 360..750), z 2-7. But it's Hive'Ashi and Twilight territory, with 129 spawns.
  None of it is really unused in 1.12, since the AQ content filled it.
- **Use:** an **open-world frontier** for desert raids on player bases. It's not for housing.

### 3.17 Ashenvale and Felwood edges

- **Hyjal's Felwood ledge.** game_tele `HyjalJump` (5234, -1220), z 1364. `HyjalCaveInside` and
  `HyjalCaveOutside` are at (4857, -1791) and (4823, -1750), z 1156-1285. These are high ledges in
  area 616 that look down on Felwood at z 380-630, about 700-900 yards below. They're **flight launch
  points** for brief flight (§11): a glide down into Felwood. They're too steep for plots.
- **Bough Shadow** (area 438, Ashenvale) is in use: it's an Emerald Dragon portal.
- **Use:** launch points only. Each one needs a landing zone and the altitude ceiling of §5.4.

### 3.18 Unused islands

- **Gillijim's Isle** (area 408, South Seas): its area id exists, but **every tile is no-height
  ocean**. There's nothing there in 1.12.
- **Alcaz Island** (2079, Dustwallow) has 62 spawns (Weavil, naga). **Sardor Isle and the Isle of
  Dread** (Feralas) are Feathermoon and in use. **Jaguero Isle** (STV) has King Mukla. **Ranazjar
  Isle** (Desolace) has naga.
- **Verdict:** not worth it. They're used, hilly and small.

### 3.19 Northrend

- It isn't in 1.12: there's no map 571 and no tiles. Out of scope.

### 3.20 Interiors and test maps

- **Maps 449 and 450, "Alliance/Horde PVP Barracks"** (type 0): the Hall of Legends and Champion's
  Hall interiors, which already hold the PvP rank vendors (11 and 12 spawns). They're natural
  **faction quartermaster halls** for kits and killstreak rewards.
- **Maps 13 "Testing", 25 "Scott Test", 29 "CashTest" and 42 "Collin's Test"** have vmaps only (WMO
  maps with no terrain tiles). game_tele `ScottTest` (0, 0, -100.5) is on map 13. Each could become a
  **small arena or killhouse** if its WMO floor is usable **(verify each one in the client)**.
- **Map 35 "<unused> StormwindPrison"** (type 0) is an interior arena candidate **(verify)**.
- **Old Ironforge** (game_tele `OldIronForge` -4819, -974, 464) is a hidden WMO space inside
  Ironforge. It's a possible Alliance quartermaster vault.

---

## 4. Recommended shortlist for homestead districts

In priority order:

| # | Site | Plots (draft) | Faction flavour | Courier from |
|---|---|---|---|---|
| 1 | **Hyjal summit** (re-laid) | 32 S + 24 M + 8 L = 64 | Neutral capital | Stormwind, Orgrimmar (exists), + Ironforge, Undercity, Darnassus, Thunder Bluff |
| 2 | **Azshara Crater** (map 37) | 32 S + 24 M + 8 L = 64 to start, room for ~2x | Horde wing (south canyon) / Alliance wing / neutral floor | Orgrimmar and Stormwind; later every capital |
| 3 | **Quel'Thalas plateau** | 64 S + 96 M + 16 L = 176 | Alliance-leaning (high elves); a Horde shelf at z 45 later | Ironforge, Stormwind, Light's Hope Chapel |
| 4 | **Stonetalon NW vale** | 24 M + 8 L (+ more along the strip) | Horde / goblin | Orgrimmar, Thunder Bluff |
| 5 | **Grim Batol basin** | 24 S + 12 M | Alliance (dwarves) stronghold + Kennel | Ironforge |
| 6 | **Gilneas basin** | 16 M or 48 S | Neutral / worgen | Undercity and Southshore |
| 7 | **Redridge east plateau** | 24 M | Alliance | Stormwind, Lakeshire |

The Dun Morogh airfield, Uldum gates, Programmer Isle and Karazhan go to the other uses (§6). They are
not districts.

### 4.1 Hyjal summit: revised layout (top priority, fixes §1.1)

All of these were measured. The ranges are per plot, in yards.

| District | Origin (x, y) | Grid | Extent | Centre z | Range p90 / max |
|---|---|---|---|---|---|
| Small, 24 yd | **(5520, -3450)** (east terrace) | 8 x 4 = 32 | 192 x 96 | 1583.5 | 6 / 9 |
| Standard, 40 yd | **(5400, -3790)** (gate pocket) | 6 x 4 = 24 | 240 x 160 | 1595.3 | 16 / 29 |
| Large, 64 yd | **(5360, -2870)** (crater floor) | **2 x 4** = 8 | 128 x 256 | 1456.0 | 24 / 26 |

- The ids stay the same (1-32, 101-124, 201-208). Only the origins move, and the large grid changes
  from 4 x 2 to 2 x 4, so `Plots::Default()` changes and no plot id is renumbered. **Any plot already
  bought must be packed (100% reclaim) or moved by a GM before the switch.**
- The standard district now covers the old gate (5476, -3729). Move the gate hub (steward, courier,
  trainer, supplier, `game_tele 9101`, `kHomestead*`) about 15 yards west, to **(5385, -3710, z 1593.8)**.
  That ground is flat (z 1593-1616 in a 35 x 80 yard box).
- The walks: from the gate to the small district is about 300 yards, and to the large district on the
  crater floor about 900 yards, down roughly 140 yards of height. Add a second courier stop or a short
  "lift" NPC at the crater ((5430, -2805, 1463), game_tele `HyjalCrater`).
- An alternative standard site with no gate move is (5480, -3110) (mid terrace, p90 21, max 35).
- **Map safety:**
  - Fence the shaft at (5450..5490, -3430..-3510), right beside the small district.
  - Add a soft altitude ceiling at about z 1700 over the districts, and a hard horizontal bound at the
    rim (about x 5300..5800, y -3850..-2600) so brief flight can't glide down into Felwood or Winterspring.
  - Link a graveyard (§5.3).

### 4.2 Azshara Crater (map 37): draft layout

| District | Origin (x, y) | Grid | Extent | Centre z | Range p90 / max | Flavour |
|---|---|---|---|---|---|---|
| Standard, 40 yd | **(75, 600)** (north arm) | 6 x 4 = 24 | 240 x 160 | 262.9 | 11 / 14 | Neutral "Crater Commons" |
| Small, 24 yd | **(150, 0)** (central floor) | 8 x 4 = 32 | 192 x 96 | 238.2 | 6 / 24 | Neutral market lots |
| Large, 64 yd | **(-675, -325)** (south canyon) | 2 x 4 = 8 | 128 x 256 | 334.2 | 21 / 21 | Horde wing, by the old Horde start |

- Plot ids would be a new block, for example 301-332, 401-424 and 501-508.
- The deed text needs to name the district. Today it's hardcoded "on the Hyjal summit".
- **Arrival:** at the crater centre (128, 135, 236) (`game_tele AzsharaCrater`). Put the steward and
  courier hub there.
- **Graveyards:** add `game_graveyard_zone` rows (49, 268, Horde) and (269, 268, Alliance). The safe
  locs already exist on map 37, but they sit at z 386 and 431 at the canyon ends, so a neutral one by
  the hub would need a server-side `WorldSafeLocs.dbc` row (§5.3).
- **Growth:** the floor between (-400..400, -300..900) holds roughly the same again. A matching Alliance
  wing goes in the canyon toward WorldSafeLocs 269 (-448, -1028), with its layout to be measured.
- **Map safety:**
  - Add a hard bound to the crater's interior: about x -800..700, y -1100..1450, following the rim.
  - Set an altitude ceiling at about z 600, over the rim at 450-560.
  - Add a server check that teleports anyone below z 0 (the filler at -29.6) back to the hub.
  - Confirm the map's PvP flag doesn't auto-flag players in the client.

### 4.3 Quel'Thalas plateau: draft layout

All three blocks measured range 0 for every plot:

| District | Origin (x, y) | Grid | Extent | z | Note |
|---|---|---|---|---|---|
| Standard, 40 yd | **(3750, -4625)** | 12 x 8 = 96 | 480 x 320 | 129.0 | Main town |
| Small, 24 yd | **(3750, -4295)** | 16 x 4 = 64 | 384 x 96 | 129.0 | 10-yard road north of the standard block |
| Large, 64 yd | **(3750, -4891)** | 4 x 4 = 16 | 256 x 256 | 129.0 | 10-yard road south of the standard block |

- Arrival is at the plateau's south-west corner, about (3740, -4300, 129), between the blocks.
- **Couriers** from Ironforge and Stormwind, plus one at Light's Hope Chapel for the EPL players.
- **Map safety:**
  - Fence the north and east edges, where the land drops to z -500 no-height ocean at x ≈ 4400-4800.
  - Add server-side "below z 0: return to the hub".
  - Decide whether the ridge gaps to EPL at y ≈ -3900..-4150 and -5250..-5400 stay open (a walkable
    "pass", nice for the lore) or get fenced. If they stay open, raids and EPL mobs can path in. The
    district rules already stop raids within 80 yards.
  - Set an altitude ceiling at about z 260, since the ridge is 217-245.
  - Graveyards: zone 139 already resolves to Blackwood Lake (909). Prefer a new server-side safe loc on
    the plateau.

---

## 5. Integration work any new district needs

### 5.1 Code (FusionConstruction / Plots)

- **Several gates.** Replace the single `kHomestead*` constant with a gate per district (map, x, y, z,
  o). The courier's gossip lists the districts, and "Take me home" goes to the gate of the district
  that holds the player's plot (`Plots::DistrictOf`).
- **The upkeep sweep** must not depend on `player->GetMapId() == kHomesteadMap`. Run it from the
  world update, or on any map that has districts.
- **Deeds and steward text** must name the district (`District` gains a name or label id).
- **Maps other than 0 and 1:** check that `FusionMgr`'s piece persistence and loading by map, and the
  visibility updates, work on map 37 or 451 (they're keyed by `map->GetId()`, so they likely do;
  **verify** by building one piece there).

### 5.2 World DB (one SQL file per district, in the style of `03_construction.sql`)

- Steward, courier and supplier spawns at the gate. Couriers in the capitals. A `game_tele` entry for
  each district.
- `game_graveyard_zone` links (§5.3).

### 5.3 Graveyards

- Hyjal (616), Azshara Crater (268), Designer Island (151) and map 169 have no linked graveyard. With
  no link, `GetClosestGraveYard` returns nullptr and the ghost has nowhere to go.
- **Cheapest fix:** link an existing safe loc. For map 37, link 49 and 269. For Hyjal, the nearest is
  511 Everlook, 1.5 km away and below the summit, which is a poor fit because the corpse run can't walk
  back up.
- **Proper fix:** add WorldSafeLocs rows to the **server's** copy of `WorldSafeLocs.dbc` (in
  `data/5875/dbc`). The client keeps its own copy and only needs it for the ghost arrow **(verify)**.
  Or have the fusion code send a ghost who dies in a district straight to its gate's spirit healer.

### 5.4 Map safety for brief flight (GAME_DESIGN §11)

- **Per-district bounds:** an axis-aligned box or a polygon around the rim, plus a ceiling z. Leaving it
  ends the flight (drops the player), and below the "void floor" teleports them to the gate.
- **Void floors to guard:**
  - Map 37 filler at z -29.6.
  - Quel'Thalas, Gilneas and Hyjal's surroundings: no-height tiles at z -500.
  - Map 169 filler at z 92 (it's walkable but untagged).
- **Escape routes to fence**, so a flier can't leave a protected district into the open world:
  Hyjal → Felwood and Winterspring, the airfield → Dun Morogh, Quel'Thalas → EPL, and Stonetalon vale →
  the Peak.
- The suggested ceilings are in §4.1-4.3. Elsewhere, use rim height + 40.

---

## 6. Other uses

| Use | Site | Why |
|---|---|---|
| **Flight launch points** | Dun Morogh airfield runway (-4790, -1660, 504); Hyjal's Felwood ledge (5234, -1220, 1364); the Quel'Thalas ridge (z 217-245) | Long flat runway or high ledges with safe landing ground below |
| **Faction strongholds** | Grim Batol basin (Alliance), Stonetalon vale (Horde), the two Azshara Crater canyons | Enclosed, empty, defensible |
| **Companion breeding grounds (Kennel)** | Grim Batol (drakes), Emerald Forest valley on map 169 later (dream beasts) | Thematic, empty |
| **Quartermaster outposts** | Uldum gates (neutral), airfield (Alliance), maps 449/450 halls (both factions' PvP quartermasters), Old Ironforge vault | Existing NPC hubs or interiors |
| **Arenas / killhouses / gun range** | Programmer Isle (map 451, has its own graveyards), Designer Island, the test-map WMOs 13/25/42, Tanaris south island | Out of the world, flat pads, or small and enclosed |
| **Battleground-style map** | Azshara Crater (map 37) as a BG copy, later | Built for it, with start points in the DBC |
| **Open-world construction frontier** (unprotected, raids on) | Stonetalon vale, Karazhan / Deadwind, Silithus north, Emerald Dream later | Empty or contested ground away from cities |

---

## 7. Still to verify in game

1. Client rendering of the map 37 floor, the Quel'Thalas plateau, the Stonetalon vale, the Gilneas
   basin and the Redridge east plateau: textures, sky, and whether a player can stand there without
   falling through. The server heights are fine, but **WMO and M2 collision and client-side walls were
   not checked**.
2. Whether walkers can enter Quel'Thalas through the ridge gaps, and the Stonetalon vale from the Peak.
3. Whether the map-37 PvP flag affects the client.
4. Before re-laying Hyjal, which Hyjal plots are already owned.
5. The server-side `WorldSafeLocs.dbc` additions against the client's ghost UI.

The scripts used for this survey (a `.map` parser, slope and plot-grid search) were scratch tools and
were not added to the repo. They can be rebuilt from §0 if the numbers need re-running after a move.
