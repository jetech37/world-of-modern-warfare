# MW2 Create-a-Class, movement and killstreaks: design

Date: 2026-10-03. Repos: `azeroth-warfare-server` (branch `fusion`, the rules and the server) and
`benilla-mw2` (branch `main`, the client). Status: approved in conversation, awaiting spec review.

## Intent

Justin wants to play and test all of Modern Warfare 2's multiplayer kit inside Azeroth Warfare
with one admin character:

- **Said:** a level 60 admin character with every MW2 gun, attachment, equipment item, perk and
  camo; MW2's Create-a-Class usable in game; MW2's stances and movement ported.
- **Decided in conversation:** the admin is mainly for Justin to test everything (no per-player
  MW2 progression); all three pieces are designed together; equipment behaves as in MW2;
  classes can be edited anytime and swap when out of combat; MW2 movement sits on top of WoW's
  base movement; the menu is a WoW-style window; killstreak selection and every streak's real
  effect are in scope; server-authoritative rules with the client drawing (approach A), reusing
  WoW creatures where they fit; the streak earn rules in "Killstreaks" below.

Success: on the admin character, open Create-a-Class, build any MW2 class, equip it out of combat,
and play it against mobs and players with each part behaving as in MW2, translated to WoW where
MW2 has no equivalent (mobs, bosses, graveyards).

## Non-goals

- MW2 rank/XP progression, challenges and Pro-perk unlock-by-use for regular players. Regular
  players keep level-based unlocks (`UnlockLevel`), extended to attachments and perks.
- Replacing WoW's base run speed, jump, swimming, mounts or flight paths.
- MW2 game modes, maps, scoreboards, prestige, titles and emblems.
- Committing anything from the MW2 or WoW installs. All MW2 art, models, sounds and effects are
  read at runtime from the player's install by asset name.

## Architecture

Server decides, client draws. Every rule lives in the standalone `src/fusion` C++ library with
unit tests in `fusion_tests`; `src/game/Fusion/FusionMgr` stays a thin hook that feeds game state
in and applies results. The client (`benilla-mw2`) predicts for feel (thrown objects appear at
once, camera stances move at once) and renders with MW2 assets; the server's state wins.

Where a WoW system already does the job it is reused: the sentry gun, the attack helicopter, the
Pave Low and the Harrier are server-spawned creatures with fusion AI and MW2 models, so aggro,
targeting, death and visibility come from vmangos.

### Units

MW2 distances are inches; 1 yard = 36 inches. MW2 times are kept in seconds. Every MW2 number in
this document is a default in a server config table (`fusion/mw2_rules.json`, beside
`weapons.json`) and is tunable without a rebuild.

## 1. Foundations

### 1.1 Content catalog

`weapons.json` becomes `catalog.json` (version 2; version 1 files still load as guns only). It
is shared by server and client: the client compiles in the seed and accepts the
`MW2_FUSION_WEAPONS` override, as now. Sections:

- `weapons[]`: the existing 43 entries plus `slot` (`primary` or `secondary`), `attachments[]`
  (the attachment ids the gun allows, in MW2's order), `variant_names{}` (attachment combination
  to MW2 weapon asset name, e.g. `"acog": "m4_acog_mp"`, `"acog+fmj": "m4_acog_fmj_mp"`) and
  `camos` (true when the gun takes camos).
- `attachments[]`: id, name, MW2 icon material, `incompatible[]`, and stat effects: zoom (ADS
  FOV), `range_scale`, `damage_scale`, `spread_scale`, `mag_scale`, `fire_time_scale`,
  `penetration`, `silenced`, `alt_weapon` (grenade launcher, shotgun), `akimbo`, `thermal`,
  `heartbeat`.
- `camos[]`: id, name, icon, and how the install applies it (resolved in build step 4.1).
- `perks[]`: id, tier (1-3), name, icon, MW2 numbers, and Pro effects.
- `deathstreaks[]`: id, deaths needed, effect numbers.
- `equipment[]`: id, kind (`lethal` or `tactical`), MW2 world model, fuse, radius, damage, and
  behaviour numbers.
- `killstreaks[]`: id, kill cost, MW2 assets, and behaviour numbers.
- `default_classes[]`: MW2's five (Grenadier, First Recon, Overwatch, Scout Sniper, Riot
  Control) as read-only templates.

`make_seed_weapons.py` grows into `make_seed_catalog.py`, which writes the seed from MW2's
published numbers. The client fills names, icons, models and sounds from the install by asset
name, and warns once for any asset the install lacks.

### 1.2 Unlocks: `CanUse`

`Fusion::CanUse(level, isGm, item)` in the library, with `FusionMgr::CanUse(Player*, item)`
feeding `GetSession()->GetSecurity() >= SEC_GAMEMASTER` and the level. Every gate goes through
it: shots (`FusionMgr.cpp:365`), held weapon in `HandleState` (`:189`, which today accepts any
id), special rounds (`:194`), the quartermaster, loadout validation, equipment use and streak
call-in. GM passes everything and has Pro perks; regular players unlock attachments and perks by
level, mapped from MW2's rank with `UnlockLevel` as guns are now.

### 1.3 Protocol

New fusion opcodes after 830, `kNumMsgTypes` raised, `kVersion` bumped to 2, every message with
a vector in `src/fusion/vectors/packets.txt` that the client crate also checks:

| Opcode | Name | Carries |
|---|---|---|
| 831 | `CMSG_FUSION_LOADOUT` | the five custom classes (names, slots) and the class to equip |
| 832 | `SMSG_FUSION_LOADOUT` | the saved classes, the equipped class, and pending/accepted/rejected with reason |
| 833 | `CMSG_FUSION_EQUIP_USE` | lethal/tactical throw, plant or detonate, cook time, origin, direction |
| 834 | `CMSG_FUSION_STREAK` | call in streak N, its target (position, direction), and steering/aim samples for controlled streaks |
| 835 | `SMSG_FUSION_ENTITIES` | spawn/update/remove of fusion entities (grenade, claymore, C4, smoke, knife, crate, aircraft, missile) with id, kind, owner, position, velocity, state |
| 836 | `CMSG_FUSION_ACTION` | weapon swap, mantle request, knife lunge, crate capture hold, pick up weapon |

Phase 1 (build steps 1-2) carries the mantle and lunge requests as `CMSG_FUSION_STATE` flags
`STATE_MANTLE` (0x20) and `STATE_LUNGE` (0x40), so it needs no new opcode and no change to the
benilla fork. The opcodes above arrive with the phases that need them.

`SMSG_FUSION_EVENTS` gains kinds: `StreakProgress` (fixed point, tenths), `PerkProc`,
`Deathstreak`, `Flashed`, `Stunned`, `Emp`, `LoadoutApplied`.

### 1.4 Saving

A new characters table, `character_fusion_class`:

| Column | Type |
|---|---|
| `guid` | int unsigned, character |
| `slot` | tinyint, 0-4 |
| `name` | varchar(24) |
| `primary`, `primary_att1`, `primary_att2`, `primary_camo` | smallint |
| `secondary`, `secondary_att` | smallint |
| `lethal`, `tactical` | smallint |
| `perk1`, `perk2`, `perk3`, `deathstreak` | smallint |
| `streak1`, `streak2`, `streak3` | smallint |

Plus `character_fusion.equipped_slot` (255 = none, i.e. the per-WoW-class default gun). Created
by a migration in `sql/migrations` (characters), loaded in `Player::LoadFromDB` and saved with
the player, as vmangos does for its own tables.

### 1.5 Admin character

Raise the existing character "Test" (your GM account, GM 3) to 60 with the mangosd console
command `character level Test 60`. No new account and no config change. Its WoW class
(warrior) still decides spells, armour and health.

## 2. Create-a-Class

### 2.1 Window

A WoW-style dialog drawn by `benilla-mw2` with bevy_ui (a FrameXML frame cannot show MW2 icons,
since benilla's Lua UI only reads textures from WoW's MPQs). It uses WoW's dialog border,
backdrop, button and font art loaded from the WoW install through `mpq://`, and MW2 icons
decoded from the MW2 install. A new public `Mw2Library::image(name)` decodes a material's
colour map or an image by name, cached.

- Opens with **K** or `/cac`. While open it sets a new `FpsState::panel` flag ORed into the
  input-ownership test in `mode.rs`, so the cursor is out and the gun lowered, as for WoW windows.
- **Left column:** the five custom classes (renameable) and MW2's five default classes
  (read-only; "Copy to slot").
- **Right:** the slots Primary, Attachment, Attachment 2 (shown with Bling), Camo, Secondary,
  Secondary attachment, Lethal, Tactical, Perk 1-3, Deathstreak; a Killstreaks tab picking three,
  ordered by cost.
- A slot opens a grid of icons filtered by compatibility (`incompatible[]`, Bling, slot). The
  gun's stat bars (damage, accuracy, range, fire rate, mobility) are computed from the catalog
  with attachments applied.
- **Equip** sends `CMSG_FUSION_LOADOUT`.

### 2.2 Equipping

The server validates every slot with `CanUse` and the compatibility rules, saves, and replies.
The equip applies when the player is out of combat (immediately if already), and on respawn.
One Man Army equips anywhere after its delay. On `LoadoutApplied` the client rebuilds the
viewmodel (primary), the HUD (ammo, equipment counts, perk icons, streak ladder) and its
controls. With no class equipped the per-WoW-class gun of today stays, so regular players see no
change.

### 2.3 Weapons in hand and keys

Primary and secondary, swapped with MW2's drop and raise clips (slots `DROP`, `RAISE`; the
`QUICK_*` clips with Sleight of Hand Pro). Keys while MW2 mode owns input: **mouse wheel** or
**1 / 2** swap, **G** lethal, **Q** tactical, **4 / 5 / 6** killstreaks, **K** Create-a-Class,
**F** (hold) capture a crate or pick up a weapon. 1, 2, 4, 5 and 6 leave WoW's action bar only
while MW2 mode owns the keyboard. Holding Alt for the cursor still reaches the bars.

## 3. Attachments, camos, perks, deathstreaks, equipment

### 3.1 Attachments

Chosen by switching the viewmodel to the variant's MW2 weapon definition. MW2's gun model holds
every attachment's geometry, and each variant's `hideTags` hide the rest, which the client
already applies. Effects:

- **Sights:**
  - Red dot and holographic: sight geometry plus the reticle surface.
  - ACOG: ADS FOV from the variant's `adsZoomFov`.
  - Thermal: a heat-vision post-process while aiming, with creatures and players drawn hot.
- **Silencer:**
  - No radar blip when firing.
  - `range_scale` 0.7.
  - Mob noise radius `kLoudNoiseRadius` scaled ×0.4.
- **FMJ:** shots pass through one target or thin cover, and ignore 25% of a creature's armour
  (the existing armour class factor).
- **Extended mags:** ×1.5 magazine, MW2's per-gun numbers.
- **Grip:** recoil ×0.75.
- **Rapid fire:** `fire_time` ×0.75 (MW2's per-gun numbers where they differ).
- **Tactical knife:** with pistols, a faster melee.
- **Heartbeat sensor:** a HUD screen of enemies and hostile mobs within 30 yards, refreshed every
  2 s; hidden by Ninja Pro.
- **Grenade launcher and underbarrel shotgun:** an alternate weapon toggled with the attachment
  key (**B**), with its own ammo and the MW2 alt clips.
- **Akimbo:** two viewmodels (the variant's left gun), alternate trigger on LMB and RMB, no ADS.

### 3.2 Camos

MW2's eight: Woodland, Desert, Arctic, Digital, Urban, Red Tiger, Blue Tiger, Fall. Build step
4.1 confirms how the install stores them (per-camo gun models in the weapon definition, or
material overrides) and the client applies them to the viewmodel and to the gun other players
see.

### 3.3 Perks

Server rules, MW2 numbers, Pro automatic for GM.

**Tier 1:**
- **Marathon:** unlimited sprint. Pro: mantle faster.
- **Sleight of Hand:** reload time ×0.5. Pro: ADS time ×0.5.
- **Scavenger:** a kill (player or mob worth XP) refills a mag of reserve for every gun, and
  lethal/tactical counts. Pro: start with extra mags.
- **Bling:** two attachments on the primary. Pro: an attachment on the secondary too.
- **One Man Army:** replaces the secondary; hold to swap class anywhere after 4 s
  (Pro: 2 s).

**Tier 2:**
- **Stopping Power:** bullet damage ×1.4 (not explosives).
- **Lightweight:** move speed ×1.07. Pro: faster ADS after sprint.
- **Hardline:** streaks cost one kill less.
- **Cold-Blooded:** hidden from UAV, air support targeting and thermal, and no red name.
- **Danger Close:** explosive damage ×1.4 for killstreaks and the grenade launcher.

**Tier 3:**
- **Commando:** lunge range 4 to 8 yards; no fall damage.
- **Steady Aim:** hip-fire spread ×0.65.
- **Scrambler:** enemy radars within 15 yards show noise.
- **Ninja:** silent footsteps, and the existing detection ×0.7 against mobs. Pro: hidden from
  heartbeat sensors.
- **SitRep:** enemy equipment highlighted through walls.
- **Last Stand:** at 0 health you go down with your pistol for 10 s before dying (vmangos death
  is deferred while downed).

**Deathstreaks:** triggered after N deaths without a kill (any player or creature death counts,
and a streak kill of any value resets the count).
- **Copycat** (4): copy your killer's class on respawn.
- **Painkiller** (3): ×3 health for 10 s after respawn.
- **Martyrdom** (4): drop a live frag on death.
- **Final Stand** (4): as Last Stand, but you can recover.

### 3.4 Equipment

Fusion entities simulated on the server, with physics against the map's vmaps, owner and team,
and cleanup on owner death or logout. The client predicts the throw arc and draws MW2 world
models and sounds. Equipment hurts players through `CanAttack` and creatures through the fusion
damage model.

**Lethal:**
- **Frag:** cookable fuse 3.5 s, bounces, radius 8 yards (MW2's 256 in falloff).
- **Semtex:** sticks to the first surface or unit it hits, fuse 2.5 s.
- **Throwing knife:** a projectile that kills a player on hit and does heavy damage to a mob;
  it can be picked back up.
- **C4:** thrown and sticks; detonate with a second press of the lethal key; two at once.
- **Claymore:** planted, a directional cone tripwire of 4 yards that triggers on enemy players
  and hostile mobs; two at once.
- **Blast Shield:** worn; explosive damage ×0.5 to the wearer.
- **Tactical Insertion:** a planted marker; on death the release spot is the marker instead of
  the graveyard (corpse run waived). Enemies can destroy it.

**Tactical:**
- **Flashbang:** players facing it are blinded (white screen) and deafened for up to 5 s by
  distance and angle. Creatures are disoriented: they stop attacking and wander for the same
  time. Bosses are immune.
- **Stun:** players have move and turn speed ×0.3 for up to 4 s. Creatures have move and attack
  speed ×0.5.
- **Smoke:** a 6-yard sphere for 15 s that blocks line of sight in `ValidateShot`, in creature
  detection and in creature targeting.

### 3.5 Effects

Explosions, smoke and muzzle effects use MW2's FX when the asset layer can play them (verified in
build step 6.1); otherwise first-pass particle stand-ins with MW2 sounds, and MW2's FX as a later
step.

## 4. Killstreaks

### 4.1 Earning

The streak counter becomes fixed point in tenths (`Fusion::StreakValue` returns tenths):

| Kill | Value |
|---|---|
| Enemy player | 1 |
| Normal creature worth XP (not gray, by WoW's gray-level rule, applied as if not capped at 60) | 0.1 |
| Elite, rare elite or boss | 1 ÷ N to each of the N players who damaged it |
| Gray creature, critter | 0 |

A streak is earned when the counter reaches its whole cost (minus one with Hardline). Death
resets the counter. The HUD ladder shows progress such as "3.4 / 5".

### 4.2 Costs

MW2's: UAV 3, Care Package 4, Counter-UAV 4, Sentry Gun 5, Predator Missile 5, Precision
Airstrike 6, Harrier Strike 7, Attack Helicopter 7, Emergency Airdrop 8, Pave Low 9, Stealth
Bomber 9, Chopper Gunner 11, AC-130 11, EMP 15, Tactical Nuke 25. The player picks three; earned
streaks are kept until used, as in MW2, and a Care Package streak goes into the same inventory.

### 4.3 Behaviour

The `Airspace` rule (two aerial streaks per 200-yard cell) applies to everything airborne.

- **UAV:** enemy players and hostile creatures on the radar for 30 s, refreshed every 3 s.
- **Counter-UAV:** enemy radars show noise for 30 s.
- **EMP:** for 60 s, enemy radar, red-dot, holo and ACOG reticles and HUD are disabled. Mechanical
  creatures (by creature type) are stunned for 5 s.
- **Care Package / Emergency Airdrop:** throw a smoke marker. A helicopter (MW2 model, server
  path) drops one or four crates. Hold F for 3 s (owner 1 s) to capture a random streak or ammo;
  enemies can steal them.
- **Sentry Gun:** carried, then placed. It is a server creature with fusion targeting (90° arc,
  MW2 fire rate and damage) that lasts 90 s and can be destroyed.
- **Precision Airstrike / Stealth Bomber:** a WoW-style map window picks the point (and the
  direction for the bomber). Aircraft fly a server path and drop ordnance in a line. The bomber
  is hidden from radar.
- **Harrier Strike:** the airstrike, then a Harrier creature hovers over the point for 45 s and
  attacks enemies.
- **Attack Helicopter / Pave Low:** creatures that circle the caller's area for 60 s (Pave Low
  armoured, 2× health, flares) and attack enemies; they can be shot down.
- **Predator Missile / Chopper Gunner / AC-130:** the client camera becomes the missile or
  gunship with MW2's thermal overlay and HUD, and sends steering and aim at 20 Hz. The server
  simulates the missile or gun rounds (AC-130: 25 mm, 40 mm, 105 mm). The player's WoW body
  stays where it stood and can be hurt.
- **Tactical Nuke:** the existing countdown; the impact kills every non-boss unit in 200 yards.

### 4.4 PvE balance

Streak and equipment damage to creatures goes through the fusion damage model. Elites take
×0.5. Dungeon and raid bosses (by vmangos boss flag and map type) take at most 5% of their
maximum health per streak use, from config key `Fusion.Streak.BossCapPct`.

## 5. Movement

Layered on WoW's base movement. Off while mounted, swimming or on a flight path.

### 5.1 Stances

- **Camera eye height:** the client scales the first-person eye height by MW2's ratios
  (stand 60, crouch 40, prone 11 inches → ×1, ×0.667, ×0.183 of the character's eye height).
  Transitions take 0.2 s stand↔crouch and 0.5 s to and from prone.
- **Keys:** **C** toggles crouch, holding **C** for 0.3 s goes prone, **Ctrl** toggles prone.
- **Prone limits:** turning limited to ±60° from the facing at go-prone (beyond that, the body
  turns slowly), no sprint, no jump, hip spread ×0.5.
- **Server:** already scales speed (stand 1.0, crouch 0.6, prone 0.2) and hitbox height. These
  numbers move to `mw2_rules.json`.
- **Viewmodel:** MW2 bob per stance (IW4L `view_bob`, `sway`).

### 5.2 Sprint

Hold **Shift**. The existing 4 s stamina (1/s regen) is shown as a HUD bar. Marathon gives
infinite stamina and Lightweight its speed; both are now fed in, replacing the hard-coded
`false` at `FusionMgr.cpp:263`. The viewmodel plays `SPRINT_IN`, `SPRINT_LOOP` and `SPRINT_OUT`.
There is no firing or ADS while sprinting, and the 0.25 s sprint-out delay (`kSprintOutTime`,
now enforced) applies before firing.

### 5.3 Speeds

The per-gun `move_speed_scale` and `ads_move_speed_scale` apply to whichever gun is held,
secondaries included, with attachment and perk effects.

### 5.4 Mantle

**Space** while standing and moving forward sends a mantle request (the WoW jump still
happens). The server checks for a ledge rising 0.5-1.4 yards within 1.2 yards ahead, with
headroom, against vmaps. If there is one, it moves the player over it in 0.6 s (Marathon Pro:
0.4 s) with a knock-back impulse (`Unit::KnockBack`), which replaces the jump's arc. MW2's mantle
hint shows at terrain ledges, the only geometry the client can probe; mantling works on all
geometry.

### 5.5 Knife lunge

**V** with an enemy within lunge range (4 yards, Commando 8) and in front: the client dashes to
the target and the server validates the range. A player dies in one hit; a creature takes
melee damage of 3× the current gun's body-shot damage (tunable), since a one-hit kill would
trivialise PvE.

### 5.6 Jump

WoW's jump stays. Landing applies MW2's spread penalty for 0.3 s and the viewmodel landing dip.
Airborne fire has client-side bloom ×2 (`kAirborneSpreadMultiplier` becomes 2); the server does
not simulate spread, since the client's ray picks the target.

## 6. Testing and verification

- **Unit tests:** every rule above gets a `fusion_tests` case:
  - `CanUse` (GM and level).
  - Loadout validation and compatibility.
  - Each perk's number.
  - Deathstreak counting.
  - Streak values (player, normal, gray, elite split) and costs with Hardline.
  - Equipment physics: bounce, stick, fuse, cook.
  - Claymore cone and smoke line-of-sight blocking.
  - Flash and stun falloff.
  - Airspace, and the boss cap.
- **Protocol:** each new message has a shared vector checked by both `fusion_tests` and the
  client's protocol tests.
- **Live checks:** every gameplay step is shown in a live run (auto-login with your GM account on "Test",
  `DIAG`-tagged logging removed afterwards, screenshots), with Justin providing input where a
  step needs it. A step is done when its instrument shows the expected numbers at the reported
  spot.

## 7. Build order

Each step is usable on its own and lands as its own commits:

1. **Admin:** "Test" to 60; `CanUse` with GM unlock-all, routed through every existing gate.
2. **Movement:** stance camera and transitions, sprint (clips, HUD, perks hooks), speeds,
   mantle, knife lunge, jump penalties.
3. **Catalog and saving:** `catalog.json` v2 and its generator, `mw2_rules.json`, the
   `character_fusion_class` table, opcodes 831-832.
4. **Create-a-Class:**
   1. Verify how the install stores camos and attachment variants.
   2. The window and icons.
   3. Primary and secondary with attachments and camos, swap clips and keys.
5. **Perks and deathstreaks.**
6. **Equipment:**
   1. Verify MW2 FX playback in the asset layer.
   2. Entities, opcodes 833 and 835, each lethal and tactical.
7. **Killstreaks:** earning (tenths, elite split), then info streaks and drops, then sentry and
   AI aircraft, then targeted strikes, then player-controlled streaks and the nuke.

## Risks

- **World models:** aircraft, equipment and other players' guns need MW2 world models. The
  asset layer keeps only viewmodels today, so step 4.1 extends it to world XModels.
- **Controlled streaks:** these need client camera modes and a 20 Hz input stream; they are
  built last.
- **MW2 FX playback** may not exist in IW4L; stand-ins keep the steps unblocked.
- **Movement prediction:** mantle and lunge move the player server-side through vmangos movement
  splines; the client must not fight them.
- **Scale:** this is several weeks of work. The build order keeps each step playable and
  reviewable.
