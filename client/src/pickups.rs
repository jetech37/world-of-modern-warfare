//! Weapon pickups, as in MW2 (design doc section 6): a dead NPC gunner or player drops the gun in
//! hand. The server lists the guns lying near (world channel op 60); each is drawn on the ground
//! as its MW2 world model (`crate::npc_guns`' models). Look at one within reach and hold F: the
//! client asks for it (op 61) and the server says what the player now holds (op 62), which
//! replaces the kit's gun in that slot until death, a kit swap or logout (`crate::cac` reads it).
//! F on a door still works the door: a door under the crosshair wins.

use std::collections::HashMap;

use bevy::prelude::*;

use benilla_app::Objects;
use benilla_app::fusion::{
    CHANNEL_WORLD, ChannelMessage, ClassLoadout, FusionChannelReceived, SendFusionChannel,
};
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::catalog::{Catalog, Slot};
use crate::mode::FpsState;
use crate::npc_guns::NpcGuns;

pub const OP_GUNS: u8 = 60;
pub const OP_TAKE: u8 = 61;
pub const OP_HELD: u8 = 62;
pub const OP_REFUSED: u8 = 63;
/// F is held this long to take a gun (the server's `Pickups::kHoldMs`).
pub const HOLD_SECS: f32 = 0.6;
/// How far the eye looks for a gun on the ground, and how near the crosshair's line must pass it.
pub const LOOK_REACH: f32 = 3.5;
pub const LOOK_SLACK: f32 = 0.45;
/// The server takes a gun within this of the feet (`Pickups::kReach`), a little less to be sure.
pub const FEET_REACH: f32 = 3.8;
/// A door within this under the crosshair takes F (`crate::build`'s door reach).
const DOOR_REACH: f32 = 4.0;

/// A gun lying on the ground (raw WoW coordinates).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Ground {
    pub id: u32,
    pub gun: u16,
    pub pos: [f32; 3],
    pub yaw: f32,
}

/// A picked-up gun as the server says it is held.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Held {
    pub gun: u16,
    pub camo: u8,
    pub att1: String,
    pub att2: String,
}

/// What the player holds from pickups, by slot (0 primary, 1 secondary).
#[derive(Resource, Debug, Default)]
pub struct PickedUp {
    pub slots: [Option<Held>; 2],
    /// A slot just filled: the hand goes to it (`crate::cac`), as MW2 raises a picked-up gun.
    pub raise: Option<u8>,
}

impl PickedUp {
    /// The class with its slots replaced by the pickups; `None` when there is neither.
    pub fn overlay(&self, class: Option<&ClassLoadout>) -> Option<ClassLoadout> {
        if self.slots.iter().all(Option::is_none) {
            return class.cloned();
        }
        let mut c = class.cloned().unwrap_or_default();
        if let Some(p) = &self.slots[0] {
            c.primary = p.gun;
            c.primary_att1 = p.att1.clone();
            c.primary_att2 = p.att2.clone();
            // A gun off the ground carries two attachments at most.
            c.primary_att3.clear();
            c.primary_att4.clear();
            c.primary_camo = p.camo;
        }
        if let Some(s) = &self.slots[1] {
            c.secondary = s.gun;
            c.secondary_att = s.att1.clone();
            c.secondary_att2 = s.att2.clone();
        }
        Some(c)
    }
}

/// The guns near, what is drawn of them, and the hold on F.
#[derive(Resource, Default)]
pub struct Pickups {
    pub ground: Vec<Ground>,
    drawn: HashMap<u32, Entity>,
    /// The gun under the crosshair within reach, if any.
    pub aimed: Option<u32>,
    hold: f32,
    /// Asked for this one; no second ask until the server answers or the aim moves.
    asked: Option<u32>,
    /// The server's last refusal and until when it shows.
    refused: Option<(u8, f32)>,
}

fn u32_at(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes(b[i..i + 4].try_into().unwrap_or_default())
}

fn f32_at(b: &[u8], i: usize) -> f32 {
    f32::from_le_bytes(b[i..i + 4].try_into().unwrap_or_default())
}

/// Op 60: `[60, count u8, count × (id u32, gun u16, x y z f32, yaw f32)]`.
pub fn decode_guns(b: &[u8]) -> Option<Vec<Ground>> {
    let (&op, rest) = b.split_first()?;
    let (&n, rest) = rest.split_first()?;
    if op != OP_GUNS || rest.len() != usize::from(n) * 22 {
        return None;
    }
    Some(
        rest.chunks_exact(22)
            .map(|c| Ground {
                id: u32_at(c, 0),
                gun: u16::from_le_bytes([c[4], c[5]]),
                pos: [f32_at(c, 6), f32_at(c, 10), f32_at(c, 14)],
                yaw: f32_at(c, 18),
            })
            .collect(),
    )
}

/// Op 61: `[61, id u32]`.
pub fn encode_take(id: u32) -> Vec<u8> {
    let mut b = vec![OP_TAKE];
    b.extend_from_slice(&id.to_le_bytes());
    b
}

/// Op 62: `[62, count u8, count × (slot u8, gun u16, camo u8, len u8, att1, len u8, att2)]`.
pub fn decode_held(b: &[u8]) -> Option<[Option<Held>; 2]> {
    let (&op, rest) = b.split_first()?;
    let (&n, mut rest) = rest.split_first()?;
    if op != OP_HELD {
        return None;
    }
    let mut out: [Option<Held>; 2] = Default::default();
    let text = |rest: &mut &[u8]| -> Option<String> {
        let (&len, r) = rest.split_first()?;
        let s = r.get(..usize::from(len))?;
        *rest = &r[usize::from(len)..];
        Some(String::from_utf8_lossy(s).into_owned())
    };
    for _ in 0..n {
        let (&slot, r) = rest.split_first()?;
        let g = r.get(..3)?;
        rest = &r[3..];
        let held = Held {
            gun: u16::from_le_bytes([g[0], g[1]]),
            camo: g[2],
            att1: text(&mut rest)?,
            att2: text(&mut rest)?,
        };
        *out.get_mut(usize::from(slot))? = Some(held);
    }
    rest.is_empty().then_some(out)
}

/// Op 63: `[63, reason u8]`: 1 gone, 2 too far.
pub fn decode_refused(b: &[u8]) -> Option<u8> {
    (b.len() == 2 && b[0] == OP_REFUSED).then_some(b[1])
}

/// What a refusal says.
pub fn refusal_text(reason: u8) -> &'static str {
    match reason {
        1 => "That gun is gone",
        2 => "Too far away",
        _ => "You can't take that",
    }
}

/// The gun whose spot the crosshair's line passes nearest, within `LOOK_REACH` of the eye and
/// `LOOK_SLACK` of the line, and within `FEET_REACH` of the feet.
pub fn aimed_gun(
    guns: &[Ground],
    eye: Vec3,
    forward: Vec3,
    feet: Vec3,
    to_bevy: impl Fn([f32; 3]) -> Vec3,
) -> Option<u32> {
    guns.iter()
        .filter_map(|g| {
            let at = to_bevy(g.pos);
            let rel = at - eye;
            let along = rel.dot(forward);
            if !(0.0..=LOOK_REACH).contains(&along) || at.distance(feet) > FEET_REACH {
                return None;
            }
            let off = (rel - forward * along).length();
            (off <= LOOK_SLACK).then_some((off, g.id))
        })
        .min_by(|a, b| a.0.total_cmp(&b.0))
        .map(|(_, id)| id)
}

/// The ground gun's turn: its barrel along the way the server laid it (`yaw`, WoW's facing), on
/// its side. MW2's world guns are built pointing -Z in Bevy (crate::tracer::from_wow).
pub fn lying(yaw: f32) -> Quat {
    Quat::from_rotation_y(yaw) * Quat::from_rotation_z(std::f32::consts::FRAC_PI_2)
}

/// The prompt line under the crosshair.
#[derive(Component)]
struct PromptLine;

pub struct PickupsPlugin;

impl Plugin for PickupsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Pickups>()
            .init_resource::<PickedUp>()
            .add_systems(Startup, spawn_prompt)
            .add_systems(
                Update,
                (read_world, draw_ground, aim_and_hold, show_prompt)
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn read_world(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut pickups: ResMut<Pickups>,
    mut picked: ResMut<PickedUp>,
    time: Res<Time>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD
            || m.body
                .first()
                .is_none_or(|&op| !(OP_GUNS..=OP_REFUSED).contains(&op))
        {
            continue;
        }
        if let Some(list) = decode_guns(&m.body) {
            pickups.ground = list;
        } else if let Some(slots) = decode_held(&m.body) {
            pickups.asked = None;
            // A slot that is new (or another gun now): raise it.
            let raise = (0..2u8).find(|&s| {
                slots[s as usize].is_some() && slots[s as usize] != picked.slots[s as usize]
            });
            picked.slots = slots;
            picked.raise = raise;
            info!("benilla-mw2: pickups now {:?}", picked.slots);
        } else if let Some(reason) = decode_refused(&m.body) {
            pickups.asked = None;
            pickups.refused = Some((reason, time.elapsed_secs() + 2.0));
        }
    }
}

/// Each gun on the ground as its MW2 world model, lying on its side.
#[allow(clippy::too_many_arguments)]
fn draw_ground(
    mut commands: Commands,
    mut pickups: ResMut<Pickups>,
    mut npc_guns: ResMut<NpcGuns>,
    assets: Res<crate::mw2::Mw2Assets>,
    catalog: Res<Catalog>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut images: ResMut<Assets<Image>>,
) {
    let pickups = &mut *pickups;
    let ground = &pickups.ground;
    pickups.drawn.retain(|id, e| {
        let keep = ground.iter().any(|g| g.id == *id);
        if !keep && let Ok(mut c) = commands.get_entity(*e) {
            c.despawn();
        }
        keep
    });
    for g in ground {
        if pickups.drawn.contains_key(&g.id) {
            continue;
        }
        let Some((scale, parts)) = npc_guns.world_model(
            g.gun,
            &assets,
            &catalog,
            &mut meshes,
            &mut materials,
            &mut images,
        ) else {
            continue; // the install is still loading, or has no model: try again later
        };
        let root = commands
            .spawn((
                Name::new("mw2 dropped gun"),
                Transform::from_translation(crate::tracer::from_wow(g.pos))
                    .with_rotation(lying(g.yaw))
                    .with_scale(Vec3::splat(scale)),
                Visibility::default(),
            ))
            .id();
        for (mesh, material) in parts {
            commands.spawn((
                Mesh3d(mesh),
                MeshMaterial3d(material),
                Transform::default(),
                ChildOf(root),
            ));
        }
        pickups.drawn.insert(g.id, root);
    }
}

/// The gun under the crosshair, and F held on it: ask for it once the hold is long enough. A
/// door under the crosshair keeps F for itself.
#[allow(clippy::too_many_arguments)]
fn aim_and_hold(
    time: Res<Time>,
    state: Res<FpsState>,
    input: Res<crate::input::Mw2Input>,
    mut pickups: ResMut<Pickups>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
    objects: Objects,
    bodies: Query<&GlobalTransform>,
    spatial: avian3d::prelude::SpatialQuery,
    boxes: Query<&crate::build::PieceBox>,
    build: Res<crate::build::Building>,
    mut send: MessageWriter<SendFusionChannel>,
) {
    let pickups = &mut *pickups;
    let eye = cameras.iter().next();
    let feet = objects
        .player()
        .and_then(|e| bodies.get(e).ok())
        .map(GlobalTransform::translation);
    let (Some(cam), Some(feet)) = (eye, feet) else {
        pickups.aimed = None;
        pickups.hold = 0.0;
        return;
    };
    if !state.owns_input || state.building || pickups.ground.is_empty() {
        pickups.aimed = None;
        pickups.hold = 0.0;
        return;
    }
    let forward = *cam.forward();
    // A door in reach under the crosshair: F is the door's.
    let door = spatial
        .cast_ray(
            cam.translation(),
            cam.forward(),
            DOOR_REACH,
            true,
            &benilla_world::collision::WorldCollision::body_filter(),
        )
        .and_then(|h| boxes.get(h.entity).ok())
        .is_some_and(|b| {
            build
                .pieces
                .get(&b.0)
                .is_some_and(|p| p.wire.kind.base() == crate::build::pieces::PieceType::WoodDoor)
        });
    let aimed = if door {
        None
    } else {
        aimed_gun(
            &pickups.ground,
            cam.translation(),
            forward,
            feet,
            crate::tracer::from_wow,
        )
    };
    if aimed != pickups.aimed {
        pickups.hold = 0.0;
        pickups.asked = None;
    }
    pickups.aimed = aimed;
    let Some(id) = aimed else {
        return;
    };
    if !input.interact.held {
        pickups.hold = 0.0;
        return;
    }
    pickups.hold += time.delta_secs();
    if pickups.hold >= HOLD_SECS && pickups.asked != Some(id) {
        pickups.asked = Some(id);
        send.write(SendFusionChannel(ChannelMessage {
            channel: CHANNEL_WORLD,
            body: encode_take(id),
        }));
    }
}

fn spawn_prompt(mut commands: Commands, assets: Res<AssetServer>) {
    commands.spawn((
        Name::new("mw2 pickup prompt"),
        PromptLine,
        Node {
            position_type: PositionType::Absolute,
            left: percent(50),
            top: percent(58),
            margin: UiRect::left(px(-200)),
            width: px(400),
            justify_content: JustifyContent::Center,
            ..default()
        },
        Text::new(""),
        TextFont::from_font_size(16.0).with_font(assets.load(crate::wow_ui::FONT)),
        TextColor(Color::srgb(0.95, 0.95, 0.95)),
        TextLayout::new_with_justify(Justify::Center),
        TextShadow {
            offset: Vec2::splat(1.5),
            color: Color::srgba(0.0, 0.0, 0.0, 0.85),
        },
        GlobalZIndex(i32::MAX - 1),
        Pickable::IGNORE,
    ));
}

/// "Hold F to pick up the Thorium Siege Rifle", with the hold's progress, or the last refusal.
fn show_prompt(
    time: Res<Time>,
    state: Res<FpsState>,
    pickups: Res<Pickups>,
    catalog: Res<Catalog>,
    mut lines: Query<&mut Text, With<PromptLine>>,
    keymap: Res<crate::keybinds::Keymap>,
) {
    let t = time.elapsed_secs();
    let s = match (pickups.aimed, pickups.refused) {
        _ if !state.active => String::new(),
        (_, Some((reason, until))) if t < until => refusal_text(reason).to_owned(),
        (Some(id), _) => pickups
            .ground
            .iter()
            .find(|g| g.id == id)
            .and_then(|g| catalog.gun(g.gun))
            .map_or_else(String::new, |gun| {
                let slot = match Catalog::slot(gun) {
                    Slot::Primary => "primary",
                    _ => "secondary",
                };
                let bar = if pickups.hold > 0.0 {
                    let n = ((pickups.hold / HOLD_SECS).clamp(0.0, 1.0) * 10.0) as usize;
                    format!("\n[{}{}]", "|".repeat(n), ".".repeat(10 - n))
                } else {
                    String::new()
                };
                format!(
                    "Hold {} to pick up the {} ({slot}){bar}",
                    keymap.label(crate::keybinds::Action::Interact),
                    crate::arsenal::gun_name(gun)
                )
            }),
        _ => String::new(),
    };
    for mut text in &mut lines {
        if text.0 != s {
            text.0 = s.clone();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(s: &str) -> Vec<u8> {
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
            .collect()
    }

    #[test]
    fn the_server_messages_decode() {
        // The server's PickupTests vectors.
        let list = decode_guns(&hex("3c0104030201150000d60bc6004004c30000a7420000c03f")).unwrap();
        assert_eq!(
            list,
            vec![Ground {
                id: 0x0102_0304,
                gun: 21,
                pos: [-8949.5, -132.25, 83.5],
                yaw: 1.5
            }]
        );
        assert_eq!(decode_guns(&[OP_GUNS, 0]), Some(vec![]));
        assert_eq!(decode_guns(&[OP_GUNS, 1, 0]), None);
        assert_eq!(encode_take(0x0102_0304), hex("3d04030201"));
        let held = decode_held(&hex("3e01000200010461636f6702676c")).unwrap();
        assert_eq!(
            held[0],
            Some(Held {
                gun: 2,
                camo: 1,
                att1: "acog".into(),
                att2: "gl".into()
            })
        );
        assert_eq!(held[1], None);
        assert_eq!(decode_held(&[OP_HELD, 0]), Some([None, None]));
        assert_eq!(decode_held(&[OP_HELD, 1, 5]), None);
        assert_eq!(decode_refused(&[OP_REFUSED, 2]), Some(2));
    }

    #[test]
    fn a_pickup_replaces_its_slot_only() {
        let mut p = PickedUp::default();
        let class = ClassLoadout {
            primary: 2,
            primary_att1: "acog".into(),
            secondary: 30,
            ..Default::default()
        };
        assert_eq!(p.overlay(Some(&class)), Some(class.clone()));
        assert_eq!(p.overlay(None), None);
        p.slots[0] = Some(Held {
            gun: 21,
            ..Default::default()
        });
        let c = p.overlay(Some(&class)).unwrap();
        assert_eq!(
            (c.primary, c.primary_att1.as_str(), c.secondary),
            (21, "", 30)
        );
        // No kit at all: the pickup alone.
        assert_eq!(
            p.overlay(None).map(|c| (c.primary, c.secondary)),
            Some((21, 0))
        );
    }

    #[test]
    fn the_crosshair_finds_the_gun_at_the_feet() {
        let guns = [
            Ground {
                id: 1,
                gun: 2,
                pos: [0.0, -2.0, 0.0],
                yaw: 0.0,
            },
            Ground {
                id: 2,
                gun: 2,
                pos: [0.0, 2.0, 0.0],
                yaw: 0.0,
            },
        ];
        let to_bevy = crate::tracer::from_wow;
        let eye = Vec3::new(0.0, 1.6, 0.0);
        let feet = Vec3::ZERO;
        // WoW +y is Bevy -x: looking down at the second gun.
        let at = to_bevy([0.0, 2.0, 0.0]);
        let forward = (at - eye).normalize();
        assert_eq!(aimed_gun(&guns, eye, forward, feet, to_bevy), Some(2));
        // Looking up and away: none.
        assert_eq!(aimed_gun(&guns, eye, Vec3::Y, feet, to_bevy), None);
        // Too far from the feet.
        assert_eq!(
            aimed_gun(&guns, eye, forward, Vec3::new(10.0, 0.0, 0.0), to_bevy),
            None
        );
    }

    #[test]
    fn a_ground_gun_lies_along_its_yaw() {
        // Built pointing -Z; laid at WoW yaw 0 it points along WoW +x, which is Bevy -Z.
        let barrel = lying(0.0) * Vec3::NEG_Z;
        assert!(barrel.distance(crate::tracer::from_wow([1.0, 0.0, 0.0])) < 1e-5);
        let barrel = lying(std::f32::consts::FRAC_PI_2) * Vec3::NEG_Z;
        assert!(barrel.distance(crate::tracer::from_wow([0.0, 1.0, 0.0])) < 1e-5);
    }
}
