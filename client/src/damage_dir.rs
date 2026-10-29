//! Where the damage came from: a red crescent around the crosshair on the side the hit came
//! from, as MW2 draws it, turning with the view while it fades.
//!
//! The bearing is flattened and taken from the camera's forward and right, one crescent per recent attacker (the same attacker
//! refreshes its own), placed on a ring around the screen's centre and turned to face out.
//!
//! The source of a hit is the server's word where it gives one (a `Hurt` event that hit: the
//! shooter, where it stood then); damage the fusion did not report (a WoW spell, a melee swing)
//! points at the nearest unit that has us targeted.

use benilla_app::Objects;
use bevy::picking::Pickable;
use bevy::prelude::*;
use bevy::ui::Val2;

use crate::Mw2Set;
use crate::combat::CombatEvent;
use crate::fusion::IncomingFire;
use crate::mode::FpsState;

/// At most this many crescents at once.
pub const MAX_HITS: usize = 8;
/// A crescent fades out over this long (seconds).
pub const FADE_SECS: f32 = 1.75;
/// How far out from the screen's centre the crescents sit (pixels).
pub const RADIUS_PX: f32 = 150.0;
/// A health drop this soon after the server's own word on a hit is that hit (seconds).
pub const FALLBACK_WINDOW: f32 = 0.5;
/// The pixels per UI unit [`RADIUS_PX`] and the crescent's size are drawn for (a 1080p window
/// at the default `uiScale` 0.9); the ring and the crescents scale from it with the seam.
pub const DESIGN_SEAM: f32 = 1080.0 / 768.0 * 0.9;
/// The crescent's texture (and node) size in pixels.
pub const CRESCENT_W: u32 = 128;
pub const CRESCENT_H: u32 = 32;

/// The angle (radians) from the camera's flattened forward to a source `to_source` away,
/// clockwise seen from above: 0 ahead, π/2 to the right, -π/2 to the left, ±π behind.
pub fn bearing(to_source: Vec3, cam_forward: Vec3) -> f32 {
    let forward = Vec3::new(cam_forward.x, 0.0, cam_forward.z).normalize_or_zero();
    let d = Vec3::new(to_source.x, 0.0, to_source.z);
    let right = forward.cross(Vec3::Y);
    d.dot(right).atan2(d.dot(forward))
}

/// How strongly a crescent `age` seconds old shows (1 fresh, 0 gone).
pub fn fade(age: f32) -> f32 {
    (1.0 - age / FADE_SECS).clamp(0.0, 1.0)
}

/// Where a crescent at `bearing` sits, from the screen's centre (pixels, y down): straight up
/// for ahead, to the right for the right.
pub fn ring_offset(bearing: f32) -> Vec2 {
    Vec2::new(bearing.sin(), -bearing.cos()) * RADIUS_PX
}

/// A hit: who, where it came from, when.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DamageHit {
    pub source: u64,
    pub at: Vec3,
    pub born: f32,
}

/// The recent hits, oldest first.
#[derive(Resource, Debug, Default)]
pub struct DamageDirs {
    pub hits: Vec<DamageHit>,
    /// When the server last told of a hit on us.
    last_reported: Option<f32>,
}

impl DamageDirs {
    /// A hit from `source` at `at`: the same attacker's crescent moves and refreshes; past
    /// `MAX_HITS` the oldest goes.
    pub fn add(&mut self, source: u64, at: Vec3, t: f32) {
        self.hits.retain(|h| h.source != source);
        self.hits.push(DamageHit {
            source,
            at,
            born: t,
        });
        if self.hits.len() > MAX_HITS {
            let extra = self.hits.len() - MAX_HITS;
            self.hits.drain(..extra);
        }
    }

    /// Forget the faded.
    pub fn prune(&mut self, t: f32) {
        self.hits.retain(|h| fade(t - h.born) > 0.0);
    }
}

/// The crescent's texture: a red arc of the ring the crescents sit on, bright along its middle,
/// thinning to its ends and edges (`CRESCENT_W` × `CRESCENT_H`, RGBA).
pub fn crescent_rgba() -> Vec<u8> {
    let (w, h) = (CRESCENT_W as usize, CRESCENT_H as usize);
    let centre = Vec2::new(w as f32 / 2.0, h as f32 / 2.0 + RADIUS_PX);
    let half_span = (w as f32 / 2.0 / RADIUS_PX).asin() * 0.92;
    let mut rgba = vec![0u8; w * h * 4];
    for y in 0..h {
        for x in 0..w {
            let p = Vec2::new(x as f32 + 0.5, y as f32 + 0.5);
            let rel = p - centre;
            let radial = (rel.length() - RADIUS_PX).abs();
            let thick = (1.0 - (radial - 3.0).max(0.0) / 7.0).clamp(0.0, 1.0);
            let angle = rel.x.atan2(-rel.y).abs();
            let along = (1.0 - (angle / half_span).powi(2)).clamp(0.0, 1.0);
            let a = thick * thick * along;
            let i = (y * w + x) * 4;
            rgba[i..i + 4].copy_from_slice(&[230, 24, 12, (a * 255.0).round() as u8]);
        }
    }
    rgba
}

#[derive(Component)]
struct DamageRoot;

/// One crescent's node, by slot.
#[derive(Component)]
struct Crescent(usize);

pub struct DamageDirPlugin;

impl Plugin for DamageDirPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<DamageDirs>()
            .add_systems(Startup, setup)
            .add_systems(Update, (collect, draw).chain().in_set(Mw2Set::Hud));
    }
}

fn setup(mut commands: Commands, images: Option<ResMut<Assets<Image>>>) {
    let Some(mut images) = images else {
        return;
    };
    let tex = images.add(benilla_assets::sprite_image(
        CRESCENT_W,
        CRESCENT_H,
        crescent_rgba(),
    ));
    commands
        .spawn((
            Name::new("mw2 damage direction"),
            DamageRoot,
            Node {
                position_type: PositionType::Absolute,
                left: percent(50),
                top: percent(50),
                width: px(0),
                height: px(0),
                ..default()
            },
            GlobalZIndex(i32::MAX - 2),
            Visibility::Hidden,
            Pickable::IGNORE,
        ))
        .with_children(|root| {
            for i in 0..MAX_HITS {
                root.spawn((
                    Crescent(i),
                    Node {
                        position_type: PositionType::Absolute,
                        left: px(-(CRESCENT_W as f32) / 2.0),
                        top: px(-(CRESCENT_H as f32) / 2.0),
                        width: px(CRESCENT_W as f32),
                        height: px(CRESCENT_H as f32),
                        ..default()
                    },
                    ImageNode {
                        image: tex.clone(),
                        color: Color::WHITE.with_alpha(0.0),
                        ..default()
                    },
                    UiTransform::default(),
                    Visibility::Hidden,
                    Pickable::IGNORE,
                ));
            }
        });
}

/// The hits: the server's (a round that hit us, from where its shooter stood), and health lost
/// with no word from it (the nearest unit with us targeted).
#[allow(clippy::too_many_arguments)]
fn collect(
    time: Res<Time>,
    state: Res<FpsState>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut fire: MessageReader<IncomingFire>,
    mut combat: MessageReader<CombatEvent>,
    mut dirs: ResMut<DamageDirs>,
) {
    let t = time.elapsed_secs();
    let position = |guid: u64| {
        objects
            .entity(guid)
            .and_then(|e| bodies.get(e).ok())
            .map(GlobalTransform::translation)
    };
    for f in fire.read() {
        if !f.hit {
            continue;
        }
        dirs.last_reported = Some(t);
        if let Some(at) = position(f.guid) {
            dirs.add(f.guid, at, t);
        }
    }
    let hurt = combat.read().any(|e| matches!(e, CombatEvent::Hurt { .. }));
    if hurt
        && dirs.last_reported.is_none_or(|r| t - r > FALLBACK_WINDOW)
        && let (Some(me), Some(eye)) = (
            state.self_guid,
            cameras.iter().next().map(GlobalTransform::translation),
        )
    {
        let nearest = objects
            .iter()
            .filter(|(guid, _, f)| {
                *guid != me && f.unit_target() == Some(me) && f.unit_health().unwrap_or(0) > 0
            })
            .filter_map(|(guid, e, _)| Some((guid, bodies.get(e).ok()?.translation())))
            .min_by(|a, b| a.1.distance(eye).total_cmp(&b.1.distance(eye)));
        if let Some((guid, at)) = nearest {
            dirs.add(guid, at, t);
        }
    }
    dirs.prune(t);
}

/// Each crescent on the ring at its hit's bearing from where the camera looks now, fading.
#[allow(clippy::type_complexity)]
fn draw(
    time: Res<Time>,
    state: Res<FpsState>,
    dirs: Res<DamageDirs>,
    scale: Option<Res<crate::hud::HudScale>>,
    cameras: Query<&GlobalTransform, With<benilla_world::view::WorldCamera>>,
    mut roots: Query<&mut Visibility, (With<DamageRoot>, Without<Crescent>)>,
    mut crescents: Query<
        (&Crescent, &mut UiTransform, &mut ImageNode, &mut Visibility),
        Without<DamageRoot>,
    >,
) {
    let t = time.elapsed_secs();
    let camera = cameras.iter().next();
    let show = state.active && camera.is_some() && !dirs.hits.is_empty();
    for mut v in &mut roots {
        let want = if show {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
    let Some(cam) = camera.filter(|_| show) else {
        return;
    };
    let eye = cam.translation();
    let forward = *cam.forward();
    for (slot, mut tf, mut image, mut vis) in &mut crescents {
        let Some(hit) = dirs.hits.get(slot.0) else {
            if *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
            continue;
        };
        let b = bearing(hit.at - eye, forward);
        let zoom = (scale.as_ref().map_or(DESIGN_SEAM, |s| s.0) / DESIGN_SEAM).clamp(0.5, 3.0);
        let off = ring_offset(b) * zoom;
        tf.translation = Val2::px(off.x, off.y);
        tf.scale = Vec2::splat(zoom);
        tf.rotation = Rot2::radians(b);
        image.color = Color::WHITE.with_alpha(fade(t - hit.born));
        *vis = Visibility::Inherited;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f32::consts::{FRAC_PI_2, PI};

    #[test]
    fn bearings_ahead_right_left_and_behind() {
        let fwd = Vec3::NEG_Z; // Bevy's camera looks down -Z; right is +X
        assert!(bearing(Vec3::new(0.0, 0.0, -10.0), fwd).abs() < 1e-5);
        assert!((bearing(Vec3::new(10.0, 0.0, 0.0), fwd) - FRAC_PI_2).abs() < 1e-5);
        assert!((bearing(Vec3::new(-10.0, 0.0, 0.0), fwd) + FRAC_PI_2).abs() < 1e-5);
        assert!((bearing(Vec3::new(0.0, 0.0, 10.0), fwd).abs() - PI).abs() < 1e-5);
        // Height and a camera looking down make no difference.
        let down = Vec3::new(0.0, -0.8, -0.6);
        assert!((bearing(Vec3::new(10.0, 25.0, 0.0), down) - FRAC_PI_2).abs() < 1e-5);
    }

    #[test]
    fn a_turn_of_the_camera_turns_the_bearing() {
        let source = Vec3::new(0.0, 0.0, -10.0); // ahead at first
        // Turned a quarter to the right (now facing +X), the source is on the left.
        assert!((bearing(source, Vec3::X) + FRAC_PI_2).abs() < 1e-5);
        // Turned to face it from behind.
        assert!((bearing(source, Vec3::Z).abs() - PI).abs() < 1e-5);
    }

    #[test]
    fn ahead_is_up_the_screen_and_right_is_right() {
        let up = ring_offset(0.0);
        assert!(up.x.abs() < 1e-4 && (up.y + RADIUS_PX).abs() < 1e-4);
        let right = ring_offset(FRAC_PI_2);
        assert!((right.x - RADIUS_PX).abs() < 1e-4 && right.y.abs() < 1e-3);
    }

    #[test]
    fn a_crescent_fades_out() {
        assert_eq!(fade(0.0), 1.0);
        assert!(fade(FADE_SECS * 0.5) > 0.4 && fade(FADE_SECS * 0.5) < 0.6);
        assert_eq!(fade(FADE_SECS), 0.0);
        let mut d = DamageDirs::default();
        d.add(1, Vec3::ZERO, 0.0);
        d.prune(FADE_SECS * 0.9);
        assert_eq!(d.hits.len(), 1);
        d.prune(FADE_SECS + 0.01);
        assert!(d.hits.is_empty());
    }

    #[test]
    fn hits_stack_up_to_eight_oldest_first_out() {
        let mut d = DamageDirs::default();
        for g in 0..10u64 {
            d.add(g, Vec3::splat(g as f32), g as f32 * 0.01);
        }
        assert_eq!(d.hits.len(), MAX_HITS);
        assert_eq!(d.hits[0].source, 2);
        assert_eq!(d.hits.last().unwrap().source, 9);
    }

    #[test]
    fn the_same_attacker_refreshes_its_own() {
        let mut d = DamageDirs::default();
        d.add(7, Vec3::X, 0.0);
        d.add(8, Vec3::Y, 0.1);
        d.add(7, Vec3::Z, 1.0);
        assert_eq!(d.hits.len(), 2);
        let seven = d.hits.iter().find(|h| h.source == 7).unwrap();
        assert_eq!((seven.at, seven.born), (Vec3::Z, 1.0));
    }

    #[test]
    fn the_crescent_is_solid_in_the_middle_and_clear_at_its_ends() {
        let rgba = crescent_rgba();
        let (w, h) = (CRESCENT_W as usize, CRESCENT_H as usize);
        assert_eq!(rgba.len(), w * h * 4);
        let alpha = |x: usize, y: usize| rgba[(y * w + x) * 4 + 3];
        assert!(alpha(w / 2, h / 2) > 200);
        assert_eq!(alpha(0, 0), 0);
        assert_eq!(alpha(w - 1, 0), 0);
        assert_eq!(alpha(w / 2, 0), 0);
        assert_eq!(alpha(w / 2, h - 1), 0);
        // Its ends taper.
        assert!(alpha(6, h / 2 + 2) < alpha(w / 2, h / 2));
    }

    #[test]
    fn the_damage_direction_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((setup, collect, draw));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
}
