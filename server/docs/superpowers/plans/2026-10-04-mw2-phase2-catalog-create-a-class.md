# MW2 Phase 2: Catalog, Saving and Create-a-Class Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every MW2 gun, attachment and camo in a shared catalog with MW2's own numbers; five saved custom classes per character, validated and equipped by the server; and a WoW-style Create-a-Class window in the client that builds them and puts the class's guns (with attachments and camo) in the player's hands.

**Architecture:** The server's `src/fusion` library gains the catalog (v2 JSON), loadout validation and the two loadout messages, each unit-tested in `fusion_tests`; `FusionMgr` saves classes in a new characters table and applies the equipped class. The catalog seed is generated from the player's MW2 install by the client's `mw2_export` example (MW2's real WeaponDef numbers, one definition per attachment combination). The client parses the same catalog, draws the window with bevy_ui and swaps the viewmodel to the variant's WeaponDef and the camo's model.

**Tech Stack:** C++17 (vmangos fork, `src/fusion` standalone library, MariaDB migrations), Rust 2024 / Bevy 0.18 (benilla-mw2), Python 3 (seed generator).

**Spec:** `docs/superpowers/specs/2026-10-03-mw2-create-a-class-movement-killstreaks-design.md` (sections 1.1-1.4, 2, 3.1-3.2, build steps 3 and 4).

## Global Constraints

- Server decides, client draws; every rule lives in `src/fusion` with a `fusion_tests` case.
- Nothing from the MW2 or WoW installs is committed: the catalog holds numbers and asset names only.
- `catalog.json` version 2; version 1 files (`weapons.json`) still load as guns only.
- Opcodes `CMSG_FUSION_LOADOUT` 831 and `SMSG_FUSION_LOADOUT` 832; `kNumMsgTypes` 833.
- Existing messages keep `kVersion` 1 and the existing event kinds: the client on `main` pins a benilla fork revision whose decoder rejects a whole packet on an unknown version or event kind (Ruling P2-1). The new messages carry their own version byte, 1.
- Five custom classes per character (slots 0-4); `equipped_slot` 255 = none (the per-WoW-class gun of today).
- The equip applies out of combat (immediately if already), and on respawn.
- GM (`Fusion.Admin.MinSecurity`) passes every unlock; regular players unlock attachments by level, MW2's rank through `UnlockLevel`.
- Keys while MW2 mode owns input: **K** or `/cac` opens the window; **1 / 2** or the mouse wheel swap primary and secondary.
- The jchoxha/benilla fork is not pushed without Justin's OK: fork work goes on a local branch `cac` of a clone at `C:\Users\<you>\benilla`, and client work that needs it on a local branch `cac` of benilla-mw2 (built with a `[patch]` to the clone that is not committed to `main`). Server commits stay local (never pushed).

## Review Focus

1. **A loadout naming a gun, attachment or camo the catalog lacks, or a slot out of range:** rejected with a reason, nothing saved, the old class kept. Tested in Task 3.
2. **Two attachments without Bling, or two incompatible ones (two sights, GL with shotgun):** rejected. Tested in Task 3.
3. **A level-10 non-GM equipping a gun unlocked at 40:** rejected (`CanUse`). Tested in Task 3.
4. **Equip requested in combat:** kept pending and applied on leaving combat, not dropped. Tested in Task 6.
5. **A variant with no gun model of its own (`m4_eotech_mp`):** the client falls back to the base gun's model with the variant's hide tags. Tested in Task 9.

---

### Task 1: Catalog v2 in the library

**Files:**
- Create: `src/fusion/include/fusion/Catalog.h`, `src/fusion/src/Catalog.cpp`, `src/fusion/tests/CatalogTests.cpp`
- Modify: `src/fusion/include/fusion/Weapon.h` (add `slot`, `attachments`, `variants`, `camos` to `WeaponProfile`)

**Interfaces:**
- Produces: `enum class WeaponSlot : uint8_t { Primary, Secondary }`; `WeaponProfile::slot`, `std::vector<std::string> attachments` (allowed ids, MW2 order), `std::map<std::string, std::string> variants` (attachment ids joined with `+` in catalog order → MW2 weapon name), `bool camos`. `struct Attachment { std::string id, name, icon; std::vector<std::string> incompatible; uint8_t unlockRank; }`. `struct Camo { uint8_t id; std::string name, modelSuffix; }`. `class Catalog { bool LoadJson(std::string const&, std::string& error); WeaponTable const& Weapons() const; Attachment const* FindAttachment(std::string const&) const; Camo const* FindCamo(uint8_t) const; std::string VariantName(WeaponProfile const&, std::vector<std::string> attachments) const; }` — `VariantName` returns the base name for no attachments and "" for a combination the gun lacks.

- [ ] **Step 1: Write the failing tests** `catalog_loads_version_1_as_guns_only` (the current seed text: 43 guns, no attachments), `catalog_v2_reads_attachments_camos_and_variants` (a two-gun fixture: `m4_mp` with `acog`, `silencer`, variant `"acog+silencer": "m4_acog_silencer_mp"`; camo 1 Woodland suffix `_woodland`), `variant_name_orders_attachments_by_catalog_order` (`{"silencer","acog"}` → `m4_acog_silencer_mp`; `{}` → `m4_mp`; `{"gl","shotgun"}` → `""`), `catalog_rejects_a_variant_naming_an_unknown_attachment`.
- [ ] **Step 2: Run** `bash azeroth-warfare/scripts/fusion-tests.sh` — Expected: compile failure (no `Catalog.h`).
- [ ] **Step 3: Implement** `Catalog` over the existing `Json::Parse`; v1 when `"version"` is 1 or missing.
- [ ] **Step 4: Run** the tests — Expected: all pass.
- [ ] **Step 5: Commit** `Fusion: catalog v2 (attachments, camos, variants)`.

### Task 2: The seed catalog from the MW2 install

**Files:**
- Create: `contrib/fusion/make_seed_catalog.py`, `azeroth-warfare/data/fusion/catalog.json`, client `assets/fusion_catalog.json` (same bytes)
- Modify: `src/game/Fusion/FusionMgr.cpp` (`Fusion.CatalogFile`, falling back to `fusion/catalog.json`, then `fusion/weapons.json`), `src/mangosd/mangosd.conf.dist.in`

**Interfaces:**
- Consumes: the client's `cargo run --release --example mw2_export > weapons.tsv` and `-- --models > models.txt` (columns in its header line).
- Produces: `catalog.json` v2 with the 43 guns (ids unchanged from `weapons.json`; numbers from the TSV's base rows: damage, min_damage, ranges /36, fire_time = fire_ms/1000, pellets, clip, max_ammo, reload times, move scales), each gun's `attachments` and `variants` from its `<base>_<att>[_<att>]_mp` rows, `camos` true when `viewmodel_<gun>_woodland` is in models.txt; `attachments[]` MW2's 15 (`acog, akimbo, eotech, fmj, gl, heartbeat, reflex, rof, shotgun, silencer, tactical, thermal, xmags, grip`) with names, icons and `incompatible` (sights `acog, eotech, reflex, thermal` exclude each other; `gl` and `shotgun` exclude each other and `grip`); `camos[]` the eight (1 Woodland `_woodland`, 2 Desert `_desert`, 3 Arctic `_arctic`, 4 Digital `_digital`, 5 Urban `_red_urban`, 6 Red Tiger `_red_tiger`, 7 Blue Tiger `_blue_tiger`, 8 Fall `_orange_fall`).

- [ ] **Step 1:** Write the generator; with no TSV argument it rewrites v2 from the v1 table (no attachments) so the build never needs an install.
- [ ] **Step 2: Run** it on the export; check `python3 -c` that `m4_mp` lists `acog` and its variant `acog+silencer` is `m4_acog_silencer_mp`, and that every gun id matches `weapons.json`.
- [ ] **Step 3:** Load it in `FusionMgr::Load` through `Catalog`; the startup line reads `Fusion: enabled, 43 weapons, 15 attachments, 8 camos from …/catalog.json.`
- [ ] **Step 4: Run** the image build and restart; Expected: that line in `docker compose logs mangosd`.
- [ ] **Step 5: Commit** `Fusion: catalog.json from MW2's own numbers`.

### Task 3: Loadout validation

**Files:**
- Create: `src/fusion/include/fusion/Loadout.h`, `src/fusion/src/Loadout.cpp`, `src/fusion/tests/LoadoutTests.cpp`

**Interfaces:**
- Consumes: `Catalog` (Task 1), `CanUseWeapon`, `EffectiveLevel` (`Access.h`).
- Produces: `struct ClassLoadout { std::string name; uint16_t primary = 0; std::string primaryAtt1, primaryAtt2; uint8_t primaryCamo = 0; uint16_t secondary = 0; std::string secondaryAtt; uint16_t lethal = 0, tactical = 0; uint16_t perk1 = 0, perk2 = 0, perk3 = 0, deathstreak = 0; uint16_t streak1 = 0, streak2 = 0, streak3 = 0; }` (perk/equipment/streak ids pass through unchecked until phases 5-7); `enum class LoadoutError : uint8_t { None, UnknownWeapon, WrongSlot, Locked, UnknownAttachment, AttachmentNotOnGun, TwoAttachmentsNeedBling, IncompatibleAttachments, UnknownCamo, NameTooLong }`; `LoadoutError ValidateLoadout(Catalog const&, ClassLoadout const&, uint32_t level, bool isGm, bool bling)`; `constexpr size_t kClassNameMax = 24`.

- [ ] **Step 1: Write the failing tests** `a_full_admin_class_is_valid` (m4 + acog + camo 1, usp, GM, level 60, bling false → None), `unknown_pieces_are_rejected` (primary 999 → UnknownWeapon; camo 9 → UnknownCamo; attachment `laser` → UnknownAttachment), `a_pistol_cannot_be_primary` (WrongSlot), `two_attachments_need_bling_and_compatibility` (acog+silencer without bling → TwoAttachmentsNeedBling; with bling → None; acog+eotech with bling → IncompatibleAttachments; gl on a gun without it → AttachmentNotOnGun), `a_low_level_player_cannot_take_a_locked_gun` (level 10, not GM, a rank-60 gun → Locked), `a_long_name_is_rejected` (25 chars → NameTooLong).
- [ ] **Step 2: Run** — Expected: compile failure.
- [ ] **Step 3: Implement** `ValidateLoadout`; the variant must exist (`Catalog::VariantName` non-empty).
- [ ] **Step 4: Run** — Expected: pass.
- [ ] **Step 5: Commit** `Fusion: Create-a-Class loadout validation`.

### Task 4: The loadout messages

**Files:**
- Modify: `src/fusion/include/fusion/Packets.h`, `src/fusion/src/Packets.cpp`, `src/fusion/vectors/packets.txt`, `src/fusion/tests/PacketTests.cpp`

**Interfaces:**
- Produces: `CMSG_FUSION_LOADOUT = 831`, `SMSG_FUSION_LOADOUT = 832`, `kNumMsgTypes = 833`, `kLoadoutVersion = 1`. `struct LoadoutRequest { uint8_t equipSlot; /*255 none*/ std::vector<std::pair<uint8_t, ClassLoadout>> classes; /*slots to save, 0-5 entries*/ }`; `enum class LoadoutStatus : uint8_t { Saved, Pending, Applied, Rejected }`; `struct LoadoutReply { LoadoutStatus status; LoadoutError error; uint8_t errorSlot; uint8_t equippedSlot; std::vector<std::pair<uint8_t, ClassLoadout>> classes; }`. Wire: `U8 version, U8 equip, U8 count`, per class `U8 slot, Str name (U8 length + bytes, ≤24), U16 primary, Str att1, Str att2, U8 camo, U16 secondary, Str att, U16 lethal, U16 tactical, U16 perk1..3, U16 deathstreak, U16 streak1..3`; the reply `U8 version, U8 status, U8 error, U8 errorSlot, U8 equipped, U8 count, classes as above`. `Encode`/`Decode` overloads as for the others.
- [ ] **Step 1: Write the failing tests** `loadout_request_round_trips`, `loadout_reply_round_trips`, `a_truncated_loadout_is_refused`, and the frozen vectors `loadout_request_one` and `loadout_reply_applied` (written into `packets.txt` from the first green encode; the client checks the same lines).
- [ ] **Step 2-4:** Run (fail), implement, run (pass).
- [ ] **Step 5: Commit** `Fusion: CMSG/SMSG_FUSION_LOADOUT (831-832)`.

### Task 5: Saving the classes

**Files:**
- Create: `sql/migrations/<YYYYmmddHHMMSS>_characters.sql` (via `sql/make_migration.py`): `character_fusion_class` (columns as spec 1.4, PK (`guid`,`slot`), attachment columns as `varchar(16)`), `character_fusion` (`guid` PK, `equipped_slot` tinyint unsigned default 255).
- Modify: `src/game/Objects/Player.h` (`PLAYER_LOGIN_QUERY_FUSION_CLASSES`, `PLAYER_LOGIN_QUERY_FUSION`), `src/game/Handlers/CharacterHandler.cpp` (`LoginQueryHolder::Initialize`), `src/game/Objects/Player.cpp` (`LoadFromDB` hands the results to `FusionPlayerData`; `DeleteFromDB` deletes both tables' rows), `src/game/Fusion/FusionMgr.h` (`FusionPlayerData::classes[5]`, `equippedSlot`, `pendingSlot`).
- [ ] **Step 1:** Write the migration; run the setup's migration step; check `SHOW CREATE TABLE character_fusion_class`.
- [ ] **Step 2:** Load at login; save on an accepted request with `CharacterDatabase.PExecute` (REPLACE INTO), escaping names.
- [ ] **Step 3: Verify** live in Task 6.
- [ ] **Step 4: Commit** `Fusion: character_fusion_class, saved and loaded`.

### Task 6: Equipping a class

**Files:**
- Modify: `src/game/Server/Protocol/Opcodes_1_12_1.h`, `Opcodes.cpp`, `src/game/Server/Packets/Fusion.h/.cpp`, `src/game/Server/WorldSession.h`, `src/game/Handlers/FusionHandler.cpp`, `src/game/Fusion/FusionMgr.h/.cpp`
- Test: `src/fusion/tests/LoadoutTests.cpp` (`EquipRule`)

**Interfaces:**
- Produces: `Fusion::EquipDecision EquipRule(bool inCombat, bool alive)` → `ApplyNow | WaitOutOfCombat | WaitRespawn` (library, tested: `equipping_waits_for_out_of_combat_and_respawn`); `FusionMgr::HandleLoadout(Player*, LoadoutRequest const&)`; `FusionMgr::SendLoadout(Player*, LoadoutStatus, LoadoutError, uint8 errorSlot)`; `FusionPlayerData::EquippedPrimary()` / `EquippedSecondary()` return the variant `WeaponProfile` ids the shot gate accepts (`HandleState` accepts `heldWeaponId` only from the equipped class when one is equipped).
- [ ] **Step 1: Write the failing test** for `EquipRule`; run; implement; pass.
- [ ] **Step 2:** Wire the opcode, validate every class (reply Rejected with the first error and its slot), save, then apply or mark pending; `UpdatePlayer` applies a pending equip when out of combat and alive; login sends the saved classes with status Applied.
- [ ] **Step 3: Live check:** a request from the client (Task 8) saves, a relog sends the classes back, an equip in combat replies Pending then Applied after combat.
- [ ] **Step 4: Commit** `Fusion: equip a Create-a-Class loadout`.

### Task 7: The loadout messages in the benilla fork (local branch `cac`, not pushed)

**Files:** in `C:\Users\<you>\benilla` (clone of jchoxha/benilla, branch `fusion` → new branch `cac`):
- Modify: `crates/benilla-protocol/src/fusion.rs` (opcodes, `ClassLoadout`, `LoadoutRequest`, `LoadoutReply`, encode/decode, `WorldWriter::fusion_loadout`, the `STATE_MANTLE`, `STATE_LUNGE`, `EVENT_PROGRESS` constants), `messages/packet.rs` + `messages/parse.rs` (`ServerPacket::FusionLoadout`), `events.rs` + `events/decode.rs`, `crates/benilla-app/src/net.rs` + `net/io.rs` (`ClientCommand::FusionLoadout`), `crates/benilla-app/src/fusion.rs` (`SendFusionLoadout`, `FusionLoadoutReceived`).
- Test: `crates/benilla-protocol` vector test reads the server's `packets.txt` lines `loadout_request_one`, `loadout_reply_applied`.
- [ ] **Step 1:** Vector tests fail; implement; pass (`cargo test -p benilla-protocol fusion`).
- [ ] **Step 2: Commit** on `cac`; do not push.

### Task 8: The catalog and loadouts in the client (branch `cac`)

**Files:**
- Create: `src/catalog.rs` (parse `assets/fusion_catalog.json` v2 / `MW2_FUSION_WEAPONS`), `src/cac.rs` (the `Classes` resource: the server's five classes, equipped and pending slots; `SendFusionLoadout` on Equip; `FusionLoadoutReceived` handling)
- Modify: `Cargo.toml` (local-only `[patch]` to the clone, kept out of `main`), `src/fusion.rs` (guns from the catalog; `held_weapon_id` from the equipped class's variant)
- [ ] **Step 1: Write the failing tests** `catalog_reads_the_seed` (43 guns, `m4_mp` allows `acog`), `variant_names_match_the_server` (same three cases as Task 1), `an_applied_reply_equips_the_class`.
- [ ] **Step 2-4:** Run (fail), implement, run (pass).
- [ ] **Step 5: Commit** on the client's `cac` branch.

### Task 9: Variants and camos on the viewmodel (branch `cac`)

**Files:** Modify `src/mw2/load.rs` (`Mw2Library::weapon_with(variant, camo_suffix)`: the variant's WeaponDef for clips, hide tags, sounds, reload and stats; its own gun model, else the base gun's (Review Focus 5); the camo's `viewmodel_<gun><suffix>` when present), `src/mw2/mod.rs` (rebuild on a new variant or camo), `src/mw2/viewmodel.rs` (ADS FOV from the variant's `ads_zoom_fov`).
- [ ] **Step 1: Write the failing tests** `a_variant_without_a_model_uses_the_base_gun` and `a_camo_picks_its_model` against a small in-memory `Mw2Library` fixture.
- [ ] **Step 2-4:** Run, implement, pass. **Live:** `MW2_GUN=m4_acog_silencer_mp` shows the ACOG and silencer and no other attachment.
- [ ] **Step 5: Commit** on `cac`.

### Task 10: The Create-a-Class window (branch `cac`)

**Files:** Create `src/cac_ui.rs`; Modify `src/mode.rs` (`FpsState::panel` ORed into input ownership), `src/input.rs` (K), `src/mw2/load.rs` (`Mw2Library::image(name) -> Option<Arc<RgbaImage>>`, cached).
- Layout (spec 2.1): WoW dialog border/backdrop/button art via `mpq://` (`Interface\DialogFrame\UI-DialogBox-Border`, `-Background`, `Interface\Buttons\UI-Panel-Button-Up`); left column the five custom classes (rename) and MW2's five defaults (read-only, "Copy to slot"); right the slots; a slot opens an icon grid filtered by `incompatible` and Bling; stat bars (damage, accuracy, range, fire rate, mobility) from the variant's stats; **Equip** sends the request.
- [ ] **Step 1: Write the failing tests** `stat_bars_are_normalised_over_the_catalog` and `the_attachment_grid_hides_incompatible_choices`.
- [ ] **Step 2-4:** Run, implement, pass. **Live:** screenshots of the window open, a slot grid, and an equipped class in hand.
- [ ] **Step 5: Commit** on `cac`.

### Task 11: Primary and secondary (branch `cac`)

**Files:** Modify `src/input.rs` (1, 2, wheel while MW2 mode owns input), `src/mw2/mod.rs` (two assembled weapons, DROP then RAISE clips, `QUICK_*` with Sleight of Hand Pro later), `src/weapon.rs` (per-gun magazines), `src/fusion.rs` (`held_weapon_id` follows the gun in hand).
- [ ] **Step 1: Write the failing test** `swapping_keeps_each_guns_magazine`.
- [ ] **Step 2-4:** Run, implement, pass. **Live:** swap plays drop and raise; the server accepts shots from both.
- [ ] **Step 5: Commit** on `cac`; READMEs (controls K, 1/2, wheel; `Fusion.CatalogFile`).

## Rulings made with this plan

- **P2-1:** existing fusion messages keep `kVersion` 1 and no new event kinds (`LoadoutApplied` is a status in `SMSG_FUSION_LOADOUT`, not an event), because the pushed client's decoder drops a whole packet on an unknown version or kind. Cost if wrong: none for play; the spec's version bump is deferred until the fork is pushed.
- **P2-2:** attachment ids are MW2's own suffixes (`acog`, `xmags`, `rof`, …), matching the install's variant names, instead of numeric ids. Cost if wrong: a renaming pass in the catalog.
