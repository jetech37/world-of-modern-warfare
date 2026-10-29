# MW2 Phase 1: Admin Character and MW2 Movement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build steps 1-2 of the spec: the level 60 admin character with GM unlock-all, and MW2
stances and movement on top of WoW's.

**Architecture:** Rules go in the standalone `src/fusion` C++ library with `fusion_tests`. The
game hook is `src/game/Fusion/FusionMgr`. The client is `benilla-mw2` (Rust, Bevy). Mantle and
lunge requests ride on two new `CMSG_FUSION_STATE` flag bits, so this phase needs no new opcode
and no change to the benilla fork. The server moves the player for a mantle or a lunge with
vmangos `Unit::KnockBack`, which benilla's client already handles (`player/walk.rs`).

**Tech Stack:** C++17 (vmangos, CMake, Docker), Rust 2024 with Bevy (benilla-mw2), IW4L crates
(`weapon_iw4::view_bob`).

**Spec:** `docs/superpowers/specs/2026-10-03-mw2-create-a-class-movement-killstreaks-design.md`
(sections 1.2, 1.5, 5). Read it first.

## Global Constraints

- **Repos and branches:**
  - Server: `~/azeroth-warfare-server` in WSL Ubuntu, branch `fusion`. Run every shell command
    there through `wsl.exe -d Ubuntu -- bash <script>`.
  - Client: `C:\Users\<you>\benilla-mw2`, branch `main`.
  - Commit on these branches with explicit paths, ending each message with
    `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - Push the client to `origin main`. Do not push the server repo.
- **Docker image builds** must keep `AW_BUILD_JOBS=6`, already in `.env`. Without it the build
  runs out of WSL memory.
- **MW2 units** are inches. Eye heights: stand 60, crouch 40, prone 11 (`weapon_iw4`
  `VIEWHEIGHT_TARGET_*`). 1 yard = 36 inches.
- **Stance transitions:** 0.2 s stand↔crouch, 0.5 s to and from prone. Holding C for 0.3 s goes
  prone.
- **Prone limits:** turning past ±60° from the facing at go-prone is damped to ×0.25 mouse yaw.
  Hip spread ×0.5. No sprint, no jump.
- **Sprint:** 4 s stamina at 1/s regen, Marathon unlimited, Lightweight ×1.07. No fire or ADS
  while sprinting or for 0.25 s after.
- **Airborne and landing:** airborne bloom ×2, client-side (`kAirborneSpreadMultiplier` becomes
  2). Landing: bloom +0.35 for 0.3 s and the viewmodel landing dip.
- **Mantle:** a ledge rising 0.5-1.4 yd within 1.2 yd ahead, 2 yd of headroom above it. Duration
  0.6 s (Marathon Pro 0.4 s; Pro is phase 5, so 0.6 s here). WoW gravity is 19.2911 yd/s².
- **Knife lunge:** 4 yd (Commando 8), 60° cone in front. Kills a player. A creature takes 3× the
  held gun's body-shot health damage.
- **GM:** security ≥ `Fusion.Admin.MinSecurity` (default 3, `SEC_GAMEMASTER`) passes every
  unlock gate.
- **MW2 movement is off** while mounted, swimming, dead, on a taxi, or with MW2 mode off (F10).
- **Temporary diagnostics:** tagged `DIAG` and removed before the task's commit.

## Review Focus

1. **F10 (MW2 mode off) mid-crouch or mid-prone:** the camera returns to standing height in the
   same frame, and the server stance goes back to Stand. Tested in Task 6.
2. **Death or respawn while prone:** the stance resets to Stand and the eye offset clears.
   Tested in Task 6.
3. **Mounting, swimming or a taxi while crouched:** no eye offset, no mantle or lunge requests,
   and the server ignores stance speed (`IsMounted` is already handled). Tested in Tasks 6
   and 10.
4. **Mantle spam, a mantle while airborne, or a mantle under a low ceiling:** the server sends no
   knock-back. Tested in Task 4.
5. **Lunge at a target the player may not attack** (same faction without FFA, sanctuary, a pet
   of a friendly): no dash and no damage. Tested in Task 5, through `IsValidAttackTarget` and
   `Fusion::CanAttack`.

---

### Task 0: Commit the setup fixes, and a fast fusion test loop

**Files:**
- Commit as is: `azeroth-warfare/Dockerfile`, `azeroth-warfare/docker-compose.yml`, `azeroth-warfare/scripts/extract-data.sh`, `azeroth-warfare/scripts/run.sh`, `azeroth-warfare/scripts/setup-db.sh`.
- Create: `azeroth-warfare/.gitignore`, `.dockerignore`, `azeroth-warfare/Dockerfile.fusion-tests`, `azeroth-warfare/scripts/fusion-tests.sh`.

- [ ] **Step 1: Ignore local state.**
  - `azeroth-warfare/.gitignore` lists `.env`, `data/` and `*.zip`.
  - The root `.dockerignore` lists `azeroth-warfare/.env`, `azeroth-warfare/data` and `azeroth-warfare/*.zip`, so secrets and map data stay out of the build context.
- [ ] **Step 2: Write the test image.** `Dockerfile.fusion-tests` is `FROM ubuntu:24.04` plus `build-essential cmake ninja-build`.
- [ ] **Step 3: Write `scripts/fusion-tests.sh`.** It builds that image as `azeroth-warfare-fusion-tests`, then runs
  `cmake -S /repo/src/fusion -B /build -G Ninja && cmake --build /build && /build/fusion_tests`, with the repo mounted read-only at `/repo` and a named volume `aw-fusion-build` at `/build`. It exits with the tests' status.
- [ ] **Step 4: Run it.** `./scripts/fusion-tests.sh` from `azeroth-warfare/`.
  Expected: the last line is `53 tests, 0 failed checks`, within a couple of minutes on the first run.
- [ ] **Step 5: Commit.** Two commits:
  - "Setup fixes: build jobs, mangos.sql dump name, DBC path" (the five files).
  - "Fusion: fast test loop" (the four new files).

### Task 1: Raise "Test" to 60

**Files:** none (a database change).

- [ ] **Step 1: Check the character is offline.** `SELECT online FROM characters.characters WHERE name='Test'` returns `0`. If it returns 1, ask Justin to log out.
- [ ] **Step 2: Set the level.** `UPDATE characters.characters SET level=60, xp=0 WHERE name='Test'`. vmangos recomputes stats for the level on login (`InitStatsForLevel`).
- [ ] **Step 3: Verify.** `SELECT name, level FROM characters.characters` shows `Test 60`.

### Task 2: `CanUse` in the fusion library

**Files:**
- Create: `src/fusion/include/fusion/Access.h`, `src/fusion/src/Access.cpp`.
- Test: `src/fusion/tests/AccessTests.cpp`.

**Interfaces:**
- Produces:
  - `bool Fusion::CanUseWeapon(uint32_t level, bool gm, WeaponProfile const& w)`
  - `bool Fusion::CanUseAmmo(uint32_t level, bool gm, AmmoType t)`
  - `uint32_t Fusion::EffectiveLevel(uint32_t level, bool gm)`, which returns 60 for a GM.

- [ ] **Step 1: Write the failing tests.**
  - `TEST(gm_uses_every_gun)`: for every weapon in the seed table, `CanUseWeapon(1, true, w)`.
  - `TEST(level_gates_non_starters)`: for a weapon with `starter=false` and `mw2UnlockRank=70`:
    - `!CanUseWeapon(59, false, w)` and `CanUseWeapon(60, false, w)`.
    - A starter: `CanUseWeapon(1, false, starter)`.
  - `TEST(gm_uses_every_round)`:
    - `CanUseAmmo(1, true, AmmoType::Blessed)`.
    - `!CanUseAmmo(29, false, AmmoType::Blessed)`.
    - `EffectiveLevel(5, true) == 60`, `EffectiveLevel(5, false) == 5`.
- [ ] **Step 2: Run them.** `./scripts/fusion-tests.sh` fails to compile: `fusion/Access.h` not found.
- [ ] **Step 3: Implement.** Using the existing `UnlockLevel(mw2Rank)` and `AmmoUnlockLevel(type)`.
- [ ] **Step 4: Run again.** All pass; the count rises by 3 tests.
- [ ] **Step 5: Commit.** "Fusion: CanUse, GM passes every unlock gate".

### Task 3: Route every gate through `CanUse`

**Files:**
- Modify:
  - `src/game/Fusion/FusionMgr.h`: add `bool IsAdmin(Player const*) const;` and a member `uint32 m_adminMinSecurity`.
  - `src/game/Fusion/FusionMgr.cpp`: `Load` at `:127`, `HandleState` at `:189` and `:194`, the rearm at `:217-231`, `HandleShots` at `:365`.
  - `src/mangosd/mangosd.conf.dist.in`, in the AZEROTH WARFARE section: add `Fusion.Admin.MinSecurity = 3` with a comment line.

- [ ] **Step 1: Read the config.** `Load` reads `Fusion.Admin.MinSecurity` (default 3). `IsAdmin` returns `player->GetSession()->GetSecurity() >= m_adminMinSecurity`.
- [ ] **Step 2: Gate shots.** `HandleShots :365` becomes `if (!Fusion::CanUseWeapon(shooter->GetLevel(), IsAdmin(shooter), *w)) continue;`.
- [ ] **Step 3: Gate the held weapon.** `HandleState :189` stores `heldWeaponId` only when the weapon exists and `CanUseWeapon` passes; otherwise it stores 0. This closes the hole where a locked gun's move speed could be claimed.
- [ ] **Step 4: Gate rounds and the rearm.**
  - The special round check at `:194` becomes `CanUseAmmo(...)`.
  - The rearm's `QuartermasterRearm(player->GetLevel())` becomes `QuartermasterRearm(Fusion::EffectiveLevel(player->GetLevel(), IsAdmin(player)))`.
- [ ] **Step 5: Build the image.** `docker compose --profile setup build setup`. It ends with the fusion tests passing (one more file of tests).
- [ ] **Step 6: Restart and check.** `docker compose up -d --force-recreate realmd mangosd`. `docker compose logs mangosd` shows `World initialized.`
- [ ] **Step 7: Commit.** "Fusion: GM unlock-all through CanUse at every gate".

### Task 4: Movement rules (library)

**Files:**
- Modify: `src/fusion/include/fusion/Movement.h`, `src/fusion/src/Movement.cpp`, `src/fusion/include/fusion/Packets.h` (flags only).
- Test: `src/fusion/tests/RulesTests.cpp` (append).

**Interfaces:**
- Produces:
  - `struct PerkFlags { bool marathon = false; bool marathonPro = false; bool lightweight = false; bool commando = false; };`
  - `constexpr float kSprintOutTime = 0.25f;` (exists) and `bool SprintOutReady(float secondsSinceSprint, bool sprinting)`.
  - `kAirborneSpreadMultiplier` changed to `2.f`.
  - Mantle constants: `kMantleMinRise = 0.5f`, `kMantleMaxRise = 1.4f`, `kMantleReach = 1.2f`, `kMantleHeadroom = 2.f`, `kWowGravity = 19.2911f`, `kMantleSeconds = 0.6f`.
  - `struct MantleProbe { float feetZ; float groundAhead[3]; bool headroomClear; bool airborne; };`, with heights sampled at 0.4, 0.8 and 1.2 yd ahead.
  - `std::optional<float> MantleRise(MantleProbe const&)`: the highest rise in [0.5, 1.4], or none.
  - `struct Impulse { float horizontal; float vertical; };` and `Impulse MantleImpulse(float rise)`.
    - `vertical = sqrt(2 * g * (rise + 0.3))`.
    - `horizontal = kMantleReach / (2 * vertical / g)`, i.e. reach the far edge at the end of the arc.
  - `float LungeRange(PerkFlags const&)`: 4, or 8 with Commando.
  - `bool InLungeCone(Vec3 from, float facing, Vec3 to)`: within ±30° of facing.
  - `Impulse LungeImpulse(float distance)`: `horizontal = distance / 0.25f`, capped at 30, and `vertical = 2.f`.
  - `float LungeCreatureDamage(float bodyShotHealth)`: returns `3 * bodyShotHealth`.
  - `Packets::STATE_MANTLE = 0x20` and `Packets::STATE_LUNGE = 0x40`.

- [ ] **Step 1: Write the failing tests.**
  - `TEST(sprint_out_delay)`:
    - `!SprintOutReady(0.1f, false)`, `SprintOutReady(0.25f, false)` and `!SprintOutReady(5.f, true)`.
  - `TEST(mantle_rise_window)`: with `feetZ = 0`, headroom clear, not airborne:
    - Ahead `{0, 1.0, 1.0}` gives `1.0`.
    - `{0.3, 0.3, 0.3}` gives none (too low).
    - `{0, 2.0, 2.0}` gives none (too high).
  - `TEST(mantle_refused_airborne_or_ceiling)`: the same probe with `airborne = true` gives none, and with `headroomClear = false` gives none.
  - `TEST(mantle_impulse_clears_ledge)`: for rise 1.0, the apex `v²/(2g)` is ≥ 1.3, and the time to apex times `horizontal` is ≥ 0.6.
  - `TEST(lunge_range_and_cone)`:
    - `LungeRange({}) == 4` and `LungeRange({.commando=true}) == 8`.
    - `InLungeCone({0,0,0}, 0, {3,0,0})` and `!InLungeCone({0,0,0}, 0, {0,3,0})`.
  - `TEST(lunge_creature_damage)`: `LungeCreatureDamage(100) == 300`.
  - `TEST(state_flag_values)` in `PacketTests.cpp`: `STATE_MANTLE == 0x20`, `STATE_LUNGE == 0x40`, and neither overlaps the existing flags.
- [ ] **Step 2: Run them.** `./scripts/fusion-tests.sh` fails to compile.
- [ ] **Step 3: Implement** in `Movement.cpp` and `Packets.h`.
- [ ] **Step 4: Run again.** All pass.
- [ ] **Step 5: Commit.** "Fusion: sprint-out, mantle, lunge and perk-flag rules".

### Task 5: Movement in the game hook

**Files:**
- Modify:
  - `src/game/Fusion/FusionMgr.h`: in `FusionPlayerData`, add `Fusion::PerkFlags perks;`, `uint32 sprintEndedMs = 0;` and `uint32 lastMoveActionMs = 0;`. Add the private methods `void TryMantle(Player*)` and `void TryLunge(Player*)`.
  - `src/game/Fusion/FusionMgr.cpp`: `MoveSpeedMultiplier :168`, `HandleState`, `UpdatePlayer :262-268`, `HandleShots`.

- [ ] **Step 1: Feed the perks in.**
  - `MoveSpeedMultiplier` sets `m.lightweight = d.perks.lightweight`.
  - `UpdatePlayer` passes `d.perks.marathon` to `stamina.Update`. This replaces the `false` at `:263`.
  - When `sprinting` turns false, record `d.sprintEndedMs = now`.
- [ ] **Step 2: Enforce the sprint-out delay.** In `HandleShots`, after the weapon lookup, skip the shot unless `Fusion::SprintOutReady((now - d.sprintEndedMs) / 1000.f, d.sprinting)`.
- [ ] **Step 3: Handle the new flags.** In `HandleState`, with at most one move action per 500 ms per player (`lastMoveActionMs`):
  - `STATE_MANTLE` calls `TryMantle`.
  - `STATE_LUNGE` calls `TryLunge`.
  - Neither runs while mounted, in a taxi, dead, or in water (`IsInWater()`).
- [ ] **Step 4: `TryMantle`.**
  - Build the `MantleProbe`:
    - Ground heights from `GetMap()->GetHeight(x, y, z + 2)` at 0.4, 0.8 and 1.2 yd along the orientation.
    - `headroomClear` from `GetMap()->isInLineOfSight` between `feet + rise + 0.2` and `+ kMantleHeadroom` above the far sample.
    - `airborne = player->m_movementInfo.HasMovementFlag(MOVEFLAG_FALLING)`.
  - If `MantleRise` returns a rise, call `player->KnockBack(player->GetOrientation(), imp.horizontal, imp.vertical)`. `KnockBack(angle, h, v)` is at `Unit.cpp:9953`; check the sign convention of `angle` there, since it may push away from the angle. Pass whatever angle moves the player forward.
- [ ] **Step 5: `TryLunge`.**
  - Find the nearest unit within `LungeRange(d.perks)` that is `InLungeCone`, `IsAlive`, `IsValidAttackTarget`, and passes `Fusion::CanAttack` for players. Use a `MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck` searcher, as other vmangos code does.
  - Knock the player back toward it with `LungeImpulse(distance)`.
  - Damage:
    - A player target: `DealDamage(target, target->GetHealth(), ...)`.
    - A creature: `LungeCreatureDamage` of the body-shot health. That is the `CodDamage` → `ToCreatureHealth` path from `HandleShots :442-460`, with `HitLocation::TorsoUpper` at distance 1 with the held weapon (fall back to the class's starter gun if `heldWeaponId` is 0). Factor that path out into `float BodyShotHealth(Player*, Unit*, WeaponProfile const&, Fusion::HitLocation, float distance)` and use it from both places.
  - Send the same `Hit`/`Kill`/`Hurt` events as a shot, and call `OnKill`.
- [ ] **Step 6: Build the image** and restart `realmd` and `mangosd` (Task 3, steps 5-6).
- [ ] **Step 7: Commit.** "Fusion: sprint-out, mantle and knife lunge in game".

### Task 6: Client stance view (camera height, transitions, keys)

**Files (client repo):**
- Create: `src/stance_view.rs`.
- Modify: `src/lib.rs` (add the plugin), `src/fusion.rs:298-363` (`update_stance`), `src/input.rs` (add `jump: Control` for Space, read-only, blanked only while prone or crouched).

**Interfaces:**
- Produces:
  - `pub fn eye_scale(stance: Stance) -> f32`: 1, 40/60 and 11/60.
  - `pub fn transition_secs(from: Stance, to: Stance) -> f32`: 0.5 if either is Prone, else 0.2.
  - `#[derive(Resource)] pub struct StanceView { pub scale: f32, from: f32, to: f32, t: f32, dur: f32 }` with `fn retarget(&mut self, to: f32, dur: f32)` and `fn step(&mut self, dt: f32) -> f32`, eased with smoothstep.
  - `Fusion.stance_changed_at: f32`.

- [ ] **Step 1: Write the failing unit tests** in `stance_view.rs`:
  - `eye_scale_matches_mw2`: `eye_scale(Crouch) ≈ 0.6667` and `eye_scale(Prone) ≈ 0.1833`.
  - `transition_times`: `transition_secs(Stand, Crouch) == 0.2` and `transition_secs(Crouch, Prone) == 0.5`.
  - `view_reaches_target`: after `retarget(0.5, 0.2)` and `step(0.1)` the scale is between 0.5 and 1. After a further `step(0.2)` it equals 0.5.
  - `hold_c_goes_prone`: the pure `fn stance_input(current, c_pressed, c_held_secs, ctrl_pressed, space_pressed) -> Stance`:
    - A press toggles crouch.
    - Holding 0.3 s gives Prone.
    - Ctrl toggles prone.
    - Space from Prone gives Crouch, and from Crouch gives Stand.
- [ ] **Step 2: Run them.** `cargo test --release -p benilla-mw2 stance` fails: unresolved items.
- [ ] **Step 3: Implement.**
  - **`update_stance`:** uses `stance_input`. It forces `Stance::Stand` when `!state.active`, when the player is dead (`UnitIsDeadOrGhost("player")` through `lua::eval`, polled every 0.5 s as `weapon.rs` polls), or when mounted (`IsMounted()`), or swimming (`IsSwimming()`).
  - **The view system:** `apply_stance_view` runs in `Update`, `.in_set(Mw2Set::Aim)` (after `WorldStage::Input`, where benilla seats the camera).
    - It reads the camera `Transform` and the player's `GlobalTransform`.
    - It takes `stand_eye = cam.translation.y - feet.y`, then subtracts `(1 - view.scale) * stand_eye` from `cam.translation.y`.
    - It does nothing while `!state.active`, and resets `view.scale` to 1 at once in that case.
  - **Space** is blanked (WoW's jump suppressed) only while Prone or Crouch.
- [ ] **Step 4: Run the tests.** All pass. `cargo clippy --release -p benilla-mw2 --no-deps -- -D warnings` is clean.
- [ ] **Step 5: Live check.**
  - Run the scratch `run.ps1` with temporary `DIAG` logging of `view.scale` and the camera Y offset.
  - Justin presses C, holds C, presses Ctrl, presses Space, then F10.
  - Expected: offsets of about −0.33 and −0.82 of the stand eye height, and back to 0 on F10.
  - Take a screenshot at each stance. Remove the `DIAG` lines.
- [ ] **Step 6: Commit and push.** "Stances: MW2 eye heights, transitions, hold-C prone, Space to rise".

### Task 7: Prone limits and stance spread

**Files:**
- Modify: `src/input.rs` (yaw damping), `src/weapon.rs:379-392` (bloom).

**Interfaces:**
- Consumes: `Fusion.stance`.
- Produces: `pub fn prone_yaw_scale(offset_deg: f32) -> f32`, returning 1 within ±60° and 0.25 beyond.

- [ ] **Step 1: Write the failing tests.**
  - `prone_yaw_scale(30.0) == 1.0` and `prone_yaw_scale(75.0) == 0.25`.
  - `prone_spread_halved`: the bloom target helper `fn bloom_rest(moving: f32, ads: f32, stance: Stance, airborne: bool) -> f32` gives half for Prone versus Stand, and twice for airborne.
- [ ] **Step 2: Run them.** They fail.
- [ ] **Step 3: Implement.**
  - **Yaw damping:** in `capture_input` (`PreUpdate`), while Prone, multiply the x of `AccumulatedMouseMotion.delta` by `prone_yaw_scale(yaw - prone_yaw0)`. `prone_yaw0` is recorded when the stance became Prone. Use the player body's yaw from its `GlobalTransform`.
  - **Bloom:** `operate_weapon` uses `bloom_rest`.
- [ ] **Step 4: Run the tests and clippy.** Both pass.
- [ ] **Step 5: Commit and push.** "Prone: damped turning past 60°, half hip spread; airborne bloom ×2".

### Task 8: Sprint (stamina mirror, HUD bar, gating, viewmodel clips)

**Files:**
- Create: `src/sprint.rs`.
- Modify: `src/fusion.rs` (gait uses stamina), `src/weapon.rs` (no fire or ADS while sprinting or within 0.25 s after), `src/hud.rs` (a stamina bar under the ammo, shown only while below max), `src/mw2/viewmodel.rs` (sprint clips).

**Interfaces:**
- Produces: `#[derive(Resource)] pub struct Sprint { pub stamina: f32, pub sprinting: bool, pub ended_at: f32 }`, with:
  - `fn update(&mut self, dt: f32, wants: bool, marathon: bool, t: f32)`, mirroring `Fusion::SprintStamina`: max 4, regen 1/s.
  - `fn fire_ready(&self, t: f32) -> bool`: `!sprinting && t - ended_at >= 0.25`.

- [ ] **Step 1: Write the failing tests.**
  - `stamina_runs_out_in_4s`: sprinting for 4.1 s at dt 0.1 ends with `!sprinting`.
  - `regen_1_per_s`: from 0, 2 s idle gives 2.
  - `marathon_never_runs_out`.
  - `fire_waits_after_sprint`: `!fire_ready(ended_at + 0.1)` and `fire_ready(ended_at + 0.3)`.
- [ ] **Step 2: Run them.** They fail.
- [ ] **Step 3: Implement.**
  - **Gait:** `update_stance` sets `Gait::Sprint` only while `sprint.sprinting`.
  - **Gating:** `operate_weapon` and `aim_down_sights` return early when `!sprint.fire_ready(t)`.
  - **HUD:** a 120×4 px bar at 60% white.
  - **Viewmodel `animate`:**
    - When the gait becomes Sprint, play `SPRINT_IN`, then loop `SPRINT_LOOP` (as `IDLE` loops).
    - When it stops, play `SPRINT_OUT` as a one-shot.
    - Read `Res<Fusion>` for the gait.
- [ ] **Step 4: Run the tests and clippy.** Both pass.
- [ ] **Step 5: Live check.** Justin sprints for 5 s, then fires straight away. Log the stamina and the first shot time after the sprint ends (≥ 0.25 s). Take a screenshot mid-sprint, showing the sprint pose and the bar.
- [ ] **Step 6: Commit and push.** "Sprint: stamina bar, MW2 sprint clips, no fire for 0.25 s after".

### Task 9: View bob and landing

**Files:**
- Modify: `Cargo.toml` (add `weapon_iw4.workspace = true` under `[dependencies]`), `src/mw2/viewmodel.rs`, `src/weapon.rs`.

**Interfaces:**
- Consumes: `weapon_iw4::view_bob::{view_org_bob, ViewOrgBobInputs, viewweapon_land_origin_z}` and `Fusion.stance`, `Fusion.gait`.
- Produces: `fn bob_inputs(stance: Stance, sprinting: bool, ads: f32, speed_ips: f32, cycle: u8) -> ViewOrgBobInputs`, with `view_height_target` 60, 40 or 11 and `pm_flags` SPRINTING when sprinting.

- [ ] **Step 1: Write the failing tests.**
  - `bob_inputs_map_stances`: Prone gives a `view_height_target` of 11.
  - `still_has_no_bob`: `view_org_bob(bob_inputs(.., speed 0, ..))` is zero.
- [ ] **Step 2: Run them.** They fail.
- [ ] **Step 3: Implement.**
  - **Bob:** the viewmodel root's local translation adds `(horizontal, vertical, 0)` inches from `view_org_bob`. The bob cycle advances with the distance moved, `cycle = (dist_in / 12) as u8` wrapping.
  - **Landing:** when the player stops being airborne, record the landing time.
    - Apply `viewweapon_land_origin_z(ms, -land_change)` with `land_change` 4 inches.
    - Add +0.35 bloom, held for 0.3 s.
- [ ] **Step 4: Run the tests and clippy.** Both pass.
- [ ] **Step 5: Commit and push.** "Viewmodel: MW2 view bob per stance, landing dip and spread".

### Task 10: Mantle and lunge requests

**Files:**
- Modify: `src/fusion.rs` (`send_state` flags), `src/input.rs`, `src/weapon.rs` (V path), `src/hud.rs` (the mantle hint).

**Interfaces:**
- Consumes: `STATE_MANTLE = 0x20` and `STATE_LUNGE = 0x40`, defined locally as `const`s in `fusion.rs` to match the server's `Packets.h`.
- Produces: `fn mantle_hint(feet_y: f32, ahead: [Option<f32>; 3]) -> bool`, using the spec window 0.5-1.4 on terrain heights from `benilla_world` `terrain_height_under`.

- [ ] **Step 1: Write the failing tests.**
  - `mantle_hint`: `[Some(0.0), Some(1.0), Some(1.0)]` at feet 0 gives true, and `[Some(0.2); 3]` gives false.
  - `flags_carry_once`: `send_state` with `fusion.mantle = true` writes one State with bit 0x20, and the next frame's State does not have it. Factor out `fn state_flags(..) -> u8` to test this.
- [ ] **Step 2: Run them.** They fail.
- [ ] **Step 3: Implement.**
  - **Space:** while standing, moving forward and in MW2 mode, set `fusion.mantle = true` and leave the WoW jump alone. The server's knock-back replaces the jump's arc when there's a ledge.
  - **V:** a press with a hostile under the crosshair within 4 yd (8 with Commando, always false in this phase) sets `fusion.lunge = true` instead of the knife spell. The aim target's distance is in `Aim`. Otherwise the knife works as now.
  - **Gating:** neither is sent while mounted, swimming or dead.
  - **Mantle hint:** a HUD text "Press SPACE to climb", using the FRIZQT font as other hints do, shown when `mantle_hint` is true.
- [ ] **Step 4: Run the tests and clippy.** Both pass.
- [ ] **Step 5: Live check with Justin.**
  - Find a terrain ledge and a wall or crate in Northshire, and Space at each. The `DIAG` log shows the client flag. `docker compose logs mangosd` gets a `DIAG`-tagged `TryMantle rise=…` line, temporary.
  - Lunge at a level-appropriate mob with V: it takes about 3× body-shot damage.
  - Lunge at a friendly NPC: nothing happens.
  - Remove the `DIAG` lines on both sides.
- [ ] **Step 6: Commit and push.**
  - Client: "Mantle and knife lunge requests, mantle hint".
  - Server: any `DIAG` removal.

### Task 11: Docs and the phase check

**Files:**
- Modify: client `README.md` (Azeroth Warfare controls: hold C prone, Space rise, Space mantle, V lunge, sprint stamina), `azeroth-warfare/README.md` ("The rules" table: `Fusion.Admin.MinSecurity`).

- [ ] **Step 1: Update both READMEs.**
- [ ] **Step 2: Run the full client tests.** `cargo test --release -p benilla-mw2` passes.
- [ ] **Step 3: Run the server tests.** `./scripts/fusion-tests.sh` passes.
- [ ] **Step 4: Live run.** 75 s of stance cycling, sprint, mantle and lunge, with no `WARN benilla_mw2` lines and a steady FPS within 10% of the 100 FPS baseline.
- [ ] **Step 5: Commit.** Commit and push the client; commit the server.

## Spec amendments made with this plan

- Mantle and lunge use `CMSG_FUSION_STATE` flags 0x20 and 0x40 in phase 1. The opcode table in
  spec 1.3 shifts to start at 831 for the loadout, with `CMSG_FUSION_ACTION` kept for the weapon
  swap and crate capture in later phases.
- The airborne spread penalty is client-side bloom ×2. The server does not simulate spread.
- The mantle hint covers terrain ledges only; the client has no WMO collision query. Mantling
  itself works on all geometry, since the server checks vmaps.
