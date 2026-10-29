# Contributing to World of Modern Warfare

Thanks for helping. WOMW is a small, early project: bug reports, playtesting notes, fixes and
well-scoped features are all welcome.

## Before you start

- **No new game content without asking.** Don't add files read from World of Warcraft, Modern
  Warfare 2 or any other game (art, models, sounds, maps, animations, database dumps,
  extracted tables) unless a maintainer agreed first. The few that are here are listed in
  `NOTICE.md` with their source. Large files never go in git: files over 100 MB are
  downloads from the project's site (`tools/release-assets.json`).
- **Never name the community content's developers.** The imported community zones are used with
  their developers' permission, on the condition that they are not named or credited. Call them
  "the community client" in code, comments, commits and docs.
- **No secrets or personal details.** Server addresses, keys, passwords and invite codes belong
  in `vps/deploy.env.local` (gitignored), never in a commit.
- **Talk first about big changes.** Open an issue (or ask on Discord) before a large feature or a
  change to the game's design (`design/GAME_DESIGN.md`), so nobody's work is wasted.

## Proposing a change

1. Fork the repository and make a branch from `main`.
2. Keep each pull request to one piece of work, with small, focused commits whose messages say
   what changed and why.
3. Run the checks below for every part you touched, and say in the pull request what you ran and
   how you verified the change (a test, a screenshot, a log line, a play session).
4. Open the pull request with the template filled in. A maintainer reviews it; expect questions.

Client and server change together when the protocol changes (the fusion packets in
`benilla/crates/benilla-protocol` and `server/src/fusion`): change both in one pull request.

## Per part: style and checks

### `client/` (Rust, Bevy)

- `cargo fmt`, and `cargo clippy --all-targets` without new warnings.
- `cargo test --release --lib` (set `CARGO_BUILD_JOBS` low on a machine with little memory).
  Tests that need data made from game installs are ignored or skipped without it.
- Style: comments say what the code does and the fact behind it, in plain sentences; keep
  modules focused; no `unwrap` on data from the network or the installs.

### `benilla/` (Rust, the client base)

benilla has its own rules: read `benilla/AGENTS.md`, `benilla/docs/METHOD.md` and
`benilla/docs/CONTRIBUTING.md`. In short: benilla is a faithful 1.12.1 client, and anything
1.12.1 never had (the fusion packets, the Create Account dialog) is marked as a deviation where
it lives. Run `cargo fmt --all`, `cargo clippy --workspace --all-targets -- -D warnings` and
`cargo test` for the crates you touched; tests that read the install use `WOW_DATA`.

### `server/` (C++, vmangos)

- Follow the existing style (`server/.clang-format`, `server/.editorconfig`).
- Game rules belong in the fusion library (`server/src/fusion`), which has no game-server
  dependencies and its own tests (`fusion_tests`): add a test for every rule you add or change.
  `server/src/game/Fusion` only wires the rules into the game.
- World data changes go in a new, idempotent file in `server/sql/custom/azeroth_warfare/`
  (numbered after the last one), safe to run twice.
- Build with `-DSUPPORTED_CLIENT_BUILD=5875` and run `fusion_tests`.

### `launcher/` (Rust)

- `cargo fmt`, `cargo clippy`, `cargo test`. The build needs `AW_HOST` and `AW_GAME_HOST` set
  (e.g. `AW_HOST=http://127.0.0.1 AW_GAME_HOST=localhost`).

### `vps/` (Python, shell)

- The gate uses the Python standard library only. Run `python -m unittest vps/test_gate.py`
  from the repository root; new endpoints and rules get tests there (they run against SQLite).
- Shell scripts: `bash -n`, `set -euo pipefail`, LF line endings; anything that reaches SQL is
  checked against a strict pattern first.

### `docs/` (the website)

Plain HTML and CSS, no build step. Devlog posts are Markdown in `docs/devlog/` (`YYYY-MM-DD-words.md`), turned
into pages by `python tools/build-devlog.py`; commit the generated HTML with the post.

## Reporting bugs

Use the bug report issue template: what you did, what you expected, what happened, the game
version (shown in the launcher) and, for a crash, the report the launcher saved. In the game,
**Esc > Report Bug / Suggest** sends a report straight to the server you play on.

## License

By contributing you agree that your contribution is licensed under the license of the part of
the repository it changes ([`NOTICE.md`](NOTICE.md)): GPL-3.0 for our own code, GPL-2.0 for
`server/`, MIT OR Apache-2.0 for `benilla/`.
