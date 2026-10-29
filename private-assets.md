# Data made from game installs (`private-assets/`)

Some files the game uses are made from the maintainer's own copies of other games. The small
ones are in the repository (`NOTICE.md`, "Third-party game data", and `game-data/`); the large
archives are downloads from the project's site (`https://womw.net/downloads/`) that
`tools/fetch-release-assets.ps1` fetches, together with a copy of `game-data/`, into a gitignored `private-assets/` folder
at the repository root (`tools/release-assets.json` lists them, with sizes and hashes). That
folder mirrors the repository's paths, and everything below can also be made again from your own
legally obtained installs:

```
private-assets/
  client/assets/fusion_catalog.json      the fusion catalog (MW2's weapon numbers), if not in place
  client/assets/hand_anims/*.json        first-person hand tracks, if not in place (optional)
  server/contrib/fusion/catalog.json     the same catalog, for the server
  womw-patch/                            the WotLK patch layer (optional, site download)
  womw-community/                        the community zones (optional, site download)
  womw-hd/                               the HD pack (optional, site download)
```

`AW_PRIVATE_ASSETS=<folder>` points the client build at another folder with the same layout.
A file in its normal place (`client/assets/...`, `server/contrib/fusion/...`) wins over the copy
in `private-assets/`.

## The fusion catalog (required)

`client/assets/fusion_catalog.json` and `server/contrib/fusion/catalog.json` (the same file) hold
every gun's numbers (damage, ranges, fire time, magazine, reloads, move speeds), the attachments
each gun takes and its per-attachment variants, camos, perks and deathstreaks. The guns' numbers
and variant names are read from a Modern Warfare 2 (2009) install.

The client's build (`client/build.rs`) stops with these instructions when it finds no catalog.

**From your own MW2 install** (the full catalog):

```sh
# 1. A first catalog without an install, so the client's examples can build:
python server/contrib/fusion/make_seed_catalog.py server/contrib/fusion/weapons.json client/assets/fusion_catalog.json
# 2. Read your install (MW2_PATH points at it):
cd client
cargo run --release --example mw2_export > ../build/weapons.tsv
cargo run --release --example mw2_export -- --models > ../build/models.txt
cd ..
# 3. The full catalog, for the client and the server:
python server/contrib/fusion/make_seed_catalog.py server/contrib/fusion/weapons.json client/assets/fusion_catalog.json build/weapons.tsv build/models.txt
cp client/assets/fusion_catalog.json server/contrib/fusion/catalog.json
```

**Without an install** (step 1 alone, then the copy): guns only, with approximate numbers from
`server/contrib/fusion/weapons.json` (public community testing) and no attachments. The game
runs, but Create-a-Class has no attachments and some client and server tests that check the full
catalog fail.

`package/build.ps1` and `vps/ship-server.sh` take the server's copy from
`server/contrib/fusion/catalog.json`, else `private-assets/server/contrib/fusion/catalog.json`,
and stop with a message when there is neither.

## First-person hand tracks (optional)

`client/assets/hand_anims/*.json` are first-person hand tracks (where each hand holds its weapon,
frame by frame) for melee and spellcasting. Without
them the hands use the hand-made keyframes in `client/src/melee_pose.rs`, and the tests that
need the tracks are ignored (`cfg(aw_hand_anim_tracks)`).

A set kept elsewhere can be put in `private-assets/client/assets/hand_anims/`.

## The arms' cut-edge test fixtures (optional)

`client/src/mw2/arm_cut_tests.rs` is a test module whose fixtures are arm poses measured from the
game installs (`cargo run --release --example wow_arm_cut` prints them). A tree without it skips
those tests (`cfg(aw_arm_cut_fixtures)`). Keep it in `private-assets/client/src/mw2/` and copy
it into place to run them.

## Game files in a shipped build

`package/build.ps1 -Wow <dir> -Mw2 <dir>` copies game folders into `dist\AzerothWarfare\game`;
without them each player picks their own installs on first run. Nothing in `dist/` is ever
committed, and the packaging scripts read the files above from `private-assets/` when they are
not in their normal places.

## The WotLK patch layer (optional)

`private-assets/womw-patch/` holds the later-version models and Dalaran, converted from your own
WotLK 3.3.5a install (design/WOTLK_ASSETS.md):

```sh
cd client
WOW_DATA=<1.12 Data> WOTLK_DATA=<3.3.5a Data> cargo run --release --example wotlk_import
```

It writes `patch-9.MPQ`, `server-dbc/*.dbc` and `report.txt`. `package/build.ps1` ships them when
present; without them the game runs with the 1.12 looks.

## The community zones (optional)

`private-assets/womw-community/` holds new open-world zones on Azeroth and Kalimdor, merged from a
1.12-format community client's own install (its content patches only) with
`tools/community-merge`, and `private-assets/server/sql/custom/azeroth_warfare/8*_community_*.sql`
their world rows, converted from that client's server database by
`tools/community-merge/db_import.py`:

```sh
cd tools/community-merge
cargo run --release -- areas --community <their Data> --ours <1.12 Data> --zones <ids> --out ../../private-assets/womw-community
python -I db_import.py --port <port> --theirs <their world db> --ours mangos --areas ../../private-assets/womw-community/areas.tsv --zones <ids> --map <id map> --sql ../../private-assets/server/sql/custom/azeroth_warfare/80_community_zones.sql --displays ../../private-assets/womw-community/displays.txt
cargo run --release -- build --community <their Data> --ours <1.12 Data> --above ../../private-assets/womw-patch/patch-9.MPQ --zones <ids> --displays ../../private-assets/womw-community/displays.txt --out ../../private-assets/womw-community
```

`build`, `verify`, `hd` and `db_import.py` also need the private branding rules at
`private-assets/brand-rules.tsv` (or `--brand-rules <file>`): they scrub the community client's own
name and mascot out of everything they write and fail if any is left
(`design/COMMUNITY_MERGE.md`, "The branding scrub").

`build` writes `patch-A.MPQ` (it mounts above `patch-9.MPQ`; build it after the WotLK layer, whose
table rows it carries), `server-dbc/`, `tiles.txt` and `report.txt`. `package/build.ps1` ships them
when present, and `aw.ps1` re-extracts the server's map data when the archive changes. Without them
the game runs with the stock continents.
