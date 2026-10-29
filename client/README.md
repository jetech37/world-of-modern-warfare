<div align="center">
  <h1>benilla-mw2</h1>
  <p><b>Modern Warfare 2 in Classic World of Warcraft</b></p>
  <p>A first-person shooter layer for <a href="https://github.com/samwhosung/benilla">benilla</a>, the from-scratch WoW 1.12.1 client written in Rust and Bevy.</p>
</div>

Log in, and Azeroth plays like MW2. You see the world in first person, the mouse aims, left click
shoots and right click aims down sights. You get a crosshair, hitmarkers, a
kill feed, a radar, a cosmetic magazine with reloads, and the classic killstreaks from the UAV up
to the Tactical Nuke.

Point it at your own copy of **Modern Warfare 2** and the gun in your hands is MW2's: the real
first-person weapon and arms, with MW2's idle, fire, reload and aim-down-sights animations, read
straight from your MW2 install with [IW4L](https://github.com/vladtrc/iw4L)'s asset code (the
Rust rewrite of MW2 behind the
[2010 Rust Rewrite Mashup](https://github.com/chasmlol/2010-rust-rewrite-mashup)).

It runs against a normal 1.12.1 server and plays by its rules. A shot is your class's own attack
(a Hunter's Auto Shot, a Mage's wand, a Warrior's gun), aimed at whatever is under the crosshair.
The server decides whether it hits and how much it does. Everything MW2 about it lives in the
client.

## Azeroth Warfare mode (with the fusion server)

Against an Azeroth Warfare server (`server/` in this repository: vmangos with the fusion rules), the gun stops casting your class's ranged spell and fires real
rounds instead. Each round goes to the server as a ray with the unit under the crosshair. The
server checks it against its own hitboxes, rewound to the moment you fired, and deals MW2's damage
for that gun, range and hit location (headshots included), scaled by your gear and level. The
magazine, fire rate and reserve are the server's.

| Control | What it does |
|---|---|
| C | Crouch (again to stand). Smaller target, quieter: creatures notice you from closer. Moves at 0.6 of a run |
| Hold C, or Left Ctrl | Prone (again to stand). Crawl past a murloc camp at low level, at 0.15 of a run, with half the bloom |
| Space while crouched or prone | Rise one stance instead of jumping |
| Space while running at a ledge | Mantle onto it (0.5 to 1.8 yards high; "Press SPACE to climb" shows when one is in front) |
| Hold Left Shift | Sprint at 1.5 times a run, for 4 seconds of stamina (the HUD bar), refilling at 1 per second. No firing or aiming while sprinting, nor for 0.25 s after |
| V with an enemy within 20 yards | Knife lunge: the server dashes you to the nearest enemy in front and strikes (40 yards with Commando) |
| K | Kits: view your five kits (MW2 classes: guns, attachments, camos, perks, equipment). Edit and switch them at a Quartermaster (one at every inn and class trainer area: talk to them, "Show me my kits") or with a Field Quartermaster's Ledger, then Save & Equip. The server checks it, saves it with the character and puts it in your hands out of combat |
| 1, 2, mouse wheel | The class's primary and secondary |
| F6 | Armory: the next gun you have unlocked (one per class at level 1; the rest by level, MW2's rank scaled to 60) |
| F7 | Armory: the next special round you have unlocked (incendiary and others at 10, 20, 30) |
| F8 | Quartermaster rearm: in a city or an inn, refills every gun and special round for a small fee |
| F9, or the Honor tab's check box | Your personal free-for-all PvP flag (on at once; off after 5 minutes out of combat) |

The gun list comes from `assets/fusion_catalog.json`, a copy of the server's `catalog.json` (MW2's own numbers, made by its `contrib/fusion/make_seed_catalog.py` from `examples/mw2_export`).
It is made from an MW2 install, so the public source tree has none: `../private-assets.md` says how
to make yours (the build stops with the same instructions until there is one).
Keep the two the same: `MW2_FUSION_WEAPONS=/path/to/catalog.json` reads another one (a version 1 `weapons.json` too).
`MW2_FUSION=0` turns the mode off (for a stock vmangos server, which ignores the fusion packets).
This mode needs the benilla fork in `../benilla` (the fusion packets, Create-a-Class's included),
which `Cargo.toml` uses by path.

## How it plays

| Control | MW2 | What it does in WoW |
|---|---|---|
| Mouse | Aim | Mouselook is always on: the mouse turns you and pitches the camera |
| Left click | Fire | Targets the unit under the crosshair and fires your class weapon (hold for auto) |
| Right click | Aim down sights | Zooms the field of view, tightens the crosshair, shows a red dot |
| R | Reload | Refills the magazine (cosmetic; Hunters see their real ammo as the reserve) |
| G | Lethal | Casts your class's "frag" at the crosshair target |
| V | Knife | Casts your class's melee strike, or swings your weapon |
| W A S D, Space | Move, jump | Unchanged |
| Hold Left Alt | | Frees the cursor for bags, vendors, quest windows and the action bar |
| F10 | | Turns the whole MW2 layer on or off |

Opening a window that needs the cursor (the Esc menu, bags, a vendor, loot, quests, the map, the
spellbook and so on) lowers the gun and gives you the cursor back until you close it.

Your 1.12 action bars and keybinds still work. Only left click, right click, R, G and V are taken
over, and only while the gun is up. When you hold Alt or type in chat, they go back to WoW.

### Class loadouts

| Class | MW2 weapon (asset) | Fire | Lethal (G) | Knife (V) |
|---|---|---|---|---|
| Hunter | Intervention (`cheytac_mp`) | Auto Shot (auto-repeat) | Arcane Shot | Raptor Strike |
| Mage | FAL (`fal_mp`) | Shoot (wand) | Fire Blast | melee |
| Priest | M9 (`beretta_mp`) | Shoot (wand) | Shadow Word: Pain | melee |
| Warlock | AK-47 (`ak47_mp`) | Shoot (wand) | Immolate | melee |
| Warrior | SPAS-12 (`spas12_mp`) | Shoot Gun | Charge | Heroic Strike |
| Rogue | UMP45 (`ump45_mp`) | Shoot Gun | Throw | Sinister Strike |
| Shaman | RPD (`rpd_mp`) | Lightning Bolt | Earth Shock | melee |
| Druid | TAR-21 (`tavor_mp`) | Wrath | Moonfire | melee |
| Paladin | Riot Shield (`riotshield_mp`) | melee | Judgement | melee |

A weapon missing from your install falls back to the M4A1 (`m4_mp`).

Hunter is the real MW2 class: give one a gun and some ammo. Wand users need a wand equipped, and
Warriors and Rogues need a gun (or set `MW2_FIRE="Shoot Bow"`). You can override any of it with
environment variables:

```sh
MW2_GUN=barrett_mp         # which MW2 weapon you hold, by asset name
MW2_HANDS=viewhands_tf141  # which MW2 arms hold it
MW2_FIRE="Shoot Bow"       # the spell the trigger casts
MW2_FIRE_MODE=auto         # auto (auto-repeat), spam (recast while held) or melee
MW2_LETHAL="Serpent Sting"
MW2_KNIFE="Mongoose Bite"
MW2_WEAPON="BARRETT .50CAL"
MW2_MAG=10
MW2_OFF=1                  # start with the layer off (F10 turns it on)
```

### Score and killstreaks

A kill is worth 100 points, plus bonuses: First Blood, Longshot (30+ yards), One Shot! One Kill!,
and Double, Triple and Multi Kill. Kills in a row without dying earn the following rewards:

| Kills | Reward | In this mod |
|---|---|---|
| 3 | UAV | Every hostile within 60 yards shows on the radar for 30 seconds |
| 5 | Predator Missile | Announced |
| 7 | Harrier Strike | Announced |
| 11 | Chopper Gunner | Announced |
| 25 | Tactical Nuke | 10 second countdown, then the white-out |

Hits and kills are read from the health the server reports for units you fired on. A kill is
credited when a unit you hit dies, so a kill your group finishes still counts.

## Running it

You need what benilla needs:

- **An English 1.12.1 client (build 5875)** for the game data. It is only read.
- **A 1.12.1 server with Warden off**, such as [vmangos](https://github.com/vmangos/core).
- **Modern Warfare 2 (Steam, PC)** for the MW2 weapons. It is only read. Without it the mod
  still runs, with the plain HUD and no gun model.
- **Stable Rust and a C compiler.** On Linux you also need the ALSA and udev development
  packages and pkg-config.

```sh
git clone <this repository> womw
cd womw/client
ln -s /path/to/WoW WoW          # or set WOW_DATA=/path/to/WoW/Data
MW2_PATH="/path/to/steamapps/common/Call of Duty Modern Warfare 2" cargo run --release
```

On Windows, in PowerShell:

```powershell
New-Item -ItemType Junction -Path WoW -Target C:\path\to\WoW
$env:MW2_PATH="C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare 2"
cargo run --release
```

On first use it reads MW2's multiplayer zones (`common_mp.ff`, `patch_mp.ff`) once, which takes a
few seconds; the log says `MW2 install read` and then `viewmodel … up`. If it cannot find the
install, it says why and keeps the plain HUD.

`WOW_HOST` points it at a server (default `localhost`). Settings, addons and screenshots live in
`benilla-config/` beside this README, the same as in benilla. The banner reads
`benilla build … · extended`, which is how a client built on top of benilla identifies itself.

For a build without benilla's developer instruments, use `cargo run --profile ship --no-default-features`.

## Playing with friends

The repository holds code only: every player needs **their own** copies of the two games, which
the client reads in place. The simplest way to play together is the Windows package
(`../package/build.ps1`, then `Play.bat`) or a hosted server with the launcher; see the
repository's top-level `README.md`. To run this client by hand against a friend's server:

```powershell
cd client
.\scripts\play.ps1 -Wow "C:\Games\WoW 1.12.1" -Mw2 "C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare 2" -Server <host address>
```

The first build takes a while; after that `.\scripts\play.ps1` alone starts it (the paths are
remembered in `scripts\play.local.json`). Log in with an account on that server.

## How it is built

benilla is a faithful 1.12.1 client and asks that anything 1.12.1 never had live on top of it in
a crate of its own. That is what this crate is. It builds on the benilla fork in `../benilla` (by
path, in `Cargo.toml`) and adds one Bevy plugin through `benilla_app::run_with`; what the fork
itself adds (the fusion packets and the few hooks the layer needs) lives there.

| File | What it does |
|---|---|
| `src/input.rs` | Takes left click, right click, R, G and V away from WoW while the gun is up, at the same point in the frame where benilla's loading screen cuts input |
| `src/mode.rs` | Turns the layer on and off. Holds first person with `CameraZoomIn` and mouselook with `MouselookStart`, the stock 1.12 camera verbs |
| `src/aim.rs` | Casts the camera ray against benilla's unit pick bounds. Uses its triangle-accurate world pick for walls, so you cannot shoot through them |
| `src/weapon.rs` | Targets through `benilla_app::Interact` (the right-click path), then casts with `CastSpellByName`. Also handles the magazine, recoil, bloom and aim down sights |
| `src/fusion.rs` | Azeroth Warfare mode: stance, sprint and the FFA flag up to the server, fired rounds as shot batches, the Armory hotkeys, and the server's hit and kill events back |
| `src/combat.rs` | Turns health changes from benilla's object mirror into hits, kills and damage taken |
| `src/streaks.rs` | Score, bonuses and killstreaks |
| `src/hud.rs` | The overlay, as a bevy_ui tree drawn over the WoW interface, in the install's own font |
| `src/mw2/load.rs` | Walks MW2's multiplayer zones with IW4L's readers and assembles one weapon: gun and arms meshes, skeletons, colour maps and animation clips |
| `src/mw2/viewmodel.rs` | Poses the gun and arms each frame with IW4L's DObj, skins them as MW2 does, places `tag_view` at the eye and draws them under benilla's camera |
| `src/mw2/sound.rs`, `src/mw2/audio.rs` | MW2's own sound aliases from the install's sound zones: the gun's shots, the reload foley its animations' notetracks name, the knife and the gear rattle, mixed on a cpal stream |
| `src/stance_view.rs`, `src/sprint.rs`, `src/movement.rs` | The stance camera heights, sprint stamina, and the mantle hint and lunge reach |
| `src/body_state.rs`, `src/body_pose.rs` | What a body seen from outside does (yours in third person, every other player's): your hands' state goes to the server (action-systems channel 10, ops 2 and 3) and comes back for others (ops 4 and 5); each body holds its MW2 gun, plays WoW's own clip for what it does (the rifle's ready and fire, each weapon's swing, a cast, a block, a tool) and gets a few bone rotations layered over it: the aim's pitch, the shot's kick, aiming down the sights, the reload, the grenade's throw, a spell's arm, the shield, crouch, prone and the sprint carry. `MW2_TP_LAYER=0` turns the layer off |
| `third_party/iw4l/` | IW4L's engine-independent asset crates, vendored unmodified (see its README) |

To move to a newer benilla, merge it into `../benilla`, copy benilla's `Cargo.lock` over this
one, and run `cargo update -p benilla-mw2`.

## Limits

- The killstreak rewards past the UAV are announcements only. A real Predator Missile would need
  the server to do something, and this mod plays by the server's rules.
- Outside Azeroth Warfare mode there is no sprint or stance. Movement speed comes from the
  server, which in Azeroth Warfare mode scales it by stance, sprint (4 seconds of stamina), aiming
  and the gun's MW2 weight.
- Talking to an NPC (a vendor, a quest giver, a flight master) means holding Alt and
  right-clicking it, as in normal WoW. Once its window opens, the cursor stays free until you close it. The gun will not fire at units that offer services, so you
  cannot open their windows by accident.
- Hit detection uses each unit's pick bounds, not its exact mesh. Close enough for a shooter.
- The MW2 gun is drawn with a simple lit material, not MW2's own shaders, and it can poke into a
  wall you stand against. MW2's HUD art is not wired in yet.
- MW2's sounds play for your own gun and movement only (shots, reloads, the knife, gear rattle),
  on an audio stream of the layer's own, unpositioned. `MW2_VOLUME` (default 0.6) sets their
  loudness under WoW's master and effects volume.

## License and credits

This crate is licensed under the GNU General Public License v3.0 (`../LICENSE`). It is built on
[benilla](https://github.com/samwhosung/benilla) (MIT or Apache-2.0) by Sam and contributors, and
vendors part of [IW4L](https://github.com/vladtrc/iw4L) by vladtrc and contributors, through the
[2010 Rust Rewrite Mashup](https://github.com/chasmlol/2010-rust-rewrite-mashup) by chasmlol
(Apache-2.0, `third_party/iw4l/LICENSE` and `NOTICE.upstream`). See `../NOTICE.md`.

This is an independent fan project, not affiliated with or endorsed by Blizzard Entertainment or
Activision. It ships no game content from either: no art, models, sounds, maps or data. You
provide your own legally obtained 1.12.1 client and, for the weapons, your own copy of MW2. World of Warcraft is a trademark of Blizzard
Entertainment, Inc. Call of Duty and Modern Warfare are trademarks of Activision Publishing, Inc.
