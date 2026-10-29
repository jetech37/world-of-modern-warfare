//! The modern atmosphere: realtime sun shadows, volumetric fog with light shafts, per-pixel lamps
//! and wind-swayed ground clutter, each off by default.
//!
//! Deviation, opt-in: 1.12 has none of these. Its sun shading is per-vertex `N·L` with baked MCSH
//! terrain shadows and a blob under each unit, its fog is one linear `Light.dbc` band, its point
//! lights are the three nearest per draw, per vertex, and its clutter is static. Every feature here
//! is a [`Quality`] knob in [`AtmosphereSettings`], each `Off` by default, so a fresh benilla
//! renders the stock look; the `atmo*` CVars (`benilla-app`) carry the knobs and their `Ours` rows.
//!
//! Everything still lights by the reference's own state: the shadow sun travels along
//! [`WowLighting::sun_dir`] (the `DayNight` lighting sun, which never sets), the shafts take the
//! scene diffuse and the zone's fog colour, and the lamps are the authored M2 light blocks and WMO
//! MOLT omnis already in the point table. The shaders read the per-frame lanes this module packs
//! into the shared light buffer (`lighting::global_light`):
//! - `fog_params.z`: the sun-shadow strength, 0 off;
//! - `wmo_fog_params.zw`: the wind strength and its clock;
//! - `point_count.y`: the per-pixel lamp count, 0 for the reference's per-vertex three.

use bevy::camera::visibility::RenderLayers;
use bevy::light::{
    CascadeShadowConfig, CascadeShadowConfigBuilder, DirectionalLightShadowMap, FogVolume,
    NotShadowCaster, ShadowFilteringMethod, VolumetricFog, VolumetricLight,
};
use bevy::pbr::StandardMaterial;
use bevy::prelude::*;
use bevy::render::extract_resource::ExtractResource;

use crate::lighting::{LightingConsumeSet, WowLighting};
use crate::view::WorldCamera;

/// One feature's level. `Off` is the reference's look; the rest trade frame time for quality.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum Quality {
    #[default]
    Off,
    Low,
    Medium,
    High,
}

impl Quality {
    /// The CVar's integer, clamped: `0` off to `3` high.
    pub fn from_level(level: f32) -> Self {
        match level.round() as i32 {
            i32::MIN..=0 => Self::Off,
            1 => Self::Low,
            2 => Self::Medium,
            _ => Self::High,
        }
    }

    /// The CVar's integer.
    pub fn level(self) -> u8 {
        self as u8
    }

    pub fn is_on(self) -> bool {
        self != Self::Off
    }
}

/// The atmosphere's knobs, written by the `atmo*` CVars. `Default` is all `Off`, the 1.12 look
/// ("Classic"); [`AtmosphereSettings::MODERN`] is the preset a layer may ship instead.
#[derive(Resource, Clone, Copy, Debug, Default, PartialEq, Eq, ExtractResource)]
pub struct AtmosphereSettings {
    /// Cascaded shadow maps from the lighting sun, cast and received by terrain, WMOs, doodads and
    /// units.
    pub shadows: Quality,
    /// Volumetric fog in the zone's fog colour, with light shafts through the shadow maps.
    pub fog: Quality,
    /// Per-pixel lamps: the nearest authored lights, outdoor ones brightest at night.
    pub lamps: Quality,
    /// Wind sway on ground clutter.
    pub wind: Quality,
}

impl AtmosphereSettings {
    /// The "Modern" preset: everything on at a level a mid-range GPU holds 60 fps at.
    pub const MODERN: Self = Self {
        shadows: Quality::Medium,
        fog: Quality::Low,
        lamps: Quality::Medium,
        wind: Quality::Medium,
    };

    /// The "Classic" preset, the reference's look: everything off.
    pub const CLASSIC: Self = Self {
        shadows: Quality::Off,
        fog: Quality::Off,
        lamps: Quality::Off,
        wind: Quality::Off,
    };

    /// The shadow maps exist: for the surfaces' shadows, or for the fog's shafts, which sample them.
    pub fn shadow_maps(&self) -> Option<Quality> {
        match (self.shadows, self.fog) {
            (Quality::Off, Quality::Off) => None,
            (Quality::Off, _) => Some(Quality::Low),
            (s, _) => Some(s),
        }
    }

    /// The surfaces' shadow strength, the `fog_params.z` lane: 0 leaves the reference's shading.
    pub fn shadow_strength(&self) -> f32 {
        if self.shadows.is_on() {
            SHADOW_STRENGTH
        } else {
            0.0
        }
    }

    /// The per-pixel lamp count, the `point_count.y` lane: the nearest this many packed lights
    /// light every exterior pixel; 0 keeps the reference's per-vertex three.
    pub fn lamp_count(&self) -> u32 {
        match self.lamps {
            Quality::Off => 0,
            Quality::Low => 32,
            Quality::Medium => 64,
            Quality::High => 128,
        }
    }

    /// The clutter sway amplitude, the `wmo_fog_params.z` lane, in yards of lean per yard of blade.
    pub fn wind_strength(&self) -> f32 {
        match self.wind {
            Quality::Off => 0.0,
            Quality::Low => 0.08,
            Quality::Medium => 0.14,
            Quality::High => 0.2,
        }
    }
}

/// How much of the sun's diffuse a fully shadowed texel loses: all of it, so a shadow reads as the
/// ambient-only side the reference's own shading already shows a model facing away from the sun.
pub const SHADOW_STRENGTH: f32 = 1.0;

/// The shadow sun's illuminance: the reciprocal of the default camera exposure (EV100 9.7), so
/// the volumetric shafts, the one Bevy pass that reads the light's colour, see the scene diffuse
/// at unit gain.
const SUN_LUX: f32 = 998.0;

/// The shadow sun, a Bevy `DirectionalLight` along the lighting sun. The world's shaders do their
/// own lighting, so it lights nothing itself: it exists for its shadow maps and the shafts.
#[derive(Component)]
pub struct WowSun;

/// The fog volume that follows the world camera.
#[derive(Component)]
pub struct WowFogVolume;

/// The fog box around the camera (yd): wide enough that the shafts reach past the shadow
/// distance, low enough that the raymarch spends its steps near the ground.
const FOG_BOX: Vec3 = Vec3::new(360.0, 140.0, 360.0);

/// The sun's cascades per quality: `(cascades, first far bound, max distance, map size)`. A map is
/// one `Depth32Float` layer per cascade: Low 2 × 1024² (8 MB), Medium 3 × 2048² (48 MB), High
/// 4 × 2048² (64 MB).
fn cascade_plan(q: Quality) -> (usize, f32, f32, usize) {
    match q {
        Quality::Off | Quality::Low => (2, 24.0, 90.0, 1024),
        Quality::Medium => (3, 20.0, 150.0, 2048),
        Quality::High => (4, 16.0, 240.0, 2048),
    }
}

/// The cascade config for a quality. The lighting sun stays 20°-37° above the horizon, so
/// shadows are long and the near cascade carries most of what the player sees.
pub fn cascade_config(q: Quality) -> CascadeShadowConfig {
    let (num_cascades, first, max, _) = cascade_plan(q);
    CascadeShadowConfigBuilder {
        num_cascades,
        minimum_distance: 0.1,
        maximum_distance: max,
        first_cascade_far_bound: first,
        overlap_proportion: 0.2,
    }
    .build()
}

/// The shadow map size for a quality.
pub fn shadow_map_size(q: Quality) -> usize {
    cascade_plan(q).3
}

/// The PCF the surfaces' samples use: the hardware 2×2 at Low, Bevy's 3×3 Gaussian above.
fn filter_for(q: Quality) -> ShadowFilteringMethod {
    match q {
        Quality::Off | Quality::Low => ShadowFilteringMethod::Hardware2x2,
        _ => ShadowFilteringMethod::Gaussian,
    }
}

/// The fog per quality: `(raymarch steps, density, shaft gain)`.
fn fog_plan(q: Quality) -> (u32, f32, f32) {
    match q {
        Quality::Off | Quality::Low => (24, 0.0016, 3.0),
        Quality::Medium => (40, 0.0022, 3.5),
        Quality::High => (64, 0.0028, 4.0),
    }
}

/// Depth bias along the light (yd) and normal bias (shadow texels). The lit side is the only side
/// that shows a shadow (the diffuse is already 0 facing away), so acne shows only on sun-facing
/// slopes; this pair clears it on terrain at the sun's lowest 20° without lifting a unit's shadow
/// off its feet.
const DEPTH_BIAS: f32 = 0.06;
const NORMAL_BIAS: f32 = 2.2;

/// The atmosphere systems.
pub(crate) struct AtmospherePlugin;

impl Plugin for AtmospherePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<AtmosphereSettings>()
            .add_observer(no_plain_material_shadows)
            // After the stream stage, whose exclusive collider attach would otherwise run in either
            // order against it.
            .add_systems(
                Update,
                drive_atmosphere
                    .in_set(LightingConsumeSet)
                    .after(crate::schedule::WorldStage::Stream),
            );
    }
}

/// Plain `StandardMaterial` meshes are the program's own effects and overlays (nameplates, the
/// layer's view arms and tracers), all unlit: none casts a sun shadow.
fn no_plain_material_shadows(
    add: On<Add, MeshMaterial3d<StandardMaterial>>,
    mut commands: Commands,
) {
    commands.entity(add.entity).try_insert(NotShadowCaster);
}

/// The sun's travel direction for the shadow maps: the lighting sun, never parallel to up.
fn shadow_transform(sun_dir: Vec3) -> Transform {
    let dir = if sun_dir.length_squared() > 1e-6 {
        sun_dir.normalize()
    } else {
        Vec3::new(0.5, -0.6, 0.5).normalize()
    };
    Transform::IDENTITY.looking_to(dir, Vec3::Y)
}

/// The linear colour Bevy's light carries for a gamma-space WoW colour: the world composes in
/// gamma, so the raw values pass through.
fn raw_color(rgb: [f32; 3]) -> Color {
    Color::LinearRgba(LinearRgba::rgb(rgb[0], rgb[1], rgb[2]))
}

/// The world camera as the atmosphere reads it.
type AtmosphereCamera<'w, 's> = Query<
    'w,
    's,
    (
        Entity,
        Option<&'static ShadowFilteringMethod>,
        Option<&'static VolumetricFog>,
    ),
    With<WorldCamera>,
>;

/// The atmosphere's one system: the shadow sun ([`drive_sun`]), then the fog ([`drive_fog`]).
/// Every write goes through `Commands` and nothing here touches `Transform`, so it shares no
/// component with the frame's other writers.
#[allow(clippy::type_complexity)]
fn drive_atmosphere(
    mut commands: Commands,
    settings: Res<AtmosphereSettings>,
    light: Res<WowLighting>,
    sun: Query<(Entity, &DirectionalLight, Has<VolumetricLight>), With<WowSun>>,
    volume: Query<(Entity, &FogVolume, Option<&ChildOf>), With<WowFogVolume>>,
    mut map: ResMut<DirectionalLightShadowMap>,
    cams: AtmosphereCamera,
    mut planned: Local<Option<(Quality, Transform)>>,
) {
    drive_sun(
        &mut commands,
        &settings,
        &light,
        &sun,
        &mut map,
        &cams,
        &mut planned,
    );
    drive_fog(&mut commands, &settings, &light, &cams, &volume);
}

/// Spawn, steer and retire the shadow sun, and keep the maps and the camera's filter at quality.
/// The sun's pose is remembered in `planned` rather than read back.
fn drive_sun(
    commands: &mut Commands,
    settings: &AtmosphereSettings,
    light: &WowLighting,
    sun: &Query<(Entity, &DirectionalLight, Has<VolumetricLight>), With<WowSun>>,
    map: &mut DirectionalLightShadowMap,
    cams: &AtmosphereCamera,
    planned: &mut Option<(Quality, Transform)>,
) {
    let Some(q) = settings.shadow_maps() else {
        for (e, ..) in sun {
            commands.entity(e).despawn();
        }
        *planned = None;
        return;
    };
    let transform = shadow_transform(light.sun_dir);
    let color = raw_color(light.diffuse);
    let volumetric = settings.fog.is_on();
    match sun.single() {
        Ok((e, dl, has_vol)) => {
            let mut sun = commands.entity(e);
            if planned.is_none_or(|(_, t)| t != transform) {
                sun.insert(transform);
            }
            if dl.color != color {
                sun.insert(DirectionalLight { color, ..*dl });
            }
            if planned.is_none_or(|(pq, _)| pq != q) {
                sun.insert(cascade_config(q));
            }
            if volumetric != has_vol {
                if volumetric {
                    sun.insert(VolumetricLight);
                } else {
                    sun.remove::<VolumetricLight>();
                }
            }
        }
        Err(_) => {
            let mut e = commands.spawn((
                WowSun,
                Name::new("wow sun (shadows)"),
                DirectionalLight {
                    color,
                    illuminance: SUN_LUX,
                    shadows_enabled: true,
                    shadow_depth_bias: DEPTH_BIAS,
                    shadow_normal_bias: NORMAL_BIAS,
                    ..default()
                },
                cascade_config(q),
                transform,
                // The world's layer only: the portrait booths draw on their own layers and take
                // neither its maps nor its light.
                RenderLayers::layer(0),
            ));
            if volumetric {
                e.insert(VolumetricLight);
            }
        }
    }
    let size = shadow_map_size(q);
    if map.size != size {
        map.size = size;
    }
    let filter = filter_for(settings.shadows);
    for (cam, current, _) in cams {
        if current != Some(&filter) {
            commands.entity(cam).insert(filter);
        }
    }
    *planned = Some((q, transform));
}

/// Keep the camera's volumetric fog and its volume at quality, tinted by the zone's fog colour.
/// The volume rides the camera as a child, so it follows with no per-frame write; a uniform box
/// that large shows no turn of the camera.
#[allow(clippy::type_complexity)]
fn drive_fog(
    commands: &mut Commands,
    settings: &AtmosphereSettings,
    light: &WowLighting,
    cams: &AtmosphereCamera,
    volume: &Query<(Entity, &FogVolume, Option<&ChildOf>), With<WowFogVolume>>,
) {
    let q = settings.fog;
    if !q.is_on() {
        for (cam, _, fog) in cams {
            if fog.is_some() {
                commands.entity(cam).remove::<VolumetricFog>();
            }
        }
        for (e, ..) in volume {
            commands.entity(e).despawn();
        }
        return;
    }
    let (steps, density, gain) = fog_plan(q);
    let Some((cam, _, current)) = cams.iter().next() else {
        return;
    };
    if current.is_none_or(|f| f.step_count != steps) {
        commands.entity(cam).insert(VolumetricFog {
            // The ambient lane is a flat add; the zone's tint rides the scattered sunlight instead.
            ambient_color: Color::WHITE,
            ambient_intensity: 0.0,
            jitter: 0.0,
            step_count: steps,
        });
    }
    // The zone's fog hue at full value, so a dark night fog does not double-darken the shafts,
    // which the scene diffuse already dims.
    let fc = light.fog_color;
    let peak = fc[0].max(fc[1]).max(fc[2]).max(1e-3);
    let fog = FogVolume {
        fog_color: raw_color([fc[0] / peak, fc[1] / peak, fc[2] / peak]),
        density_factor: density,
        density_texture: None,
        density_texture_offset: Vec3::ZERO,
        absorption: 0.08,
        scattering: 0.35,
        scattering_asymmetry: 0.65,
        light_tint: Color::WHITE,
        light_intensity: gain,
    };
    match volume.single() {
        // A volume left on a camera that respawned is replaced below.
        Ok((e, fv, parent)) if parent.is_some_and(|p| p.parent() == cam) => {
            // `FogVolume` has no `PartialEq`; these three are the only fields that move.
            if fv.fog_color != fog.fog_color
                || fv.density_factor != fog.density_factor
                || fv.light_intensity != fog.light_intensity
            {
                commands.entity(e).insert(fog);
            }
        }
        other => {
            if let Ok((e, ..)) = other {
                commands.entity(e).despawn();
            }
            commands.spawn((
                WowFogVolume,
                Name::new("wow fog volume"),
                fog,
                Transform::from_scale(FOG_BOX),
                RenderLayers::layer(0),
                ChildOf(cam),
            ));
        }
    }
}

#[cfg(test)]
mod wgsl_check;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_default_is_the_reference_look() {
        let s = AtmosphereSettings::default();
        assert_eq!(s, AtmosphereSettings::CLASSIC);
        assert_eq!(s.shadow_maps(), None, "no shadow maps, no shadow passes");
        assert_eq!(s.shadow_strength(), 0.0);
        assert_eq!(s.lamp_count(), 0, "the reference's per-vertex three");
        assert_eq!(s.wind_strength(), 0.0);
    }

    #[test]
    fn fog_alone_keeps_low_maps_for_its_shafts_and_no_surface_shadows() {
        let s = AtmosphereSettings {
            fog: Quality::High,
            ..default()
        };
        assert_eq!(s.shadow_maps(), Some(Quality::Low));
        assert_eq!(s.shadow_strength(), 0.0);
    }

    #[test]
    fn quality_round_trips_through_the_cvar_integer() {
        for q in [Quality::Off, Quality::Low, Quality::Medium, Quality::High] {
            assert_eq!(Quality::from_level(f32::from(q.level())), q);
        }
        assert_eq!(Quality::from_level(-4.0), Quality::Off);
        assert_eq!(Quality::from_level(9.0), Quality::High);
    }

    #[test]
    fn cascades_and_maps_grow_with_quality() {
        let mut last = (0usize, 0.0f32);
        for q in [Quality::Low, Quality::Medium, Quality::High] {
            let (n, _, max, size) = cascade_plan(q);
            assert!(n > last.0 || max > last.1, "{q:?} adds reach or cascades");
            assert!(size.is_power_of_two(), "Bevy wants a power-of-two map");
            // One Depth32Float layer per cascade: stay under 64 MB at every level.
            assert!(n * size * size * 4 <= 64 << 20, "{q:?} map budget");
            last = (n, max);
        }
    }

    #[test]
    fn the_shadow_sun_points_along_the_lighting_sun() {
        let dir = Vec3::new(0.4, -0.5, 0.3).normalize();
        let tf = shadow_transform(dir);
        assert!(tf.forward().as_vec3().dot(dir) > 0.999);
    }
}
