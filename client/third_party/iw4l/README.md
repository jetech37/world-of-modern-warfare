# Vendored IW4L asset layer

These crates are copied unmodified from
[2010 Rust Rewrite Mashup](https://github.com/chasmlol/2010-rust-rewrite-mashup) at
`f608f85e407ff1b7689d54a9aafdd16e95711ac4`, itself a fork of
[IW4L](https://github.com/vladtrc/iw4L) by vladtrc and contributors, a from-scratch Rust
rewrite of Modern Warfare 2. They are Apache-2.0 (`LICENSE`, `NOTICE.upstream`).

They are the engine-independent half of IW4L: finding an MW2 install and reading its
FastFiles and IWDs (`asset_transport`, `fastfile_*`, `asset_iw4`), and turning that data into
models, skeletons, animations, materials, images and sounds (`asset_model`, `xmodel_runtime`,
`anim_iw4`, `asset_anim`, `asset_material`, `render_material`, `d3d9_*`, `asset_audio`), plus
MW2's weapon and HUD rules (`weapon_iw4`, `hud_iw4`, `fx_iw4`). benilla-mw2 uses them to put
MW2's own guns, animations, sounds and HUD into the benilla WoW client.

The only change on the way in is the workspace around them: `Cargo.toml` at the repo root
points their `bevy` and `glam` at the versions benilla uses (Bevy 0.18, glam 0.30) instead of
IW4L's (Bevy 0.19, glam 0.32). No MW2 data is included; it is read from your own install.

`fx_iw4/data/fx_random_table.bin` is upstream's file, kept as upstream ships it.

To update: copy the same crate folders from a newer mashup or IW4L revision and update the
revision above.
