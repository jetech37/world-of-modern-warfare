//! Scoped aiming (MW2's sniper scopes and the thermal sight): once a gun with an overlay is
//! fully aimed, MW2 hides the gun and draws the scope's picture full screen, its window in the
//! middle at the screen's height and black to the sides, zoomed to the definition's field of
//! view.
//!
//! The thermal sight: MW2 renders the world in heat, cold scenery dark and bodies white-hot. This
//! layer cannot re-shade WoW's world, so it paints heat over it instead: the scope's view is
//! darkened to a cold grey-blue, and every living creature and player in sight is outlined
//! white-hot from its own model (crate::thermal). A wall between hides the heat, as in MW2.

use std::sync::Arc;

use bevy::asset::RenderAssetUsages;
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, TextureDimension, TextureFormat};

use benilla_ui::script::UiScript;
use benilla_world::view::WorldCamera;

use crate::Mw2Set;
use crate::mode::FpsState;
use crate::mw2::load::{RgbaImage, Scope};
use crate::weapon::Weapon;

/// Seconds of held breath, and of the gasp after running out (MW2's `player_breath_hold_time`,
/// `player_breath_gasp_time`).
pub const BREATH_SECS: f32 = 4.0;
pub const GASP_SECS: f32 = 2.0;
/// The scope's sway at rest, in degrees of yaw and pitch, and its period.
pub const SWAY_YAW: f32 = 0.55;
pub const SWAY_PITCH: f32 = 0.35;
pub const SWAY_PERIOD: f32 = 3.2;

/// Holding breath on a scope (MW2: Shift while scoped steadies it until the breath runs out,
/// then a gasp shakes it for a while).
#[derive(Resource, Debug, Clone, Copy, PartialEq)]
pub struct Breath {
    /// Breath left, 0 to `BREATH_SECS`.
    pub left: f32,
    pub holding: bool,
    /// Elapsed time the gasp ends.
    pub gasp_until: f32,
}

impl Default for Breath {
    fn default() -> Self {
        Self {
            left: BREATH_SECS,
            holding: false,
            gasp_until: -1.0,
        }
    }
}

/// What a breath step did, for its sound.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BreathEvent {
    In,
    Out,
    Gasp,
}

impl Breath {
    /// One frame: holding (Shift while scoped) spends breath and steadies the scope; letting go
    /// or running out ends it (running out gasps); breath returns while not held.
    pub fn step(&mut self, want: bool, scoped: bool, dt: f32, t: f32) -> Option<BreathEvent> {
        let can = scoped && t >= self.gasp_until && self.left > 0.0;
        let hold = want && can;
        let mut event = None;
        if hold && !self.holding {
            event = Some(BreathEvent::In);
        }
        if hold {
            self.left = (self.left - dt).max(0.0);
            if self.left == 0.0 {
                self.holding = false;
                self.gasp_until = t + GASP_SECS;
                return Some(BreathEvent::Gasp);
            }
        } else {
            if self.holding {
                event = Some(BreathEvent::Out);
            }
            self.left = (self.left + dt * 0.5).min(BREATH_SECS);
        }
        self.holding = hold;
        event
    }

    /// How much the scope sways: none holding breath, double while gasping, else 1.
    pub fn sway_scale(&self, t: f32) -> f32 {
        if self.holding {
            0.0
        } else if t < self.gasp_until {
            2.0
        } else {
            1.0
        }
    }
}

/// The scope's sway at time `t`, (yaw, pitch) in degrees: MW2's slow figure-8.
pub fn sway(t: f32, scale: f32) -> (f32, f32) {
    let w = std::f32::consts::TAU / SWAY_PERIOD;
    (
        SWAY_YAW * scale * (w * t).sin(),
        SWAY_PITCH * scale * (2.0 * w * t).sin(),
    )
}

/// Aimed this far, the scope is up (MW2 shows the overlay at the end of the ADS move).
pub const SCOPED_AT: f32 = 0.95;

/// Whether the scope's overlay is up.
pub fn scoped(has_scope: bool, ads: f32) -> bool {
    has_scope && ads >= SCOPED_AT
}

#[derive(Component)]
struct ScopeRoot;

#[derive(Component)]
struct ScopeImage;

#[derive(Component)]
struct ThermalTint;

#[derive(Component)]
struct ThermalOverlay;

/// The overlay picture uploaded for the current scope.
#[derive(Resource, Default)]
struct ScopeTexture {
    for_image: Option<Arc<RgbaImage>>,
    handle: Option<Handle<Image>>,
}

pub struct ScopePlugin;

impl Plugin for ScopePlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ScopeTexture>()
            .add_systems(Startup, spawn_scope)
            .init_resource::<Breath>()
            .add_systems(
                Update,
                scope_sway
                    .in_set(Mw2Set::Aim)
                    .before(crate::aim::update_aim),
            )
            .add_systems(
                Update,
                (show_scope, hide_interface).chain().in_set(Mw2Set::Hud),
            );
    }
}

fn spawn_scope(mut commands: Commands, rig: Res<crate::thermal::ThermalRig>) {
    commands
        .spawn((
            Name::new("mw2 scope"),
            ScopeRoot,
            Node {
                position_type: PositionType::Absolute,
                width: percent(100),
                height: percent(100),
                flex_direction: FlexDirection::Row,
                ..default()
            },
            // Under the HUD (ammo, hitmarkers), over the world and WoW's frames.
            GlobalZIndex(i32::MAX - 2),
            Visibility::Hidden,
            Pickable::IGNORE,
        ))
        .with_children(|root| {
            root.spawn((
                ThermalTint,
                Node {
                    position_type: PositionType::Absolute,
                    width: percent(100),
                    height: percent(100),
                    ..default()
                },
                // Cold scenery: the world darkened to a dull grey-blue.
                BackgroundColor(Color::srgba(0.08, 0.11, 0.16, 0.72)),
                Visibility::Hidden,
                Pickable::IGNORE,
            ));
            // The heat: the thermal camera's picture of the bodies, outlined (crate::thermal).
            root.spawn((
                ThermalOverlay,
                Node {
                    position_type: PositionType::Absolute,
                    width: percent(100),
                    height: percent(100),
                    ..default()
                },
                bevy::ui_render::ui_material::MaterialNode(rig.material.clone()),
                Visibility::Hidden,
                Pickable::IGNORE,
            ));
            let side = || {
                (
                    Node {
                        flex_grow: 1.0,
                        height: percent(100),
                        ..default()
                    },
                    BackgroundColor(Color::BLACK),
                    Pickable::IGNORE,
                )
            };
            root.spawn(side());
            root.spawn((
                ScopeImage,
                Node {
                    height: percent(100),
                    aspect_ratio: Some(1.0),
                    ..default()
                },
                ImageNode::default(),
                Pickable::IGNORE,
            ));
            root.spawn(side());
        });
}

/// An RGBA8 picture from the install as a texture.
pub fn upload(img: &RgbaImage) -> Image {
    Image::new(
        Extent3d {
            width: img.width,
            height: img.height,
            depth_or_array_layers: 1,
        },
        TextureDimension::D2,
        img.pixels.clone(),
        TextureFormat::Rgba8UnormSrgb,
        RenderAssetUsages::RENDER_WORLD,
    )
}

#[allow(clippy::type_complexity)]
fn show_scope(
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    mut texture: ResMut<ScopeTexture>,
    mut images: ResMut<Assets<Image>>,
    mut roots: Query<&mut Visibility, With<ScopeRoot>>,
    mut tints: Query<
        &mut Visibility,
        (
            Or<(With<ThermalTint>, With<ThermalOverlay>)>,
            Without<ScopeRoot>,
        ),
    >,
    mut overlay: Query<(&mut ImageNode, &mut Node), With<ScopeImage>>,
) {
    let scope: Option<&Scope> = weapon.scope.as_deref();
    let up = state.active && scoped(scope.is_some(), weapon.ads);
    for mut v in &mut roots {
        let want = if up {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
    let Some(scope) = scope else {
        return;
    };
    for mut v in &mut tints {
        let want = if scope.thermal {
            Visibility::Inherited
        } else {
            Visibility::Hidden
        };
        if *v != want {
            *v = want;
        }
    }
    // Upload the overlay once per scope.
    let same = match (&texture.for_image, &scope.image) {
        (Some(a), Some(b)) => Arc::ptr_eq(a, b),
        (None, None) => true,
        _ => false,
    };
    if !same {
        texture.for_image = scope.image.clone();
        texture.handle = scope.image.as_deref().map(|img| images.add(upload(img)));
        for (mut node, mut layout) in &mut overlay {
            node.image = texture.handle.clone().unwrap_or_default();
            // MW2 sizes the overlay in its 480-high screen: a 480x480 scope fills the height.
            layout.height = percent((scope.height / 480.0 * 100.0).clamp(10.0, 100.0));
            layout.aspect_ratio = Some((scope.width / scope.height.max(1.0)).clamp(0.2, 5.0));
        }
    }
}

/// The Lua that hides or restores WoW's interface as the scope comes up or goes down: MW2 shows
/// nothing but the world through a scope, and WoW's frames inside the lens (quest pins, chat,
/// nameplates) would draw through it. Restores only what it hid.
pub fn interface_call(hidden: bool, scoped: bool) -> Option<&'static str> {
    match (hidden, scoped) {
        (false, true) => Some("UIParent:Hide()"),
        (true, false) => Some("UIParent:Show()"),
        _ => None,
    }
}

fn hide_interface(
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    script: Option<NonSend<UiScript>>,
    mut hidden: Local<bool>,
) {
    let up = state.active && scoped(weapon.scope.is_some(), weapon.ads);
    let Some(script) = script else {
        return;
    };
    if let Some(call) = interface_call(*hidden, up) {
        crate::lua::run(&script, call);
        info!("benilla-mw2: {call}");
        *hidden = up;
    }
}

/// A scoped sniper's sway, before the aim is taken so shots go where the reticle is; Shift holds
/// breath to steady it.
fn scope_sway(
    time: Res<Time>,
    state: Res<FpsState>,
    weapon: Res<Weapon>,
    input: Res<crate::input::Mw2Input>,
    mut breath: ResMut<Breath>,
    mut cameras: Query<&mut Transform, With<WorldCamera>>,
    mut sfx: MessageWriter<crate::mw2::sound::Sfx>,
) {
    let t = time.elapsed_secs();
    let up = state.active && scoped(weapon.scope.is_some(), weapon.ads);
    // Shift is sprint's key; scoped there is no sprint, so it holds breath.
    let held = input.sprint;
    if let Some(event) = breath.step(held, up, time.delta_secs(), t) {
        let alias = match event {
            BreathEvent::In => "weap_sniper_breathin",
            BreathEvent::Out => "weap_sniper_breathout",
            BreathEvent::Gasp => "weap_sniper_breathgasp",
        };
        sfx.write(crate::mw2::sound::Sfx::new(alias));
    }
    if !up {
        return;
    }
    // Ranged attack power steadies it (crate::handling).
    let (yaw, pitch) = sway(t, breath.sway_scale(t) * weapon.handling.sway);
    for mut tf in &mut cameras {
        tf.rotate_y(yaw.to_radians());
        tf.rotate_local_x(pitch.to_radians());
    }
}

#[cfg(test)]
mod tests {

    #[test]
    fn the_scope_systems_have_no_conflicting_queries() {
        let mut world = World::new();
        let mut schedule = Schedule::default();
        schedule.add_systems((show_scope, scope_sway, hide_interface));
        schedule
            .initialize(&mut world)
            .expect("the systems initialise");
    }
    use super::*;

    #[test]
    fn the_scope_is_up_once_fully_aimed() {
        assert!(!scoped(true, 0.5));
        assert!(scoped(true, 1.0));
        assert!(!scoped(false, 1.0)); // iron sights and optics keep the gun
    }

    #[test]
    fn holding_breath_steadies_then_gasps() {
        let mut b = Breath::default();
        assert_eq!(b.step(true, true, 0.1, 0.0), Some(BreathEvent::In));
        assert_eq!(b.sway_scale(0.0), 0.0);
        let mut t = 0.1;
        let mut gasped = false;
        while t < 5.0 {
            if b.step(true, true, 0.1, t) == Some(BreathEvent::Gasp) {
                gasped = true;
                break;
            }
            t += 0.1;
        }
        assert!(gasped && (t - BREATH_SECS).abs() < 0.2); // four seconds of breath
        assert_eq!(b.sway_scale(t + 0.5), 2.0); // gasping shakes
        assert_eq!(b.step(true, true, 0.1, t + 0.5), None); // no new breath while gasping
        assert_eq!(b.sway_scale(t + GASP_SECS + 0.1), 1.0);
    }

    #[test]
    fn letting_go_breathes_out_and_unscoped_breath_is_not_held() {
        let mut b = Breath::default();
        b.step(true, true, 0.1, 0.0);
        assert_eq!(b.step(false, true, 0.1, 0.1), Some(BreathEvent::Out));
        let mut c = Breath::default();
        assert_eq!(c.step(true, false, 0.1, 0.0), None); // Shift unscoped is sprint
        assert!(!c.holding);
    }

    #[test]
    fn the_sway_is_a_small_figure_eight() {
        let (y0, p0) = sway(0.0, 1.0);
        assert_eq!((y0, p0), (0.0, 0.0));
        let (y, p) = sway(SWAY_PERIOD / 4.0, 1.0);
        assert!((y - SWAY_YAW).abs() < 1e-4 && p.abs() < 1e-4);
        assert_eq!(sway(1.3, 0.0), (0.0, 0.0));
    }

    #[test]
    fn the_interface_hides_behind_a_scope_and_comes_back() {
        assert_eq!(interface_call(false, true), Some("UIParent:Hide()"));
        assert_eq!(interface_call(true, true), None); // once, not every frame
        assert_eq!(interface_call(true, false), Some("UIParent:Show()"));
        assert_eq!(interface_call(false, false), None); // never shown over the player's choice
    }
}
