# Northrend as a bridge: seamless travel between the continents (design)

Owner: "If we were to do the full Northrend continent, I think it would be cool to figure out how to
let players go seamlessly between the continents, with Northrend acting as a bridge between the two
other continents, no loading screen."

Status: design only. Nothing here is built. It assumes the whole of Northrend is imported from the
owner's 3.3.5a install (design/WRATH_IMPORT_PLAN.md, P3 "Northrend as a new continent"), which is
itself not done: the patch layer (design/WOTLK_ASSETS.md) converts models and places Wrath's
Dalaran over classic Dalaran in Alterac (two map 0 tiles), not a Northrend terrain continent.

## 1. How 1.12 and vmangos separate the continents today

**Client data.** A continent is one map: `Map.dbc` row, a `World\Maps\<Dir>\<Dir>.wdt` and up to 64 x
64 ADT tiles (`<Dir>_<x>_<y>.adt`), each 533.33 yd square (16 x 16 chunks of 33.33 yd). The map's
coordinate space is therefore 34,133 yd across, from -17,066 to +17,066 on each axis, with the map
origin in the middle of the grid. Eastern Kingdoms is map 0 (`Azeroth`), Kalimdor map 1; Wrath's
Northrend is map 571.

Measured from the WDTs' `MAIN` grids (the owner's installs; rows run north to south, columns west to
east):

| Map | Tiles with an ADT | Columns | Rows | Bounding box |
|---|---|---|---|---|
| 0 Eastern Kingdoms (1.12) | 687 | 24-44 | 20-61 | 21 x 42 |
| 1 Kalimdor (1.12) | 1018 | 19-50 (+ a 3 x 3 test corner at 0-2) | 9-55 | 32 x 47 |
| 571 Northrend (3.3.5) | 1131 | 11-49 | 9-37 | 39 x 29 (a full rectangle: the sea around it is tiles too) |

**Server.** vmangos keeps one `Map` object per continent (`MapManager`; instances are more `Map`s).
Each continent map is a 64 x 64 grid of `GridMap`s (`MAX_NUMBER_OF_GRIDS` 64, `SIZE_OF_GRIDS`
533.33, `CENTER_GRID_ID` 32 in `Maps/GridDefines.h`), each grid 16 x 16 cells, loaded and unloaded
around players. Per map it reads the extracted `maps/MMMXXYY.map` height/liquid files, the vmaps
(`vmaps/MMM_XX_YY.vmtile`, line of sight and indoor checks) and mmaps (`mmaps/MMMXXYY.mmtile`,
creature pathing). Visibility is per map (`DEFAULT_VISIBILITY_DISTANCE` 100 yd on continents): a unit
on map 1 never exists for a player on map 0. Every spawn, waypoint, area trigger, graveyard, taxi
node, and many scripts' hard-coded positions are stored as (map, x, y, z).

**A map change is a loading screen by protocol.** `Player::TeleportTo` to another map is a far
teleport: the server sends `SMSG_TRANSFER_PENDING` and `SMSG_NEW_WORLD`, removes the player from
the old `Map`, and waits for the client's `MSG_MOVE_WORLDPORT_ACK`
(`WorldSession::HandleMoveWorldportAckOpcode`) before adding it to the new one and sending its object
updates. The stock client shows the loading screen from `SMSG_TRANSFER_PENDING` until the new map's
terrain is ready. Boats and zeppelins between the continents (Menethil-Theramore,
Undercity-Orgrimmar...) are transports whose path crosses maps: at the path's map switch every
passenger is far-teleported, with a loading screen, mid-ocean. Flight paths never cross maps
(`TaxiNodes` carry a map id; a path's nodes are on one map).

**Our client.** benilla already streams terrain: `benilla-world`'s `TerrainStreamer` loads the ADT
tiles in a window around the player, spawns their cells, colliders, placements, water and
clutter, and publishes residency to the loading screen (`benilla-app/src/loading_screen.rs`), which
rises on `SMSG_TRANSFER_PENDING` and clears when the destination is scene-presentable. The streamer
owns one map at a time: a new map directory drops every tile.

## 2. Where Northrend would sit

The lore and the 3.3.5 boat routes put Northrend north of both: the Howling Fjord faces the Eastern
Kingdoms (Menethil to Valgarde, Undercity to Vengeance Landing), the Borean Tundra faces Kalimdor
(Stormwind to Valiance Keep, Orgrimmar to Warsong Hold; Stormwind sits in the east, but the tundra is
the western landing). The layout to aim for:

```
          +---------------------- Northrend ----------------------+
          | Borean Tundra  ...  Dragonblight  ...  Howling Fjord  |
          +-------------------------------------------------------+
            ^ sea lane (west)                    sea lane (east) ^
   +---- Kalimdor ----+                      +- Eastern Kingdoms -+
   | Winterspring ... |    (Great Sea)       | Tirisfal, Quel'... |
   +------------------+                      +--------------------+
```

So a traveller from Lordaeron sails north into the Howling Fjord, crosses Northrend overland
(Grizzly Hills, Dragonblight, Borean Tundra) and sails south from the tundra to northern Kalimdor:
Northrend is the land bridge, and the open sea between the two old continents keeps its boats.

## 3. Options

### A. One combined map: stitch the three continents into one map id

Put all three continents' ADTs into one WDT (a new map id, or map 0 grown), each continent at a tile
offset, and translate every coordinate in the world database by its continent's offset.

**Does it fit in 64 x 64?** No, not at true scale. By tile count it would (2,836 of 4,096), but by
shape it does not: the two old continents side by side need 21 + 32 = 53 columns, and Northrend on
top needs 29 rows over the 47 of Kalimdor's: 76 rows, against 64. Northrend between them in one row
needs 21 + 39 + 32 = 92 columns. Cropping Northrend's open sea might save it only
barely: under Kalimdor's 47 rows 17 are left (about 9,000 yd north to south) for Northrend's land
and the sea lanes between, with no gap of open water left; that would need measuring tile by tile,
and the continents would touch like puzzle pieces rather than lie apart across a sea. It fits only by dropping content (say only Northrend's southern zones)
or by growing the grid past 64 x 64, which both clients' formats and the server assume:

- Client: the WDT `MAIN` chunk is 64 x 64 by format; the ADT file names, `WorldMapArea.dbc`, the
  minimap's `md5translate` table, the world map's continent frames and the 1.12 coordinate math
  (`(32 - tile) * 533.33`) all assume it. benilla could take a new "grid page" format (we own it),
  the stock 1.12 client never could (no longer a constraint for us, but every tool the team uses,
  map viewers and extractors, would need the same change).
- Server: `MAX_NUMBER_OF_GRIDS` is a compile-time array size in `Map`, `TerrainInfo` and the grid
  state machine; the extracted file names hold two digits per grid coordinate; Detour's mmap tile
  ids and the vmap tree are built per 64 x 64 map. Growing it is a fork-wide change plus a re-extract
  of every map (maps, vmaps, mmaps) in the new layout.
- Data: every row with a position (creature, gameobject, waypoints, `areatrigger_teleport`,
  graveyards, taxi nodes and paths, quest POIs, pools, event spawns, our own fusion tables) and every
  C++ script with a hard-coded position (boss encounters, escorts) must be translated, for all three
  continents. The script positions are the dangerous part: they are scattered literals.

Effort XL, risk very high, and it couples three continents' terrain, spawns and scripts forever. It
also puts every player of the realm on one `Map` object, which vmangos updates on one thread per map:
today the continents update in parallel.

### B. Keep the maps separate, make the transitions seamless (client pre-streaming + server handoff)

Keep maps 0, 1 and 571 exactly as they are (spawns, scripts, extracted data untouched). Add
**seams**: a small table, shared by server and client, of (map A, region on A) <-> (map B, region on
B) with the translation that takes A's coordinates to B's (an offset, same orientation). A seam is
placed where both sides are empty and alike: open sea (the two sea lanes of section 2), or a flight
corridor high over the water.

- **Client: neighbour-map streaming.** Near a seam, benilla streams the far map's tiles too, drawn at
  the seam's offset, so the coast across the water is really there. This is a second
  `TerrainStreamer` residency (the streamer is one-map today), low-detail beyond the near tiles,
  with no collision needed until the crossing. The world map and minimap can show the far continent
  the same way.
- **The crossing.** When the player passes the seam line, the server far-teleports them to the
  translated position on map B (orientation, speed and mount kept). The client receives the same
  `SMSG_TRANSFER_PENDING` / `SMSG_NEW_WORLD`, but since map B's tiles around the destination are
  already resident it does not raise the loading screen: it promotes the neighbour residency to the
  primary map, re-bases its coordinates by the seam's offset (camera, avatar, effects), and keeps
  moving the avatar locally while the server waits for `MSG_MOVE_WORLDPORT_ACK`. The old map's units
  are destroyed and map B's created by the normal object updates. At riding speed a 100-200 ms
  handoff is 1-3 yards: the server accepts the client's next position with a short anticheat
  exemption, as it does after a knockback.
- **Server.** A seam check in the movement handler (or area-trigger boxes along the seam), the
  teleport with the translation, combat rules (crossing in combat: refuse, or drop combat as a
  far teleport does today), and the things a far teleport already carries: pets and our companions
  (re-summoned on arrival), group, auras, the riding state.
- **What stays visible across a seam.** Nothing: map B's units do not exist for a player on map A.
  Seams in the open sea make that harmless (with visibility at 100 yd, a seam 300 yd from any coast
  hides nothing a player could see). Later, if wanted, the server could mirror units within range of
  a seam onto the other map as display-only proxies.
- **Boats and zeppelins.** The 3.3.5 routes (section 2) become transports that cross a seam: the
  transport's own map switch is already there in vmangos; with the client's pre-streamed destination
  it no longer shows a loading screen either. This is the cheapest part and works even before
  walk-over or fly-over seams exist.
- **Flight paths.** A flight from Lordaeron to Dragonblight is two taxi paths joined at a seam (a
  path never crosses a map); the server chains them with the seamless handoff between.
- **Pathing and vmaps.** Untouched: each map keeps its own extracted data. Creatures never cross a
  seam; a chaser evades at the seam line (seams in empty water make this rare).

Effort L (client: the second residency, re-basing, the loading-screen bypass; server: the seam
table, the check and the handoff), risk medium: the risky part is the client's re-base being
glitch-free, and it can be built and tested on one seam at a time.

### B'. The cheap first step: soft seams on transports only

Before any walk-over seam: let the transports and flight paths that change maps pre-stream their
destination during the trip and skip the loading screen on arrival (a short in-world fade if the
destination is not ready in time). No seam table on the server, no coordinate re-basing: the voyage
itself hides the map change. Effort S-M, risk low.

### C. A bigger single map later, if ever

If seamless crossings ever prove not enough (for example free flight across the whole world with
everything visible), option A with a grown grid is the only path. It should be judged then, with
the data-translation tool and the script audit costed first.

## 4. Effort and risk

| Option | Client | Server | Data | Effort | Risk |
|---|---|---|---|---|---|
| A. One stitched map (needs > 64 x 64) | new grid format, map UI, minimap | grid size, file formats, re-extract all maps | translate every position and script literal | XL+ | very high |
| B. Separate maps, seamless seams | second residency, re-base, loading-screen bypass | seam table, check, handoff, anticheat window | none (one seam table) | L | medium |
| B'. Transports and flights only | pre-stream destination, skip loading screen | none (vmangos already switches maps) | 3.3.5 routes as transports | S-M | low |

All options need Northrend itself first (section 5, phase 0), which is the largest single piece.

## 5. Recommendation and phases

Recommendation: **option B, reached through B'**. It gives the owner's "no loading screen" at every
place players actually cross (the sea lanes, the boats, the flights), keeps vmangos' per-map world,
spawns, scripts and extracted data untouched, and every phase ships on its own.

0. **Northrend as map 571, with loading screens.** benilla reads 3.3.5 ADTs (MH2O water, MCCV
   vertex colours, the 8-bit "big alpha" layers, the newer MCNK flags; the WMOs and converted M2s
   already load), the server's extractors read them for maps, vmaps and mmaps, `Map`, `AreaTable`,
   `WorldMapArea`, `LoadingScreens` rows; spawns and a first set of zones (WRATH_IMPORT_PLAN.md P3).
   Ends with the 3.3.5 boats and zeppelins running to Valgarde, Vengeance Landing, Valiance Keep
   and Warsong Hold, with loading screens.
1. **Soft seams (B').** The client pre-streams the destination of a map-changing transport or taxi
   and skips the loading screen when it is resident. Measure the handoff time on the four routes.
2. **The seam table and the client's neighbour residency.** Both continents' coasts visible across
   the two sea lanes; no crossing yet (an invisible wall at the seam). Verifies the offsets.
3. **Walk-, swim- and ride-over seams.** The server's seam check and handoff, the client's re-base
   and loading-screen bypass, the anticheat window, the combat rule. One seam first (the eastern
   lane), then the western.
4. **Fly-over seams and chained flights.** Flight corridors across the seams for the flying mounts
   (today a short glide, see the flying companions; free flight is its own project), taxi paths
   chained across seams.
5. **Optional.** Display-only proxies of units near a seam; the world map drawing the three
   continents as one picture.

Not recommended: option A. Revisit only if phase 3 shows the handoff can't be made invisible.
