# Later-version art: the WotLK patch layer

The owner decided to ship models from later WoW versions. This is how they get into the game.
Code: `client/src/wotlk/`. Nothing it makes is committed (private-assets.md).

## Pipeline

1. **Read.** The WotLK 3.3.5a (build 12340) archives are format V1, zlib-sectored with sector
   CRCs, none encrypted or single-unit: benilla's own MPQ reader reads them unchanged
   (`wotlk::source`). Only the stock archives are read (common, common-2, expansion, lichking,
   patch, patch-2, patch-3 and the enUS locale chain); the extra HD/mod `patch-?.mpq` files of the
   owner's install are left out.
2. **Convert.** An offline down-port of M2 version 264 to the 1.12 layout (256), not a 264 loader
   in benilla: benilla's rule is that it reads 1.12 data the way 1.12 does, and every reader that
   touches an M2 (mesh, bones, clips, particles, ribbons, lights, cameras, bounds) keeps working on
   a converted file with no change. `wotlk::m2conv` lays the per-sequence key lists end to end on
   one timeline, reads external `.anim` files, expands compressed quaternions, embeds the `.skin`
   files, regrows every record that embeds a track, samples particle colour/alpha/scale/cell ramps
   into 1.12's three points, rebuilds `PlayableAnimationLookup` from 1.12's `AnimationData`
   fallbacks, and maps blend mode 7 to additive. Textures (BLP2) are copied as they are.
3. **Tables.** `wotlk::import::LOOKS` copies 3.3.5 `CreatureDisplayInfo` rows under new display
   ids 40001+ (models 4001+); sound, blood and footprint rows come from a 1.12 donor display. The
   1.12 `CreatureDisplayInfo`, `CreatureModelData` and `Map` tables are written with the rows added.
   A path 1.12 already has is never overridden.
4. **Pack.** `cargo run --release --example wotlk_import` writes
   `private-assets/womw-patch/patch-9.MPQ` (plus `server-dbc/` and `report.txt`). `patch-9`
   mounts above every stock 1.12 archive in benilla's chain, and the server's extractors read
   it too (see Server data).
5. **Ship.** `package/build.ps1` copies the archive into `game\WoW\Data` and the tables to
   `womw-patch\server-dbc` (and `data\5875\dbc` when present); `aw.ps1` lays them over the
   server's extracted tables at every start.

## Companions

`33_wotlk_looks.sql` (mirrored by `kWotlkLooks` in `Companions.cpp`) gives these forms the new
displays: raptor hatchlings (lines 12, 112-114, stage 1), baby crocolisks (13, 117), hawkstrider
hatchlings (17, 129), phoenix hatchling and phoenix (19), proto-drakes (whelp lines 2-4, stage 3),
armored bears (10, 208 Poley), spectral tiger (213). Displays 40007 (black proto-drake), 40012 (black armored bear) and 40016 (riding turtle) are
built but unused: the first two break the catalog's scale rules on their lines (scale >= 0.15,
ridden forms at 1), and 1.12 already has the riding turtle. The client test
`the_new_forms_keep_the_size_rules` holds the scales to the look table's size rules.

Displays 40019-40037 (`35_flying_companions.sql`, design/COMPANION_CATALOG.md section 1g) are the
flying mounts' lines: the 3.3.5 gryphon, wind rider, dragonhawk, netherwhelp, proto-drake whelp and
frost wyrm (Frosty) young, a dragonhawk, nether drake, violet proto-drake and frost wyrm, and the
riding models (armored gryphon, tawny wind rider, blue hippogryph, red dragonhawk, purple nether
drake, red proto-drake, white frost wyrm, Ashes of Al'ar phoenix, bronze drake). Measured at the
display's scale: the riding models 9.2-26.4 yd (the proto-drake's box 46.7), the young 1.4-7.0.
`the_flying_lines_ride_their_riding_models` checks each line rides its riding model at scale 1.

## Dalaran (Alterac Mountains, map 0)

Wrath's full city stands where classic Dalaran is, replacing the violet dome (`wotlk::dalaran`,
`wotlk::adt`):

- **What 1.12 has there:** one M2, `dalarandome.mdx`, listed in tiles 30_31 and 31_31; the crater
  is plain terrain (a flat floor at Z 41.9) with farm ruins around it.
- **Removed:** the dome, and every doodad or WMO placement inside the city's footprint (the
  footprint is the city's triangles rasterized on a 2-yard grid).
- **Placed:** `ND_Dalaran.wmo` (version 17, root and 91 groups, loads through benilla's WMO reader
  unchanged; 454 doodads converted, 620 already in 1.12) with its WotLK heading (-219 degrees),
  the box centre over the dome's centre, the city's outer rim ledge (the lowest up-facing
  surface, local Z about -10) on the crater floor: streets at about 51-57, the Northrend arrival
  floor (in the citadel) at Z 71. (The first splice put the arrival at 45, which sank the streets
  15-30 yards into Alterac.) One MODF in every
  tile it reaches, the same unique id, referenced from each chunk under it (MCRF).
- **Terrain:** a plateau half a yard under the city's lowest walkable surface (the rim ledge,
  the streets; roofs and the jutting landing do not count), so the floating island's rock is
  buried and nothing pokes through; eased back to the stock land over a 60-yard ring (smoothstep),
  so the land meets the rim ledge and Lordamere's shore moves out to the ring's foot (the water
  under the plateau is hidden). Doodads and WMOs in the ring ride the new ground (nature only: farm ruins, scaffolds, fences and carts within the ring
  and 15 yards past it go); inside the
  footprint they go. Normals tilted by the change only, baked shadow cleared where it moved.
- **Portals:** `34_dalaran.sql`, archmages in Stormwind and Orgrimmar to `0, 297.12, 293.69, 71.00`
  (facing 1.663) and one in the city back. Map 800 is gone: ids 800+ stay free.
- **Gate and people:** `37_dalaran_city.sql`. You walk up onto the rim ledge from any side, but no
  street reaches the city's outline (it is walled all round; `dalaran_probe walk` floods the floors
  from the arrival), so a Kirin Tor gatekeeper on the east rim (`516, 360, 41.1`) sends you in to
  the arrival floor and one in the south court sends you back out;
  flyers can land in the squares. Round the market square (about `265, 555, 71.3`): an innkeeper,
  two bankers, four merchants, a mage trainer and six profession trainers, copies of stock 1.12
  templates (faction 35); Kirin Tor guardians on both squares. The old crater's spawns inside the
  city's outline and the ring are saved to `aw_orig_dalaran_*` and removed. Every gossip teleport
  sets data_flags 1: command 6 moves the script's source, which for a gossip script is the NPC
  (without it the Stormwind archmage teleported itself away). The client test
  `the_city_spawns_stand_on_its_floors` holds every spawn to a city floor or the open ground.

**Server data.** The tiles change the server's maps, vmaps and (if shipped) mmaps:

1. MapExtractor now opens `patch-3.MPQ` and `patch-9.MPQ` after the stock archives
   (`server/contrib/extractor/System.cpp`); VMapExtractor already scans `patch-2`..`patch-99`.
2. `aw.ps1` (`Update-WomwMapData`, at start and setup) stamps `data\womw-patch.stamp` with the
   archive's size and time. When the archive is new or changed it deletes `maps`, `vmaps` and
   `5875\dbc`, extracts again, and if `data\mmaps` exists rebuilds tiles 30-32 x 30-32 of map 0
   (`MoveMapGenerator 0 --tile x,y --silent` in the data folder).
3. By hand, the same: stop the server; in a folder with a `Data` junction to the packaged client,
   run MapExtractor, VMapExtractor, `VMapAssembler Buildings vmaps`; replace `data\maps`,
   `datamaps`, `data875\dbc`; then the nine MoveMapGenerator tiles if mmaps are used.

Not done: a flight master (needs TaxiNodes rows), zone names inside the city (the chunks keep 1.12's area
ids), the city's sounds and lights tuning.

## Needs an in-game look

Every new companion form (model, skin, animation, size), the proto-drakes' wings and the phoenix's
fire particles; Dalaran: does it render whole and sit on the ground (the rock base meeting the reshaped
terrain, the blend ring into the mountains), are doodads placed and animated right, can you walk
the streets and leave the city on foot, do the portals land on the floor, do the three portal NPCs
stand right, and is nothing of the old dome or ruins left inside.
