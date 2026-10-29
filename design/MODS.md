# Mods and systems you can lift out

World of Modern Warfare is built as a stack of separable systems on a WoW 1.12 server (a vmangos
fork) and a WoW 1.12.1 client (a benilla fork). This catalog lists each one for other projects:
what it is, where it lives, what it leans on and how hard it is to take on its own. Licenses
follow the tree (`NOTICE.md`): server code GPL-2.0, client and tools GPL-3.0, benilla changes MIT
OR Apache-2.0, the two tools under MIT OR Apache-2.0. Third-party game data (listed in
`NOTICE.md`) is not offered under any of them.

## How the pieces connect

- **The fusion rules library** (`server/src/fusion/`, headers in `include/fusion/`, unit tests in
  `tests/`, built as `fusion_tests`): plain C++ with no vmangos types. Most server rules live
  here, so they can be reused with any core.
- **The server hooks** (`server/src/game/Fusion/Fusion*.cpp`, `FusionMgr`): bind the library to
  vmangos (players, creatures, spells, packets). This is the part to rewrite for another core.
- **Fusion packets**: a custom channel between server and client (`fusion/Packets.h` on the
  server, `client/src/fusion.rs` and benilla's `benilla-app/src/fusion.rs` seams on the client).
- **The client layer** (`client/`, Rust and Bevy) on top of benilla's public seams
  (`design/BENILLA_CHANGES.md`, section 3).
- **SQL** in `server/sql/custom/azeroth_warfare/NN_*.sql` (each file applies on its own, with a
  matching script in `rollback/` where one exists).

Difficulty below: **Easy** (copy the files, little glue), **Medium** (a few seams or tables to
port), **Hard** (cuts across the client, server and data).

## Catalog

### MW2 gunplay in WoW
First-person guns refereed by the server: hitscan and projectiles against rewound hitboxes,
damage by range and body part, ADS, recoil, sway, stances, reloads, attachments, camos, perks,
killstreaks, kits and Create-a-Class.
- **Code:** client `client/src/mw2/`, `weapon.rs`, `aim.rs`, `spread.rs`, `rounds.rs`,
  `tracer.rs`, `grenade.rs`, `streaks.rs`, `cac.rs`, `cac_ui*`, `hud.rs`; server
  `fusion/{Weapon,Ballistics,HitValidation,Damage,Rounds,Reload,Ammo,Arsenal,Loadout,Kits,Perks,
  Killstreak,Catalog}.h` and `game/Fusion/FusionArmory.cpp`, `FusionMgr.cpp`.
- **Depends on:** benilla's fusion seams (camera, viewmodel, input, packets); the fusion catalog
  (`client/assets/fusion_catalog.json`, `server/contrib/fusion/catalog.json`, third-party data);
  the player's own MW2 install for models and sounds (read by the vendored IW4L crates in
  `client/third_party/iw4l/`); SQL `01_ranged_proficiency.sql`, `04`/`05_quartermasters*.sql`,
  `10_enchanted_rounds.sql`, `14_kits_at_trainers.sql`.
- **Difficulty:** Hard. The core of the game; the rules library alone (ballistics, hit checks,
  damage) is Easy to reuse.

### Two-hand action systems
Melee and spells from the hands: a weapon, spell, shield or two-hander per hand, free-aimed
bolts, held sprays, charge and release, dual cast, and four action systems on one bar (Modern,
Classic, Utility, Companion).
- **Code:** client `hands.rs`, `hand_anim.rs`, `hand_anims/` (track loader and file-format
  readers), `hand_shape.rs`, `hand_slots.rs`, `melee.rs`, `melee_pose.rs`, `spellcast.rs`,
  `spell_fx.rs`, `action_systems.rs`, `main_bar.rs`; server `fusion/{Melee,Spellcasting,
  ActionSystems}.h`, `game/Fusion/FusionSpellcasting.cpp`.
- **Depends on:** the viewmodel arms from the gunplay layer (`client/src/mw2/viewmodel.rs`);
  benilla's spell-missile seam (`benilla-app/src/fusion.rs`); optionally the hand tracks in
  `client/assets/hand_anims/` (without them it uses hand-made keyframes).
- **Difficulty:** Medium on top of the gunplay viewmodel, Hard without it.

### Dragonflight-style skyriding
Momentum flight with dives, climbs and a vigor-like resource; flying companions as mounts.
- **Code:** server `fusion/{Skyriding,Riding}.h`, `game/Fusion/FusionRiding.cpp`; client
  `riding.rs`, `mount_flight.rs`.
- **Depends on:** benilla's movement and camera seams; SQL `35_flying_companions.sql`; city wards
  (below) for where flight stops.
- **Difficulty:** Medium. The flight model is self-contained; the mount wiring is core-specific.

### Companions and card duels
Battle companions that follow, fight, level, learn moves, evolve (some into mounts) and trade;
a companion index and keeper NPC; turn-based companion battles ("card duels").
- **Code:** server `fusion/{Companions,CompanionMoves,CompanionTraits,CompanionIndex,
  CompanionKeeper,CompanionRest,CompanionBattles}.h`, `game/Fusion/FusionCompanion*.cpp`, docs
  `server/src/fusion/docs/COMPANIONS.md`; client `companion*.rs`.
- **Depends on:** fusion packets; SQL `02_companions.sql`, `08`, `13`, `15`, `20`, `24_companion_revive`,
  `29`, `31_vanilla_companions.sql`, `33_companion_battles.sql`; design
  `COMPANION_BATTLES.md`, `COMPANION_CATALOG.md`.
- **Difficulty:** Medium. The rules and battle engine are library code with tests; the UI is
  client-specific.

### Player ships and airships
Ships and airships players own, crew and steer.
- **Code:** server `fusion/{Ships,ShipCrew}.h`, `game/Fusion/FusionShips.{h,cpp}`; client
  `ships.rs`.
- **Depends on:** vmangos transports; SQL `36_player_ships.sql`; design `PLAYER_SHIPS.md`.
- **Difficulty:** Medium to Hard (transport handling is core-specific).

### Custom professions and player construction
Construction (destructible structures, homestead plots, furniture, locks, turrets), Minstrelsy,
and reworked Inscription and Jewelcrafting for 1.12.
- **Code:** server `fusion/{Construction,Build,Plots,Homestead,Furniture,Locks,Crafts,Minstrelsy,
  Gathering,ProfessionAdmin,Maintenance}.h`, `game/Fusion/Fusion{Construction,Furniture,Locks,
  Crafts,Minstrelsy,Professions,Tools}.cpp`, docs `CONSTRUCTION.md`; client `build/`,
  `professions/`, `minstrel/`, `tools.rs`.
- **Depends on:** SQL `03_construction.sql`, `16_homestead_ui.sql`, `24_construction_rules.sql`,
  `25_utility_tools.sql`, `30_minstrelsy.sql`, `32_professions.sql`, `33_furniture.sql`,
  `35_inscription_jewelcrafting.sql`; benilla's trade-window and gameobject seams.
- **Difficulty:** Medium per profession; construction is Hard (world objects, persistence).

### The class rework
Three core abilities and six picks per class, tuned to answer guns.
- **Code:** server `fusion/{Classes,ClassTuning,ClassEffects,DruidForms,Balance}.h`,
  `game/Fusion/Fusion{Classes,ClassAbilities}.cpp`; client `classes.rs`, `forms.rs`,
  `form_limbs.rs`, `ability_tips.rs`.
- **Depends on:** SQL `18_class_fixes.sql`, `21_class_options.sql`; design `CLASS_CATALOG.md`,
  `BALANCE_GUNS_VS_CLASSES.md`; the two-hand action systems for casting.
- **Difficulty:** Medium; the numbers are data, the hooks are spell-system specific.

### Wanderers
Server-driven characters that live in the world: they quest, grind and fight in squads, with
lives and memory across sessions.
- **Code:** server `fusion/{Wanderers,WandererLives}.h`, `game/Fusion/FusionWanderers.{h,cpp}`,
  `FusionWandererLives.cpp`.
- **Depends on:** vmangos' player-bot style session handling; SQL `28_wanderers.sql`; design
  `WANDERERS.md`.
- **Difficulty:** Hard (deep in the core's session and AI code).

### City wards and no-fly ceilings
Warded airspace over the classic cities and unfinished places: a flier is set down or stopped at a
ceiling where the world would break when seen from above.
- **Code:** server `fusion/Airspace.h`; client `mount_flight.rs`; the example
  `client/examples/city_holes.rs` (finds the holes).
- **Depends on:** SQL `12_districts_and_flight.sql`.
- **Difficulty:** Easy. A table of volumes plus one movement check.

### The WotLK asset import (Dalaran)
Converts WotLK 3.3.5a models, terrain and Dalaran into a 1.12 patch layer (`patch-9.MPQ`).
- **Code:** client `wotlk/` (M2 conversion, ADT, DBC, holes, MPQ writer, Dalaran), examples
  `wotlk_import.rs`, `dalaran_probe.rs`.
- **Depends on:** your own WotLK 3.3.5a install; SQL `33_wotlk_looks.sql`, `34_dalaran.sql`,
  `37_dalaran_city.sql`; design `WOTLK_ASSETS.md`.
- **Difficulty:** Easy as a tool (it only writes an MPQ and server DBCs); the in-game Dalaran needs
  the SQL.

### The community content merge tool
`tools/community-merge/`: merges another 1.12-format content project's archives and tables into
one patch layer and SQL, remapping ids and checking references.
- **Depends on:** benilla's format crates; design `COMMUNITY_MERGE.md`.
- **Difficulty:** Easy (a standalone Rust tool, MIT OR Apache-2.0).

### The HD pack tooling
`community-merge hd` builds the optional HD texture and model pack (`womw-hd-*.MPQ`) from
higher-resolution sources and surveys what changes; the client mounts it.
- **Code:** `tools/community-merge/src/hd.rs`; client `hd_pack.rs`; benilla `hd_pack.rs` and the
  MPQ chain (`benilla-formats/src/chain.rs`).
- **Depends on:** design `HD_PACK.md`.
- **Difficulty:** Easy for the tool, Medium for the client mount order.

### benilla client improvements
Fixes and additions to the from-scratch 1.12.1 client: networking, entities and animation,
rendering, terrain, the MPQ chain, the Lua UI (missing 1.12 functions), input, sound, the minimap.
- **Code and list:** `benilla/`, documented in `design/BENILLA_CHANGES.md` (by area, with the
  public seams the shooter uses).
- **Difficulty:** Easy to upstream one by one (MIT OR Apache-2.0, like benilla).

### The CASC reader
`tools/casc-read/`: reads modern Blizzard CASC storage (and dumps DB2 tables with `db2dump.py`)
to look up assets for the HD and WotLK work.
- **Difficulty:** Easy (standalone, MIT OR Apache-2.0).

### World-map zone fixes
The world map as a movable window, one continent per map, merged zones clickable on the continent
map, the zone picker working on merged map chains.
- **Code:** client `world_map.rs`, `world_map/tests.rs`; benilla `benilla-app/src/ui_world_map.rs`
  and `ui_script/world_map_tests.rs`.
- **Difficulty:** Easy (UI only, no server side).

### The achievements expansion
Account-wide achievements that unlock guns, attachments, camos, perks and streaks, plus
profession and companion achievements.
- **Code:** server `fusion/{Achievements,Progression,Prestige,Stats}.h`,
  `game/Fusion/Fusion{Achievements,Prestige,Stats}.cpp`, docs `ACHIEVEMENTS.md`, `PRESTIGE.md`;
  client `achievements.rs`, `prestige.rs`.
- **Depends on:** SQL `11_prestige_professions.sql`; fusion packets (channel 3).
- **Difficulty:** Medium; the counters and rules are library code, the unlock list is ours.

### The capture and screenshot tool
Headless captures of the client for tests and documentation: scripted probes, fixtures, depth
and effect views, frame-exact screenshots, and a switch that keeps any of our windows open with
no player or server.
- **Code:** benilla `benilla-app/src/capture/`; client `capture_keep.rs`, `probe.rs`,
  `client/docs/CAPTURE.md`.
- **Difficulty:** Easy (Bevy only; useful for any Bevy-based client).

## Not separable

The launcher (`launcher/`), the gate and hosting scripts (`vps/`, `admin/`) and the package
builder (`package/`) are tied to running this game, though each is small and readable.
