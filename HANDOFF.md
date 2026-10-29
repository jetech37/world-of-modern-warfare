# Handoff: World of Modern Warfare (WOMW)

State as of 2026-10-21, updated after the handoff verification and validation follow-up.

## Furniture branch (`furniture`)

Construction's decor layer: 55 furniture pieces (ids 200-254) of WoW 1.12 doodads, made as kits
(some sold by the Plot Steward, some found cutting lumber), placed on a quarter-yard grid with
15-degree turns (free with Sprint), on floors, table tops or walls, no piece cap. Uses with F:
seats, beds (homestead rest), the plot's shared stash, books, crafting stations as spell
focuses, a real mailbox, a training dummy with a DPS meter, trophy plinths, a bard's stage.
Docs: `server/src/fusion/docs/CONSTRUCTION.md` "Furniture". World SQL `33_furniture.sql`
(creatures 94250-94251, gameobject 194200); characters tables made at start. Server Release
build and 843 fusion tests pass; client lib tests (725) and release build pass. Not playtested:
seat/bed heights, wall offsets and the stand-ins' look want a look in game.
## Inscription, Jewelcrafting and Wrath-style achievements (branch `insc-jc-achievements`)

Two new primary professions (Inscription 9003, Jewelcrafting 9004: trainers 94300/94450, items
94300-94599, `sql/custom/azeroth_warfare/35_inscription_jewelcrafting.sql`), kit glyphs and gun
gems (the Glyphs & Gems window), and the achievements expanded with Wrath's categories, points,
meta-achievements and titles (achievements wire version 2: server and client must ship together).
Design: GAME_DESIGN.md section 12 "Inscription and Jewelcrafting" and section 14 "Achievements".
Not played in game yet: every item look, trainer look and spawn in the SQL is marked VERIFY.
## Flying companions branch (`flying-companions`, 2026-10-26)

Built on `wotlk-assets` (merged in). Nine flying-mount companion lines, species 215-223
(`35_flying_companions.sql`, catalog sections 1g/2e, COMPANIONS.md "Flying mounts"): new
families Gryphon, Wind Rider, Hippogryph, Dragonhawk; 19 new patch-layer displays 40019-40037;
ridden on the ground with a Wing Leap into the riding glide. Design only:
`design/NORTHREND_SEAMLESS.md` (seamless continents; recommends separate maps with seamless
handoffs). Not played in game: every new model, seat, size and the leap need a look.

## Validation follow-up branch

Current checkout: `fix/validation-baseline`, based on local `main` at `1bb5837b5`.
The fixes below are committed on this branch, not merged or packaged:

- `6f54cc29`: Windows path/CRLF checks, release doctest allocator handling, deterministic
  release particle-guard test, stable anonymous Lua query names, and atmosphere UI/API inventories.
- `392306a7`: wait for the in-game UI before draining queued action calls; apply server spell
  refits after network processing and before UI feeds; move action-bar VM synchronization into
  the UI feed module.
- `6d7a4fea`: scope the Clippy approximate-constant allowance to the construction data table,
  preserving the server-authored collision coordinates and angles.

Client regression: 683 passed, 4 ignored. Client Clippy completes with existing warnings.
Touched-file formatting, doc-link and render-pass lints pass. Release doctests pass.
All 29 previously missing-addon tests pass using the recovered fixture corpus.
Full workspace with required data and recovered fixtures: 7,884 passed, 1 failed,
18 ignored. The no-install run has the same totals, including 1,665 data-gated early
returns. Commands and limitations are recorded in `build/validation-follow-up.md`.

The remaining failing test is the schedule ambiguity ratchet: Update has 5,071 actionable
unordered pairs, down from 5,196, against the unchanged ceiling of 4,980. The standard
`scripts/check.sh` gate still stops at pre-existing formatting differences across other files.
This branch is not a green landing/shipping candidate. Next: review the remaining schedule
pairs by owner, prove any ordering changes, then run the full land gates and an authorized
local playtest. The full pair list is in `build/validation-round2.log`.

Addon fixtures live only in ignored `build/addon-corpus`; set `BENILLA_ADDON_CORPUS` to its
absolute path. Sources, pinned revisions and preparation details are recorded in
`build/addon-fixture-provenance.md`. No fixtures were added to the game installation.
No game was launched, no package was replaced, and nothing was pushed or deployed in this
follow-up. Local `main` is 141 commits ahead of `origin/main`; publishing this branch as-is
would also expose that unrelated backlog.

## Main/package verification before the follow-up

- HUD work is integrated into local `main` at `7cfc2a163`. The system tooltip now reports
  unbound controls accurately; its tests match the three Modern and Utility pages.
- Client: 683 tests passed, 4 explicitly ignored, with `WOW_DATA` and `BENILLA_REQUIRE_DATA=1`.
  The HUD layout sweep, switches, options, stock-bar restoration and binding hints passed.
- Server: Release build succeeded; 818 fusion tests passed. Gate: 79 tests passed.
  Launcher: 28 tests passed and Release build succeeded with the production host settings.
- Local package client and staged symbols were updated and verified by SHA-256. The previous
  client and staged symbols are preserved in `build/local-release-7cfc2a163/`.
  Packaged server binaries and custom SQL already matched the verified source; no database
  changes or server restart were needed.
- The full formatting check has existing failures outside the HUD changes. The touched Rust
  files pass formatting. Clippy reports 10 existing `approx_constant` errors in unchanged
  `client/src/build/pieces.rs`; no diagnostics were reported in the HUD files.
- The unrestricted benilla workspace run reported 7,843 passed, 42 failed, 18 ignored, plus
  a doctest compilation error. One failure was a stale buildstamp test binary from a removed
  worktree; clearing only that package's release cache and rerunning its tests passed.
  The remaining baseline is 41 failed tests (29 missing-addon fixtures, 12 other validation
  failures) plus the doctest error. Details: `build/handoff-verification.md`.
- No game client was launched, no live playtest was performed, and nothing was pushed or
  deployed. A local playtest and explicit owner approval remain necessary before live shipping.

## What this is
A WoW 1.12 private server + custom client that adds MW2-style gunplay, two-hand action systems (hands/spells),
companions (monster tamer), construction/plots, Wanderers (living NPC-players), bounty boards and more.
Owner: Justin. Site: https://womw.net. Discord: https://discord.gg/PgzdqFmHv. Subreddit: r/womw.

## Repo layout (monorepo `C:\Projects\azeroth-warfare`)
- `server/` vmangos fork (C++, GPL-2.0). Our code: `server/src/game/Fusion/` (game glue) and
  `server/src/fusion/` (pure rules library + `fusion_tests`). Custom world SQL:
  `server/sql/custom/azeroth_warfare/NN_*.sql` (applied on every ship, must stay idempotent).
- `benilla/` Rust/Bevy WoW 1.12 client fork (MIT/Apache). Read `benilla/AGENTS.md` + `docs/METHOD.md`.
- `client/` our Rust game layer on top of benilla (the game exe `benilla-mw2.exe`).
- `launcher/` Rust launcher (sign-up, attestation, downloads, crash reports).
- `package/` build.ps1 (assembles `dist/`), publish.py (uploads a release to the VPS).
- `vps/` gate.py (web: sign-up/attest/download auth/crash upload), Caddyfile, aw-admin.sh, ship/setup
  scripts, ADMIN.md (operations manual: bans, feedback, crashes, backups, wanderers).
- `admin/` Windows .bat wrappers around aw-admin over ssh; `pull-backups.ps1`.
- `docs/` website (GitHub Pages); `docs/devlog/` posts (first post is a DRAFT, unlinked).
- `design/` GAME_DESIGN.md (the source of truth for systems), CLASS_CATALOG.md,
  COMPANION_CATALOG.md, WANDERERS.md, SECURITY_REVIEW_2026-10.md, IDEAS.md.
- `OPEN_SOURCE_CHECKLIST.md`, `tools/make-public-tree.ps1`, `tools/public-exclude.txt`,
  `private-assets.md`, `NOTICE.md`.

## Config / secrets
- Deployment values live in gitignored `vps/deploy.env.local` (template `vps/deploy.env.example`):
  VPS login, ssh key path, hostnames, backup folder. Never commit it.
- SSH to the VPS is key-only. Nightly DB backups on the VPS (`aw-backup.timer`) and copied to the
  owner's PC daily by the "WOMW backup copy" scheduled task.

## Build and test (Windows)
- Client: `cd client; cargo test --release --lib; cargo build --release`
  (683 tests passed, 4 ignored at the latest verification).
- Server: `tools\cmake\bin\cmake.exe build\server` (reconfigure when files are added), then
  `cmake --build build\server --config Release -j 6`, then `server\bin\Release\fusion_tests.exe`
  (818 tests, 0 failures at the latest verification). Configure can rewrite
  `server/src/shared/Progression.h`: inspect the diff and restore only generated changes;
  preserve any pre-existing edits.
- benilla tests: use an absolute `WOW_DATA` path to `dist\AzerothWarfare\game\WoW\Data` and
  `BENILLA_REQUIRE_DATA=1`; run `cargo test --release --workspace --no-fail-fast` from `benilla/`.
  Local main's measured baseline has 41 outstanding test failures plus a doctest compilation
  error. The validation branch above repairs these except the schedule ambiguity ratchet;
  its addon checks also need the recovered `BENILLA_ADDON_CORPUS`. Neither baseline is green.
  On Windows, sandbox restrictions can add false failures in localhost and file-persistence
  tests; rerun those outside the sandbox before attributing failures to the code.
- Gate: `python -m unittest vps/test_gate.py`.
- Launcher needs compile-time env `AW_HOST=https://play.womw.net AW_GAME_HOST=play.womw.net`.
- After removing a git worktree that built benilla into the shared target:
  `cargo clean --release -p lua-src -p mlua-sys` (stale paths otherwise break builds).

## Local server (owner's PC)
`dist\AzerothWarfare\` = local package (server, DB, game files; never delete it, its DB has real
characters). `scripts\aw.ps1 start|stop`, `Play.bat`. Local deploy = start DB, apply new SQL files,
`package\build.ps1 -SkipCompile`, restart. Wanderers are enabled locally in `var\etc\mangosd.conf`.

## Shipping to the live VPS (play.womw.net)
1. `wsl -d Ubuntu-24.04 -u root -- bash vps/ship-server.sh` (Linux build + install + all custom SQL +
   gate/Caddy/aw-admin; also creates the attestation table that sign-ups need).
2. Update the local package (above), then `python package/publish.py --note "..."` (publishes the
   client/launcher from `dist`; archives client symbols under `build\symbols`).
3. `git push origin main` (also republishes the website).
Live server is on the build shipped 2026-10-21 (server only); the client was last published
2026-10-19 17:00. **Pending ship:** everything merged after that (see below) needs steps 1-3.

## State of work at handoff
Merged into local `main` (pushed to `origin/overnight-2026-10-20` earlier; main NOT pushed since):
action systems (Modern/Classic/Utility bars), UI chrome (draggable windows, renames), spells v2,
open-source prep + attestation, companion UI + Bestiary + per-stage looks, abilities scaling,
world map, kits rework (Rails up to 4 attachments), utility tools/gathering/decay/repair,
third-person animations, exploit review + follow-ups, killstreak polish, bounty boards,
Wanderers phase 1 + "lives", Rogue rework (Assassin/Trickster/Duelist; Bard removed), Minstrelsy
profession (instruments crafted only by Minstrelsy), spell visuals (real WoW effects; spray retune).

A combined build/test + local update has now been completed for the HUD integration; see the
verification above and the logs under `build/`. The broader benilla and lint baseline is not green.

Retained worktrees:
- `C:\Projects\aw-wt-hud-rework` (branch `hud-rework`): 6 larger action buttons and a HUD redesign
  around the three action systems, now reviewed, tested and integrated into local `main`.
  Kept because the shared Cargo artifacts refer to this checkout; do not remove casually.
Other worktrees under `C:\Projects\aw-wt-*` may be stale; `git worktree list` shows them.

## Open decisions for the owner
- Wanderers: final name, count per zone, PvP rules vs enemy/own-faction wanderers, level scaling
  (design/WANDERERS.md section 12).
- Open source: run `tools\make-public-tree.ps1 -Out <empty folder> -Strict`, review the scan, then
  follow OPEN_SOURCE_CHECKLIST.md to create the PUBLIC repo (owner's call; not done). The devlog
  first post needs owner approval before linking.

## Working rules the owner set
- Never delete databases, volumes, `dist/` or game files without asking.
- Deploy to the live VPS only with the owner's explicit OK.
- Live in-game tests only when the owner allows; don't start the game client while he plays.
- Keep menus/UI close to classic WoW; lore-friendly names for MW2 content; plain-language reports.
