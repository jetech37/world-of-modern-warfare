//! The atmosphere's shader paths composed and validated as Bevy's pipeline cache does it (naga_oil
//! over Bevy's own shader libraries, then naga's validator), so a WGSL mistake fails here and not
//! as an invisible draw at runtime. Each case is one pipeline's shader defs.

use std::collections::HashMap;

use bevy::prelude::*;
use bevy::shader::{Shader, ShaderImport};
use naga_oil::compose::{Composer, NagaModuleDescriptor, ShaderDefValue};

/// The headless engine boot (no window, no GPU), with the world's shaders registered and every
/// library Bevy's plugins load, run until `paths` are all loaded.
fn loaded_shaders(paths: &[&'static str]) -> (App, Vec<Handle<Shader>>) {
    let mut app = App::new();
    app.add_plugins(
        crate::boot::tuned_default_plugins(Window::default())
            .disable::<bevy::winit::WinitPlugin>()
            .disable::<bevy::log::LogPlugin>()
            .set(bevy::window::WindowPlugin {
                primary_window: None,
                exit_condition: bevy::window::ExitCondition::DontExit,
                ..default()
            })
            .set(bevy::render::RenderPlugin {
                render_creation: bevy::render::settings::WgpuSettings {
                    backends: None,
                    ..default()
                }
                .into(),
                ..default()
            }),
    );
    benilla_assets::materials::register_shaders(&mut app);
    crate::shaders::plugin(&mut app);
    app.finish();
    app.cleanup();
    let handles: Vec<Handle<Shader>> = paths
        .iter()
        .map(|p| app.world().resource::<AssetServer>().load(*p))
        .collect();
    let mut stable = 0;
    let mut last = 0;
    for _ in 0..2000 {
        app.update();
        let shaders = app.world().resource::<Assets<Shader>>();
        let all = handles.iter().all(|h| shaders.contains(h));
        let n = shaders.len();
        stable = if n == last { stable + 1 } else { 0 };
        last = n;
        if all && stable > 20 {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(2));
    }
    (app, handles)
}

fn add_import(
    composer: &mut Composer,
    by_import: &HashMap<String, &Shader>,
    import: &ShaderImport,
) -> Result<(), String> {
    let name = import.module_name();
    if composer.contains_module(&name) {
        return Ok(());
    }
    let shader = by_import
        .get(name.as_ref())
        .ok_or_else(|| format!("import {name} not loaded"))?;
    for i in &shader.imports {
        add_import(composer, by_import, i)?;
    }
    composer
        .add_composable_module((*shader).into())
        .map_err(|e| format!("{name}: {e:?}"))?;
    Ok(())
}

/// Compose and validate `shader` under `defs`; the error names the case.
fn check(shaders: &Assets<Shader>, shader: &Handle<Shader>, defs: &[(&str, ShaderDefValue)]) {
    let by_import: HashMap<String, &Shader> = shaders
        .iter()
        .filter_map(|(_, s)| match &s.import_path {
            ShaderImport::Custom(p) => Some((p.clone(), s)),
            ShaderImport::AssetPath(_) => None,
        })
        .collect();
    let shader = shaders.get(shader).expect("target shader loaded");
    let mut composer = Composer::default().with_capabilities(naga::valid::Capabilities::all());
    for i in &shader.imports {
        add_import(&mut composer, &by_import, i).unwrap_or_else(|e| panic!("{defs:?}: {e}"));
    }
    let mut shader_defs: HashMap<String, ShaderDefValue> = HashMap::new();
    // The pipeline cache's global def for a desktop adapter.
    shader_defs.insert(
        "AVAILABLE_STORAGE_BUFFER_BINDINGS".into(),
        ShaderDefValue::UInt(8),
    );
    for (k, v) in defs {
        shader_defs.insert((*k).to_string(), *v);
    }
    let module = composer
        .make_naga_module(NagaModuleDescriptor {
            shader_defs,
            ..shader.into()
        })
        .unwrap_or_else(|e| panic!("{} {defs:?}: {}", shader.path, e.emit_to_string(&composer)));
    naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(),
        naga::valid::Capabilities::all(),
    )
    .validate(&module)
    .unwrap_or_else(|e| panic!("{} {defs:?}: {e:?}", shader.path));
}

const B: ShaderDefValue = ShaderDefValue::Bool(true);

/// The defs a material's colour pass on the world camera carries (`MeshPipeline::specialize`).
fn main_pass_defs() -> Vec<(&'static str, ShaderDefValue)> {
    vec![
        ("MATERIAL_BIND_GROUP", ShaderDefValue::UInt(3)),
        ("MAX_DIRECTIONAL_LIGHTS", ShaderDefValue::UInt(10)),
        ("MAX_CASCADES_PER_LIGHT", ShaderDefValue::UInt(4)),
        ("VERTEX_OUTPUT_INSTANCE_INDEX", B),
        ("VERTEX_POSITIONS", B),
        ("VERTEX_NORMALS", B),
        ("VERTEX_UVS", B),
        ("VERTEX_UVS_A", B),
        ("VERTEX_COLORS", B),
    ]
}

#[test]
fn the_world_shaders_compose_and_validate_with_the_atmosphere() {
    let paths = [
        "embedded://benilla_assets/shaders/wow_model.wgsl",
        "embedded://benilla_assets/shaders/wow_model_caster.wgsl",
        "embedded://benilla_assets/shaders/terrain.wgsl",
        "embedded://benilla_world/shaders/static_gx.wgsl",
    ];
    let (app, handles) = loaded_shaders(&paths);
    let shaders = app.world().resource::<Assets<Shader>>();
    let [model, caster, terrain, gx] = [&handles[0], &handles[1], &handles[2], &handles[3]];

    // The model lane: plain, with clutter's UV1 (the wind), skinned, merged, water-clipped, sky,
    // under both shadow filters.
    for filter in [
        "SHADOW_FILTER_METHOD_HARDWARE_2X2",
        "SHADOW_FILTER_METHOD_GAUSSIAN",
    ] {
        let mut base = main_pass_defs();
        base.push((filter, B));
        check(shaders, model, &base);
        for extra in [
            vec![("VERTEX_UVS_B", B)],
            vec![("WOW_RIG_SKIN", B)],
            vec![("WOW_MERGED_FADE", B), ("WOW_MERGED_SLOT", B)],
            vec![("WOW_WATER_CLIP", B)],
            vec![("WOW_SKY_DEPTH", B)],
        ] {
            let mut defs = base.clone();
            defs.extend(extra);
            check(shaders, model, &defs);
        }
        // Terrain: COLOR (the layers) and UV1 (alpha and MCSH indices).
        let mut defs = base.clone();
        defs.push(("VERTEX_UVS_B", B));
        check(shaders, terrain, &defs);
    }

    // The model caster, as Bevy's prepass pipeline specializes it for a directional cascade.
    let caster_base = vec![
        ("PREPASS_PIPELINE", B),
        ("MATERIAL_BIND_GROUP", ShaderDefValue::UInt(3)),
        ("MAX_DIRECTIONAL_LIGHTS", ShaderDefValue::UInt(10)),
        ("MAX_CASCADES_PER_LIGHT", ShaderDefValue::UInt(4)),
        ("VERTEX_OUTPUT_INSTANCE_INDEX", B),
        ("DEPTH_PREPASS", B),
        ("VERTEX_POSITIONS", B),
    ];
    for extra in [
        vec![],
        vec![("VERTEX_UVS", B), ("VERTEX_UVS_A", B), ("MAY_DISCARD", B)],
        vec![("VERTEX_UVS", B), ("VERTEX_UVS_A", B), ("WOW_RIG_SKIN", B)],
        vec![("WOW_MERGED_FADE", B)],
        vec![
            ("VERTEX_UVS", B),
            ("VERTEX_UVS_A", B),
            ("UNCLIPPED_DEPTH_ORTHO_EMULATION", B),
            ("PREPASS_FRAGMENT", B),
        ],
    ] {
        let mut defs = caster_base.clone();
        defs.extend(extra);
        check(shaders, caster, &defs);
    }

    // The retained statics: colour pipelines and casters, opaque and cutout.
    let lights = vec![
        ("MAX_DIRECTIONAL_LIGHTS", ShaderDefValue::UInt(10)),
        ("MAX_CASCADES_PER_LIGHT", ShaderDefValue::UInt(4)),
    ];
    for extra in [
        vec![],
        vec![("GX_CUTOUT", B)],
        vec![("GX_SHADOW_CASTER", B)],
        vec![("GX_SHADOW_CASTER", B), ("GX_CUTOUT", B)],
    ] {
        let mut defs = lights.clone();
        defs.extend(extra);
        check(shaders, gx, &defs);
    }
}
