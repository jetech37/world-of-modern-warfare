//! The killcam, after MW2's: shot dead, the view cuts to whoever killed you and looks back at
//! where you fell, over their shoulder, for a few seconds, with the gun that did it. MW2 replays
//! the last seconds; this is the live view from the killer's place (benilla keeps no history of
//! the world to replay).

use benilla_app::Objects;
use benilla_world::view::WorldCamera;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::IncomingFire;
use crate::mode::FpsState;

/// The pause between falling and the cut, and how long the killcam runs.
pub const DELAY_SECS: f32 = 0.8;
pub const RUN_SECS: f32 = 4.0;
/// A hit this recent names the killer.
pub const KILLER_WINDOW_SECS: f32 = 6.0;

#[derive(Debug, Clone, Copy)]
pub struct Shot {
    pub killer: u64,
    pub gun: u16,
    pub start: f32,
    /// Where we fell (Bevy space).
    pub body: Vec3,
}

#[derive(Resource, Default)]
pub struct Killcam {
    /// The last unit to hit us: (guid, gun, when).
    last_hit: Option<(u64, u16, f32)>,
    pub shot: Option<Shot>,
    was_dead: bool,
}

impl Killcam {
    /// The unit that last hit us, if that was recent enough at `t` to have killed us.
    pub fn recent_killer(&self, t: f32) -> Option<u64> {
        self.last_hit
            .filter(|(_, _, when)| t - when <= KILLER_WINDOW_SECS)
            .map(|(guid, ..)| guid)
    }
}

/// Whether the killcam shows at `t`, and how far through it is (0..1).
pub fn progress(shot: &Shot, t: f32) -> Option<f32> {
    let into = t - shot.start;
    (0.0..RUN_SECS).contains(&into).then_some(into / RUN_SECS)
}

/// The killcam's eye: over the killer's shoulder (a little behind and above them, away from the
/// body), easing a quarter of the way in toward the body as it runs.
pub fn eye(killer: Vec3, body: Vec3, k: f32) -> Transform {
    let head = killer + Vec3::Y * 1.8;
    let away = (head - body).with_y(0.0).normalize_or(Vec3::X);
    let side = away.cross(Vec3::Y).normalize_or(Vec3::Z);
    let start = head + away * 1.6 + side * 0.6 + Vec3::Y * 0.4;
    let at = start.lerp(body + Vec3::Y * 1.0, 0.25 * k.clamp(0.0, 1.0));
    Transform::from_translation(at).looking_at(body + Vec3::Y * 0.4, Vec3::Y)
}

#[derive(Component)]
struct KillcamTitle;

#[derive(Component)]
struct KillcamBar;

pub struct KillcamPlugin;

impl Plugin for KillcamPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Killcam>()
            .add_systems(Startup, spawn_ui)
            .add_systems(
                Update,
                (watch, aim_camera)
                    .chain()
                    .after(crate::stance_view::apply_stance_view)
                    .before(Mw2Set::Aim),
            )
            .add_systems(Update, draw_ui.in_set(Mw2Set::Hud));
    }
}

fn watch(
    time: Res<Time>,
    mut cam: ResMut<Killcam>,
    mut state: ResMut<FpsState>,
    mut fire: MessageReader<IncomingFire>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
) {
    let t = time.elapsed_secs();
    for f in fire.read() {
        if f.hit {
            cam.last_hit = Some((f.guid, f.gun, t));
        }
    }
    let me = state.self_guid.and_then(|g| objects.object(g));
    let dead = me.and_then(|f| f.unit_health()).is_some_and(|h| h == 0);
    if dead && !cam.was_dead && state.active {
        let body = objects
            .player()
            .and_then(|e| bodies.get(e).ok())
            .map(GlobalTransform::translation);
        if let (Some((killer, gun, when)), Some(body)) = (cam.last_hit, body)
            && t - when <= KILLER_WINDOW_SECS
        {
            cam.shot = Some(Shot {
                killer,
                gun,
                start: t + DELAY_SECS,
                body,
            });
        }
    }
    if !dead {
        cam.shot = None;
        if cam.was_dead {
            // Back up: a fresh life, no killer.
            cam.last_hit = None;
        }
    }
    cam.was_dead = dead;
    let on = cam.shot.as_ref().is_some_and(|s| progress(s, t).is_some());
    if cam.shot.as_ref().is_some_and(|s| t - s.start >= RUN_SECS) {
        cam.shot = None;
    }
    if state.killcam != on {
        state.killcam = on;
    }
}

fn aim_camera(
    time: Res<Time>,
    cam: Res<Killcam>,
    objects: Objects,
    bodies: Query<&GlobalTransform, Without<WorldCamera>>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
) {
    let Some(shot) = cam.shot else {
        return;
    };
    let Some(k) = progress(&shot, time.elapsed_secs()) else {
        return;
    };
    let Some(killer) = objects
        .entity(shot.killer)
        .and_then(|e| bodies.get(e).ok())
        .map(GlobalTransform::translation)
    else {
        return;
    };
    for mut tf in &mut cameras {
        *tf = eye(killer, shot.body, k);
    }
}

fn spawn_ui(mut commands: Commands, assets: Res<AssetServer>) {
    let font: Handle<Font> = assets.load(crate::wow_ui::FONT);
    for top in [true, false] {
        commands.spawn((
            KillcamBar,
            Node {
                position_type: PositionType::Absolute,
                top: if top { px(0) } else { auto() },
                bottom: if top { auto() } else { px(0) },
                width: percent(100),
                height: percent(11),
                ..default()
            },
            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.85)),
            GlobalZIndex(i32::MAX - 3),
            Visibility::Hidden,
            bevy::picking::Pickable::IGNORE,
        ));
    }
    commands.spawn((
        KillcamTitle,
        Node {
            position_type: PositionType::Absolute,
            top: percent(2.5),
            width: percent(100),
            justify_content: JustifyContent::Center,
            ..default()
        },
        Text::new(""),
        TextFont::from_font_size(30.0).with_font(font),
        TextColor(Color::srgb(1.0, 0.82, 0.3)),
        TextLayout::new_with_justify(Justify::Center),
        GlobalZIndex(i32::MAX - 2),
        Visibility::Hidden,
        bevy::picking::Pickable::IGNORE,
    ));
}

/// "KILLCAM" and the gun, by its name in the world (crate::arsenal).
pub fn title(gun: Option<&str>) -> String {
    match gun {
        Some(g) => format!("KILLCAM\n{}", crate::arsenal::weapon_name(g).to_uppercase()),
        None => "KILLCAM".into(),
    }
}

fn draw_ui(
    cam: Res<Killcam>,
    state: Res<FpsState>,
    catalog: Res<crate::catalog::Catalog>,
    mut bars: Query<&mut Visibility, (With<KillcamBar>, Without<KillcamTitle>)>,
    mut title_q: Query<(&mut Text, &mut Visibility), With<KillcamTitle>>,
) {
    let want = if state.killcam {
        Visibility::Visible
    } else {
        Visibility::Hidden
    };
    for mut v in &mut bars {
        if *v != want {
            *v = want;
        }
    }
    for (mut text, mut v) in &mut title_q {
        if *v != want {
            *v = want;
        }
        if let Some(shot) = cam.shot {
            let s = title(catalog.gun(shot.gun).map(|g| g.name.as_str()));
            if text.0 != s {
                text.0 = s;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_killcam_runs_after_the_fall() {
        let shot = Shot {
            killer: 1,
            gun: 2,
            start: 10.0,
            body: Vec3::ZERO,
        };
        assert!(progress(&shot, 9.9).is_none());
        assert_eq!(progress(&shot, 10.0), Some(0.0));
        assert!((progress(&shot, 12.0).unwrap() - 0.5).abs() < 1e-6);
        assert!(progress(&shot, 14.0).is_none());
    }

    #[test]
    fn the_eye_looks_from_behind_the_killer_at_the_body() {
        let killer = Vec3::new(0.0, 0.0, -20.0);
        let body = Vec3::ZERO;
        let tf = eye(killer, body, 0.0);
        // Behind the killer (farther from the body than they are), above their head.
        assert!(tf.translation.distance(body) > killer.distance(body));
        assert!(tf.translation.y > 1.8);
        // Facing the body.
        let to_body = (body - tf.translation).normalize();
        assert!(tf.forward().dot(to_body) > 0.95);
        // Easing in.
        assert!(eye(killer, body, 1.0).translation.distance(body) < tf.translation.distance(body));
    }

    #[test]
    fn the_title_names_the_gun() {
        assert_eq!(title(Some("m4_mp")), "KILLCAM\nSTORMWIND GUARD CARBINE");
        assert_eq!(title(Some("barrett_mp")), "KILLCAM\nTHORIUM SIEGE RIFLE");
        assert_eq!(title(Some("no_such_mp")), "KILLCAM\nNO SUCH");
        assert_eq!(title(None), "KILLCAM");
    }
}
