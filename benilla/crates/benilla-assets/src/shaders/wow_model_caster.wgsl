// The model lane's shadow caster (`WowModelExt`'s prepass shaders): depth only, into the opt-in
// atmosphere's sun cascades (`benilla_world::atmosphere`); no camera has a depth prepass, so only
// the shadow views run it. `WowModelExt::specialize` rebuilds the vertex layout to these
// locations. It mirrors `wow_model.wgsl`'s vertex transform (camera-relative, the palette skin)
// and drops every batch that leaves no opaque surface in the reference: additive, Mod and Mod2x
// cards, depth-off and depth-prime batches, the sky lane, unlit M2 batches, ground clutter and a
// model faded below half alpha.

#import bevy_pbr::{
    mesh_functions,
    mesh_view_bindings::view,
    pbr_bindings,
    pbr_types,
}

// In `WowModelExt`'s field order (materials.rs), as `wow_model.wgsl`'s.
struct ModelParams {
    clutter_fade: vec4<f32>,
    model_flags: vec4<f32>,
    sun_scale: vec4<f32>,
    tint: vec4<f32>,
    sidn: vec4<f32>,
    anim_slots: vec4<f32>,
};
@group(#{MATERIAL_BIND_GROUP}) @binding(100) var<uniform> m: ModelParams;

// The shared light buffer's layout through the skin palettes, as `wow_model.wgsl` declares it.
struct WowLight {
    light_ambient: vec4<f32>,
    light_diffuse: vec4<f32>,
    light_sun: vec4<f32>,
    light_spec: vec4<f32>,
    fog_color: vec4<f32>,
    fog_params: vec4<f32>,
    sh_c10_r: vec4<f32>,
    sh_c10_g: vec4<f32>,
    sh_c10_b: vec4<f32>,
    sh_c13_r: vec4<f32>,
    sh_c13_g: vec4<f32>,
    sh_c13_b: vec4<f32>,
    sh_c16: vec4<f32>,
    _water: array<vec4<f32>, 4>,
    grade: vec4<f32>,
    wmo_fog_color: vec4<f32>,
    wmo_fog_params: vec4<f32>,
    point_count: vec4<f32>,
    points: array<vec4<f32>, 512>,
    prop_probes: array<vec4<f32>, 57344>,
    rig_table: array<u32, 2048>,
    rig_tint: array<u32, 2048>,
    rig_origin: array<vec4<f32>, 2048>,
    matanim: array<vec4<f32>, 2048>,
    water_clip: array<vec2<f32>, 2048>,
    palettes: array<vec4<f32>>,
};
@group(#{MATERIAL_BIND_GROUP}) @binding(90) var<storage, read> wow_light: WowLight;

struct CasterVertex {
    @builtin(instance_index) instance_index: u32,
    @location(0) position: vec3<f32>,
#ifdef VERTEX_UVS_A
    @location(1) uv: vec2<f32>,
#endif
#ifdef WOW_RIG_SKIN
    @location(10) joint_indices: vec4<u32>,
    @location(11) joint_weights: vec4<f32>,
#endif
#ifdef WOW_MERGED_FADE
    @location(12) fade_sphere: vec4<f32>,
#endif
}

struct CasterOut {
    @builtin(position) position: vec4<f32>,
#ifdef VERTEX_UVS_A
    @location(0) uv: vec2<f32>,
#endif
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    @location(1) unclipped_depth: f32,
#endif
}

#ifdef WOW_RIG_SKIN
fn wow_rig_slot(instance_index: u32) -> u32 {
    return (mesh_functions::get_tag(instance_index) >> 19u) & 0x7ffu;
}

// `wow_model.wgsl`'s palette blend, verbatim.
fn wow_skin_model(instance_index: u32, indices: vec4<u32>, weights: vec4<f32>) -> mat4x4<f32> {
    let base = wow_light.rig_table[wow_rig_slot(instance_index)];
    let b0 = 3u * (base + indices.x);
    let b1 = 3u * (base + indices.y);
    let b2 = 3u * (base + indices.z);
    let b3 = 3u * (base + indices.w);
    let r0 = weights.x * wow_light.palettes[b0]
        + weights.y * wow_light.palettes[b1]
        + weights.z * wow_light.palettes[b2]
        + weights.w * wow_light.palettes[b3];
    let r1 = weights.x * wow_light.palettes[b0 + 1u]
        + weights.y * wow_light.palettes[b1 + 1u]
        + weights.z * wow_light.palettes[b2 + 1u]
        + weights.w * wow_light.palettes[b3 + 1u];
    let r2 = weights.x * wow_light.palettes[b0 + 2u]
        + weights.y * wow_light.palettes[b1 + 2u]
        + weights.z * wow_light.palettes[b2 + 2u]
        + weights.w * wow_light.palettes[b3 + 2u];
    return mat4x4<f32>(
        vec4<f32>(r0.x, r1.x, r2.x, 0.0),
        vec4<f32>(r0.y, r1.y, r2.y, 0.0),
        vec4<f32>(r0.z, r1.z, r2.z, 0.0),
        vec4<f32>(r0.w, r1.w, r2.w, 1.0),
    );
}
#endif

// The batch marker bits (`clutter_fade.z`, materials.rs `WowModelKey`) that cast nothing: no
// depth write (0x1), no depth test (0x2), additive (0x4), Mod (0x80), Mod2x (0x100), the
// depth-prime twin (0x200), the far-side waterline copy (0x800) and the sky lane (0x2000).
const NO_CAST_MARKERS: u32 = 0x2b87u;

// Whether this draw leaves no shadow at all; its triangles then collapse to one point.
fn casts_nothing(instance_index: u32) -> bool {
    let markers = u32(m.clutter_fade.z);
    if ((markers & NO_CAST_MARKERS) != 0u) {
        return true;
    }
    // Ground clutter (its overdraw for a blade's shadow), the UI tile lane, unlit M2 batches
    // (light cones, glows; a WMO's unlit exterior batch is a wall and casts).
    if (m.clutter_fade.w > 0.5 || m.anim_slots.w > 0.5) {
        return true;
    }
    if (m.model_flags.w > 0.5 && m.model_flags.x < 0.5) {
        return true;
    }
    // A model the fades have taken below half alpha (the MeshTag's six fade bits, 0 = opaque).
    let fade_tag = mesh_functions::get_tag(instance_index) & 0x3fffffffu;
    let alpha6 = f32(fade_tag & 0x3fu) / 63.0;
    return fade_tag != 0u && alpha6 < 0.5;
}

@vertex
fn vertex(vertex: CasterVertex) -> CasterOut {
    var out: CasterOut;
#ifdef VERTEX_UVS_A
    out.uv = vertex.uv;
#endif
    if (casts_nothing(vertex.instance_index)) {
        // One point for every vertex: zero-area triangles, which rasterize nothing even with depth
        // clipping off.
        out.position = vec4<f32>(4.0, 4.0, 0.5, 1.0);
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
        out.unclipped_depth = 0.5;
#endif
        return out;
    }
    let mesh_world_from_local = mesh_functions::get_world_from_local(vertex.instance_index);
#ifdef WOW_RIG_SKIN
    let frame_from_local = wow_skin_model(
        vertex.instance_index,
        vertex.joint_indices,
        vertex.joint_weights
    );
    let frame_origin = wow_light.rig_origin[wow_rig_slot(vertex.instance_index)].xyz;
#else
    var frame_from_local = mesh_world_from_local;
    let frame_origin = mesh_world_from_local[3].xyz;
    frame_from_local[3] = vec4<f32>(0.0, 0.0, 0.0, 1.0);
#endif
    // Camera-relative, as the colour pass: no f32 product mixes the world offset and the rotation.
    let p_cam = (frame_from_local * vec4<f32>(vertex.position, 1.0)).xyz
        + (frame_origin - view.world_position);
    let view_rot = mat3x3<f32>(
        view.view_from_world[0].xyz,
        view.view_from_world[1].xyz,
        view.view_from_world[2].xyz,
    );
    out.position = view.clip_from_view * vec4<f32>(view_rot * p_cam, 1.0);
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    out.unclipped_depth = out.position.z;
    out.position.z = min(out.position.z, 1.0);
#endif
    return out;
}

// The cutout: the texel alpha against the material's mask reference, or one half for a blended
// batch (a fade twin, a translucent leaf), so a card casts its painted shape.
fn caster_discard(in: CasterOut) {
#ifdef VERTEX_UVS_A
    let mat = pbr_bindings::material;
    let opaque = (mat.flags & pbr_types::STANDARD_MATERIAL_FLAGS_ALPHA_MODE_RESERVED_BITS)
        == pbr_types::STANDARD_MATERIAL_FLAGS_ALPHA_MODE_OPAQUE;
    // An opaque batch's texel alpha is not coverage (a BLP may carry garbage there).
    if (!opaque && (mat.flags & pbr_types::STANDARD_MATERIAL_FLAGS_BASE_COLOR_TEXTURE_BIT) != 0u) {
        let a = textureSample(
            pbr_bindings::base_color_texture,
            pbr_bindings::base_color_sampler,
            in.uv,
        ).a * mat.base_color.a;
        let mode = mat.flags & pbr_types::STANDARD_MATERIAL_FLAGS_ALPHA_MODE_RESERVED_BITS;
        let cutoff = select(0.5, mat.alpha_cutoff, mode == pbr_types::STANDARD_MATERIAL_FLAGS_ALPHA_MODE_MASK);
        if (a < cutoff) {
            discard;
        }
    }
#endif
}

#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
@fragment
fn fragment(in: CasterOut) -> @builtin(frag_depth) f32 {
    caster_discard(in);
    return in.unclipped_depth;
}
#else
@fragment
fn fragment(in: CasterOut) {
    caster_discard(in);
}
#endif
