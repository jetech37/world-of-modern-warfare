# Overnight: 2026-10-04 → 05

What was built while you slept, how to try it, and what's left. Everything is committed locally;
nothing has been pushed.

## Try it

Start the client as usual (`target/release/benilla-mw2.exe`, `MW2_PATH` set). The server image in
WSL is already rebuilt and running.

| Key | What it does |
|---|---|
| **B** | Build mode: puts the gun away and shows the build bar |
| Mouse wheel | Choose a piece (or a blueprint, at the end of the bar) |
| **R** | Turn the piece a quarter |
| **LMB** | Place it where the ghost shows (green = OK, red = can't) |
| **MMB** | Take one of your pieces back down (materials refunded) |
| **F** | Open or shut a door (in or out of build mode) |

In build mode every piece is tinted by how well it's held up: blue (on the ground), green,
yellow, red (about to fall). Admins (your account) build for free; everyone else pays in WoW
trade goods: Simple Wood, Rough Stone, Linen Cloth, Copper Bar.

## What's new

### NPCs with guns (the plan you approved)
- **Who gets a gun:**
  - Humanoids that already use a ranged weapon always swap it for a gun.
  - Labourers (miners, peons, slaves) stay melee trash.
  - Casters wait for the spell system.
  - About 60% of the other humanoid fighters get a gun. The choice is fixed per creature kind.
- **How gunners behave:** a WoW gun model in their hands, about 20 yd standoff, bursts of a real MW2 gun.
- **Hit chance** drops with range, your stance and speed, and the level gap.
- **Damage** scales by level, so gray mobs barely scratch you.
- **What you see and hear:** every round fired at you draws a tracer from the shooter, with a muzzle flash and the gun's third-person sound.

### Building (Valheim / Enshrouded style)
- **17 pieces:**
  - wood floor, wall, half wall, doorway, door, window, 45° roof, stairs and beam;
  - stone floor, wall and half wall;
  - sandbags, campfire and torch;
  - sentry gun and bedroll.
- **Snapping:** pieces snap to each other's corners and edges, squared to what they attach to.
- **Server authority:** the server checks reach, overlap, support and materials, saves pieces to the database (`fusion_build_piece`) and streams them to everyone within 160 yd.
- **Structural support:** support falls off a little with each storey and more with each reach outward. Wood reaches farther sideways; stone stacks taller. Take out a supporting piece and everything it held collapses.
- **Combat:**
  - Your bullets and blasts stop at pieces and break them.
  - NPC gunners shoot the wall that's in their way.
  - Melee mobs stop at your walls and batter them down.
- **Looks:** pieces use WoW's own textures (Westfall boards, Elwynn planks, Duskwood shingles, Shadowfang stone) and WoW's sun and ambient light, and they get darker as they take damage. They are solid: you can walk on floors, stairs and roofs.
- **Breaking:** broken pieces fall apart in tumbling chunks, using WoW's own sounds.
- **Blueprints** (at the end of the build bar) put up a whole structure piece by piece:
  - **Watchtower:** a room, a rooftop with parapets, outside stairs and a torch.
  - **Outpost:** a 2-room house with a window and door, and a roof sentry.
  - **Sandbag nest:** a ring of sandbags with a sentry.

### Sentry gun, bedroll, campfire
- **Sentry gun:** shoots hostile creatures within 35 yd of itself while you're within 80 yd, in M240 bursts. Its kills are yours, with hitmarkers.
- **Bedroll** (MW2's Tactical Insertion): when you release your spirit, you wake at your bedroll instead of a graveyard. You can have one at a time.
- **Campfire:** heals you 4% a second when you're out of combat.

### Raids
- **When they come:** a base of 8 or more of your pieces draws a raid every 20–35 minutes, the first no sooner than 8 minutes after you build.
- **The wave:**
  - First a "RAID INCOMING" banner and a 20-second warning.
  - Then 3–8 hostile humanoid fighters of your level arrive from one side.
  - They use the new aggro, gunner and siege AI.
- **Rewards:** repel the raid and you earn experience.
- **Admin controls:** call a raid with probe `raid` (channel message 9), or end it with `endraid` (message 10).

### Doors
- Wood doors open and shut with **F**.
- An open door lets bullets, sight lines and bodies through; a shut one stops them.

### Killcam
- When you're shot dead, the view cuts to your killer and looks back over their shoulder at where you fell, with the gun's name.
- It's live, not a replay, because there's no world history to replay.

### Muzzle flashes
- Every NPC round fired at you, and every sentry burst, flashes at the muzzle.

### World of Skatecraft (your request)
- **What I ported:** Kimmo3223's world-of-skatecraft, a Skate 3 engine driving your WoW character. It's in the fork behind a cargo feature called `skate`, off by default.
  - Build it with `cargo build --release --features skate`.
  - It needs your own extracted **Xbox 360 Skate 3** folder (set `WOW_SKATE_ASSETS`) and an Xbox pad.
  - There's no Skate 3 install on this machine, so it couldn't be played tonight.
  - Without the files, J only prints "Skatecraft: no Skate 3 assets found".
- **Keys:** J gets on and off the board; **Y** switches to the Skate camera. It was K upstream, but K is Create-a-Class.
- **The MW2 layer stands down while you ride** (gun, HUD, first person).
- **Licence warning:** the original engine is GPL-3.0, while the crates we pull in say Apache-2.0. See `NOTICE.skatecraft` in the fork, and don't distribute a `--features skate` build until that's settled.
- **The fork's API-wall test was already failing** ("world API leaks: 151, ceiling 107") from the fusion work before this port. It needs its ceiling raised or the leaks closed.

### WoW in the browser (wenilla), phase 1
- **It works up to the login screen.**
  - Stock wenilla (benilla compiled to WebAssembly) built on this machine, in `C:\Users\<you>\wenilla`.
  - It's served at **http://127.0.0.1:8088/** in Chrome or Edge (it needs WebGPU), from `wenilla-host`, PID 4512, localhost only.
  - Its realm proxy connects to our Docker server.
- **Login is untested.** I couldn't type into the canvas through automation (Enter and Tab work, letters don't), so log in by hand.
- **Sizes:** the download is 15 MB compressed (76 MB of wasm).
- **Getting our build onto it:**
  - Merging our fork into it conflicts in only two files (`net/io.rs`, `world/writer/mod.rs`), about a day of work.
  - Porting the MW2 layer means replacing threads, file reads and `Instant` with their web equivalents: 1–2 weeks.
- **To stop the host:** `Stop-Process -Id 4512`.

## Verified live tonight
- **NPC gunners:** a Defias Thug aiming an Old Blunderbuss; tracers and damage at you; balance fixed after the first test (lowbie packs nearly killed a level 60).
- **Building:**
  - placing, snapping, turning and textures;
  - the gun hiding in build mode;
  - persistence across a server restart;
  - the watchtower and outpost blueprints (9 of 11 and 9 of 12 pieces on uneven ground; there's now a fix that lifts blueprints to the highest ground).
- **Raids:** the warning, "DEFEND YOUR BASE" and "RAIDERS FALL BACK" banners; level-60 raiders spawning and charging; sentry hitmarkers; raiders breaking pieces (12 became 9).
- **Bedroll:** respawn ("You wake at your bedroll" after dying and releasing), and taking pieces down with MMB.
- **Unit tests only:** the killcam, debris and doors (the server half is live, the client half untested).

## How it's wired
- **Fork (`C:\Users\<you>\benilla`, branch `overnight`, not pushed):** adds one general fusion channel, opcodes 833/834.
  - Building and world events ride it, so future features need no fork changes.
  - The client builds against this checkout through a `[patch]` section at the end of `Cargo.toml`.
  - To switch back to the git pin: push the branch, bump the `rev`s, and drop the `[patch]`.
- **Server rules:** `src/fusion/{NpcGuns,Build,Defense}.{h,cpp}`, with unit tests.
- **Server game hooks:** `FusionMgr.cpp`, `Creature.cpp` (gun and siege updates) and `Player.cpp` (bedroll respawn).
- **Database:** the server creates its one new table itself at startup. There was no migration and no change to your existing data.
- **Client:**
  - `src/build/` (pieces, wire, render, mod), `src/killcam.rs`;
  - tracer streaks and muzzle flashes in `src/tracer.rs`;
  - probes `MW2_PROBE_BUILD` (`b`, `r`, `place`, `remove`, `wheel<n>`, `f`, `raid`, `endraid`).

## Not pushed
Client, server and fork commits are all local. Say the word and I'll push them.
