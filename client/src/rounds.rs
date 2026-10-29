//! Enchanted rounds (design doc section 12, Enchanting): an enchanter's consumable loads a special
//! round into the gun in hand for a few magazines or seconds; the server applies its effect to
//! every hit (`fusion/Rounds.h`) and tells the client what is loaded (world channel op 64). The
//! HUD shows a tag in the round's colour over the ammunition, our own tracers take the colour,
//! and frost rounds cycle the gun slower (`crate::fusion`'s fire rate).

use bevy::prelude::*;

use benilla_app::fusion::{CHANNEL_WORLD, FusionChannelReceived};

use crate::Mw2Set;
use crate::catalog::Catalog;
use crate::config::Loadout;
use crate::mode::FpsState;

pub const OP_LOADED: u8 = 64;

/// The rounds by the server's `Rounds::Round`: the HUD's tag, the tracer's colour (0xRRGGBB) and
/// the gun's fire time multiplier (`Rounds::Def`).
pub const ROUNDS: [(&str, u32, f32); 6] = [
    ("", 0xFFFFFF, 1.0),
    ("INCENDIARY", 0xFF7A1F, 1.0),
    ("FROST", 0x7FD4FF, 1.12),
    ("ARMOR-PIERCING", 0xC8D0E0, 1.0),
    ("ARCANE", 0xB04CFF, 1.0),
    ("HOLY", 0xFFE680, 1.0),
];

/// The load in a gun, as the server last said.
#[derive(Resource, Debug, Default, Clone, PartialEq)]
pub struct EnchantedRounds {
    pub round: u8,
    pub gun: u16,
    pub timed: bool,
    /// Rounds left (by magazines).
    pub left: u32,
    /// A timed load ends then (the clock's elapsed seconds).
    pub until: f32,
}

/// Op 64: `[64, round u8, gun u16, timed u8, left u32]`, `left` rounds or milliseconds.
pub fn decode_loaded(b: &[u8]) -> Option<(u8, u16, bool, u32)> {
    if b.len() != 9 || b[0] != OP_LOADED || usize::from(b[1]) >= ROUNDS.len() {
        return None;
    }
    Some((
        b[1],
        u16::from_le_bytes([b[2], b[3]]),
        b[4] != 0,
        u32::from_le_bytes([b[5], b[6], b[7], b[8]]),
    ))
}

impl EnchantedRounds {
    /// Take the server's word at time `t`.
    pub fn set(&mut self, (round, gun, timed, left): (u8, u16, bool, u32), t: f32) {
        *self = Self {
            round,
            gun,
            timed,
            left: if timed { 0 } else { left },
            until: if timed { t + left as f32 / 1000.0 } else { 0.0 },
        };
    }

    /// Loaded in `gun` and still going at `t`.
    pub fn active_for(&self, gun: u16, t: f32) -> bool {
        self.round != 0 && self.gun == gun && (!self.timed || t < self.until)
    }

    /// How much slower the gun cycles with this load (frost), 1 otherwise.
    pub fn fire_time_multiplier(&self, gun: u16, t: f32) -> f32 {
        if self.active_for(gun, t) {
            ROUNDS[usize::from(self.round)].2
        } else {
            1.0
        }
    }

    /// The tracer's colour for our own rounds from `gun`.
    pub fn tracer(&self, gun: u16, t: f32) -> Option<Color> {
        self.active_for(gun, t).then(|| {
            let rgb = ROUNDS[usize::from(self.round)].1;
            Color::srgb_u8((rgb >> 16) as u8, (rgb >> 8) as u8, rgb as u8)
        })
    }

    /// The HUD's tag: "INCENDIARY  2 MAGS", "ARCANE  0:32".
    pub fn label(&self, gun: u16, mag_size: u32, t: f32) -> String {
        if !self.active_for(gun, t) {
            return String::new();
        }
        let tag = ROUNDS[usize::from(self.round)].0;
        if self.timed {
            let s = (self.until - t).max(0.0).ceil() as u32;
            format!("{tag}  {}:{:02}", s / 60, s % 60)
        } else {
            let mags = self.left.div_ceil(mag_size.max(1));
            format!("{tag}  {mags} MAG{}", if mags == 1 { "" } else { "S" })
        }
    }
}

/// Our own tracers' material while rounds are loaded in the gun in hand (`crate::tracer`).
#[derive(Resource, Default)]
pub struct RoundTracer(pub Option<Handle<StandardMaterial>>);

#[derive(Component)]
struct RoundTag;

pub struct RoundsPlugin;

impl Plugin for RoundsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<EnchantedRounds>()
            .init_resource::<RoundTracer>()
            .add_systems(Startup, spawn_tag)
            .add_systems(
                Update,
                (read_world, tracer_material, dock_tag, show_tag)
                    .chain()
                    .in_set(Mw2Set::Weapon),
            );
    }
}

/// Seat the rounds tag at the top of the gun HUD's column (crate::hud), over the ammunition, so
/// it moves with it clear of the bag bar.
fn dock_tag(
    mut commands: Commands,
    tags: Query<Entity, (With<RoundTag>, Without<ChildOf>)>,
    column: Query<Entity, With<crate::hud::AmmoColumn>>,
) {
    let Ok(column) = column.single() else {
        return;
    };
    for tag in &tags {
        commands
            .entity(tag)
            .insert(Node::default())
            .remove::<GlobalZIndex>();
        commands.entity(column).insert_children(0, &[tag]);
    }
}

fn read_world(
    mut msgs: MessageReader<FusionChannelReceived>,
    mut rounds: ResMut<EnchantedRounds>,
    time: Res<Time>,
) {
    for FusionChannelReceived(m) in msgs.read() {
        if m.channel != CHANNEL_WORLD {
            continue;
        }
        if let Some(l) = decode_loaded(&m.body) {
            rounds.set(l, time.elapsed_secs());
        }
    }
}

/// The gun in hand's catalog id.
fn held_gun(catalog: &Catalog, loadout: &Loadout) -> u16 {
    catalog.gun_named(&loadout.mw2_weapon).map_or(0, |g| g.id)
}

fn tracer_material(
    time: Res<Time>,
    rounds: Res<EnchantedRounds>,
    catalog: Res<Catalog>,
    loadout: Res<Loadout>,
    mut tracer: ResMut<RoundTracer>,
    materials: Option<ResMut<Assets<StandardMaterial>>>,
    mut last: Local<Option<Color>>,
) {
    let want = rounds.tracer(held_gun(&catalog, &loadout), time.elapsed_secs());
    if want == *last {
        return;
    }
    *last = want;
    tracer.0 = match (want, materials) {
        (Some(c), Some(mut materials)) => Some(materials.add(StandardMaterial {
            base_color: c.with_alpha(0.85),
            unlit: true,
            alpha_mode: AlphaMode::Add,
            ..default()
        })),
        _ => None,
    };
}

fn spawn_tag(mut commands: Commands, assets: Res<AssetServer>) {
    commands.spawn((
        Name::new("mw2 enchanted rounds tag"),
        RoundTag,
        Node {
            position_type: PositionType::Absolute,
            right: px(36),
            bottom: px(118),
            ..default()
        },
        Text::new(""),
        crate::hud::FontUnits(12.0),
        TextFont::from_font_size(15.0).with_font(assets.load(crate::wow_ui::FONT)),
        TextColor(Color::WHITE),
        TextShadow {
            offset: Vec2::splat(1.5),
            color: Color::srgba(0.0, 0.0, 0.0, 0.85),
        },
        GlobalZIndex(i32::MAX - 1),
        Pickable::IGNORE,
    ));
}

fn show_tag(
    time: Res<Time>,
    state: Res<FpsState>,
    rounds: Res<EnchantedRounds>,
    catalog: Res<Catalog>,
    loadout: Res<Loadout>,
    systems: Res<crate::action_systems::ActionSystems>,
    mut tags: Query<(&mut Text, &mut TextColor), With<RoundTag>>,
) {
    let t = time.elapsed_secs();
    let gun = held_gun(&catalog, &loadout);
    // The gun's rounds are Modern's: the gun is away in the other systems.
    let s = if state.active && systems.active == crate::action_systems::System::Modern {
        rounds.label(gun, loadout.mag_size, t)
    } else {
        String::new()
    };
    let colour = rounds.tracer(gun, t).unwrap_or(Color::WHITE);
    for (mut text, mut c) in &mut tags {
        if text.0 != s {
            text.0 = s.clone();
        }
        if c.0 != colour {
            c.0 = colour;
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
    fn the_server_message_decodes() {
        // The server's RoundTests vectors: frost in gun 0x0102, 90 rounds; none.
        assert_eq!(
            decode_loaded(&hex("40020201005a000000")),
            Some((2, 0x0102, false, 90))
        );
        assert_eq!(
            decode_loaded(&hex("400000000000000000")),
            Some((0, 0, false, 0))
        );
        assert_eq!(decode_loaded(&[OP_LOADED, 9, 0, 0, 0, 0, 0, 0, 0]), None);
    }

    #[test]
    fn a_load_shows_its_magazines_or_its_time() {
        let mut r = EnchantedRounds::default();
        r.set((1, 2, false, 61), 0.0);
        assert_eq!(r.label(2, 30, 5.0), "INCENDIARY  3 MAGS");
        assert_eq!(r.label(30, 30, 5.0), ""); // another gun in hand
        assert!(r.tracer(2, 0.0).is_some() && r.tracer(30, 0.0).is_none());
        r.set((4, 2, true, 45_000), 10.0);
        assert_eq!(r.label(2, 30, 23.5), "ARCANE  0:32");
        assert!(!r.active_for(2, 56.0));
        r.set((2, 2, false, 90), 0.0);
        assert!((r.fire_time_multiplier(2, 0.0) - 1.12).abs() < 1e-6);
        assert_eq!(r.fire_time_multiplier(3, 0.0), 1.0);
    }
}
