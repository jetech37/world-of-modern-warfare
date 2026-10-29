# MW2 Phase 3: Perks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** MW2's perks and deathstreaks in the catalog and the Create-a-Class window, validated by tier and unlock, and the perks whose systems exist acting in play with MW2's numbers.

**Architecture:** The catalog gains `perks[]`; `ValidateLoadout` checks each perk's tier and unlock and takes Bling from the class itself; `Fusion::PerksOf(class, isGm)` turns a class into `PerkFlags` that the existing rules read (sprint, speed, lunge, damage, streaks, ammo, detection). The client applies the two perks it owns (reload, hip spread) and shows perk rows in the window.

**Spec:** `docs/superpowers/specs/2026-10-03-mw2-create-a-class-movement-killstreaks-design.md` section 3.3, build step 5.

## Global Constraints

- Server rules in `src/fusion` with `fusion_tests`; Pro automatic for a GM.
- Perk ids (catalog): tier 1 — 1 Marathon, 2 Sleight of Hand, 3 Scavenger, 4 Bling, 5 One Man Army; tier 2 — 6 Stopping Power, 7 Lightweight, 8 Hardline, 9 Cold-Blooded, 10 Danger Close; tier 3 — 11 Commando, 12 Steady Aim, 13 Scrambler, 14 Ninja, 15 SitRep, 16 Last Stand. Deathstreaks — 21 Copycat (4 deaths), 22 Painkiller (3), 23 Martyrdom (4), 24 Final Stand (4).
- MW2 numbers: Sleight of Hand reload ×0.5; Stopping Power bullet damage ×1.4; Lightweight ×1.07 (existing); Hardline streaks −1 (existing); Commando lunge ×2 and no fall damage; Steady Aim hip spread ×0.65; Ninja detection ×0.7; Scavenger refills one magazine of reserve per gun on a kill worth a streak.

## Review Focus

1. A perk in the wrong tier slot (Stopping Power as perk 1) is rejected. Task 1.
2. Two primary attachments without Bling in perk 1 are rejected, for a GM too. Task 1.
3. An equipped class's perks change after a re-equip and on login, not before (the applied class). Task 2.
4. Scavenger on a gray kill gives nothing. Task 2.
5. A class with no perks plays exactly as before. Tasks 2-3.

### Task 1: Perks in the catalog and the validation
Files: `contrib/fusion/make_seed_catalog.py` (perks[], deathstreaks[]), `catalog.json` (server and client), `src/fusion/{Catalog,Loadout}.*`, tests in `LoadoutTests.cpp`.
- [ ] Tests `perks_are_checked_by_tier_and_unlock`, `bling_comes_from_the_class`; RED; implement `Perk{id, tier, name, unlockRank}` in `Catalog`, `ValidateLoadout(catalog, l, level, isGm)` (Bling = `perk1 == 4`); GREEN; commit.

### Task 2: Perks in play on the server
Files: `src/fusion/{Movement,Damage?,Ammo}.*` as needed, `FusionMgr.cpp`.
- [ ] Tests `perks_of_a_class` (flags from ids, Pro for a GM), `stopping_power_is_bullets_only`, `scavenger_refills_a_magazine`, `ninja_shortens_detection`; RED; implement `PerksOf`; apply at ApplyEquip/login; GREEN; image build; live check; commit.

### Task 3: Perks in the client
Files: `src/catalog.rs`, `src/cac_ui.rs` (Perk 1-3, Deathstreak rows), `src/cac.rs` (class perks), `src/weapon.rs` (Sleight of Hand reload, Steady Aim bloom), `src/sprint.rs` (Marathon), `src/movement.rs` (Commando reach).
- [ ] Tests `perk_rows_cycle_within_their_tier`, `sleight_of_hand_halves_the_reload`, `steady_aim_tightens_the_hip`; RED; implement; GREEN; live screenshot; commit on `cac`.

## Rulings made with this plan

- **P3-1:** Cold-Blooded, Danger Close, Scrambler, SitRep, One Man Army and Last Stand, and the deathstreaks' effects, are selectable and saved but act only once their systems exist (UAV and radar on the server, explosives, class swapping in the field, downed state, respawn effects). Cost if wrong: those perks do nothing for now.
- **P3-2:** Marathon Pro's faster mantle is left out (the mantle is a knock-back arc, not a timed move). Cost if wrong: none in feel.
