//! MW2's mantle and knife lunge on the client: the requests that ride on `CMSG_FUSION_STATE`
//! (`STATE_MANTLE`, `STATE_LUNGE`; the server checks and moves the player) and the mantle hint,
//! probed with down-rays against benilla's static-world pick (terrain, buildings, doodads).

use benilla_app::Objects;
use benilla_app::fusion::Stance;
use benilla_world::interact::WorldPick;
use benilla_world::view::WorldCamera;
use bevy::platform::collections::HashSet;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::fusion::Fusion;
use crate::mode::FpsState;

/// The ledge window the server climbs (`Fusion::kMantleMinRise`, `kMantleMaxRise`), in yards.
const MANTLE_MIN_RISE: f32 = 0.5;
const MANTLE_MAX_RISE: f32 = 1.8;
/// Where ahead the terrain is sampled, in yards, as the server samples it.
const MANTLE_REACH: [f32; 3] = [0.4, 0.8, 1.2];

/// The knife lunge's reach (`Fusion::LungeRange`): five times MW2's, 20 yards, 40 with Commando.
pub fn lunge_range(commando: bool) -> f32 {
    if commando { 40.0 } else { 20.0 }
}

/// Whether the unit under the crosshair is close enough to lunge at.
pub fn lunge_in_range(distance: Option<f32>, commando: bool) -> bool {
    distance.is_some_and(|d| d <= lunge_range(commando))
}

/// Whether the terrain ahead rises into the mantle window: the highest of the samples ahead,
/// over the feet, within 0.5-1.4 yards.
pub fn mantle_hint(feet_y: f32, ahead: [Option<f32>; 3]) -> bool {
    ahead
        .into_iter()
        .flatten()
        .map(|g| g - feet_y)
        .fold(None, |best: Option<f32>, r| {
            Some(best.map_or(r, |b| b.max(r)))
        })
        .is_some_and(|rise| (MANTLE_MIN_RISE..=MANTLE_MAX_RISE).contains(&rise))
}

/// Whether the HUD shows the mantle hint.
#[derive(Resource, Default)]
pub struct MantleHint {
    pub show: bool,
    next_probe: f32,
}

pub struct MovementPlugin;

impl Plugin for MovementPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MantleHint>()
            .init_resource::<MantleVault>()
            .add_systems(Update, (probe_mantle, read_vault).in_set(Mw2Set::Aim));
    }
}

/// World op 22, `[22, rise f32]`: the server put us up a ledge.
pub const OP_MANTLE: u8 = 22;

/// A vault in progress: when the server said it began (crate::mw2::viewmodel dips the gun).
#[derive(Resource, Debug, Default)]
pub struct MantleVault {
    pub at: Option<f32>,
}

/// Seconds a vault takes: the hands plant, the body goes over, the gun comes back up.
pub const VAULT_SECS: f32 = 0.6;

/// The gun's dip `since` seconds into a vault: how far down it goes (inches) and how far it tips
/// (degrees, nose down), out and back on a half sine.
pub fn vault_dip(since: f32) -> (f32, f32) {
    if !(0.0..VAULT_SECS).contains(&since) {
        return (0.0, 0.0);
    }
    let k = (std::f32::consts::PI * since / VAULT_SECS).sin();
    (4.0 * k, 12.0 * k)
}

fn read_vault(
    time: Res<Time>,
    mut msgs: MessageReader<benilla_app::fusion::FusionChannelReceived>,
    mut vault: ResMut<MantleVault>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
) {
    for benilla_app::fusion::FusionChannelReceived(m) in msgs.read() {
        if m.channel == benilla_app::fusion::CHANNEL_WORLD && m.body.first() == Some(&OP_MANTLE) {
            vault.at = Some(time.elapsed_secs());
            debug!("benilla-mw2: vault over a ledge");
            sfx.write(crate::mw2::sound::Sfx::new("gear_rattle_plr_mantle"));
        }
    }
}

/// Ten times a second, standing in MW2 mode: is there a terrain ledge in front?
fn probe_mantle(
    time: Res<Time>,
    state: Res<FpsState>,
    fusion: Res<Fusion>,
    mut hint: ResMut<MantleHint>,
    world: WorldPick,
    objects: Objects,
    bodies: Query<&GlobalTransform, Without<WorldCamera>>,
    cameras: Query<&GlobalTransform, With<WorldCamera>>,
) {
    let t = time.elapsed_secs();
    if t < hint.next_probe {
        return;
    }
    hint.next_probe = t + 0.1;
    hint.show = false;
    if !state.active || !fusion.enabled || fusion.stance != Stance::Stand {
        return;
    }
    let (Some(feet), Ok(cam)) = (
        objects
            .player()
            .and_then(|e| bodies.get(e).ok())
            .map(|g| g.translation()),
        cameras.single(),
    ) else {
        return;
    };
    let forward = cam.forward().as_vec3();
    let flat = Vec3::new(forward.x, 0.0, forward.z).normalize_or_zero();
    if flat == Vec3::ZERO {
        return;
    }
    // Down-rays against the whole static world (terrain, buildings, doodads), from above the
    // window's top, at the server's sample points.
    let top = MANTLE_MAX_RISE + 0.5;
    let ahead = MANTLE_REACH.map(|d| {
        let from = feet + flat * d + Vec3::Y * top;
        world
            .cast(Ray3d::new(from, Dir3::NEG_Y), &HashSet::new(), false)
            .into_iter()
            .map(|h| h.hit.distance)
            .fold(None, |acc: Option<f32>, d| {
                Some(acc.map_or(d, |a| a.min(d)))
            })
            .map(|d| from.y - d)
    });
    hint.show = mantle_hint(feet.y, ahead);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_vault_dips_the_gun_and_brings_it_back() {
        assert_eq!(vault_dip(-0.1), (0.0, 0.0)); // not yet
        let (down, tip) = vault_dip(VAULT_SECS / 2.0);
        assert!((down - 4.0).abs() < 1e-4 && (tip - 12.0).abs() < 1e-4); // the bottom
        assert_eq!(vault_dip(VAULT_SECS), (0.0, 0.0)); // back up
    }

    #[test]
    fn mantle_hint_window() {
        assert!(mantle_hint(0.0, [Some(0.0), Some(1.0), Some(1.0)]));
        assert!(!mantle_hint(0.0, [Some(0.2), Some(0.2), Some(0.2)])); // a step
        assert!(!mantle_hint(0.0, [Some(0.0), Some(2.0), Some(2.0)])); // a wall
        assert!(!mantle_hint(0.0, [None, None, None])); // nothing streamed
        assert!(mantle_hint(10.0, [None, Some(11.4), None])); // the top of the window
    }

    #[test]
    fn lunge_only_in_range() {
        // Five times MW2's reach, by request: 20 yards, 40 with Commando.
        assert!(lunge_in_range(Some(19.5), false));
        assert!(!lunge_in_range(Some(21.0), false));
        assert!(lunge_in_range(Some(39.0), true));
        assert!(!lunge_in_range(None, false));
    }
}
