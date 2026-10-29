//! The thermal sight's heat, drawn from the bodies themselves: a second camera, following the
//! world camera, renders only the creatures and players in sight (their parts are put on its
//! render layer while the sight is up) into a picture, and a UI material over the scope's dark
//! tint turns that picture's coverage into a white-hot outline around a faint warm fill (MW2's
//! white-hot thermal). A wall between hides the heat: only parts the eye can see are layered.

use bevy::asset::RenderAssetUsages;
use bevy::camera::visibility::RenderLayers;
use bevy::camera::{ClearColorConfig, RenderTarget};
use bevy::core_pipeline::tonemapping::Tonemapping;
use bevy::platform::collections::HashSet;
use bevy::prelude::*;
use bevy::render::render_resource::{
    AsBindGroup, Extent3d, TextureDimension, TextureFormat, TextureUsages,
};
use bevy::shader::ShaderRef;
use bevy::ui_render::ui_material::UiMaterial;
use bevy::window::PrimaryWindow;

use benilla_app::Objects;
use benilla_world::interact::{CreaturePickPart, WorldPick};
use benilla_world::view::WorldCamera;
use bevy::camera::primitives::Aabb;

use crate::Mw2Set;
use crate::mode::FpsState;
use crate::weapon::Weapon;

/// The render layer the thermal camera sees: unused by benilla (its booths and the minimap
/// composite take the low ones).
pub const THERMAL_LAYER: usize = 27;

/// Thermal sees this far, in yards.
pub const HEAT_RANGE: f32 = 120.0;

/// The heat overlay's material: the thermal camera's picture, outlined.
#[derive(Asset, TypePath, AsBindGroup, Clone)]
pub struct ThermalMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub mask: Handle<Image>,
    /// xy one texel in uv, z the fill's alpha, w the outline's width in texels.
    #[uniform(2)]
    pub params: Vec4,
}

impl UiMaterial for ThermalMaterial {
    fn fragment_shader() -> ShaderRef {
        "embedded://benilla_mw2/thermal.wgsl".into()
    }
}

/// The picture, the material over the scope, and the camera once the world camera exists.
#[derive(Resource)]
pub struct ThermalRig {
    pub image: Handle<Image>,
    pub material: Handle<ThermalMaterial>,
    camera: Option<Entity>,
    size: UVec2,
}

/// The thermal camera.
#[derive(Component)]
struct ThermalCam;

/// A body part put on the thermal layer.
#[derive(Component)]
struct Heated;

/// The fill under the outline, and the outline's width in texels.
const FILL_ALPHA: f32 = 0.35;
const OUTLINE_TEXELS: f32 = 2.0;

pub struct ThermalPlugin;

impl Plugin for ThermalPlugin {
    fn build(&self, app: &mut App) {
        // The shader and its material need the asset system (absent in the headless test app).
        if app
            .world()
            .contains_resource::<bevy::asset::io::embedded::EmbeddedAssetRegistry>()
        {
            bevy::asset::embedded_asset!(app, "thermal.wgsl");
            app.add_plugins(bevy::ui_render::UiMaterialPlugin::<ThermalMaterial>::default());
        }
        app.add_systems(PreStartup, setup_rig).add_systems(
            Update,
            (drive_camera, heat_layers).chain().in_set(Mw2Set::Hud),
        );
    }
}

/// A transparent picture `size` big the thermal camera can render into and the UI can sample.
fn target_image(size: UVec2) -> Image {
    let mut image = Image::new_fill(
        Extent3d {
            width: size.x.max(1),
            height: size.y.max(1),
            depth_or_array_layers: 1,
        },
        TextureDimension::D2,
        &[0; 4],
        TextureFormat::Rgba8UnormSrgb,
        RenderAssetUsages::default(),
    );
    image.texture_descriptor.usage =
        TextureUsages::TEXTURE_BINDING | TextureUsages::COPY_DST | TextureUsages::RENDER_ATTACHMENT;
    image
}

/// The material's parameters for a picture `size` big.
pub fn params_for(size: UVec2) -> Vec4 {
    Vec4::new(
        1.0 / size.x.max(1) as f32,
        1.0 / size.y.max(1) as f32,
        FILL_ALPHA,
        OUTLINE_TEXELS,
    )
}

fn setup_rig(
    mut commands: Commands,
    mut images: ResMut<Assets<Image>>,
    mut materials: ResMut<Assets<ThermalMaterial>>,
) {
    let size = UVec2::new(1280, 720);
    let image = images.add(target_image(size));
    let material = materials.add(ThermalMaterial {
        mask: image.clone(),
        params: params_for(size),
    });
    commands.insert_resource(ThermalRig {
        image,
        material,
        camera: None,
        size,
    });
}

/// Whether the thermal sight is up (fully aimed, MW2 mode on).
fn thermal_up(state: &FpsState, weapon: &Weapon) -> bool {
    let thermal = weapon.scope.as_deref().is_some_and(|s| s.thermal);
    state.active && thermal && crate::scope::scoped(true, weapon.ads)
}

/// Seat the thermal camera on the world camera, match its lens and the window, and run it only
/// while the sight is up.
#[allow(clippy::type_complexity)]
fn drive_camera(
    mut commands: Commands,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    mut rig: ResMut<ThermalRig>,
    mut images: ResMut<Assets<Image>>,
    mut materials: ResMut<Assets<ThermalMaterial>>,
    windows: Query<&Window, With<PrimaryWindow>>,
    world: Query<(Entity, &Projection), (With<WorldCamera>, Without<ThermalCam>)>,
    mut cams: Query<(&mut Camera, &mut Projection), With<ThermalCam>>,
) {
    let Ok((world_cam, world_proj)) = world.single() else {
        return;
    };
    let up = thermal_up(&state, &weapon);
    // The picture follows the window.
    if let Ok(window) = windows.single() {
        let size = UVec2::new(window.physical_width(), window.physical_height());
        if size != rig.size && size.x > 0 && size.y > 0 {
            rig.size = size;
            if let Some(img) = images.get_mut(&rig.image) {
                img.resize(Extent3d {
                    width: size.x,
                    height: size.y,
                    depth_or_array_layers: 1,
                });
            }
            if let Some(m) = materials.get_mut(&rig.material) {
                m.params = params_for(size);
            }
        }
    }
    let Some(cam) = rig.camera else {
        let cam = commands
            .spawn((
                Name::new("mw2 thermal camera"),
                ThermalCam,
                Camera3d::default(),
                Camera {
                    is_active: false,
                    order: -1,
                    clear_color: ClearColorConfig::Custom(Color::NONE),
                    ..default()
                },
                RenderTarget::Image(rig.image.clone().into()),
                RenderLayers::layer(THERMAL_LAYER),
                Msaa::Off,
                Tonemapping::None,
                world_proj.clone(),
                Transform::IDENTITY,
                ChildOf(world_cam),
            ))
            .id();
        rig.camera = Some(cam);
        return;
    };
    if let Ok((mut camera, mut proj)) = cams.get_mut(cam) {
        if camera.is_active != up {
            camera.is_active = up;
        }
        // The scope's zoom narrows the world camera's lens; the heat must line up with it.
        if up {
            *proj = world_proj.clone();
        }
    }
}

/// Put every living body part in sight on the thermal layer while the sight is up, and take them
/// off as they leave sight or the sight comes down.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn heat_layers(
    mut commands: Commands,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    objects: Objects,
    world: WorldPick,
    cameras: Query<&Transform, With<WorldCamera>>,
    parts: Query<
        (
            Entity,
            &Aabb,
            &GlobalTransform,
            Option<&InheritedVisibility>,
            Has<Heated>,
        ),
        With<CreaturePickPart>,
    >,
    parents: Query<&ChildOf>,
) {
    let up = thermal_up(&state, &weapon);
    let cam = cameras.single().ok().copied();
    let units = if up {
        crate::aim::living_units(&objects, state.self_guid)
    } else {
        Default::default()
    };
    for (part, aabb, gt, vis, heated) in &parts {
        let hot = up
            && cam.is_some_and(|cam| {
                if vis.is_some_and(|v| !v.get())
                    || crate::aim::unit_of(part, &parents, &units).is_none()
                {
                    return false;
                }
                let center = gt.transform_point(Vec3::from(aabb.center));
                in_sight(&world, cam.translation, *cam.forward(), center)
            });
        if hot && !heated {
            commands
                .entity(part)
                .insert((Heated, RenderLayers::from_layers(&[0, THERMAL_LAYER])));
        } else if !hot && heated {
            commands.entity(part).remove::<(Heated, RenderLayers)>();
        }
    }
}

/// Whether a body at `target` shows in the thermal: ahead, in range, no wall between.
fn in_sight(world: &WorldPick, eye: Vec3, forward: Vec3, target: Vec3) -> bool {
    let to = target - eye;
    let distance = to.length();
    if !heat_reaches(distance, to.dot(forward)) {
        return false;
    }
    let ray = Ray3d::new(eye, Dir3::new(to).unwrap_or(Dir3::NEG_Z));
    !world
        .cast(ray, &HashSet::new(), false)
        .into_iter()
        .any(|h| h.hit.distance + 0.3 < distance)
}

/// The thermal's reach: in range and ahead of the eye (`ahead` the target's distance along the
/// view).
pub fn heat_reaches(distance: f32, ahead: f32) -> bool {
    (0.5..=HEAT_RANGE).contains(&distance) && ahead > 0.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn heat_reaches_bodies_ahead_and_in_range() {
        assert!(heat_reaches(30.0, 25.0));
        assert!(!heat_reaches(30.0, -5.0)); // behind
        assert!(!heat_reaches(HEAT_RANGE + 1.0, 100.0));
        assert!(!heat_reaches(0.2, 0.2)); // inside the eye
    }

    #[test]
    fn the_outline_is_measured_in_the_pictures_texels() {
        let p = params_for(UVec2::new(1920, 1080));
        assert!((p.x - 1.0 / 1920.0).abs() < 1e-9 && (p.y - 1.0 / 1080.0).abs() < 1e-9);
        assert_eq!((p.z, p.w), (FILL_ALPHA, OUTLINE_TEXELS));
    }

    #[test]
    fn the_thermal_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((drive_camera, heat_layers));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
}
