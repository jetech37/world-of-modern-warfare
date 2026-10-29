//! Dumps every multiplayer weapon definition in the install as tab-separated rows (name, models,
//! hidden tags, stats), for building the catalog; `--models` lists the viewmodel XModels instead, and
//! `--surfaces <model>...` each surface's material and textures, `--image <material> <out.tga>`
//! a material's colour map:
//! `MW2_PATH=... cargo run --release --example mw2_export > weapons.tsv`

use benilla_mw2::mw2::load::Mw2Library;

const COLUMNS: [&str; 36] = [
    "name",
    "display",
    "gun",
    "world",
    "hide_tags",
    "damage",
    "min_damage",
    "max_range",
    "min_range",
    "fire_ms",
    "pellets",
    "clip",
    "max_ammo",
    "move",
    "ads_move",
    "ads_fov",
    "reload",
    "reload_empty",
    "segmented",
    "class",
    "inventory",
    "fire_type",
    "alternate",
    "rechamber_ms",
    "bolt",
    "overlay",
    "overlay_interface",
    "overlay_reticle",
    "overlay_w",
    "overlay_h",
    "hip_min",
    "hip_max",
    "ads_spread",
    "explosion_radius",
    "explosion_inner",
    "explosion_outer",
];

fn main() {
    let lib = Mw2Library::open().expect("MW2 install");
    let args: Vec<String> = std::env::args().collect();
    if let Some(i) = args.iter().position(|a| a == "--image") {
        // `--image <material> <out.tga>`: a material's colour map as an uncompressed TGA.
        let (name, out) = (&args[i + 1], &args[i + 2]);
        let img = lib.material_image(name).expect("no such material image");
        let mut tga = vec![0u8, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        tga.extend_from_slice(&(img.width as u16).to_le_bytes());
        tga.extend_from_slice(&(img.height as u16).to_le_bytes());
        tga.extend_from_slice(&[32, 0x28]); // 32 bit, top-left origin, 8 alpha bits
        for px in img.pixels.as_chunks::<4>().0 {
            tga.extend_from_slice(&[px[2], px[1], px[0], px[3]]);
        }
        std::fs::write(out, tga).expect("write");
        println!("{name}: {}x{} -> {out}", img.width, img.height);
        return;
    }
    if let Some(i) = args.iter().position(|a| a == "--material") {
        for line in lib.describe_material(&args[i + 1]) {
            println!("{line}");
        }
        return;
    }
    if let Some(i) = args.iter().position(|a| a == "--materials") {
        // `--materials <substring>...`: material names containing any of them.
        let mut names: Vec<String> = lib
            .material_names()
            .filter(|n| args[i + 1..].iter().any(|f| n.contains(f.as_str())))
            .collect();
        names.sort();
        names.dedup();
        for n in names {
            println!("{n}");
        }
        return;
    }
    if let Some(i) = args.iter().position(|a| a == "--bones") {
        for model in &args[i + 1..] {
            println!("== {model}: {}", lib.bone_names(model).join(" "));
        }
        return;
    }
    if let Some(i) = args.iter().position(|a| a == "--surfaces") {
        for model in &args[i + 1..] {
            println!("== {model}");
            for line in lib.describe_surfaces(model) {
                println!("{line}");
            }
        }
        return;
    }
    if std::env::args().any(|a| a == "--models") {
        let mut models: Vec<_> = lib.viewmodel_names().collect();
        models.sort();
        for m in models {
            println!("{m}");
        }
        return;
    }
    let mut names: Vec<_> = lib.weapons.keys().cloned().collect();
    names.sort();
    println!("{}", COLUMNS.join("\t"));
    for n in names {
        let r = &lib.weapons[&n];
        let s = &r.stats;
        let row: [String; COLUMNS.len()] = [
            r.name.clone(),
            s.display_name.clone().unwrap_or_else(|| "-".into()),
            r.gun.clone().unwrap_or_else(|| "-".into()),
            s.world_model.clone().unwrap_or_else(|| "-".into()),
            r.hide_tags.join(","),
            s.damage.to_string(),
            s.min_damage.to_string(),
            s.max_damage_range.to_string(),
            s.min_damage_range.to_string(),
            s.fire_time_ms.to_string(),
            s.shots_per_fire.to_string(),
            s.clip_size.to_string(),
            s.max_ammo.to_string(),
            s.move_speed_scale.to_string(),
            s.ads_move_speed_scale.to_string(),
            s.ads_zoom_fov.to_string(),
            r.reload.secs.to_string(),
            r.reload.empty_secs.to_string(),
            r.reload.segmented.to_string(),
            s.weap_class.to_string(),
            s.inventory_type.to_string(),
            s.fire_type.to_string(),
            s.alternate.clone().unwrap_or_else(|| "-".into()),
            s.rechamber_time_ms.to_string(),
            s.bolt_action.to_string(),
            r.scope.overlay.clone().unwrap_or_else(|| "-".into()),
            r.scope.interface.to_string(),
            r.scope.reticle.to_string(),
            r.scope.width.to_string(),
            r.scope.height.to_string(),
            s.spread.stand_min.to_string(),
            s.spread.stand_max.to_string(),
            s.spread.ads.to_string(),
            s.explosion_radius.to_string(),
            s.explosion_inner_damage.to_string(),
            s.explosion_outer_damage.to_string(),
        ];
        println!("{}", row.join("\t"));
    }
}
