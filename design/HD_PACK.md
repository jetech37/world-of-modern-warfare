# The optional HD pack (PRIVATE)

Private, like `COMMUNITY_MERGE.md`: never in the public tree (`tools/public-exclude.txt`), and never
name the community client's developers anywhere public ("the community client").

Owner decision (Oct 2026): "HD pack: include it as an optional download with an easy in game on/off
option."

## Source

The community client's HD archives in its `Data` folder: `patch-A`, `B`, `C`, `D`, `E`, `G`, `I`,
`L`, `M`, `T`, `U`, `Y` (12 GB on disk, 96,097 listed files, 21.4 GB uncompressed). Read only;
nothing there is run. Their content patches `patch-3`..`patch-9` are already in our community layer.

## Survey (`community-merge hd ... --survey-only`; full list in `private-assets/womw-hd/survey.tsv`)

What each archive holds (top copy only):

| archive | GB | files | mostly |
|---|---:|---:|---|
| patch-A | 1.75 | 9647 | baked NPC skins, guild emblems, dungeon textures; 21 character models |
| patch-B | 1.61 | 4486 | dungeon/WMO textures |
| patch-C | 2.08 | 4968 | creature textures and 318 creature models, 218 sounds |
| patch-D | 1.65 | 7268 | world textures, 917 doodads, 171 WMOs |
| patch-E | 0.72 | 1685 | terrain (tileset) textures |
| patch-G | 0.77 | 4156 | item textures and 478 item models |
| patch-I | 0.16 | 31707 | UI textures (icons) |
| patch-L | 0.04 | 2 | character models |
| patch-M | 0.44 | 3563 | UI textures (world map, glues) |
| patch-T | 0.54 | 6751 | guild emblems, character textures |
| patch-U | 1.66 | 21861 | item texture components, character skins |
| patch-Y | 0.00 | 3 | UI code |

Per kind, against our side and benilla:

| kind | files | GB | kept | kept GB | our layer wins | left out otherwise |
|---|---:|---:|---:|---:|---:|---|
| UI code (Lua/XML/TOC) | 3 | 0.00 | 0 | 0 | 0 | UI code 3 |
| UI textures | 35240 | 1.40 | 3428 | 0.14 | 2831 | new and unreferenced 28981 (their own icons/maps) |
| WMOs | 171 | 0.05 | 0 | 0 | 5 | WMO geometry 166 |
| character models | 23 | 0.41 | 8 | 0.13 | 2 | texture ours/missing 12, unreferenced 1 |
| character textures | 7500 | 1.20 | 111 | 0.02 | 3 | skin composite size 4021, unreferenced 3365 |
| creature models | 318 | 0.94 | 164 | 0.31 | 10 | texture ours/missing 38, unreferenced 104, no decode 2 |
| creature textures | 4372 | 2.41 | 1866 | 0.90 | 226 | unreferenced 2280 |
| item models | 478 | 0.02 | 285 | 0.01 | 0 | texture ours/missing 48, unreferenced 142, no decode 3 |
| item textures | 18976 | 5.36 | 2531 | 0.66 | 835 | skin composite size 9958, unreferenced 5652 |
| other textures (dungeon, emblems, baked NPC) | 20190 | 5.95 | 8898 | 2.27 | 1289 | unreferenced 9999, no decode 4 |
| sound | 218 | 0.02 | 216 | 0.02 | 0 | unreferenced 2 |
| spell textures/models | 63 | 0.01 | 55 | 0.01 | 2 | no decode 1, unreferenced 5 |
| tables (DBC) | 12 | 0.01 | 0 | 0 | 6 | client table 6 |
| terrain textures | 1335 | 0.88 | 1047 | 0.63 | 25 | unreferenced 263 |
| world doodads | 918 | 0.05 | 354 | 0.01 | 6 | collision differs 346, unreferenced 198, texture 7, no decode 7 |
| world textures | 6168 | 2.68 | 4097 | 1.63 | 443 | unreferenced 1628 |
| other (txt, etc.) | 109 | 0.02 | 3 | 0 | 0 | unused kind |

Collisions with our side: 152 paths in our WotLK layer `patch-9` (Dalaran/Wrath), 5531 in our
community layer `patch-A`; all left out. Our guns, hands and companions are MW2 assets and hand tracks read
from those installs (no WoW paths) or rows over stock/patch-9 models, so nothing in the pack
shadows them. Terrain tiles: the HD archives hold none.

Decode through benilla (benilla-mpq reads every archive; models through the M2 loader's submesh
and skeleton parse, textures through the BLP decoder, WMOs through benilla-wmo): 17 fail, all left
out: 10 WotLK-format M2s (version 264), 3 non-MD20 models (Legion-era doodads), 5 BLPs with a
broken mip table. Kept textures are 8 to 1024 px (5970 at 1024); benilla takes them.

## The rules (`tools/community-merge/src/hd.rs`, `decide`/`judge`)

1. Our layers win: any path in `patch-9` or `patch-A` is left out.
2. The world stays the server's: no client tables, terrain tiles or WMO geometry (the server's
   maps and vmaps are built from ours); a world doodad only when its collision hull equals stock's.
3. Stock UI code stays stock (benilla loads FrameXML byte for byte); UI textures may upgrade.
4. Character-skin composite textures (CharSections skins/faces/facial hair/scalps/underwear and
   `Item\TextureComponents`) only at the stock size: benilla, like 1.12, blits them unscaled into a
   256² atlas, so a 2x copy would land in the wrong tiles. This drops most "HD skins".
5. A model whose hard-coded texture our layers override, or nobody has, is left out.
6. A file stock lacks comes in only when a kept model names it.
7. It must decode through benilla.

## The pack

`private-assets/womw-hd/`: `womw-hd-1.MPQ` .. `womw-hd-5.MPQ` (four of 1.61 GB, one of 0.31 GB),
23,063 files, **6.75 GB**, plus `womw-hd.txt` (the manifest: each archive and its byte size),
`report.txt`, `survey.tsv`. Stored uncompressed (our writer's V1 form).

Build:

    cd tools\community-merge; cargo build --release
    target\release\community-merge.exe hd --hd "<community client>\Data" `
      --ours dist\AzerothWarfare\game\WoW\Data `
      --wotlk private-assets\womw-patch\patch-9.MPQ `
      --community-layer private-assets\womw-community\patch-A.MPQ `
      --out private-assets\womw-hd          # --survey-only: the report alone

Rebuild after either of our layers changes.

## Mounting and the switch

- The names are not `patch-?.MPQ`: in a `Data` folder they mount nowhere and the server's
  extractors skip them. The pack lives in `<package>\hd\` (launcher install: `<install>\hd\`).
- benilla (`benilla-app/src/hd_pack.rs`) finds it at boot (`$WOMW_HD_DIR`, else `hd\` beside the
  game or the client), requires the manifest with every archive at its size (a partial download
  never mounts), and mounts it with `Chain::open_with_inserted` just below the first built layer
  (`patch-?` past `patch-3`): stock < HD pack < patch-9 < patch-A.
- CVar `womwHdPack` (default 1, saved in `benilla-config/config.toml`) is read at boot only:
  **a change applies at the next start** (the `mpq://` source is built before the app; a live
  remount would mean reloading every loaded asset). The row prints a restart note in chat.
- Options > Graphics > **HD Textures and Models**: greyed with the hint "Requires the HD pack,
  which is not installed..." when boot found none (session CVar `womwHdInstalled`).
- Without the pack, or with it off, the chain is exactly the `Data` folder's.
- A pack that fails to open falls back to the `Data` folder alone.

## Download

- `package/build.ps1` copies the pack to `dist\AzerothWarfare\hd\` (manifest last), not into the
  base game.
- `package/publish.py --hd` uploads `hd/**` and `hd-manifest.json` (behind the same account check
  as `/files/*`); the normal publish never includes it.
- The launcher, after the game update, offers "Download HD pack? [y/N]" with its size when the
  server has `hd-manifest.json`; no is remembered (`hd-declined.txt`; `--hd` asks again); the pack's
  manifest downloads last, and an interrupted download resumes next run without blocking play.

## Local install (owner)

Without running build.ps1:

    robocopy C:\Projects\azeroth-warfare\private-assets\womw-hd C:\Projects\azeroth-warfare\dist\AzerothWarfare\hd womw-hd-*.MPQ
    copy C:\Projects\azeroth-warfare\private-assets\womw-hd\womw-hd.txt C:\Projects\azeroth-warfare\dist\AzerothWarfare\hd\

and the client built from this branch in `dist\AzerothWarfare\client\`. The game log says
`hd pack: on (...) (5 archives mounted)`.

## Still to check in game

Look test of HD character/creature models with first-person arms and companions; any creature
whose DBC skin and HD model disagree.
