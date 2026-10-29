# Notices, licenses and credits

World of Modern Warfare (WOMW) is several programs in one repository, under more than one
license. Each part keeps the license it came with. Each license covers exactly the paths listed
for it below; a path that is not listed is not covered by any license of this repository.

## License layout

| Path | License | Notes |
|---|---|---|
| `client/src/`, `client/examples/`, `client/docs/`, `client/scripts/`, `client/build.rs`, `client/Cargo.toml`, `client/Cargo.lock`, `client/rust-toolchain.toml`, `client/README.md`, `client/*.bat`, `client/assets/guide.md`, `client/assets/class_options.json`, `client/assets/arsenal_names.json` | GNU GPL v3.0 only (`LICENSE`) | The shooter client: our code and documents |
| `launcher/`, `package/`, `vps/`, `admin/`, `docs/`, `design/` | GNU GPL v3.0 only (`LICENSE`) | Launcher, packaging, hosting, website, design documents |
| `tools/*.ps1`, `tools/*.py`, `tools/release-assets.json` | GNU GPL v3.0 only (`LICENSE`) | Our scripts |
| `tools/community-merge/`, `tools/casc-read/` | MIT OR Apache-2.0 (their `Cargo.toml`) | Tools that reuse benilla's code |
| `README.md`, `NOTICE.md`, `CONTRIBUTING.md`, `SECURITY.md`, `HANDOFF.md`, `OPEN_SOURCE_CHECKLIST.md`, `private-assets.md`, `.gitignore` | GNU GPL v3.0 only (`LICENSE`) | Top-level documents |
| `server/` | GNU GPL v2.0 (`server/LICENSE`) | A fork of [vmangos](https://github.com/vmangos/core) (itself from MaNGOS); our additions (`server/src/fusion`, `server/src/game/Fusion`, `server/sql/custom/azeroth_warfare`, `server/azeroth-warfare`, `server/contrib/fusion`) are GPL-2.0 like the rest of the server. Its file headers and notices are kept as they are |
| `benilla/` | MIT OR Apache-2.0 (`benilla/LICENSE-MIT`, `benilla/LICENSE-APACHE`) | A fork of [benilla](https://github.com/samwhosung/benilla) by Sam and contributors; our changes to it are under the same terms |
| `client/third_party/iw4l/` | Apache-2.0 (`LICENSE`, `NOTICE.upstream` there) | Vendored crates of [IW4L](https://github.com/vladtrc/iw4L) by vladtrc and contributors, through the [2010 Rust Rewrite Mashup](https://github.com/chasmlol/2010-rust-rewrite-mashup) by chasmlol |
| `benilla/third_party/kira/` | MIT OR Apache-2.0 | [Kira](https://github.com/tesselode/kira) by Andrew Minnich, vendored by benilla |
| `benilla/third_party/lua-src/` | MIT (`LICENSE` there) | The `lua-src` crate (Aleksandr Orlenko) with Lua 5.1.5 (MIT, Lua.org, PUC-Rio), vendored by benilla |
| `server/dep/` | Each library's own | Vendored by vmangos; see below |

The client links benilla (MIT OR Apache-2.0) and the IW4L crates (Apache-2.0); both are
compatible with the GPL v3, and their license texts stay with them.

### Libraries vendored by the server (`server/dep/`)

| Path | License |
|---|---|
| `cpptrace/` | MIT (`LICENSE`) |
| `g3dlite/` | BSD-style (G3D Innovation Engine), patched as `Readme.txt` describes |
| `gsoap/` | gSOAP public license or GPL (per its headers) |
| `include/bzip2/`, `src/bzip2/` | bzip2 license (BSD-style) |
| `include/mersennetwister/` | BSD-3-Clause (in its header) |
| `include/nlohmann/` | MIT |
| `include/utf8cpp/` | Boost Software License 1.0 (`LICENSE`) |
| `libmpq/` | GPL-2.0 (`COPYING`) |
| `loadlib/` | GPL-2.0 (MaNGOS) |
| `recastnavigation/` | zlib (`License.txt`) |
| `src/libseh/` | as in its `README.txt` |
| `src/zlib/` | zlib |
| `windows/lib/` | prebuilt Windows libraries as vmangos ships them: OpenSSL (`libeay32`), the MySQL client library, Microsoft's dbghelp |

## Credits

- **vmangos** and the MaNGOS family of projects: the server this one is built on.
- **benilla** by Sam and contributors: the from-scratch 1.12.1 client this one is built on.
- **IW4L** by vladtrc and contributors, and the **2010 Rust Rewrite Mashup** by chasmlol: the
  asset-reading crates the client vendors.

## Third-party game data in this repository

Unlike the rest of the tree, these files are derived from commercial games and are **not** under
this repository's licenses. They are included for people who own the games, and stay the property
of their owners:

| Path | Derived from | Owner |
|---|---|---|
| `client/assets/fusion_catalog.json`, `server/contrib/fusion/catalog.json` | Weapon numbers, attachment combinations and model names read from Modern Warfare 2 (2009) | Activision Publishing, Inc. |
| `client/src/mw2/arm_cut_tests.rs` (fixtures) | Joint and vertex measurements from MW2 and WoW 1.12 | Activision Publishing, Inc.; Blizzard Entertainment, Inc. |
| `game-data/womw-patch/server-dbc/*.dbc` | Display tables read from World of Warcraft 3.3.5a | Blizzard Entertainment, Inc. |
| `server/sql/` world data, `benilla/reference/*.tsv` | The World of Warcraft 1.12 world, as vmangos and benilla publish it | Blizzard Entertainment, Inc. |
| `server/sql/custom/azeroth_warfare/8*_community_*.sql`, `design/COMMUNITY_*` | Custom 1.12-format content from a community project, used with its developers' permission (they asked not to be named or credited) | Its developers |

The large data files (the WotLK, community and HD archives) are not in git. They are downloaded
from the project's site (`tools/release-assets.json`, `tools/fetch-release-assets.ps1`) on the same
terms.
None of this is offered under the GPL, and none of it may be sold. If you hold rights in any of it
and want it removed, open an issue or use the contact in `SECURITY.md`.

## Fan project, trademarks and copyright

**World of Modern Warfare is an unofficial, non-commercial fan project.** It is not affiliated
with, endorsed, sponsored or approved by Blizzard Entertainment, Activision or Microsoft, or by
the owner of any other game whose data is listed above. It is free. Nothing about it is sold, and no donation buys anything
in the game.

- World of Warcraft, Warcraft and Blizzard Entertainment are trademarks or registered trademarks
  of Blizzard Entertainment, Inc. The World of Warcraft game, its world, art, data and lore are
  (c) Blizzard Entertainment, Inc.
- Call of Duty and Modern Warfare are trademarks of Activision Publishing, Inc. Call of Duty:
  Modern Warfare 2 (2009), its weapons' data and assets are (c) Activision Publishing, Inc.

All other trademarks belong to their owners. No game client is included: the programs read each
player's own installs at run time. You must own and have legal access to World of Warcraft and the
original Call of Duty: Modern Warfare 2 (2009) to play.
