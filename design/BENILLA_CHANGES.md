# What we changed in benilla

`benilla/` is our fork of [benilla](https://github.com/samwhosung/benilla), the open-source
Rust/Bevy client for WoW 1.12.1. This file lists every change we made relative to upstream, the
seams our game layer (`client/`, the `benilla-mw2` crate) depends on, licensing, and test status.
Verified against the tree at `main` 4cbe413c8 (2026-10-28).

## 1. How benilla is vendored

- **A git subtree**, not a submodule or a plain copy. Commit `d990f35b3` (2026-10-05, "Import the
  benilla fork (branch overnight) with its history") carries `git-subtree-dir: benilla` and
  `git-subtree-split: 3500511ae`. Upstream's full history is in our repository.
- **No upstream remote** is configured (only `origin`, our own repository). There is no
  `.gitmodules`. Nothing in `design/` or `benilla/README.md` records the base.
- **Upstream base: `cf891dc37`** ("fix: close a loot window that opens while you move, as 1.12.1
  does (#786)", 2026-10-03), the last upstream commit. Above it, before the import, ten fork
  commits on the `overnight` branch (`148178454`..`3500511ae`, 2026-10-03/04) added the fusion
  packets (opcodes 828-834) and `fusion-bot`.
- No upstream update has been pulled since. Every merge under `benilla/` since the import is one
  of our own branches.
- To reproduce the diff: `git diff cf891dc37 HEAD:benilla`.

**Size of the diff:** 213 files, +14,185 / -356 lines; 19 files added, 194 modified, none deleted.
110 commits touch `benilla/` after the import. About 3,200 of the added lines are the five
fusion files (`benilla-app/src/fusion*.rs`, `benilla-protocol/src/fusion.rs`, `fusion-bot`).
The rest are mostly small hooks in existing files. Most edits are additive: a new resource or
message that is inert unless a layer crate sets it, so a stock run behaves as upstream does.

Key: **U** = general improvement, an upstream candidate. **W** = WOMW-specific. **U/W** = the
mechanism is general but it was built for us and would need cleaning up before it is offered.

## 2. Changes by area

### Networking and opcodes (`benilla-protocol`, `benilla-app/src/net*`)

| What | Why | Files | |
|---|---|---|---|
| Fusion packets: `CMSG_FUSION_SHOTS` 828, `SMSG_FUSION_EVENTS` 829, `CMSG_FUSION_STATE` 830, `CMSG/SMSG_FUSION_LOADOUT` 831/832, `CMSG/SMSG_FUSION_CHANNEL` 833/834. Encoders, decoders, wire constants (shot flags, stance/gait state bits, loadout v5 with four primary attachments and per-slot Pro bits, event kinds), pellet seed/spread (`pellet_direction`, `pellet_hash`), the general channel (`CHANNEL_WORLD`, `CHANNEL_BUILD`, ops such as `OP_LOOT_SOURCES` 36) | Gunplay, Create-a-Class and all layer-server traffic | `protocol/src/fusion.rs` (new), `messages/packet.rs`, `messages/parse.rs`, `events.rs`, `events/decode.rs`, `tests/data/fusion_packets.txt` | W |
| `ClientCommand::Fusion*` variants and their send arms; `WorldWriter::send` made `pub(crate)` | Send the fusion packets from the app | `app/src/net.rs`, `net/io.rs`, `protocol/src/world/writer/mod.rs` | W |
| `fusion-bot`, a headless load-test binary | Server load testing | `protocol/src/bin/fusion-bot.rs` (new) | W |
| `item_property_seed()` on player fields | Companion item level/stage on tooltips | `update_object/fields/player.rs` | U |
| Create Account on the login screen: when `$AW_SIGNUP_URL` is set, a dialog queries `GET …/api/signup-info`, posts `POST …/api/signup` over HTTPS (adds `ureq` with rustls), and requires the legal-access box. Without the variable the screen is stock | Our realm's sign-up | `app/src/login/signup.rs` (new), `login/mod.rs`, `login/screen.rs`, `app/Cargo.toml` | W |

### Fusion seams (`benilla-app/src/fusion.rs`, `fusion_bar.rs`, `fusion_spells.rs`, `ui_action/fusion.rs`)

The main seam. `fusion.rs` re-exports the protocol types and defines the resources and messages
our layer reads and writes (full list in section 3). Behind them, benilla systems:

- send shots, state, loadout and channel messages, and surface server events
  (`FusionEventsReceived`, `FusionLoadoutReceived`, `FusionChannelReceived`);
- a layer action bar (`FusionActionBar`, `MainBarView`, `WriteRule`, `ACTION_KIND_FUSION`) that
  shares benilla's action buttons, cursor pick-up (`hold_layer_action`) and tooltips;
- class spell refits from the server: one entry per class spell with no "Rank N", while old macros
  still cast; quiet learn/unlearn and `SMSG_SUPERCEDED_SPELL` announcements on kit swaps
  (`LayerQuietSpellbook`); druid forms as real shapeshifts with their own bars;
- layer-flown missiles and impacts, effect placements, unit-attached effects and ground reticles
  (`FusionSpellMissile`, `FusionSpellImpact`, `FusionEffect`, `FusionUnitFx`,
  `LayerGroundReticles`);
- unit holds and animation requests (`LayerUnitHold`, `PlayUnitAnim`, `UnitAttach` with
  `mount_of`), mount look/tilt/anim, riding, flight, first-person and zoom state, minimap marks,
  a creature preview borrowing the stable pane, layer skill lines and trade skills.

All W. Upstream would only want a generic "extension crate" API, not these types.

### Entities and animation (`benilla-app/src/entities*`, `creature_anim*`, `benilla-world/src/rig_*`)

| What | Why | Files | |
|---|---|---|---|
| A model whose hard-coded texture fails to load still draws: parts wait for each batch texture to settle, and a failed one takes the display's skin. Creature Monster skin slots draw the display's skin | Missing or broken textures from patched archives made models vanish | `entities/display.rs`, `entities/attach/mod.rs` | U |
| Mounts: rider seated on its mount's box, seat heights for seatless mounts, mount tilt ordered in `PostUpdate`, `WOW_MOUNT_TRACE` diagnostics, the mount strikes with its rider | Companion mounts, mounted combat | `entities.rs`, `entities/mount.rs`, `entities/conform.rs`, `creature_anim/driver.rs` | U/W |
| Held ready stance (a gun body holds a settled `ReadyRifle` frame), layer-requested anims, mounted rider not stood up | Guns on NPCs and players | `creature_anim/driver.rs`, `driver/play.rs`, `creature_anim.rs` | W |
| Layer missiles fly free-aimed bolts and re-aim in flight; destination effects; spell-visual placements | Layer spells | `entities/missile.rs`, `entities/dest_fx.rs`, `creature_anim/spell_visual.rs` (+ tests) | W |
| No blood spurt on one's own body in first person | It filled the screen | `creature_anim/blood.rs` | U/W |
| `PhysicsDrivenPose` marker, `PhysicsPose` set and `PosePost` export; `RigPalettesWritten` | Ragdolls and gun holds in our layer write bones after benilla's pose | `world/src/rig_anim*.rs`, `rig_palette.rs` | U |
| `FadeMaterials::get_with` (zoom fade switch) | First-person own body | `world/src/model_fade.rs` | U/W |

### Rendering (`benilla-assets` shaders and materials, `benilla-world`)

| What | Why | Files | |
|---|---|---|---|
| Atmosphere: cascaded sun shadows (with a shadow-caster shader and a fade over the last cascade), volumetric fog with light shafts, per-pixel lamps (`AtmosphereLamp`), clutter wind. `AtmosphereSettings` with `MODERN`/`CLASSIC` presets, `atmo*` CVars and Graphics-menu rows; off means stock look | A modern look, optional | `world/src/atmosphere/` (new, with a naga WGSL validation test), `lighting/global_light.rs`, `static_gx/render.rs`, `static_gx.wgsl`, `assets/src/shaders/{terrain,wow_model,wow_model_caster}.wgsl`, `assets/src/materials.rs`, `clutter.rs`, `sky.rs`, `sun/materials.rs`, `terrain_stream/spawn/fx.rs`, `app/src/video.rs`, `cvars/*`, `assets/ui/OptionsFrame.xml` | U (a deliberate deviation from 1.12, so upstream may not want it on by default) |
| static-gx spawns exiles after shadow cascades pick their entities | Ordering bug found with shadows | `world/src/static_gx/mod.rs` | U |
| `NearClipOverride` | First-person near plane for guns and arms | `world/src/view.rs` | U/W |
| Particle buffer fix | Windows/release validation | `world/src/particles/buffer.rs` | U |

### World and terrain (`benilla-adt`, `benilla-wdt`, `benilla-formats`, `benilla-world`)

| What | Why | Files | |
|---|---|---|---|
| Later-format terrain: MH2O liquid, MCCV vertex colours, 8-bit alpha maps, MPHD flags (`has_vertex_colors`, `has_big_alpha`) | Read WotLK-era tiles (Northrend, Dalaran) imported into our chain | `adt/src/lib.rs`, `adt/src/mh2o.rs` (new), `wdt/src/lib.rs`, `formats/src/terrain.rs`, `terrain_later_tests.rs` (new), `assets/src/adt.rs`, `terrain.wgsl`, `terrain_stream/collider.rs` | U (not 1.12 data, but harmless) |
| `SpellCatalog::get_mut` | Server class-spell refits patch the catalog | `formats/src/spells/mod.rs` | U |
| Player ships: streamed transport drive (`StreamedDecks`) | Player-owned ships | `app/src/transport.rs` | W |
| World map: one continent per map, merged zones clickable, zone picker on merged chains | Our merged map chain | `app/src/ui_world_map.rs` (+ tests) | U/W |

### Assets, MPQ chain and HD pack (`benilla-formats/src/chain.rs`, `install.rs`, `benilla-assets`, `benilla-app/src/hd_pack.rs`)

| What | Why | Files | |
|---|---|---|---|
| `Chain::open_filtered` (mount only accepted `Data` archives), `Chain::open_with_inserted` (seat extra archives below a chosen layer), `archive_paths()`; `benilla_assets::open_with_inserted` / `register_mpq_source_with` | Tools that rebuild a patch layer read the chain beneath it; the HD pack mounts under our built layers | `formats/src/chain.rs`, `assets/src/lib.rs` | U |
| `install::chain_patches` and `skip_unless_stock` (see section 5) | Our chain carries custom patches | `formats/src/install.rs` | U |
| Optional HD pack: `womw-hd-N.MPQ` in an `hd` folder beside the game, listed by `womw-hd.txt`, found via `WOMW_HD_DIR` or beside the exe; mounted above stock and below our built layers (`is_built_layer`); switched by the `womwHdPack` CVar at boot; falls back to `Data` alone if mounting fails | Owner's optional HD download | `app/src/hd_pack.rs` (new), `app/src/lib.rs` (`launch`), `cvars/*`, `OptionsFrame.xml` | W |

### UI and Lua (`benilla-ui/src/script`, `benilla-app/src/ui_*`, `assets/ui`)

| What | Why | Files | |
|---|---|---|---|
| Layer key-binding plane in the Key Bindings window (`set_layer_binding_prefix`, `is_layer_binding`, `in_layer_plane`); `LayoutNames` for keyboard-layout key names | Our layer's own bindings | `ui/src/script/keybind.rs`, `app/src/bindings*` | U/W |
| Action bar: 228 slots (`ACTION_SLOTS`), layer tooltips (`LayerTipLine`, `set_layer_tips`), ready mode keeps the clicking mouse button, queued action calls gated, toggles | Layer bar and abilities | `ui/src/script/action.rs`, `cursor/bar.rs`, `app/src/ui_action/*`, `script_calls.rs` | W |
| Item name/note hooks (`set_item_name`, `set_item_note`, `instance_name`), multi-line notes, companion items 91100-91999 | Companion and kit items | `ui/src/script/tooltip_item/*`, `app/src/ui_items/*`, auction/mail/trade/container scripts | W |
| Spell tooltips for layer abilities; a buff's hover titled by the aura's own name | Layer abilities; aura naming | `ui/src/script/tooltip_spell.rs` (+ tests), `app/src/ui_aura.rs` | buff title U, rest W |
| Spellbook: one entry per class spell | Server refits | `ui/src/script/spellbook.rs` | W |
| Area loot sectioned by corpse (`LootSections`, `LootFrameAdapters.xml`) | Server area loot | `ui/src/script/loot.rs`, `app/src/ui_loot/*`, `assets/ui/*`, tests | W |
| Layer windows take the pointer from the world (`LayerPointerOver`); stable pane lent to the layer for a creature model | Our windows | `app/src/ui_script/mod.rs`, `ui_stable/mod.rs`, `portrait/*` | U/W |
| Lua error log; `error_log.rs` | Diagnostics | `app/src/ui_script/error_log.rs` (new) | U |
| Professions as stock-like trades, tradeskill and character pane hooks; group members exported (`GroupMembers`); hostility helper (`Hostility::is_enemy`) | Construction, Minstrelsy, heartbeat sensor | `ui_tradeskill.rs`, `ui_char.rs`, `ui_party/mod.rs`, `target/relations.rs` | W / U |

### Input and camera (`benilla-app/src/player*`, `target*`)

| What | Why | Files | |
|---|---|---|---|
| Momentum flight (skyriding), Ascent/Surge, swim changes, crouch | Flying companions, gunplay | `player/controller.rs`, `player/swim.rs` | W |
| First-person mounted eye, camera cut, zoom snap, leaving the world frees the cursor | First-person game | `player/camera.rs` (+ mouselook tests), `player.rs` | W (cursor release U) |
| Click targeting: F targets and uses, target on hit, shots through allies, mounted riders elected by their mount | Gunplay | `target/click.rs`, `target/mod.rs`, `target/highlight.rs`, `target/script.rs` | W |
| Ground reticle for layer abilities | Layer spells | `target/reticle.rs` | W |
| Spell cast targeting and usability hooks (cast arm's own target column, downed view) | Forms, revival | `spell/cast_send.rs`, `cast_target.rs`, `net.rs`, `usable.rs` | W |

### Sound (`benilla-app/src/sound`)

- `sound/layer.rs` (new): `LayerSound`, one looping 2D kit whose gain the layer sets each frame
  (flight wind) and one-shot kits by name. W.

### Minimap

- Layer minimap marks (`MinimapMarks`) drawn as dots. W. Files: `minimap/blips/dots.rs`,
  `blips/mod.rs`, `minimap/mod.rs`.

### Tests and tooling

- Capture harness: `WOW_CAPTURE_UI=0`, player-race subjects, mount trace, probe keys for layer
  windows, `MW2_PROBE_ANIM`. W. Files: `capture/*`.
- `#[global_allocator]` gated `not(doctest)`, Windows/release validation fixes, the `Yield`
  macro clash. U.
- `skip_unless_stock` calls in about 30 stock-pinned tests, schedule ratchet ceilings raised
  (section 5).
- `benilla-world` dev-dependencies `naga` 27 and `naga_oil` 0.20 for the WGSL check.

## 3. Public seams `client/` depends on

An upstream update breaks us wherever it changes these. **New** = added by us (we own them; they
must be carried forward). **Upstream** = existing benilla API we call.

**New, in `benilla_app`:**
- `fusion::*`: `Stance`, `Gait`, `State`, `Shot`, `Event`, `EventKind`, `ClassLoadout`,
  `ChannelMessage`, `CHANNEL_WORLD`, `CHANNEL_BUILD`, `CLASS_NAME_MAX`, `NO_CLASS`,
  `PERK_PRO_MASK`, `pellet_direction`, `pellet_hash`, `SendFusionChannel`,
  `FusionChannelReceived`, `FusionEventsReceived`, `SendFusionLoadout`, `SendGossipHello`,
  `FusionActionBar`, `ACTION_KIND_FUSION`, `FusionLook`, `SlotMark`, `FusionEffect`,
  `FusionUnitFx`, `LAYER_EFFECT_CAP`, `FusionSpellMissile`, `FusionSpellImpact`,
  `LayerGroundReticles`, `LayerReticle`, `LayerInput`, `LayerUse`, `LayerBindings`,
  `LayerBoundKeys`, `binding_key_name`, `LayoutNames`, `LayerPointerOver`, `LayerUnitHold`,
  `UnitAttach` (incl. `mount_of`), `PlayUnitAnim`, `LayerMountAnim`, `MountAnimRequest`,
  `LayerMountLook`, `MountLook`, `LayerMountTilt`, `LayerFlight`, `LayerSound`,
  `LayerFirstPerson`, `FirstPersonBody`, `LayerZoomSnap`, `LayerQuietSpellbook`,
  `StreamedDecks`, `MinimapMarks`, `MinimapMark`, `LayerCreaturePreview`, `LayerSkillLines`,
  `LayerTradeSkill`;
- `fusion_bar`, `fusion_spells`, `hd_pack` modules;
- `Hostility`, `GroupMembers`, `LayerDefaults`.

**New, elsewhere:**
- `benilla_ui::script`: `LayerTipLine`, `ACTION_SLOTS`, `set_item_name`, `set_item_note`,
  `hold_layer_action`, `set_layer_binding_prefix`;
- `benilla_world`: `view::NearClipOverride`, `rig_anim::{PhysicsDrivenPose, PhysicsPose,
  PosePost}`, `rig_palette::RigPalettesWritten`, `atmosphere::AtmosphereSettings`;
- `benilla_formats::Chain::{open_filtered, open_with_inserted, archive_paths}`.

**Upstream, used heavily** (signature changes break us): `benilla_app::{run_with, BuildId,
Objects, Interact, net}`; `benilla_ui::script::UiScript`, `benilla_ui::{framexml::parse,
loader::load_in, source::decode}`; `benilla_world::{view::{WorldCamera, CAM_FOVY,
NEARCLIP_RANGE}, collision::WorldCollision::body_filter, rig_anim::RigPose, mesh_tag,
interact::{WorldPick, PickMesh}, lighting::WowLighting, interior, model_fade::FadeMaterials,
log_ring}`; `benilla_assets::{coords, WorldAssets, ModelAnimations, PoseTrack,
materials::WowModelMaterial, sprite_image}`; `benilla_formats::{open_chain, Chain, wow_data,
wow_data_or_skip, parse_m2, load_m2, RenderSubmesh, ModelBlend, CharSkinSlot, SpellCatalog,
load_spell_catalog, load_spell_visual_catalog, load_creature_catalog, load_anim_data_catalog,
wmo_group_submeshes}`; `benilla_mpq::Archive::open`; `benilla_adt::{parse_adt, ParsedAdt}`;
`benilla_m2`. `client/Cargo.toml` depends on benilla by path with `default-features = false`.

Beyond types, our layer depends on **schedule placement**: benilla systems it orders against
(`WorldStage`, `UiInput`, the feed/drain phases of `ui_action`) and the ratchet ceilings below.
An upstream reshuffle of those sets breaks ordering silently, not at compile time.

## 4. Licensing

- benilla is **MIT OR Apache-2.0** (`benilla/LICENSE-MIT`, `benilla/LICENSE-APACHE`,
  `license` in `benilla/Cargo.toml`). Vendored `third_party/kira` and `third_party/lua-src`
  keep their own licenses. Our root `LICENSE` is GPL-3.0; `NOTICE.md` lists `benilla/` as a fork
  under the same terms and credits the project. Both licenses are GPL-3.0 compatible.
- **What a fork must do.** MIT: keep the copyright and permission notice in all copies or
  substantial portions. Apache-2.0 (if we distribute under it): include the license, keep existing
  notices, and give **modified files a prominent notice that we changed them** (section 4(b)).
  Neither requires our changes to stay under the same license, but `NOTICE.md` says they do.
- **Do we comply today?**
  - Source tree: yes for MIT. Both license files are intact and `NOTICE.md` names the fork.
  - Apache 4(b): **not fully.** None of the 194 modified files has a "changed by" header. Many
    added items say "World of Modern Warfare" or "Azeroth Warfare" in doc comments, which is not a
    file-level notice. Fix: one line at the top of each modified file, or rely on MIT, which
    has no such clause, and say so in `NOTICE.md`.
  - Binary distribution: **gap.** `dist/AzerothWarfare/client/` ships `benilla-mw2.exe` with no
    license text for benilla, Kira or Lua. MIT requires the notice in copies, binaries included.
    Fix: have `package/build.ps1` copy the license files (or a combined third-party notices file)
    into `dist/`.

## 5. Test status

- **How to run** (`HANDOFF.md`): from `benilla/`, with an absolute `WOW_DATA` pointing at
  `dist\AzerothWarfare\game\WoW\Data` and `BENILLA_REQUIRE_DATA=1`,
  `cargo test --release --workspace --no-fail-fast`. Addon tests need `BENILLA_ADDON_CORPUS`.
- **Last recorded result** (`HANDOFF.md`, not re-run for this write-up): 7,843 passed, 42 failed,
  18 ignored, plus a doctest compile error. One failure was a stale binary, leaving 41
  (29 missing addon fixtures, 12 other validation failures). The 2026-10-21/28 fix commits
  ("repair Windows and release validation checks", `not(doctest)` allocator gate, stock-pinned
  skips) target those. The workspace has **not been recorded green** since.
- **`skip_unless_stock`** (`benilla-formats/src/install.rs`): returns true, printing a skip,
  when the install's `Data` holds `patch-*.MPQ` files beyond stock `patch-2.MPQ`
  (`chain_patches`). Tests that pin stock 1.12.1 numbers (DBC row counts, a stock tile's bytes)
  call it first. It is not a failure under `BENILLA_REQUIRE_DATA`. It is used in about 30 files:
  most DBC loaders in `benilla-formats` (area POI/sound/trigger, creatures, factions, item
  sounds/visuals, liquid, sound entries, spell duration, taxi, WMO area, world map, zone map,
  terrain), `formats/tests/{chain,helm_vis_wants_a_helm,wmo_chunk_overrun,wmo_liquid}.rs`,
  and `app` tests in `ui_reputation`, `ui_taxi/routing`, `ui_world_map`, `ui_macro/tests`,
  `ui_script/bag_tests`. To check those numbers, run against a stock `Data` folder.
- **Schedule ratchet** (`benilla-app/src/game_plugins.rs`,
  `the_schedules_have_no_more_undeclared_orders_than_the_ceilings_say`): the count of
  undeclared-order system pairs may not rise above the ceiling, and must fall with it.
  - `POST_UPDATE_CEILING`: upstream 371, ours **374** (the layer mount tilt moved to
    `PostUpdate`).
  - `UPDATE_ACTIONABLE_CEILING`: upstream 4,980, ours **5,140** (layer systems added to `Update`).
  `HANDOFF.md` notes the ratchet was still failing on the validation branch. Each new layer
  system in `Update` needs a declared order or a raised ceiling with a reason.

## 6. Issues found while writing this

- **`hd_pack` is gated on `dev` by accident.** In `benilla-app/src/lib.rs`, `pub mod hd_pack;`
  was inserted between `#[cfg(feature = "dev")]` and `mod hover_log;`. So `hd_pack` compiles only
  with `dev`, and `hover_log` (dev-only upstream) now compiles always. `launch()` and
  `cvars/mod.rs` use `hd_pack` unconditionally, so a `--no-default-features` build (benilla's
  player gate in `scripts/gates.sh`) should fail to compile. Our package builds with default
  features (`dev` on), which hides it. Fix: move `pub mod hd_pack;` above the `cfg` line.
- No record of the upstream base exists in the tree. Add a line naming `cf891dc37` to
  `benilla/README.md` or here, and pull upstream with
  `git subtree pull --prefix benilla <upstream> main`.
