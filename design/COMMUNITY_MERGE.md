# Community zones: the open-world merge (PRIVATE)

Private notes, like `COMMUNITY_IMPORT_INVENTORY.md`: never in the public tree
(`tools/public-exclude.txt`), and never name the community client's developers anywhere public.
Code, commits and public docs say "the community client" and "its repack".

Owner decisions (Oct 2026): merge the community client's open-world zones on Azeroth (0) and
Kalimdor (1) into our world; **our Alterac tiles win** (map 0, tiles 30-32 x 30-32: Wrath's Dalaran,
`client/src/wotlk/dalaran.rs` `TILES`); keep their maps 800-822 (their `Map` rows win over ours
there); renumber their colliding ids; our own archive names.

## What the community tiles are (survey, Oct 2026)

`community-merge survey` over their content patches `patch-3`..`patch-9` (their HD pack
`patch-A`..`patch-Y` is left out) against our stock `Data`:

| | tiles | new | changed | MH2O | MCCV | big alpha |
|---|---:|---:|---:|---:|---:|---:|
| Azeroth | 355 | 54 | 301 | 201 | 9 | 0 |
| Kalimdor | 320 | 91 | 229 | 199 | 5 | 0 |

- **None of their tiles touches our Alterac tiles** (the one changed Alterac Mountains tile is
  outside 30-32 x 30-32). The rule still stands in code (`tiles::OURS_WIN`).
- Their WDTs set MPHD flag 0x2 (MCCV). Only 2 of the 14 MCCV tiles paint anything (Kalimdor 35_49,
  Azeroth 35_43); the rest are neutral 0x7f.
- MH2O liquid types seen: 1, 2, 5, 7, 9 (class `(id-1) % 4`: water, ocean, water, magma, water),
  vertex formats 0, 1, 2. Every MH2O tile also sets `MHDR.offsMH2O`.
- New zones (AreaTable ids we lack): Gilneas 5179 (22 changed tiles), Tel'Abim 5121, Icepoint Rock
  5024, North Sea 5027, Blackstone Island 5536, Moonwhisper Coast 5642, Alah'Thalas 2040,
  Thalassian Highlands 5225, Scarlet Enclave 4012, Balor 5561, Northwind 5581, Grim Reaches 5602,
  Hateforge Quarry 5098 (its entrance), plus edits to almost every stock zone (530 changed tiles, most
  of them rewritten to MH2O water).

## First zone: Tel'Abim (5121)

Why: an island off Tanaris that is almost entirely **new** tiles (14 new, 5 changed coastal tiles
of Tanaris whose chunks are mostly sea), so it adds content without rewriting a stock zone our
spawns stand in; a complete level 52-58 zone (585 creature and 529 object spawns, 45 quests); no
instance portal or core scripts it depends on; and it exercises the new formats (all 19 tiles are
MH2O). Gilneas, the other candidate, is 22 *changed* stock tiles: it replaces terrain we have and
mixes their copies of stock spawns with new ones.

Merged for it: 27 tiles (19 with Tel'Abim chunks + 8 new open-sea tiles around them), our WDT with
the new tiles flagged, 1486 new files (models, WMOs and groups, textures, loading screens, world map
art) and the community's new rows in 31 tables (114 MB `patch-A.MPQ`). Server: 59 creature, 81
object, 109 item and 45 quest templates, 585 + 529 spawns, loot, vendors, trainers, gossip, waypoints,
area rows, one flight node, one area trigger.

## The pipeline

Everything below writes to gitignored `private-assets/`; nothing from either folder is run.

1. **Their world DB** (only when re-importing): copy `their_world` with `mysql/`, `ibdata1`,
   `ib_logfile0`, `undo00*`, `aria_log*` (never `their_char*`, `their_logon*`, `their_logs`) to a scratch
   dir and open it with our `tools/mariadb` on a spare port with `skip-grant-tables`,
   `innodb_undo_tablespaces=3`, `innodb_log_file_size=96M`. Load a vmangos world with our
   migrations and custom SQL (CRLF stripped) as `mangos` on the same server: the import reads our
   ids there. Shut the server down afterwards.
2. **Tiles and areas**: `community-merge areas --community "<client>\Data" --ours
   dist\AzerothWarfare\game\WoW\Data --zones 5121 --out private-assets\womw-community` writes
   `areas.tsv` (chunk to area) and `area_template.tsv`.
3. **World rows**: `python -I tools/community-merge/db_import.py --port <p> --theirs their_world
   --ours mangos --areas private-assets/womw-community/areas.tsv --zones 5121
   --map design/COMMUNITY_ID_MAP.tsv
   --sql private-assets/server/sql/custom/azeroth_warfare/80_community_zones.sql
   --displays private-assets/womw-community/displays.txt`. Several zones: `--zones 5121,5179`
   (one SQL file for all of them; rerun with the full list).
4. **The archive**: `community-merge build ... --above private-assets\womw-patch\patch-9.MPQ
   --zones 5121 --displays private-assets\womw-community\displays.txt --out
   private-assets\womw-community`, then `community-merge verify --ours ... --above ...patch-9.MPQ
   --archive private-assets\womw-community\patch-A.MPQ`. Build it **after** the WotLK layer: the
   tables it writes (Map, CreatureDisplayInfo, CreatureModelData, ...) win over patch-9's, so they
   carry patch-9's rows; rebuild whenever patch-9 changes.
5. **Package**: `package/build.ps1` copies `patch-A.MPQ` into `game\WoW\Data`, `server-dbc` and
   `tiles.txt` into `womw-community\`, and `80_community_zones.sql` beside our custom SQL.
   `aw.ps1` then (Update-CommunityMapData) re-extracts maps/vmaps when the archive is new or changed
   (MapExtractor opens `patch-A.MPQ` last and reads MH2O; VMapExtractor scans lettered patches after
   `patch-99`), rebuilds mmaps for the tiles in `tiles.txt` when the package has mmaps, lays
   `server-dbc` over the extracted tables after the WotLK layer's, and applies the SQL whenever it
   changed (Update-CommunitySql).

Manual server data for a dev box: in a folder holding a `Data` junction to a game folder with
`patch-A.MPQ`: `MapExtractor`, `VMapExtractor`, `VMapAssembler Buildings vmaps`, then
`MoveMapGenerator 1 --tile x,y --silent` per merged tile (or the whole map).

## The branding scrub (every build)

Their data carries the community client's own name, web addresses and mascot (item, spell, quest
and gossip text in every locale, icon and model names). None of it ships. The words are private:
`private-assets/brand-rules.tsv` (format in `src/brand.rs`; `--brand-rules <file>` overrides), and
`build`, `verify`, `hd` and `db_import.py` refuse to run without it.

- **Text** (every table's string block, the SQL, the tsv files): the name in each locale becomes
  "Azeroth" (Chinese 艾泽拉斯), its addresses are dropped, the abbreviation as a word becomes
  "Azeroth". A table's string block is rebuilt in order with every string kept and each string
  column's offsets remapped; a table whose rewritten string is named from a column that does not
  read as strings is refused (the build fails) rather than guessed.
- **File names and embedded paths: aliased, not left.** A branded file name is stored under a
  same-length neutral name (the mascot -> `rivermaw`, the abbreviation -> `womw`, case kept per
  letter), and every reference is rewritten by the same rule: table strings, the NUL-terminated
  paths inside models, tiles and buildings (same length, so their offset tables hold), the HD
  pack's names (so its art still overrides the aliased file). Text rules and file rules agree on
  every path token (one holding `\` or `_`).
- **Their documents and screens** (credits, terms, connection help, readme, `Interface\Glues\
  Credits\`) are dropped from anything we pack; a text file with a branded name too.
- **The gate**: `build` reads its archive back and scans every file (text and tables whole,
  binaries in their embedded path strings), the server tables and the tsv files; `verify` scans
  the archive; `hd` scans its folder; `db_import.py` scans its SQL and tsv files;
  `community-merge brand-scan <file|folder|archive>...` runs it alone. Any hit fails the step.
- Pixels are not text: `cargo run --release --example brand_screens -- <patch-A.MPQ> <dir>`
  writes every glue and loading screen as a PNG for a look by eye.

## Ids (design/COMMUNITY_ID_MAP.tsv)

Kept where free and outside our bands; otherwise renumbered (and always for spawn guids):
creatures 110000-129999, items 140000-149999, objects 220000-229999, quests 160000-169999,
reference loot 700000+, gossip menus 62000-65535, npc text 300000+, broadcast text 400000+, page text
30000+, equipment and spell lists 1100000+, EventAI scripts 21000000+, spawn guids 12000000+
(creatures and objects). Any id of theirs in 90000-199999 (our custom bands and the planned import
blocks) is renumbered. Tel'Abim needed no renumbering beyond guids. The map is read first and
extended, so a re-import keeps every number.

## Then: every zone new to us

After Tel'Abim, the same pipeline ran for every zone whose AreaTable id we lack: Tel'Abim 5121,
Icepoint Rock 5024, North Sea 5027, Blackstone Island 5536, Moonwhisper Coast 5642, Alah'Thalas 2040,
Thalassian Highlands 5225, Scarlet Enclave 4012, Balor 5561, Northwind 5581, Grim Reaches 5602,
Gilneas 5179 (`--zones 5121,5024,5027,5536,5642,2040,5225,4012,5561,5581,5602,5179`):

- Client: 275 tiles (the zones' tiles plus the open sea around them), 4008 new files, 31 tables'
  new rows: `patch-A.MPQ` 814 MB. Verified: every tile meshes through benilla (48,260 wet chunks)
  and both WDTs flag every new tile.
- Server: 1253 creature, 674 object, 1231 item and 632 quest templates; 11,376 creature and 4,980
  object spawns; 170 areas, 16 flight nodes, 20 area triggers (3 teleports), EventAI for 91 events.
  Renumbered: 4 creatures (90975, 90976, 91195, 93100 -> 110000-110003), 2 items (91762, 92001 ->
  140000-140001), 7 objects (-> 220000+), every gossip menu above 65535 (-> 62000-65535), all
  guids (-> 12000000+). The SQL applies twice cleanly in strict mode and a re-import over the
  imported database writes the same file.
- Several zones sit on **changed stock tiles** (Gilneas, Moonwhisper Coast, Northwind, Grim
  Reaches, Thalassian Highlands, Alah'Thalas, Scarlet Enclave): the community's terrain replaces
  ours on those tiles, including the stock-zone chunks they share (e.g. Silverpine, Eastern
  Plaguelands, Azshara edges). Our stock spawns there stay; check them in game.

## Phase 2: everything (owner's decisions, Oct 27)

`--zones all --instance-maps 800-822` (import) and `--zones all --maps 800-822 --idmap
design/COMMUNITY_ID_MAP.tsv` (build): every changed and new tile on Azeroth and Kalimdor (675; our
Alterac/Dalaran tiles 30-32 x 30-32 still win, and none of theirs touches them), their instanced
maps whole, and all their custom content:

- Client: 861 tiles (675 open world + 186 on 15 instanced maps; 803-806 and 809-812 have no client
  map), 6874 new files, the new rows of 45 tables including Spell (5568 rows) and its visual
  tables: `patch-A.MPQ` 1.86 GB.
- Server: 3202 creature, 1985 object, 3873 item, 2153 quest templates; in zones we have, only spawns
  of entries we lack (20,700 of their 36,611 creature and 13,650 of 27,593 object spawns there);
  every spawn on the instanced maps; 18 `map_template` rows; 5575 spells (`spell_template`, ids past
  our smallint columns or in our 60000-69999 band renumbered into 65000-65535, the `Spell.dbc` rows
  renumbered the same way); 27 conditions (closed over and/or/not, ids inside remapped), 2 game
  events, 294 pools (with mother pools), quest start/end, gossip, waypoint and area-trigger
  scripts (talk texts, summons and spell casts remapped), instance entrances (every teleport into
  800-822 with its trigger).
- Their unit_class values our `creature_classlevelstats` lacks (0, 4) become 8 with mana, else 1.
- C++ scripts: their core's source is not in either folder (only binaries, which we do not run), so
  their behaviour cannot be ported from code. Of 338 script uses (244 names), 3 name a script our
  core has (`custom_npc_training_dummy`) and keep it; the rest are listed in
  `private-assets/womw-community/scripts.tsv` (kind, id, name) and import with no script: such NPCs,
  objects and bosses act on their EventAI where they have one, else plain melee. Porting means
  writing each anew in our core from its observed behaviour.
- Server data: MapExtractor reads their WDTs (no MWMO chunk) and empty MCLQ offsets; aw.ps1 rebuilds
  mmaps per merged tile and per whole instanced map (lines of `tiles.txt` with a map id alone).

## Phase 3: every area on their world map (Oct 27)

Why some were missing: Gillijim's Isle (408), Lapidis Isle (409), Hyjal (616) and GM Island (876)
have AreaTable ids stock 1.12 already has, so their rows "conflicted" and ours (no music, no
ambience) were kept. The import placed spawns only on merged tiles, and GM Island's terrain is
unchanged stock, so it had no spawns. Their extra maps (26-28, 31, 45, 49, 50, 150, 532) were
outside 800-822. Their WorldMapArea "World" row (map 0, area 0) would have become a second
continent row.

Now:
- **Areas**: the zones whose world-map row is new to us (GM Island, Hyjal, Gillijim's Isle, Lapidis
  Isle, and the dungeon-entrance maps: Deadmines, Wailing Caverns, Maraudon, Gnomeregan, Blackrock
  Mountain, Scarlet Monastery, Uldaman, Ahn'Qiraj, Caverns of Time, Dire Maul, Timbermaw Tunnels
  and entrance, Windhorn Canyon entrance) take their AreaTable rows, subareas included. Their
  "World" row is dropped.
- **Spawns**: placed by the chunk under them over every tile of both continents (`areas.tsv` now
  covers our unchanged tiles too). Every zone runs through the we-have-it rule (entries we lack
  only, never within 3 yd of ours). That adds GM Island, Silithus, Moonglade, Blasted Lands, the
  Ahn'Qiraj entrance and the rest.
- **Maps**: `--instance-maps` / `--maps 26-28,31,45,49,50,150,532,800-822` (26 `map_template`
  rows; client art for the ones their Map.dbc names: 26-28, 532, 800-822).
- **Counts** (strict apply twice, re-import identical): 3784 creature, 2788 object, 4444 item,
  2371 quest templates; 404 areas, 40 flight nodes, 79 area triggers.

Could not come in: maps 31, 45, 49, 50 and 150 have server rows only (their client has no map files
for them); their C++ scripts (355 uses now) as before.
## Still not merged

- Their 335 C++ script uses (above), conditions inside script target parameters, and guid-based
  script commands (respawn/open door by guid) on renumbered spawns.
- Their edits of stock files (1666 retextured stock models and the like) and edited stock table rows
  (incl. 22,348 edited stock Spell rows and the Kalimdor/Azeroth world map bounds): ours kept.
- Their spawns in zones we have that duplicate ours (by entry, within 3 yd) and their edited stock
  templates/quests: ours kept.
- WDL far terrain for new tiles; in-game look and walk test (needs the owner's go-ahead).