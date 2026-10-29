//! The heartbeat sensor attachment, as MW2 has it: its screen on the gun itself shows enemies
//! ahead within reach, swept fresh with each pulse: our place at the bottom middle, forward up, a
//! red blip for each hostile body, fading until the next sweep. The screen is a small texture
//! redrawn here and held on the sensor's bone (or by the gun's frame if the model names none),
//! turned to the eye.

use bevy::asset::RenderAssetUsages;
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};

use benilla_app::{ObjectType, Objects};
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::config::Loadout;
use crate::mode::FpsState;
use crate::mw2::viewmodel::Viewmodel;

/// The sensor's reach (yards), its pulse (seconds), and the blips it shows at most.
pub const HEARTBEAT_RANGE: f32 = 40.0;
pub const HEARTBEAT_PULSE: f32 = 1.0;
const HEARTBEAT_DOTS: usize = 24;

/// The screen's pixels and its size on the gun (MW2 inches).
const SCREEN_W: u32 = 128;
const SCREEN_H: u32 = 84;
const SCREEN_INCHES: Vec2 = Vec2::new(3.4, 2.25);

/// The gun's own sensor screen (MW2's `mtl_motion_tracker_screen`, on the M4's viewmodel) shows
/// only this window of its texture (u0, v0, u1, v1, read from the model: examples/mw2_screen_uv),
/// so its radar is painted there, on a texture of its own.
const WORN_SIZE: (u32, u32) = (256, 128);
const WORN_VIEW: [f32; 4] = [0.038, 0.076, 0.504, 0.876];

#[derive(Resource)]
struct Screen {
    image: Handle<Image>,
    material: Handle<StandardMaterial>,
    worn_image: Handle<Image>,
    worn_material: Handle<StandardMaterial>,
    quad: Handle<Mesh>,
    entity: Option<Entity>,
    blips: Vec<Vec2>,
    swept_at: f32,
}

#[derive(Component)]
struct ScreenQuad;

/// Whether the gun in hand carries the sensor (`m4_heartbeat_mp`, `ak47_heartbeat_fmj_mp`).
pub fn has_heartbeat(loadout: &Loadout) -> bool {
    // A test probe's gun in hand counts too (MW2_PROBE_GUN, crate::mw2).
    let probe = std::env::var("MW2_PROBE_GUN").is_ok_and(|g| g.contains("heartbeat"));
    probe
        || loadout.class_gun.as_ref().is_some_and(|h| {
            // A third or fourth attachment (Tinker's Rails) is not in the definition held.
            h.base == loadout.mw2_weapon
                && (h.variant.contains("heartbeat")
                    || h.attachments.iter().any(|a| a == "heartbeat"))
        })
}

/// Where a body shows on the screen, as a share of it (x 0 left to 1 right, y 0 top to 1 at our
/// feet), from its offset to us (`right`, `forward` yards): `None` behind us or out of range.
pub fn heartbeat_point(right: f32, forward: f32) -> Option<Vec2> {
    if forward < 0.0 || right.hypot(forward) > HEARTBEAT_RANGE {
        return None;
    }
    Some(Vec2::new(
        0.5 + right / (2.0 * HEARTBEAT_RANGE),
        1.0 - forward / HEARTBEAT_RANGE,
    ))
}

pub struct HeartbeatPlugin;

impl Plugin for HeartbeatPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, setup).add_systems(
            Update,
            (sense, draw, wear_screen, hold).chain().in_set(Mw2Set::Hud),
        );
    }
}

/// The sensor's own screen on the gun (MW2's `mtl_motion_tracker_screen`) shows the radar.
fn wear_screen(
    screen: Option<Res<Screen>>,
    mut surfaces: Query<
        &mut MeshMaterial3d<StandardMaterial>,
        With<crate::mw2::viewmodel::MotionTrackerScreen>,
    >,
) {
    let Some(screen) = screen else {
        return;
    };
    for mut m in &mut surfaces {
        if m.0 != screen.worn_material {
            m.0 = screen.worn_material.clone();
        }
    }
}

fn setup(
    mut commands: Commands,
    images: Option<ResMut<Assets<Image>>>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
    meshes: Option<ResMut<Assets<Mesh>>>,
) {
    let (Some(mut images), Some(mut materials), Some(mut meshes)) = (images, materials, meshes)
    else {
        return;
    };
    let image = images.add(Image::new_fill(
        Extent3d {
            width: SCREEN_W,
            height: SCREEN_H,
            depth_or_array_layers: 1,
        },
        TextureDimension::D2,
        &[10, 34, 20, 255],
        TextureFormat::Rgba8UnormSrgb,
        RenderAssetUsages::default(),
    ));
    let worn_image = images.add(Image::new_fill(
        Extent3d {
            width: WORN_SIZE.0,
            height: WORN_SIZE.1,
            depth_or_array_layers: 1,
        },
        TextureDimension::D2,
        &[10, 34, 20, 255],
        TextureFormat::Rgba8UnormSrgb,
        RenderAssetUsages::default(),
    ));
    commands.insert_resource(Screen {
        material: materials.add(StandardMaterial {
            base_color_texture: Some(image.clone()),
            unlit: true,
            cull_mode: None,
            ..default()
        }),
        image,
        worn_material: materials.add(StandardMaterial {
            base_color_texture: Some(worn_image.clone()),
            unlit: true,
            cull_mode: None,
            ..default()
        }),
        worn_image,
        quad: meshes.add(Rectangle::new(SCREEN_INCHES.x, SCREEN_INCHES.y)),
        entity: None,
        blips: Vec::new(),
        swept_at: -10.0,
    });
}

#[allow(clippy::too_many_arguments)]
fn sense(
    time: Res<Time>,
    state: Res<FpsState>,
    loadout: Res<Loadout>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    camera: Query<&Transform, With<WorldCamera>>,
    screen: Option<ResMut<Screen>>,
    hostility: benilla_app::Hostility,
) {
    let Some(mut screen) = screen else {
        return;
    };
    let t = time.elapsed_secs();
    if !(state.active && has_heartbeat(&loadout)) || t < screen.swept_at + HEARTBEAT_PULSE {
        return;
    }
    screen.swept_at = t;
    screen.blips.clear();
    let (Some(me), Ok(cam)) = (
        objects
            .player()
            .and_then(|e| bodies.get(e).ok())
            .map(|g| g.translation()),
        camera.single(),
    ) else {
        return;
    };
    let fwd = Vec2::new(cam.forward().x, cam.forward().z)
        .try_normalize()
        .unwrap_or(Vec2::NEG_Y);
    let right = Vec2::new(-fwd.y, fwd.x);
    for (guid, entity, fields) in objects.iter() {
        if Some(guid) == state.self_guid || fields.unit_health().unwrap_or(0) == 0 {
            continue;
        }
        // Enemies only: what we can attack and is not friendly (guards and townsfolk stay off).
        let unit = matches!(
            fields.object_type(),
            Some(ObjectType::Unit | ObjectType::Player)
        );
        if !unit || !hostility.is_enemy(entity) {
            continue;
        }
        let Ok(gt) = bodies.get(entity) else {
            continue;
        };
        let d = gt.translation() - me;
        let rel = Vec2::new(d.x, d.z);
        if let Some(p) = heartbeat_point(rel.dot(right), rel.dot(fwd)) {
            screen.blips.push(p);
        }
        if screen.blips.len() == HEARTBEAT_DOTS {
            break;
        }
    }
}

/// The screen's picture `age` seconds after the sweep (RGBA rows, SCREEN_W × SCREEN_H): dark
/// green, range rings, the sweep line rising, our mark, and the blips fading.
pub fn paint(pixels: &mut [u8], blips: &[Vec2], age: f32) {
    paint_into(
        pixels,
        (SCREEN_W, SCREEN_H),
        [0.0, 0.0, 1.0, 1.0],
        blips,
        age,
    );
}

/// The same picture in the window `view` (u0, v0, u1, v1: shares of the texture) of a texture
/// `size` pixels big: the gun's own sensor screen shows only part of its texture.
pub fn paint_into(pixels: &mut [u8], size: (u32, u32), view: [f32; 4], blips: &[Vec2], age: f32) {
    let (tw, th) = (size.0 as i32, size.1 as i32);
    for px in pixels.chunks_exact_mut(4) {
        px.copy_from_slice(&[10, 34, 20, 255]);
    }
    let (x0, y0) = ((view[0] * tw as f32) as i32, (view[1] * th as f32) as i32);
    let (x1, y1) = ((view[2] * tw as f32) as i32, (view[3] * th as f32) as i32);
    let (w, h) = (x1 - x0, y1 - y0);
    let mut put = |x: i32, y: i32, c: [u8; 3]| {
        if (0..w).contains(&x) && (0..h).contains(&y) {
            let i = (((y0 + y) * tw + x0 + x) * 4) as usize;
            if let Some(px) = pixels.get_mut(i..i + 4) {
                px.copy_from_slice(&[c[0], c[1], c[2], 255]);
            }
        }
    };
    // Range rings round our place, the bottom middle.
    let (cx, cy) = (w / 2, h - 2);
    for ring in [0.33f32, 0.66, 1.0] {
        let (rx, ry) = (ring * (w / 2 - 1) as f32, ring * (h - 4) as f32);
        for k in 0..120 {
            let a = std::f32::consts::PI * k as f32 / 119.0;
            put(
                cx + (rx * a.cos()) as i32,
                cy - (ry * a.sin()) as i32,
                [50, 170, 95],
            );
        }
    }
    // The sweep rising up the screen through the pulse.
    let k = (age / HEARTBEAT_PULSE).clamp(0.0, 1.0);
    let sweep = (h as f32 * (1.0 - k)) as i32;
    for x in 0..w {
        put(x, sweep, [90, 230, 140]);
    }
    for x in -2..=2 {
        put(cx + x, cy, [190, 255, 210]);
    }
    // The blips hold until the next sweep, dimming a little.
    let fade = 1.0 - k * 0.35;
    let red = [
        (255.0 * fade) as u8,
        (40.0 * fade) as u8,
        (30.0 * fade) as u8,
    ];
    // A blip big enough to read at a glance: a red disc with a bright core.
    let core = [255, (170.0 * fade) as u8, (150.0 * fade) as u8];
    for b in blips {
        let (bx, by) = ((b.x * w as f32) as i32, (b.y * h as f32) as i32);
        for dy in -4..=4 {
            for dx in -4..=4 {
                let r2 = dx * dx + dy * dy;
                if r2 <= 2 {
                    put(bx + dx, by + dy, core);
                } else if r2 <= 13 {
                    put(bx + dx, by + dy, red);
                }
            }
        }
    }
}

fn draw(time: Res<Time>, screen: Option<Res<Screen>>, mut images: ResMut<Assets<Image>>) {
    let Some(screen) = screen else {
        return;
    };
    let age = time.elapsed_secs() - screen.swept_at;
    if let Some(image) = images.get_mut(&screen.image)
        && let Some(data) = image.data.as_mut()
    {
        paint(data, &screen.blips, age);
    }
    if let Some(image) = images.get_mut(&screen.worn_image)
        && let Some(data) = image.data.as_mut()
    {
        paint_into(data, WORN_SIZE, WORN_VIEW, &screen.blips, age);
    }
}

/// The screen on the gun: on the sensor's bone, else by the gun's frame, turned to the eye.
fn hold(
    mut commands: Commands,
    state: Res<FpsState>,
    loadout: Res<Loadout>,
    screen: Option<ResMut<Screen>>,
    viewmodels: Query<(Entity, &Viewmodel)>,
    mut quads: Query<(&mut Transform, &mut Visibility), With<ScreenQuad>>,
    worn: Query<(), With<crate::mw2::viewmodel::MotionTrackerScreen>>,
) {
    let Some(mut screen) = screen else {
        return;
    };
    // The gun's own sensor screen carries the radar (wear_screen); the card is for a model
    // without one.
    let on = state.active && has_heartbeat(&loadout) && worn.is_empty();
    // The gun actually in hand: through a swap the old one still shows while the loadout names
    // the next, and the sensor belongs on the sensor gun only.
    let vm = viewmodels
        .iter()
        .find(|(_, vm)| vm.hand().0 == 0 && vm.weapon_name().contains("heartbeat"));
    let quad = screen.entity.filter(|e| quads.get(*e).is_ok());
    let Some((vm_entity, vm)) = vm.filter(|_| on) else {
        if let Some(e) = quad
            && let Ok((_, mut v)) = quads.get_mut(e)
        {
            *v = Visibility::Hidden;
        }
        return;
    };
    let Some(e) = quad else {
        screen.entity = Some(
            commands
                .spawn((
                    Name::new("mw2 heartbeat screen"),
                    ScreenQuad,
                    Mesh3d(screen.quad.clone()),
                    MeshMaterial3d(screen.material.clone()),
                    Transform::default(),
                    Visibility::Hidden,
                    ChildOf(vm_entity),
                ))
                .id(),
        );
        return;
    };
    let Ok((mut tf, mut vis)) = quads.get_mut(e) else {
        return;
    };
    // The sensor's own bone if the model has one; else on the gun's left side, toward the front.
    let at = vm
        .bone_named("heartbeat")
        .and_then(|b| vm.bone_in_root(b))
        .map(|m| m.w_axis.truncate())
        .or_else(|| {
            vm.bone_named("j_gun")
                .and_then(|b| vm.bone_in_root(b))
                .map(|m| m.transform_point3(Vec3::new(9.0, 2.2, 1.2)))
        });
    let Some(at) = at else {
        *vis = Visibility::Hidden;
        return;
    };
    // Facing the eye (the root's origin): the quad's front (+Z) toward it.
    *tf = Transform::from_translation(at).looking_at(at * 2.0, Vec3::Y);
    *vis = Visibility::Inherited;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_sensor_shows_bodies_ahead_within_reach() {
        assert_eq!(heartbeat_point(0.0, 0.0), Some(Vec2::new(0.5, 1.0))); // at our feet
        assert_eq!(
            heartbeat_point(0.0, HEARTBEAT_RANGE),
            Some(Vec2::new(0.5, 0.0))
        );
        let p = heartbeat_point(HEARTBEAT_RANGE / 2.0, 0.0).unwrap();
        assert!((p.x - 0.75).abs() < 1e-6); // to the right
        assert!(heartbeat_point(0.0, -3.0).is_none()); // behind
        assert!(heartbeat_point(30.0, 30.0).is_none()); // past the reach
    }

    #[test]
    fn the_sensor_is_on_the_gun_that_carries_it() {
        let mut l = Loadout {
            mw2_weapon: "m4_mp".into(),
            class_gun: Some(crate::cac::InHand {
                base: "m4_mp".into(),
                variant: "m4_heartbeat_mp".into(),
                camo_model: None,
                look: None,
                attachments: Vec::new(),
            }),
            ..Default::default()
        };
        assert!(has_heartbeat(&l));
        l.mw2_weapon = "usp_mp".into(); // the secondary in hand
        assert!(!has_heartbeat(&l));
    }

    #[test]
    fn the_screen_paints_blips_red() {
        let mut px = vec![0u8; (SCREEN_W * SCREEN_H * 4) as usize];
        paint(&mut px, &[Vec2::new(0.5, 0.5)], 0.0);
        let (x, y) = (SCREEN_W / 2, SCREEN_H / 2);
        let i = ((y * SCREEN_W + x) * 4) as usize;
        assert!(px[i] > 200); // a blip in the middle
        let edge = ((y * SCREEN_W + x + 3) * 4) as usize;
        assert!(px[edge] > 200 && px[edge + 1] < 60); // red round its core
        assert_eq!(&px[0..4], &[10, 34, 20, 255]); // dark screen in the corner
    }

    #[test]
    fn the_guns_own_screen_shows_the_radar_in_its_window() {
        let (w, h) = WORN_SIZE;
        let mut px = vec![0u8; (w * h * 4) as usize];
        paint_into(&mut px, WORN_SIZE, WORN_VIEW, &[Vec2::new(0.5, 0.5)], 0.0);
        // A blip in the middle of the radar lands in the middle of the window, not the texture.
        let x = ((WORN_VIEW[0] + WORN_VIEW[2]) / 2.0 * w as f32) as u32;
        let y = ((WORN_VIEW[1] + WORN_VIEW[3]) / 2.0 * h as f32) as u32;
        let i = ((y * w + x) * 4) as usize;
        assert!(px[i] > 200);
        let mid = (((h / 2) * w + w * 3 / 4) * 4) as usize; // the texture's right half: unseen
        assert!(px[mid] < 20);
    }

    #[test]
    fn the_heartbeat_system_has_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((sense, hold));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
}
