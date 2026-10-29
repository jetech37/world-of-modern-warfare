//! MW2's own assets in the WoW client: the first-person gun and arms from the player's MW2
//! install, read with IW4L's asset layer. Off unless an install is configured (`MW2_PATH`, or
//! `IW4L_GAMES` as IW4L reads it); without one the layer keeps its plain HUD.

pub mod audio;
pub mod held;
pub mod load;
pub mod sound;
pub mod viewmodel;
pub mod wow_arms;

use std::sync::{Arc, Mutex};

use benilla_world::view::WorldCamera;
use bevy::prelude::*;

use crate::Mw2Set;
use crate::config::Loadout;
use crate::mode::FpsState;
use crate::weapon::Weapon;
use load::{LoadedWeapon, Mw2Library};

/// The weapon used when the class's own is missing from the install.
const FALLBACK_WEAPON: &str = "m4_mp";

/// A background job's slot.
type Job<T> = Arc<Mutex<Option<Result<T, String>>>>;

fn spawn_job<T: Send + 'static>(f: impl FnOnce() -> Result<T, String> + Send + 'static) -> Job<T> {
    let slot: Job<T> = Arc::new(Mutex::new(None));
    let out = slot.clone();
    std::thread::Builder::new()
        .name("mw2-assets".into())
        .spawn(move || {
            let result = f();
            if let Ok(mut s) = out.lock() {
                *s = Some(result);
            }
        })
        .expect("spawn the MW2 asset thread");
    slot
}

fn take<T>(job: &Job<T>) -> Option<Result<T, String>> {
    job.lock().ok().and_then(|mut s| s.take())
}

/// Where the MW2 assets stand.
#[derive(Resource, Default)]
pub enum Mw2Assets {
    /// No install configured.
    #[default]
    Off,
    Cataloguing(Job<Mw2Library>),
    Ready(Arc<Mw2Library>),
    Failed(String),
}

/// The viewmodel for the current loadout.
#[derive(Resource, Default)]
struct ViewmodelSlot {
    /// The weapon name it was built for.
    weapon: Option<String>,
    assembling: Option<Job<LoadedWeapon>>,
    root: Option<Entity>,
    /// Guns assembled before, by key: swapping back draws at once.
    cache: std::collections::HashMap<String, Arc<LoadedWeapon>>,
    /// The key the job in flight is for.
    assembling_key: Option<String>,
    /// A swap: the old gun is dropping until then (elapsed seconds).
    switch_at: Option<f32>,
    /// A gun has been up: the next comes up with MW2's quick raise, not the first draw.
    swapped: bool,
    /// Akimbo's left-hand gun, beside `root`.
    left: Option<Entity>,
}

/// How long the old gun's drop plays before a swap brings the new one up: its drop clip, at
/// most 0.6 s (none without a clip).
pub fn drop_secs(drop_clip: Option<f32>) -> f32 {
    drop_clip.map_or(0.0, |d| d.clamp(0.0, 0.6))
}

pub struct Mw2AssetsPlugin;

impl Plugin for Mw2AssetsPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins(sound::Mw2SoundPlugin)
            .init_resource::<Mw2Assets>()
            .init_resource::<ViewmodelSlot>()
            .init_resource::<wow_arms::WowArms>()
            .init_resource::<viewmodel::ViewLight>()
            .init_resource::<held::HeldWeapon>()
            .init_resource::<crate::hand_anim::HandAnimClips>()
            .add_systems(Startup, start_catalogue)
            .add_systems(
                Update,
                (
                    poll_catalogue,
                    keep_viewmodel,
                    wow_arms::gather,
                    viewmodel::update_view_light,
                    viewmodel::animate,
                    viewmodel::hide_gun_for_melee,
                    wow_arms::skin,
                    held::build_held,
                    held::place_held,
                )
                    .chain()
                    .in_set(Mw2Set::Hud),
            );
    }
}

fn start_catalogue(mut assets: ResMut<Mw2Assets>) {
    if std::env::var_os("MW2_PATH").is_none() && std::env::var_os("IW4L_GAMES").is_none() {
        info!("benilla-mw2: no MW2 install configured (MW2_PATH), using the plain HUD");
        return;
    }
    info!("benilla-mw2: reading the MW2 install");
    *assets = Mw2Assets::Cataloguing(spawn_job(Mw2Library::open));
}

fn poll_catalogue(mut assets: ResMut<Mw2Assets>) {
    let Mw2Assets::Cataloguing(job) = &*assets else {
        return;
    };
    match take(job) {
        Some(Ok(lib)) => {
            info!(
                "benilla-mw2: MW2 install read, {} weapons",
                lib.weapons.len()
            );
            *assets = Mw2Assets::Ready(Arc::new(lib));
        }
        Some(Err(e)) => {
            warn!("benilla-mw2: could not read the MW2 install: {e}");
            *assets = Mw2Assets::Failed(e);
        }
        None => {}
    }
}

/// The MW2 weapon of the grenade readied in a hand, if any: the kit's lethal or tactical.
fn readied_grenade(
    systems: &crate::action_systems::ActionSystems,
    classes: &crate::cac::Classes,
    catalog: &crate::catalog::Catalog,
) -> Option<String> {
    use crate::action_systems::Ability;
    let (which, _) = systems.grenade?;
    let class = classes.equipped_class()?;
    let id = if which == Ability::Lethal {
        class.lethal
    } else {
        class.tactical
    };
    catalog.gun(id).map(|g| g.name.clone())
}

/// Assemble the loadout's weapon off the main thread, then spawn it under the world camera;
/// rebuild it when the loadout changes or the camera is replaced.
#[allow(clippy::too_many_arguments)]
fn keep_viewmodel(
    mut commands: Commands,
    time: Res<Time>,
    state: Res<FpsState>,
    loadout: Res<Loadout>,
    assets: Res<Mw2Assets>,
    mut slot: ResMut<ViewmodelSlot>,
    mut weapon: ResMut<Weapon>,
    cameras: Query<Entity, With<WorldCamera>>,
    mut roots: Query<&mut viewmodel::Viewmodel>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut images: ResMut<Assets<Image>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    held_grenade: (
        Res<crate::action_systems::ActionSystems>,
        Res<crate::cac::Classes>,
        Res<crate::catalog::Catalog>,
        Res<crate::equipment::EquipState>,
    ),
) {
    let Mw2Assets::Ready(lib) = &*assets else {
        return;
    };
    if !state.in_world || loadout.class.is_none() {
        return;
    }
    // The camera went (a world change): build again under the new one. Checked before any spawn,
    // since a root spawned below only exists once this system's commands apply.
    if let Some(root) = slot.root
        && roots.get(root).is_err()
    {
        slot.switch_at = None;
        slot.root = None;
        if let Some(left) = slot.left.take() {
            commands.entity(left).despawn();
        }
        slot.weapon = None;
    }
    // A Create-a-Class gun in hand shows its attachments and camo (crate::cac).
    let mut base = loadout.mw2_weapon.clone();
    let (variant, camo, look) = match loadout.class_gun.as_ref().filter(|h| h.base == base) {
        Some(h) => (h.variant.clone(), h.camo_model.clone(), h.look.clone()),
        None => (base.clone(), None, None),
    };
    // A test probe (crate::probe): MW2_PROBE_GUN=<variant>[@<camo model>] shows that gun in hand
    // without saving a class.
    let (variant, camo) = match std::env::var("MW2_PROBE_GUN") {
        Ok(probe) => {
            let (v, c) = probe
                .split_once('@')
                .map_or((probe.as_str(), None), |(v, c)| (v, Some(c.to_owned())));
            base = format!("{}_mp", v.split('_').next().unwrap_or(v));
            (v.to_owned(), c)
        }
        Err(_) => (variant, camo),
    };
    // A grenade readied in a hand (crate::action_systems `grenade`): MW2's own first-person
    // model of it in hand (the gun dropped, the grenade raised), until it is thrown or put back.
    let (base, variant, camo, look) = match readied_grenade(&held_grenade.0, &held_grenade.1, &held_grenade.2) {
        Some(name) => (name.clone(), name, None, None),
        None => (base, variant, camo, look),
    };
    let want = format!(
        "{variant}|{}|{}",
        camo.as_deref().unwrap_or(""),
        look.as_deref().unwrap_or("")
    );
    let mut ready: Option<Arc<LoadedWeapon>> = None;
    let t = time.elapsed_secs();
    // A swap: MW2 drops the gun in hand before raising the next.
    if slot.weapon.as_deref() != Some(want.as_str())
        && slot.switch_at.is_none()
        && let Some(root) = slot.root
        && let Ok(mut vm) = roots.get_mut(root)
    {
        // Agility swaps quicker (crate::handling). A grenade just thrown has left the hand: the
        // throw ends where the drop would, so the gun comes straight back up.
        let just_thrown = slot
            .weapon
            .as_deref()
            .is_some_and(viewmodel::is_thrown_weapon)
            && held_grenade.3.thrown_at.is_some_and(|at| t - at < 1.5);
        let secs = if just_thrown {
            0.0
        } else {
            vm.drop_from = Some(t);
            drop_secs(vm.duration(load::anim::DROP)) * weapon.handling.swap
        };
        slot.switch_at = Some(t + secs);
        // Akimbo's left gun goes down with it.
        if let Some(left) = slot.left
            && let Ok(mut lvm) = roots.get_mut(left)
        {
            lvm.drop_from = Some(t);
        }
    }
    let dropping = slot.switch_at.is_some_and(|at| t < at);
    if slot.weapon.as_deref() != Some(want.as_str()) && !dropping {
        slot.switch_at = None;
        if let Some(root) = slot.root.take() {
            commands.entity(root).despawn();
        }
        if let Some(left) = slot.left.take() {
            commands.entity(left).despawn();
        }
        slot.weapon = Some(want.clone());
        slot.assembling = None;
        if let Some(cached) = slot.cache.get(&want) {
            ready = Some(cached.clone());
        } else {
            let lib = lib.clone();
            slot.assembling_key = Some(want);
            slot.assembling = Some(spawn_job(move || {
                match &look {
                    // The underbarrel weapon wears the rifle it hangs under.
                    Some(look) => {
                        let look_base = format!("{}_mp", look.split('_').next().unwrap_or(look));
                        lib.weapon_dressed(&variant, &base, look, &look_base, camo.as_deref())
                    }
                    None => lib.weapon_with(&variant, &base, camo.as_deref()),
                }
                .or_else(|_| lib.weapon(&base, FALLBACK_WEAPON))
            }));
        }
    }
    if let Some(job) = &slot.assembling {
        match take(job) {
            Some(Ok(loaded)) => {
                slot.assembling = None;
                let loaded = Arc::new(loaded);
                if let Some(key) = slot.assembling_key.take() {
                    slot.cache.insert(key, loaded.clone());
                }
                ready = Some(loaded);
            }
            Some(Err(e)) => {
                slot.assembling = None;
                warn!("benilla-mw2: {e}");
            }
            None => {}
        }
    }
    if let Some(loaded) = ready {
        let Some(camera) = viewmodel::world_camera(&cameras) else {
            slot.weapon = None;
            return;
        };
        match viewmodel::spawn(
            &mut commands,
            camera,
            loaded.clone(),
            &mut meshes,
            &mut images,
            &mut materials,
            time.elapsed_secs(),
            if slot.swapped {
                load::anim::RAISE
            } else {
                load::anim::FIRST_RAISE
            },
        ) {
            Ok(root) => {
                info!(
                    "benilla-mw2: viewmodel `{}` up, {} clips (idle {}, aim {})",
                    loaded.name,
                    loaded.clips.iter().filter(|c| c.is_some()).count(),
                    loaded
                        .clips
                        .get(load::anim::IDLE)
                        .is_some_and(Option::is_some),
                    loaded
                        .clips
                        .get(load::anim::ADS_UP)
                        .is_some_and(Option::is_some),
                );
                slot.root = Some(root);
                // A grenade in hand is no gun: the gun's own handling stays as it was.
                if held_grenade.0.grenade.is_some() {
                    slot.swapped = true;
                    return;
                }
                // Akimbo: the second gun, in the left hand.
                weapon.akimbo = loaded.akimbo();
                if let Some(left) = loaded.left_hand() {
                    match viewmodel::spawn(
                        &mut commands,
                        camera,
                        Arc::new(left),
                        &mut meshes,
                        &mut images,
                        &mut materials,
                        time.elapsed_secs(),
                        load::anim::RAISE,
                    ) {
                        Ok(l) => slot.left = Some(l),
                        Err(e) => warn!("benilla-mw2: left gun `{}`: {e}", loaded.name),
                    }
                }
                slot.swapped = true;
                weapon.timing = loaded.reload;
                weapon.ads_fov = loaded.ads_fov;
                weapon.spread_stats = loaded.spread;
                weapon.scope = loaded.scope.clone();
                if let Some(d) = loaded
                    .clips
                    .get(load::anim::RELOAD)
                    .and_then(Option::as_ref)
                    .map(|c| c.duration())
                {
                    weapon.reload_secs = d.clamp(0.5, 6.0);
                }
            }
            Err(e) => warn!("benilla-mw2: viewmodel `{}`: {e}", loaded.name),
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn a_swap_drops_before_it_raises() {
        use super::drop_secs;
        assert_eq!(drop_secs(Some(0.4)), 0.4);
        assert_eq!(drop_secs(Some(1.5)), 0.6); // a long drop is cut short
        assert_eq!(drop_secs(None), 0.0); // no clip: straight to the next gun
    }
}
